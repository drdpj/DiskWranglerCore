/*
 * FAT as a Volume - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include "fatvolume.h"

namespace dw {
namespace fat {

bool NameRules::valid(const QString &name, fs::NodeKind kind, QString *why) const {
    if (kind == fs::NodeKind::Namespace) { if (why) *why = "FAT has no namespaces"; return false; }
    return Volume::validName(name, why);
}

QString NameRules::fromHost(const QString &host, fs::NodeKind kind) const {
    return Volume::fromHost(host, kind == fs::NodeKind::Dir);
}

static void noDelete(fat::Volume *) {}

FsVolume::FsVolume(fat::Volume *v) : m_v(v, noDelete) {}
FsVolume::FsVolume(std::shared_ptr<fat::Volume> v) : m_v(std::move(v)) {}

fs::Caps FsVolume::caps() const {
    fs::Caps c;
    c.dirs = true;
    c.title = true;
    c.dates = true;
    return c;
}

QVector<fs::AttrSpec> FsVolume::fileAttrs() const {
    QVector<fs::AttrSpec> v;
    const struct { const char *key, *label, *sample, *letter; } a[] = {
        {"readonly", "Read-only", "R", "R"}, {"hidden", "Hidden", "H", "H"}, {"system", "System", "S", "S"}, {"archive", "Archive", "A", "A"}};
    for (const auto &x : a) {
        fs::AttrSpec s;
        s.key = x.key; s.label = x.label; s.type = fs::AttrSpec::Bool; s.sample = x.sample; s.letter = x.letter; s.dirs = true;
        s.retroFamily = "fat"; s.retroKey = x.key;
        v << s;
    }
    return v;
}

QVector<fs::AttrSpec> FsVolume::volumeAttrs() const {
    fs::AttrSpec l;
    l.key = "label"; l.label = "Label"; l.type = fs::AttrSpec::Text; l.sample = "MMMMMMMMMMM";
    return {l};
}

fs::VolumeInfo FsVolume::info() const {
    fs::VolumeInfo i;
    i.title = m_v->label();
    i.total = qint64(m_v->totalClusters()) * m_v->clusterBytes();
    i.free = qint64(m_v->freeClusters()) * m_v->clusterBytes();
    return i;
}

bool FsVolume::parsePath(const QString &text, fs::Path *out, QString *) const {
    QString t = text;
    t.replace('\\', '/');
    *out = fs::Path::fromString(t, '/');
    return true;
}

quint8 FsVolume::attrBits(const fs::AttrValues &a, quint8 base) {
    auto bit = [&](const char *k, quint8 b) {
        if (!a.contains(k)) return;
        if (a.value(k).toBool()) base |= b; else base &= quint8(~b);
    };
    bit("readonly", AttrR);
    bit("hidden", AttrH);
    bit("system", AttrS);
    bit("archive", AttrA);
    return base;
}

fs::AttrValues FsVolume::attrValues(quint8 b) {
    return {{"readonly", (b & AttrR) != 0}, {"hidden", (b & AttrH) != 0}, {"system", (b & AttrS) != 0},
            {"archive", (b & AttrA) != 0}};
}

bool FsVolume::dirCluster(const fs::Path &dir, quint32 *cluster, QString *err) const {
    quint32 c = 0;
    for (const QString &part : dir.parts) {
        QVector<DEntry> l;
        if (!m_v->listDir(c, &l, err)) return false;
        bool found = false;
        for (const DEntry &e : l)
            if (e.isDir() && e.name != "." && e.name != ".." && e.name.compare(part, Qt::CaseInsensitive) == 0) {
                c = e.cluster;
                found = true;
                break;
            }
        if (!found) { *err = "no such directory: " + dir.toString(); return false; }
    }
    *cluster = c;
    return true;
}

bool FsVolume::find(const fs::Path &node, quint32 *dir, DEntry *out, QString *err) const {
    if (node.isRoot()) { *err = "the root has no entry"; return false; }
    if (!dirCluster(node.parent(), dir, err)) return false;
    QVector<DEntry> l;
    if (!m_v->listDir(*dir, &l, err)) return false;
    for (const DEntry &e : l)
        if (e.name != "." && e.name != ".." && e.name.compare(node.name(), Qt::CaseInsensitive) == 0) { *out = e; return true; }
    *err = "not found: " + node.toString();
    return false;
}

bool FsVolume::list(const fs::Path &dir, QVector<fs::Entry> *out, QString *err) const {
    quint32 c;
    QVector<DEntry> l;
    if (!dirCluster(dir, &c, err) || !m_v->listDir(c, &l, err)) return false;
    out->clear();
    for (const DEntry &d : l) {
        if (d.name == "." || d.name == "..") continue;
        fs::Entry e;
        e.name = d.name;
        e.kind = d.isDir() ? fs::NodeKind::Dir : fs::NodeKind::File;
        e.size = d.isDir() ? 0 : d.size;
        e.modified = d.when();
        e.attrs = attrValues(d.attr);
        out->append(e);
    }
    return true;
}

bool FsVolume::read(const fs::Path &file, QByteArray *out, QString *err) const {
    quint32 dir;
    DEntry e;
    if (!find(file, &dir, &e, err)) return false;
    if (e.isDir()) { *err = file.toString() + " is a directory"; return false; }
    return m_v->readFile(e, out, err);
}

bool FsVolume::write(const fs::Path &dir, const QString &name, const QByteArray &data, const fs::AttrValues &a,
                     const QDateTime &, bool replace, QString *err) {
    quint32 c;
    if (!dirCluster(dir, &c, err) || !m_v->addFile(c, name, data, replace, err)) return false;
    const quint8 bits = attrBits(a, AttrA);
    if (bits == AttrA) return true;                                 // what addFile gives a new file
    return m_v->modify(c, name, name, bits, err);
}

bool FsVolume::mkdir(const fs::Path &dir, const QString &name, QString *err) {
    quint32 c;
    return dirCluster(dir, &c, err) && m_v->makeDir(c, name, err);
}

bool FsVolume::remove(const fs::Path &node, QString *err) {
    quint32 dir;
    DEntry e;
    return find(node, &dir, &e, err) && m_v->remove(dir, e.name, err);
}

bool FsVolume::rename(const fs::Path &from, const fs::Path &to, QString *err) {
    if (from.parent() != to.parent()) { *err = "moving between directories is not supported here"; return false; }
    quint32 dir;
    DEntry e;
    return find(from, &dir, &e, err) && m_v->modify(dir, e.name, to.name(), quint8(e.attr & (AttrR | AttrH | AttrS | AttrA)), err);
}

bool FsVolume::setAttrs(const fs::Path &node, const fs::AttrValues &a, QString *err) {
    quint32 dir;
    DEntry e;
    if (!find(node, &dir, &e, err)) return false;
    return m_v->modify(dir, e.name, e.name, attrBits(a, quint8(e.attr & (AttrR | AttrH | AttrS | AttrA))), err);
}

bool FsVolume::setVolumeAttrs(const fs::AttrValues &a, QString *err) {
    for (auto it = a.begin(); it != a.end(); ++it) {
        if (it.key() != "label") { *err = "FAT volumes have no " + it.key(); return false; }
        if (!m_v->setLabel(it.value().toString(), err)) return false;
    }
    return true;
}

} // namespace fat
} // namespace dw
