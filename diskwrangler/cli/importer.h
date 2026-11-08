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
#pragma once

// The Importer: one set of rules for putting items into a volume, for both put and copy (docs/cli.md, "Importing").
// It works through the Volume interface only, inside the caller's transaction; the first failure stops it and the
// caller abandons the run. QtCore only.

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <diskwrangler/cli/cli.h>
#include <diskwrangler/fs/volume.h>

namespace dw {
namespace cli {

struct Change {
    QString op;                          // add, replace, skip, remove, mkdir, rename, attr, title, boot, convert, new
    QString vol;                         // the mount's label
    QString path;                        // display path on the volume (or the file made)
    QString from;                        // the source, or empty
    QString note;
    QJsonObject json() const;
};

struct ImportResult {
    int added = 0, replaced = 0, skipped = 0, dirs = 0;
    QVector<QPair<QString, QString>> renamed;      // from, to
    QVector<Change> changes;
    QStringList warnings;
    QJsonObject json() const;            // {added, replaced, skipped, dirs, renamed}
};

class Importer {
public:
    typedef fs::NodeKind NodeKind;
    Importer(fs::Volume *vol, const QString &volLabel, const ImportOptions &o) : m_v(vol), m_label(volLabel), m_o(o) {}
    // Imports the items into directory dir (made if it doesn't exist). froms[i] is item i's source, for the log.
    // false: err is set (code and message) and the run is to be abandoned.
    bool import(const QVector<TransferItem> &items, const QStringList &froms, const fs::Path &dir, ImportResult *r, Error *err);

private:
    bool importOne(const TransferItem &it, const QString &from, const fs::Path &dir, ImportResult *r, Error *err);
    bool ensureDir(const fs::Path &dir, ImportResult *r, Error *err);
    bool exists(const fs::Path &p, fs::Entry *e) const;
    QString madeKey(const fs::Path &dir, const QString &name) const;
    QString uniqueName(const fs::Path &dir, const QString &name, NodeKind kind) const;

    fs::Volume *m_v;
    QString m_label;
    ImportOptions m_o;
    QSet<QString> m_made;                // dir + name of everything made in this run
};

} // namespace cli
} // namespace dw
