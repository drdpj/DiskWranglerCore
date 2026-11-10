/*
 * Filesystem interface - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
 * Copyright (C) 2026 Daniel Jameson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

// The filesystem interface (docs/design.md, "Filesystem interface"). QtCore only.
//
// A Volume is one filesystem on one device. It lists, reads and changes files by Path and describes its own
// attributes (AttrSpec), so a front end or a command-line tool never needs to know which filesystem it shows.
// A Disc (disc.h) is an opened image: one container plus one or more mounted volumes.
//
// The first volumes are adapters over each program's existing filesystem classes (ADFS, DFS, FAT, CP/M); their
// Path resolves to whatever the class takes (a directory sector, a cluster, a user number and name).

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

#include <diskwrangler/xfer/transferitem.h>

namespace dw {
namespace fs {

// A node, as the names from the volume's root down. The root is the empty path. Stable across reloads.
struct Path {
    QStringList parts;
    Path() = default;
    Path(const QStringList &p) : parts(p) {}                         // NOLINT: a list of names is a path
    static Path fromString(const QString &s, QChar sep = '/');        // "a/b" -> {a, b}; "" and "/" -> root
    bool isRoot() const { return parts.isEmpty(); }
    QString name() const { return parts.isEmpty() ? QString() : parts.last(); }
    Path parent() const;
    Path child(const QString &n) const;
    QString toString(QChar sep = '/') const { return parts.join(sep); }
    bool operator==(const Path &o) const { return parts == o.parts; }
    bool operator!=(const Path &o) const { return parts != o.parts; }
};

// Namespace: a level that isn't a directory on the disc (a CP/M user area). DFS directory letters are not levels:
// they stay part of the name ("A.BOOT"), and a copy's destination decides what they become (see placeIncoming).
enum class NodeKind { File, Dir, Namespace };

typedef QVariantMap AttrValues;          // keyed by AttrSpec::key

struct Entry {
    QString name;
    NodeKind kind = NodeKind::File;
    qint64 size = 0;
    bool sizeExact = true;               // false: the filesystem only knows it to a record (CP/M 2.2)
    QDateTime modified, created, accessed;   // invalid = none
    AttrValues attrs;
    QString problem;                     // something wrong with it: shown, and the entry is read-only
    bool isDir() const { return kind != NodeKind::File; }
};

// One attribute of a file (or of the volume). It drives columns, the Properties dialog and retro-files.
struct AttrSpec {
    enum Type { Bool, Hex, Int, Choice, Text, Date };
    QString key, label;                  // "load", "Load"
    Type type = Text;
    QStringList choices;                 // Choice: the values, in order (the value stored is the index)
    int bits = 32;                       // Hex: width (a DFS address has 18 bits)
    bool editable = true;
    bool column = true;                  // shown as a column in a file list
    bool dirs = false;                   // directories have it too
    QString sample;                      // column width hint
    QString letter;                      // one letter for a text listing ("L" locked, "R" read-only); empty = none.
                                         // Only Bool attributes shown as columns are listed by it (command-line `ls`)
    QString retroFamily, retroKey;       // retro-files: "acorn", "load"; empty = not carried
};

struct Caps {
    bool dirs = false;                   // real directories
    bool namespaces = false;             // levels that aren't directories (CP/M users)
    bool rename = true, renameAcrossDirs = false;
    bool title = false;                  // the volume has a title or label
    bool dates = false, settableDates = false;
    bool contiguousOnly = false;         // a file needs one free run (DFS)
    int maxEntriesPerDir = -1;           // DFS 31 or 62, ADFS 47, CP/M the directory size; -1 = no fixed limit
};

struct VolumeInfo {
    QString title;
    qint64 total = 0, free = 0;          // bytes
    qint64 largestFree = -1;             // bytes in the largest free run, -1 = not meaningful
    int entriesFree = -1;                // directory slots left, -1 = not meaningful
};

// The result of a check: errors make a volume read-only and veto a save; warnings and info are shown.
struct Report {
    QStringList errors, warnings, info;
    bool ok() const { return errors.isEmpty(); }
};

class NameRules {
public:
    virtual ~NameRules() = default;
    virtual bool valid(const QString &name, NodeKind kind, QString *why) const = 0;
    virtual QString fromHost(const QString &hostName, NodeKind kind) const = 0;   // a valid name made from any name
    virtual Qt::CaseSensitivity caseSensitivity() const { return Qt::CaseInsensitive; }
};

// Where an incoming item goes in a volume: the directory (made on demand where it doesn't exist yet), the name, and
// anything to tell the user about the choice.
struct Placement {
    Path dir;
    QString name;
    QString warning;
};

class Volume {
public:
    virtual ~Volume() = default;

    virtual QString fsId() const = 0;                       // "adfs", "dfs", "fat", "cpm"
    virtual Caps caps() const = 0;
    virtual QVector<AttrSpec> fileAttrs() const = 0;
    virtual QVector<AttrSpec> volumeAttrs() const { return {}; }     // title, boot option, label ...
    // The order in which a text listing shows the attribute letters, as AttrSpec keys; empty (the default) = the order
    // of fileAttrs(). A volume that lists its attributes in one order and shows them in another (ADFS: R W L E in the
    // specs, L W R E as *INFO prints them) overrides this and leaves fileAttrs() as it is. Keys not named come after.
    virtual QStringList attrLetterOrder() const { return {}; }
    // Whether a text listing marks a size that is only known to a record with "~". CP/M shows the plain number, as its
    // own program does; default: marked.
    virtual bool marksInexactSizes() const { return true; }
    virtual AttrValues volumeValues() const { return {}; }
    virtual const NameRules &names() const = 0;
    virtual VolumeInfo info() const = 0;

    // Path text, as a command line or a script writes it (docs/cli.md, "Volumes and paths"). The defaults split on
    // "/" (a leading one optional), the empty string and "/" being the root, and show "/a/b"; a filesystem with its own syntax overrides both. parsePath only
    // reads the syntax: it neither checks that the names exist nor rejects wildcard characters. displayPath is the
    // text parsePath reads back to the same Path.
    virtual bool parsePath(const QString &text, Path *out, QString *err) const;
    virtual QString displayPath(const Path &p) const;
    // What to call an entry on the host when that isn't its name (CP/M in a national character set). Empty = its name.
    virtual QString hostNameFor(const Entry &) const { return QString(); }

    virtual bool list(const Path &dir, QVector<Entry> *out, QString *err) const = 0;
    virtual bool stat(const Path &node, Entry *out, QString *err) const;     // by default: list the parent, find it
    virtual bool read(const Path &file, QByteArray *out, QString *err) const = 0;

    // Changes. A Disc wraps each in a transaction: on failure the container is restored and reload() called, so a
    // volume needs no rollback code of its own (the adapters' classes still have theirs).
    virtual bool write(const Path &dir, const QString &name, const QByteArray &data, const AttrValues &attrs,
                       const QDateTime &when, bool replace, QString *err) = 0;
    virtual bool mkdir(const Path &dir, const QString &name, QString *err);       // default: no directories
    virtual bool remove(const Path &node, QString *err) = 0;                       // directories with their contents
    virtual bool rename(const Path &from, const Path &to, QString *err);           // default: not supported
    virtual bool setAttrs(const Path &node, const AttrValues &a, QString *err) = 0;   // only the keys given change
    virtual bool setVolumeAttrs(const AttrValues &a, QString *err);                // default: none

    virtual Report check() const = 0;         // also what vetoes a save
    virtual bool reload(QString *err) = 0;    // read everything cached again from the (staged) disc

    // retro-files. toRetro: an entry's attributes as the document's families; fromRetro: the attributes to give an
    // incoming item. By default each AttrSpec with a retroFamily maps its value straight to retroKey (booleans as
    // JSON booleans, Hex as 8 hex digits, the rest as they are); a filesystem whose attributes don't map one to one
    // (Acorn's "access" letters) overrides them.
    virtual QMap<QString, QJsonObject> toRetro(const Entry &e) const;
    // The same for an entry listed in dir, for a filesystem whose attributes depend on where the entry is (CP/M's user
    // number). What extraction and copying use; by default toRetro(e).
    virtual QMap<QString, QJsonObject> toRetroIn(const Path &, const Entry &e) const { return toRetro(e); }
    virtual AttrValues fromRetro(const QMap<QString, QJsonObject> &families, NodeKind kind) const;

    // Where an item copied in (from any filesystem) goes when dropped on targetDir. By default: there, with the
    // name made valid by names().fromHost. (ADFS puts DFS directory letters in subdirectories.)
    virtual Placement placeIncoming(const TransferItem &item, const Path &targetDir) const;
};

// The default mapping on its own, for volumes that override toRetro / fromRetro and still want it for some keys.
QMap<QString, QJsonObject> retroFromSpecs(const QVector<AttrSpec> &specs, const AttrValues &values, bool isDir);
AttrValues specsFromRetro(const QVector<AttrSpec> &specs, const QMap<QString, QJsonObject> &families, bool isDir);

} // namespace fs
} // namespace dw
