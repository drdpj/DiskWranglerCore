/*
 * Transfer items - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// A file or folder in flight between discs, the host and other programs: what a drag, a copy or an extract carries.
// It is a retro-files item (docs/retro-files-format.md) held in memory. QtCore only.

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QVector>

namespace dw {

struct TransferItem {
    QString name;                           // the name in the source, as the document's "name" has it (a DFS file: "A.BOOT", "$.BOOT")
    QString hostName;                       // what to call it on the host when that differs from name (CP/M: a name shown in a national character set); empty = from name
    bool isDir = false;
    QByteArray data;
    QDateTime when;                         // the source's date (invalid if none); local, no zone
    QMap<QString, QJsonObject> attrs;       // the document's "attributes", by family: "acorn", "fat", "cpm", later "flex"
    QString sourceFs;                       // the document's source filesystem: "adfs", "dfs", "fat", "cpm"; empty for a host file
    QString hostPath;                       // the host file or folder it was read from (xfer::readHost); empty for anything else
    QVector<TransferItem> kids;
};

} // namespace dw
