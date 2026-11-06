/*
 * Retro-files - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include "retrofiles.h"
#include "hostname.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUrl>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace dw {
namespace xfer {

const char *const RetroFilesMime = "application/x-retro-files+json";

// ------------------------------------------------------------------------------ writing to the host

QString UsedNames::unique(const QString &name, bool keepExtension) {
    QString n = name;
    int dot = keepExtension ? name.lastIndexOf('.') : -1;
    for (int k = 2; has(n); ++k)
        n = dot > 0 ? name.left(dot) + "_" + QString::number(k) + name.mid(dot) : name + "_" + QString::number(k);
    add(n);
    return n;
}

QString HostPolicy::hostName(const TransferItem &it) const {
    return hostSafeName(it.hostName.isEmpty() ? it.name : it.hostName);
}

bool HostPolicy::planNames(const QVector<TransferItem> &, const QString &, QVector<QString> *) const { return false; }

void HostPolicy::afterFile(const TransferItem &, const QString &, const QString &, const QString &, UsedNames &,
                           QStringList *, QStringList *) const {}

static void writeOne(const TransferItem &x, const QString &absDir, const QString &relDir, UsedNames &used, const QString *planned,
                     const HostPolicy &policy, QStringList *topLevel, QJsonArray *items, QStringList *errors) {
    const QString shown = policy.listedName(x), label = policy.errorLabel(x);
    const QString host = planned ? *planned : used.unique(policy.hostName(x), policy.keepExtension(x) && !x.isDir);
    const QString abs = absDir + "/" + host, rel = relDir.isEmpty() ? host : relDir + "/" + host;

    QJsonObject j;
    j["path"] = rel;
    j["type"] = x.isDir ? "directory" : "file";
    j["name"] = shown;
    if (!x.attrs.isEmpty()) {
        QJsonObject attrs;
        for (auto a = x.attrs.begin(); a != x.attrs.end(); ++a) attrs[a.key()] = a.value();
        j["attributes"] = attrs;
    }
    if (x.when.isValid()) j["modified"] = x.when.toString("yyyy-MM-ddTHH:mm:ss");

    if (x.isDir) {
        if (!QDir().mkpath(abs)) { errors->append(QString("%1: cannot create folder %2").arg(label, abs)); return; }
        items->append(j);
        if (topLevel) topLevel->append(rel);
        UsedNames inner;
        for (const TransferItem &k : x.kids) writeOne(k, abs, rel, inner, nullptr, policy, nullptr, items, errors);
        return;
    }
    const QByteArray data = policy.content(x);
    QFile f(abs);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(data) != data.size()) {
        errors->append(QString("%1: %2").arg(label, f.errorString()));
        return;
    }
    f.close();
    if (x.when.isValid()) {                       // the file's date, as the disc records it
        QFile t(abs);
        if (t.open(QIODevice::Append)) t.setFileTime(x.when, QFileDevice::FileModificationTime);
    }
    j["size"] = double(data.size());
    items->append(j);
    if (topLevel) topLevel->append(rel);
    policy.afterFile(x, absDir, relDir, host, used, topLevel, errors);
}

void writeHost(const QVector<TransferItem> &items, const QString &hostDir, const HostPolicy &policy,
               QStringList *topLevel, QJsonArray *jsonItems, QStringList *errors) {
    QDir().mkpath(hostDir);
    QVector<QString> planned;
    const bool havePlan = policy.planNames(items, hostDir, &planned) && planned.size() == items.size();
    std::map<QString, UsedNames> used;            // per folder: "" is the host folder itself
    QSet<QString> folders;
    for (int i = 0; i < items.size(); ++i) {
        const TransferItem &x = items[i];
        const QString folder = policy.topFolder(x);
        QString absDir = hostDir;
        if (!folder.isEmpty()) {
            absDir = hostDir + "/" + folder;
            if (!folders.contains(folder)) {
                folders.insert(folder);
                if (!QDir().mkpath(absDir)) { errors->append("cannot create folder " + folder); continue; }
                QJsonObject d;
                d["path"] = folder;
                d["type"] = "directory";
                d["name"] = folder;
                jsonItems->append(d);
                if (topLevel) topLevel->append(folder);
            }
        }
        writeOne(x, absDir, folder, used[folder], havePlan ? &planned[i] : nullptr, policy, folder.isEmpty() ? topLevel : nullptr,
                 jsonItems, errors);
    }
}

QByteArray makeJson(const QJsonArray &items, const QString &hostDir, const QString &imageName, const QString &filesystem,
                    const QString &generator) {
    QJsonObject src;
    src["filesystem"] = filesystem;
    if (!imageName.isEmpty()) src["image"] = imageName;
    QJsonObject o;
    o["format"] = "retro-files";
    o["version"] = 1;
    o["generator"] = generator;
    o["source"] = src;
    o["base"] = QUrl::fromLocalFile(hostDir).toString();
    o["items"] = items;
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

// ------------------------------------------------------------------------------ reading a drop

bool readJson(const QByteArray &json, QVector<TransferItem> *out, QString *err) {
    out->clear();
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (!doc.isObject()) { *err = "the dropped file list is not valid JSON: " + pe.errorString(); return false; }
    QJsonObject o = doc.object();
    if (o["format"].toString() != "retro-files") { *err = "the dropped data is not a retro-files list"; return false; }
    if (o["version"].toInt() != 1) { *err = QString("retro-files version %1 is not supported").arg(o["version"].toInt()); return false; }
    const QString fs = o["source"].toObject()["filesystem"].toString();
    QString base = QUrl(o["base"].toString()).toLocalFile();
    if (base.isEmpty() || !QFileInfo(base).isDir()) { *err = "the dropped files' folder is missing"; return false; }
    QString canonBase = QFileInfo(base).canonicalFilePath();

    struct Node { TransferItem x; std::vector<int> kids; };
    std::vector<Node> nodes;
    std::vector<int> roots;
    std::map<QString, int> byPath;
    for (const QJsonValue &v : o["items"].toArray()) {
        QJsonObject it = v.toObject();
        QString path = it["path"].toString();
        QStringList parts = path.split('/');
        if (path.isEmpty() || path.startsWith('/') || parts.contains("..") || parts.contains(".") || parts.contains("")) {
            *err = QString("'%1': not a path inside the drop folder").arg(path);
            return false;
        }
        Node n;
        TransferItem &x = n.x;
        x.isDir = it["type"].toString() == "directory";
        x.name = it["name"].toString().isEmpty() ? parts.last() : it["name"].toString();
        x.sourceFs = fs;
        x.when = QDateTime::fromString(it["modified"].toString(), "yyyy-MM-ddTHH:mm:ss");
        const QJsonObject attrs = it["attributes"].toObject();
        for (auto a = attrs.begin(); a != attrs.end(); ++a) x.attrs[a.key()] = a.value().toObject();
        QString abs = QDir(base).filePath(path);
        QString canon = QFileInfo(abs).canonicalFilePath();
        if (canon.isEmpty() || !canon.startsWith(canonBase + "/")) {
            *err = QString("'%1': the dropped file is missing or outside the drop folder").arg(path);
            return false;
        }
        if (!x.isDir) {
            QFile f(abs);
            if (!f.open(QIODevice::ReadOnly)) { *err = QString("%1: %2").arg(path, f.errorString()); return false; }
            x.data = f.readAll();
            if (it.contains("size") && qint64(it["size"].toDouble()) != x.data.size()) {
                *err = QString("'%1': the file is not the size the list says").arg(path);
                return false;
            }
        }
        int idx = int(nodes.size());
        nodes.push_back(n);
        QString parent = parts.size() > 1 ? path.left(path.lastIndexOf('/')) : QString();
        auto p = byPath.find(parent);
        if (!parent.isEmpty() && p != byPath.end() && nodes[size_t(p->second)].x.isDir) nodes[size_t(p->second)].kids.push_back(idx);
        else roots.push_back(idx);             // a parent that isn't listed: take it as a top-level item
        byPath[path] = idx;
    }
    std::function<TransferItem(int)> build = [&](int i) {
        TransferItem x = nodes[size_t(i)].x;
        for (int k : nodes[size_t(i)].kids) x.kids.append(build(k));
        return x;
    };
    for (int r : roots) out->append(build(r));
    if (out->isEmpty()) { *err = "the dropped list has no files"; return false; }
    return true;
}

// ------------------------------------------------------------------------------ temporary folders

static std::vector<std::unique_ptr<QTemporaryDir>> &dragDirs() {
    static std::vector<std::unique_ptr<QTemporaryDir>> dirs;    // removed when the program exits
    return dirs;
}

QString newDragDir(const QString &prefix, QString *err) {
    std::unique_ptr<QTemporaryDir> d(new QTemporaryDir(prefix + "XXXXXX"));
    if (!d->isValid()) { *err = "cannot create a temporary folder: " + d->errorString(); return QString(); }
    QString p = d->path();
    dragDirs().push_back(std::move(d));
    return p;
}

int cleanStale(const QString &prefix, qint64 maxAgeSecs) {
    int n = 0;
    QFileInfo pre(prefix);
    QDir tmp(pre.absolutePath());
    for (const QFileInfo &fi : tmp.entryInfoList({pre.fileName() + "*"}, QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (fi.lastModified().secsTo(QDateTime::currentDateTime()) < maxAgeSecs) continue;
        if (QDir(fi.absoluteFilePath()).removeRecursively()) ++n;
    }
    return n;
}

} // namespace xfer
} // namespace dw
