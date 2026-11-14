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
//
// Drops (step 6c): the pane sorts what is dropped on it - items from a window of this process (the private formats of
// xfer/mime.h), another program's retro-files JSON, local files - and emits one signal for each kind; which drops it
// takes is the program's (dropRules). The program's subclass makes the drags out (makeMime, startDrag): their payload
// is its own; it runs them with execDragOut, which says what came of them (6c-2).

#include <QByteArray>
#include <QDrag>
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
    // false: nothing is selected again after a refresh, and there is no current row (Acorn Disk Wrangler's ADFS and
    // DOS panes, which have always been rebuilt from nothing); reselectFolders is then not used.
    bool keepSelection = true;
    // The row that was current is made current again after a refresh, without changing the selection (Acorn Disk
    // Wrangler's DFS pane, where the current row says which side is in use).
    bool keepCurrent = false;
    // Root rows are expanded at every refresh, whatever they were before (Acorn Disk Wrangler's ADFS and DOS panes);
    // otherwise only when first seen.
    bool rootsAlwaysOpen = false;
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
    // The order of a directory's rows: by default the volume's own (Volume::list). Acorn Disk Wrangler sorts a DFS
    // catalogue by directory letter and name.
    virtual void orderRows(int, const fs::Path &, QVector<fs::Entry> *) const {}
    // Whether the rows under a directory row are made; depth is the number of names in its path. false: the row is
    // shown with nothing under it (Acorn Disk Wrangler stops at a directory it has already shown, a loop on a damaged
    // disc, and at its depth limits). Called after the row's decorateRow. Default: always.
    virtual bool listChildren(int, const fs::Path &, int) const { return true; }
};

class VolumePane : public QTreeWidget {
    Q_OBJECT
public:
    // What a pane takes when something is dragged over it (asked on enter, move and drop). The defaults take
    // everything: items from another pane or window, retro-files JSON and local files.
    struct DropRules {
        bool filesOnly = false;          // only local files (CP/M Disk Wrangler's window with no disc read, which opens them)
        bool take = true;                // false: nothing (CP/M Disk Wrangler's read-only disc)
        bool fromItself = false;         // items dragged from this very pane too (a disc with two sides in one pane)
        bool filesNotJson = false;       // a drop with retro-files JSON and files gives the files (Acorn Disk
                                         // Wrangler's empty window, which opens an image dragged out of another window)
    };

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

    // ----- drag and drop (step 6c)
    // The window and pane a drag from here names: its DW_MIME_SOURCE is "pid:window:pane" (sourceTag()).
    void setDragSource(quint64 window, int pane) { m_window = window; m_paneNo = pane; }
    quint64 sourceWindow() const { return m_window; }
    int sourcePane() const { return m_paneNo; }
    QByteArray sourceTag() const;
    // Whether a drag came from a window of this process: its tag names this process and a window still open. (Window
    // ids are addresses, so another copy of the program can have a window with the same id: the process is checked too.)
    static bool fromThisProcess(const QMimeData *m);
    // Where a drop event is, in the viewport (QDropEvent::pos() is deprecated in Qt 6, position() new in it).
    static QPoint dropPos(const QDropEvent *e);

    // How long a drag let go outside the program waits for the target to ask for its files (on X11 it asks after
    // QDrag::exec() returns) before dragOutNothing says none were (6c-2).
    static constexpr int DragOutWaitMs = 5000;

signals:
    // A drop, already sorted (only one is emitted): items from a window of this process (the DW_MIME_ITEMS payload,
    // the source's own), retro-files JSON from another program or copy, or local files and folders. target: the row
    // it was dropped on, or null.
    void internalDrop(const QByteArray &payload, QTreeWidgetItem *target);
    void retroFilesDropped(const QByteArray &json, QTreeWidgetItem *target);
    void pathsDropped(const QStringList &paths, QTreeWidgetItem *target);
    // What came of a drag run by execDragOut (6c-2), when it was let go outside this program (a drop on a window of
    // this program says neither: its import reports). dragOutFinished: the target asked for the files and they were
    // written (files and folders at the top, and what couldn't be written). dragOutNothing: the drop was refused or
    // cancelled, or the target never asked for the files, so nothing was copied.
    void dragOutFinished(int files, int dirs, const QStringList &errors);
    void dragOutNothing();

protected:
    // ----- hooks for the program's pane
    virtual DropRules dropRules() const { return DropRules(); }
    // The drag data for these rows (the program's own payload; null: nothing to drag). QTreeWidget's mimeData()
    // calls it.
    virtual QMimeData *makeMime(const QList<QTreeWidgetItem *> &) const { return nullptr; }

    // Runs a drag made from makeMime's data (the program's startDrag sets its picture first), copy only, and reports
    // what came of it with dragOutFinished or dragOutNothing. Its data (a DragOutMime) are served by serveXdnd on X11
    // (xdndsource.h). The drag is not deleted: on X11 the target asks for the files after exec() returns, and Qt
    // deletes the drag when the target has finished.
    Qt::DropAction execDragOut(QDrag *drag);

    // Whether this pane takes m, by dropRules(). Only format names are looked at, never the data: asking for a drag's
    // files makes its source write them.
    bool acceptable(const QMimeData *m) const;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    // Sorts the drop (items from this process, then retro-files JSON unless filesNotJson, then local files) and emits
    // the signal for it.
    void dropEvent(QDropEvent *e) override;
    Qt::DropActions supportedDropActions() const override { return Qt::CopyAction; }   // copying never removes the source
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)               // Qt 6 passes the list by reference, Qt 5 by value
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override { return makeMime(items); }
#else
    QMimeData *mimeData(const QList<QTreeWidgetItem *> items) const override { return makeMime(items); }
#endif

private:
    void addChildren(QTreeWidgetItem *parent, int mount, const fs::Path &dir);
    static QString key(int mount, const fs::Path &p) { return QString::number(mount) + QChar(1) + p.parts.join(QChar(1)); }
    const PaneHost *m_host = nullptr;
    PaneLayout m_layout;
    QVector<int> m_mounts;
    QHash<QString, bool> m_expanded;             // during refresh: the rows as they were
    quint64 m_window = 0;                        // setDragSource
    int m_paneNo = 0;
};

} // namespace ui
} // namespace dw
