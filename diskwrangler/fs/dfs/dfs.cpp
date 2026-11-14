/*
 * Acorn DFS - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include "dfs.h"

#include <diskwrangler/fs/presets.h>

#include <algorithm>
#include <cstring>

namespace dw {
namespace dfs {

QString bootOptionName(int opt) {
    switch (opt) {
    case 0: return "0 - off";
    case 1: return "1 - *LOAD !BOOT";
    case 2: return "2 - *RUN !BOOT";
    case 3: return "3 - *EXEC !BOOT";
    default: return QString("%1 - not a standard option").arg(opt);
    }
}

quint8 bcdInc(quint8 v) {
    unsigned lo = (v & 0x0F) + 1, hi = v >> 4;
    if (lo > 9) { lo = 0; ++hi; }
    if (hi > 9) hi = 0;
    return quint8((hi << 4) | lo);
}

QString addrText(quint32 v) {
    v &= 0x3FFFFu;
    return QString("%1").arg((v & 0x30000u) == 0x30000u ? (v | 0xFF0000u) : v, 6, 16, QChar('0')).toUpper();
}

bool parseAddr(const QString &text, quint32 *out) {
    bool ok = false;
    quint32 a = text.toUInt(&ok, 16);
    if (!ok) return false;
    if (a <= 0x3FFFFu) *out = a;
    else if ((a & 0xFFFF0000u) == 0xFF0000u || (a & 0xFFFF0000u) == 0xFFFF0000u) *out = 0x30000u | (a & 0xFFFFu);
    else return false;
    return true;
}

QByteArray cpmFenceCatalogue(int sectorsPerSide) {
    QByteArray c(512, '\0');
    c.replace(0, 8, "Acorn CP");                 // title, first 8 characters (sector 0)
    c.replace(8, 7, "CPMDISC");
    c[15] = char('$' | 0x80);                    // directory $, locked
    c.replace(256, 4, "/M  ");                   // title, last 4 characters (sector 1)
    c[256 + 5] = 8;                              // one file
    c[256 + 6] = char((sectorsPerSide >> 8) & 3);   // boot option 0, sector count high bits
    c[256 + 7] = char(sectorsPerSide & 0xFF);
    const int start = 2, len = (sectorsPerSide - start) * 256;
    c[256 + 12] = char(len & 0xFF);              // load and exec 0
    c[256 + 13] = char((len >> 8) & 0xFF);
    c[256 + 14] = char(((len >> 16) & 3) << 4 | ((start >> 8) & 3));
    c[256 + 15] = char(start & 0xFF);
    return c;
}

bool isCpmFence(const QByteArray &b) {
    return b.size() >= 512 && quint8(b[256 + 5]) == 8 && b.mid(8, 7) == "CPMDISC" && (quint8(b[15]) & 0x7F) == '$';
}

LetterPlace placeLetter(QChar letter, const QString &name, bool flatten, const std::function<bool(QChar)> &canName) {
    LetterPlace p;
    p.name = name;
    if (flatten || letter == '$') return p;
    if (canName ? !canName(letter) : QString("^@&*#\"").contains(letter)) {
        p.how = LetterPlace::Flat;
        p.name = "X_" + name;
        p.warning = QString("%1.%2: ADFS can't have a directory called '%1', so it was copied as %3")
                        .arg(QString(letter), name, p.name);
        return p;
    }
    p.how = LetterPlace::Subdir;
    p.subdir = QString(letter);      // case kept: ADFS names are case-insensitive but keep their case
    return p;
}

bool validNameChar(QChar qc) {
    ushort c = qc.unicode();
    if (c <= 0x20 || c >= 0x7F) return false;
    return !QByteArray(".:\"#*").contains(char(c));
}
bool validDir(QChar c) { return validNameChar(c); }

bool validName(const QString &n, QString *why) {
    if (n.isEmpty()) { if (why) *why = "name is empty"; return false; }
    if (n.size() > MaxName) { if (why) *why = "DFS names are at most 7 characters"; return false; }
    for (QChar c : n)
        if (!validNameChar(c)) { if (why) *why = QString("character '%1' is not allowed in DFS names").arg(c); return false; }
    return true;
}

QString sanitizeName(const QString &host) {
    QString o;
    for (QChar c : host) { if (o.size() >= MaxName) break; o += validNameChar(c) ? c : QChar('_'); }
    return o.isEmpty() ? QString("_") : o;
}

bool plausibleCatalogue(const QByteArray &s0, const QByteArray &s1, quint32 *sectors) {
    if (s0.size() < 256 || s1.size() < 256) return false;
    quint8 cnt = quint8(s1[5]);
    if (cnt % 8 != 0 || cnt > MaxFiles * 8) return false;
    quint32 sec = (quint32(quint8(s1[6]) & 3) << 8) | quint8(s1[7]);
    if (sec < 2 || sec > 800) return false;
    for (int i = 1; i <= cnt / 8; ++i) {
        for (int k = 0; k < 7; ++k) { quint8 c = quint8(s0[i * 8 + k]) & 0x7F; if (c < 0x20 || c > 0x7E) return false; }
        quint8 d = quint8(s0[i * 8 + 7]) & 0x7F;
        if (d < 0x20 || d > 0x7E) return false;
        quint8 hi = quint8(s1[i * 8 + 6]);
        quint32 start = (quint32(hi & 3) << 8) | quint8(s1[i * 8 + 7]);
        quint32 len = quint8(s1[i * 8 + 4]) | (quint32(quint8(s1[i * 8 + 5])) << 8) | (quint32((hi >> 4) & 3) << 16);
        if (start < 2 || start + (len + 255) / 256 > sec) return false;
    }
    if (sectors) *sectors = sec;
    return true;
}

// The entries of one catalogue (sector 0/2 + sector 1/3).
static QVector<File> parseEntries(const QByteArray &s0, const QByteArray &s1) {
    QVector<File> out;
    int cnt = quint8(s1[5]) / 8;
    for (int i = 1; i <= cnt; ++i) {
        File f;
        QByteArray nm;
        for (int k = 0; k < 7; ++k) nm.append(char(quint8(s0[i * 8 + k]) & 0x7F));
        f.name = QString::fromLatin1(nm).trimmed();
        f.dir = QChar(quint8(s0[i * 8 + 7]) & 0x7F);
        f.locked = (quint8(s0[i * 8 + 7]) & 0x80) != 0;
        quint8 hi = quint8(s1[i * 8 + 6]);
        f.load = quint8(s1[i * 8]) | (quint32(quint8(s1[i * 8 + 1])) << 8) | (quint32((hi >> 2) & 3) << 16);
        f.exec = quint8(s1[i * 8 + 2]) | (quint32(quint8(s1[i * 8 + 3])) << 8) | (quint32((hi >> 6) & 3) << 16);
        f.length = quint8(s1[i * 8 + 4]) | (quint32(quint8(s1[i * 8 + 5])) << 8) | (quint32((hi >> 4) & 3) << 16);
        f.start = (quint32(hi & 3) << 8) | quint8(s1[i * 8 + 7]);
        out.append(f);
    }
    return out;
}

static void putEntries(QByteArray &s0, QByteArray &s1, const QVector<File> &files) {
    s1[5] = char(files.size() * 8);
    for (int i = 0; i < files.size(); ++i) {
        const File &f = files[i];
        int o = (i + 1) * 8;
        QByteArray nm = f.name.toLatin1().left(MaxName);
        for (int k = 0; k < 7; ++k) s0[o + k] = k < nm.size() ? nm[k] : ' ';
        s0[o + 7] = char(f.dir.unicode() | (f.locked ? 0x80 : 0));
        s1[o] = char(f.load & 0xFF); s1[o + 1] = char((f.load >> 8) & 0xFF);
        s1[o + 2] = char(f.exec & 0xFF); s1[o + 3] = char((f.exec >> 8) & 0xFF);
        s1[o + 4] = char(f.length & 0xFF); s1[o + 5] = char((f.length >> 8) & 0xFF);
        s1[o + 6] = char(((f.start >> 8) & 3) | (((f.load >> 16) & 3) << 2) | (((f.length >> 16) & 3) << 4) |
                         (((f.exec >> 16) & 3) << 6));
        s1[o + 7] = char(f.start & 0xFF);
    }
}

// Watford's entry in the first catalogue that keeps Acorn DFS off sectors 2-3 (BeebWiki, "Watford 62 DFS").
static bool isGuard(const File &f) {
    return f.dir == '!' && f.name == "!!!!!!!" && f.start == 2 && f.length <= 2 * SectorSize;
}

bool watfordCatalogue(const QByteArray &t, quint32 *sectors) {
    if (t.size() < 4 * SectorSize) return false;
    QByteArray s0 = t.mid(0, 256), s1 = t.mid(256, 256), s2 = t.mid(512, 256), s3 = t.mid(768, 256);
    for (int i = 0; i < 8; ++i) if (quint8(s2[i]) != 0xAA) return false;     // all Watford's DFS 1.44 looks for
    quint32 sec2 = 0;
    if (!plausibleCatalogue(s2, s3, &sec2)) return false;
    quint32 sec1 = (quint32(quint8(s1[6]) & 3) << 8) | quint8(s1[7]);
    quint32 sec = qMax(sec1, sec2);
    QByteArray s1x = s1;                       // the first catalogue judged against the true size
    s1x[6] = char((quint8(s1[6]) & ~3) | ((sec >> 8) & 3));
    s1x[7] = char(sec & 0xFF);
    if (!plausibleCatalogue(s0, s1x)) return false;
    // On a 62-file disc sectors 2-3 are catalogue: a file there (other than Watford's guard entry) means the &AA
    // bytes are just that file's data on an ordinary disc.
    for (const File &f : parseEntries(s0, s1)) if (f.start < 4 && !isGuard(f)) return false;
    for (const File &f : parseEntries(s2, s3)) if (f.start < 4) return false;
    if (sectors) *sectors = sec;
    return true;
}

// ------------------------------------------------------------ sector access

quint32 Disc::phys(quint32 n) const {
    if (!m_lay.doubleSided) return n;
    if (m_lay.interleaved) return ((n / SectorsPerTrack) * 2 + quint32(m_side)) * SectorsPerTrack + n % SectorsPerTrack;
    return quint32(m_side) * m_lay.sectorsPerSide + n;
}

bool Disc::rd(quint32 n, quint32 count, QByteArray *out, QString *err) const {
    out->clear();
    while (count) {
        quint32 chunk = count;
        if (m_lay.doubleSided && m_lay.interleaved) chunk = qMin(count, quint32(SectorsPerTrack) - n % SectorsPerTrack);
        QByteArray t;
        const quint64 off = quint64(phys(n)) * SectorSize;
        if (off + chunk * SectorSize > m_len) { *err = "read beyond the end of the disc"; return false; }
        if (!m_lin->read(qint64(off), qint64(chunk) * SectorSize, &t, err)) return false;
        out->append(t);
        n += chunk; count -= chunk;
    }
    return true;
}

bool Disc::wr(quint32 n, const QByteArray &data, QString *err) {
    quint32 count = quint32(data.size() / SectorSize);
    int pos = 0;
    while (count) {
        quint32 chunk = count;
        if (m_lay.doubleSided && m_lay.interleaved) chunk = qMin(count, quint32(SectorsPerTrack) - n % SectorsPerTrack);
        const quint64 off = quint64(phys(n)) * SectorSize;
        if (off + chunk * SectorSize > m_len) { *err = "write beyond the end of the disc"; return false; }
        if (!m_lin->write(qint64(off), data.mid(pos, int(chunk * SectorSize)), err)) return false;
        n += chunk; count -= chunk; pos += int(chunk * SectorSize);
    }
    return true;
}

// ----------------------------------------------------------------- catalogue

bool Disc::attach(dw::device::Linear *disc, quint64 size, int side, const Layout &lay, QString *err) {
    m_lin = disc; m_side = side; m_lay = lay; m_len = quint32(size);
    if (!load(err)) { m_lin = nullptr; return false; }
    return true;
}

bool Disc::load(QString *err) {
    QByteArray t;
    if (!rd(0, 4, &t, err)) return false;
    QByteArray s0 = t.mid(0, 256), s1 = t.mid(256, 256), s2 = t.mid(512, 256), s3 = t.mid(768, 256);
    quint32 sec = 0;
    m_watford = watfordCatalogue(t, &sec);
    if (!m_watford && !plausibleCatalogue(s0, s1, &sec)) { *err = "not a valid DFS catalogue"; return false; }
    m_sectors = sec;
    m_cycle = quint8(s1[4]);
    m_boot = (quint8(s1[6]) >> 4) & 3;
    QByteArray tt;
    for (int i = 0; i < 8; ++i) tt.append(s0[i]);
    for (int i = 0; i < 4; ++i) tt.append(s1[i]);
    int end = tt.indexOf('\0');
    if (end >= 0) tt.truncate(end);
    m_title = QString::fromLatin1(tt).trimmed();
    m_files = parseEntries(s0, s1);
    m_guard = false;
    if (m_watford) {
        for (int i = 0; i < m_files.size(); ++i)
            if (isGuard(m_files[i])) { m_guard = true; m_guardFile = m_files.takeAt(i); break; }
        m_cycle2 = quint8(s3[4]);
        m_s3b6 = quint8(s3[6]);
        // Watford keeps the second catalogue's files above the first's, so it comes first in descending order
        QVector<File> c2 = parseEntries(s2, s3);
        for (File &f : c2) f.cat = 1;
        m_files = c2 + m_files;
    }
    return true;
}

bool Disc::writeCatalogue(QString *err) {
    QByteArray s0(SectorSize, 0), s1(SectorSize, 0);
    QByteArray t = m_title.toLatin1().left(MaxTitle);
    for (int i = 0; i < 12; ++i) {
        char c = i < t.size() ? t[i] : 0;
        if (i < 8) s0[i] = c; else s1[i - 8] = c;
    }
    m_cycle = bcdInc(m_cycle);
    s1[4] = char(m_cycle);
    if (!m_watford) {
        s1[6] = char(((m_boot & 3) << 4) | ((m_sectors >> 8) & 3));
        s1[7] = char(m_sectors & 0xFF);
        putEntries(s0, s1, m_files);
        return wr(0, s0 + s1, err);
    }
    // Watford 62-file disc: each file stays in its own catalogue (placeWatford chose a new file's), and only a
    // catalogue whose contents change is written, with its own cycle number counted on - as Watford's DFS does.
    QVector<File> all = m_files;
    std::stable_sort(all.begin(), all.end(), [](const File &a, const File &b) { return a.start > b.start; });
    QVector<File> cat1, cat2;
    for (const File &f : all) (f.cat ? cat2 : cat1).append(f);
    if (m_guard) cat1.append(m_guardFile);
    // The first catalogue's size is the start of the second's lowest file (the true size while it has none), so
    // that Acorn DFS, which can't see those files, finds no room among them. Watford's DFS 1.44 and DDFS 1.54T set
    // it so on every save. Never below the top of the first catalogue's own files.
    quint32 size1 = m_sectors;
    if (!cat2.isEmpty()) {
        size1 = cat2.last().start;
        for (const File &f : cat1) size1 = qMax(size1, f.start + f.sectors());
    }
    s1[6] = char(((m_boot & 3) << 4) | ((size1 >> 8) & 3));
    s1[7] = char(size1 & 0xFF);
    putEntries(s0, s1, cat1);
    QByteArray s2(SectorSize, 0), s3(SectorSize, 0);
    QByteArray cur;
    if (!rd(0, 4, &cur, err)) return false;
    for (int i = 0; i < 8; ++i) s2[i] = char(0xAA);
    for (int i = 0; i < 4; ++i) s3[i] = cur[768 + i];        // whatever was there (zeros from Watford's *FORM)
    s3[4] = char(m_cycle2);
    s3[6] = char((m_s3b6 & ~3) | ((m_sectors >> 8) & 3));
    s3[7] = char(m_sectors & 0xFF);
    putEntries(s2, s3, cat2);
    // s1 got the counted-on cycle above; compare the first catalogue as it would be without counting
    QByteArray s1Same = s1;
    s1Same[4] = cur[256 + 4];
    if (s0 + s1Same == cur.left(512)) {
        m_cycle = quint8(cur[256 + 4]);
    } else if (!wr(0, s0 + s1, err)) {
        return false;
    }
    if (s2 + s3 != cur.mid(512, 512)) {
        m_cycle2 = bcdInc(m_cycle2);
        s3[4] = char(m_cycle2);
        if (!wr(2, s2 + s3, err)) return false;
    }
    return true;
}

// Watford's rule for a new file (DFS 1.44's save routine): it goes in the first catalogue unless it lies above the
// second catalogue's lowest file. If that catalogue is full, the entry at the boundary moves across - the first's
// highest to the second, or the second's lowest to the first - so the first catalogue's files stay below the
// second's. Existing files are never shuffled between catalogues otherwise.
void Disc::placeWatford(int idx) {
    File &nf = m_files[idx];
    bool have2 = false;
    quint32 low2 = 0;
    int n1 = 0, n2 = 0;
    for (int i = 0; i < m_files.size(); ++i) {
        if (i == idx) continue;
        const File &f = m_files[i];
        if (f.cat) { low2 = have2 ? qMin(low2, f.start) : f.start; have2 = true; ++n2; } else ++n1;
    }
    nf.cat = (have2 && nf.start > low2) ? 1 : 0;
    (nf.cat ? n2 : n1)++;
    auto pick = [&](int cat, bool highest) {
        int best = -1;
        for (int i = 0; i < m_files.size(); ++i) {
            if (m_files[i].cat != cat) continue;
            if (best < 0 || (highest ? m_files[i].start > m_files[best].start : m_files[i].start < m_files[best].start)) best = i;
        }
        return best;
    };
    if (n1 > MaxFiles - (m_guard ? 1 : 0)) m_files[pick(0, true)].cat = 1;
    else if (n2 > MaxFiles) m_files[pick(1, false)].cat = 0;
}

int Disc::indexOf(QChar dir, const QString &name) const {
    for (int i = 0; i < m_files.size(); ++i)
        if (m_files[i].dir.toUpper() == dir.toUpper() && m_files[i].name.compare(name, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

static quint32 capacity(const Disc &d, const Layout &lay) {
    quint32 c = d.sectors();
    if (lay.sectorsPerSide) c = qMin(c, lay.sectorsPerSide);
    return c;
}

quint32 Disc::freeSectors() const {
    quint32 used = firstData(), cap = capacity(*this, m_lay);
    for (const File &f : m_files) used += f.sectors();
    return cap > used ? cap - used : 0;
}

bool Disc::findGap(quint32 n, quint32 *start) const {
    QVector<QPair<quint32, quint32>> occ;
    for (const File &f : m_files) if (f.sectors()) occ.append({f.start, f.start + f.sectors()});
    std::sort(occ.begin(), occ.end());
    quint32 cursor = firstData(), cap = capacity(*this, m_lay);
    for (const auto &o : occ) {
        if (o.first >= cursor && o.first - cursor >= n) { *start = cursor; return true; }
        cursor = qMax(cursor, o.second);
    }
    if (cap >= cursor && cap - cursor >= n) { *start = cursor; return true; }
    return false;
}

quint32 Disc::largestFree() const {
    QVector<QPair<quint32, quint32>> occ;
    for (const File &f : m_files) if (f.sectors()) occ.append({f.start, f.start + f.sectors()});
    std::sort(occ.begin(), occ.end());
    quint32 cursor = firstData(), best = 0, cap = capacity(*this, m_lay);
    for (const auto &o : occ) {
        if (o.first > cursor) best = qMax(best, o.first - cursor);
        cursor = qMax(cursor, o.second);
    }
    if (cap > cursor) best = qMax(best, cap - cursor);
    return best;
}

bool Disc::readFile(const File &f, QByteArray *out, QString *err) const {
    out->clear();
    if (f.length == 0) return true;
    if (f.start + f.sectors() > capacity(*this, m_lay)) {
        *err = "file lies beyond the end of the disc";
        return false;
    }
    if (!rd(f.start, f.sectors(), out, err)) return false;
    out->truncate(int(f.length));
    return true;
}

// ---------------------------------------------------------------- mutations

// A change that fails stops where it is: what it has staged, and the catalogue it has changed in memory, are put back
// by the caller's dw::fs::Disc::transact, which puts the staging back and has the volume reload (step 6b: the class
// keeps no snapshot of its own). Only the refusal of a read-only disc is here.
bool Disc::canWrite(QString *err) const {
    if (!m_lin || !m_lin->writable()) { *err = "image is read-only"; return false; }
    return true;
}

bool Disc::addFile(QChar dir, const QString &name, const QByteArray &data, quint32 load, quint32 exec, bool locked,
                   bool replace, QString *err) {
    return canWrite(err) && addInner(dir, name, data, load, exec, locked, replace, err);
}

bool Disc::addInner(QChar dir, const QString &name, const QByteArray &data, quint32 load, quint32 exec, bool locked,
                    bool replace, QString *err) {
    QString why;
    if (!validName(name, &why)) { *err = QString("'%1': %2").arg(name, why); return false; }
    if (!validDir(dir)) { *err = QString("'%1' is not a valid DFS directory character").arg(dir); return false; }
    if (data.size() > 0x3FFFF) { *err = "file too large for DFS"; return false; }
    int idx = indexOf(dir, name);
    if (idx >= 0) {
        if (!replace) { *err = QString("'%1.%2' already exists").arg(dir, name); return false; }
        m_files.removeAt(idx);
    }
    if (m_files.size() >= maxFiles()) {
        *err = m_watford ? QString("the catalogues are full (this Watford disc holds %1 files per side)").arg(maxFiles())
                         : QString("the catalogue is full (DFS allows 31 files per disc side)");
        return false;
    }
    quint32 n = quint32((data.size() + SectorSize - 1) / SectorSize), start = 0;
    if (n == 0) {
        start = firstData();
        for (const File &f : m_files) start = qMax(start, f.start);       // harmless address for an empty file
        if (start > capacity(*this, m_lay)) start = firstData();
    } else if (!findGap(n, &start)) {
        *err = QString("not enough contiguous free space on the DFS disc for '%1' (needs %2 sectors; largest free block %3, total free %4)")
                   .arg(name).arg(n).arg(largestFree()).arg(freeSectors());
        return false;
    }
    if (n) {
        QByteArray padded = data;
        padded.append(QByteArray(int(n * SectorSize) - data.size(), 0));
        if (!wr(start, padded, err)) return false;
    }
    File f;
    f.name = name; f.dir = dir; f.locked = locked;
    f.load = load & 0x3FFFF; f.exec = exec & 0x3FFFF;
    f.length = quint32(data.size()); f.start = start;
    m_files.append(f);
    if (m_watford) placeWatford(m_files.size() - 1);
    std::stable_sort(m_files.begin(), m_files.end(), [](const File &a, const File &b) { return a.start > b.start; });
    return writeCatalogue(err);
}

bool Disc::remove(QChar dir, const QString &name, QString *err) {
    if (!canWrite(err)) return false;
    int idx = indexOf(dir, name);
    if (idx < 0) { *err = QString("'%1.%2' not found").arg(dir, name); return false; }
    m_files.removeAt(idx);
    return writeCatalogue(err);
}

bool Disc::modify(QChar oldDir, const QString &oldName, QChar newDir, const QString &newName, bool locked,
                  quint32 load, quint32 exec, QString *err) {
    if (!canWrite(err)) return false;
    int idx = indexOf(oldDir, oldName);
    if (idx < 0) { *err = QString("'%1.%2' not found").arg(oldDir, oldName); return false; }
    QString why;
    if (!validName(newName, &why)) { *err = QString("'%1': %2").arg(newName, why); return false; }
    if (!validDir(newDir)) { *err = QString("'%1' is not a valid DFS directory character").arg(newDir); return false; }
    int clash = indexOf(newDir, newName);
    if (clash >= 0 && clash != idx) { *err = QString("'%1.%2' already exists").arg(newDir, newName); return false; }
    File &f = m_files[idx];
    f.name = newName; f.dir = newDir; f.locked = locked;
    f.load = load & 0x3FFFF; f.exec = exec & 0x3FFFF;
    return writeCatalogue(err);
}

bool Disc::setTitle(const QString &title, QString *err) {
    if (!canWrite(err)) return false;
    if (title.size() > MaxTitle) { *err = "the DFS disc title can be at most 12 characters"; return false; }
    for (QChar c : title)
        if (c.unicode() < 0x20 || c.unicode() > 0x7E) { *err = "the title may only contain printable ASCII characters"; return false; }
    m_title = title;
    return writeCatalogue(err);
}

bool Disc::setBootOption(int opt, QString *err) {
    if (!canWrite(err)) return false;
    if (opt < 0 || opt > 3) { *err = "the boot option must be 0 (off), 1 (*LOAD), 2 (*RUN) or 3 (*EXEC)"; return false; }
    m_boot = opt;
    return writeCatalogue(err);
}

// -------------------------------------------------------------------- check

Report Disc::check() {
    Report r;
    QString err;
    if (!attached()) { r.errors << "no DFS disc"; return r; }
    if (!load(&err)) { r.errors << err; return r; }
    if (m_lay.sectorsPerSide && m_sectors > m_lay.sectorsPerSide)
        r.errors << QString("the catalogue claims %1 sectors but the image only holds %2 per side").arg(m_sectors).arg(m_lay.sectorsPerSide);
    struct Ext { quint32 s, e; QString n; };
    QVector<Ext> ex;
    for (int i = 0; i < m_files.size(); ++i) {
        const File &f = m_files[i];
        QString id = f.fullName();
        QString why;
        if (!validName(f.name, &why)) r.warnings << QString("file %1 has an unusual name (%2)").arg(id, why);
        if (f.start < firstData()) r.errors << id + (m_watford ? ": starts inside the catalogues (sectors 0-3)" : ": starts inside the catalogue");
        if (f.start + f.sectors() > m_sectors) r.errors << id + ": extends beyond the end of the disc";
        if (f.sectors()) ex.append({f.start, f.start + f.sectors(), id});
        for (int j = 0; j < i; ++j)
            if (m_files[j].dir.toUpper() == f.dir.toUpper() && m_files[j].name.compare(f.name, Qt::CaseInsensitive) == 0)
                r.errors << id + ": duplicate file name";
        if (i > 0 && f.start > m_files[i - 1].start) r.warnings << id + ": catalogue is not in descending sector order";
    }
    std::sort(ex.begin(), ex.end(), [](const Ext &a, const Ext &b) { return a.s < b.s; });
    for (int i = 1; i < ex.size(); ++i)
        if (ex[i].s < ex[i - 1].e) r.errors << QString("%1 overlaps %2").arg(ex[i].n, ex[i - 1].n);
    r.info << QString("DFS drive %1%2%3: %4 files, %5 of %6 sectors free")
                  .arg(m_side * 2).arg(m_title.isEmpty() ? QString() : " '" + m_title + "'")
                  .arg(m_watford ? QString(" (Watford 62-file catalogue)") : QString())
                  .arg(m_files.size()).arg(freeSectors()).arg(m_sectors);
    return r;
}

// ------------------------------------------------------------------------------------------------ new discs

void addPresets(fs::PresetRegistry &r) {
    struct P { const char *id, *name; int tracks; bool ds, wat; };
    static const P list[] = {
        {"dfs-ss40", "DFS - 100K, 40 track, single-sided (.ssd)", 40, false, false},
        {"dfs-ss80", "DFS - 200K, 80 track, single-sided (.ssd)", 80, false, false},
        {"dfs-ds40", "DFS - 2 x 100K, 40 track, double-sided (.dsd)", 40, true, false},
        {"dfs-ds80", "DFS - 2 x 200K, 80 track, double-sided (.dsd)", 80, true, false},
        {"wat-ss40", "Watford DFS 62-file - 100K, 40 track, single-sided (.ssd)", 40, false, true},
        {"wat-ss80", "Watford DFS 62-file - 200K, 80 track, single-sided (.ssd)", 80, false, true},
        {"wat-ds40", "Watford DFS 62-file - 2 x 100K, 40 track, double-sided (.dsd)", 40, true, true},
        {"wat-ds80", "Watford DFS 62-file - 2 x 200K, 80 track, double-sided (.dsd)", 80, true, true},
    };
    for (const P &l : list) {
        fs::Preset p;
        p.id = l.id;
        p.name = l.name;
        p.fsId = "dfs";
        p.source = "dfs";
        p.cylinders = l.tracks;
        p.heads = l.ds ? 2 : 1;
        p.sectors = SectorsPerTrack;
        p.sectorSize = SectorSize;
        p.bytes = quint64(p.cylinders) * quint64(p.heads) * quint64(p.sectors) * quint64(p.sectorSize);
        p.containers = QStringList{"raw", "imd", "edsk", "dsk"};
        p.suffix = l.ds ? "dsd" : "ssd";
        p.doubleSided = l.ds;
        p.sideOrder = l.ds;
        p.titleWhat = "Disc title";
        p.titleMax = 12;
        p.notes = l.wat ? "Watford DFS's 62-file catalogue: a second catalogue in sectors 2-3" : QString();
        p.extra["watford"] = l.wat;
        r.add(p);
    }
}

} // namespace dfs
} // namespace dw
