/*
 * New-disc presets - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// The kinds of blank disc a program can make (File > New disc image, `new --format` / `new --list`). QtCore only.
//
// A Preset describes one kind of new disc: what it is called, which filesystem it holds, its size and shape, the
// files it can be written in, whether its two sides can be stored one after the other, and its title field. It says
// nothing about how the blank disc is made: that stays with whoever makes it (the program, from the preset's id).
//
// The registry is the program's one list. Each core module adds the presets it knows (dw::dfs::addPresets: DFS and
// Watford DFS; dw::fat::addPresets: Acorn DOS Plus 800K and the PC floppies) and the program adds its own (Acorn
// Disk Wrangler: ADFS and the DOS Plus 640K hybrid; CP/M Disk Wrangler: its format library). The New-disc dialogs and
// `new --list` read it; each program picks and orders what it offers. Adding a preset whose id is already there
// replaces it in its place, so adding a module's presets twice changes nothing.

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

namespace dw {
namespace fs {

struct Preset {
    QString id;                 // `new --format` id, e.g. "dfs-ss40", "pc-720", "ibm-3740"
    QString name;               // shown in the New-disc dialog and `new --list`
    QString fsId;               // the filesystem on the new disc, as Volume::fsId() names it ("dfs", "fat", "adfs", "cpm", "torch")
    QString source;             // who added it: the module ("dfs", "fat") or the program (its own word)

    // Size and shape: physical cylinders, sides, sectors per track and bytes per sector (0 where the program
    // doesn't say), and the disc's size in bytes, every side.
    int cylinders = 0, heads = 0, sectors = 0, sectorSize = 0;
    quint64 bytes = 0;

    QStringList containers;     // the kinds of file a new disc can be held in ("raw", "imd", "dsk", "edsk"), the program's order
    QString suffix;             // the usual file suffix of a raw image, no dot ("ssd", "img"); may be empty
    bool doubleSided = false;   // two sides
    bool sideOrder = false;     // its raw file may hold the sides interleaved or one after the other: a side order to choose

    QString titleWhat;          // what the disc's title is called ("Disc title", "Volume label"); empty = no title
    int titleMax = 0;           // its longest

    QString notes;              // a description; may be empty
    QVariantMap extra;          // anything only the program that added it reads
};

class PresetRegistry {
public:
    // Adds p at the end, or in place of the preset with the same id (ids compared ignoring case).
    void add(const Preset &p);
    // Takes out every preset a source added (a program rebuilding its own list).
    void removeSource(const QString &source);
    const QVector<Preset> &all() const { return m_presets; }
    QVector<Preset> fromSource(const QString &source) const;
    const Preset *find(const QString &id) const;            // ids compared ignoring case; null = none
    void clear() { m_presets.clear(); }

private:
    QVector<Preset> m_presets;
};

// The program's registry (one per process).
PresetRegistry &presets();

} // namespace fs
} // namespace dw
