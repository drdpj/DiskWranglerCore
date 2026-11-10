/*
 * The disc window - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include <diskwrangler/ui/discwindow.h>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QMenu>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>

#include <diskwrangler/app/appsettings.h>

namespace dw {
namespace ui {

bool DiscWindow::autoAnswer = false;

QList<DiscWindow *> &DiscWindow::all() { static QList<DiscWindow *> w; return w; }

DiscWindow *DiscWindow::byId(quint64 id) {
    for (DiscWindow *w : all()) if (w->id() == id) return w;
    return nullptr;
}

DiscWindow::DiscWindow(QWidget *parent) : QMainWindow(parent) { all().append(this); }

DiscWindow::~DiscWindow() {
    all().removeAll(this);
    for (VolumePane *p : m_panes) p->disconnect(this);    // the panes outlive this part of the window: no updateUi from them now
    QApplication::clipboard()->disconnect(this);
}

// ------------------------------------------------------------------------------------------- setting up

void DiscWindow::setupKit() {
    m_groups = paneGroups();
    if (m_groups.isEmpty()) m_groups << QString();
    for (const QString &g : m_groups) {
        VolumePane *p = createPane(g);
        p->setHost(this);
        p->setPaneLayout(paneLayout(g));
        m_panes << p;
        connect(p, &QTreeWidget::itemSelectionChanged, this, &DiscWindow::updateUi);
        connect(p, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
            const PaneRow r = VolumePane::rowOf(it);
            if (r.isFile()) activateFile(r);
        });
        connect(p, &QWidget::customContextMenuRequested, this, &DiscWindow::showContextMenu);
    }
    if (m_panes.size() == 1) {
        setCentralWidget(m_panes.first());
    } else {
        m_tabs = new QTabWidget;
        for (int i = 0; i < m_panes.size(); ++i) m_tabs->addTab(m_panes[i], paneTitle(m_groups[i]));
        connect(m_tabs, &QTabWidget::currentChanged, this, &DiscWindow::updateUi);
        setCentralWidget(m_tabs);
    }

    // The standard actions, in this order (the program puts them in its menus in its own).
    struct Slot { Act a; void (DiscWindow::*f)(); };
    const QVector<Slot> table = {
        {Act::NewWindow, &DiscWindow::newWindow}, {Act::NewImage, &DiscWindow::newImageDialog},
        {Act::Open, &DiscWindow::openDialog}, {Act::SaveAs, &DiscWindow::saveAsDialog}, {Act::Revert, &DiscWindow::revert},
        {Act::View, &DiscWindow::viewSelected}, {Act::Extract, &DiscWindow::extractSelected},
        {Act::AddFiles, &DiscWindow::addFilesDialog}, {Act::AddFolder, &DiscWindow::addFolderDialog},
        {Act::Copy, &DiscWindow::copySelected}, {Act::Paste, &DiscWindow::paste}, {Act::Rename, &DiscWindow::renameSelected},
        {Act::Properties, &DiscWindow::editProperties}, {Act::Delete, &DiscWindow::deleteSelected},
        {Act::NewDirectory, &DiscWindow::newDirectory},
    };
    auto make = [this](Act a) -> QAction * {
        const ActionSpec s = actionSpec(a);
        if (s.text.isEmpty()) return nullptr;
        auto *x = new QAction(s.text, this);
        if (!s.keys.isEmpty()) x->setShortcuts(s.keys);
        if (!s.iconText.isEmpty()) x->setIconText(s.iconText);
        if (!s.toolTip.isEmpty()) x->setToolTip(s.toolTip);
        m_actions.insert(int(a), x);
        return x;
    };
    for (const Slot &s : table)
        if (QAction *x = make(s.a)) connect(x, &QAction::triggered, this, [this, s] { (this->*s.f)(); });
    if (QAction *x = make(Act::Save)) connect(x, &QAction::triggered, this, [this] { save(); });
    if (QAction *x = make(Act::Close)) connect(x, &QAction::triggered, this, &QWidget::close);
    if (QAction *x = make(Act::Quit)) connect(x, &QAction::triggered, qApp, &QApplication::closeAllWindows);
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &DiscWindow::updateUi);
}

VolumePane *DiscWindow::createPane(const QString &) { return new VolumePane; }

VolumePane *DiscWindow::pane(const QString &group) const {
    const int i = m_groups.indexOf(group);
    return i >= 0 ? m_panes.value(i) : m_panes.value(0);
}

VolumePane *DiscWindow::currentPane() const {
    if (m_tabs) return qobject_cast<VolumePane *>(m_tabs->currentWidget());
    return m_panes.value(0);
}

const fs::Disc *DiscWindow::paneDisc() const {
    const app::Document *d = document();
    return d && d->isOpen() ? &d->disc() : nullptr;
}

QString DiscWindow::documentName() const {
    const app::Document *d = document();
    return d && !d->path().isEmpty() ? QFileInfo(d->path()).fileName() : QString("untitled");
}

// ------------------------------------------------------------------------------------------- rows

QString DiscWindow::rootLabel(int mount) const {
    const app::Document *d = document();
    return d ? d->mountTitle(mount) : QString();
}

void DiscWindow::decorateRow(QTreeWidgetItem *row, int, const fs::Path &, const fs::Entry *entry) const {
    if (entry) row->setText(0, entry->name);
}

void DiscWindow::refresh() {
    const fs::Disc *disc = paneDisc();
    for (int i = 0; i < m_panes.size(); ++i) {
        QVector<int> mounts;
        if (disc)
            for (int m = 0; m < disc->mounts().size(); ++m)
                if (m_panes.size() == 1 || disc->mounts()[m].group == m_groups[i]) mounts << m;
        m_panes[i]->setMounts(mounts);
        m_panes[i]->refresh();
    }
    updateUi();
}

void DiscWindow::filesUnder(int mount, const fs::Path &dir, QVector<fs::Path> *out) const {
    const fs::Disc *disc = paneDisc();
    const fs::Volume *v = disc ? disc->volume(mount) : nullptr;
    QVector<fs::Entry> entries;
    QString err;
    if (!v || !v->list(dir, &entries, &err)) return;
    for (const fs::Entry &e : entries) {
        if (e.isDir()) filesUnder(mount, dir.child(e.name), out);
        else out->append(dir.child(e.name));
    }
}

QVector<PaneRow> DiscWindow::selectedRows() const {
    const VolumePane *p = currentPane();
    return p ? p->selectedRows() : QVector<PaneRow>();
}

QVector<DiscWindow::Item> DiscWindow::selectedFiles() const {
    QVector<Item> v;
    for (const PaneRow &r : selectedRows()) {
        if (r.isFile()) { v << Item(r.mount, r.path); continue; }
        QVector<fs::Path> under;
        filesUnder(r.mount, r.path, &under);
        for (const fs::Path &p : under) v << Item(r.mount, p);
    }
    QVector<Item> unique;
    for (const Item &i : v) if (!unique.contains(i)) unique << i;
    return unique;
}

// ------------------------------------------------------------------------------------------- the window's state

QString DiscWindow::readOnlyReason(int mount) const {
    const app::Document *d = document();
    if (!d || !d->isOpen() || d->disc().mounts().isEmpty()) return "no disc";
    return d->readOnlyWhy(mount);
}

QString DiscWindow::title() const {
    const app::Document *d = document();
    const QString n = d && d->isOpen() ? documentName() : QString("no disc");
    return n + (d && d->dirty() ? " *" : "") + " - " + appInfo().name;
}

void DiscWindow::updateActions() {
    const app::Document *d = document();
    const bool open = d && d->isOpen();
    const bool have = open && !d->disc().mounts().isEmpty();
    const QVector<PaneRow> rows = selectedRows();
    const int mount = rows.isEmpty() ? (currentPane() && !currentPane()->mounts().isEmpty() ? currentPane()->mounts().first() : 0)
                                     : rows.first().mount;
    const bool w = have && writable(mount);
    const bool files = have && !selectedFiles().isEmpty();
    const bool oneFile = rows.size() == 1 && rows.first().isFile();
    auto set = [this](Act a, bool on) { if (QAction *x = action(a)) x->setEnabled(on); };
    set(Act::Save, have && d->dirty() && w);
    set(Act::SaveAs, open);
    set(Act::Revert, open && d->dirty() && !d->path().isEmpty());
    set(Act::AddFiles, w);
    set(Act::AddFolder, w);
    set(Act::NewDirectory, w);
    set(Act::Delete, w && files);
    set(Act::Properties, files);
    set(Act::Rename, w && oneFile);
    set(Act::Extract, files);
    set(Act::Copy, files);
    set(Act::Paste, w);
    set(Act::View, have && oneFile);
}

void DiscWindow::updateUi() {
    updateActions();
    setWindowTitle(title());
    presentStatus(statusVariants());
}

void DiscWindow::presentStatus(const QStringList &variants) { statusBar()->showMessage(variants.value(0)); }

void DiscWindow::showMessage(const QString &text) { statusBar()->showMessage(text); }

void DiscWindow::message(const QString &title, const QString &text, bool warning) {
    if (autoAnswer) { showMessage(title + ": " + text.section('\n', 0, 0)); return; }
    if (warning) QMessageBox::warning(this, title, text);
    else QMessageBox::information(this, title, text);
}

void DiscWindow::showContextMenu(const QPoint &pos) {
    const QList<QAction *> acts = contextActions();
    VolumePane *p = qobject_cast<VolumePane *>(sender());
    if (acts.isEmpty() || !p) return;
    QMenu m;
    for (QAction *a : acts) { if (a) m.addAction(a); else m.addSeparator(); }
    m.exec(p->viewport()->mapToGlobal(pos));
}

// ------------------------------------------------------------------------------------------- closing, saving

bool DiscWindow::confirmDiscard() {
    const app::Document *d = document();
    if (!d || !d->dirty()) return true;
    if (autoAnswer) return true;
    auto r = QMessageBox::question(this, "Unsaved changes", "Discard the changes to " + QFileInfo(d->path()).fileName() + "?",
                                   QMessageBox::Discard | QMessageBox::Cancel);
    return r == QMessageBox::Discard;
}

void DiscWindow::closeEvent(QCloseEvent *e) {
    if (confirmDiscard()) e->accept(); else e->ignore();
}

bool DiscWindow::beforeSave() {
    const app::Document *d = document();
    for (int m = 0; m < d->disc().mounts().size(); ++m)
        if (!writable(m)) { message("Save", "The disc is read-only: " + readOnlyReason(m)); return false; }
    return true;
}

QString DiscWindow::savedMessage() const { return "Saved " + documentName(); }

bool DiscWindow::save() {
    app::Document *d = document();
    if (!d || !d->isOpen() || !d->dirty()) return true;
    if (!beforeSave()) return false;
    QString err;
    if (!d->save(keepBackup(), &err)) { message("Save", "Not saved: " + err); return false; }
    afterSave();
    refresh();
    showMessage(savedMessage());
    return true;
}

void DiscWindow::saveAsDialog() {
    const app::Document *d = document();
    if (!d || !d->isOpen()) return;
    QString path;
    device::Kind kind = device::Kind::None;
    if (askSaveAs(&path, &kind)) saveAs(path, kind);
}

bool DiscWindow::saveAs(const QString &path, device::Kind kind) {
    app::Document *d = document();
    if (!d || !d->isOpen()) return false;
    QString err;
    if (!d->saveAs(path, kind, &err)) { message("Save As", "Not saved: " + err); return false; }
    if (!afterSaveAs(path, &err)) { message("Save As", err); return false; }
    refresh();
    return true;
}

void DiscWindow::revertDocument() {
    if (app::Document *d = document()) d->revert();
}

void DiscWindow::revert() {
    const app::Document *d = document();
    if (!d || !d->isOpen() || d->path().isEmpty()) return;
    revertDocument();
    refresh();
}

// ------------------------------------------------------------------------------------------- Delete: the pattern for a change

QString DiscWindow::deleteQuestion(const QVector<Item> &items) const {
    if (items.size() != 1) return QString("Delete %1 files?").arg(items.size());
    const fs::Volume *v = paneDisc() ? paneDisc()->volume(items[0].mount) : nullptr;
    return QString("Delete %1?").arg(v ? v->displayPath(items[0].path) : items[0].path.toString());
}

bool DiscWindow::deleteFiles(const QVector<Item> &items, QString *err) {
    app::Document *d = document();
    if (!d || !d->isOpen()) { *err = "no disc"; return false; }
    for (const Item &i : items)
        if (!writable(i.mount)) { *err = "read-only: " + readOnlyReason(i.mount); return false; }
    return d->disc().transact([&](QString *e) {
        for (const Item &i : items) {
            fs::Volume *v = d->disc().volume(i.mount);
            if (!v) { *e = "no such volume"; return false; }
            if (!canRemove(i, e) || !v->remove(i.path, e)) return false;
        }
        return true;
    }, err);
}

void DiscWindow::deleteSelected() {
    const QVector<Item> items = selectedFiles();
    if (items.isEmpty()) return;
    if (!autoAnswer && QMessageBox::question(this, "Delete", deleteQuestion(items)) != QMessageBox::Yes) return;
    QString err;
    if (!deleteFiles(items, &err)) message("Delete", deleteFailedText(err));
    refresh();
}

// ------------------------------------------------------------------------------------------- Extract: the pattern for copying out

bool DiscWindow::exportItems(const QVector<Item> &items, QVector<TransferItem> *out, QString *err) const {
    out->clear();
    const fs::Disc *disc = paneDisc();
    if (!disc) { *err = "no disc"; return false; }
    for (const Item &i : items) {
        const fs::Volume *v = disc->volume(i.mount);
        if (!v) { *err = "no such volume"; return false; }
        fs::Entry e;
        if (!v->stat(i.path, &e, err)) return false;
        TransferItem t;
        if (!v->read(i.path, &t.data, err)) return false;
        t.name = e.name;
        t.hostName = v->hostNameFor(e);
        t.when = e.modified;
        t.attrs = v->toRetroIn(i.path.parent(), e);
        t.sourceFs = v->fsId();
        out->append(t);
    }
    return true;
}

std::shared_ptr<xfer::HostPolicy> DiscWindow::hostPolicy(const QVector<TransferItem> &) const {
    return std::make_shared<xfer::HostPolicy>();
}

QString DiscWindow::extractedMessage(int files, const QString &dir) const {
    return QString("Extracted %1 file%2 to %3").arg(files).arg(files == 1 ? "" : "s").arg(dir);
}

void DiscWindow::extractSelected() {
    const QVector<Item> items = selectedFiles();
    if (items.isEmpty()) return;
    QString dir = QFileDialog::getExistingDirectory(this, "Extract to folder", AppSettings().value("extractDir").toString());
    if (dir.isEmpty()) return;
    AppSettings().setValue("extractDir", dir);
    int n = 0;
    QStringList errors;
    QJsonArray j;
    QString err;
    if (!extractTo(items, dir, &n, &errors, &j, &err)) { message("Extract", err); return; }
    if (!errors.isEmpty()) message("Extract", errors.join("\n"));
    showMessage(extractedMessage(n, dir));
}

bool DiscWindow::extractTo(const QVector<Item> &items, const QString &dir, int *files, QStringList *errors, QJsonArray *json,
                           QString *err) {
    QVector<TransferItem> t;
    if (!exportItems(items, &t, err)) return false;
    QStringList top;
    xfer::writeHost(t, dir, *hostPolicy(t), &top, json, errors);
    *files = t.size();
    return true;
}

} // namespace ui
} // namespace dw
