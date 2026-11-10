// Part of DiskWranglerCore - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
// uitest: the small helpers both programs use (file dialog filters, sizes, root labels, delete keys, tree column
// layout), the plain text and hex views, the viewer registry (order, replacing, choosing the view to open on) and the
// viewer dialog built on it (view menu, a view's check boxes, the 1 MiB limit, "Save as text" names). Run offscreen.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QTreeWidget>
#include <cstdio>
#include <diskwrangler/text/textviews.h>
#include <diskwrangler/text/viewers.h>
#include <diskwrangler/ui/treecolumns.h>
#include <diskwrangler/ui/uihelpers.h>
#include <diskwrangler/ui/viewerdialog.h>
#include <diskwrangler/app/appsettings.h>
#include <diskwrangler/app/handoff.h>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QJsonArray>
#include <QJsonObject>
#include <diskwrangler/ui/discwindow.h>
#include <diskwrangler/xfer/retrofiles.h>
#include "memvolume.h"

using namespace dw;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", msg); ++failures; } else printf("  ok: %s\n", msg); } while (0)

// A viewer like CP/M's text view: stops at a marker byte when asked, and reports where it was.
struct MarkerViewer : FileViewer {
    QString id() const override { return "marker"; }
    QString name() const override { return "Marker text"; }
    int order() const override { return 0; }
    int accepts(const QByteArray &d, const QVariantMap &) const override { return d.contains(char(0x1A)) || text::looksLikeText(d) ? 2 : 0; }
    QVector<ViewOption> options(const QByteArray &) const override { return {{"stop", "Stop at ^Z", "end of text", true}, {"nat", "National", QString(), false}}; }
    ViewResult render(const QByteArray &d, const QVariantMap &o) const override {
        int z = d.indexOf(char(0x1A));
        ViewResult r{text::plain(o["stop"].toBool() && z >= 0 ? d.left(z) : d), QString()};
        if (z >= 0) r.note = QString("^Z at byte %1").arg(z);
        if (o.contains("ctx")) r.text += "[" + o["ctx"].toString() + "]";
        return r;
    }
};

// A viewer like Acorn's BASIC view: only suits some bytes, wants the whole file, and a better score than text.
struct AllViewer : FileViewer {
    QString id() const override { return "all"; }
    QString name() const override { return "Whole file"; }
    int order() const override { return 20; }
    int accepts(const QByteArray &d, const QVariantMap &) const override { return d.startsWith("ALL") ? 3 : 0; }
    bool wantsAllData() const override { return true; }
    ViewResult render(const QByteArray &d, const QVariantMap &) const override { return {QString("%1 bytes seen").arg(d.size()), "whole"}; }
};

static QLabel *info(QWidget *w) { return w->findChildren<QLabel *>().last(); }      // the line under the text is the last label

static QCheckBox *box(QWidget *w, const QString &label) {
    for (QCheckBox *b : w->findChildren<QCheckBox *>()) if (b->text() == label) return b;
    return nullptr;
}

// ------------------------------------------------------------------------------------------- the window kit (step 5c)

// A window of the kit over a MemDocument: what a program's main window fills in, at its plainest.
class KitWindow : public dw::ui::DiscWindow {
public:
    explicit KitWindow(memvol::MemDocument *d, dw::ui::PaneLayout l = {}) : m_doc(d), m_layout(l) {
        if (m_layout.columns.isEmpty()) m_layout.columns = {{"Name", "", false}, {"Size", "100000", true}};
        setupKit();
        refresh();
    }
    dw::app::Document *document() const override { return m_doc; }
    dw::app::AppInfo appInfo() const override { dw::app::AppInfo a; a.name = "Kit Test"; return a; }
    QStringList shown;                                         // what the status line was given (first variant), and messages
protected:
    ActionSpec actionSpec(Act a) const override {
        switch (a) {
        case Act::Save: return {"&Save", {QKeySequence::Save}, QString(), QString()};
        case Act::Delete: return {"&Delete", {}, "Delete", QString()};
        case Act::Extract: return {"E&xtract to folder...", {}, "Extract", QString()};
        case Act::Rename: return {"Re&name...", {}, QString(), QString()};
        default: return {};
        }
    }
    dw::ui::PaneLayout paneLayout(const QString &) const override { return m_layout; }
    void decorateRow(QTreeWidgetItem *row, int mount, const dw::fs::Path &p, const dw::fs::Entry *e) const override {
        DiscWindow::decorateRow(row, mount, p, e);
        if (e && !e->isDir()) dw::setNumCell(row, 1, QString::number(e->size));
        row->setData(0, Qt::UserRole, 7);                     // a program's own role, beside the kit's
    }
    QStringList statusVariants() const override { return {QString("%1 mounts").arg(m_doc->disc().mounts().size())}; }
    void showMessage(const QString &t) override { shown << t; }
private:
    memvol::MemDocument *m_doc;
    dw::ui::PaneLayout m_layout;
};

static QStringList rowTexts(QTreeWidget *t) {
    QStringList out;
    QList<QPair<QTreeWidgetItem *, int>> q;
    for (int i = 0; i < t->topLevelItemCount(); ++i) q << qMakePair(t->topLevelItem(i), 0);
    while (!q.isEmpty()) {
        auto p = q.takeFirst();
        out << QString(p.second * 2, ' ') + p.first->text(0);
        for (int i = p.first->childCount() - 1; i >= 0; --i) q.prepend(qMakePair(p.first->child(i), p.second + 1));
    }
    return out;
}

static void kitTests() {
    using dw::fs::Path;
    using Item = dw::ui::DiscWindow::Item;
    dw::ui::DiscWindow::autoAnswer = true;
    QString err;

    printf("== window kit: one mount (rows, roles, columns)\n");
    {
        memvol::MemDocument doc(1, "/tmp/kit.img");
        auto *v = doc.vol(0);
        v->write(Path(), "a.txt", "aaa", {}, QDateTime(), false, &err);
        v->mkdir(Path(), "dir", &err);
        v->write(Path({"dir"}), "b.txt", "bbbb", {}, QDateTime(), false, &err);
        v->write(Path(), "c.txt", "c", {{"locked", true}}, QDateTime(), false, &err);
        doc.markClean();
        KitWindow w(&doc);
        dw::ui::VolumePane *p = w.pane();
        CHECK(p && w.centralWidget() == p && w.panes().size() == 1, "one group: one pane, the central widget");
        CHECK(p->columnCount() == 2 && p->headerItem()->text(1) == QString("Size") + dw::NumPad, "columns from the program's layout");
        const QStringList rows = rowTexts(p);
        CHECK(rows == QStringList({"MEM", "  a.txt", "  c.txt", "  dir", "    b.txt"}), qPrintable("rows in the volume's order, the root titled by the mount: " + rows.join("|")));
        QTreeWidgetItem *b = p->findRow(0, Path({"dir", "b.txt"}));
        const dw::ui::PaneRow r = dw::ui::VolumePane::rowOf(b);
        CHECK(b && r.mount == 0 && r.path == Path({"dir", "b.txt"}) && r.isFile() && b->data(0, Qt::UserRole).toInt() == 7,
              "a row carries (mount, Path, kind) and the program's own role");
        CHECK(b && b->text(1) == QString("4") + dw::NumPad, "decorateRow writes the cells");
        CHECK(p->findRow(0, Path({"dir"}))->isExpanded() && p->topLevelItem(0)->isExpanded(), "ExpandAll: every row with something under it opens");
        CHECK(w.windowTitle() == "kit.img - Kit Test" && w.shown.isEmpty(), qPrintable("title: " + w.windowTitle()));
        CHECK(w.action(dw::ui::DiscWindow::Act::Save) && !w.action(dw::ui::DiscWindow::Act::View), "only the actions the program names are made");

        // selection
        p->findRow(0, Path({"dir"}))->setSelected(true);
        p->findRow(0, Path({"a.txt"}))->setSelected(true);
        QVector<Item> sel = w.selectedFiles();
        CHECK(sel.size() == 2 && sel.contains(Item(0, Path({"a.txt"}))) && sel.contains(Item(0, Path({"dir", "b.txt"}))),
              "the selection as (mount, Path): a folder stands for the files under it");
        CHECK(w.action(dw::ui::DiscWindow::Act::Delete)->isEnabled() && !w.action(dw::ui::DiscWindow::Act::Rename)->isEnabled(),
              "Delete enabled with files selected; Rename only for one file");

        // expansion and selection kept across a refresh
        p->findRow(0, Path({"dir"}))->setExpanded(false);
        v->mkdir(Path(), "new", &err);
        v->write(Path({"new"}), "n.txt", "n", {}, QDateTime(), false, &err);
        w.refresh();
        CHECK(!p->findRow(0, Path({"dir"}))->isExpanded() && p->findRow(0, Path({"new"}))->isExpanded(),
              "a folder closed stays closed; a new one opens");
        CHECK(p->findRow(0, Path({"dir"}))->isSelected() && p->findRow(0, Path({"a.txt"}))->isSelected(), "the selection is kept");

        // Delete: one transaction; a refusal half way puts everything back
        p->clearSelection();
        doc.markClean();
        w.refresh();
        CHECK(!doc.dirty(), "nothing changed yet (the disc as made)");
        CHECK(!w.deleteFiles({Item(0, Path({"a.txt"})), Item(0, Path({"c.txt"}))}, &err) && err.contains("locked"),
              qPrintable("deleting a.txt and the locked c.txt fails: " + err));
        dw::fs::Entry e;
        CHECK(v->stat(Path({"a.txt"}), &e, &err) && !doc.dirty(), "a.txt is back, nothing is staged");
        p->findRow(0, Path({"a.txt"}))->setSelected(true);
        p->findRow(0, Path({"dir"}))->setSelected(true);
        w.deleteSelected();
        CHECK(!p->findRow(0, Path({"a.txt"})) && !p->findRow(0, Path({"dir", "b.txt"})) && p->findRow(0, Path({"c.txt"})),
              "Delete removes the selected files, and the rows go");
        CHECK(doc.dirty() && w.windowTitle() == "kit.img * - Kit Test", "the document is changed: the title says so");
        p->findRow(0, Path({"c.txt"}))->setSelected(true);
        w.deleteSelected();
        CHECK(p->findRow(0, Path({"c.txt"})) && !w.shown.isEmpty() && w.shown.last().startsWith("Delete: Nothing was deleted: "),
              qPrintable("a refused delete: the program's text, the file stays: " + w.shown.value(w.shown.size() - 1)));

        // Save
        CHECK(w.save() && doc.saves() == 1 && !doc.dirty() && w.shown.last() == "Saved kit.img" && w.windowTitle() == "kit.img - Kit Test",
              "Save: the document saves, the window says so");
        doc.vol(0)->write(Path(), "z.txt", "z", {}, QDateTime(), false, &err);
        doc.readOnly = "a test";
        w.refresh();
        CHECK(!w.save() && doc.saves() == 1 && w.shown.last() == "Save: The disc is read-only: a test", "a read-only disc isn't saved");
        CHECK(!w.action(dw::ui::DiscWindow::Act::Save)->isEnabled() && !w.deleteFiles({Item(0, Path({"z.txt"}))}, &err) && err == "read-only: a test",
              "read-only: Save off, Delete refused");
        doc.readOnly.clear();
    }

    printf("== window kit: two mounts in one group; Extract\n");
    {
        memvol::MemDocument doc(2, "/tmp/two.img");
        doc.vol(0)->write(Path(), "zero.txt", "side zero\n", {}, QDateTime(), false, &err);
        doc.vol(1)->mkdir(Path(), "d", &err);
        doc.vol(1)->write(Path({"d"}), "one.bin", QByteArray("\x01\x02\x03", 3), {{"locked", true}}, QDateTime(), false, &err);
        dw::ui::PaneLayout l;
        l.expand = dw::ui::PaneLayout::ExpandRoots;
        l.reselectFolders = false;
        l.columns = {{"Name", "", false}};
        KitWindow w(&doc, l);
        dw::ui::VolumePane *p = w.pane();
        CHECK(p->topLevelItemCount() == 2 && p->topLevelItem(0)->text(0) == "part 0" && p->topLevelItem(1)->text(0) == "part 1",
              "two mounts in one group: two root rows in one pane");
        CHECK(p->topLevelItem(1)->isExpanded() && !p->findRow(1, Path({"d"}))->isExpanded(), "ExpandRoots: only the roots open at first");
        p->findRow(1, Path({"d"}))->setSelected(true);
        p->findRow(0, Path({"zero.txt"}))->setSelected(true);
        QVector<Item> sel = w.selectedFiles();
        CHECK(sel.size() == 2 && sel.contains(Item(1, Path({"d", "one.bin"}))) && sel.contains(Item(0, Path({"zero.txt"}))),
              "the selection spans both roots");
        QTemporaryDir td;
        int n = 0;
        QStringList errors;
        QJsonArray j;
        CHECK(w.extractTo(sel, td.path(), &n, &errors, &j, &err) && n == 2 && errors.isEmpty(), "Extract writes them");
        QFile a(td.path() + "/zero.txt"), b(td.path() + "/one.bin");
        CHECK(a.open(QIODevice::ReadOnly) && a.readAll() == "side zero\n" && b.open(QIODevice::ReadOnly) && b.readAll() == QByteArray("\x01\x02\x03", 3),
              "the files hold the bytes read from each volume");
        bool locked = false;
        for (const QJsonValue &x : j)
            if (x.toObject()["name"].toString() == "one.bin")
                locked = x.toObject()["attributes"].toObject()["mem"].toObject()["locked"].toBool();
        const QByteArray doc2 = dw::xfer::makeJson(j, td.path(), "two.img", "mem", "Kit Test");
        QVector<dw::TransferItem> back;
        CHECK(j.size() == 2 && locked && dw::xfer::readJson(doc2, &back, &err) && back.size() == 2,
              qPrintable("the retro-files items carry each file's attributes, and the document reads back: " + err));
        w.refresh();
        CHECK(!p->findRow(1, Path({"d"}))->isSelected() && p->findRow(0, Path({"zero.txt"}))->isSelected(),
              "reselectFolders off: a refresh keeps the files selected, not the folders");
    }
    dw::ui::DiscWindow::autoAnswer = false;
}

int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    printf("== helpers\n");
    CHECK(anyCase("*.dsk *.img") == "*.dsk *.DSK *.img *.IMG", "a file filter gets upper-case twins");
    CHECK(anyCase("*.dat *.dsk dosboot*") == "*.dat *.DAT *.dsk *.DSK dosboot* DOSBOOT*", "also a pattern that doesn't start with *.");
    CHECK(anyCase("*") == "*" && anyCase("*.IMG") == "*.IMG" && anyCase("") == "", "nothing to add for *, upper case or nothing");
    CHECK(fmtBytes(1536 * 1024) == "1.5 MB" && fmtBytes(360 * 1024) == "360.0 KB" && fmtBytes(1024 * 1024) == "1.0 MB", "MB and KB");
    CHECK(fmtBytes(512) == "0.5 KB" && fmtBytes(0) == "0.0 KB", "under 1 KB in KB, as Acorn Disk Wrangler shows it");
    CHECK(fmtBytes(300, SmallSizes::AsBytes) == "300 bytes" && fmtBytes(0, SmallSizes::AsBytes) == "0 bytes" && fmtBytes(1023, SmallSizes::AsBytes) == "1023 bytes", "under 1 KB in bytes, as CP/M Disk Wrangler shows it");
    CHECK(fmtBytes(1024, SmallSizes::AsBytes) == "1.0 KB" && fmtBytes(2 * 1024 * 1024, SmallSizes::AsBytes) == "2.0 MB", "1 KB and up are the same either way");
    CHECK(rootLabel("$", "BeebSCSI0") == "$   [BeebSCSI0]" && rootLabel("C:\\", "") == "C:\\", "root label");
    CHECK(!deleteKeys().isEmpty(), "the platform has a Delete key");

    printf("== tree columns\n");
    {
        QTreeWidget t;
        setupTreeColumns(&t, {{"Name", "", false}, {"Attr", "R/O SYS", false}, {"Size", "1048576", true}});
        CHECK(t.columnCount() == 3 && t.headerItem()->text(0) == "Name" && t.headerItem()->text(1) == "Attr" && t.headerItem()->text(2) == QString("Size") + NumPad, "headers; a number column's has the pad");
        CHECK(t.header()->sectionResizeMode(0) == QHeaderView::Stretch && t.header()->sectionResizeMode(1) == QHeaderView::Interactive && !t.header()->stretchLastSection(), "Name takes the spare width");
        CHECK(t.columnWidth(2) > t.columnWidth(1) - 100 && t.columnWidth(1) > 0 && t.minimumWidth() > t.columnWidth(1) + t.columnWidth(2), "columns sized from the font; the tree can't be made narrower than they need");
        CHECK(t.headerItem()->textAlignment(2) & Qt::AlignRight, "numbers are right-aligned");
        CHECK(t.selectionMode() == QAbstractItemView::ExtendedSelection && t.uniformRowHeights() && t.contextMenuPolicy() == Qt::CustomContextMenu, "selection, rows, context menu");
        auto *it = new QTreeWidgetItem(&t);
        setNumCell(it, 2, "42");
        CHECK(it->text(2) == QString("42") + NumPad && (it->textAlignment(2) & Qt::AlignRight), "a number cell");
    }

    printf("== text views\n");
    CHECK(text::plain("a\r\nb\rc\nd") == "a\nb\nc\nd", "CR, LF and CRLF become newlines");
    CHECK(text::plain("x\ty\x01z") == "x\ty.z" && text::plain(QByteArray("end\x1A", 4)) == "end" && text::plain(QByteArray("\x1A" "end", 4)) == ".end", "tabs kept, control characters '.', a final ^Z dropped");
    CHECK(text::shown(0x60, true) == QChar(0x00A3) && text::shown(0x60, false) == '`' && text::shown(0xA9, false) == QChar(0xA9) && text::shown(0x80, false) == '.', "characters shown");
    CHECK(text::looksLikeText("Hello\r\nworld\r\n") && !text::looksLikeText(QByteArray("\x00\x01\x02\x03", 4)) && text::looksLikeText(QByteArray()), "looks like text");
    CHECK(text::hexDump("Hello, hex dump!\x01\xFF").startsWith("00000000  48 65 6C 6C 6F 2C 20 68  65 78 20 64 75 6D 70 21  |Hello, hex dump!|\n00000010  01 FF "), "hex dump layout");

    printf("== viewer registry\n");
    {
        ViewerRegistry r;
        addDefaultViewers(r);
        CHECK(r.viewers().size() == 2 && r.viewers()[0]->id() == "plain" && r.viewers()[0]->name() == "Text" && r.viewers()[1]->id() == "hex" && r.viewers()[1]->name() == "Hex dump", "Text, then Hex dump");
        CHECK(r.best("Hello\r\n", {})->id() == "plain" && r.best(QByteArray("\x00\x01", 2), {})->id() == "hex", "text opens as text, anything else as hex");
        r.add(std::make_shared<AllViewer>());
        CHECK(r.viewers().size() == 3 && r.viewers()[2]->id() == "all", "a program's own view comes after");
        CHECK(r.best("ALL of it", {})->id() == "all" && r.best("not all", {})->id() == "plain", "the best score wins");
        r.add(std::make_shared<AllViewer>());
        CHECK(r.viewers().size() == 3, "adding a view with an id already there replaces it");
        r.add(std::make_shared<MarkerViewer>());
        CHECK(r.viewers().size() == 4 && r.viewers()[0]->id() == "plain" && r.viewers()[1]->id() == "marker", "equal positions keep the order they were added in");
        r.remove("plain"); r.remove("nothing");
        CHECK(r.viewers().size() == 3 && r.viewers()[0]->id() == "marker" && !r.find("plain"), "a program replaces Text by removing it and adding its own; removing an absent id is harmless");
        ViewerRegistry none;
        CHECK(!none.best("x", {}), "no viewers, no best");
    }

    printf("== viewer dialog\n");
    {
        ViewerRegistry plainHex;
        addDefaultViewers(plainHex);
        auto *d = new ViewerDialog(plainHex, "NOTES", "Hello\r\nworld\r\n", nullptr);
        auto *combo = d->findChild<QComboBox *>();
        CHECK(d->windowTitle() == "View - NOTES" && combo->count() == 2 && combo->itemText(0) == "Text" && combo->itemText(1) == "Hex dump", "title and view menu");
        CHECK(d->viewIndex() == 0 && d->viewId() == "plain" && d->shownText() == "Hello\nworld\n", "text opens as text");
        combo->setCurrentIndex(1);
        CHECK(d->viewId() == "hex" && d->shownText().startsWith("00000000  48 65 6C 6C 6F 0D 0A"), "switching to hex");
        CHECK(box(d, "Wrap lines") && info(d)->text() == "14 bytes", "wrap check box");
        delete d;
        auto *b = new ViewerDialog(plainHex, "BIN", QByteArray("\x00\x01\x02\xFF", 4), nullptr);
        CHECK(b->viewId() == "hex", "binary opens as hex");
        delete b;

        ViewerRegistry reg;
        addDefaultViewers(reg);
        reg.remove("plain");                                    // as CP/M's own text view replaces the core's
        reg.add(std::make_shared<MarkerViewer>());
        reg.add(std::make_shared<AllViewer>());
        QByteArray t = QByteArray("one\r\ntwo\x1A") + QByteArray(20, '\x1A');
        auto *m = new ViewerDialog(reg, "T", t, nullptr, QVariantMap{{"ctx", "uk"}});
        auto *stop = box(m, "Stop at ^Z"), *nat = box(m, "National");
        CHECK(stop && nat && stop->isChecked() && !nat->isChecked(), "a view's check boxes, in their starting state");
        CHECK(m->viewId() == "marker" && stop->isEnabled() && m->shownText() == "one\ntwo[uk]", "opens on the program's view; the context reaches it; it stops at ^Z");
        CHECK(info(m)->text() == QString("%1 bytes  -  ^Z at byte 8").arg(t.size()), "byte count and the view's note");
        stop->setChecked(false);
        CHECK(m->shownText().startsWith("one\ntwo.") && m->shownText().endsWith("[uk]"), "unticking it shows the rest");
        m->findChild<QComboBox *>()->setCurrentIndex(1);
        CHECK(m->viewId() == "hex" && !stop->isEnabled() && !nat->isEnabled(), "another view: the first view's boxes are greyed");
        CHECK(box(m, "Wrap lines")->isEnabled(), "wrap stays on");
        delete m;

        QByteArray big = QByteArray("ALL") + QByteArray(1024 * 1024 + 100, 'x');
        auto *w = new ViewerDialog(reg, "BIG", big, nullptr);
        CHECK(w->viewId() == "all" && w->shownText() == QString("%1 bytes seen").arg(big.size()), "a view that wants the whole file gets it");
        CHECK(info(w)->text() == QString("%1 bytes  -  whole").arg(big.size()), "and no limit is announced");
        w->findChild<QComboBox *>()->setCurrentIndex(1);       // hex of the first MiB only
        CHECK(info(w)->text() == QString("%1 bytes  -  showing the first 1 MiB only").arg(big.size()) && w->shownText().count('\n') == 1024 * 1024 / 16, "the others get the first 1 MiB and say so");
        delete w;
    }

#ifdef Q_OS_UNIX
    printf("== handing an image to another program of the family\n");
    {
        QCoreApplication::setOrganizationName("diskwranglercore-uitest");
        QCoreApplication::setApplicationName("handoff");
        QTemporaryDir tmp;
        const QString prog = tmp.filePath("fakewrangler"), out = tmp.filePath("out.txt"), img = tmp.filePath("disc.img");
        QFile s(prog);
        s.open(QIODevice::WriteOnly);
        s.write(QString("#!/bin/sh\nprintf '%s\\n%s\\n' \"$1\" \"$DISKWRANGLER_HANDOFF\" > '%1.part' && mv '%1.part' '%1'\n").arg(out).toUtf8());
        s.close();
        s.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QFile(img).open(QIODevice::WriteOnly);
        const dw::fs::OtherApp other{"no-such-wrangler-xyz", "No Such Wrangler", "No Such Wrangler", "https://example.org"};
        QString why;
        CHECK(dw::app::findOtherApp(other).isEmpty() && !dw::app::startOtherApp(other, img, &why) && why.contains("not found"),
              "a program that isn't there: not started, and says so");
        AppSettings().setValue("otherApps/" + other.id, prog);       // the path the user set wins
        CHECK(dw::app::findOtherApp(other) == prog, "the path set in the settings is used");
        CHECK(dw::app::startOtherApp(other, img, &why), "started");
        QElapsedTimer t;
        t.start();
        while (!QFile::exists(out) && t.elapsed() < 10000) QThread::msleep(20);
        QFile o(out);
        const QStringList lines = o.open(QIODevice::ReadOnly) ? QString::fromUtf8(o.readAll()).split('\n') : QStringList();
        CHECK(lines.size() >= 2 && lines[0] == img && lines[1] == img, "it got the image, and was told in its environment that it was handed it");
        CHECK(!dw::app::handedBack(img), "this program wasn't handed the image");
        qputenv(dw::app::HandoffVariable, img.toUtf8());
        CHECK(dw::app::handedBack(img) && !dw::app::handedBack(prog), "a program started with it knows not to hand it back");
        qunsetenv(dw::app::HandoffVariable);
        AppSettings().remove("otherApps/" + other.id);
    }
#endif

    kitTests();

    printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
