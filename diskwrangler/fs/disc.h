/*
 * Discs and transactions - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// A Disc is an opened image: one container and the volumes mounted on it (ADFS and the DOS Plus partition inside
// it; DFS side 0 and side 2; one CP/M volume, or one per side). QtCore only.
//
// Every change to any volume is staged in the container, so a transaction is simply: take the container's
// snapshot, run the change, and if it fails put the snapshot back and have every volume reload what it caches.
// It is the only rollback there is: the filesystem classes keep no snapshot of their own (step 6b).

#include <QString>
#include <QVector>
#include <functional>
#include <memory>

#include <diskwrangler/device/container.h>
#include <diskwrangler/fs/volume.h>

namespace dw {
namespace fs {

class Disc {
public:
    struct Mount {
        Mount() = default;
        Mount(const QString &l, std::shared_ptr<Volume> v, bool ro = false, const QString &why = QString(),
              const QString &g = QString())
            : label(l), vol(std::move(v)), readOnly(ro), readOnlyWhy(why), group(g) {}
        QString label;                   // "ADFS", "DOS", "drive 0", "side 1" ...
        std::shared_ptr<Volume> vol;
        bool readOnly = false;
        QString readOnlyWhy;
        QString group;                   // mounts with the same group show as several roots in one tree (DFS sides);
                                         // different groups as tabs (ADFS / DOS)
        // Whether the staged writes touch this mount (validateTouched). Empty: any staged write does (the mount
        // takes the whole disc). Acorn's DOS Plus partition inside ADFS gives its own region.
        std::function<bool()> touched;
    };

    // The container must outlive the Disc. (An app's image class may own it: Acorn's Adfs::Image does.)
    explicit Disc(device::Container *c = nullptr) : m_c(c) {}
    device::Container *container() const { return m_c; }
    void setContainer(device::Container *c) { m_c = c; }

    QVector<Mount> &mounts() { return m_mounts; }
    const QVector<Mount> &mounts() const { return m_mounts; }
    void addMount(const Mount &m) { m_mounts.append(m); }
    void clearMounts() { m_mounts.clear(); }
    Volume *volume(int i) const { return i >= 0 && i < m_mounts.size() ? m_mounts[i].vol.get() : nullptr; }

    // Runs f; if it fails (returns false), the staged writes are put back as they were and every volume reloads,
    // so the disc is exactly as before. err: f's error, or the reload's if that failed too.
    bool transact(const std::function<bool(QString *)> &f, QString *err);
    // Every mount's check(), with the mount's label in front of each line. Errors veto a commit. perMount (if given)
    // gets each mount's own report, in mount order, as check() gave it (an empty one for a mount with no volume).
    Report validateAll(QVector<Report> *perMount = nullptr) const;
    // The same for the mounts the staged writes touch (Mount::touched); the others are left out (an empty report in
    // perMount). A window lets only a volume its changes touch veto a save (an untouched DOS partition with errors,
    // opened read-only, doesn't stop an ADFS change being saved); the command line checks every mount (validateAll).
    Report validateTouched(QVector<Report> *perMount = nullptr) const;
    // Saves: validateAll() (as SaveOptions::validate, before anything else), then the container's checked save.
    // o.validate and o.verify are kept and run as well. Every volume reloads afterwards.
    bool commit(device::Container::SaveOptions o, QString *err);
    bool dirty() const { return m_c && m_c->hasPending(); }
    void revert();                       // drops the staged writes; every volume reloads

private:
    Report validate(bool touchedOnly, QVector<Report> *perMount) const;
    bool reloadAll(QString *err);
    device::Container *m_c = nullptr;
    QVector<Mount> m_mounts;
};

} // namespace fs
} // namespace dw
