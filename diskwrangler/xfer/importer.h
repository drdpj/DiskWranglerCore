/*
 * Transfer layer - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// The Importer: one set of rules for putting items into a volume, for the command line (put, copy) and for the
// windows (Add files, drops, Paste). It works through the Volume interface only, inside the caller's transaction.
// QtCore only. (docs/cli.md, "Importing"; docs/design.md, "Amendments from step 5".)
//
// Two ways to run it:
// - The command line passes neither a conflict callback nor a directory hook, and onError = Stop: the options decide
//   (--replace, --skip, --force), the first failure ends the run, import() returns false and the caller abandons the
//   run (its transaction puts everything back). The messages are the command line's.
// - A window passes a conflict callback (its Replace / Replace all / Skip / Skip all / Cancel question), maybe a
//   directory hook, and onError = Continue: an item that fails is recorded in ImportResult::errors (with its source)
//   and the run goes on; import() then always returns true, and nothing that was done is undone. Cancel (from either
//   callback) ends the run with what was done so far kept: ImportResult::cancelled, and import() returns true.
//   The windows run it through dw::ui::ImportSession (ui/importsession.h), which also passes a guard per file
//   (Disc::transact) and, where a window's rules aren't the volume's, its own placement and attributes (step 5e).

#include <QJsonObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

#include <diskwrangler/fs/volume.h>
#include <diskwrangler/xfer/transferitem.h>

namespace dw {
namespace xfer {

struct ImportOptions {
    // Used only when no conflict callback is set: what to do with a file that exists (neither = a Conflict error), and
    // whether a locked or read-only file may be replaced (otherwise a Locked error).
    bool replace = false, skip = false, force = false;
    // DFS items arriving on ADFS: "dirs" or "flatten". The Importer doesn't read it; the caller sets the volume up
    // from it before the run (acorndw's Backend::configureTarget; the Acorn window's dfsLetterDirs setting).
    QString letters = "dirs";
    // A name made twice in one run (two host files that become the same disc name): the second is called NAME~1 (the
    // command line), or (false) it is a file that exists by then, so the conflict question or the options decide (the
    // CP/M window, as it always did).
    bool renameRepeats = true;
    // A file exists only under exactly the same name (the CP/M window: a CP/M directory compares the bytes, so on a disc
    // that allows lower case, readme.txt is not README.TXT). Otherwise the volume's own case rule
    // (NameRules::caseSensitivity) decides, as the command line does.
    bool exactNames = false;
    // A locked or read-only file being replaced (a Replace answer, or --force) is unlocked first, then written (the
    // default). false: written over as it is, for volumes whose write replaces such a file itself (the Acorn window,
    // step 5l: ADFS, DFS and FAT do, and on DFS every catalogue write counts the cycle number on, so an unlock first
    // would count it twice).
    bool unlockBeforeReplace = true;
};

struct Change {
    QString op;                          // add, replace, skip, remove, mkdir, rename, attr, title, boot, convert, new
    QString vol;                         // the mount's label
    QString path;                        // display path on the volume (or the file made)
    QString from;                        // the source, or empty
    QString note;
    QJsonObject json() const;
};

// What went wrong with one item. The kinds are the Importer's; the command line maps them to its exit codes
// (Failed 1, Conflict 5, Locked 6).
struct ImportError {
    enum Kind { None, Failed, Conflict, Locked };
    // Which check failed, for a program that words its own messages (step 5l: the Acorn window). Other: anything else
    // (a conflict without a callback, a locked file, a sentence).
    enum Cause { Other, DirInTheWay, FileInTheWay, MakeDir, Write };
    Kind kind = None;
    Cause cause = Other;                 // DirInTheWay: a directory where a file was to go; FileInTheWay: a file where a
                                         // directory was to go (or be made on the way); MakeDir, Write: the volume refused
    QString message;                     // "<display path>: <why>", or a whole sentence
    QString why;                         // the reason alone, as the volume gave it (the same as message for a sentence)
    QString path;                        // the display path on the volume concerned, or empty
    QString name;                        // the name concerned on the volume: the incoming item's, or (FileInTheWay,
                                         // MakeDir on the way to a file's place) the directory's; or empty
    QString from;                        // the item's source (froms[i], with "/kid" for a folder's contents)
    bool failed() const { return kind != None; }
};

struct ImportResult {
    int added = 0, replaced = 0, skipped = 0, dirs = 0;
    int flattened = 0;                   // folders (with something in them) whose files went into the target, as the
                                         // volume has no directories. Not in json().
    QVector<QPair<QString, QString>> renamed;      // from, to
    QVector<Change> changes;
    QStringList warnings;
    QVector<ImportError> errors;         // onError = Continue: every item that failed, in order
    bool cancelled = false;              // a callback answered Cancel: the run ended there, what was done is kept
    QJsonObject json() const;            // {added, replaced, skipped, dirs, renamed}
};

// The answer to "X exists: replace it?". The "all" answers are remembered by the Importer for the rest of the run
// (the callback is not asked again). Replace (or Replace all) also replaces a locked or read-only file: asking is
// the GUI's --force.
enum class Conflict { Replace, Skip, ReplaceAll, SkipAll, Cancel };
// target: where the item would go (the existing entry's path); existing: that entry; incoming: the item.
typedef std::function<Conflict(const fs::Path &target, const fs::Entry &existing, const TransferItem &incoming)>
    ConflictCallback;

// What to do with a folder arriving on a volume without directories (caps().dirs false). The answer is for this
// folder only; a hook that offers "... to all" remembers that itself.
struct DirChoice {
    enum Action {
        Flatten,                         // its files go where the folder's own files would (the default, with a warning)
        Into,                            // its files go into dir, each name with prefix in front: a CP/M folder userN
                                         // into user area N (dir = that level), an ADFS directory F into DFS letter F
                                         // (dir = the root, prefix "F.")
        Skip,                            // nothing from it is imported (a "skip" change, note "folder"; not counted)
        Cancel                           // end the run here, keeping what was done
    };
    Action action = Flatten;
    fs::Path dir;
    QString prefix;
};
// dir: the folder item; target: where its files would go if flattened.
typedef std::function<DirChoice(const TransferItem &dir, const fs::Path &target)> DirHook;

enum class OnError { Stop, Continue };

// Where an item goes and under what name, for a caller whose rules differ from the volume's placeIncoming (a window
// keeping its own: CP/M Disk Wrangler's target user, the Acorn window's DFS letters). target: the directory the item
// is imported into (the run's, or where the directory hook put a folder's files).
typedef std::function<fs::Placement(const TransferItem &item, const fs::Path &target)> PlaceHook;
// The attributes a file is written with. replacing: the file it replaces (unlocked first, if it was locked), or null.
// Default: Volume::fromRetro, and a plain item replacing a file keeps that file's attributes (locked apart).
typedef std::function<fs::AttrValues(const TransferItem &item, const fs::Entry *replacing)> AttrsHook;
// Runs one file's changes (unlocking the file it replaces, then writing it), and each directory the run makes, so that
// a failure leaves nothing of that file or directory behind: a window passes Disc::transact. Without one the changes
// are made as they come (the command line: its first error abandons the run). Since step 6b the transaction is the only
// rollback: the filesystem classes keep no snapshot of their own.
typedef std::function<bool(const std::function<bool(QString *)> &change, QString *err)> ItemGuard;
// Told of each error as it is recorded (onError = Continue), so a program can keep its own list in the order things
// happened (step 5l: the Acorn window lists its DFS-letter warnings among the errors).
typedef std::function<void(const ImportError &e)> ErrorHook;

class Importer {
public:
    typedef fs::NodeKind NodeKind;
    Importer(fs::Volume *vol, const QString &volLabel, const ImportOptions &o) : m_v(vol), m_label(volLabel), m_o(o) {}

    void setConflictCallback(const ConflictCallback &f) { m_conflict = f; }
    void setDirHook(const DirHook &f) { m_dirHook = f; }
    void setOnError(OnError e) { m_onError = e; }
    void setPlaceHook(const PlaceHook &f) { m_place = f; }
    void setAttrsHook(const AttrsHook &f) { m_attrs = f; }
    void setItemGuard(const ItemGuard &f) { m_guard = f; }
    void setErrorHook(const ErrorHook &f) { m_errorHook = f; }

    // Imports the items into directory dir (made if it doesn't exist). froms[i] is item i's source, for the log.
    // false (onError = Stop only): err says why, and the run is to be abandoned. true: the run finished, or was
    // cancelled (r->cancelled), or (Continue) went on past the errors in r->errors.
    bool import(const QVector<TransferItem> &items, const QStringList &froms, const fs::Path &dir, ImportResult *r,
                ImportError *err);

private:
    // Each returns false when the run must stop (a failure under Stop, or Cancel); true to go on.
    bool importOne(const TransferItem &it, const QString &from, const fs::Path &dir, const QString &prefix, ImportResult *r);
    bool ensureDir(const fs::Path &dir, ImportResult *r, ImportError *e);
    bool fail(ImportResult *r, ImportError::Kind kind, const QString &msg, const QString &path, const QString &from,
              const QString &why = QString(), ImportError::Cause cause = ImportError::Other, const QString &name = QString());
    bool exists(const fs::Path &p, fs::Entry *e) const;
    bool guarded(const std::function<bool(QString *)> &change, QString *err) const { return m_guard ? m_guard(change, err) : change(err); }
    QString madeKey(const fs::Path &dir, const QString &name) const;
    QString uniqueName(const fs::Path &dir, const QString &name, NodeKind kind) const;

    fs::Volume *m_v;
    QString m_label;
    ImportOptions m_o;
    ConflictCallback m_conflict;
    DirHook m_dirHook;
    PlaceHook m_place;
    AttrsHook m_attrs;
    ItemGuard m_guard;
    ErrorHook m_errorHook;
    OnError m_onError = OnError::Stop;
    int m_all = 0;                       // 0 = ask, 1 = replace all, 2 = skip all (a conflict callback's answer)
    bool m_abandon = false;              // stopped by a failure under Stop
    ImportError m_err;
    QSet<QString> m_made;                // dir + name of everything made in this run
};

} // namespace xfer
} // namespace dw
