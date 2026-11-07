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
#pragma once

// One side of a DFS disc as a dw::fs::Volume: an adapter over dfs::Disc. QtCore only.
//
// The list is flat. A file's path is its full name, "$.BOOT" or "A.PROG": the directory letter is part of the name
// (a name written without one goes in $). Attributes: locked, load, exec (18-bit). Volume attributes: title, boot.

#include <memory>

#include <diskwrangler/fs/dfs/dfs.h>
#include <diskwrangler/fs/volume.h>

namespace dw {
namespace dfs {

class NameRules : public fs::NameRules {
public:
    bool valid(const QString &name, fs::NodeKind kind, QString *why) const override;
    QString fromHost(const QString &hostName, fs::NodeKind kind) const override;    // keeps a "D." in front
};

// "A.PROG" -> ('A', "PROG"); "PROG" -> ('$', "PROG")
void splitName(const QString &full, QChar *dir, QString *name);

class Volume : public fs::Volume {
public:
    explicit Volume(Disc *disc);                        // the Disc must outlive the volume, and be attached
    explicit Volume(std::shared_ptr<Disc> disc);
    Disc *disc() const { return m_d.get(); }

    QString fsId() const override { return "dfs"; }
    fs::Caps caps() const override;
    QVector<fs::AttrSpec> fileAttrs() const override;
    QVector<fs::AttrSpec> volumeAttrs() const override;
    fs::AttrValues volumeValues() const override;
    const fs::NameRules &names() const override { return m_names; }
    fs::VolumeInfo info() const override;

    bool list(const fs::Path &dir, QVector<fs::Entry> *out, QString *err) const override;
    bool read(const fs::Path &file, QByteArray *out, QString *err) const override;
    bool write(const fs::Path &dir, const QString &name, const QByteArray &data, const fs::AttrValues &attrs,
               const QDateTime &when, bool replace, QString *err) override;
    bool remove(const fs::Path &node, QString *err) override;
    bool rename(const fs::Path &from, const fs::Path &to, QString *err) override;
    bool setAttrs(const fs::Path &node, const fs::AttrValues &a, QString *err) override;
    bool setVolumeAttrs(const fs::AttrValues &a, QString *err) override;
    fs::Report check() const override { return m_d->check(); }
    bool reload(QString *err) override { return m_d->reload(err); }

    // retro-files "acorn": load and exec as 8 hex digits (FFFFxxxx for the I/O processor), access "L" or "WR"
    QMap<QString, QJsonObject> toRetro(const fs::Entry &e) const override;
    fs::AttrValues fromRetro(const QMap<QString, QJsonObject> &families, fs::NodeKind kind) const override;

private:
    bool find(const fs::Path &p, int *idx, QString *err) const;
    std::shared_ptr<Disc> m_d;
    NameRules m_names;
};

} // namespace dfs
} // namespace dw
