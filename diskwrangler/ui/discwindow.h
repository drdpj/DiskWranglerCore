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
// Disc::transact), Extract (Volume::read and toRetroIn into TransferItems, written by the program's HostPolicy), the
// import session (importItems), and Properties, the disc's own attributes and Check (step 5f): the dialogs are the
// program's (propertiesEditor, volumeEditor, checkText / showCheck), the change, its transaction and the refresh the
// kit's. Opening (step 5g, openFile): the program's detection gives a Verdict, the kit asks which reading when two
// are equally likely, hands a disc another program owns to it (with Locate... when that program can't be found), and
// the program opens the reading chosen into its Document.
//
// A program's main window derives from it and fills the hooks below. Everything that names a filesystem, a character
// set, a format, DFS letters or side order stays in the program (it may test fsId(), the kit never does). Texts the
// user sees come from the program where the two programs differ; the kit's own texts are the ones both used.
//
// How a program builds on it (CP/M Disk Wrangler's MainWindow is the example of one pane; Acorn Disk Wrangler's, of
// several pane groups as tabs that come and go with the disc):
//  1. In its constructor, setupKit() first: it makes the panes (paneGroups(), createPane(), paneLayout()) and the
//     standard actions (actionSpec() gives each one's text, keys and icon text; an empty text leaves it out).
//  2. Build the menus, the button bar and its own actions from action(Act::...) in its own order.
//  3. refresh(). After any change: refresh() (or refreshChanged); the kit keeps the rows' expansion and selection as
//     the pane's layout says, and shows each group's tab or not as paneShown says.
// Slots a test calls by name stay on the program's window (as forwarding functions if the kit does the work) until
// the tests are ported after step 5.

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>
#include <memory>

#include <diskwrangler/app/appinfo.h>
#include <diskwrangler/app/document.h>
#include <diskwrangler/fs/detection.h>
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
    // What a Properties dialog changes in one file (or folder): the attributes (only the keys given change; none: no
    // setAttrs), then, if `to` is set and differs, a rename or move there (CP/M: another user area).
    struct PropertyChange {
        Item item;
        fs::AttrValues attrs;
        fs::Path to;                     // empty: it stays where it is
        bool moves() const { return !to.parts.isEmpty() && !(to == item.path); }
    };
    struct PropertiesEdit {
        QVector<PropertyChange> changes;
        // false: every change in one transaction, all or nothing (CP/M; a failure says propertiesFailedText). true:
        // each change in its own transaction, the ones that fail listed (propertiesItemError) and the rest kept.
        bool eachOnItsOwn = false;
    };
    // What a disc-attribute dialog (title, label, boot option) gives: the values for Volume::setVolumeAttrs, and the
    // title of the message if they are refused.
    struct VolumeEdit {
        fs::AttrValues values;
        QString title;
    };
    // The message when a disc isn't opened here: one another program owns that couldn't be started, or one the
    // program can't read. The words are the program's; showNotOpened adds the link to get the owner and Locate...
    struct NotOpened {
        QString title, text;
        // The title of the message that the owner was started instead ("This is a CP/M disc, so it has been opened in
        // CP/M Disk Wrangler."): empty = "Opened in <program>" (CP/M Disk Wrangler); Acorn's is its own title.
        QString startedTitle;
        bool warning = false;                    // the warning icon (else information)
        Qt::TextFormat format = Qt::PlainText;
        bool selectable = true;                  // the text can be selected and the link followed by keyboard too
        struct Button {
            QString text;
            std::function<void()> run;           // after the box has closed
        };
        QVector<Button> buttons;                 // the program's own (CP/M: "Choose a format..."), after OK
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
    // The tabs, when there are several pane groups (null with one).
    QTabWidget *tabWidget() const { return m_tabs; }

    // ----- the selection
    // The files the selection stands for, in order, each once: a selected file, and every file under a selected folder,
    // user area or root (filesUnder). Only from the current pane.
    QVector<Item> selectedFiles() const;
    QVector<PaneRow> selectedRows() const;

    // ----- read-only
    QString readOnlyReason(int mount) const;          // empty: writable (Document::readOnlyWhy, or "no disc")
    bool writable(int mount) const { return readOnlyReason(mount).isEmpty(); }

    // ----- the reference actions, for programs and tests
    // Removes the files (or folders, with what is in them) in one transaction: all of them, or (on any failure,
    // canRemove's included) none.
    bool deleteFiles(const QVector<Item> &items, QString *err);
    // The files as transfer items (Volume::read; names, hostName, dates and toRetroIn attributes as the volume gives
    // them). A folder is an item with its contents as kids, in the volume's order.
    bool exportItems(const QVector<Item> &items, QVector<TransferItem> *out, QString *err) const;
    // Extract without the folder question: the files written into dir by the program's HostPolicy. false: they couldn't
    // be read (err); otherwise the files that couldn't be written are in *errors, the rest are written, and *json gets
    // the retro-files items (makeJson turns them into the document). Returns the number of items in *files.
    bool extractTo(const QVector<Item> &items, const QString &dir, int *files, QStringList *errors, QJsonArray *json, QString *err);
    // Properties without the dialog: the changes in one transaction (all or none). Every mount writable, else
    // "read-only: <reason>".
    bool applyProperties(const QVector<PropertyChange> &changes, QString *err);
    // A disc attribute without the dialog: Volume::setVolumeAttrs in one transaction. Writable, else "read-only: ...".
    bool applyVolumeAttrs(int mount, const fs::AttrValues &values, QString *err);

    // ----- importing (importsession.h): Add files, drops, Paste and drops from another window all end here
    // Imports items into dir of mount through an ImportSession with the program's importHooks(mount) (the conflict
    // question is askReplace, answered Replace under autoAnswer), then refreshChanged({mount}) and importSummary().
    // readErrors: what the program couldn't read (host files), first in the result's errors. Returns the result.
    xfer::ImportResult importItems(int mount, const fs::Path &dir, const QVector<TransferItem> &items,
                                   const QStringList &froms = {}, const QVector<xfer::ImportError> &readErrors = {});

    // ----- opening (step 5g): the open pipeline. Open..., drops, the command line and Revert all end here.
    // Opens path in this window: beforeOpen (unsaved changes) → the program's detectImage gives a Verdict → two readings
    // equally likely (Verdict::Ask): askWhich (the first under autoAnswer; cancelled: openStopped) → a reading another
    // program owns: openStopped, then that program started with the image and the message that it was (showStarted,
    // titled handOverMessage's startedTitle); if it couldn't be started (or this program was handed the image by it),
    // handOverMessage in showNotOpened; under autoAnswer it isn't started and nothing is shown → else openDocument
    // opens the reading chosen (null: nothing recognised the disc) → afterOpen. true: opened here. force is the
    // program's own (Acorn's side order), passed to its hooks.
    bool openFile(const QString &path, int force = -1);
    // Open... and a dropped image: in this window if no image is open in it, else in a new window (createWindow, if
    // the program makes one), which is closed again if it didn't open the disc and closeFailedWindow(). Returns the
    // window the disc opened in, or null.
    DiscWindow *openInWindow(const QString &path);
    // Shows m (nothing under autoAnswer). For a disc owner->owner owns: the link to get that program and, when the
    // automatic lookup can't find it, Locate... (deliberate change 7: the user shows where it is, it is stored as
    // otherApps/<id> and started with the image). A program's button runs after the box closes. Returns true if the
    // owner was started.
    bool showNotOpened(const QString &path, const NotOpened &m, const fs::Candidate *owner = nullptr);

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
    // Properties: propertiesItems() → the program's propertiesEditor (its dialog) → applyProperties → refreshChanged().
    virtual void editProperties();
    // A disc attribute (key: "title", "label", "boot" ... as Volume::volumeAttrs names it) of a mount: the program's
    // volumeEditor (its dialog) → applyVolumeAttrs; refused: message(title, err); then refreshChanged({mount}).
    virtual void editVolume(int mount, const QString &key);
    // Check: Disc::validateAll, each mount's report beside it, shown by showCheck (default: checkText in a message
    // titled "Check disc", a warning if there are errors).
    virtual void checkDisc();

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
    // A group's tab: its text, and whether it is there at all. Both are asked again at every refresh(), so tabs can
    // come and go with the disc (Acorn Disk Wrangler: ADFS, DOS Plus, DFS); a tab keeps its place in paneGroups()'
    // order. Only with several groups (one pane has no tab).
    virtual QString paneTitle(const QString &group) const { return group; }
    virtual bool paneShown(const QString &) const { return true; }
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

    // After a change to these mounts (Delete, Properties, a disc attribute, an import): the rows again. Default:
    // refresh(), every pane. A program whose other panes don't show anything those mounts hold refreshes only theirs
    // (Acorn Disk Wrangler: a DOS or DFS change leaves the ADFS pane, its selection and current row, as they were).
    virtual void refreshChanged(const QVector<int> &) { refresh(); }

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
    // What Delete removes (default: selectedFiles(); a program that deletes folders with what is in them gives the
    // selected rows that aren't inside another selected row).
    virtual QVector<Item> deleteItems() const { return selectedFiles(); }
    // The question before deleting ("Delete <name>?", "Delete N files?"); the window shows it as a Yes / No box titled
    // "Delete".
    virtual QString deleteQuestion(const QVector<Item> &items) const;
    // Asks it: true = go ahead (default: deleteQuestion in that box; yes under autoAnswer). A program with a box of
    // its own (another default button) shows it here.
    virtual bool confirmDelete(const QVector<Item> &items);
    // false (default): everything in one transaction, all or nothing (deleteFailedText). true: each item in its own
    // transaction; the ones that fail are listed after the refresh (deleteItemError, a box titled "Delete"), the rest
    // are deleted.
    virtual bool deleteEachOnItsOwn() const { return false; }
    virtual QString deleteItemError(const Item &item, const QString &err) const { return item.path.name() + ": " + err; }
    // A file the program won't delete (CP/M: a read-only file): false, and err says why. Asked inside the transaction.
    virtual bool canRemove(const Item &, QString *) const { return true; }
    virtual QString deleteFailedText(const QString &err) const { return "Nothing was deleted: " + err; }

    // ----- hooks: Extract
    // How the items become host files (default: the core's plain policy).
    virtual std::shared_ptr<xfer::HostPolicy> hostPolicy(const QVector<TransferItem> &items) const;
    virtual QString extractedMessage(int files, const QString &dir) const;   // "Extracted N file(s) to <dir>"

    // ----- hooks: Properties, disc attributes, Check (step 5f). The dialogs are the program's; it builds them on the
    // Volume, Path and AttrValues (or its own classes), asks, and returns what changes. Nothing is written in them.
    // The items Properties shows (default: selectedFiles(); a program whose Properties also edit folders gives the rows).
    virtual QVector<Item> propertiesItems() const { return selectedFiles(); }
    // The dialog: false = cancelled (nothing applied, no refresh); true = apply edit->changes (may be none).
    virtual bool propertiesEditor(const QVector<Item> &, PropertiesEdit *) { return false; }
    virtual QString propertiesFailedText(const QString &err) const { return "Nothing was changed: " + err; }
    virtual QString propertiesItemError(const PropertyChange &c, const QString &err) const { return c.item.path.name() + ": " + err; }
    // The dialog for one disc attribute: false = cancelled (nothing applied, no refresh).
    virtual bool volumeEditor(int, const QString &, VolumeEdit *) { return false; }
    // Check's text from the whole report (each line with its mount's label when there are several) and each mount's.
    // Default: "No problems found.", or the errors and warnings under "Errors:" / "Warnings:".
    virtual QString checkText(const fs::Report &all, const QVector<fs::Report> &perMount) const;
    virtual void showCheck(const fs::Report &all, const QVector<fs::Report> &perMount);

    // ----- hooks: opening (openFile; step 5g)
    // Before anything is read: false stops (default: confirmDiscard()).
    virtual bool beforeOpen(const QString &) { return confirmDiscard(); }
    // The program's detection: open the file and say what it could be. false: it couldn't be read, and the program has
    // said why. Default: false (a program that doesn't open through the pipeline).
    virtual bool detectImage(const QString &, int, fs::Verdict *) { return false; }
    // Open pick, a reading this program handles (null when nothing recognised the disc), into the document. false: not
    // opened; the program has said why (its own message, or showNotOpened).
    virtual bool openDocument(const QString &, const fs::Verdict &, const fs::Candidate *, int) { return false; }
    // The disc isn't opened here after all (the question cancelled, or the disc handed to its owner): put the window
    // back as the program wants it (CP/M: no reading attached, the rows refreshed).
    virtual void openStopped() {}
    // The words when pick's owner couldn't be started (not found, or this program was handed the image by it), and
    // the title of the message when it was. Default: titled "Open", "<file>: <description>", information; the started
    // message titled "Opened in <program>".
    virtual NotOpened handOverMessage(const QString &path, const fs::Verdict &verdict, const fs::Candidate &pick) const;
    // Opened: the program's notices (read-only reasons, warnings, the status line). Default: refresh().
    virtual void afterOpen(const QString &) { refresh(); }
    // Locate...'s file chooser (default: ui::chooseOtherApp, a file dialog).
    virtual QString locateProgram(const fs::OtherApp &app);
    // A new window of this program, shown, for openInWindow; null: open in this one. And whether that window closes
    // again when the disc wasn't opened in it.
    virtual DiscWindow *createWindow() { return nullptr; }
    virtual bool closeFailedWindow() const { return true; }

    // ----- hooks: importing (importItems)
    // The session's hooks for an import into mount (ImportHooks). The kit fills conflict with askReplace if the
    // program leaves it empty. Default: nothing but that.
    virtual ImportHooks importHooks(int mount);
    // A file exists: Replace, Skip, the "all" answers or Cancel. Default: "<path> already exists. Replace it?" with
    // Yes, Yes to all, No, No to all and Cancel, titled "File exists".
    virtual xfer::Conflict askReplace(int mount, const fs::Path &target, const fs::Entry &existing, const TransferItem &incoming);
    // After an import (refreshChanged done): what happened, in the program's words. Default: "Added N file(s) (M replaced),
    // skipped K." in the status line, and the errors in a box titled "Add files".
    virtual void importSummary(const xfer::ImportResult &r);

    // The file name of the document, for messages ("untitled" if none).
    QString documentName() const;

private:
    void showContextMenu(const QPoint &pos);
    void updateTabs();
    bool exportEntry(const fs::Volume *v, const fs::Path &path, const fs::Entry &e, TransferItem *t, QString *err, int depth) const;
    QHash<int, QAction *> m_actions;
    QVector<VolumePane *> m_panes;
    QStringList m_groups;
    QTabWidget *m_tabs = nullptr;
};

} // namespace ui
} // namespace dw
