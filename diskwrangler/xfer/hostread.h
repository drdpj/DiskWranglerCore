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

// The one host-file reader (step 5l, deliberate change 9): how both windows and both console programs read files and
// folders from the computer before they are put on a disc. One policy for all four:
//  - a folder's contents come folders first, then files, each in name order ignoring case;
//  - symbolic links met inside a folder are skipped, files and folders alike (a path the user gave is read as what
//    it points to, as it always was: following links inside folders may become an option later);
//  - folders are read to any depth;
//  - a file larger than the program's limit is refused before it is read;
//  - it never stops: what can't be read is left out and listed, with its kind, its host path and the system's reason.
// What a program does with the errors stays its own: the console programs stop with the first one and their own
// message and exit code; the windows pass them to DiscWindow::importItems as readErrors, in their own words.
// A program's per-file hook may drop a file or fill in its item (Acorn's .inf sidecars, CP/M's .inf rule).
// QtCore only.

#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

#include <diskwrangler/xfer/transferitem.h>

namespace dw {
namespace xfer {

struct HostReadError {
    enum Kind { CannotRead, TooLarge, NotFound };
    Kind kind = CannotRead;
    QString path;                        // the host path: as given for a path named, absolute for one inside a folder
    QString reason;                      // the system's reason (CannotRead, NotFound); empty for TooLarge
    bool named = false;                  // path was one of the paths given (not found inside a folder)
};

// A program's per-file rule. hostFile: the file's path (as given when named, absolute inside a folder); named: it was
// one of the paths given; item: its name, date and hostPath filled in, no data yet. Return false to leave the file
// out (no error). Called before the size check and the read, so a file left out is never read.
typedef std::function<bool(const QString &hostFile, bool named, TransferItem *item)> HostFileHook;

struct HostReadOptions {
    qint64 sizeLimit = -1;               // bytes: a larger file is refused (TooLarge) before it is read; -1 = no limit
    HostFileHook fileHook;
};

// The host files and folders at paths, as items (a folder's item holds its contents in kids). Each item's name is
// the host name (a folder given as "dir/" is named after the folder), when its modification date, hostPath where it
// was read from. errors (may be null): everything left out with a problem, in the order met.
QVector<TransferItem> readHost(const QStringList &paths, const HostReadOptions &options, QVector<HostReadError> *errors);

} // namespace xfer
} // namespace dw
