/*
 * Drag-out data - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include "dragoutmime.h"

#include <QCursor>
#include <QGuiApplication>
#include <QJsonObject>
#include <QMetaType>

namespace dw {

static const char *const UriList = "text/uri-list";
static const char *const GnomeCopied = "x-special/gnome-copied-files";    // GNOME Files pastes this

DragOutMime::DragOutMime(Exporter exporter, const DragSpec &spec) : m_exporter(exporter), m_spec(spec), m_state(new DragState) {}

QStringList DragOutMime::formats() const {
    QStringList f = QMimeData::formats();
    for (const char *m : {UriList, xfer::RetroFilesMime})
        if (!f.contains(m)) f << m;
    if (m_clipboard && !f.contains(GnomeCopied)) f << GnomeCopied;
    return f;
}

bool DragOutMime::hasFormat(const QString &mimeType) const { return formats().contains(mimeType); }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
QVariant DragOutMime::retrieveData(const QString &mimeType, QMetaType type) const {
    const int typeId = type.id();
#else
QVariant DragOutMime::retrieveData(const QString &mimeType, QVariant::Type type) const {
    const int typeId = int(type);                  // QVariant::Type values are QMetaType ids
#endif
    if (mimeType == UriList) {
        extract();
        if (typeId == QMetaType::QByteArray || typeId == QMetaType::QString) {
            QByteArray b;
            for (const QUrl &u : m_urls) b += u.toEncoded() + "\r\n";
            return typeId == QMetaType::QString ? QVariant(QString::fromUtf8(b)) : QVariant(b);
        }
        QVariantList l;
        for (const QUrl &u : m_urls) l << u;
        return l;
    }
    if (mimeType == xfer::RetroFilesMime) {
        extract();
        return m_json;
    }
    if (m_clipboard && mimeType == GnomeCopied) {     // "copy", then a URL per line
        extract();
        QByteArray b = "copy";
        for (const QUrl &u : m_urls) b += "\n" + u.toEncoded();
        return b;
    }
    return QMimeData::retrieveData(mimeType, type);
}

void DragOutMime::extract() const {
    if (m_state->extracted) return;
    m_state->extracted = true;
    QGuiApplication::setOverrideCursor(Qt::WaitCursor);
    QVector<TransferItem> items;
    QString err;
    QString dir = m_exporter(&items, &err) ? xfer::newDragDir(m_spec.tempPrefix, &err) : QString();
    if (dir.isEmpty()) {
        m_state->errors << err;
    } else {
        QStringList top;
        QJsonArray j;
        xfer::writeHost(items, dir, *m_spec.policy, &top, &j, &m_state->errors);
        for (const QString &t : top) m_urls << QUrl::fromLocalFile(dir + "/" + t);
        m_json = xfer::makeJson(j, dir, m_spec.imageName, m_spec.filesystem, m_spec.generator);
        m_state->dir = dir;
        for (const QJsonValue &v : j) (v.toObject()["type"].toString() == "directory" ? m_state->dirs : m_state->files)++;
    }
    QGuiApplication::restoreOverrideCursor();
}

} // namespace dw
