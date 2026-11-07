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
#include "fatprobe.h"

#include <functional>

#include <diskwrangler/device/linear.h>

namespace dw {
namespace fat {

static QString sizeText(quint64 bytes) {
    if (bytes >= 1024 * 1024 && bytes % (1024 * 1024) == 0) return QString("%1M").arg(bytes / (1024 * 1024));
    if (bytes >= 8 * 1024 * 1024) return QString("%1M").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    return QString("%1K").arg(bytes / 1024);
}

int sideScore(const device::Container &orig, quint64 full, const device::SideOrder &order, int fmt) {
    device::Container c(orig);
    c.discardPending();
    device::SideOrder sl = order;
    if (c.kind() != device::Kind::Raw) sl.sequential = false;      // every track says where it is: stored interleaved
    const bool raw = c.kind() == device::Kind::Raw;
    const quint64 fileSize = raw ? quint64(c.rawFileSize()) : quint64(c.storedSize());
    if (!raw && fileSize == 0) return -1;
    const quint64 fullSize = raw ? qMax(full, fileSize) : fileSize;
    const quint64 rounded = (fullSize + 255) / 256 * 256;
    c.setRawPadding(qint64(fullSize), 0);
    c.setWritable(false);
    device::Linear lin(&c, sl, raw ? qint64(rounded) : qMin<qint64>(c.storedSize(), qint64(rounded)));
    Volume v;
    QString e;
    bool ok = fmt < 0 ? v.attach(&lin, 0, quint32(full), &e) : v.attachFormat(&lin, 0, quint32(full), Format(fmt), &e);
    if (!ok || !v.check().ok()) return -1;
    std::function<bool(quint32, int)> walk = [&](quint32 dir, int depth) -> bool {
        if (depth > 16) return true;
        QVector<DEntry> L;
        if (!v.listDir(dir, &L, &e)) return false;
        for (const DEntry &x : L) {
            if (!x.isDir() || x.name == "." || x.name == "..") continue;
            QVector<DEntry> C;
            if (!v.listDir(x.cluster, &C, &e)) return false;
            if (C.size() < 2 || C[0].name != "." || !C[0].isDir() || C[0].cluster != x.cluster ||
                C[1].name != ".." || !C[1].isDir() || C[1].cluster != dir)
                return false;
            if (!walk(x.cluster, depth + 1)) return false;
        }
        return true;
    };
    return walk(0, 0) ? int(qMin<quint32>(v.lostClusters(), 0x7FFFFFFF)) : -1;
}

void decideSides(int il, int seq, bool *isSequential, bool *confirmed) {
    if (il >= 0 && seq >= 0 && il != seq) {           // both read without errors: the one that accounts for more wins
        *isSequential = seq < il;
        *confirmed = true;
        return;
    }
    *isSequential = seq >= 0 && il < 0;
    *confirmed = (il >= 0) != (seq >= 0);
}

QVector<fs::Candidate> Probe::probe(const fs::ProbeContext &ctx) const {
    const quint64 size = quint64(ctx.size());
    if (size < 512) return {};
    const QByteArray s01 = ctx.stored(0, 512);
    fs::Candidate c;
    c.fsId = "fat";
    c.name = "FAT";
    c.tier = fs::Tier::Likely;
    Found f;

    // a boot sector BPB
    Params p;
    if (Volume::readBpb(s01, &p) && Volume::plausible(p) && quint64(p.total) * p.bps <= 64ull * 1024 * 1024 &&
        quint8(s01[0]) != 0) {
        f.fullSize = qMax(size, quint64(p.total) * p.bps);
        const quint64 v = quint64(p.total) * p.bps;
        const bool pc = v == 368640 || v == 737280 || v == 1228800 || v == 1474560;
        c.name = pc ? "PC FAT" : "FAT";
        c.description = pc ? QString("PC %1 FAT floppy").arg(v == 368640 ? "360K" : v == 737280 ? "720K" : v == 1228800 ? "1.2M" : "1.44M")
                           : QString("FAT12/16 volume (%1)").arg(sizeText(v));
        // PC floppies are always stored interleaved, so they have no side order to detect or change
        if (size < v) f.notes << QString("the image file is %1 bytes shorter than the volume; it will be padded with zeros when saved").arg(v - size);
        c.evidence << "a boot sector with a BPB";
        c.notes = f.notes;
        c.data = f;
        return {c};
    }
    // no BPB (old DOS 1.x / 2.0 images): a PC floppy's size and the media byte
    struct Std { quint64 size; Format fmt; const char *name; };
    static const Std stds[] = {{368640, FmtPc360, "PC 360K FAT floppy"}, {737280, FmtPc720, "PC 720K FAT floppy"},
                               {1228800, FmtPc1200, "PC 1.2M FAT floppy"}, {1474560, FmtPc1440, "PC 1.44M FAT floppy"}};
    for (const Std &s : stds) {
        if (size < s.size) continue;
        const QByteArray fat = ctx.stored(512, 3);
        if (size == s.size && quint8(fat[0]) == Volume::standard(s.fmt).media && quint8(fat[1]) == 0xFF && quint8(fat[2]) == 0xFF) {
            f.format = s.fmt;
            f.fromTable = true;
            f.fullSize = size;
            c.name = "PC FAT";
            c.description = QString(s.name) + " (no BPB)";
            c.evidence << "a PC floppy's size, and a FAT with its media byte in sector 1";
            c.data = f;
            return {c};
        }
    }
    // Acorn DOS Plus 800K: FAT at byte 0, media byte FD. 80 cylinders x 2 sides x 5 x 1024 bytes; DOS Plus numbers
    // these sectors in interleaved order, so an interleaved file needs no mapping.
    if (size == 819200) {
        const QByteArray fat = ctx.stored(0, 3);
        if (quint8(fat[0]) == 0xFD && quint8(fat[1]) == 0xFF && quint8(fat[2]) == 0xFF) {
            f.acorn800 = true;
            f.format = FmtAcorn800;
            f.fromTable = true;
            f.fullSize = 819200;
            f.sides.trackBytes = 5120;
            f.sides.cylinders = 80;
            f.sides.sideFirst = false;                 // numbered sector, side, track
            if (ctx.forceSequential >= 0) {
                f.sides.sequential = ctx.forceSequential == 1;
            } else {
                device::SideOrder il = f.sides, sq = f.sides;
                il.sequential = false;
                sq.sequential = true;
                decideSides(sideScore(*ctx.container, 819200, il, int(FmtAcorn800)),
                            sideScore(*ctx.container, 819200, sq, int(FmtAcorn800)), &f.sides.sequential, &f.sidesConfirmed);
            }
            c.name = "DOS Plus";
            c.description = "Acorn DOS Plus 800K floppy";
            if (f.sides.sequential) c.description += ", sequential sides";
            c.evidence << "a FAT with media byte FD at the start of an 800K disc";
            c.data = f;
            return {c};
        }
    }
    return {};
}

} // namespace fat
} // namespace dw
