/*
 * Viewer dialog - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// The read-only window that shows a file: a view menu with every viewer in the registry (viewers.h), the check
// boxes the selected view asks for, "Wrap lines", and Copy all / Save as text buttons. Text is selectable (Ctrl+C).
// It opens on the viewer that suits the bytes best. A program derives from it to keep its own constructor and
// names for the views.

#include <QByteArray>
#include <QDialog>
#include <QMap>
#include <QVariantMap>
#include <diskwrangler/text/viewers.h>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;

namespace dw {

class ViewerDialog : public QDialog {
    Q_OBJECT
public:
    // registry must outlive the dialog. ctx is handed to the viewers (accepts() and render()).
    ViewerDialog(const ViewerRegistry &registry, const QString &title, const QByteArray &data, QWidget *parent = nullptr,
                 const QVariantMap &ctx = QVariantMap());
    int viewIndex() const;                  // position of the selected view in the menu
    QString viewId() const;                 // its id: "plain", "hex", ...
    QString shownText() const;

private slots:
    void render();
    void copyAll();
    void saveText();

private:
    struct Opt { QString viewer, id; QCheckBox *box; };
    const ViewerRegistry &m_reg;
    QString m_title;
    QByteArray m_data;
    QVariantMap m_ctx;
    QComboBox *m_mode;
    QCheckBox *m_wrap;
    QVector<Opt> m_opts;
    QLabel *m_info;
    QPlainTextEdit *m_edit;
};

} // namespace dw
