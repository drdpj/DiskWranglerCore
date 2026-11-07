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

    printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
