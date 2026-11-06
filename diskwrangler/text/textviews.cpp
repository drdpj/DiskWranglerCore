/*
 * Text views - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include "textviews.h"

namespace dw {
namespace text {

QChar shown(quint8 c, bool bbc) {
    if (bbc && c == 0x60) return QChar(0x00A3);          // BBC character set: 0x60 is the pound sign
    if (c >= 0x20 && c < 0x7F) return QChar(c);
    if (c >= 0xA0) return QChar(c);
    return QChar('.');
}

QString plain(const QByteArray &d) {
    QString s;
    s.reserve(d.size());
    for (int i = 0; i < d.size(); ++i) {
        quint8 c = quint8(d[i]);
        if (c == 0x0D) { s += '\n'; if (i + 1 < d.size() && d[i + 1] == 0x0A) ++i; }
        else if (c == 0x0A || c == 0x09) s += QChar(c);
        else if (c == 0x1A && i == d.size() - 1) { /* DOS end-of-file marker */ }
        else s += shown(c, false);
    }
    return s;
}

QString hexDump(const QByteArray &d) {
    QString out;
    out.reserve(d.size() * 4 + 64);
    for (int o = 0; o < d.size(); o += 16) {
        QString hex, asc;
        for (int i = 0; i < 16; ++i) {
            if (o + i < d.size()) {
                quint8 c = quint8(d[o + i]);
                hex += QString("%1 ").arg(c, 2, 16, QChar('0')).toUpper();
                asc += (c >= 0x20 && c < 0x7F) ? QChar(c) : QChar('.');
            } else hex += "   ";
            if (i == 7) hex += ' ';
        }
        out += QString("%1  %2 |%3|\n").arg(o, 8, 16, QChar('0')).toUpper().arg(hex, asc);
    }
    return out;
}

bool looksLikeText(const QByteArray &d) {
    if (d.isEmpty()) return true;
    int n = qMin(d.size(), 4096), good = 0;
    for (int i = 0; i < n; ++i) {
        quint8 c = quint8(d[i]);
        if (c == 0) return false;
        if ((c >= 0x20 && c < 0x7F) || c == 0x0D || c == 0x0A || c == 0x09 || (c == 0x1A && i == d.size() - 1)) ++good;
    }
    return good * 100 >= n * 95;
}

} // namespace text
} // namespace dw
