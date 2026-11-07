/*
 * Side order of double-sided discs - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// A double-sided floppy has two independent orderings, and SideOrder keeps them apart. QtCore only.
//  * how the DISC numbers its sectors (sideFirst) - what a filesystem works in:
//      sideFirst = true:  all of side 0 (cylinders 0..n-1), then all of side 1. ADFS L and DOS Plus 640K: the
//                         machine reads side 0 to the end, then side 1 (checked against real .adl discs).
//      sideFirst = false: sides alternate cylinder by cylinder (sector, side, track): DOS Plus 800K, PC, and a
//                         DFS .dsd (whose two sides are separate discs, mapped by the DFS code on top of this).
//  * how the image STORES the tracks (sequential):
//      false: interleaved - cylinder 0 side 0, cylinder 0 side 1, cylinder 1 side 0, ...  (the usual layout; also
//             how Container::readStored presents an ImageDisk, DSK or TeleDisk image)
//      true:  sequential  - all of side 0, then all of side 1 (a raw file only)
// Linear (linear.h) translates between the two track by track as it reads and writes. (Moved from Acorn Disk
// Wrangler, where it was Adfs::SideLayout.)
#include <QByteArray>
#include <QtGlobal>

namespace dw {
namespace device {

struct SideOrder {
    quint32 trackBytes = 0;          // one track (one side of one cylinder); 0 = not a double-sided floppy
    quint32 cylinders = 0;
    bool sequential = false;         // stored order
    bool sideFirst = false;          // disc's own sector numbering
    bool doubleSided() const { return trackBytes != 0 && cylinders != 0; }
    bool remaps() const { return doubleSided() && (sequential || sideFirst); }
    quint64 discBytes() const { return quint64(trackBytes) * cylinders * 2; }
    // stored offset of a disc offset (in the disc's own numbering), and how many bytes from there stay in the same track
    quint64 toFile(quint64 off, quint64 *room = nullptr) const;
};

// The stored bytes for a whole disc given in its own numbering, laid out as lay says. Bytes beyond discBytes() are
// kept as they are.
QByteArray layoutFile(const QByteArray &disc, const SideOrder &lay);
// Rearranges interleaved stored bytes into sequential ones (toSequential) or back - a pure reordering of the
// tracks, whatever the disc's numbering. Bytes beyond discBytes() are kept as they are.
QByteArray reorderSides(const QByteArray &file, const SideOrder &lay, bool toSequential);

} // namespace device
} // namespace dw
