/*
 * Acorn .inf sidecar files - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// Acorn .inf sidecar files: NAME LOAD EXEC [LENGTH] [ATTR|L|Locked], one line beside each file on the host. NAME may
// be "D.NAME" (a DFS directory letter). Part of the DFS module (moved from Acorn Disk Wrangler). QtCore only.

#include <QChar>
#include <QString>

namespace dw {
namespace dfs {

// The access byte as an .inf file has it (the Stardot convention): R W E L in bits 0-3
enum InfAccess { InfR = 1, InfW = 2, InfE = 4, InfL = 8 };

struct Inf {
    QString name;                    // the last part of NAME
    QChar dir;                       // the letter of "D.NAME" (null if none)
    quint32 load = 0, exec = 0;      // a 6-digit FFxxxx is taken as FFFFxxxx (I/O processor)
    quint8 access = InfR | InfW;
    bool hasAccess = false;          // the line gave an access byte or L / Locked
};

// The sidecar of a host file (<path>.inf or <path>.INF), or empty if there is none.
QString infPathFor(const QString &hostPath);
bool parseInfLine(const QString &line, Inf *out);
bool readInf(const QString &hostPath, Inf *out);       // the sidecar of hostPath

// The lines Extract to host writes: a DFS file ("D.NAME   LOAD EXEC LENGTH 08|03") and an ADFS file.
QString infLineDfs(QChar dir, const QString &name, quint32 load, quint32 exec, quint32 length, bool locked);
QString infLineAdfs(const QString &name, quint32 load, quint32 exec, quint32 length, quint8 access);

} // namespace dfs
} // namespace dw
