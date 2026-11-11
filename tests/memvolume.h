/*
 * A small volume for the window kit's tests - part of DiskWranglerCore
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

// MemVolume: a filesystem with directories, kept as a JSON document in a region of an in-memory raw container, so a
// Disc's transactions really roll it back (the container's staged writes are put back and the volume reads the
// document again). One "locked" attribute: a locked file can't be removed or replaced (or renamed). A title (a volume
// attribute, up to 10 characters). check() finds an error in every file named "bad..." and a warning in every file
// named "warn..." (step 5f). MemDocument: a Document over a
// Disc of one or more MemVolumes (two in one group = two root rows in one pane, like CP/M's sides). For the window
// kit's tests (uitest); later series' tests reuse it.

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <memory>

#include <diskwrangler/app/document.h>
#include <diskwrangler/device/container.h>
#include <diskwrangler/fs/disc.h>
#include <diskwrangler/fs/volume.h>

namespace memvol {

class PlainNames : public dw::fs::NameRules {
public:
    bool valid(const QString &n, dw::fs::NodeKind, QString *why) const override {
        if (n.isEmpty() || n.contains('/')) { *why = "names can't be empty or hold /"; return false; }
        return true;
    }
    QString fromHost(const QString &n, dw::fs::NodeKind) const override { QString s = n; return s.replace('/', '_'); }
};

class MemVolume : public dw::fs::Volume {
public:
    struct Node { bool dir = false; QByteArray data; bool locked = false; };
    // Keeps its document at byte offset `off` of c (up to `size` bytes).
    MemVolume(dw::device::Container *c, qint64 off, qint64 size) : m_c(c), m_off(off), m_size(size) {}

    // A fresh, empty volume written to the container (staged).
    bool format(QString *err) { m_nodes.clear(); return store(err); }

    QString fsId() const override { return "mem"; }
    dw::fs::Caps caps() const override { dw::fs::Caps c; c.dirs = true; return c; }
    QVector<dw::fs::AttrSpec> fileAttrs() const override {
        dw::fs::AttrSpec l;
        l.key = "locked"; l.label = "Locked"; l.type = dw::fs::AttrSpec::Bool; l.letter = "L";
        l.retroFamily = "mem"; l.retroKey = "locked";
        return {l};
    }
    QVector<dw::fs::AttrSpec> volumeAttrs() const override {
        dw::fs::AttrSpec t;
        t.key = "title"; t.label = "Title"; t.type = dw::fs::AttrSpec::Text;
        return {t};
    }
    dw::fs::AttrValues volumeValues() const override { return {{"title", m_title}}; }
    const dw::fs::NameRules &names() const override { return m_names; }
    dw::fs::VolumeInfo info() const override { dw::fs::VolumeInfo i; i.total = m_size; i.title = m_title; return i; }
    QString title() const { return m_title; }

    bool list(const dw::fs::Path &dir, QVector<dw::fs::Entry> *out, QString *err) const override {
        out->clear();
        if (!dir.isRoot() && !(m_nodes.contains(key(dir)) && m_nodes[key(dir)].dir)) { *err = "no such directory: " + key(dir); return false; }
        for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
            const dw::fs::Path p = dw::fs::Path::fromString(it.key());
            if (p.parent() != dir) continue;
            dw::fs::Entry e;
            e.name = p.name();
            e.kind = it->dir ? dw::fs::NodeKind::Dir : dw::fs::NodeKind::File;
            e.size = it->data.size();
            e.attrs = {{"locked", it->locked}};
            out->append(e);
        }
        return true;
    }
    bool read(const dw::fs::Path &p, QByteArray *out, QString *err) const override {
        if (!m_nodes.contains(key(p)) || m_nodes[key(p)].dir) { *err = "no such file: " + key(p); return false; }
        *out = m_nodes[key(p)].data;
        return true;
    }
    bool write(const dw::fs::Path &dir, const QString &name, const QByteArray &data, const dw::fs::AttrValues &a,
               const QDateTime &, bool replace, QString *err) override {
        const QString k = key(dir.child(name));
        if (m_nodes.contains(k) && (!replace || m_nodes[k].locked)) { *err = k + (replace ? " is locked" : " already exists"); return false; }
        m_nodes[k] = Node{false, data, a.value("locked").toBool()};
        return store(err);
    }
    bool mkdir(const dw::fs::Path &dir, const QString &name, QString *err) override {
        m_nodes[key(dir.child(name))] = Node{true, QByteArray(), false};
        return store(err);
    }
    bool remove(const dw::fs::Path &p, QString *err) override {
        const QString k = key(p);
        if (!m_nodes.contains(k)) { *err = "no such file: " + k; return false; }
        if (m_nodes[k].locked) { *err = k + " is locked"; return false; }
        for (const QString &c : m_nodes.keys()) if (c.startsWith(k + "/")) m_nodes.remove(c);
        m_nodes.remove(k);
        return store(err);
    }
    bool setAttrs(const dw::fs::Path &p, const dw::fs::AttrValues &a, QString *err) override {
        if (!m_nodes.contains(key(p))) { *err = "no such file: " + key(p); return false; }
        if (a.contains("locked")) m_nodes[key(p)].locked = a["locked"].toBool();
        return store(err);
    }
    bool rename(const dw::fs::Path &from, const dw::fs::Path &to, QString *err) override {
        const QString f = key(from), t = key(to);
        if (!m_nodes.contains(f)) { *err = "no such file: " + f; return false; }
        if (m_nodes[f].locked) { *err = f + " is locked"; return false; }
        if (m_nodes.contains(t)) { *err = t + " already exists"; return false; }
        if (!to.parent().isRoot() && !(m_nodes.contains(key(to.parent())) && m_nodes[key(to.parent())].dir)) {
            *err = "no such directory: " + key(to.parent());
            return false;
        }
        for (const QString &c : m_nodes.keys())
            if (c.startsWith(f + "/")) { m_nodes[t + c.mid(f.size())] = m_nodes[c]; m_nodes.remove(c); }
        m_nodes[t] = m_nodes[f];
        m_nodes.remove(f);
        return store(err);
    }
    bool setVolumeAttrs(const dw::fs::AttrValues &a, QString *err) override {
        for (auto it = a.begin(); it != a.end(); ++it) {
            if (it.key() != "title") { *err = "no such attribute: " + it.key(); return false; }
            if (it.value().toString().size() > 10) { *err = "a title has at most 10 characters"; return false; }
            m_title = it.value().toString();
        }
        return store(err);
    }
    dw::fs::Report check() const override {
        dw::fs::Report r;
        for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
            const QString n = dw::fs::Path::fromString(it.key()).name();
            if (n.startsWith("bad")) r.errors << it.key() + ": bad";
            if (n.startsWith("warn")) r.warnings << it.key() + ": odd";
        }
        return r;
    }
    bool reload(QString *err) override {
        QByteArray len, doc;
        if (!m_c->readStored(m_off, 4, &len, err)) return false;
        const quint32 n = quint32(uchar(len[0])) | quint32(uchar(len[1])) << 8 | quint32(uchar(len[2])) << 16 | quint32(uchar(len[3])) << 24;
        m_nodes.clear();
        m_title.clear();
        if (n == 0) return true;
        if (n > quint32(m_size - 4) || !m_c->readStored(m_off + 4, n, &doc, err)) { if (err->isEmpty()) *err = "bad length"; return false; }
        const QJsonObject o = QJsonDocument::fromJson(doc).object();
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (it.key() == "/title") { m_title = it.value().toString(); continue; }    // no name holds a /
            const QJsonObject v = it.value().toObject();
            m_nodes[it.key()] = Node{v["dir"].toBool(), QByteArray::fromBase64(v["data"].toString().toLatin1()), v["locked"].toBool()};
        }
        return true;
    }

private:
    static QString key(const dw::fs::Path &p) { return p.toString(); }
    bool store(QString *err) {
        QJsonObject o;
        for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it)
            o[it.key()] = QJsonObject{{"dir", it->dir}, {"data", QString::fromLatin1(it->data.toBase64())}, {"locked", it->locked}};
        if (!m_title.isEmpty()) o["/title"] = m_title;
        const QByteArray doc = QJsonDocument(o).toJson(QJsonDocument::Compact);
        if (doc.size() + 4 > m_size) { *err = "the volume is full"; return false; }
        const quint32 n = quint32(doc.size());
        QByteArray b;
        b.append(char(n & 0xFF)).append(char(n >> 8 & 0xFF)).append(char(n >> 16 & 0xFF)).append(char(n >> 24 & 0xFF));
        return m_c->writeStored(m_off, b + doc, err);
    }
    dw::device::Container *m_c;
    qint64 m_off, m_size;
    QMap<QString, Node> m_nodes;
    QString m_title;
    PlainNames m_names;
};

// A document of `mounts` MemVolumes, each in its own 64K of one in-memory raw container, all in group "mem".
class MemDocument : public dw::app::Document {
public:
    explicit MemDocument(int mounts = 1, const QString &path = QString()) : m_path(path) {
        dw::device::RawLayout l;
        l.seclen = 256; l.sectrk = 16; l.cyls = 16 * mounts; l.heads = 1;
        m_c.createBlank(dw::device::Kind::Raw, l, QString());
        m_disc.setContainer(&m_c);
        QString err;
        for (int i = 0; i < mounts; ++i) {
            auto v = std::make_shared<MemVolume>(&m_c, qint64(i) * 65536, 65536);
            v->format(&err);
            m_disc.addMount({mounts > 1 ? QString("part %1").arg(i) : QString("MEM"), v, false, QString(), "mem"});
        }
        m_savedBytes = stored();
    }
    MemVolume *vol(int i) { return static_cast<MemVolume *>(m_disc.volume(i)); }
    dw::device::Container &container() { return m_c; }
    QString readOnly;                            // a reason every mount is read-only, for the tests

    dw::fs::Disc &disc() override { return m_disc; }
    const dw::fs::Disc &disc() const override { return m_disc; }
    bool isOpen() const override { return !closed; }
    bool closed = false;                         // no image open (the tests of opening, step 5g)
    QString path() const override { return m_path; }
    // Nothing is written anywhere: a save remembers the bytes as they are, and the document is changed again when
    // they differ.
    bool dirty() const override { return stored() != m_savedBytes; }
    bool save(bool, QString *) override { m_savedBytes = stored(); m_saved++; return true; }
    bool saveAs(const QString &p, dw::device::Kind, QString *) override { m_path = p; return true; }
    QString readOnlyWhy(int) const override { return readOnly; }
    int saves() const { return m_saved; }
    void markClean() { m_savedBytes = stored(); }       // as if what is there now had been saved (not counted)

private:
    QByteArray stored() const {
        QByteArray b;
        QString err;
        m_c.readStored(0, m_c.rawLayout().bytes(), &b, &err);
        return b;
    }
    dw::device::Container m_c;
    dw::fs::Disc m_disc;
    QByteArray m_savedBytes;
    QString m_path;
    int m_saved = 0;
};

} // namespace memvol
