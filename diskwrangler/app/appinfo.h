/*
 * Who a program is - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// Who a program of the family is: what its windows' titles, its About box and its console program's --version say,
// where its settings live, and the other programs it hands discs to. QtCore only: the console programs use it too
// (dw::cli::AppInfo holds one). Nothing here assumes there are only two Wranglers: each other program is found by its
// own id (settings key otherApps/<id>, diskwrangler/app/handoff.h).

#include <QString>
#include <QVector>

#include <diskwrangler/fs/detection.h>

namespace dw {
namespace app {

struct AppInfo {
    QString name;                        // "CP/M Disk Wrangler": window titles, Help > About, the retro-files generator
    QString id;                          // "cpmdiskwrangler": the program file, and its QSettings organisation and name
    QString version;                     // as Help > About shows it ("unknown" in a build without one)
    QString organisation;                // QSettings organisation (both programs use their id)
    QString icon;                        // the window icon, a resource path; empty = none set here
    QString releases;                    // where to get this program
    QVector<fs::OtherApp> handsTo;       // the programs this one hands discs to (Candidate::owner names one of them)

    // The program of the family with that id that this one hands discs to; a null OtherApp if there is none.
    fs::OtherApp other(const QString &otherId) const {
        for (const fs::OtherApp &a : handsTo) if (a.id == otherId) return a;
        return fs::OtherApp();
    }
};

} // namespace app
} // namespace dw
