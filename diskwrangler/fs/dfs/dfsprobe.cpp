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
#include "dfsprobe.h"

namespace dw {
namespace dfs {

QString sizeText(quint64 bytes) {
    if (bytes >= 1024 * 1024 && bytes % (1024 * 1024) == 0) return QString("%1M").arg(bytes / (1024 * 1024));
    if (bytes >= 8 * 1024 * 1024) return QString("%1M").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    return QString("%1K").arg(bytes / 1024);
}

// Each side has its own catalogue on its own track 0, so the second catalogue's position (file sector 10 if
// interleaved, file sector <sectors per side> if sequential) settles the side order. A Watford 62-file disc may lower
// the first catalogue's disc size: its true size, which places side 1 of a sequential image, is the larger of the two
// catalogues' claims.
QVector<fs::Candidate> Probe::probe(const fs::ProbeContext &ctx) const {
    const quint64 size = quint64(ctx.size());
    if (size < 512) return {};
    quint32 sec = 0;
    const bool watford = watfordCatalogue(ctx.stored(0, 1024), &sec);
    if (!watford && !plausibleCatalogue(ctx.stored(0, 256), ctx.stored(256, 256), &sec)) return {};
    fs::Candidate c;
    if (isCpmFence(ctx.stored(0, 512))) {
        // the disc is CP/M: editing it as DFS (or writing on side 1, which has no catalogue) would destroy it
        c.fsId = "acorn-cpm";
        c.name = "Acorn Z80 CP/M";
        c.description = "Acorn Z80 CP/M disc";
        c.tier = fs::Tier::Certain;
        c.owner = fs::cpmDiskWrangler();
        c.evidence << "its DFS catalogue holds only $.CPMDISC, a locked file that keeps DFS off the CP/M files";
        return {c};
    }
    const int hint = ctx.suffix == "dsd" ? 2 : (ctx.suffix == "ssd" ? 1 : 0);
    const bool il = plausibleCatalogue(ctx.stored(10 * 256, 256), ctx.stored(11 * 256, 256), nullptr);
    const bool seq = size >= quint64(sec) * 256 + 512 &&
                     plausibleCatalogue(ctx.stored(qint64(sec) * 256, 256), ctx.stored(qint64(sec) * 256 + 256, 256), nullptr);
    const bool dsd = hint == 2 || (hint == 0 && (il || seq) && size > quint64(sec) * 256);
    Found f;
    f.watford = watford;
    f.layout.sectorsPerSide = sec;
    f.layout.doubleSided = dsd;
    f.layout.interleaved = true;
    if (dsd) {
        f.sides.trackBytes = 10 * 256;
        f.sides.cylinders = (sec + 9) / 10;
        f.sides.sequential = ctx.forceSequential >= 0 ? ctx.forceSequential == 1 : (!il && seq);
        f.sides.sideFirst = false;
    }
    f.fullSize = qMax(size, quint64(sec) * 256 * (dsd ? 2 : 1));
    if (size < f.fullSize)
        f.notes << QString("the image file is %1 bytes shorter than a full disc (blank tracks were trimmed); "
                           "it will be padded with zeros when saved").arg(f.fullSize - size);
    c.fsId = "dfs";
    c.name = "DFS";
    c.description = QString("DFS %1 %2-track (%3 per side)%4")
                        .arg(dsd ? "double-sided" : "single-sided").arg(sec / 10).arg(sizeText(quint64(sec) * 256))
                        .arg(watford ? QString(", Watford 62-file catalogue") : QString());
    if (f.sides.doubleSided() && f.sides.sequential) c.description += ", sequential sides";
    c.tier = fs::Tier::Likely;
    c.notes = f.notes;
    c.evidence << (watford ? "a Watford 62-file catalogue in sectors 0-3" : "a DFS catalogue in sectors 0-1");
    c.data = f;
    return {c};
}

} // namespace dfs
} // namespace dw
