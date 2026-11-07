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
#pragma once

// A FAT12/16 volume as a dw::fs::Volume: an adapter over fat::Volume. QtCore only.
//
// Paths are directory names from the root ("GAMES/ELITE.COM"); "." and ".." are not listed. Attributes: readonly,
// hidden, system, archive (files and directories), the date as modified. Volume attribute: label.

#include <memory>

#include <diskwrangler/fs/fat/fat.h>
#include <diskwrangler/fs/volume.h>

namespace dw {
namespace fat {

class NameRules : public fs::NameRules {
public:
    bool valid(const QString &name, fs::NodeKind kind, QString *why) const override;
    QString fromHost(const QString &hostName, fs::NodeKind kind) const override;
};

class FsVolume : public fs::Volume {
public:
    explicit FsVolume(fat::Volume *v);                       // the fat::Volume must outlive this, and be attached
    explicit FsVolume(std::shared_ptr<fat::Volume> v);
    fat::Volume *fat() const { return m_v.get(); }

    QString fsId() const override { return "fat"; }
    fs::Caps caps() const override;
    QVector<fs::AttrSpec> fileAttrs() const override;
    QVector<fs::AttrSpec> volumeAttrs() const override;
    fs::AttrValues volumeValues() const override { return {{"label", m_v->label()}}; }
    const fs::NameRules &names() const override { return m_names; }
    fs::VolumeInfo info() const override;

    bool list(const fs::Path &dir, QVector<fs::Entry> *out, QString *err) const override;
    bool read(const fs::Path &file, QByteArray *out, QString *err) const override;
    bool write(const fs::Path &dir, const QString &name, const QByteArray &data, const fs::AttrValues &attrs,
               const QDateTime &when, bool replace, QString *err) override;
    bool mkdir(const fs::Path &dir, const QString &name, QString *err) override;
    bool remove(const fs::Path &node, QString *err) override;
    bool rename(const fs::Path &from, const fs::Path &to, QString *err) override;     // within one directory
    bool setAttrs(const fs::Path &node, const fs::AttrValues &a, QString *err) override;
    bool setVolumeAttrs(const fs::AttrValues &a, QString *err) override;
    fs::Report check() const override { return m_v->check(); }
    bool reload(QString *err) override { return m_v->reload(err); }

    static quint8 attrBits(const fs::AttrValues &a, quint8 base = 0);   // readonly/hidden/system/archive -> AttrR...
    static fs::AttrValues attrValues(quint8 bits);

private:
    bool dirCluster(const fs::Path &dir, quint32 *cluster, QString *err) const;
    bool find(const fs::Path &node, quint32 *dir, DEntry *e, QString *err) const;
    std::shared_ptr<fat::Volume> m_v;
    NameRules m_names;
};

} // namespace fat
} // namespace dw
