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
#pragma once

// Turning file bytes into readable text: the two views every program offers, plain text and a hex dump. QtCore only.
// (The programs' own views - BBC BASIC, CP/M text - are in the programs; they plug in through viewers.h.)

#include <QByteArray>
#include <QString>

namespace dw {
namespace text {

// CR / LF / CRLF normalised to newlines, tabs kept, other control characters shown as '.'; a DOS ^Z as the very
// last byte is dropped.
QString plain(const QByteArray &d);
// "00000000  41 42 ...  |AB..|" 16 bytes per line.
QString hexDump(const QByteArray &d);
// Heuristic: mostly printable (95% of the first 4 KB), no NULs.
bool looksLikeText(const QByteArray &d);
// The character shown for a byte in a listing: printable ASCII and 0xA0 up are themselves, the rest '.'. With
// bbc set, 0x60 is the pound sign (the BBC character set).
QChar shown(quint8 c, bool bbc);

} // namespace text
} // namespace dw
