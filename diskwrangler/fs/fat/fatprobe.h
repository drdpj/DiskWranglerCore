/*
 * FAT detection - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// Recognising a FAT disc: the FAT module's Probe (moved from Acorn Disk Wrangler's detectDisk). QtCore only.
//
// Candidates ("fat", Likely), data a Found:
//  * a boot sector with a usable BPB (PC floppies, any FAT12/16 volume up to 64 MB);
//  * no BPB, but a PC floppy's size and the FAT's media byte where DOS 1.x puts it;
//  * Acorn DOS Plus 800K: the FAT at byte 0, media byte FD (its side order found from the contents).

#include <QStringList>

#include <diskwrangler/device/container.h>
#include <diskwrangler/device/sideorder.h>
#include <diskwrangler/fs/detection.h>
#include <diskwrangler/fs/fat/fat.h>

namespace dw {
namespace fat {

struct Found {
    bool acorn800 = false;           // Acorn DOS Plus 800K (no boot record)
    Format format = FmtPc720;
    bool fromTable = false;          // no usable BPB: open with the table's geometry (attachFormat)
    quint64 fullSize = 0;            // what the volume should be; a shorter file is padded when saved
    device::SideOrder sides;         // Acorn 800K: geometry and the side order of the FILE (PC floppies have none)
    bool sidesConfirmed = true;      // false: both orders read the same (a blank disc), so the usual one was used
    QStringList notes;
};

class Probe : public fs::Probe {
public:
    QString id() const override { return "fat"; }
    QVector<fs::Candidate> probe(const fs::ProbeContext &ctx) const override;
};

// How well a FAT volume reads in one side order: -1 = inconsistent, otherwise the number of lost clusters (in use in
// the FAT but in no file). The FAT itself is on track 0 in every format, so in the wrong order it still says what is
// in use while the directories that should account for it read as garbage or as empty. fmt < 0: geometry from the
// boot sector BPB. c is not changed (a copy is read).
int sideScore(const device::Container &c, quint64 full, const device::SideOrder &sl, int fmt);
// From the scores of both orders: whether the file is sequential, and whether the contents could tell. A tie can't
// be decided, and then the format's usual order (interleaved) is used.
void decideSides(int interleaved, int sequential, bool *isSequential, bool *confirmed);

} // namespace fat
} // namespace dw
