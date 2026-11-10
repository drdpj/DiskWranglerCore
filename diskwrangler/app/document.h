/*
 * The disc a window shows - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// A Document is the image a window shows (docs/design.md, "UI layer"): an opened file and the disc on it (a container
// and its mounted volumes, fs::Disc), with the checked save, Save As and the reasons a mount can't be changed. Each
// program implements it on its own opened-disc class (CP/M Disk Wrangler: Cpm::OpenedDisc), which its console
// program uses too, so the window and the command line save through the same checks. QtCore only.
//
// It is the window's twin of dw::cli::OpenedImage: the two are separate interfaces implemented by (or over) the same
// class, because each asks for what its own front end needs.

#include <QString>

#include <diskwrangler/device/container.h>
#include <diskwrangler/fs/disc.h>

namespace dw {
namespace app {

class Document {
public:
    virtual ~Document() = default;

    virtual fs::Disc &disc() = 0;
    virtual const fs::Disc &disc() const = 0;
    // An image file is open. It may have no mounts: an image that wasn't read (the window says why).
    virtual bool isOpen() const = 0;
    virtual QString path() const = 0;            // empty: not saved anywhere yet
    // Something staged that a save would write.
    virtual bool dirty() const { return disc().dirty(); }

    // The checked save. false: err says why, as the end of a sentence that starts "Not saved: " (the window adds
    // that). The document checks what its program checks before and after writing (the volumes, the file not changed
    // by something else since it was opened, the copy as written read back), keeps a .bak if backup, and every
    // volume reads the disc again afterwards.
    virtual bool save(bool backup, QString *err) = 0;
    // Writes the disc to another file, as kind, and goes on working on that file. Nothing is read again: the window
    // asks the program to do that afterwards (DiscWindow::afterSaveAs). err as for save.
    virtual bool saveAs(const QString &path, device::Kind kind, QString *err) = 0;
    // Drops the staged changes; every volume reads the disc again.
    virtual void revert() { disc().revert(); }

    // Why mount can't be changed, in words for "read-only: ..."; empty when it can.
    virtual QString readOnlyWhy(int mount) const = 0;
    // A few words for the mount (a tab, a root row): by default its label.
    virtual QString mountTitle(int mount) const { return disc().mounts().value(mount).label; }
};

} // namespace app
} // namespace dw
