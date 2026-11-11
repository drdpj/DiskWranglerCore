/*
 * Handing a disc to another program - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// Opening an image another program of the family owns (docs/design.md, "Opening an image another Wrangler owns"):
// find that program and start it with the image. (The messages that go with it are diskwrangler/ui/handoffdialog.h.)
//
// Where the program is looked for, in order:
//  1. the path set in this program's settings, key "otherApps/<id>": the user's answer to Locate... on the "not
//     found" message (diskwrangler/ui/handoffdialog.h; step 5g). A path that no longer works is ignored;
//  2. the folder this program runs from (both installed side by side: the .deb, a zip, two AppImages, two .app
//     bundles in one folder);
//  3. the system's record of it: Windows "App Paths" (the installers register it) and the Program Files folders,
//     the .app bundle in /Applications on macOS, the .desktop file on Linux; then the program search path.
//
// The started program is told, in the environment, which image it was handed. If it finds that image belongs back
// here, it shows its old message instead of handing the image on again, so two programs that disagree can't pass
// one image to and fro.

#include <QString>

#include <diskwrangler/fs/detection.h>

namespace dw {
namespace app {

// The program file (or on macOS the .app bundle) of app, or empty.
QString findOtherApp(const fs::OtherApp &app);
// Starts app with the image. false (and why) if it isn't there or won't start.
bool startOtherApp(const fs::OtherApp &app, const QString &imagePath, QString *why);
// True when this program was started by another one to open imagePath: it must not be handed on again.
bool handedBack(const QString &imagePath);

// Locate... (step 5g). Whether file is app: its name is the one the automatic lookup looks for (<id>, <id>.exe,
// <id>.app, <id>-*.AppImage, in any case) and it can be run (a .app bundle on macOS: the bundle or the program inside
// it). false: why, as the end of "<file> is not <program>: ...".
bool isOtherApp(const QString &file, const fs::OtherApp &app, QString *why);
// Stores file (the .app bundle for a program inside one) as where app is, settings key otherApps/<id>, if
// isOtherApp. findOtherApp then finds it first.
bool setOtherAppPath(const fs::OtherApp &app, const QString &file, QString *why);

const char *const HandoffVariable = "DISKWRANGLER_HANDOFF";

} // namespace app
} // namespace dw
