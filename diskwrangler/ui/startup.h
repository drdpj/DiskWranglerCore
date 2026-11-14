/*
 * A program's start-up - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// A program's start-up in main() (step 6c), as every Disk Wrangler does it:
//
//     int main(int argc, char *argv[]) {
//         dw::ui::beforeApplication("cpmdiskwrangler");
//         QApplication app(argc, argv);
//         dw::ui::setUpApplication("cpmdiskwrangler", APP_VERSION, appIcon());
//         ... anything of the program's own (CP/M Disk Wrangler: the extra diskdefs files)
//         return dw::ui::runWindows<MainWindow>(app, argc, argv);
//     }
//
// The program id names the settings (organisation and application), the desktop file and the drag folders.

#include <QApplication>
#include <QIcon>
#include <QString>

#include <diskwrangler/app/fileopen.h>
#include <diskwrangler/ui/discwindow.h>

namespace dw {
namespace ui {

// Before the QApplication is made: on a GNOME Wayland session, Qt's platform chosen as "xcb;wayland" (unless the user
// set QT_QPA_PLATFORM), so Qt has nothing to warn about; the Wayland text-input plugin's harmless focus warnings
// hidden; the desktop file name set (so the window registers with the portal first).
void beforeApplication(const QString &programId);

// After it: the settings' organisation and application name (the id), AppSettings::init (portable mode when
// <program>.ini is beside the program), the version, the desktop file name again (what ties the windows to
// <id>.desktop, where Linux docks take the icon and name from), the window icon every window and dialog inherits, and
// the drag folders a crash left behind removed (older than a day).
void setUpApplication(const QString &programId, const QString &version, const QIcon &icon);

// Then the windows: files opened from the macOS Finder (FileOpen::FileOpenFilter<W>), the first window, each image
// named on the command line opened in its own window (the first in that one), and the event loop.
template <class W> int runWindows(QApplication &app, int argc, char *argv[]) {
    FileOpen::FileOpenFilter<W> fileOpen;
    app.installEventFilter(&fileOpen);
    W w;
    w.show();
    for (int i = 1; i < argc; ++i) {
        const QString p = QString::fromLocal8Bit(argv[i]);
        if (i == 1) w.openFile(p);
        else DiscWindow::makeWindow<W>()->openFile(p);    // further images each get their own window
    }
    return app.exec();
}

} // namespace ui
} // namespace dw
