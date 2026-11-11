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
#pragma once

// The window kit (docs/design.md, "UI layer"; step 5): a DiscWindow shows the Document (app/document.h) a program
// opened, in one VolumePane per mount group (volumepane.h), and does what every Disk Wrangler window does the same
// way: the pane rows, refresh, the selection as (mount, Path), the standard actions and their enabling, the title,
// the status line, Save / Save As / Revert / Close with unsaved changes, Delete (a change made inside one
// Disc::transact) and Extract (Volume::read and toRetroIn into TransferItems, written by the program's HostPolicy).
//
// A program's main window derives from it and fills the hooks below. Everything that names a filesystem, a character
// set, a format, DFS letters or side order stays in the program (it may test fsId(), the kit never does). Texts the
// user sees come from the program where the two programs differ; the kit's own texts are the ones both used.
//
// How a program builds on it (CP/M Disk Wrangler's MainWindow is the example):
//  1. In its constructor, setupKit() first: it makes the panes (paneGroups(), createPane(), paneLayout()) and the
//     standard actions (actionSpec() gives each one's text, keys and icon text; an empty text leaves it out).
//  2. Build the menus, the button bar and its own actions from action(Act::...) in its own order.
//  3. refresh(). After any change: refresh(); the kit keeps the rows' expansion and selection.
// Slots a test calls by name stay on the program's window (as forwarding functions if the kit does the work) until
// the tests are ported after step 5.

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

#include <diskwrangler/app/appinfo.h>
#include <diskwrangler/app/document.h>
#include <diskwrangler/fs/volume.h>
#include <diskwrangler/ui/importsession.h>
#include <diskwrangler/ui/volumepane.h>
#include <diskwrangler/xfer/retrofiles.h>
#include <diskwrangler/xfer/transferitem.h>

class QAction;
class QTabWidget;

namespace dw {
namespace ui {

class DiscWindow : public QMainWindow, public PaneHost {
    Q_OBJECT
public:
    // The standard actions. The kit makes each one the program gives a text for, connected to the slot of the same
    // name below; where the kit has no generic version yet the slot does nothing until the program overrides it.
    enum class Act {
        NewWindow, NewImage, Open, Save, SaveAs, Revert, Close, Quit,
        View, Extract, AddFiles, AddFolder, Copy, Paste, Rename, Properties, Delete, NewDirectory,
    };
    struct ActionSpec {
        QString text;                    // menu text with its & ("E&xtract to folder..."); empty: no such action
        QList<QKeySequence> keys;        // first is the primary shortcut
        QString iconText;                // the button bar's text, if it has a button
        QString toolTip;
    };
    // A file (or folder) of the disc: which mount, and where.
    struct Item {
        int mount = -1;
        fs::Path path;
        Item() = default;
        Item(int m, const fs::Path &p) : mount(m), path(p) {}
        bool operator==(const Item &o) const { return mount == o.mount && path == o.path; }
    };

    explicit DiscWindow(QWidget *parent = nullptr);
    ~DiscWindow() override;

    // Every window of the kit in this program, and one by id (the id goes in drag and clipboard data).
    static QList<DiscWindow *> &all();
    static DiscWindow *byId(quint64 id);
    quint64 id() const { return quint64(quintptr(this)); }
    // Tests: no message boxes or questions (each question takes its first, "go ahead" answer; a message goes to the
    // status line as "Title: first line").
    static bool autoAnswer;

    virtual app::Document *document() const = 0;
    virtual app::AppInfo appInfo() const = 0;

    QAction *action(Act a) const { return m_actions.value(int(a)); }
    // The pane of a group (paneGroups()), or the first; null before setupKit().
    VolumePane *pane(const QString &group = QString()) const;
    QVector<VolumePane *> panes() const { return m_panes; }
    // The pane the user works in now (the current tab), or null.
    VolumePane *currentPane() const;

    // ----- the selection
    // The files the selection stands for, in order, each once: a selected file, and every file under a selected folder,
    // user area or root (filesUnder). Only from the current pane.
    QVector<Item> selectedFiles() const;
    QVector<PaneRow> selectedRows() const;

    // ----- read-only
    QString readOnlyReason(int mount) const;          // empty: writable (Document::readOnlyWhy, or "no disc")
    bool writable(int mount) const { return readOnlyReason(mount).isEmpty(); }

    // ----- the reference actions, for programs and tests
    // Removes the files in one transaction: all of them, or (on any failure, canRemove's included) none.
    bool deleteFiles(const QVector<Item> &items, QString *err);
    // The files as transfer items (Volume::read; names, hostName, dates and toRetroIn attributes as the volume gives them).
    bool exportItems(const QVector<Item> &items, QVector<TransferItem> *out, QString *err) const;
    // Extract without the folder question: the files written into dir by the program's HostPolicy. false: they couldn't
    // be read (err); otherwise the files that couldn't be written are in *errors, the rest are written, and *json gets
    // the retro-files items (makeJson turns them into the document). Returns the number of items in *files.
    bool extractTo(const QVector<Item> &items, const QString &dir, int *files, QStringList *errors, QJsonArray *json, QString *err);

    // ----- importing (importsession.h): Add files, drops, Paste and drops from another window all end here
    // Imports items into dir of mount through an ImportSession with the program's importHooks(mount) (the conflict
    // question is askReplace, answered Replace under autoAnswer), then refresh() and importSummary(). readErrors: what
    // the program couldn't read (host files), first in the result's errors. Returns the result.
    xfer::ImportResult importItems(int mount, const fs::Path &dir, const QVector<TransferItem> &items,
                                   const QStringList &froms = {}, const QVector<xfer::ImportError> &readErrors = {});

    // The disc of the current document, or null (PaneHost).
    const fs::Disc *paneDisc() const override;

public slots:
    // Rebuilds every pane's rows from the volumes, keeping expansion and selection, then updateUi().
    virtual void refresh();
    // Enables the actions, sets the title and the status line.
    virtual void updateUi();

    virtual bool save();
    virtual void saveAsDialog();
    virtual bool saveAs(const QString &path, dw::device::Kind kind);
    virtual void revert();
    virtual void deleteSelected();
    virtual void extractSelected();

    // Not generic yet: the program overrides these (each does nothing in the kit).
    virtual void newWindow() {}
    virtual void newImageDialog() {}
    virtual void openDialog() {}
    virtual void viewSelected() {}
    virtual void addFilesDialog() {}
    virtual void addFolderDialog() {}
    virtual void copySelected() {}
    virtual void paste() {}
    virtual void renameSelected() {}
    virtual void editProperties() {}
    virtual void newDirectory() {}
    virtual void about() {}

protected:
    // Makes the panes and the standard actions. Call it first in the program's constructor (the hooks it calls are
    // the program's by then).
    void setupKit();

    // ----- hooks: the window's parts
    virtual ActionSpec actionSpec(Act a) const = 0;
    // The pane groups, in order (Disc::Mount::group): one pane each; several are shown as tabs. Default: one pane,
    // which shows every mount.
    virtual QStringList paneGroups() const { return {QString()}; }
    virtual QString paneTitle(const QString &group) const { return group; }
    virtual PaneLayout paneLayout(const QString &group) const = 0;
    // The pane widget for a group: a program with its own drag-and-drop code returns a VolumePane subclass.
    virtual VolumePane *createPane(const QString &group);
    // The actions of the context menu, in order; nullptr is a separator.
    virtual QList<QAction *> contextActions() const { return {}; }
    // A file row was double-clicked (default: View).
    virtual void activateFile(const PaneRow &) { viewSelected(); }

    // ----- hooks: rows (PaneHost). Defaults: the mount's title; the entry's name in column 0.
    QString rootLabel(int mount) const override;
    void decorateRow(QTreeWidgetItem *row, int mount, const fs::Path &path, const fs::Entry *entry) const override;
    // The files under a folder, user area or (dir empty) root of mount, in the order the selection gives them.
    // Default: Volume::list, depth first.
    virtual void filesUnder(int mount, const fs::Path &dir, QVector<fs::Path> *out) const;

    // ----- hooks: title, status line, messages
    // "<file> * - <program>": the file name ("untitled" without one, "no disc" with none open), " *" when changed.
    virtual QString title() const;
    // The status line, longest first (the program shows the one that fits, or the first).
    virtual QStringList statusVariants() const { return {}; }
    virtual void presentStatus(const QStringList &variants);     // default: the first, in the status bar
    virtual void showMessage(const QString &text);                // a message in the status line until the next one
    virtual void message(const QString &title, const QString &text, bool warning = true);
    // Enables the standard actions (default: by the rules both programs share; see discwindow.cpp).
    virtual void updateActions();

    // ----- hooks: closing and saving
    virtual bool confirmDiscard();                 // unsaved changes: "Discard the changes to <file>?"
    void closeEvent(QCloseEvent *e) override;
    virtual bool keepBackup() const { return false; }             // a .bak copy on save (the program's setting)
    // Before a save: false stops it (the program has said why, or offered Save As). Default: every mount writable,
    // else "Save": "The disc is read-only: <reason>".
    virtual bool beforeSave();
    virtual void afterSave() {}                                   // saved (and every volume read again)
    virtual QString savedMessage() const;                         // "Saved <file>"
    // Save As: asks for the file and its kind (default: nothing to ask, false).
    virtual bool askSaveAs(QString *, device::Kind *) { return false; }
    // After Save As wrote the copy and the document moved to it: read what the program caches again. false (err): the
    // window says "Save As": err.
    virtual bool afterSaveAs(const QString &, QString *) { return true; }
    // Revert (default: the document drops its changes).
    virtual void revertDocument();

    // ----- hooks: Delete
    // The question before deleting ("Delete <name>?", "Delete N files?"); the window shows it as a Yes / No box titled
    // "Delete".
    virtual QString deleteQuestion(const QVector<Item> &items) const;
    // A file the program won't delete (CP/M: a read-only file): false, and err says why. Asked inside the transaction.
    virtual bool canRemove(const Item &, QString *) const { return true; }
    virtual QString deleteFailedText(const QString &err) const { return "Nothing was deleted: " + err; }

    // ----- hooks: Extract
    // How the items become host files (default: the core's plain policy).
    virtual std::shared_ptr<xfer::HostPolicy> hostPolicy(const QVector<TransferItem> &items) const;
    virtual QString extractedMessage(int files, const QString &dir) const;   // "Extracted N file(s) to <dir>"

    // ----- hooks: importing (importItems)
    // The session's hooks for an import into mount (ImportHooks). The kit fills conflict with askReplace if the
    // program leaves it empty. Default: nothing but that.
    virtual ImportHooks importHooks(int mount);
    // A file exists: Replace, Skip, the "all" answers or Cancel. Default: "<path> already exists. Replace it?" with
    // Yes, Yes to all, No, No to all and Cancel, titled "File exists".
    virtual xfer::Conflict askReplace(int mount, const fs::Path &target, const fs::Entry &existing, const TransferItem &incoming);
    // After an import (refresh() done): what happened, in the program's words. Default: "Added N file(s) (M replaced),
    // skipped K." in the status line, and the errors in a box titled "Add files".
    virtual void importSummary(const xfer::ImportResult &r);

    // The file name of the document, for messages ("untitled" if none).
    QString documentName() const;

private:
    void showContextMenu(const QPoint &pos);
    QHash<int, QAction *> m_actions;
    QVector<VolumePane *> m_panes;
    QStringList m_groups;
    QTabWidget *m_tabs = nullptr;
};

} // namespace ui
} // namespace dw
