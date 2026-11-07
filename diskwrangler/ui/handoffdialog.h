/*
 * Detection questions and hand-off messages - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// What the user sees when detection (diskwrangler/fs/detection.h) hands a disc to another program or can't decide.

#include <QString>

#include <diskwrangler/fs/detection.h>

class QWidget;

namespace dw {
namespace ui {

// The disc belongs to c.owner. Starts that program with the image and says so ("This is a CP/M disc, so it has been
// opened in CP/M Disk Wrangler.", with "Don't show this again"). If the program isn't found or won't start (or the
// image was handed here by it), shows the program's own message instead: title, text (which should name the
// program it belongs to) and a link to where to get it - unless title is empty, when the caller shows its own
// (with releasesLink; the message that it was started is then titled "Opened in <program>"). Returns true if the
// other program was started.
// interactive false (the tests): no message boxes; the other program is started only if allowStart.
bool handOff(QWidget *parent, const fs::Candidate &c, const QString &imagePath, const QString &title, const QString &text,
             bool interactive = true, bool allowStart = true);
// "Get CP/M Disk Wrangler", linked to its releases page (rich text, for a message box's informative text)
QString releasesLink(const fs::OtherApp &app);

// Two readings are equally likely (Verdict::Ask). Asks which one it is: returns 0 for a, 1 for b, -1 if cancelled.
// A foreign candidate's button says it opens the image in its own program.
int askWhich(QWidget *parent, const QString &imagePath, const fs::Candidate &a, const fs::Candidate &b);

} // namespace ui
} // namespace dw
