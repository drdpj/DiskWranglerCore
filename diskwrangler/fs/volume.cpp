/*
 * Filesystem interface - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include "volume.h"

namespace dw {
namespace fs {

Path Path::fromString(const QString &s, QChar sep) {
    Path p;
    for (const QString &x : s.split(sep))
        if (!x.isEmpty()) p.parts << x;
    return p;
}

Path Path::parent() const {
    Path p = *this;
    if (!p.parts.isEmpty()) p.parts.removeLast();
    return p;
}

Path Path::child(const QString &n) const {
    Path p = *this;
    p.parts << n;
    return p;
}

bool Volume::stat(const Path &node, Entry *out, QString *err) const {
    if (node.isRoot()) {
        *out = Entry();
        out->kind = NodeKind::Dir;
        return true;
    }
    QVector<Entry> l;
    if (!list(node.parent(), &l, err)) return false;
    const Qt::CaseSensitivity cs = names().caseSensitivity();
    for (const Entry &e : l)
        if (e.name.compare(node.name(), cs) == 0) { *out = e; return true; }
    *err = "not found: " + node.toString();
    return false;
}

bool Volume::mkdir(const Path &, const QString &, QString *err) {
    *err = "this filesystem has no directories";
    return false;
}

bool Volume::rename(const Path &, const Path &, QString *err) {
    *err = "renaming is not supported here";
    return false;
}

bool Volume::setVolumeAttrs(const AttrValues &a, QString *err) {
    if (a.isEmpty()) return true;
    *err = "this volume has no attributes to set";
    return false;
}

QMap<QString, QJsonObject> Volume::toRetro(const Entry &e) const { return retroFromSpecs(fileAttrs(), e.attrs, e.isDir()); }

AttrValues Volume::fromRetro(const QMap<QString, QJsonObject> &families, NodeKind kind) const {
    return specsFromRetro(fileAttrs(), families, kind != NodeKind::File);
}

Placement Volume::placeIncoming(const TransferItem &item, const Path &targetDir) const {
    Placement p;
    p.dir = targetDir;
    p.name = names().fromHost(item.name, item.isDir ? NodeKind::Dir : NodeKind::File);
    return p;
}

QMap<QString, QJsonObject> retroFromSpecs(const QVector<AttrSpec> &specs, const AttrValues &values, bool isDir) {
    QMap<QString, QJsonObject> out;
    for (const AttrSpec &s : specs) {
        if (s.retroFamily.isEmpty() || s.retroKey.isEmpty() || (isDir && !s.dirs) || !values.contains(s.key)) continue;
        const QVariant v = values.value(s.key);
        QJsonObject &o = out[s.retroFamily];
        switch (s.type) {
        case AttrSpec::Bool: o[s.retroKey] = v.toBool(); break;
        case AttrSpec::Hex: o[s.retroKey] = QString("%1").arg(v.toUInt(), 8, 16, QChar('0')).toUpper(); break;
        case AttrSpec::Int:
        case AttrSpec::Choice: o[s.retroKey] = v.toInt(); break;
        case AttrSpec::Date: o[s.retroKey] = v.toDateTime().toString(Qt::ISODate); break;
        case AttrSpec::Text: o[s.retroKey] = v.toString(); break;
        }
    }
    return out;
}

AttrValues specsFromRetro(const QVector<AttrSpec> &specs, const QMap<QString, QJsonObject> &families, bool isDir) {
    AttrValues out;
    for (const AttrSpec &s : specs) {
        if (s.retroFamily.isEmpty() || s.retroKey.isEmpty() || (isDir && !s.dirs)) continue;
        const QJsonObject o = families.value(s.retroFamily);
        if (!o.contains(s.retroKey)) continue;
        const QJsonValue v = o.value(s.retroKey);
        switch (s.type) {
        case AttrSpec::Bool: out[s.key] = v.toBool(); break;
        case AttrSpec::Hex: {
            bool ok = false;
            const quint32 n = v.toString().toUInt(&ok, 16);
            if (ok && v.isString()) out[s.key] = n;
            break;
        }
        case AttrSpec::Int:
        case AttrSpec::Choice: out[s.key] = v.toInt(); break;
        case AttrSpec::Date: out[s.key] = QDateTime::fromString(v.toString(), Qt::ISODate); break;
        case AttrSpec::Text: out[s.key] = v.toString(); break;
        }
    }
    return out;
}

} // namespace fs
} // namespace dw
