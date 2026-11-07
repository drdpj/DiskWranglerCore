/*
 * FAT12 / FAT16 - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include "fat.h"

#include <cstring>


namespace dw {
namespace fat {

static quint32 u16(const char *p) { return quint8(p[0]) | (quint32(quint8(p[1])) << 8); }
static quint32 u32(const char *p) { return u16(p) | (u16(p + 2) << 16); }
static void p16(char *p, quint32 v) { p[0] = char(v & 0xFF); p[1] = char((v >> 8) & 0xFF); }
static void p32(char *p, quint32 v) { p16(p, v & 0xFFFF); p16(p + 2, v >> 16); }

static bool dosChar(QChar c) {
    ushort u = c.unicode();
    if ((u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9')) return true;
    return u < 0x80 && QByteArray("$%'-_@~`!(){}^#&").contains(char(u));
}

static void nowStamp(quint16 *date, quint16 *time) {
    QDateTime n = QDateTime::currentDateTime();
    int y = qBound(1980, n.date().year(), 2107);
    *date = quint16(((y - 1980) << 9) | (n.date().month() << 5) | n.date().day());
    *time = quint16((n.time().hour() << 11) | (n.time().minute() << 5) | (n.time().second() / 2));
}

QString DEntry::attrString() const {
    QString s;
    if (attr & 0x10) s += 'D';
    if (attr & AttrR) s += 'R';
    if (attr & AttrH) s += 'H';
    if (attr & AttrS) s += 'S';
    if (attr & AttrA) s += 'A';
    return s;
}

QDateTime DEntry::when() const {
    int y = 1980 + (date >> 9), mo = (date >> 5) & 15, d = date & 31;
    int h = time >> 11, mi = (time >> 5) & 63, s = (time & 31) * 2;
    QDate dt(y, mo, d);
    QTime tm(h, mi, s);
    if (!dt.isValid() || !tm.isValid()) return QDateTime();
    return QDateTime(dt, tm);
}

// ------------------------------------------------------------------ names

bool Volume::toRaw(const QString &name, QByteArray *raw, QString *why) {
    QString n = name.toUpper();
    int dot = n.lastIndexOf('.');
    QString base = dot < 0 ? n : n.left(dot), ext = dot < 0 ? QString() : n.mid(dot + 1);
    if (base.isEmpty() || base.size() > 8) { if (why) *why = "base name must be 1-8 characters"; return false; }
    if (ext.size() > 3) { if (why) *why = "extension must be at most 3 characters"; return false; }
    for (QChar c : base + ext)
        if (!dosChar(c)) { if (why) *why = QString("character '%1' is not allowed in DOS names").arg(c); return false; }
    QByteArray r(11, ' ');
    for (int i = 0; i < base.size(); ++i) r[i] = base[i].toLatin1();
    for (int i = 0; i < ext.size(); ++i) r[8 + i] = ext[i].toLatin1();
    *raw = r;
    return true;
}

bool Volume::validName(const QString &name, QString *why) {
    QByteArray r;
    return toRaw(name, &r, why);
}

QString Volume::fromHost(const QString &host, bool directory) {
    QString h = host.toUpper(), base, ext;
    int dot = directory ? -1 : h.lastIndexOf('.');
    if (dot > 0) { base = h.left(dot); ext = h.mid(dot + 1); } else base = h;
    auto clean = [](const QString &s, int max) {
        QString o;
        for (QChar c : s) { if (o.size() >= max) break; o += dosChar(c) ? c : QChar('_'); }
        return o;
    };
    base = clean(base, 8);
    ext = clean(ext, 3);
    if (base.isEmpty()) base = "_";
    return ext.isEmpty() ? base : base + "." + ext;
}

static DEntry parseEntry(const char *p) {
    DEntry e;
    QByteArray b(p, 8);
    if (quint8(b[0]) == 0x05) b[0] = char(0xE5);
    QString base = QString::fromLatin1(b).trimmed(), ext = QString::fromLatin1(p + 8, 3).trimmed();
    e.name = ext.isEmpty() ? base : base + "." + ext;
    e.attr = quint8(p[11]);
    e.time = quint16(u16(p + 22));
    e.date = quint16(u16(p + 24));
    e.cluster = u16(p + 26);
    e.size = u32(p + 28);
    return e;
}

// ------------------------------------------------------------- attach / FAT

bool Volume::rd(quint64 off, quint32 len, QByteArray *out, QString *err) const {
    if (off + len > m_len) { *err = "read beyond the end of the file"; return false; }
    if (len == 0) { out->clear(); return true; }
    return m_lin->read(qint64(m_sin) * 256 + qint64(off), len, out, err);
}
bool Volume::wr(quint64 off, const QByteArray &d, QString *err) {
    if (!m_lin->writable()) { *err = "image is read-only (file not writable, or its free space map failed validation)"; return false; }
    if (d.isEmpty()) return true;
    if (off + d.size() > m_len) { *err = "write beyond the end of the file"; return false; }
    return m_lin->write(qint64(m_sin) * 256 + qint64(off), d, err);
}

Params Volume::standard(Format f) {
    Params p;
    switch (f) {
    case FmtPc360:    p = {512, 2, 1, 2, 112, 2, 720, 0xFD, false}; break;
    case FmtPc720:    p = {512, 2, 1, 2, 112, 3, 1440, 0xF9, false}; break;
    case FmtPc1200:   p = {512, 1, 1, 2, 224, 7, 2400, 0xF9, false}; break;
    case FmtPc1440:   p = {512, 1, 1, 2, 224, 9, 2880, 0xF0, false}; break;
    case FmtAcorn800: p = {1024, 1, 0, 1, 192, 2, 800, 0xFD, false}; break;      // no boot record at all
    case FmtAcorn640: p = {256, 8, 16, 1, 112, 2, 2560, 0xFF, false}; break;  // sectors 0-15: ADFS cat + DOSBOOT; FAT@16 (numbered side 0 first: see dw::device::SideOrder)
    }
    return p;
}

bool Volume::readBpb(const QByteArray &b, Params *out) {
    if (b.size() < 36) return false;
    const char *p = b.constData();
    Params q;
    q.bps = u16(p + 11); q.spc = quint8(p[13]); q.res = u16(p + 14); q.nfats = quint8(p[16]);
    q.rootEnt = u16(p + 17); q.total = u16(p + 19); q.spf = u16(p + 22); q.media = quint8(p[21]);
    if (!q.total) q.total = u32(p + 32);
    q.bpb = true;
    *out = q;
    return true;
}

bool Volume::plausible(const Params &p) {
    auto pow2 = [](quint32 v) { return v && !(v & (v - 1)); };
    return pow2(p.bps) && p.bps >= 128 && p.bps <= 8192 && pow2(p.spc) && p.spc <= 128 && p.res >= 1 &&
           p.nfats >= 1 && p.nfats <= 2 && p.rootEnt > 0 && p.spf > 0 && p.total > 0 && p.media >= 0xF0;
}

bool Volume::attach(dw::device::Linear *img, quint32 sin, quint32 length, QString *err) {
    m_lin = img; m_sin = sin; m_len = length;
    if (length < 1024) { *err = "not a FAT12/FAT16 DOS volume: file too small"; m_lin = nullptr; return false; }
    QByteArray b;
    if (!rd(0, 512, &b, err)) { m_lin = nullptr; return false; }
    Params p;
    if (!readBpb(b, &p)) { *err = "not a FAT12/FAT16 DOS volume: no boot sector"; m_lin = nullptr; return false; }
    return attachParams(img, sin, length, p, err);
}

bool Volume::attachFormat(dw::device::Linear *img, quint32 sin, quint32 length, Format f, QString *err) {
    return attachParams(img, sin, length, standard(f), err);
}

bool Volume::attachParams(dw::device::Linear *img, quint32 sin, quint32 length, const Params &p, QString *err) {
    m_lin = img; m_sin = sin; m_len = length;
    m_hasBpb = p.bpb;
    auto fail = [&](const QString &why) { *err = "not a FAT12/FAT16 DOS volume: " + why; m_lin = nullptr; return false; };
    m_bps = p.bps; m_spc = p.spc; m_res = p.res; m_nfats = p.nfats; m_rootEnt = p.rootEnt; m_spf = p.spf;
    m_total = p.total; m_media = p.media;
    auto pow2 = [](quint32 v) { return v && !(v & (v - 1)); };
    if (!pow2(m_bps) || m_bps < 128 || m_bps > 8192) return fail("bad bytes-per-sector");
    if (!pow2(m_spc) || m_spc > 128) return fail("bad sectors-per-cluster");
    if (m_res < 1 && p.bpb) return fail("bad reserved sector count");
    if (m_nfats < 1 || m_nfats > 2) return fail("bad FAT count");
    if (m_rootEnt == 0 || m_spf == 0 || m_total == 0) return fail("bad directory/FAT/total size");
    m_rootSectors = (m_rootEnt * 32 + m_bps - 1) / m_bps;
    m_dataStart = m_res + m_nfats * m_spf + m_rootSectors;
    if (m_total <= m_dataStart) return fail("no data area");
    if (quint64(m_total) * m_bps > length) return fail("volume is larger than the file holding it");
    m_clusters = (m_total - m_dataStart) / m_spc;
    if (m_clusters < 1 || m_clusters >= 65525) return fail("unsupported cluster count (FAT32?)");
    m_fat16 = m_clusters >= 4085;
    quint32 need = m_fat16 ? (m_clusters + 2) * 2 : ((m_clusters + 2) * 3 + 1) / 2;
    if (m_spf * m_bps < need) return fail("FAT is too small for the volume");
    if (!loadFat(err)) { m_lin = nullptr; return false; }
    loadLabel();
    err->clear();
    return true;
}

void Volume::loadLabel() {
    m_label.clear();
    QByteArray root;
    QString e;
    if (rd(rootOff(), m_rootEnt * 32, &root, &e))
        for (quint32 i = 0; i < m_rootEnt; ++i) {
            const char *x = root.constData() + i * 32;
            if (x[0] == 0) break;
            if (quint8(x[0]) != 0xE5 && (x[11] & 0x0F) == 0x08) { m_label = QString::fromLatin1(x, 11).trimmed(); break; }
        }
}

bool Volume::reload(QString *err) {
    if (!m_lin) { *err = "no volume"; return false; }
    if (!loadFat(err)) return false;
    loadLabel();
    return true;
}

bool Volume::loadFat(QString *err) { return rd(quint64(m_res) * m_bps, m_spf * m_bps, &m_fat, err); }

bool Volume::flushFat(QString *err) {
    for (quint32 k = 0; k < m_nfats; ++k)
        if (!wr(quint64(m_res + k * m_spf) * m_bps, m_fat, err)) return false;
    return true;
}

quint32 Volume::fatGet(quint32 c) const {
    if (m_fat16) return u16(m_fat.constData() + c * 2);
    quint32 o = c + c / 2, v = u16(m_fat.constData() + o);
    return (c & 1) ? (v >> 4) : (v & 0xFFF);
}

void Volume::fatSet(quint32 c, quint32 v) {
    char *f = m_fat.data();
    if (m_fat16) { p16(f + c * 2, v); return; }
    quint32 o = c + c / 2;
    if (c & 1) { f[o] = char((quint8(f[o]) & 0x0F) | ((v & 0x0F) << 4)); f[o + 1] = char(v >> 4); }
    else { f[o] = char(v & 0xFF); f[o + 1] = char((quint8(f[o + 1]) & 0xF0) | ((v >> 8) & 0x0F)); }
}

quint32 Volume::freeClusters() const {
    quint32 n = 0;
    for (quint32 c = 2; c <= maxCluster(); ++c) if (fatGet(c) == 0) ++n;
    return n;
}

bool Volume::chain(quint32 start, QVector<quint32> *out, QString *err) const {
    out->clear();
    quint32 c = start;
    while (true) {
        if (c < 2 || c > maxCluster()) { *err = QString("cluster chain leaves the data area (cluster %1)").arg(c); return false; }
        out->append(c);
        if (quint32(out->size()) > m_clusters) { *err = "loop in cluster chain"; return false; }
        quint32 n = fatGet(c);
        if (n == 0) { *err = QString("cluster chain runs into a free cluster (after %1)").arg(c); return false; }
        if (isBad(n)) { *err = QString("cluster chain contains a bad cluster (after %1)").arg(c); return false; }
        if (isEoc(n)) return true;
        c = n;
    }
}

bool Volume::alloc(quint32 n, QVector<quint32> *out) {
    out->clear();
    if (!n) return true;
    for (quint32 c = 2; c <= maxCluster() && quint32(out->size()) < n; ++c)
        if (fatGet(c) == 0) out->append(c);
    if (quint32(out->size()) < n) { out->clear(); return false; }
    for (int i = 0; i < out->size(); ++i) fatSet((*out)[i], i + 1 < out->size() ? (*out)[i + 1] : eocValue());
    return true;
}

bool Volume::freeChain(quint32 start, QString *err) {
    QVector<quint32> ch;
    if (!chain(start, &ch, err)) return false;
    for (quint32 c : ch) fatSet(c, 0);
    return true;
}

bool Volume::writeChain(const QVector<quint32> &ch, const QByteArray &data, QString *err) {
    quint32 cb = clusterBytes();
    int i = 0;
    qint64 pos = 0;
    while (i < ch.size()) {
        int j = i;
        while (j + 1 < ch.size() && ch[j + 1] == ch[j] + 1) ++j;          // contiguous run of clusters
        quint32 bytes = quint32(j - i + 1) * cb;
        QByteArray piece = data.mid(int(pos), int(bytes));
        if (piece.size() < int(bytes)) piece.append(QByteArray(int(bytes) - piece.size(), 0));
        if (!wr(clusterOff(ch[i]), piece, err)) return false;
        pos += bytes;
        i = j + 1;
    }
    return true;
}

// -------------------------------------------------------------- directories

bool Volume::dirRegions(quint32 dir, QVector<Region> *out, QString *err) const {
    out->clear();
    if (dir == 0) { out->append(Region{rootOff(), m_rootEnt * 32}); return true; }
    QVector<quint32> ch;
    if (!chain(dir, &ch, err)) return false;
    for (quint32 c : ch) out->append(Region{clusterOff(c), clusterBytes()});
    return true;
}

bool Volume::readRegions(const QVector<Region> &rg, QByteArray *out, QString *err) const {
    out->clear();
    for (const Region &r : rg) {
        QByteArray t;
        if (!rd(r.off, r.len, &t, err)) return false;
        out->append(t);
    }
    return true;
}

quint64 Volume::slotOffset(const QVector<Region> &rg, int idx) {
    quint64 pos = quint64(idx) * 32;
    for (const Region &r : rg) {
        if (pos < r.len) return r.off + pos;
        pos -= r.len;
    }
    return ~quint64(0);
}

int Volume::findActive(const QByteArray &data, const QByteArray &raw11) {
    for (int i = 0; i + 32 <= data.size(); i += 32) {
        quint8 f = quint8(data[i]);
        if (f == 0) break;
        if (f == 0xE5) continue;
        quint8 a = quint8(data[i + 11]);
        if ((a & 0x0F) == 0x0F || (a & 0x08)) continue;
        if (memcmp(data.constData() + i, raw11.constData(), 11) == 0) return i / 32;
    }
    return -1;
}

bool Volume::listDir(quint32 dir, QVector<DEntry> *out, QString *err) const {
    out->clear();
    QVector<Region> rg;
    QByteArray data;
    if (!dirRegions(dir, &rg, err) || !readRegions(rg, &data, err)) return false;
    for (int i = 0; i + 32 <= data.size(); i += 32) {
        const char *p = data.constData() + i;
        quint8 f = quint8(p[0]);
        if (f == 0) break;
        if (f == 0xE5) continue;
        if ((p[11] & 0x0F) == 0x0F || (p[11] & 0x08)) continue;       // LFN fragment / volume label
        out->append(parseEntry(p));
    }
    return true;
}

bool Volume::readFile(const DEntry &e, QByteArray *out, QString *err) const {
    out->clear();
    if (e.size == 0) return true;
    QVector<quint32> ch;
    if (e.cluster < 2 || !chain(e.cluster, &ch, err)) { if (e.cluster < 2) *err = "file has no clusters"; return false; }
    quint32 cb = clusterBytes();
    if (quint64(ch.size()) * cb < e.size) { *err = "cluster chain is shorter than the file size"; return false; }
    int i = 0;
    while (i < ch.size() && quint64(out->size()) < e.size) {
        int j = i;
        while (j + 1 < ch.size() && ch[j + 1] == ch[j] + 1) ++j;
        QByteArray t;
        if (!rd(clusterOff(ch[i]), quint32(j - i + 1) * cb, &t, err)) return false;
        out->append(t);
        i = j + 1;
    }
    out->truncate(int(e.size));
    return true;
}

bool Volume::freeSlot(quint32 dir, QVector<Region> &rg, QByteArray &data, int *idx, QString *err) {
    for (int i = 0; i + 32 <= data.size(); i += 32) {
        quint8 f = quint8(data[i]);
        if (f == 0 || f == 0xE5) { *idx = i / 32; return true; }
    }
    if (dir == 0) { *err = QString("the root directory is full (%1 entries)").arg(m_rootEnt); return false; }
    QVector<quint32> ch, nc;
    if (!chain(dir, &ch, err)) return false;
    if (!alloc(1, &nc)) { *err = "no free cluster to extend the directory"; return false; }
    QByteArray zero(int(clusterBytes()), 0);
    if (!wr(clusterOff(nc[0]), zero, err)) return false;
    fatSet(ch.last(), nc[0]);
    *idx = data.size() / 32;
    rg.append(Region{clusterOff(nc[0]), clusterBytes()});
    data.append(zero);
    return true;
}

// --------------------------------------------------------------- mutations

#define DOS_TXN(call)                                                         \
    do {                                                                      \
        if (!m_lin || !m_lin->writable()) { *err = "image is read-only"; return false; } \
        dw::device::Container::Snapshot snap = m_lin->container()->snapshot(); \
        bool ok = (call);                                                     \
        if (!ok) { m_lin->container()->restore(snap); QString e2; loadFat(&e2); return false; } \
        return true;                                                          \
    } while (0)

bool Volume::addFile(quint32 dir, const QString &name, const QByteArray &data, bool replace, QString *err) {
    DOS_TXN(addFileInner(dir, name, data, replace, err));
}
bool Volume::makeDir(quint32 dir, const QString &name, QString *err, quint32 *nc) {
    DOS_TXN(makeDirInner(dir, name, err, nc));
}
bool Volume::remove(quint32 dir, const QString &name, QString *err) { DOS_TXN(removeInner(dir, name, err)); }
bool Volume::modify(quint32 dir, const QString &o, const QString &n, quint8 attr, QString *err) {
    DOS_TXN(modifyInner(dir, o, n, attr, err));
}

bool Volume::setLabel(const QString &label, QString *err) { DOS_TXN(setLabelInner(label, err)); }

bool Volume::setLabelInner(const QString &label, QString *err) {
    QString L = label.toUpper().trimmed();
    if (L.size() > 11) { *err = "a volume label can be at most 11 characters"; return false; }
    for (QChar c : L)
        if (!(dosChar(c) || c == ' ' || c == '.')) { *err = QString("character '%1' is not allowed in a volume label").arg(c); return false; }
    QVector<Region> rg;
    QByteArray dd;
    if (!dirRegions(0, &rg, err) || !readRegions(rg, &dd, err)) return false;
    int idx = -1;
    for (int i = 0; i + 32 <= dd.size(); i += 32) {
        quint8 f = quint8(dd[i]);
        if (f == 0) break;
        if (f == 0xE5) continue;
        quint8 a = quint8(dd[i + 11]);
        if ((a & 0x0F) == 0x0F) continue;
        if ((a & 0x08) && !(a & 0x10)) { idx = i / 32; break; }
    }
    if (L.isEmpty()) {
        if (idx >= 0 && !wr(slotOffset(rg, idx), QByteArray(1, char(0xE5)), err)) return false;
        m_label.clear();
        return true;
    }
    QByteArray raw(11, ' ');
    for (int i = 0; i < L.size(); ++i) raw[i] = L[i].toLatin1();
    quint16 d, t;
    nowStamp(&d, &t);
    if (idx < 0) {
        if (!freeSlot(0, rg, dd, &idx, err)) return false;
        char e[32] = {};
        memcpy(e, raw.constData(), 11);
        e[11] = 0x08;
        p16(e + 22, t); p16(e + 24, d);
        if (!wr(slotOffset(rg, idx), QByteArray(e, 32), err)) return false;
    } else {
        QByteArray head = dd.mid(idx * 32, 26);
        memcpy(head.data(), raw.constData(), 11);
        p16(head.data() + 22, t); p16(head.data() + 24, d);
        if (!wr(slotOffset(rg, idx), head, err)) return false;
    }
    QByteArray boot;                               // extended boot sector label, if the BPB has one
    if (m_hasBpb && rd(0, 512, &boot, err) && quint8(boot[38]) == 0x29 && !wr(43, raw, err)) return false;
    m_label = L;
    return true;
}

bool Volume::addFileInner(quint32 dir, const QString &name, const QByteArray &data, bool replace, QString *err) {
    QByteArray raw;
    QString why;
    if (!toRaw(name, &raw, &why)) { *err = QString("'%1': %2").arg(name, why); return false; }
    if (quint64(data.size()) > 0x7FFFFFFFull) { *err = "file too large"; return false; }
    QVector<Region> rg;
    QByteArray dd;
    if (!dirRegions(dir, &rg, err) || !readRegions(rg, &dd, err)) return false;
    int idx = findActive(dd, raw);
    if (idx >= 0) {
        if (dd[idx * 32 + 11] & 0x10) { *err = QString("'%1' is a directory").arg(name); return false; }
        if (!replace) { *err = QString("'%1' already exists").arg(name); return false; }
        quint32 oc = u16(dd.constData() + idx * 32 + 26);
        if (oc >= 2 && !freeChain(oc, err)) return false;
    }
    quint32 cb = clusterBytes(), n = quint32((quint64(data.size()) + cb - 1) / cb);
    QVector<quint32> ch;
    if (!alloc(n, &ch)) {
        *err = QString("not enough free space on the DOS volume for '%1' (needs %2 KB, %3 KB free)")
                   .arg(name).arg((quint64(n) * cb + 1023) / 1024).arg(quint64(freeClusters()) * cb / 1024);
        return false;
    }
    if (n && !writeChain(ch, data, err)) return false;
    if (idx < 0 && !freeSlot(dir, rg, dd, &idx, err)) return false;
    char e[32] = {};
    memcpy(e, raw.constData(), 11);
    e[11] = char(AttrA);
    quint16 d, t;
    nowStamp(&d, &t);
    p16(e + 14, t); p16(e + 16, d); p16(e + 18, d);                    // created / accessed
    p16(e + 22, t); p16(e + 24, d);                                    // modified
    p16(e + 26, ch.isEmpty() ? 0 : ch[0]);
    p32(e + 28, quint32(data.size()));
    return wr(slotOffset(rg, idx), QByteArray(e, 32), err) && flushFat(err);
}

bool Volume::makeDirInner(quint32 dir, const QString &name, QString *err, quint32 *newCluster) {
    QByteArray raw;
    QString why;
    if (!toRaw(name, &raw, &why)) { *err = QString("'%1': %2").arg(name, why); return false; }
    QVector<Region> rg;
    QByteArray dd;
    if (!dirRegions(dir, &rg, err) || !readRegions(rg, &dd, err)) return false;
    if (findActive(dd, raw) >= 0) { *err = QString("'%1' already exists").arg(name); return false; }
    QVector<quint32> ch;
    if (!alloc(1, &ch)) { *err = "no free cluster for a new directory"; return false; }
    quint16 d, t;
    nowStamp(&d, &t);
    QByteArray cl(int(clusterBytes()), 0);
    auto mk = [&](char *e, const char *nm, quint32 cluster) {
        memset(e, ' ', 11);
        memcpy(e, nm, strlen(nm));
        e[11] = 0x10;
        memset(e + 12, 0, 20);
        p16(e + 22, t); p16(e + 24, d); p16(e + 26, cluster);
    };
    mk(cl.data(), ".", ch[0]);
    mk(cl.data() + 32, "..", dir);
    if (!wr(clusterOff(ch[0]), cl, err)) return false;
    int idx;
    if (!freeSlot(dir, rg, dd, &idx, err)) return false;
    char e[32] = {};
    memcpy(e, raw.constData(), 11);
    e[11] = 0x10;
    p16(e + 22, t); p16(e + 24, d); p16(e + 26, ch[0]);
    if (!wr(slotOffset(rg, idx), QByteArray(e, 32), err) || !flushFat(err)) return false;
    if (newCluster) *newCluster = ch[0];
    return true;
}

bool Volume::removeTree(quint32 cluster, int depth, QString *err) {
    if (depth > 32) { *err = "directory nesting too deep"; return false; }
    QVector<DEntry> v;
    if (!listDir(cluster, &v, err)) return false;
    for (const DEntry &e : v) {
        if (e.name == "." || e.name == "..") continue;
        if (e.isDir()) { if (e.cluster >= 2 && !removeTree(e.cluster, depth + 1, err)) return false; }
        else if (e.cluster >= 2 && !freeChain(e.cluster, err)) return false;
    }
    return freeChain(cluster, err);
}

bool Volume::removeInner(quint32 dir, const QString &name, QString *err) {
    QByteArray raw;
    QString why;
    if (!toRaw(name, &raw, &why)) { *err = why; return false; }
    QVector<Region> rg;
    QByteArray dd;
    if (!dirRegions(dir, &rg, err) || !readRegions(rg, &dd, err)) return false;
    int idx = findActive(dd, raw);
    if (idx < 0) { *err = QString("'%1' not found").arg(name); return false; }
    const char *p = dd.constData() + idx * 32;
    quint32 c = u16(p + 26);
    if (p[11] & 0x10) { if (c >= 2 && !removeTree(c, 0, err)) return false; }
    else if (c >= 2 && !freeChain(c, err)) return false;
    return wr(slotOffset(rg, idx), QByteArray(1, char(0xE5)), err) && flushFat(err);
}

bool Volume::modifyInner(quint32 dir, const QString &oldName, const QString &newName, quint8 attr, QString *err) {
    QByteArray ro, rn;
    QString why;
    if (!toRaw(oldName, &ro, &why)) { *err = why; return false; }
    if (!toRaw(newName, &rn, &why)) { *err = QString("'%1': %2").arg(newName, why); return false; }
    QVector<Region> rg;
    QByteArray dd;
    if (!dirRegions(dir, &rg, err) || !readRegions(rg, &dd, err)) return false;
    int idx = findActive(dd, ro);
    if (idx < 0) { *err = QString("'%1' not found").arg(oldName); return false; }
    int clash = findActive(dd, rn);
    if (clash >= 0 && clash != idx) { *err = QString("'%1' already exists").arg(newName); return false; }
    QByteArray head = dd.mid(idx * 32, 12);
    memcpy(head.data(), rn.constData(), 11);
    quint8 old = quint8(head[11]);
    head[11] = char((old & ~0x27) | (attr & 0x27));
    return wr(slotOffset(rg, idx), head, err);
}

// ------------------------------------------------------------------- check

Report Volume::check() {
    Report r;
    QString err;
    if (!attached()) { r.errors << "no DOS volume"; return r; }
    if (!loadFat(&err)) { r.errors << err; return r; }
    for (quint32 k = 1; k < m_nfats; ++k) {
        QByteArray other;
        if (rd(quint64(m_res + k * m_spf) * m_bps, m_spf * m_bps, &other, &err) && other != m_fat)
            r.warnings << QString("FAT copy %1 differs from FAT copy 0").arg(k + 1);
    }
    if (quint8(m_fat[0]) != m_media) r.warnings << "FAT media byte differs from the boot sector";

    QVector<quint8> owner(int(m_clusters) + 2, 0);
    struct Item { quint32 cluster; QString path; quint32 parent; };
    QVector<Item> stack;
    stack.append({0, "C:\\", 0});
    int files = 0, dirs = 0;
    while (!stack.isEmpty()) {
        Item it = stack.takeLast();
        QVector<Region> rg;
        QByteArray dd;
        if (!dirRegions(it.cluster, &rg, &err) || !readRegions(rg, &dd, &err)) {
            r.errors << QString("%1: %2").arg(it.path, err);
            continue;
        }
        ++dirs;
        for (int i = 0; i + 32 <= dd.size(); i += 32) {
            const char *p = dd.constData() + i;
            quint8 f = quint8(p[0]);
            if (f == 0) break;
            if (f == 0xE5 || (p[11] & 0x0F) == 0x0F || (p[11] & 0x08)) continue;
            DEntry e = parseEntry(p);
            if (e.name == ".") {
                if (e.cluster != it.cluster) r.warnings << QString("%1: '.' entry points to the wrong cluster").arg(it.path);
                continue;
            }
            if (e.name == "..") {
                if (e.cluster != it.parent) r.warnings << QString("%1: '..' entry points to the wrong cluster").arg(it.path);
                continue;
            }
            QString path = it.path + e.name + (e.isDir() ? "\\" : "");
            if (e.cluster == 0) {
                if (e.isDir()) r.errors << path + ": directory has no cluster";
                else if (e.size) r.errors << path + ": file has a size but no clusters";
                if (!e.isDir()) ++files;
                continue;
            }
            QVector<quint32> ch;
            if (!chain(e.cluster, &ch, &err)) { r.errors << path + ": " + err; continue; }
            bool crossed = false;
            for (quint32 c : ch) {
                if (owner[int(c)]) { crossed = true; break; }
                owner[int(c)] = 1;
            }
            if (crossed) { r.errors << path + ": cluster chain is cross-linked with another file or directory"; continue; }
            if (e.isDir()) { stack.append({e.cluster, path, it.cluster}); continue; }
            ++files;
            quint32 need = quint32((quint64(e.size) + clusterBytes() - 1) / clusterBytes());
            if (quint32(ch.size()) < need) r.errors << path + ": cluster chain is shorter than the file size";
            else if (quint32(ch.size()) > need) r.warnings << path + ": has more clusters than its size needs";
        }
    }
    quint32 lost = 0, used = 0, bad = 0;
    for (quint32 c = 2; c <= maxCluster(); ++c) {
        quint32 v = fatGet(c);
        if (v == 0) continue;
        if (isBad(v)) { ++bad; continue; }
        ++used;
        if (!owner[int(c)]) ++lost;
    }
    m_lost = lost;
    if (lost) r.warnings << QString("%1 clusters are marked in use but belong to no file (lost space)").arg(lost);
    r.info << QString("DOS volume%1: %2 files, %3 directories, %4 of %5 clusters free (%6, %7-byte clusters)")
                  .arg(m_label.isEmpty() ? QString() : " '" + m_label + "'")
                  .arg(files).arg(dirs - 1).arg(m_clusters - used - bad).arg(m_clusters)
                  .arg(m_fat16 ? "FAT16" : "FAT12").arg(clusterBytes());
    return r;
}

} // namespace fat
} // namespace dw
