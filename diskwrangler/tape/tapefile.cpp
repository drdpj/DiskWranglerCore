/*
 * Tape image files - part of DiskWranglerCore
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
#include "tapefile.h"
#include "gzip.h"
#include <QFile>
#include <QFileInfo>
#include <diskwrangler/device/safesave.h>

namespace dw {
namespace tape {

namespace {

const char UefMagic[10] = {'U', 'E', 'F', ' ', 'F', 'i', 'l', 'e', '!', '\0'};
const char TzxMagic[8] = {'Z', 'X', 'T', 'a', 'p', 'e', '!', '\x1A'};

quint32 le(const QByteArray &b, qint64 at, int n) {
    quint32 v = 0;
    for (int i = 0; i < n; ++i) v |= quint32(uchar(b.constData()[at + i])) << (8 * i);
    return v;
}

void putLe(QByteArray *b, quint32 v, int n) {
    for (int i = 0; i < n; ++i) b->append(char((v >> (8 * i)) & 0xFF));
}

// A TAP file's blocks run exactly to its end.
bool tapChain(const QByteArray &f) {
    if (f.size() < 2) return false;
    qint64 p = 0;
    while (p + 2 <= f.size()) p += 2 + le(f, p, 2);
    return p == f.size();
}

bool readFile(const QString &path, QByteArray *out, QString *err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { *err = f.errorString(); return false; }
    if (f.size() > 64 * 1024 * 1024) { *err = "too big for a tape image"; return false; }
    *out = f.readAll();
    return true;
}

} // namespace

QString kindName(Kind k, bool gzip) {
    switch (k) {
    case Kind::Uef: return gzip ? "UEF (gzip)" : "UEF";
    case Kind::Tzx: return "TZX";
    case Kind::Tap: return "TAP";
    case Kind::None: break;
    }
    return "none";
}

Kind TapeFile::identify(const QByteArray &file, const QString &suffix, bool *gz) {
    bool packed = false;
    if (gz) *gz = false;
    QByteArray plain = file;
    if (isGzip(file)) {
        QString e;
        if (!gunzip(file, &plain, &e)) return Kind::None;
        packed = true;
    }
    if (plain.startsWith(QByteArray(UefMagic, 10)) && plain.size() >= 12) {
        if (gz) *gz = packed;
        return Kind::Uef;
    }
    if (packed) return Kind::None;                       // only UEF is ever compressed
    if (plain.startsWith(QByteArray(TzxMagic, 8)) && plain.size() >= 10) return Kind::Tzx;
    if (suffix.toLower() == "tap" && tapChain(plain)) return Kind::Tap;
    return Kind::None;
}

qint64 TapeFile::tzxBodyLength(int id, const QByteArray &b, qint64 at) {
    auto have = [&](qint64 n) { return at + n <= b.size(); };
    auto u8 = [&](qint64 o) { return qint64(uchar(b.constData()[at + o])); };
    auto u16 = [&](qint64 o) { return qint64(le(b, at + o, 2)); };
    auto u24 = [&](qint64 o) { return qint64(le(b, at + o, 3)); };
    auto u32 = [&](qint64 o) { return qint64(le(b, at + o, 4)); };
    qint64 n = -1;
    switch (id) {
    case 0x10: if (have(4)) n = 4 + u16(2); break;
    case 0x11: if (have(0x12)) n = 0x12 + u24(0x0F); break;
    case 0x12: n = 4; break;
    case 0x13: if (have(1)) n = 1 + 2 * u8(0); break;
    case 0x14: if (have(0x0A)) n = 0x0A + u24(7); break;
    case 0x15: if (have(8)) n = 8 + u24(5); break;
    // 16, 17 (C64, deprecated in 1.20): "length [00,01,02,03]", the whole block's length in its first 4 bytes.
    case 0x16: case 0x17: if (have(4)) n = u32(0) < 4 ? -1 : u32(0); break;
    case 0x18: case 0x19: if (have(4)) n = 4 + u32(0); break;
    case 0x20: case 0x23: case 0x24: n = 2; break;
    case 0x21: case 0x30: if (have(1)) n = 1 + u8(0); break;
    case 0x22: case 0x25: case 0x27: n = 0; break;
    case 0x26: if (have(2)) n = 2 + 2 * u16(0); break;
    case 0x28: case 0x32: if (have(2)) n = 2 + u16(0); break;
    case 0x2A: case 0x2B: if (have(4)) n = 4 + u32(0); break;
    case 0x31: if (have(2)) n = 2 + u8(1); break;
    case 0x33: if (have(1)) n = 1 + 3 * u8(0); break;
    case 0x34: n = 8; break;
    case 0x35: if (have(0x14)) n = 0x14 + u32(0x10); break;
    case 0x40: if (have(4)) n = 4 + u24(1); break;
    case 0x5A: n = 9; break;
    default:                                             // the extension rule: a later block's length comes first
        if (have(4)) n = 4 + u32(0);
        break;
    }
    if (n < 0 || at + n > b.size()) return -1;
    return n;
}

QVector<int> TapeFile::tzxOffsets(const Chunk &c) {
    QVector<int> o;
    const QByteArray &b = c.body;
    auto s16 = [&](int at) { return int(qint16(le(b, at, 2))); };
    if (c.id == 0x23 && b.size() >= 2) o << s16(0);
    else if (c.id == 0x26 && b.size() >= 2) {
        const int n = int(le(b, 0, 2));
        for (int i = 0; i < n && 2 + 2 * i + 2 <= b.size(); ++i) o << s16(2 + 2 * i);
    } else if (c.id == 0x28 && b.size() >= 3) {
        const int n = uchar(b.constData()[2]);
        int p = 3;
        for (int i = 0; i < n && p + 3 <= b.size(); ++i) {
            o << s16(p);
            p += 3 + uchar(b.constData()[p + 2]);
        }
    }
    return o;
}

Chunk TapeFile::tzxWithOffsets(const Chunk &c, const QVector<int> &off) {
    Chunk r = c;
    auto put = [&](int at, int v) { r.body[at] = char(v & 0xFF); r.body[at + 1] = char((v >> 8) & 0xFF); };
    if (c.id == 0x23 && !off.isEmpty()) put(0, off[0]);
    else if (c.id == 0x26) for (int i = 0; i < off.size(); ++i) put(2 + 2 * i, off[i]);
    else if (c.id == 0x28) {
        int p = 3;
        for (int i = 0; i < off.size() && p + 3 <= r.body.size(); ++i) {
            put(p, off[i]);
            p += 3 + uchar(r.body[p + 2]);
        }
    }
    return r;
}

bool TapeFile::parse(const QByteArray &plain, QString *err) {
    m_chunks.clear();
    m_readOnlyWhy.clear();
    if (m_kind == Kind::Uef) {
        m_minor = uchar(plain.constData()[10]);
        m_major = uchar(plain.constData()[11]);
        qint64 p = 12;
        while (p < plain.size()) {
            if (p + 6 > plain.size() || p + 6 + qint64(le(plain, p + 2, 4)) > plain.size()) {
                m_chunks.append(Chunk(Chunk::Tail, plain.mid(p)));
                m_readOnlyWhy = QString("the file ends inside a chunk (at byte %1 of the tape)").arg(p);
                break;
            }
            const qint64 len = le(plain, p + 2, 4);
            m_chunks.append(Chunk(int(le(plain, p, 2)), plain.mid(p + 6, len)));
            p += 6 + len;
        }
    } else if (m_kind == Kind::Tzx) {
        m_major = uchar(plain.constData()[8]);
        m_minor = uchar(plain.constData()[9]);
        if (m_major != 1) { *err = QString("TZX version %1.%2: only version 1 can be read").arg(m_major).arg(m_minor); return false; }
        qint64 p = 10;
        while (p < plain.size()) {
            const int id = uchar(plain.constData()[p]);
            const qint64 len = tzxBodyLength(id, plain, p + 1);
            if (len < 0) {
                m_chunks.append(Chunk(Chunk::Tail, plain.mid(p)));
                m_readOnlyWhy = QString("block %1 (ID %2) is cut short or can't be read")
                                    .arg(m_chunks.size()).arg(QString::number(id, 16).rightJustified(2, '0').toUpper());
                break;
            }
            m_chunks.append(Chunk(id, plain.mid(p + 1, len)));
            p += 1 + len;
        }
    } else if (m_kind == Kind::Tap) {
        qint64 p = 0;
        while (p < plain.size()) {
            const qint64 len = p + 2 <= plain.size() ? le(plain, p, 2) : -1;
            if (len < 0 || p + 2 + len > plain.size()) {
                m_chunks.append(Chunk(Chunk::Tail, plain.mid(p)));
                m_readOnlyWhy = QString("block %1 is cut short").arg(m_chunks.size());
                break;
            }
            m_chunks.append(Chunk(0, plain.mid(p + 2, len)));
            p += 2 + len;
        }
    }
    m_saved = m_chunks;
    return true;
}

bool TapeFile::open(const QString &path, QString *err) {
    QByteArray f;
    if (!readFile(path, &f, err)) return false;
    return openBytes(f, path, QFileInfo(path).suffix(), err);
}

bool TapeFile::openBytes(const QByteArray &file, const QString &path, const QString &suffix, QString *err) {
    close();
    bool gz = false;
    QByteArray plain = file;
    if (isGzip(file)) {
        QString e;
        if (!gunzip(file, &plain, &e)) { *err = e; return false; }
        gz = true;
    }
    Kind k = Kind::None;
    if (plain.startsWith(QByteArray(UefMagic, 10)) && plain.size() >= 12) k = Kind::Uef;
    else if (!gz && plain.startsWith(QByteArray(TzxMagic, 8)) && plain.size() >= 10) k = Kind::Tzx;
    else if (!gz && suffix.toLower() == "tap" && tapChain(plain)) k = Kind::Tap;
    if (k == Kind::None) { *err = gz ? "a gzip file that isn't a UEF tape" : "not a tape image (UEF, TZX, CDT or TAP)"; return false; }
    m_kind = k;
    m_path = path;
    m_suffix = suffix;
    m_gzip = m_savedGzip = gz;
    m_file = file;
    m_fileSize = file.size();
    if (!parse(plain, err)) { close(); return false; }
    return true;
}

void TapeFile::createBlank(Kind kind, bool gz) {
    close();
    m_kind = kind;
    m_gzip = m_savedGzip = kind == Kind::Uef && gz;
    if (kind == Kind::Uef) { m_major = 0; m_minor = 10; }
    else if (kind == Kind::Tzx) { m_major = 1; m_minor = 20; }
    m_forceSave = true;                                  // a new tape is saved even with nothing on it
}

void TapeFile::close() {
    m_kind = Kind::None;
    m_path.clear();
    m_suffix.clear();
    m_major = m_minor = 0;
    m_gzip = m_savedGzip = m_forceSave = false;
    m_fileSize = 0;
    m_file.clear();
    m_readOnlyWhy.clear();
    m_chunks.clear();
    m_saved.clear();
}

bool TapeFile::setChunks(const QVector<Chunk> &c, QString *err) {
    return replaceChunks(0, m_chunks.size(), c, err);
}

bool TapeFile::replaceChunks(int first, int count, const QVector<Chunk> &with, QString *err) {
    if (!isOpen()) { *err = "no tape"; return false; }
    if (!writable()) { *err = m_readOnlyWhy.isEmpty() ? "the tape is read-only" : "the tape is read-only: " + m_readOnlyWhy; return false; }
    if (first < 0 || count < 0 || first + count > m_chunks.size()) { *err = "no such chunks"; return false; }
    for (const Chunk &c : with) if (c.id == Chunk::Tail) { *err = "a damaged end can't be added to a tape"; return false; }
    QVector<Chunk> out = m_chunks.mid(0, first) + with + m_chunks.mid(first + count);
    if (m_kind == Kind::Tzx && first + count <= m_chunks.size()) {
        const int end = first + count, delta = with.size() - count;
        auto moved = [&](int i) { return i < first ? i : i >= end ? i + delta : -1; };
        // Loops: a loop start (24) and its end (25) must both be kept or both go. A loop is the 24 and the next 25.
        for (int i = 0; i < m_chunks.size(); ++i) {
            if (m_chunks[i].id != 0x24) continue;
            int j = i + 1;
            while (j < m_chunks.size() && m_chunks[j].id != 0x25) ++j;
            if (j >= m_chunks.size()) continue;
            const bool startGone = i >= first && i < end, endGone = j >= first && j < end;
            if (startGone != endGone) { *err = QString("the change would split the loop in blocks %1-%2").arg(i + 1).arg(j + 1); return false; }
            // Both kept: the change mustn't touch the blocks between them (the repeated ones), nor add any there.
            const bool inBody = count > 0 ? first < j && end > i + 1 : first > i && first <= j;
            if (!startGone && inBody) {
                *err = QString("the change is inside the loop in blocks %1-%2, which would repeat it").arg(i + 1).arg(j + 1);
                return false;
            }
        }
        // Blocks that point at others by a count of blocks.
        for (int i = 0; i < m_chunks.size(); ++i) {
            const QVector<int> off = tzxOffsets(m_chunks[i]);
            if (off.isEmpty()) continue;
            const int ni = moved(i);
            if (ni < 0) continue;                        // replaced: it goes
            QVector<int> fixed;
            for (int o : off) {
                const int t = i + o;
                int nt;
                if (t >= first && t < end) {
                    if (t != first) {
                        *err = QString("block %1 points at block %2, which the change takes away").arg(i + 1).arg(t + 1);
                        return false;
                    }
                    nt = first;                          // the start of what replaces it
                } else nt = moved(t);
                if (nt - ni < -32768 || nt - ni > 32767) { *err = "a jump would be too long"; return false; }
                fixed << nt - ni;
            }
            if (fixed != off) out[ni] = tzxWithOffsets(out[ni], fixed);
        }
    }
    m_chunks = out;
    return true;
}

std::shared_ptr<const void> TapeFile::stagedState() const {
    return std::make_shared<State>(State{m_chunks, m_gzip, m_forceSave});
}

void TapeFile::restoreStaged(const std::shared_ptr<const void> &s) {
    const State *st = static_cast<const State *>(s.get());
    if (!st) return;
    m_chunks = st->chunks;
    m_gzip = st->gzip;
    m_forceSave = st->forceSave;
}

QByteArray TapeFile::serialize(bool gz) const {
    if (m_kind == Kind::None) return QByteArray();
    if (!m_file.isEmpty() && m_chunks == m_saved && gz == m_savedGzip) return m_file;
    QByteArray b;
    if (m_kind == Kind::Uef) {
        b.append(UefMagic, 10);
        b.append(char(m_minor));
        b.append(char(m_major));
        for (const Chunk &c : m_chunks) {
            if (c.id == Chunk::Tail) { b.append(c.body); continue; }
            putLe(&b, quint32(c.id), 2);
            putLe(&b, quint32(c.body.size()), 4);
            b.append(c.body);
        }
        return gz ? tape::gzip(b) : b;
    }
    if (m_kind == Kind::Tzx) {
        b.append(TzxMagic, 8);
        b.append(char(m_major));
        b.append(char(m_minor));
        for (const Chunk &c : m_chunks) {
            if (c.id != Chunk::Tail) b.append(char(c.id));
            b.append(c.body);
        }
        return b;
    }
    for (const Chunk &c : m_chunks) {                    // TAP
        if (c.id != Chunk::Tail) putLe(&b, quint32(c.body.size()), 2);
        b.append(c.body);
    }
    return b;
}

bool TapeFile::writeChecked(const QString &path, bool gz, bool backup,
                            const std::function<bool(TapeFile &, QString *)> &verify, QString *err) {
    const QByteArray bytes = serialize(gz);
    device::SafeSave s(path);
    if (!s.begin(bytes.size(), err)) return false;
    if (s.device()->write(bytes) != bytes.size()) { *err = "writing failed: " + s.device()->errorString(); return false; }
    if (!s.finishWriting(err)) return false;
    TapeFile w;
    QString e;
    {
        QByteArray back;
        if (!readFile(s.tempPath(), &back, &e)) { *err = "the new file can't be read back: " + e; return false; }
        if (back != bytes) { *err = "the new file doesn't read back as written"; return false; }
        if (!w.openBytes(back, path, m_suffix, &e)) { *err = "the new file can't be read as a tape: " + e; return false; }
    }
    if (w.kind() != m_kind || w.chunks() != m_chunks || w.gzip() != gz) { *err = "the new file doesn't hold the tape as it should"; return false; }
    if (verify && !verify(w, err)) return false;
    if (!s.replace(backup, err)) return false;
    m_path = path;
    m_file = bytes;
    m_fileSize = bytes.size();
    m_saved = m_chunks;
    m_gzip = m_savedGzip = gz;
    m_forceSave = false;
    return true;
}

bool TapeFile::commit(const SaveOptions &o, QString *err) {
    if (!isOpen()) { *err = "no tape"; return false; }
    if (m_path.isEmpty()) { *err = "the tape has no file yet: use Save As"; return false; }
    if (!writable()) { *err = m_readOnlyWhy.isEmpty() ? "the tape is read-only" : "the tape is read-only: " + m_readOnlyWhy; return false; }
    if (o.validate && !o.validate(err)) return false;
    return writeChecked(m_path, m_gzip, o.backup, o.verify, err);
}

bool TapeFile::saveAs(const QString &path, bool gz, QString *err, const std::function<bool(TapeFile &, QString *)> &verify) {
    if (!isOpen()) { *err = "no tape"; return false; }
    if (m_kind != Kind::Uef) gz = false;
    if (!m_readOnlyWhy.isEmpty()) { *err = "the tape can't be written: " + m_readOnlyWhy; return false; }
    return writeChecked(path, gz, false, verify, err);
}

} // namespace tape
} // namespace dw
