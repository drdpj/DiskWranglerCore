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
#pragma once

// The data of a drag (or Edit > Copy) out of a disc: the program's own private types (set with setData) plus the
// files themselves, as text/uri-list and the retro-files JSON. The files are written only when a drop target first
// asks for either, so a drag between the program's own windows, or one that is cancelled, writes nothing.

#include <QJsonArray>
#include <QMimeData>
#include <QStringList>
#include <QUrl>
#include <QVector>
#include <functional>
#include <memory>
#include <diskwrangler/xfer/retrofiles.h>
#include <diskwrangler/xfer/transferitem.h>

namespace dw {

// What a drag did, for the window that started it (filled in when a target asks for the files).
struct DragState {
    bool extracted = false;
    int files = 0, dirs = 0;
    QString dir;
    QStringList errors;
};

// How a program's drags are written out.
struct DragSpec {
    std::shared_ptr<const xfer::HostPolicy> policy;
    QString filesystem;         // the document's source filesystem: "adfs", "dfs", "fat", "cpm"
    QString generator;          // the program's name, for the document
    QString imageName;          // the disc image's file name, for the document
    QString tempPrefix;         // path prefix of the temporary folders (xfer::newDragDir)
};

class DragOutMime : public QMimeData {
    Q_OBJECT
public:
    typedef std::function<bool(QVector<TransferItem> *, QString *)> Exporter;
    DragOutMime(Exporter exporter, const DragSpec &spec);
    QStringList formats() const override;
    bool hasFormat(const QString &mimeType) const override;
    std::shared_ptr<DragState> state() const { return m_state; }
    // For Edit > Copy: also offer GNOME's file list (x-special/gnome-copied-files), which its file manager pastes.
    void setForClipboard(bool on) { m_clipboard = on; }
protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)               // Qt 6 passes a QMetaType, Qt 5 a QVariant::Type
    QVariant retrieveData(const QString &mimeType, QMetaType type) const override;
#else
    QVariant retrieveData(const QString &mimeType, QVariant::Type type) const override;
#endif
private:
    void extract() const;
    Exporter m_exporter;
    DragSpec m_spec;
    mutable QList<QUrl> m_urls;
    mutable QByteArray m_json;
    std::shared_ptr<DragState> m_state;
    bool m_clipboard = false;
};

} // namespace dw
