/*
 * Linear views of a disc - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include "linear.h"

namespace dw {
namespace device {

Linear::Linear(Container *c, const SideOrder &order, qint64 size)
    : m_c(c), m_order(order), m_size(size >= 0 ? size : (c ? c->storedSize() : 0)) {}

Linear Linear::slice(qint64 off, qint64 len) const {
    Linear l = *this;
    l.m_base = m_base + qMax<qint64>(0, off);
    l.m_size = qMax<qint64>(0, qMin(len, m_size - off));
    return l;
}

// Calls f(stored offset, length, position in the range) for each piece of [off, off + len) that stays in one track.
template <class F> bool Linear::pieces(qint64 off, qint64 len, F f) const {
    qint64 done = 0;
    while (done < len) {
        quint64 room = 0;
        const quint64 s = m_order.toFile(quint64(m_base + off + done), &room);
        const qint64 n = qint64(qMin<quint64>(quint64(len - done), room));
        if (!f(qint64(s), n, done)) return false;
        done += n;
    }
    return true;
}

bool Linear::read(qint64 off, qint64 len, QByteArray *out, QString *err) const {
    out->clear();
    if (!m_c) { *err = "no image"; return false; }
    if (off < 0 || len < 0 || off + len > m_size) { *err = "read beyond the end of the disc"; return false; }
    return pieces(off, len, [&](qint64 s, qint64 n, qint64) {
        QByteArray b;
        if (!m_c->readStored(s, n, &b, err)) return false;
        out->append(b);
        return true;
    });
}

bool Linear::write(qint64 off, const QByteArray &data, QString *err) {
    if (!m_c) { *err = "no image"; return false; }
    if (!m_c->writable()) { *err = "image is read-only"; return false; }
    if (off < 0 || off + data.size() > m_size) { *err = "write beyond the end of the disc"; return false; }
    return pieces(off, data.size(), [&](qint64 s, qint64 n, qint64 at) {
        return m_c->writeStored(s, data.mid(int(at), int(n)), err);
    });
}

bool Linear::touched(qint64 off, qint64 len) const {
    if (!m_c) return false;
    bool any = false;
    pieces(off, len, [&](qint64 s, qint64 n, qint64) { any = any || m_c->storedTouched(s, n); return !any; });
    return any;
}

void Linear::discard(qint64 off, qint64 len) {
    if (!m_c) return;
    pieces(off, len, [&](qint64 s, qint64 n, qint64) { m_c->discardStored(s, n); return true; });
}

} // namespace device
} // namespace dw
