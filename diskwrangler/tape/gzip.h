/*
 * gzip reading and writing - part of DiskWranglerCore
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
// gzip files (RFC 1952) for UEF tapes, which are usually compressed. QtCore only, no zlib of our own: QtCore can
// compress (qCompress) but has no way to read a gzip stream, so the inflater here is written from RFC 1951; writing
// wraps qCompress's deflate data in a gzip header and trailer.
#include <QByteArray>
#include <QString>

namespace dw {
namespace tape {

bool isGzip(const QByteArray &file);                     // starts 1F 8B 08
// Every member of a gzip file, uncompressed and joined; each member's CRC-32 and length checked. maxOut guards
// against a file that claims to unpack to more than any tape could hold.
bool gunzip(const QByteArray &file, QByteArray *out, QString *err, qint64 maxOut = 64 * 1024 * 1024);
// One gzip member: no name, time 0 (so the same data always gives the same bytes), deflated at level 9.
QByteArray gzip(const QByteArray &data);
// Raw deflate data (RFC 1951) from *pos on; *pos is left after the last byte used.
bool inflate(const QByteArray &in, qint64 *pos, QByteArray *out, QString *err, qint64 maxOut);
quint32 crc32(const QByteArray &data);                   // the gzip (ISO 3309) CRC

} // namespace tape
} // namespace dw
