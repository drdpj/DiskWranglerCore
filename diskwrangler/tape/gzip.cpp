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
// The inflater is written from RFC 1951 (DEFLATE) and the gzip wrapper from RFC 1952. Its Huffman decoding - codes
// counted by length and decoded a bit at a time against those counts, without lookup tables - follows the method of
// puff.c by Mark Adler (part of zlib, https://github.com/madler/zlib/tree/develop/contrib/puff), whose notice is:
//
//     Copyright (C) 2002-2013 Mark Adler, all rights reserved
//     This software is provided 'as-is', without any express or implied warranty. In no event will the author be
//     held liable for any damages arising from the use of this software. Permission is granted to anyone to use this
//     software for any purpose, including commercial applications, and to alter it and redistribute it freely,
//     subject to the following restrictions: 1. The origin of this software must not be misrepresented; you must not
//     claim that you wrote the original software. If you use this software in a product, an acknowledgment in the
//     product documentation would be appreciated but is not required. 2. Altered source versions must be plainly
//     marked as such, and must not be misrepresented as being the original software. 3. This notice may not be
//     removed or altered from any source distribution.
//
// No code was copied from puff.c; this file was written for DiskWranglerCore and is distributed under the GPL with it.
// The credit and notice are given because its decoding follows puff's method. See the programs' THIRD-PARTY-NOTICES.md.
#include "gzip.h"

namespace dw {
namespace tape {

namespace {

// Bits from the input, least significant first (RFC 1951 3.1.1).
struct Bits {
    const uchar *p;
    qint64 n, pos;
    quint32 buf = 0;
    int cnt = 0;
    bool past = false;                                   // ran off the end
    int get(int need) {
        quint32 v = buf;
        while (cnt < need) {
            if (pos >= n) { past = true; return 0; }
            v |= quint32(p[pos++]) << cnt;
            cnt += 8;
        }
        buf = need >= 32 ? 0 : v >> need;
        cnt -= need;
        return int(v & ((need >= 32 ? 0 : (1u << need)) - 1));
    }
    void align() { buf = 0; cnt = 0; }                   // drop the rest of the current byte
};

// A canonical Huffman code: how many codes of each length, and the symbols in code order (RFC 1951 3.2.2).
struct Huffman {
    short count[16] = {};
    short symbol[320] = {};
    // false: more codes of some length than the lengths allow (over-subscribed). An incomplete code is allowed; a
    // code that isn't in it fails when met.
    bool build(const short *lengths, int n) {
        for (short &c : count) c = 0;
        for (int i = 0; i < n; ++i) ++count[lengths[i]];
        if (count[0] == n) return true;                  // no codes: fails only if used
        int left = 1;
        for (int len = 1; len < 16; ++len) {
            left <<= 1;
            left -= count[len];
            if (left < 0) return false;
        }
        short offs[16];
        offs[1] = 0;
        for (int len = 1; len < 15; ++len) offs[len + 1] = short(offs[len] + count[len]);
        for (int i = 0; i < n; ++i) if (lengths[i]) symbol[offs[lengths[i]]++] = short(i);
        return true;
    }
    // A symbol, or -1 for a code that isn't there (or the input ran out).
    int decode(Bits &b) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; ++len) {
            code |= b.get(1);
            if (b.past) return -1;
            const int c = count[len];
            if (code - c < first) return symbol[index + (code - first)];
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};

const short LenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131,
                           163, 195, 227, 258};
const short LenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const short DistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537,
                            2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const short DistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool codes(Bits &b, const Huffman &lit, const Huffman &dist, QByteArray *out, QString *err, qint64 maxOut) {
    for (;;) {
        int sym = lit.decode(b);
        if (sym < 0) { *err = b.past ? "the compressed data ends early" : "a code that isn't in the table"; return false; }
        if (sym < 256) {
            if (out->size() >= maxOut) { *err = "unpacks to more than a tape could hold"; return false; }
            out->append(char(sym));
            continue;
        }
        if (sym == 256) return true;
        sym -= 257;
        if (sym >= 29) { *err = "a length code that doesn't exist"; return false; }
        const int len = LenBase[sym] + b.get(LenExtra[sym]);
        const int ds = dist.decode(b);
        if (ds < 0 || ds >= 30) { *err = b.past ? "the compressed data ends early" : "a distance code that doesn't exist"; return false; }
        const int d = DistBase[ds] + b.get(DistExtra[ds]);
        if (b.past) { *err = "the compressed data ends early"; return false; }
        if (d > out->size()) { *err = "a distance back past the start"; return false; }
        if (out->size() + len > maxOut) { *err = "unpacks to more than a tape could hold"; return false; }
        const qint64 from = out->size() - d;
        for (int i = 0; i < len; ++i) out->append(out->at(from + i));   // may overlap what it writes: byte by byte
    }
}

const quint32 *crcTable() {
    static quint32 t[256];
    static bool made = false;
    if (!made) {
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        made = true;
    }
    return t;
}

quint32 le32(const QByteArray &b, qint64 at) {
    return quint32(uchar(b.constData()[at])) | quint32(uchar(b.constData()[at + 1])) << 8 | quint32(uchar(b.constData()[at + 2])) << 16 |
           quint32(uchar(b.constData()[at + 3])) << 24;
}

void putLe32(QByteArray *b, quint32 v) {
    for (int i = 0; i < 4; ++i) b->append(char((v >> (8 * i)) & 0xFF));
}

} // namespace

quint32 crc32(const QByteArray &data) {
    const quint32 *t = crcTable();
    quint32 c = 0xFFFFFFFFu;
    for (char ch : data) c = t[(c ^ uchar(ch)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool isGzip(const QByteArray &file) {
    return file.size() >= 3 && uchar(file.constData()[0]) == 0x1F && uchar(file.constData()[1]) == 0x8B && uchar(file.constData()[2]) == 8;
}

bool inflate(const QByteArray &in, qint64 *pos, QByteArray *out, QString *err, qint64 maxOut) {
    Bits b{reinterpret_cast<const uchar *>(in.constData()), in.size(), *pos};
    int last;
    do {
        last = b.get(1);
        const int type = b.get(2);
        if (b.past) { *err = "the compressed data ends early"; return false; }
        if (type == 0) {                                 // stored
            b.align();
            if (b.pos + 4 > b.n) { *err = "the compressed data ends early"; return false; }
            const int len = uchar(in.constData()[b.pos]) | uchar(in.constData()[b.pos + 1]) << 8;
            const int nlen = uchar(in.constData()[b.pos + 2]) | uchar(in.constData()[b.pos + 3]) << 8;
            if (len != (~nlen & 0xFFFF)) { *err = "a stored block's length check is wrong"; return false; }
            b.pos += 4;
            if (b.pos + len > b.n) { *err = "the compressed data ends early"; return false; }
            if (out->size() + len > maxOut) { *err = "unpacks to more than a tape could hold"; return false; }
            out->append(in.constData() + b.pos, len);
            b.pos += len;
        } else if (type == 1) {                          // fixed codes
            static Huffman lit, dist;
            static bool made = false;
            if (!made) {
                short l[288];
                for (int i = 0; i < 144; ++i) l[i] = 8;
                for (int i = 144; i < 256; ++i) l[i] = 9;
                for (int i = 256; i < 280; ++i) l[i] = 7;
                for (int i = 280; i < 288; ++i) l[i] = 8;
                lit.build(l, 288);
                short d[30];
                for (short &x : d) x = 5;
                dist.build(d, 30);
                made = true;
            }
            if (!codes(b, lit, dist, out, err, maxOut)) return false;
        } else if (type == 2) {                          // dynamic codes
            const int nlen = b.get(5) + 257, ndist = b.get(5) + 1, ncode = b.get(4) + 4;
            if (b.past) { *err = "the compressed data ends early"; return false; }
            if (nlen > 286 || ndist > 30) { *err = "a block with too many codes"; return false; }
            static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            short lengths[320] = {};
            for (int i = 0; i < ncode; ++i) lengths[order[i]] = short(b.get(3));
            Huffman lencode;
            if (!lencode.build(lengths, 19)) { *err = "a bad code-length table"; return false; }
            int i = 0;
            while (i < nlen + ndist) {
                int sym = lencode.decode(b);
                if (sym < 0) { *err = b.past ? "the compressed data ends early" : "a bad code length"; return false; }
                if (sym < 16) { lengths[i++] = short(sym); continue; }
                short v = 0;
                int rep;
                if (sym == 16) {
                    if (i == 0) { *err = "a repeat with nothing before it"; return false; }
                    v = lengths[i - 1];
                    rep = 3 + b.get(2);
                } else if (sym == 17) rep = 3 + b.get(3);
                else rep = 11 + b.get(7);
                if (i + rep > nlen + ndist) { *err = "too many code lengths"; return false; }
                while (rep--) lengths[i++] = v;
            }
            if (lengths[256] == 0) { *err = "no end-of-block code"; return false; }
            Huffman lit, dist;
            if (!lit.build(lengths, nlen) || !dist.build(lengths + nlen, ndist)) { *err = "a bad code table"; return false; }
            if (!codes(b, lit, dist, out, err, maxOut)) return false;
        } else {
            *err = "a block of an unknown type";
            return false;
        }
    } while (!last);
    *pos = b.pos;                                        // the rest of the last byte is padding
    return true;
}

bool gunzip(const QByteArray &file, QByteArray *out, QString *err, qint64 maxOut) {
    out->clear();
    qint64 p = 0;
    int member = 0;
    while (p < file.size()) {
        if (member > 0 && file.mid(p).count('\0') == file.size() - p) break;   // zeros after the last member
        if (!isGzip(file.mid(p, 3))) {
            *err = member == 0 ? "not a gzip file" : "something that isn't gzip after the compressed data";
            return false;
        }
        if (p + 10 > file.size()) { *err = "the gzip header is cut short"; return false; }
        const int flags = uchar(file.constData()[p + 3]);
        if (flags & 0xE0) { *err = "a gzip header with flags this program doesn't know"; return false; }
        qint64 q = p + 10;
        if (flags & 4) {                                 // FEXTRA
            if (q + 2 > file.size()) { *err = "the gzip header is cut short"; return false; }
            q += 2 + (uchar(file.constData()[q]) | uchar(file.constData()[q + 1]) << 8);
        }
        for (int bit : {8, 16}) {                        // FNAME, FCOMMENT: zero-terminated
            if (!(flags & bit)) continue;
            const qint64 z = file.indexOf('\0', q);
            if (z < 0) { *err = "the gzip header is cut short"; return false; }
            q = z + 1;
        }
        if (flags & 2) q += 2;                           // FHCRC
        if (q > file.size()) { *err = "the gzip header is cut short"; return false; }
        QByteArray part;
        QString e;
        if (!inflate(file, &q, &part, &e, maxOut - out->size())) { *err = "gzip: " + e; return false; }
        if (q + 8 > file.size()) { *err = "the gzip file ends before its check"; return false; }
        if (le32(file, q) != crc32(part)) { *err = "the gzip file's CRC is wrong: it is damaged"; return false; }
        if (le32(file, q + 4) != quint32(part.size())) { *err = "the gzip file's length check is wrong: it is damaged"; return false; }
        out->append(part);
        p = q + 8;
        ++member;
    }
    if (member == 0) { *err = "an empty gzip file"; return false; }
    return true;
}

QByteArray gzip(const QByteArray &data) {
    // qCompress: a 4-byte big-endian length, then a zlib stream (2-byte header, deflate data, 4-byte Adler-32).
    const QByteArray z = qCompress(data, 9);
    const QByteArray deflate = data.isEmpty() ? QByteArray("\x03\x00", 2)   // qCompress gives no stream for nothing
                                              : z.mid(6, z.size() - 10);
    QByteArray out;
    out.append("\x1F\x8B\x08\x00", 4);                   // ID, deflate, no flags
    out.append(QByteArray(4, '\0'));                     // no time
    out.append(char(2));                                 // XFL: the slowest, best compression
    out.append(char(0xFF));                              // OS: unknown
    out.append(deflate);
    putLe32(&out, crc32(data));
    putLe32(&out, quint32(data.size()));
    return out;
}

} // namespace tape
} // namespace dw
