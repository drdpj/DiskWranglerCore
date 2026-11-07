/*
 * Acorn DFS as a Volume - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include "dfsvolume.h"

namespace dw {
namespace dfs {

void splitName(const QString &full, QChar *dir, QString *name) {
    if (full.size() >= 3 && full[1] == '.') { *dir = full[0]; *name = full.mid(2); }
    else { *dir = '$'; *name = full; }
}

bool NameRules::valid(const QString &full, fs::NodeKind kind, QString *why) const {
    if (kind != fs::NodeKind::File) { if (why) *why = "DFS has no directories"; return false; }
    QChar d;
    QString n;
    splitName(full, &d, &n);
    if (!validDir(d)) { if (why) *why = QString("'%1' can't be a DFS directory").arg(d); return false; }
    return validName(n, why);
}

QString NameRules::fromHost(const QString &host, fs::NodeKind) const {
    QChar d;
    QString n;
    splitName(host, &d, &n);
    n = sanitizeName(n);
    return d == '$' || !validDir(d) ? n : QString(d) + "." + n;
}

static void noDelete(Disc *) {}

Volume::Volume(Disc *disc) : m_d(disc, noDelete) {}
Volume::Volume(std::shared_ptr<Disc> disc) : m_d(std::move(disc)) {}

fs::Caps Volume::caps() const {
    fs::Caps c;
    c.title = true;
    c.contiguousOnly = true;
    c.maxEntriesPerDir = m_d->maxFiles();
    return c;
}

QVector<fs::AttrSpec> Volume::fileAttrs() const {
    fs::AttrSpec locked;
    locked.key = "locked"; locked.label = "Locked"; locked.type = fs::AttrSpec::Bool; locked.sample = "L";
    fs::AttrSpec load;
    load.key = "load"; load.label = "Load"; load.type = fs::AttrSpec::Hex; load.bits = 18; load.sample = "FF1900";
    fs::AttrSpec exec = load;
    exec.key = "exec"; exec.label = "Exec";
    locked.retroFamily = load.retroFamily = exec.retroFamily = "acorn";
    locked.retroKey = "access"; load.retroKey = "load"; exec.retroKey = "exec";
    return {locked, load, exec};
}

QVector<fs::AttrSpec> Volume::volumeAttrs() const {
    fs::AttrSpec title;
    title.key = "title"; title.label = "Title"; title.type = fs::AttrSpec::Text; title.sample = "MMMMMMMMMMMM";
    fs::AttrSpec boot;
    boot.key = "boot"; boot.label = "Boot option"; boot.type = fs::AttrSpec::Choice;
    for (int i = 0; i < 4; ++i) boot.choices << bootOptionName(i);
    return {title, boot};
}

fs::AttrValues Volume::volumeValues() const {
    return {{"title", m_d->title()}, {"boot", m_d->bootOption()}};
}

fs::VolumeInfo Volume::info() const {
    fs::VolumeInfo i;
    i.title = m_d->title();
    i.total = qint64(m_d->sectors()) * SectorSize;
    i.free = qint64(m_d->freeSectors()) * SectorSize;
    i.largestFree = qint64(m_d->largestFree()) * SectorSize;
    i.entriesFree = m_d->maxFiles() - m_d->files().size();
    return i;
}

bool Volume::list(const fs::Path &dir, QVector<fs::Entry> *out, QString *err) const {
    if (!dir.isRoot()) { *err = "DFS has no directories"; return false; }
    out->clear();
    for (const File &f : m_d->files()) {
        fs::Entry e;
        e.name = f.fullName();
        e.size = f.length;
        e.attrs = {{"locked", f.locked}, {"load", f.load}, {"exec", f.exec}};
        out->append(e);
    }
    return true;
}

bool Volume::find(const fs::Path &p, int *idx, QString *err) const {
    if (p.parts.size() != 1) { *err = "no such file: " + p.toString(); return false; }
    QChar d;
    QString n;
    splitName(p.parts[0], &d, &n);
    *idx = m_d->indexOf(d, n);
    if (*idx < 0) { *err = "no such file: " + p.parts[0]; return false; }
    return true;
}

bool Volume::read(const fs::Path &file, QByteArray *out, QString *err) const {
    int i;
    return find(file, &i, err) && m_d->readFile(m_d->files()[i], out, err);
}

bool Volume::write(const fs::Path &dir, const QString &full, const QByteArray &data, const fs::AttrValues &a,
                   const QDateTime &, bool replace, QString *err) {
    if (!dir.isRoot()) { *err = "DFS has no directories"; return false; }
    QChar d;
    QString n;
    splitName(full, &d, &n);
    return m_d->addFile(d, n, data, a.value("load").toUInt() & 0x3FFFFu, a.value("exec").toUInt() & 0x3FFFFu,
                        a.value("locked").toBool(), replace, err);
}

bool Volume::remove(const fs::Path &node, QString *err) {
    int i;
    if (!find(node, &i, err)) return false;
    const File f = m_d->files()[i];
    return m_d->remove(f.dir, f.name, err);
}

bool Volume::rename(const fs::Path &from, const fs::Path &to, QString *err) {
    int i;
    if (!find(from, &i, err)) return false;
    if (to.parts.size() != 1) { *err = "DFS has no directories"; return false; }
    const File f = m_d->files()[i];
    QChar d;
    QString n;
    splitName(to.parts[0], &d, &n);
    return m_d->modify(f.dir, f.name, d, n, f.locked, f.load, f.exec, err);
}

bool Volume::setAttrs(const fs::Path &node, const fs::AttrValues &a, QString *err) {
    int i;
    if (!find(node, &i, err)) return false;
    const File f = m_d->files()[i];
    return m_d->modify(f.dir, f.name, f.dir, f.name, a.value("locked", f.locked).toBool(),
                       a.value("load", f.load).toUInt() & 0x3FFFFu, a.value("exec", f.exec).toUInt() & 0x3FFFFu, err);
}

bool Volume::setVolumeAttrs(const fs::AttrValues &a, QString *err) {
    for (auto it = a.begin(); it != a.end(); ++it) {
        if (it.key() == "title") { if (!m_d->setTitle(it.value().toString(), err)) return false; }
        else if (it.key() == "boot") { if (!m_d->setBootOption(it.value().toInt(), err)) return false; }
        else { *err = "DFS discs have no " + it.key(); return false; }
    }
    return true;
}

static QString hex8(quint32 v) { return QString("%1").arg(v, 8, 16, QChar('0')).toUpper(); }

QMap<QString, QJsonObject> Volume::toRetro(const fs::Entry &e) const {
    QJsonObject a;
    a["load"] = hex8(extendAddr(e.attrs.value("load").toUInt()));
    a["exec"] = hex8(extendAddr(e.attrs.value("exec").toUInt()));
    a["access"] = e.attrs.value("locked").toBool() ? "L" : "WR";
    return {{"acorn", a}};
}

fs::AttrValues Volume::fromRetro(const QMap<QString, QJsonObject> &families, fs::NodeKind) const {
    fs::AttrValues v;
    const QJsonObject a = families.value("acorn");
    for (const char *k : {"load", "exec"}) {
        bool ok = false;
        const quint32 n = a.value(k).toString().toUInt(&ok, 16);
        if (ok) v[k] = n & 0x3FFFFu;
    }
    bool locked = a.value("access").toString().contains('L');        // case-sensitive: 'l' is public
    for (auto f = families.begin(); f != families.end(); ++f)           // any family's "readonly" (fat, cpm)
        if (f.value().value("readonly").toBool()) locked = true;
    v["locked"] = locked;
    return v;
}

} // namespace dfs
} // namespace dw
