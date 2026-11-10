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
#pragma once

// A VolumePane is the tree of one or more mounts of a disc (one root row each: CP/M's two sides, DFS drives 0 and 2)
// in a DiscWindow (discwindow.h). Its rows come from Volume::list, in the volume's order; each row carries what it
// is - (mount, Path, NodeKind) in the roles below - and nothing is parsed back from cell text. What a row says (its
// cells, colours, the program's own roles) is the program's: the host's decorateRow writes it. Expansion and
// selection are kept across a refresh, by (mount, Path).

#include <QString>
#include <QTreeWidget>
#include <QVector>

#include <diskwrangler/fs/disc.h>
#include <diskwrangler/fs/volume.h>

namespace dw {
namespace ui {

// A column: its header, the widest text it normally shows (it sets the width; ignored for the first, Name, column,
// which takes the spare width), and whether it holds numbers (right-aligned, padded: setNumCell). As
// setupTreeColumns (treecolumns.h) lays out every pane.
struct PaneColumn {
    QString label, sample;
    bool numeric = false;
};

struct PaneLayout {
    QVector<PaneColumn> columns;
    // Rows seen for the first time: every row with something under it expanded (CP/M Disk Wrangler), or only the
    // root rows (Acorn Disk Wrangler). A row seen before keeps the state it had.
    enum Expand { ExpandAll, ExpandRoots } expand = ExpandAll;
    // After a refresh, rows selected before are selected again: every kind (the default), or files only (CP/M Disk
    // Wrangler has always let a selected user area or side go).
    bool reselectFolders = true;
};

// One row: the mount it is in, its path there (the root: an empty path), and what it is.
struct PaneRow {
    int mount = -1;
    fs::Path path;
    bool root = false;
    fs::NodeKind kind = fs::NodeKind::Dir;      // a root row: Dir
    bool isFile() const { return !root && kind == fs::NodeKind::File; }
    bool valid() const { return mount >= 0; }
};

// What the pane asks of the window that shows it.
class PaneHost {
public:
    virtual ~PaneHost() = default;
    virtual const fs::Disc *paneDisc() const = 0;                        // null: no disc
    // The root row's text for mount.
    virtual QString rootLabel(int mount) const = 0;
    // Writes a row's cells (and anything else: colours, tooltips, the program's own roles). entry is null for a
    // root row. Called once per row, when it is made, after its kit roles are set and before the rows under it.
    virtual void decorateRow(QTreeWidgetItem *row, int mount, const fs::Path &path, const fs::Entry *entry) const = 0;
    // A directory that couldn't be listed (err): by default the row says so in its tooltip.
    virtual void listFailed(QTreeWidgetItem *row, int mount, const fs::Path &path, const QString &err) const;
};

class VolumePane : public QTreeWidget {
    Q_OBJECT
public:
    // The kit's roles, on column 0 of every row (beside any the program adds in decorateRow).
    enum Role { RoleMount = Qt::UserRole + 64, RolePath, RoleNode };      // RolePath: QStringList; RoleNode: -1 root, else NodeKind

    explicit VolumePane(QWidget *parent = nullptr);

    void setHost(const PaneHost *host) { m_host = host; }
    void setPaneLayout(const PaneLayout &l);     // the columns (setupTreeColumns) and the expansion rules
    const PaneLayout &paneLayout() const { return m_layout; }
    void setMounts(const QVector<int> &mounts) { m_mounts = mounts; }   // which of the disc's mounts it shows, in order
    const QVector<int> &mounts() const { return m_mounts; }

    // Lists every mount again and rebuilds the rows, keeping expansion and selection by (mount, Path).
    void refresh();

    static PaneRow rowOf(const QTreeWidgetItem *it);
    // The selected rows, in the order the tree gives them.
    QVector<PaneRow> selectedRows() const;
    QTreeWidgetItem *findRow(int mount, const fs::Path &path) const;

private:
    void addChildren(QTreeWidgetItem *parent, int mount, const fs::Path &dir);
    static QString key(int mount, const fs::Path &p) { return QString::number(mount) + QChar(1) + p.parts.join(QChar(1)); }
    const PaneHost *m_host = nullptr;
    PaneLayout m_layout;
    QVector<int> m_mounts;
    QHash<QString, bool> m_expanded;             // during refresh: the rows as they were
};

} // namespace ui
} // namespace dw
