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
#include "viewerdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace dw {

static const int kMaxShown = 1024 * 1024;     // keep the window responsive for very large files

ViewerDialog::ViewerDialog(const ViewerRegistry &registry, const QString &title, const QByteArray &data, QWidget *parent,
                           const QVariantMap &ctx)
    : QDialog(parent), m_reg(registry), m_title(title), m_data(data), m_ctx(ctx) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("View - " + title);
    resize(860, 620);

    m_mode = new QComboBox;
    for (const auto &v : m_reg.viewers()) m_mode->addItem(v->name());
    m_wrap = new QCheckBox("Wrap lines");
    auto *copy = new QPushButton("Copy all");
    auto *save = new QPushButton("Save as text...");
    m_info = new QLabel;
    m_edit = new QPlainTextEdit;
    m_edit->setReadOnly(true);
    m_edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_edit->setLineWrapMode(QPlainTextEdit::NoWrap);

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel("View as:"));
    top->addWidget(m_mode);
    for (const auto &v : m_reg.viewers())               // each view's own check boxes, enabled while it is selected
        for (const ViewOption &o : v->options(data)) {
            auto *box = new QCheckBox(o.label);
            if (!o.tip.isEmpty()) box->setToolTip(o.tip);
            box->setChecked(o.checked);
            top->addWidget(box);
            m_opts.append({v->id(), o.id, box});
        }
    top->addWidget(m_wrap);
    top->addStretch();
    top->addWidget(copy);
    top->addWidget(save);
    auto *lay = new QVBoxLayout(this);
    lay->addLayout(top);
    lay->addWidget(m_edit, 1);
    lay->addWidget(m_info);

    // pick the most useful view first
    if (auto best = m_reg.best(data, m_ctx))
        for (int i = 0; i < m_reg.viewers().size(); ++i)
            if (m_reg.viewers()[i] == best) m_mode->setCurrentIndex(i);
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ViewerDialog::render);
    for (const Opt &o : m_opts) connect(o.box, &QCheckBox::toggled, this, &ViewerDialog::render);
    connect(m_wrap, &QCheckBox::toggled, this, [this](bool on) {
        m_edit->setLineWrapMode(on ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap);
    });
    connect(copy, &QPushButton::clicked, this, &ViewerDialog::copyAll);
    connect(save, &QPushButton::clicked, this, &ViewerDialog::saveText);
    render();
}

int ViewerDialog::viewIndex() const { return m_mode->currentIndex(); }

QString ViewerDialog::viewId() const {
    int i = viewIndex();
    return i >= 0 && i < m_reg.viewers().size() ? m_reg.viewers()[i]->id() : QString();
}

QString ViewerDialog::shownText() const { return m_edit->toPlainText(); }

void ViewerDialog::render() {
    const int i = viewIndex();
    if (i < 0 || i >= m_reg.viewers().size()) return;
    const std::shared_ptr<FileViewer> v = m_reg.viewers()[i];
    QVariantMap opts = m_ctx;
    for (const Opt &o : m_opts) {
        o.box->setEnabled(o.viewer == v->id());
        if (o.viewer == v->id()) opts[o.id] = o.box->isChecked();
    }
    const bool truncated = m_data.size() > kMaxShown && !v->wantsAllData();
    ViewResult r = v->render(truncated ? m_data.left(kMaxShown) : m_data, opts);
    QString note = r.note;
    if (truncated) note += (note.isEmpty() ? "" : " - ") + QString("showing the first 1 MiB only");
    m_edit->setPlainText(r.text);
    m_info->setText(QString("%1 bytes%2").arg(m_data.size()).arg(note.isEmpty() ? QString() : "  -  " + note));
}

void ViewerDialog::copyAll() {
    QApplication::clipboard()->setText(m_edit->toPlainText());
}

void ViewerDialog::saveText() {
    const int i = viewIndex();
    const QString suffix = i >= 0 && i < m_reg.viewers().size() ? m_reg.viewers()[i]->saveSuffix() : QString(".txt");
    QString p = QFileDialog::getSaveFileName(this, "Save as text", m_title + suffix);
    if (p.isEmpty()) return;
    QFile f(p);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(m_edit->toPlainText().toUtf8()) < 0)
        QMessageBox::warning(this, "Save", f.errorString());
}

} // namespace dw
