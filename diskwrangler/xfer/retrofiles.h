/*
 * Retro-files - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// The retro-files drag format (docs/retro-files-format.md) in both directions: writing items to a host folder
// together with the JSON document that describes them, and reading such a document and the files it lists. What
// differs between the programs - how names are made, sidecar files, content transforms, folder layout - is a
// HostPolicy that each program supplies. QtCore only.

#include <QByteArray>
#include <QJsonArray>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <diskwrangler/xfer/transferitem.h>

namespace dw {
namespace xfer {

extern const char *const RetroFilesMime;        // "application/x-retro-files+json"

// The names taken in one host folder. Compared ignoring case, because Windows and macOS folders are
// case-insensitive, unless the host is known to be case sensitive.
class UsedNames {
public:
    explicit UsedNames(bool caseSensitive = false) : m_sensitive(caseSensitive) {}
    bool has(const QString &n) const { return m_names.contains(key(n)); }
    void add(const QString &n) { m_names.insert(key(n)); }
    // The name if it is free, else name_2, name_3 ... (before the extension when keepExtension and there is one); taken.
    QString unique(const QString &name, bool keepExtension);
private:
    QString key(const QString &n) const { return m_sensitive ? n : n.toLower(); }
    bool m_sensitive;
    QSet<QString> m_names;
};

// How one program's items become host files. The defaults suit a plain list of files; a program overrides what
// differs.
class HostPolicy {
public:
    virtual ~HostPolicy() = default;
    // The host file name for an item, before it is made unique: its hostName or name, made safe (hostSafeName).
    virtual QString hostName(const TransferItem &it) const;
    // Is the extension kept apart when a clash adds _2? (FAT names: "AUTOEXEC_2.BAT")
    virtual bool keepExtension(const TransferItem &) const { return false; }
    // The "name" the document lists the item under.
    virtual QString listedName(const TransferItem &it) const { return it.name; }
    // A folder (under the host folder) that a top-level item goes into; empty for the host folder itself.
    virtual QString topFolder(const TransferItem &) const { return QString(); }
    // The bytes to write (CP/M: with the ^Z padding trimmed).
    virtual QByteArray content(const TransferItem &it) const { return it.data; }
    // What error messages call the item.
    virtual QString errorLabel(const TransferItem &it) const { return listedName(it); }
    // A program that must work out all the top-level names together can return true and fill *names (one per
    // item, in order); the host folder exists when this is called. Otherwise each name is made unique as its file
    // is written, in order.
    virtual bool planNames(const QVector<TransferItem> &items, const QString &hostDir, QVector<QString> *names) const;
    // Called after a file is written: for a sidecar file (Acorn .inf). used is the folder's names, to be kept
    // unique; topLevel is null unless the file is a top-level item with no top folder, relDir is its folder.
    virtual void afterFile(const TransferItem &it, const QString &absDir, const QString &relDir, const QString &hostFile,
                           UsedNames &used, QStringList *topLevel, QStringList *errors) const;
};

// Writes items (files and folder trees) into hostDir: names from the policy, made unique within each folder, file
// dates set from TransferItem::when. The top-level paths (relative to hostDir) go to *topLevel, and one JSON item
// per file and folder to *jsonItems. Errors (e.g. a full disc) go to *errors; the rest is still written.
void writeHost(const QVector<TransferItem> &items, const QString &hostDir, const HostPolicy &policy,
               QStringList *topLevel, QJsonArray *jsonItems, QStringList *errors);

// The document for files written by writeHost. filesystem is the source filesystem ("adfs", "cpm"...), generator
// the program's name.
QByteArray makeJson(const QJsonArray &items, const QString &hostDir, const QString &imageName,
                    const QString &filesystem, const QString &generator);

// Reads a document (from any program) and the files it lists into a tree. Refuses a list whose paths leave the
// document's folder, or whose files are missing or not the size it says. Attributes of every family are kept as
// they are; the program turns them into its own.
bool readJson(const QByteArray &json, QVector<TransferItem> *out, QString *err);

// Temporary folders for drags: one per drag, named prefix + random letters, kept until the program exits (a drop
// target may still be copying when the drag returns). cleanStale() removes ones a crash left behind that are older
// than maxAgeSecs; returns how many.
QString newDragDir(const QString &prefix, QString *err);
int cleanStale(const QString &prefix, qint64 maxAgeSecs = 24 * 3600);

} // namespace xfer
} // namespace dw
