/*
 * Disc image containers - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
// container.cpp - opening, raw files, staging, the physical and stored views, and saving.
// (The ImageDisk, DSK and TeleDisk formats themselves are in container_formats.cpp.)
#include "container.h"
#include "safesave.h"

#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <cstring>

namespace dw {
namespace device {

QString kindName(Kind k) {
    switch (k) {
    case Kind::Raw: return "raw sector image";
    case Kind::Imd: return "ImageDisk (.IMD)";
    case Kind::Dsk: return "standard DSK image";
    case Kind::Edsk: return "extended DSK image";
    case Kind::Td0: return "TeleDisk (.TD0)";
    default: return "none";
    }
}

QString kindExtension(Kind k) {
    switch (k) {
    case Kind::Raw: return "img";
    case Kind::Imd: return "imd";
    case Kind::Dsk: case Kind::Edsk: return "dsk";
    case Kind::Td0: return "td0";
    default: return QString();
    }
}

static int sizeCode(int size) {
    for (int n = 0; n <= 7; ++n) if ((128 << n) == size) return n;
    return -1;
}

// ------------------------------------------------------------------------------------------- opening

Container::Container() = default;
Container::~Container() = default;
Container::Container(const Container &o) = default;
Container &Container::operator=(const Container &o) = default;

void Container::close() { *this = Container(); }

bool Container::open(const QString &path, QString *err) {
    close();
    auto f = std::make_shared<QFile>(path);
    if (!f->open(QIODevice::ReadOnly)) { *err = f->errorString(); return false; }
    const qint64 size = f->size();
    if (size == 0) { *err = "the file is empty"; return false; }
    QByteArray head = f->read(64);
    m_path = path;
    if (head.startsWith("IMD ")) m_kind = Kind::Imd;
    else if (head.startsWith("EXTENDED CPC DSK File")) m_kind = Kind::Edsk;
    else if (head.startsWith("MV - CPC")) m_kind = Kind::Dsk;
    else if (head.size() >= 12 && ((head[0] == 'T' && head[1] == 'D') || (head[0] == 't' && head[1] == 'd')) &&
             head[2] == char(0) && quint8(head[9]) <= 2)
        m_kind = Kind::Td0;
    else m_kind = Kind::Raw;
    if (m_kind == Kind::Raw) {                   // read as needed: a hard disc image can be hundreds of MB
        m_rawFile = f;
        m_rawFileSize = m_fileBacked = size;
        m_padSize = 0;
        return true;
    }
    if (size > 64LL * 1024 * 1024) { *err = "the file is too large for a " + kindName(m_kind); close(); return false; }
    f->seek(0);
    QByteArray b = f->readAll();
    f->close();
    bool ok = false;
    switch (m_kind) {
    case Kind::Imd: ok = parseImd(b, err); break;
    case Kind::Edsk: case Kind::Dsk: ok = parseDsk(b, err); break;
    case Kind::Td0: ok = parseTd0(b, err); break;
    default: break;
    }
    if (!ok) { close(); return false; }
    m_file = b;
    index();
    return true;
}

void Container::createBlank(Kind kind, const RawLayout &l, const QString &comment) {
    close();
    m_kind = kind;
    m_comment = comment;
    m_layout = l;
    if (kind == Kind::Raw) {                     // nothing in a file yet: every byte is the E5 padding
        m_rawFileSize = l.bytes();
        m_fileBacked = 0;
        QString e;
        setRawLayout(l, &e);
        m_forceSave = true;
        return;
    }
    for (int c = 0; c < l.cyls; ++c)
        for (int h = 0; h < l.heads; ++h) {
            Track t;
            t.cyl = c;
            t.head = h;
            t.mode = l.mode;
            for (int s = 0; s < l.sectrk; ++s) {
                Sector x;
                x.c = quint8(c); x.h = quint8(h); x.r = quint8(l.firstId + s); x.n = quint8(qMax(0, sizeCode(l.seclen)));
                x.size = l.seclen;
                x.data = QByteArray(l.seclen, char(0xE5));
                x.imdType = 2;
                t.secs.append(x);
            }
            m_tracks.append(t);
        }
    index();
    m_forceSave = true;
}

bool Container::reshapeTrack(int cyl, int head, int sectrk, int seclen, int firstId, int mode, QString *err) {
    if (m_kind == Kind::Raw) { *err = "a raw image can't hold tracks of different shapes: use ImageDisk or DSK"; return false; }
    auto it = m_trackIndex.find(key(cyl, head, 0));
    if (it == m_trackIndex.end()) { *err = "no such track"; return false; }
    Track &t = m_tracks[it.value()];
    t.secs.clear();
    t.imdRaw.clear();
    t.mode = mode;
    for (int s = 0; s < sectrk; ++s) {
        Sector x;
        x.c = quint8(cyl); x.h = quint8(head); x.r = quint8(firstId + s); x.n = quint8(qMax(0, sizeCode(seclen)));
        x.size = seclen;
        x.data = QByteArray(seclen, char(0xE5));
        x.imdType = 2;
        t.secs.append(x);
    }
    index();
    m_forceSave = true;
    return true;
}

bool Container::setRawLayout(const RawLayout &l, QString *err) {
    if (m_kind != Kind::Raw) { *err = "not a raw image"; return false; }
    if (!l.valid()) { *err = "invalid geometry"; return false; }
    if (l.bytes() > 512LL * 1024 * 1024) { *err = "geometry too large"; return false; }
    m_layout = l;
    m_padSize = l.bytes();
    m_padByte = char(0xE5);
    m_tracks.clear();
    int sc = sizeCode(l.seclen);
    for (int c = 0; c < l.cyls; ++c)
        for (int h = 0; h < l.heads; ++h) {
            Track t;
            t.cyl = c;
            t.head = h;
            t.mode = l.mode;
            bool t0 = c == 0 && l.t0sectrk > 0;
            int n = t0 ? l.t0sectrk : l.sectrk, size = t0 ? l.t0seclen : l.seclen;
            for (int s = 0; s < n; ++s) {
                Sector x;
                x.c = quint8(c); x.h = quint8(h); x.r = quint8(l.firstId + s); x.n = quint8(qMax(0, t0 ? sizeCode(size) : sc));
                x.size = size;
                x.fileOffset = l.trackOffset(c, h) + qint64(s) * size;
                t.secs.append(x);
            }
            m_tracks.append(t);
        }
    index();
    return true;
}

void Container::setRawPadding(qint64 size, char fill) {
    m_padSize = size;
    m_padByte = fill;
}

void Container::index() {
    m_trackIndex.clear();
    m_cyls = m_heads = 0;
    for (int i = 0; i < m_tracks.size(); ++i) {
        Track &t = m_tracks[i];
        m_trackIndex[key(t.cyl, t.head, 0)] = i;
        m_cyls = qMax(m_cyls, t.cyl + 1);
        m_heads = qMax(m_heads, t.head + 1);
        t.byId.clear();
        for (int s = 0; s < t.secs.size(); ++s) t.byId.append(s);
        std::stable_sort(t.byId.begin(), t.byId.end(), [&t](int a, int b) { return t.secs[a].r < t.secs[b].r; });
        t.duplicateIds = false;
        for (int s = 1; s < t.byId.size(); ++s)
            if (t.secs[t.byId[s]].r == t.secs[t.byId[s - 1]].r) t.duplicateIds = true;
    }
}

// ------------------------------------------------------------------------------------------- raw files

QByteArray Container::rawFileBytes(qint64 off, qint64 len) const {
    QByteArray out;
    if (m_kind != Kind::Raw || off < 0 || len <= 0 || !m_rawFile || off >= m_fileBacked) return out;
    len = qMin(len, m_fileBacked - off);
    if (!m_rawFile->seek(off)) return out;
    return m_rawFile->read(len);
}

QByteArray Container::rawBytes() const {
    if (m_kind != Kind::Raw) return QByteArray();
    QByteArray b = rawFileBytes(0, m_fileBacked);
    if (b.size() < m_rawFileSize) b.append(QByteArray(int(m_rawFileSize - b.size()), m_padByte));   // a new blank disc
    return b;
}

// The bytes as a save would store them: the file, its padding, and every staged block over them.
QByteArray Container::rawRange(qint64 off, qint64 len) const {
    QByteArray out = rawFileBytes(off, len);
    if (out.size() < len) out.append(QByteArray(int(len - out.size()), m_padByte));
    if (m_blocks.isEmpty()) return out;
    const qint64 first = off / RawBlock, last = (off + len - 1) / RawBlock;
    for (auto it = m_blocks.lowerBound(first); it != m_blocks.end() && it.key() <= last; ++it) {
        const qint64 bs = it.key() * RawBlock;
        const qint64 lo = qMax(off, bs), hi = qMin(off + len, bs + RawBlock);
        memcpy(out.data() + (lo - off), it.value().constData() + (lo - bs), size_t(hi - lo));
    }
    return out;
}

void Container::stageRaw(qint64 off, const QByteArray &data, bool dropIdentical) {
    const qint64 end = off + data.size();
    if (end > qMax(m_rawFileSize, m_padSize)) m_rawEnd = qMax(m_rawEnd, end);   // the file grows to hold it
    for (qint64 b = off / RawBlock; b * RawBlock < end; ++b) {
        const qint64 bs = b * RawBlock;
        QByteArray blk = rawRange(bs, RawBlock);
        const qint64 lo = qMax(off, bs), hi = qMin(end, bs + RawBlock);
        memcpy(blk.data() + (lo - bs), data.constData() + (lo - off), size_t(hi - lo));
        if (dropIdentical) {
            QByteArray orig = rawFileBytes(bs, RawBlock);
            if (orig.size() < RawBlock) orig.append(QByteArray(RawBlock - orig.size(), m_padByte));
            if (blk == orig) { m_blocks.remove(b); continue; }
        }
        m_blocks[b] = blk;
    }
}

qint64 Container::rawOutputSize() const {
    return qMax(qMax(m_rawFileSize, m_padSize), m_rawEnd);
}

bool Container::streamRaw(QIODevice *dev, QString *err) const {
    const qint64 total = rawOutputSize();
    const qint64 chunk = 1 << 20;
    for (qint64 off = 0; off < total; off += chunk) {
        const qint64 n = qMin(chunk, total - off);
        QByteArray b = rawRange(off, n);
        if (b.size() != n) { *err = "could not read the original image"; return false; }
        if (dev->write(b) != n) { *err = dev->errorString(); return false; }
    }
    return true;
}

// ------------------------------------------------------------------------------------------- the bytes as stored

bool Container::uniformTracks(int *trackBytes, int *seclen, QString *why) const {
    const Track *t0 = track(0, 0);
    if (!t0 || t0->secs.isEmpty()) { if (why) *why = "the image has no first track"; return false; }
    const int n = t0->secs.size(), size = t0->secs[0].size;
    for (int c = 0; c < m_cyls; ++c)
        for (int h = 0; h < m_heads; ++h) {
            const Track *t = track(c, h);
            if (!t || t->secs.size() != n) { if (why) *why = "the image's tracks don't all have the same shape"; return false; }
            for (const Sector &s : t->secs)
                if (s.size != size) { if (why) *why = "the image's tracks don't all have the same shape"; return false; }
        }
    *trackBytes = n * size;
    *seclen = size;
    return true;
}

qint64 Container::storedSize(QString *why) const {
    if (m_kind == Kind::None) { if (why) *why = "no image"; return 0; }
    if (m_kind == Kind::Raw) return qMax(m_rawFileSize, m_padSize);
    int tb = 0, sl = 0;
    if (!uniformTracks(&tb, &sl, why)) return 0;
    return qint64(tb) * m_cyls * m_heads;
}

bool Container::readStored(qint64 off, qint64 len, QByteArray *out, QString *err) const {
    out->clear();
    if (off < 0 || len < 0) { *err = "bad position"; return false; }
    if (len == 0) return true;
    if (m_kind == Kind::Raw) { *out = rawRange(off, len); return true; }
    int tb = 0, sl = 0;
    if (!uniformTracks(&tb, &sl, err)) return false;
    if (off + len > qint64(tb) * m_cyls * m_heads) { *err = "read beyond the end of the disc"; return false; }
    while (len > 0) {
        const qint64 t = off / tb, in = off % tb;
        const int c = int(t / m_heads), h = int(t % m_heads), idx = int(in / sl), at = int(in % sl);
        QByteArray s;
        if (!read(c, h, idx, &s) || s.size() < sl) {
            *err = QString("cylinder %1 head %2 sector %3 can't be read").arg(c).arg(h).arg(idx);
            return false;
        }
        const qint64 n = qMin<qint64>(len, sl - at);
        out->append(s.constData() + at, int(n));
        off += n; len -= n;
    }
    return true;
}

bool Container::writeStored(qint64 off, const QByteArray &data, QString *err) {
    if (!m_writable) { *err = "image is read-only"; return false; }
    if (off < 0) { *err = "bad position"; return false; }
    if (data.isEmpty()) return true;
    if (m_kind == Kind::Raw) { stageRaw(off, data, false); return true; }
    int tb = 0, sl = 0;
    if (!uniformTracks(&tb, &sl, err)) return false;
    qint64 len = data.size(), pos = 0;
    if (off + len > qint64(tb) * m_cyls * m_heads) { *err = "write beyond the end of the disc"; return false; }
    while (len > 0) {
        const qint64 t = off / tb, in = off % tb;
        const int c = int(t / m_heads), h = int(t % m_heads), idx = int(in / sl), at = int(in % sl);
        const qint64 n = qMin<qint64>(len, sl - at);
        QByteArray s;
        if (n < sl && (!read(c, h, idx, &s) || s.size() < sl)) {
            *err = QString("cylinder %1 head %2 sector %3 can't be read").arg(c).arg(h).arg(idx);
            return false;
        }
        if (n == sl) s = data.mid(int(pos), sl);
        else memcpy(s.data() + at, data.constData() + pos, size_t(n));
        if (!write(c, h, idx, s, err)) return false;
        off += n; len -= n; pos += n;
    }
    return true;
}

bool Container::storedTouched(qint64 off, qint64 len) const {
    if (len <= 0) return false;
    if (m_kind == Kind::Raw) {
        auto it = m_blocks.lowerBound(off / RawBlock);
        return it != m_blocks.end() && it.key() * RawBlock < off + len;
    }
    int tb = 0, sl = 0;
    if (!uniformTracks(&tb, &sl, nullptr)) return !m_pending.isEmpty();
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        const int c = int(it.key() >> 16), h = int((it.key() >> 8) & 0xFF), i = int(it.key() & 0xFF);
        const qint64 s = (qint64(c) * m_heads + h) * tb + qint64(i) * sl;
        if (s < off + len && off < s + sl) return true;
    }
    return false;
}

void Container::discardStored(qint64 off, qint64 len) {
    if (len <= 0) return;
    if (m_kind == Kind::Raw) {
        for (qint64 b = (off + RawBlock - 1) / RawBlock; (b + 1) * RawBlock <= off + len; ++b) m_blocks.remove(b);
        return;
    }
    int tb = 0, sl = 0;
    if (!uniformTracks(&tb, &sl, nullptr)) return;
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        const int c = int(it.key() >> 16), h = int((it.key() >> 8) & 0xFF), i = int(it.key() & 0xFF);
        const qint64 s = (qint64(c) * m_heads + h) * tb + qint64(i) * sl;
        if (s >= off && s + sl <= off + len) it = m_pending.erase(it);
        else ++it;
    }
}

// ------------------------------------------------------------------------------------------- physical access

const Track *Container::track(int cyl, int head) const {
    auto it = m_trackIndex.find(key(cyl, head, 0));
    return it == m_trackIndex.end() ? nullptr : &m_tracks[it.value()];
}

int Container::sectorCount(int cyl, int head) const {
    const Track *t = track(cyl, head);
    return t ? t->secs.size() : 0;
}

const Sector *Container::sector(int cyl, int head, int idx) const {
    const Track *t = track(cyl, head);
    if (!t || idx < 0 || idx >= t->byId.size()) return nullptr;
    return &t->secs[t->byId[idx]];
}

int Container::indexOfId(int cyl, int head, int sectorId) const {
    const Track *t = track(cyl, head);
    if (!t) return -1;
    for (int i = 0; i < t->byId.size(); ++i)
        if (t->secs[t->byId[i]].r == sectorId) return i;
    return -1;
}

bool Container::sectorUsable(int cyl, int head, int idx) const {
    const Track *t = track(cyl, head);
    const Sector *s = sector(cyl, head, idx);
    if (!s || s->missing || s->crcError || t->duplicateIds) return false;
    return m_kind == Kind::Raw || s->data.size() == s->size;
}

QByteArray Container::sectorData(int ti, int si) const {
    const Track &t = m_tracks[ti];
    const Sector &s = t.secs[si];
    if (m_kind == Kind::Raw) return rawRange(s.fileOffset, s.size);
    int idx = t.byId.indexOf(si);
    auto it = m_pending.find(key(t.cyl, t.head, idx));
    return it != m_pending.end() ? it.value() : s.data;
}

bool Container::read(int cyl, int head, int idx, QByteArray *out) const {
    const Sector *s = sector(cyl, head, idx);
    if (m_kind == Kind::Raw) {
        if (!s) return false;
        *out = rawRange(s->fileOffset, s->size);
        return true;
    }
    auto it = m_pending.find(key(cyl, head, idx));
    if (it != m_pending.end()) { *out = it.value(); return true; }
    if (!s || s->missing || s->data.isEmpty()) return false;
    *out = s->data;
    return true;
}

bool Container::write(int cyl, int head, int idx, const QByteArray &data, QString *err) {
    if (!m_writable) { *err = "image is read-only"; return false; }
    const Sector *s = sector(cyl, head, idx);
    if (!s) { *err = QString("cylinder %1 head %2 has no sector %3").arg(cyl).arg(head).arg(idx); return false; }
    if (!sectorUsable(cyl, head, idx)) {
        *err = QString("sector ID %1 on cylinder %2 head %3 is bad or protected and can't be written").arg(s->r).arg(cyl).arg(head);
        return false;
    }
    QByteArray d = data.left(s->size);
    if (d.size() < s->size) d.append(QByteArray(s->size - d.size(), char(0xE5)));
    if (m_kind == Kind::Raw) { stageRaw(s->fileOffset, d, true); return true; }
    if (d == s->data) m_pending.remove(key(cyl, head, idx));      // back to what the file holds
    else m_pending[key(cyl, head, idx)] = d;
    return true;
}

Shape Container::shape() const {
    Shape sh;
    sh.allHeads = m_heads;
    QMap<quint64, int> counts;                         // (sectrk, seclen) -> tracks
    for (const Track &t : m_tracks) {
        if (t.secs.isEmpty()) continue;
        bool same = true;
        for (const Sector &s : t.secs) if (s.size != t.secs[0].size) same = false;
        if (!same) continue;
        counts[quint64(t.secs.size()) << 32 | quint64(t.secs[0].size)]++;
    }
    quint64 best = 0;
    int bestN = -1;
    for (auto it = counts.begin(); it != counts.end(); ++it) {
        int bytes = it.value() * int(it.key() >> 32) * int(it.key() & 0xFFFFFFFF);
        if (bytes > bestN) { bestN = bytes; best = it.key(); }
    }
    sh.sectrk = int(best >> 32);
    sh.seclen = int(best & 0xFFFFFFFF);
    sh.firstId = 256;
    for (const Track &t : m_tracks) {
        bool common = t.secs.size() == sh.sectrk && !t.secs.isEmpty();
        for (const Sector &s : t.secs) if (s.size != sh.seclen) common = false;
        if (common) {
            sh.uniformTracks++;
            sh.heads = qMax(sh.heads, t.head + 1);
            sh.cyls = qMax(sh.cyls, t.cyl + 1);
            for (const Sector &s : t.secs) sh.firstId = qMin(sh.firstId, int(s.r));
        } else {
            sh.oddTracks++;
        }
        for (const Sector &s : t.secs) if (s.missing || s.crcError) sh.badSectors++;
        if (t.duplicateIds) sh.badSectors++;
    }
    if (sh.firstId == 256) sh.firstId = 0;
    if (const Track *t0 = track(0, 0))
        for (int i : t0->byId) sh.idsOfFirstTrack.append(t0->secs[i].r);
    return sh;
}

// ------------------------------------------------------------------------------------------- saving

bool Container::writeRawFromTracks(QByteArray *out, QString *err) const {
    Shape sh = shape();
    if (sh.oddTracks || sh.uniformTracks != m_cyls * m_heads || sh.badSectors) {
        *err = "the disc has tracks of different shapes, missing tracks or bad sectors, which a raw image can't hold";
        return false;
    }
    QByteArray o;
    for (int c = 0; c < m_cyls; ++c)
        for (int h = 0; h < m_heads; ++h) {
            const Track *t = track(c, h);
            for (int i = 0; i < t->byId.size(); ++i) o += sectorData(int(t - m_tracks.constData()), t->byId[i]);
        }
    *out = o;
    return true;
}

bool Container::serialize(Kind as, QByteArray *out, QString *err) const {
    switch (as) {
    case Kind::Imd: return writeImd(out, err);
    case Kind::Dsk: return writeDsk(out, false, err);
    case Kind::Edsk: return writeDsk(out, true, err);
    case Kind::Raw:
        if (m_kind == Kind::Raw) {
            const qint64 n = rawOutputSize();
            if (n > 512LL * 1024 * 1024) { *err = "the image is too large to build in memory"; return false; }
            *out = rawRange(0, n);
            return true;
        }
        return writeRawFromTracks(out, err);
    case Kind::Td0: *err = "TeleDisk images can't be written; use Save As to save as ImageDisk, DSK or raw"; return false;
    default: *err = "no image"; return false;
    }
}

// Opens a file just written as this container would be: a raw file gets the same layout and padding.
bool Container::reopenAs(const QString &path, Kind as, const RawLayout &l, Container *out, QString *err) const {
    if (!out->open(path, err)) return false;
    if (out->kind() != as) { *err = "the file written does not read back as a " + kindName(as); return false; }
    if (as == Kind::Raw) {
        if (l.valid() && !out->setRawLayout(l, err)) return false;
        if (m_kind == Kind::Raw && !l.valid()) out->setRawPadding(m_padSize, m_padByte);
    }
    return true;
}

// Reads back what was written. Same container: every staged write, and the size. Another container: every sector.
bool Container::checkWritten(const Container &w, Kind as, QString *err) const {
    if (m_kind == Kind::Raw && as == Kind::Raw) {
        if (w.rawFileSize() != rawOutputSize()) { *err = "the new file has the wrong size"; return false; }
        for (auto it = m_blocks.begin(); it != m_blocks.end(); ++it)
            if (w.rawFileBytes(it.key() * RawBlock, RawBlock) != it.value().left(int(qMin<qint64>(RawBlock, w.rawFileSize() - it.key() * RawBlock)))) {
                *err = QString("the new file does not hold what was written at byte %1").arg(it.key() * RawBlock);
                return false;
            }
        return true;
    }
    if (as == m_kind) {
        for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
            const int c = int(it.key() >> 16), h = int((it.key() >> 8) & 0xFF), i = int(it.key() & 0xFF);
            QByteArray d;
            if (!w.read(c, h, i, &d) || d != it.value()) {
                *err = QString("the new file does not hold what was written to cylinder %1 head %2 sector %3").arg(c).arg(h).arg(i);
                return false;
            }
        }
        return true;
    }
    for (int c = 0; c < m_cyls; ++c)
        for (int h = 0; h < m_heads; ++h)
            for (int i = 0; i < sectorCount(c, h); ++i) {
                QByteArray a, b;
                const bool ra = read(c, h, i, &a), rb = w.read(c, h, i, &b);
                if (ra && (!rb || b.left(a.size()) != a)) {
                    *err = QString("the new file does not hold cylinder %1 head %2 sector %3").arg(c).arg(h).arg(i);
                    return false;
                }
            }
    return true;
}

bool Container::writeChecked(const QString &path, Kind as, const RawLayout &l, bool backup,
                             const std::function<bool(Container &, QString *)> &verify, Container *fresh, QString *err) {
    // the new contents, or for a raw file a way to stream them
    QByteArray bytes;
    const bool stream = m_kind == Kind::Raw && as == Kind::Raw;
    if (!stream && !serialize(as, &bytes, err)) return false;
    const qint64 size = stream ? rawOutputSize() : bytes.size();

    SafeSave save(path);
    if (!save.begin(size, err)) return false;
    bool ok = stream ? streamRaw(save.device(), err)
                     : (save.device()->write(bytes) == bytes.size() || (*err = save.device()->errorString(), false));
    if (!ok || !save.finishWriting(err)) { save.abandon(); return false; }

    {   // read it back, then let the program check it with its own code
        Container w;
        if (!reopenAs(save.tempPath(), as, l, &w, err) || !checkWritten(w, as, err) || (verify && !verify(w, err))) {
            save.abandon();
            return false;
        }
    }
    if (m_rawFile && path == m_path) m_rawFile->close();   // Windows can't replace a file we hold open
    if (!save.replace(backup, err)) {
        if (m_rawFile && path == m_path && !m_rawFile->isOpen()) m_rawFile->open(QIODevice::ReadOnly);
        return false;
    }
    return reopenAs(path, as, l, fresh, err);
}

bool Container::commit(const SaveOptions &o, QString *err) {
    if (m_kind == Kind::Td0) { *err = "TeleDisk images are read-only here: use Save As"; return false; }
    if (m_path.isEmpty()) { *err = "the image has no file yet: use Save As"; return false; }
    if (o.validate && !o.validate(err)) return false;
    Container fresh;
    if (!writeChecked(m_path, m_kind, m_layout, o.backup, o.verify, &fresh, err)) return false;
    // the new file is the image from now on (so the in-memory state is exactly the file)
    const qint64 pad = m_padSize;
    const char fill = m_padByte;
    const bool wr = m_writable;
    *this = fresh;
    if (m_kind == Kind::Raw && !m_layout.valid()) setRawPadding(pad, fill);
    m_writable = wr;
    return true;
}

bool Container::saveAs(const QString &path, Kind as, QString *err, const std::function<bool(Container &, QString *)> &verify) {
    if (m_kind == Kind::None) { *err = "no image"; return false; }
    RawLayout l = m_layout;
    if (as == Kind::Raw && m_kind != Kind::Raw) {
        Shape sh = shape();
        l.seclen = sh.seclen; l.sectrk = sh.sectrk; l.cyls = m_cyls; l.heads = m_heads; l.firstId = sh.firstId;
        l.t0sectrk = l.t0seclen = 0;
        l.sideBySide = false;
        if (!m_tracks.isEmpty()) l.mode = m_tracks[0].mode;
    }
    const RawLayout readBack = as == Kind::Raw ? l : RawLayout();
    Container fresh;
    if (!writeChecked(path, as, readBack, false, verify, &fresh, err)) return false;
    const qint64 pad = m_padSize;
    const char fill = m_padByte;
    const bool wasRaw = m_kind == Kind::Raw;
    *this = fresh;
    if (as != Kind::Raw) m_layout = l;
    else if (wasRaw && !l.valid()) setRawPadding(pad, fill);
    return true;
}

} // namespace device
} // namespace dw
