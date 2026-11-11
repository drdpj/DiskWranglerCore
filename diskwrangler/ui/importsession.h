/*
 * The import session - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// One import into one mount of a disc, the windows' way (step 5e): Add files, drops of host files, Paste, retro-files
// drops and drops from another window all end here. The session runs the Importer (xfer/importer.h) with
//  - the program's hooks (ImportHooks): what is done to the items first, a question before anything is written, the
//    conflict question, folders on a volume without directories, and, where the program's rules differ from the
//    volume's own (the command line's), where an item goes and which attributes it is written with;
//  - onError = Continue: an item that fails is recorded with its source and the rest are imported; nothing that was
//    done is undone, Cancel included;
//  - one Disc::transact per file, so a write that fails half way leaves nothing of that file.
// A question that names a filesystem (CP/M's character set and user folders, Acorn's DFS letters) is always a hook
// filled by the program, never a branch here. DiscWindow::importItems builds the hooks from the window's own
// (DiscWindow::importHooks), runs the session, refreshes and calls the window's importSummary.

#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

#include <diskwrangler/fs/disc.h>
#include <diskwrangler/xfer/importer.h>
#include <diskwrangler/xfer/transferitem.h>

namespace dw {
namespace ui {

struct ImportHooks {
    // The items as they arrive, before anything else (CP/M: the host's clash suffixes taken off).
    std::function<void(QVector<TransferItem> *items)> prepare;
    // Asked once, after the read-only check and before anything is written: false cancels the run, nothing written
    // (CP/M: the character set for host names a plain ASCII disc can't hold).
    std::function<bool(const QVector<TransferItem> &items)> beforeRun;
    // A file exists: the program's question (see Importer). Without one, options decide, as on the command line.
    xfer::ConflictCallback conflict;
    // A folder arriving on a volume without directories (CP/M: a folder userN is user area N; Acorn: an ADFS
    // directory with a one-letter name onto DFS). Default: flattened, with a warning.
    xfer::DirHook dirHook;
    // Where an item goes and its attributes, when the program's rules aren't the volume's (Importer). Default: the
    // volume's placeIncoming and fromRetro.
    xfer::PlaceHook place;
    xfer::AttrsHook attrs;
    xfer::ImportOptions options;
};

class ImportSession {
public:
    ImportSession(fs::Disc *disc, int mount, const ImportHooks &hooks) : m_disc(disc), m_mount(mount), m_h(hooks) {}

    // Imports items into dir. readOnlyWhy non-empty: nothing is done, and the result has the one error "The disc is
    // read-only: <why>". froms: each item's source, for the errors (default: the item's name). readErrors: problems
    // the program met while reading the items (host files it couldn't read); they come first in the result's errors.
    xfer::ImportResult run(QVector<TransferItem> items, const QStringList &froms, const fs::Path &dir,
                           const QString &readOnlyWhy = QString(), const QVector<xfer::ImportError> &readErrors = {});

private:
    fs::Disc *m_disc;
    int m_mount;
    ImportHooks m_h;
};

} // namespace ui
} // namespace dw
