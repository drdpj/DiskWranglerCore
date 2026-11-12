/*
 * A pane of a disc window - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include <diskwrangler/ui/volumepane.h>

#include <QSet>

#include <diskwrangler/ui/treecolumns.h>

namespace dw {
namespace ui {

void PaneHost::listFailed(QTreeWidgetItem *row, int, const fs::Path &, const QString &err) const {
    row->setToolTip(0, err);
}

VolumePane::VolumePane(QWidget *parent) : QTreeWidget(parent) {}

void VolumePane::setPaneLayout(const PaneLayout &l) {
    m_layout = l;
    setupTreeColumnsFrom(this, l.columns);
}

PaneRow VolumePane::rowOf(const QTreeWidgetItem *it) {
    PaneRow r;
    if (!it || !it->data(0, RoleMount).isValid()) return r;
    r.mount = it->data(0, RoleMount).toInt();
    r.path = fs::Path(it->data(0, RolePath).toStringList());
    const int k = it->data(0, RoleNode).toInt();
    r.root = k < 0;
    r.kind = k < 0 ? fs::NodeKind::Dir : fs::NodeKind(k);
    return r;
}

QVector<PaneRow> VolumePane::selectedRows() const {
    QVector<PaneRow> v;
    for (QTreeWidgetItem *it : selectedItems()) {
        const PaneRow r = rowOf(it);
        if (r.valid()) v << r;
    }
    return v;
}

QTreeWidgetItem *VolumePane::findRow(int mount, const fs::Path &path) const {
    QList<QTreeWidgetItem *> q;
    for (int i = 0; i < topLevelItemCount(); ++i) q << topLevelItem(i);
    while (!q.isEmpty()) {
        QTreeWidgetItem *it = q.takeFirst();
        const PaneRow r = rowOf(it);
        if (r.mount == mount && r.path == path) return it;
        for (int i = 0; i < it->childCount(); ++i) q << it->child(i);
    }
    return nullptr;
}

static void setKitRoles(QTreeWidgetItem *it, int mount, const fs::Path &p, int node) {
    it->setData(0, VolumePane::RoleMount, mount);
    it->setData(0, VolumePane::RolePath, p.parts);
    it->setData(0, VolumePane::RoleNode, node);
}

void VolumePane::refresh() {
    // what the rows were: expanded or not (every row), selected (files, or every kind)
    m_expanded.clear();
    QSet<QString> sel;
    QString cur;
    if (m_layout.keepCurrent && currentItem()) {
        const PaneRow r = rowOf(currentItem());
        if (r.valid()) cur = key(r.mount, r.path);
    }
    QList<QTreeWidgetItem *> q;
    for (int i = 0; i < topLevelItemCount(); ++i) q << topLevelItem(i);
    while (!q.isEmpty()) {
        QTreeWidgetItem *it = q.takeFirst();
        const PaneRow r = rowOf(it);
        if (r.valid()) {
            const QString k = key(r.mount, r.path);
            m_expanded.insert(k, it->isExpanded());
            if (it->isSelected() && m_layout.keepSelection && (r.isFile() || m_layout.reselectFolders)) sel.insert(k);
        }
        for (int i = 0; i < it->childCount(); ++i) q << it->child(i);
    }
    clear();
    const fs::Disc *disc = m_host ? m_host->paneDisc() : nullptr;
    if (disc) {
        for (int m : m_mounts) {
            if (!disc->volume(m)) continue;
            auto *root = new QTreeWidgetItem(this);
            setKitRoles(root, m, fs::Path(), -1);
            root->setText(0, m_host->rootLabel(m));
            m_host->decorateRow(root, m, fs::Path(), nullptr);
            addChildren(root, m, fs::Path());
            root->setExpanded(m_layout.rootsAlwaysOpen || m_expanded.value(key(m, fs::Path()), true));
        }
    }
    // the selection and the current row, as they were
    if (!sel.isEmpty() || !cur.isEmpty()) {
        QTreeWidgetItem *current = nullptr;
        q.clear();
        for (int i = 0; i < topLevelItemCount(); ++i) q << topLevelItem(i);
        while (!q.isEmpty()) {
            QTreeWidgetItem *it = q.takeFirst();
            const PaneRow r = rowOf(it);
            const QString k = key(r.mount, r.path);
            if (sel.contains(k)) it->setSelected(true);
            if (k == cur) current = it;
            for (int i = 0; i < it->childCount(); ++i) q << it->child(i);
        }
        if (current) setCurrentItem(current, 0, QItemSelectionModel::NoUpdate);
    }
    m_expanded.clear();
}

void VolumePane::addChildren(QTreeWidgetItem *parent, int mount, const fs::Path &dir) {
    const fs::Volume *v = m_host->paneDisc()->volume(mount);
    QVector<fs::Entry> entries;
    QString err;
    if (!v->list(dir, &entries, &err)) { m_host->listFailed(parent, mount, dir, err); return; }
    m_host->orderRows(mount, dir, &entries);
    for (const fs::Entry &e : entries) {
        const fs::Path p = dir.child(e.name);
        auto *it = new QTreeWidgetItem(parent);
        setKitRoles(it, mount, p, int(e.kind));
        it->setText(0, e.name);
        m_host->decorateRow(it, mount, p, &e);
        if (!e.isDir()) continue;
        if (m_host->listChildren(mount, p, p.parts.size())) addChildren(it, mount, p);
        const QString k = key(mount, p);
        it->setExpanded(m_expanded.contains(k) ? m_expanded.value(k) : m_layout.expand == PaneLayout::ExpandAll);
    }
}

} // namespace ui
} // namespace dw
