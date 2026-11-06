/*
 * Host file names - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

#include <QString>
#include <QStringList>

namespace dw {

// A disc file's name made safe to create on any host (Windows being the strictest): characters no host allows
// become '_', names Windows reserves for devices (CON, AUX, COM1... with or without an extension - an ADFS file
// can be called "Aux") get a '_' added, an empty name or "." / ".." gets a leading '_', and a trailing '.' or ' ',
// which Windows drops silently, becomes '_'. Header only.
inline QString hostSafeName(const QString &n) {
    QString s = n;
    for (QChar &c : s)
        if (QString("/\\:*?\"<>|").contains(c) || c.unicode() < 0x20) c = '_';
    static const QStringList dev = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
                                    "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    if (dev.contains(s.section('.', 0, 0), Qt::CaseInsensitive)) s.insert(s.indexOf('.') < 0 ? s.size() : s.indexOf('.'), '_');
    if (s.isEmpty() || s == "." || s == "..") s = "_" + s;
    while (s.endsWith('.') || s.endsWith(' ')) s[s.size() - 1] = '_';
    return s;
}

} // namespace dw
