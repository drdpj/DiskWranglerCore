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
#include <functional>

#include <diskwrangler/fs/detection.h>

class QMessageBox;
class QPushButton;
class QWidget;

namespace dw {
namespace ui {

// The disc belongs to c.owner. Starts that program with the image and says so ("This is a CP/M disc, so it has been
// opened in CP/M Disk Wrangler.", with "Don't show this again"). If the program isn't found or won't start (or the
// image was handed here by it), shows the program's own message instead: title, text (which should name the
// program it belongs to) and a link to where to get it - unless title is empty, when the caller shows its own
// (with releasesLink and addLocateButton; the message that it was started is then titled "Opened in <program>").
// Returns true if the other program was started (after Locate... too).
// interactive false (the tests): no message boxes; the other program is started only if allowStart.
bool handOff(QWidget *parent, const fs::Candidate &c, const QString &imagePath, const QString &title, const QString &text,
             bool interactive = true, bool allowStart = true);
// "This is a CP/M disc, so it has been opened in CP/M Disk Wrangler." with "Don't show this again" (handoff/quiet/<id>),
// titled title or, if empty, "Opened in <program>"; nothing if the user said not to show it again.
void showStarted(QWidget *parent, const fs::Candidate &c, const QString &imagePath, const QString &title = QString());
// "Get CP/M Disk Wrangler", linked to its releases page (rich text, for a message box's informative text)
QString releasesLink(const fs::OtherApp &app);

// ----- Locate... (deliberate change 7, step 5g): the "not found" message lets the user say where the program is.
// Shown only when the automatic lookup (app::findOtherApp: the setting, beside this program, the system's records)
// finds nothing; the answer is stored as otherApps/<id> for whichever program the disc belongs to.
//
// The file chooser: the program file the user picks for app, or empty if cancelled.
using LocateChooser = std::function<QString(QWidget *parent, const fs::OtherApp &app)>;
QString chooseOtherApp(QWidget *parent, const fs::OtherApp &app);
// Adds "Locate..." to box if app can't be found automatically; returns the button, or null when it is found.
QPushButton *addLocateButton(QMessageBox *box, const fs::OtherApp &app);
// After the box was shown: if locate was clicked, asks for the program (choose, default chooseOtherApp), checks it is
// c.owner (app::setOtherAppPath; if not, says why in a box titled "Locate <program>") and stores it, then starts it
// with the image and says so (showStarted, with startedTitle). Returns true if it was started.
bool locateAndStart(QWidget *parent, QMessageBox *box, QPushButton *locate, const fs::Candidate &c, const QString &imagePath,
                    const LocateChooser &choose = LocateChooser(), const QString &startedTitle = QString());

// Two readings are equally likely (Verdict::Ask). Asks which one it is: returns 0 for a, 1 for b, -1 if cancelled.
// A foreign candidate's button says it opens the image in its own program.
int askWhich(QWidget *parent, const QString &imagePath, const fs::Candidate &a, const fs::Candidate &b);

} // namespace ui
} // namespace dw
