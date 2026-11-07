/*
 * Acorn DFS detection - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// Recognising a DFS disc (SSD / DSD, Acorn and Watford 62-file catalogues): the DFS module's Probe. Moved from Acorn
// Disk Wrangler's detectDisk. QtCore only.
//
// Candidates:
//  * "dfs", Likely: a plausible catalogue. data is a Found: the layout, side order and size to open it with.
//  * "acorn-cpm", Certain, owned by CP/M Disk Wrangler: the catalogue Acorn's Z80 CP/M writes ($.CPMDISC).

#include <QStringList>

#include <diskwrangler/device/sideorder.h>
#include <diskwrangler/fs/detection.h>
#include <diskwrangler/fs/dfs/dfs.h>

namespace dw {
namespace dfs {

struct Found {
    Layout layout;                   // layout.interleaved is always true: the Linear does the side translation
    device::SideOrder sides;         // a double-sided image: geometry and the side order of the FILE
    quint64 fullSize = 0;            // what the disc should be; a shorter file is padded when saved
    bool watford = false;
    QStringList notes;
};

class Probe : public fs::Probe {
public:
    QString id() const override { return "dfs"; }
    QVector<fs::Candidate> probe(const fs::ProbeContext &ctx) const override;
};

// "640K", "1.2M": the sizes in descriptions
QString sizeText(quint64 bytes);

} // namespace dfs
} // namespace dw
