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
#include "safesave.h"

#include <QDir>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QStorageInfo>

#ifdef Q_OS_WIN
#include <io.h>
#include <windows.h>
#else
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace dw {
namespace device {

static QString mb(qint64 n) { return QString::number(double(n) / (1024.0 * 1024.0), 'f', 1) + " MB"; }

SafeSave::SafeSave(const QString &path) : m_path(QFileInfo(path).absoluteFilePath()) {}

SafeSave::~SafeSave() { abandon(); }

void SafeSave::abandon() {
    if (m_done) return;
    m_done = true;
    if (m_tmp.isOpen()) m_tmp.close();
    if (!m_tmp.fileName().isEmpty()) QFile::remove(m_tmp.fileName());
}

bool SafeSave::begin(qint64 expectedSize, QString *err) {
    const QString dir = QFileInfo(m_path).absolutePath();
    if (!QFileInfo(dir).isDir()) { *err = "the folder " + QDir::toNativeSeparators(dir) + " does not exist"; return false; }
    QStorageInfo st(dir);
    if (st.isValid() && st.isReady() && st.bytesAvailable() >= 0 && st.bytesAvailable() < expectedSize + 65536) {
        *err = QString("there isn't enough free space beside the image to write the new copy (%1 needed, %2 free). "
                       "Nothing was changed.").arg(mb(expectedSize), mb(st.bytesAvailable()));
        return false;
    }
    for (int tries = 0; tries < 20; ++tries) {
        m_tmp.setFileName(m_path + QString(".dwsave-%1").arg(QRandomGenerator::global()->generate() & 0xFFFFFF, 6, 16, QChar('0')));
        if (m_tmp.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return true;
        if (!m_tmp.exists()) break;            // not a name clash: the folder can't be written
    }
    *err = "cannot create a new file beside the image: " + m_tmp.errorString();
    m_tmp.setFileName(QString());
    return false;
}

bool SafeSave::finishWriting(QString *err) {
    if (!m_tmp.flush()) { *err = m_tmp.errorString(); return false; }
#ifdef Q_OS_WIN
    if (_commit(m_tmp.handle()) != 0) { *err = "could not flush the new file to the disc"; return false; }
#else
    if (::fsync(m_tmp.handle()) != 0) { *err = QString("could not flush the new file to the disc: %1").arg(strerror(errno)); return false; }
#endif
    m_tmp.close();
    if (m_tmp.error() != QFileDevice::NoError) { *err = m_tmp.errorString(); return false; }
    if (QFileInfo::exists(m_path)) {                           // the new file gets the original's permissions (and owner)
        QFile::setPermissions(m_tmp.fileName(), QFile::permissions(m_path));
#ifndef Q_OS_WIN
        struct stat sb;
        if (::stat(QFile::encodeName(m_path).constData(), &sb) == 0)
            if (::chown(QFile::encodeName(m_tmp.fileName()).constData(), sb.st_uid, sb.st_gid) != 0) { /* only root can */ }
#endif
    }
    return true;
}

bool SafeSave::replace(bool backup, QString *err) {
    const QString tmp = m_tmp.fileName();
    if (backup && QFileInfo::exists(m_path)) {
        const QString bak = m_path + ".bak";
        QFile::remove(bak);
        bool linked = false;
#ifndef Q_OS_WIN
        linked = ::link(QFile::encodeName(m_path).constData(), QFile::encodeName(bak).constData()) == 0;
#endif
        if (!linked && !QFile::copy(m_path, bak)) {
            *err = "cannot write the backup " + QDir::toNativeSeparators(bak) + ". Nothing was changed.";
            abandon();
            return false;
        }
    }
#ifdef Q_OS_WIN
    const std::wstring from = QDir::toNativeSeparators(tmp).toStdWString(), to = QDir::toNativeSeparators(m_path).toStdWString();
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD e = GetLastError();
        if (e == ERROR_SHARING_VIOLATION || e == ERROR_ACCESS_DENIED || e == ERROR_LOCK_VIOLATION || e == ERROR_USER_MAPPED_FILE)
            *err = "the image is in use by another program (an emulator?), so it can't be replaced. Nothing was changed: "
                   "close the other program and save again.";
        else
            *err = QString("could not replace the image (Windows error %1). Nothing was changed.").arg(e);
        abandon();
        return false;
    }
#else
    if (::rename(QFile::encodeName(tmp).constData(), QFile::encodeName(m_path).constData()) != 0) {
        *err = QString("could not replace the image: %1. Nothing was changed.").arg(strerror(errno));
        abandon();
        return false;
    }
    const int dfd = ::open(QFile::encodeName(QFileInfo(m_path).absolutePath()).constData(), O_RDONLY);
    if (dfd >= 0) { ::fsync(dfd); ::close(dfd); }             // the rename itself reaches the disc
#endif
    m_done = true;
    return true;
}

} // namespace device
} // namespace dw
