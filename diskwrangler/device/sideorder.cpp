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
#include "sideorder.h"

#include <cstring>

namespace dw {
namespace device {

quint64 SideOrder::toFile(quint64 off, quint64 *room) const {
    if (!doubleSided()) { if (room) *room = ~quint64(0) - off; return off; }
    quint64 t = off / trackBytes, in = off % trackBytes;
    if (room) *room = trackBytes - in;
    if (t >= 2ull * cylinders) return off;
    quint64 cyl = sideFirst ? t % cylinders : t / 2, side = sideFirst ? t / cylinders : t % 2;
    quint64 f = sequential ? side * cylinders + cyl : cyl * 2 + side;
    return f * trackBytes + in;
}

QByteArray layoutFile(const QByteArray &disc, const SideOrder &lay) {
    if (!lay.remaps()) return disc;
    QByteArray out = disc;
    const quint64 T = lay.trackBytes;
    for (quint64 t = 0; t < 2ull * lay.cylinders && (t + 1) * T <= quint64(disc.size()); ++t) {
        quint64 f = lay.toFile(t * T);
        if (f + T <= quint64(out.size())) memcpy(out.data() + f, disc.constData() + t * T, size_t(T));
    }
    return out;
}

QByteArray reorderSides(const QByteArray &file, const SideOrder &lay, bool toSequential) {
    if (!lay.doubleSided()) return file;
    SideOrder il = lay, seq = lay;                    // file track t of an interleaved file <-> its place in a sequential one
    il.sideFirst = seq.sideFirst = false;
    il.sequential = false;
    seq.sequential = true;
    QByteArray out = file;
    const quint64 T = lay.trackBytes;
    for (quint64 t = 0; t < 2ull * lay.cylinders && (t + 1) * T <= quint64(file.size()); ++t) {
        quint64 s = seq.toFile(t * T), i = il.toFile(t * T);
        if (s + T > quint64(file.size())) continue;
        if (toSequential) memcpy(out.data() + s, file.constData() + i, size_t(T));
        else memcpy(out.data() + i, file.constData() + s, size_t(T));
    }
    return out;
}


} // namespace device
} // namespace dw
