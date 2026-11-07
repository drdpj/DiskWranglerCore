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
#pragma once

// Linear: a disc as one flat run of bytes in its own sector numbering, over a Container's stored bytes and a
// SideOrder. QtCore only.
//
// What a filesystem that thinks in logical sectors or byte offsets works on: an ADFS disc, a DFS side, the DOS Plus
// partition inside an ADFS file (slice), a PC FAT floppy. Reads overlay the container's staged writes, and writes are
// staged in it, so a Linear never touches a file. It works over any container whose tracks all have the same shape
// (a raw file always), so an Acorn disc in an ImageDisk, DSK or TeleDisk file reads exactly like a raw one.
// A Linear is a small value: copy it freely; it must not outlive its container.
#include "container.h"
#include "sideorder.h"

namespace dw {
namespace device {

class Linear {
public:
    Linear() = default;
    // The whole disc. size < 0: the container's stored size.
    explicit Linear(Container *c, const SideOrder &order = SideOrder(), qint64 size = -1);

    bool isNull() const { return m_c == nullptr; }
    Container *container() const { return m_c; }
    const SideOrder &order() const { return m_order; }
    qint64 base() const { return m_base; }                // where this view starts, in the disc's numbering
    qint64 size() const { return m_size; }
    bool writable() const { return m_c && m_c->writable(); }

    // Byte ranges, staged writes included. Fails outside [0, size()), or where the container can't be read.
    bool read(qint64 off, qint64 len, QByteArray *out, QString *err) const;
    bool write(qint64 off, const QByteArray &data, QString *err);       // staged in the container
    bool touched(qint64 off, qint64 len) const;          // anything staged in the range
    void discard(qint64 off, qint64 len);                // drop staged writes lying wholly inside the range

    // A window onto part of this view: [off, off + len). Reads and writes outside it fail.
    Linear slice(qint64 off, qint64 len) const;

private:
    template <class F> bool pieces(qint64 off, qint64 len, F f) const;
    Container *m_c = nullptr;
    SideOrder m_order;
    qint64 m_base = 0, m_size = 0;
};

} // namespace device
} // namespace dw
