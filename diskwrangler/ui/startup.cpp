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
#include <diskwrangler/ui/startup.h>

#include <QGuiApplication>
#include <QLoggingCategory>

#include <diskwrangler/app/appsettings.h>
#include <diskwrangler/xfer/retrofiles.h>

namespace dw {
namespace ui {

void beforeApplication(const QString &programId) {
#if defined(Q_OS_LINUX)
    // On a GNOME Wayland session Qt starts under XWayland (xcb) unless it is told otherwise, and says so on stderr:
    // "Warning: Ignoring WAYLAND_DISPLAY on Gnome. Use QT_QPA_PLATFORM=wayland to run on Wayland anyway." Make that
    // choice ourselves, so there is nothing to warn about. Wayland stays the fallback, and QT_QPA_PLATFORM set by the
    // user wins.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") && qgetenv("XDG_SESSION_TYPE") == "wayland" &&
        qgetenv("XDG_CURRENT_DESKTOP").toUpper().contains("GNOME"))
        qputenv("QT_QPA_PLATFORM", "xcb;wayland");
#endif
    // Qt's Wayland text-input-v3 plugin warns "Try to disable surface ... with focusing surface" whenever focus moves
    // between a window and a dialog. It is a harmless Qt-internal bookkeeping complaint, so hide that one category's
    // warnings.
    QLoggingCategory::setFilterRules("qt.qpa.wayland.textinput.warning=false");
    QGuiApplication::setDesktopFileName(programId);    // before QApplication: registers with the portal first
}

void setUpApplication(const QString &programId, const QString &version, const QIcon &icon) {
    QApplication::setOrganizationName(programId);
    QApplication::setApplicationName(programId);
    AppSettings::init();                             // portable mode if <program>.ini is beside the program
    QApplication::setApplicationVersion(version);
    // Ties the windows to <id>.desktop, which is where Linux docks and task bars (GNOME, KDE) take the icon and name
    // from; the window icon below is not enough there. (X11 matches StartupWMClass, Wayland this.)
    QGuiApplication::setDesktopFileName(programId);
    QApplication::setWindowIcon(icon);               // inherited by every window and dialog
    xfer::cleanStale(xfer::dragDirPrefix(programId));   // drag folders left behind by a crash
}

} // namespace ui
} // namespace dw
