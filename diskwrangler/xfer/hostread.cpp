/*
 * Transfer layer - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include "hostread.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace dw {
namespace xfer {

namespace {

struct Reader {
    const HostReadOptions &o;
    QVector<HostReadError> *errors;

    void error(HostReadError::Kind kind, const QString &path, const QString &reason, bool named) {
        if (!errors) return;
        HostReadError e;
        e.kind = kind;
        e.path = path;
        e.reason = reason;
        e.named = named;
        errors->append(e);
    }

    // One file or folder; false when it is left out (with or without an error).
    bool read(const QString &path, const QFileInfo &fi, bool named, TransferItem *out) {
        TransferItem t;
        t.name = fi.fileName().isEmpty() ? QDir(path).dirName() : fi.fileName();
        t.when = fi.lastModified();
        t.hostPath = path;
        if (fi.isDir()) {
            t.isDir = true;
            // links inside a folder are skipped, files and folders alike (NoSymLinks); hidden files are not listed
            const QFileInfoList l = QDir(path).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                                                             QDir::Name | QDir::IgnoreCase | QDir::DirsFirst);
            for (const QFileInfo &k : l) {
                TransferItem kid;
                if (read(k.absoluteFilePath(), k, false, &kid)) t.kids.append(kid);
            }
            *out = t;
            return true;
        }
        if (o.fileHook && !o.fileHook(path, named, &t)) return false;
        if (o.sizeLimit >= 0 && fi.size() > o.sizeLimit) { error(HostReadError::TooLarge, path, QString(), named); return false; }
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) { error(HostReadError::CannotRead, path, f.errorString(), named); return false; }
        t.data = f.readAll();
        *out = t;
        return true;
    }
};

} // namespace

QVector<TransferItem> readHost(const QStringList &paths, const HostReadOptions &options, QVector<HostReadError> *errors) {
    Reader r{options, errors};
    QVector<TransferItem> items;
    for (const QString &p : paths) {
        const QFileInfo fi(p);
        if (!fi.exists()) {                                // (a link to nothing included)
            QFile f(p);
            f.open(QIODevice::ReadOnly);                   // for the system's own words
            r.error(HostReadError::NotFound, p, f.errorString(), true);
            continue;
        }
        TransferItem t;
        if (r.read(p, fi, true, &t)) items.append(t);
    }
    return items;
}

} // namespace xfer
} // namespace dw
