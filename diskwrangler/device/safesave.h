/*
 * Checked saving - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// SafeSave: write a new copy of a file beside it, let the caller check that copy, then atomically put it in the
// original's place, optionally keeping the original as <name>.bak. QtCore only.
//
//     SafeSave s(path);
//     s.begin(expectedSize, &err);        // free space; creates <path>.dwsave-XXXXXX
//     s.device()->write(...);              // as much as needed
//     s.finishWriting(&err);               // flushed to the disc itself, not just the system's cache
//     ... open s.tempPath() and check it ...
//     s.replace(backup, &err);             // .bak (hard link where possible), then the atomic rename
//
// Anything short of a successful replace() leaves the original untouched and removes the temporary file (abandon(),
// or the destructor). The new file gets the original's permissions.
#include <QFile>
#include <QString>
#include <memory>

namespace dw {
namespace device {

class SafeSave {
public:
    explicit SafeSave(const QString &path);
    ~SafeSave();
    SafeSave(const SafeSave &) = delete;
    SafeSave &operator=(const SafeSave &) = delete;

    bool begin(qint64 expectedSize, QString *err);
    QIODevice *device() { return &m_tmp; }
    bool finishWriting(QString *err);
    QString tempPath() const { return m_tmp.fileName(); }
    bool replace(bool backup, QString *err);
    void abandon();

private:
    QString m_path;
    QFile m_tmp;
    bool m_done = false;
};

} // namespace device
} // namespace dw
