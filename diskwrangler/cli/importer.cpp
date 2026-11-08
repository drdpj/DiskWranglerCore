/*
 * Command-line layer - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include "importer.h"

#include <diskwrangler/app/clock.h>

namespace dw {
namespace cli {

QJsonObject Change::json() const {
    QJsonObject o;
    o["op"] = op;
    o["vol"] = vol;
    o["path"] = path;
    o["from"] = from.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(from);
    o["note"] = note.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(note);
    return o;
}

QJsonObject ImportResult::json() const {
    QJsonObject o;
    o["added"] = added;
    o["replaced"] = replaced;
    o["skipped"] = skipped;
    o["dirs"] = dirs;
    QJsonArray a;
    for (const auto &p : renamed) {
        QJsonObject x;
        x["from"] = p.first;
        x["to"] = p.second;
        a.append(x);
    }
    o["renamed"] = a;
    return o;
}

static Error fail(int code, const QString &msg, const QString &path = QString()) {
    Error e;
    e.code = code;
    e.message = msg;
    e.path = path;
    return e;
}

bool Importer::exists(const fs::Path &p, fs::Entry *e) const {
    QString err;
    fs::Entry x;
    return m_v->stat(p, e ? e : &x, &err);
}

QString Importer::madeKey(const fs::Path &dir, const QString &name) const {
    const bool cs = m_v->names().caseSensitivity() == Qt::CaseSensitive;
    const QString k = m_v->displayPath(dir) + QChar(0) + name;
    return cs ? k : k.toLower();
}

// NAME -> NAME~1, NAME~2 ...: the base is shortened until the volume takes the name (a FAT extension and a DFS
// directory letter stay), and the name is free in the volume and in what this run made.
QString Importer::uniqueName(const fs::Path &dir, const QString &name, NodeKind kind) const {
    QString prefix = name, stem, ext;
    if (m_v->fsId() == "dfs" && name.size() >= 3 && name[1] == '.') { prefix = name.left(2); stem = name.mid(2); }
    else { prefix.clear(); stem = name; }
    if (kind == NodeKind::File && m_v->caps().dirs) {                    // FAT: the extension stays after the suffix
        const int dot = stem.lastIndexOf('.');
        if (dot > 0) { ext = stem.mid(dot); stem = stem.left(dot); }
    }
    for (int i = 1; i < 1000; ++i) {
        const QString suf = "~" + QString::number(i);
        for (int keep = stem.size(); keep >= 1; --keep) {
            const QString cand = prefix + stem.left(keep) + suf + ext;
            QString why;
            if (!m_v->names().valid(cand, kind, &why)) continue;
            if (m_made.contains(madeKey(dir, cand)) || exists(dir.child(cand), nullptr)) break;   // taken: next number
            return cand;
        }
    }
    return name;
}

bool Importer::ensureDir(const fs::Path &dir, ImportResult *r, Error *err) {
    fs::Path cur;
    for (const QString &part : dir.parts) {
        const fs::Path next = cur.child(part);
        fs::Entry e;
        if (exists(next, &e)) {
            if (!e.isDir()) { *err = fail(ExitFailed, QString("a file called %1 is in the way").arg(m_v->displayPath(next)), m_v->displayPath(next)); return false; }
        } else {
            QString why;
            if (!m_v->mkdir(cur, part, &why)) { *err = fail(ExitFailed, QString("%1: %2").arg(m_v->displayPath(next), why), m_v->displayPath(next)); return false; }
            if (m_v->caps().dirs) {
                ++r->dirs;
                Change c; c.op = "mkdir"; c.vol = m_label; c.path = m_v->displayPath(next);
                r->changes << c;
            }
        }
        cur = next;
    }
    return true;
}

bool Importer::import(const QVector<TransferItem> &items, const QStringList &froms, const fs::Path &dir, ImportResult *r, Error *err) {
    if (!ensureDir(dir, r, err)) return false;
    for (int i = 0; i < items.size(); ++i)
        if (!importOne(items[i], i < froms.size() ? froms[i] : items[i].name, dir, r, err)) return false;
    return true;
}

bool Importer::importOne(const TransferItem &it, const QString &from, const fs::Path &dir, ImportResult *r, Error *err) {
    const bool isDir = it.isDir;
    // a folder (or CP/M user area) arriving where there are no directories: its files go in the target directory
    if (isDir && !m_v->caps().dirs) {
        const QString id = m_v->fsId();
        const QString fsName = id == "cpm" ? QString("CP/M") : id == "torch" ? QString("Torch CP/N") : id.toUpper();
        r->warnings << QString("%1 has no directories: the files of '%2' were put in %3").arg(fsName, it.name, m_v->displayPath(dir));
        for (const TransferItem &k : it.kids)
            if (!importOne(k, from + "/" + k.name, dir, r, err)) return false;
        return true;
    }
    fs::Placement p = m_v->placeIncoming(it, dir);
    if (!p.warning.isEmpty()) r->warnings << (p.warning.startsWith(from + ": ") ? p.warning : QString("%1: %2").arg(from, p.warning));   // a name's own warning may already say which
    if (!ensureDir(p.dir, r, err)) return false;
    QString name = p.name;
    const NodeKind kind = isDir ? NodeKind::Dir : NodeKind::File;
    if (m_made.contains(madeKey(p.dir, name))) {
        const QString u = uniqueName(p.dir, name, kind);
        r->renamed.append({name, u});
        r->warnings << QString("%1: the name %2 was already made in this run: called %3 instead").arg(from, name, u);
        name = u;
    }
    const fs::Path target = p.dir.child(name);
    const QString shown = m_v->displayPath(target);
    fs::Entry ex;
    const bool there = exists(target, &ex);

    if (isDir) {
        if (there && !ex.isDir()) { *err = fail(ExitFailed, QString("a file called %1 is in the way").arg(shown), shown); return false; }
        if (!there) {
            QString why;
            if (!m_v->mkdir(p.dir, name, &why)) { *err = fail(ExitFailed, QString("%1: %2").arg(shown, why), shown); return false; }
            ++r->dirs;
            Change c; c.op = "mkdir"; c.vol = m_label; c.path = shown; c.from = from; c.note = p.warning;
            r->changes << c;
        }
        m_made.insert(madeKey(p.dir, name));
        for (const TransferItem &k : it.kids)
            if (!importOne(k, from + "/" + k.name, target, r, err)) return false;
        return true;
    }

    bool replace = false;
    if (there) {
        if (ex.isDir()) { *err = fail(ExitFailed, QString("a directory called %1 is in the way").arg(shown), shown); return false; }
        if (m_o.skip) {
            ++r->skipped;
            Change c; c.op = "skip"; c.vol = m_label; c.path = shown; c.from = from; c.note = "exists";
            r->changes << c;
            return true;
        }
        if (!m_o.replace) { *err = fail(ExitConflict, QString("%1 exists: use --replace or --skip").arg(shown), shown); return false; }
        const QVector<fs::AttrSpec> specs = m_v->fileAttrs();
        for (const fs::AttrSpec &s : specs) {
            if (s.type != fs::AttrSpec::Bool || (s.key != "locked" && s.key != "readonly") || !ex.attrs.value(s.key).toBool()) continue;
            if (!m_o.force) { *err = fail(ExitUnsupported, QString("%1 is %2: use --force to replace it").arg(shown, s.key == "locked" ? "locked" : "read-only"), shown); return false; }
            QString why;
            if (!m_v->setAttrs(target, {{s.key, false}}, &why)) { *err = fail(ExitFailed, QString("%1: %2").arg(shown, why), shown); return false; }
        }
        replace = true;
    }
    fs::AttrValues attrs = m_v->fromRetro(it.attrs, NodeKind::File);
    if (attrs.isEmpty() && replace) {                        // a plain file over a file keeps the old file's attributes
        attrs = ex.attrs;
        for (const fs::AttrSpec &s : m_v->fileAttrs())
            if (s.key == "locked" || s.key == "readonly") attrs.remove(s.key);     // (cleared above when forced)
    }
    const QDateTime when = it.when.isValid() ? it.when : dw::now();
    QString why;
    if (!m_v->write(p.dir, name, it.data, attrs, when, replace, &why)) {
        *err = fail(ExitFailed, QString("%1: %2").arg(shown, why), shown);
        return false;
    }
    m_made.insert(madeKey(p.dir, name));
    if (replace) ++r->replaced; else ++r->added;
    Change c; c.op = replace ? "replace" : "add"; c.vol = m_label; c.path = shown; c.from = from; c.note = p.warning;
    r->changes << c;
    return true;
}

} // namespace cli
} // namespace dw
