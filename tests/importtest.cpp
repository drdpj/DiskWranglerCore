// Part of DiskWranglerCore - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
// importtest: the Importer (diskwrangler/xfer/importer.h) on the core's DFS and FAT volumes, on discs made here.
// The command line's way (options decide, the first failure stops the run) and the windows' way (a conflict callback
// with its five answers, "all" answers remembered, Cancel keeping what was done, onError = Continue recording errors
// and importing the rest), the directory hook's answers, attributes kept when a plain file replaces a file, and a
// locked or read-only file replaced only with --force or a Replace answer. QtCore only.
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>
#include <cstdio>
#include <memory>
#include <diskwrangler/device/container.h>
#include <diskwrangler/device/linear.h>
#include <diskwrangler/fs/dfs/dfsvolume.h>
#include <diskwrangler/fs/disc.h>
#include <diskwrangler/fs/fat/fatvolume.h>
#include <diskwrangler/xfer/importer.h>

using namespace dw;
using xfer::Conflict;
using xfer::DirChoice;
using xfer::ImportError;
using xfer::Importer;
using xfer::ImportOptions;
using xfer::ImportResult;
using xfer::OnError;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", qPrintable(QString(msg))); ++failures; } else printf("  ok: %s\n", qPrintable(QString(msg))); } while (0)

static void spit(const QString &p, const QByteArray &d) { QFile f(p); f.open(QIODevice::WriteOnly); f.write(d); }

// A fresh disc with one volume: a 400-sector DFS side, or a 720K PC FAT floppy.
struct Fixture {
    device::Container c;
    std::unique_ptr<device::Linear> lin;
    std::shared_ptr<fs::Volume> vol;
    std::unique_ptr<fs::Disc> disc;
    fs::Volume *v() const { return vol.get(); }
};

static int serial = 0;

static std::unique_ptr<Fixture> makeDfs(const QString &dir) {
    QByteArray img(400 * 256, 0);
    const QByteArray t = QByteArray("IMPORT TEST").leftJustified(12, ' ');
    img.replace(0, 8, t.left(8));
    img.replace(256, 4, t.mid(8, 4));
    img[256 + 6] = char((400 >> 8) & 3);
    img[256 + 7] = char(400 & 0xFF);
    const QString path = QString("%1/d%2.ssd").arg(dir).arg(++serial);
    spit(path, img);
    auto f = std::make_unique<Fixture>();
    QString err;
    if (!f->c.open(path, &err)) { printf("  FAIL: open %s: %s\n", qPrintable(path), qPrintable(err)); ++failures; return nullptr; }
    f->lin = std::make_unique<device::Linear>(&f->c);
    auto d = std::make_shared<dfs::Disc>();
    dfs::Layout lay;
    lay.sectorsPerSide = 400;
    if (!d->attach(f->lin.get(), quint64(img.size()), 0, lay, &err)) { printf("  FAIL: attach: %s\n", qPrintable(err)); ++failures; return nullptr; }
    f->vol = std::make_shared<dfs::Volume>(d);
    f->disc = std::make_unique<fs::Disc>(&f->c);
    f->disc->addMount({"drive 0", f->vol});
    return f;
}

static std::unique_ptr<Fixture> makeFat(const QString &dir) {
    QByteArray img(737280, 0);
    const unsigned char bpb[] = {0xEB, 0x3C, 0x90, 'M', 'S', 'D', 'O', 'S', '5', '.', '0', 0x00, 0x02, 2, 1, 0, 2, 112, 0,
                                 0xA0, 0x05, 0xF9, 3, 0, 9, 0, 2, 0};
    img.replace(0, int(sizeof(bpb)), QByteArray(reinterpret_cast<const char *>(bpb), int(sizeof(bpb))));
    img[510] = char(0x55); img[511] = char(0xAA);
    for (int fat : {1, 4}) { img[fat * 512] = char(0xF9); img[fat * 512 + 1] = char(0xFF); img[fat * 512 + 2] = char(0xFF); }
    const QString path = QString("%1/f%2.img").arg(dir).arg(++serial);
    spit(path, img);
    auto f = std::make_unique<Fixture>();
    QString err;
    if (!f->c.open(path, &err)) { printf("  FAIL: open %s: %s\n", qPrintable(path), qPrintable(err)); ++failures; return nullptr; }
    f->lin = std::make_unique<device::Linear>(&f->c);
    auto fv = std::make_shared<fat::Volume>();
    if (!fv->attach(f->lin.get(), 0, 737280, &err)) { printf("  FAIL: attach: %s\n", qPrintable(err)); ++failures; return nullptr; }
    f->vol = std::make_shared<fat::FsVolume>(fv);
    f->disc = std::make_unique<fs::Disc>(&f->c);
    f->disc->addMount({"DOS", f->vol});
    return f;
}

static TransferItem file(const QString &name, const QByteArray &data, const QMap<QString, QJsonObject> &attrs = {}) {
    TransferItem t;
    t.name = name;
    t.data = data;
    t.attrs = attrs;
    return t;
}

static TransferItem folder(const QString &name, const QVector<TransferItem> &kids) {
    TransferItem t;
    t.name = name;
    t.isDir = true;
    t.kids = kids;
    return t;
}

static QByteArray readBack(fs::Volume *v, const fs::Path &p) {
    QByteArray d;
    QString err;
    if (!v->read(p, &d, &err)) return QByteArray("<missing>");
    return d;
}

static fs::Entry statOf(fs::Volume *v, const fs::Path &p) {
    fs::Entry e;
    QString err;
    v->stat(p, &e, &err);
    return e;
}

static int countFiles(fs::Volume *v, const fs::Path &dir = fs::Path()) {
    QVector<fs::Entry> l;
    QString err;
    v->list(dir, &l, &err);
    return l.size();
}

static QStringList names(const QVector<TransferItem> &items) {
    QStringList s;
    for (const TransferItem &t : items) s << t.name;
    return s;
}

// Runs one import as the windows do (a scripted conflict callback, onError Continue) or as the command line does.
struct Run {
    ImportResult r;
    ImportError err;
    bool ok = false;
    int asked = 0;
    QStringList askedFor;
};

static Run runGui(fs::Volume *v, const QVector<TransferItem> &items, QList<Conflict> answers, const xfer::DirHook &hook = {},
                  OnError onError = OnError::Continue, const fs::Path &dir = fs::Path()) {
    Run run;
    Importer imp(v, "vol", ImportOptions());
    imp.setOnError(onError);
    imp.setConflictCallback([&](const fs::Path &target, const fs::Entry &existing, const TransferItem &incoming) {
        ++run.asked;
        run.askedFor << target.toString() + "<-" + incoming.name + (existing.name.isEmpty() ? "?" : "");
        return answers.isEmpty() ? Conflict::Cancel : answers.takeFirst();
    });
    if (hook) imp.setDirHook(hook);
    run.ok = imp.import(items, names(items), dir, &run.r, &run.err);
    return run;
}

static Run runCli(fs::Volume *v, const QVector<TransferItem> &items, const ImportOptions &o, const fs::Path &dir = fs::Path()) {
    Run run;
    Importer imp(v, "vol", o);
    run.ok = imp.import(items, names(items), dir, &run.r, &run.err);
    return run;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    const QString d = tmp.path();
    QString err;
    const QMap<QString, QJsonObject> locked = {{"acorn", QJsonObject{{"access", "LWR"}, {"load", "00001900"}, {"exec", "00008023"}}}};
    const QMap<QString, QJsonObject> addr = {{"acorn", QJsonObject{{"load", "00003000"}, {"exec", "00003100"}, {"access", "WR"}}}};

    printf("Without a callback: the options decide (the command line)\n");
    {
        auto f = makeDfs(d);
        fs::Volume *v = f->v();
        Run a = runCli(v, {file("ONE", "1", addr), file("TWO", "2")}, ImportOptions());
        CHECK(a.ok && a.r.added == 2 && a.r.changes.size() == 2 && a.r.changes[0].op == "add" && a.r.changes[0].path == "$.ONE" &&
                  a.r.changes[0].from == "ONE", "two files added, the changes logged");
        Run b = runCli(v, {file("ONE", "x")}, ImportOptions());
        CHECK(!b.ok && b.err.kind == ImportError::Conflict && b.err.message == "$.ONE exists: use --replace or --skip" && b.err.path == "$.ONE",
              "an existing file with neither --replace nor --skip: a Conflict error, " + b.err.message);
        ImportOptions skip; skip.skip = true;
        Run c = runCli(v, {file("ONE", "x")}, skip);
        CHECK(c.ok && c.r.skipped == 1 && c.r.changes[0].op == "skip" && c.r.changes[0].note == "exists" && readBack(v, fs::Path({"$.ONE"})) == "1",
              "--skip: skipped, the old data stays");
        ImportOptions rep; rep.replace = true;
        Run e = runCli(v, {file("ONE", "new")}, rep);
        const fs::Entry one = statOf(v, fs::Path({"$.ONE"}));
        CHECK(e.ok && e.r.replaced == 1 && readBack(v, fs::Path({"$.ONE"})) == "new" && one.attrs["load"].toUInt() == 0x3000u &&
                  one.attrs["exec"].toUInt() == 0x3100u, "--replace with a plain file: replaced, the old file's load and exec kept");
        CHECK(runCli(v, {file("LOCKED", "l", locked)}, ImportOptions()).ok && statOf(v, fs::Path({"$.LOCKED"})).attrs["locked"].toBool(), "a locked file added");
        Run g = runCli(v, {file("LOCKED", "m")}, rep);
        CHECK(!g.ok && g.err.kind == ImportError::Locked && g.err.message == "$.LOCKED is locked: use --force to replace it" &&
                  readBack(v, fs::Path({"$.LOCKED"})) == "l", "--replace on a locked file without --force: a Locked error, nothing written");
        ImportOptions force = rep; force.force = true;
        Run h = runCli(v, {file("LOCKED", "m")}, force);
        const fs::Entry lk = statOf(v, fs::Path({"$.LOCKED"}));
        CHECK(h.ok && h.r.replaced == 1 && readBack(v, fs::Path({"$.LOCKED"})) == "m" && !lk.attrs["locked"].toBool() &&
                  lk.attrs["load"].toUInt() == 0x1900u, "--replace --force: replaced, unlocked, the load address kept");
        // Stop: the run ends at the first failure, and the caller's transaction puts everything back
        const device::Container::Snapshot before = f->c.snapshot();
        Run s;
        CHECK(!f->disc->transact([&](QString *x) {
                  s = runCli(v, {file("THREE", "3"), file("ONE", "again"), file("FOUR", "4")}, ImportOptions());
                  if (!s.ok) *x = s.err.message;
                  return s.ok;
              }, &err), "Stop: a conflict in the middle fails the run: " + err);
        CHECK(s.r.added == 1 && s.r.errors.isEmpty() && f->c.snapshot() == before && readBack(v, fs::Path({"$.THREE"})) == "<missing>" &&
                  readBack(v, fs::Path({"$.FOUR"})) == "<missing>", "Stop: FOUR never tried, THREE put back by the transaction");
    }

    printf("FAT without a callback: read-only files, attributes kept, directories made\n");
    {
        auto f = makeFat(d);
        fs::Volume *v = f->v();
        const QMap<QString, QJsonObject> hidden = {{"fat", QJsonObject{{"hidden", true}, {"readonly", false}, {"system", false}, {"archive", true}}}};
        Run a = runCli(v, {folder("GAMES", {file("ELITE.COM", "elite", hidden)})}, ImportOptions(), fs::Path({"SUB"}));
        CHECK(a.ok && a.r.dirs == 2 && a.r.added == 1 && statOf(v, fs::Path({"SUB", "GAMES", "ELITE.COM"})).attrs["hidden"].toBool(),
              "a folder into a directory that doesn't exist yet: both made, the file hidden");
        ImportOptions rep; rep.replace = true;
        Run b = runCli(v, {file("ELITE.COM", "plain")}, rep, fs::Path({"SUB", "GAMES"}));
        CHECK(b.ok && b.r.replaced == 1 && readBack(v, fs::Path({"SUB", "GAMES", "ELITE.COM"})) == "plain" &&
                  statOf(v, fs::Path({"SUB", "GAMES", "ELITE.COM"})).attrs["hidden"].toBool(), "a plain file over a hidden one: still hidden");
        CHECK(v->setAttrs(fs::Path({"SUB", "GAMES", "ELITE.COM"}), {{"readonly", true}}, &err), "made read-only");
        Run c = runCli(v, {file("ELITE.COM", "again")}, rep, fs::Path({"SUB", "GAMES"}));
        CHECK(!c.ok && c.err.kind == ImportError::Locked && c.err.message == "/SUB/GAMES/ELITE.COM is read-only: use --force to replace it",
              "a read-only file without --force: " + c.err.message);
        Run e = runCli(v, {file("SUB", "x")}, rep);
        CHECK(!e.ok && e.err.kind == ImportError::Failed && e.err.message == "a directory called /SUB is in the way", "a file over a directory: " + e.err.message);
    }

    printf("A conflict callback: Replace, Skip, and the \"all\" answers remembered\n");
    {
        auto f = makeDfs(d);
        fs::Volume *v = f->v();
        CHECK(runCli(v, {file("A", "a0"), file("B", "b0"), file("C", "c0"), file("D", "d0")}, ImportOptions()).ok, "four files to clash with");
        Run a = runGui(v, {file("A", "a1"), file("B", "b1"), file("NEW", "n")}, {Conflict::Replace, Conflict::Skip});
        CHECK(a.ok && a.asked == 2 && a.r.replaced == 1 && a.r.skipped == 1 && a.r.added == 1 && readBack(v, fs::Path({"$.A"})) == "a1" &&
                  readBack(v, fs::Path({"$.B"})) == "b0", "Replace replaces, Skip skips, a new file is not asked about");
        CHECK(a.askedFor.size() == 2 && a.askedFor[0] == "A<-A", "the callback gets the target path and the item: " + a.askedFor.join(", "));
        Run b = runGui(v, {file("A", "a2"), file("B", "b2"), file("C", "c2"), file("D", "d2")}, {Conflict::Skip, Conflict::ReplaceAll});
        CHECK(b.ok && b.asked == 2 && b.r.skipped == 1 && b.r.replaced == 3 && readBack(v, fs::Path({"$.D"})) == "d2" &&
                  readBack(v, fs::Path({"$.A"})) == "a1", "Replace all: asked once, the rest replaced without asking");
        Run c = runGui(v, {file("A", "a3"), file("B", "b3"), file("C", "c3")}, {Conflict::SkipAll});
        CHECK(c.ok && c.asked == 1 && c.r.skipped == 3 && c.r.replaced == 0 && readBack(v, fs::Path({"$.C"})) == "c2",
              "Skip all: asked once, the rest skipped without asking");
        CHECK(c.r.changes.size() == 3 && c.r.changes[2].op == "skip" && c.r.changes[2].note == "exists", "skips logged as changes");
    }

    printf("A conflict callback: Cancel half way keeps what was done\n");
    {
        auto f = makeDfs(d);
        fs::Volume *v = f->v();
        CHECK(runCli(v, {file("B", "b0")}, ImportOptions()).ok, "a file to clash with");
        Run c;
        CHECK(f->disc->transact([&](QString *) {
                  c = runGui(v, {file("A", "a"), file("B", "b"), file("C", "c")}, {Conflict::Cancel});
                  return c.ok;
              }, &err), "Cancel: import() returns true, so the window's transaction keeps the work: " + err);
        CHECK(c.r.cancelled && c.r.added == 1 && c.r.errors.isEmpty() && readBack(v, fs::Path({"$.A"})) == "a" &&
                  readBack(v, fs::Path({"$.B"})) == "b0" && readBack(v, fs::Path({"$.C"})) == "<missing>", "A kept, B untouched, C never tried");
        Run d2 = runGui(v, {file("B", "b"), file("C", "c")}, {});
        CHECK(d2.r.cancelled && d2.asked == 1 && d2.r.added == 0, "a callback with no answer left cancels at once");
    }

    printf("A conflict callback: a locked file is replaced on a Replace answer\n");
    {
        auto f = makeDfs(d);
        fs::Volume *v = f->v();
        CHECK(runCli(v, {file("L1", "1", locked), file("L2", "2", locked)}, ImportOptions()).ok, "two locked files");
        Run a = runGui(v, {file("L1", "x"), file("L2", "y")}, {Conflict::Replace, Conflict::Skip});
        CHECK(a.ok && a.r.replaced == 1 && a.r.skipped == 1 && a.r.errors.isEmpty() && readBack(v, fs::Path({"$.L1"})) == "x" &&
                  !statOf(v, fs::Path({"$.L1"})).attrs["locked"].toBool() && statOf(v, fs::Path({"$.L1"})).attrs["load"].toUInt() == 0x1900u,
              "Replace: replaced and unlocked, the load address kept");
        CHECK(readBack(v, fs::Path({"$.L2"})) == "2" && statOf(v, fs::Path({"$.L2"})).attrs["locked"].toBool(), "Skip: the locked file untouched");
        Run b = runGui(v, {file("L2", "z", locked)}, {Conflict::ReplaceAll});
        CHECK(b.ok && b.r.replaced == 1 && readBack(v, fs::Path({"$.L2"})) == "z" && statOf(v, fs::Path({"$.L2"})).attrs["locked"].toBool(),
              "Replace all with a locked incoming file: replaced, locked as the item says");
    }

    printf("onError = Continue: errors recorded, the rest imported\n");
    {
        auto f = makeFat(d);
        fs::Volume *v = f->v();
        CHECK(v->mkdir(fs::Path(), "DOCS", &err) && runCli(v, {file("NOTE", "n")}, ImportOptions()).ok, "a directory DOCS and a file NOTE");
        const QVector<TransferItem> items = {file("A.TXT", "a"), file("DOCS", "in the way"), file("B.TXT", "b"),
                                             folder("NOTE", {file("X.TXT", "x")}), file("C.TXT", "c")};
        Run a = runGui(v, items, {});
        CHECK(a.ok && !a.r.cancelled && a.r.added == 3 && a.r.errors.size() == 2 && a.asked == 0, "two errors, three files added");
        CHECK(a.r.errors.size() == 2 && a.r.errors[0].kind == ImportError::Failed && a.r.errors[0].from == "DOCS" &&
                  a.r.errors[0].message == "a directory called /DOCS is in the way" && a.r.errors[1].from == "NOTE" &&
                  a.r.errors[1].message == "a file called /NOTE is in the way",
              "each error with its source and message: " + (a.r.errors.size() == 2 ? a.r.errors[0].message + " | " + a.r.errors[1].message : QString()));
        CHECK(readBack(v, fs::Path({"C.TXT"})) == "c" && readBack(v, fs::Path({"NOTE"})) == "n" && countFiles(v) == 5,
              "the files after the errors are there, nothing in the way was changed");
        auto g = makeFat(d);
        CHECK(g->v()->mkdir(fs::Path(), "DOCS", &err), "the same on a fresh disc, onError = Stop");
        Run b = runGui(g->v(), items, {}, {}, OnError::Stop);
        CHECK(!b.ok && b.r.added == 1 && b.r.errors.isEmpty() && b.err.kind == ImportError::Failed && b.err.from == "DOCS",
              "Stop: the first error ends the run and is the run's error");
    }

    printf("The directory hook: a folder on a volume without directories\n");
    {
        const QVector<TransferItem> items = {folder("F", {file("ONE", "1"), file("TWO", "2")}), file("TOP", "t")};
        auto f = makeDfs(d);
        Run a = runCli(f->v(), items, ImportOptions());
        CHECK(a.ok && a.r.added == 3 && a.r.flattened == 1 && readBack(f->v(), fs::Path({"$.ONE"})) == "1" &&
                  a.r.warnings.contains("DFS has no directories: the files of 'F' were put in $"), "no hook: flattened into $, with the warning");
        auto g = makeDfs(d);
        fs::Path seenTarget({"x"});
        QString seenName;
        Run b = runGui(g->v(), items, {}, [&](const TransferItem &dir, const fs::Path &target) {
            seenName = dir.name; seenTarget = target;
            DirChoice c; c.action = DirChoice::Into; c.prefix = dir.name + "."; return c;
        });
        CHECK(b.ok && b.r.added == 3 && b.r.flattened == 0 && b.r.warnings.isEmpty() && readBack(g->v(), fs::Path({"F.ONE"})) == "1" &&
                  readBack(g->v(), fs::Path({"F.TWO"})) == "2" && readBack(g->v(), fs::Path({"$.TOP"})) == "t",
              "Into with a prefix: F.ONE and F.TWO (an ADFS directory F as DFS letter F), TOP still in $");
        CHECK(seenName == "F" && seenTarget.isRoot(), "the hook gets the folder and where its files would go");
        auto h = makeDfs(d);
        Run c = runGui(h->v(), items, {}, [](const TransferItem &, const fs::Path &) { DirChoice c; c.action = DirChoice::Skip; return c; });
        CHECK(c.ok && c.r.added == 1 && countFiles(h->v()) == 1 && c.r.changes.size() == 2 && c.r.changes[0].op == "skip" &&
                  c.r.changes[0].note == "folder" && c.r.changes[0].from == "F", "Skip: nothing from the folder, a skip change for it");
        auto k = makeDfs(d);
        Run e = runGui(k->v(), items, {}, [](const TransferItem &, const fs::Path &) { return DirChoice(); });
        CHECK(e.ok && e.r.added == 3 && e.r.flattened == 1 && e.r.warnings.size() == 1, "Flatten from the hook: as with no hook");
        auto m = makeDfs(d);
        Run x = runGui(m->v(), {file("FIRST", "1"), folder("F", {file("ONE", "1")}), file("LAST", "l")}, {},
                       [](const TransferItem &, const fs::Path &) { DirChoice c; c.action = DirChoice::Cancel; return c; });
        CHECK(x.ok && x.r.cancelled && x.r.added == 1 && countFiles(m->v()) == 1, "Cancel from the hook: FIRST kept, the rest not tried");
        // a folder inside a folder: asked again; Flatten keeps the outer folder's place and prefix
        auto n = makeDfs(d);
        QStringList asked;
        Run y = runGui(n->v(), {folder("G", {file("A", "a"), folder("SUB", {file("B", "b")}), folder("H", {file("C", "c")})})}, {},
                       [&](const TransferItem &dir, const fs::Path &) {
                           asked << dir.name;
                           DirChoice c;
                           if (dir.name.size() == 1) { c.action = DirChoice::Into; c.prefix = dir.name + "."; }
                           return c;
                       });
        CHECK(y.ok && asked == QStringList({"G", "SUB", "H"}) && readBack(n->v(), fs::Path({"G.A"})) == "a" &&
                  readBack(n->v(), fs::Path({"G.B"})) == "b" && readBack(n->v(), fs::Path({"H.C"})) == "c",
              "nested folders asked in turn: SUB flattened into G's letter, H into its own: " + asked.join(" "));
    }

    printf("Clash names within one run\n");
    {
        auto f = makeFat(d);
        Run a = runCli(f->v(), {file("long name one.txt", "1"), file("long name two.txt", "2")}, ImportOptions());
        CHECK(a.ok && a.r.added == 2 && a.r.renamed.size() == 1 && a.r.renamed[0].first == "LONG_NAM.TXT" &&
                  a.r.renamed[0].second == "LONG_N~1.TXT" && readBack(f->v(), fs::Path({"LONG_N~1.TXT"})) == "2",
              "the second LONG_NAM.TXT made in the run is called LONG_N~1.TXT");
        CHECK(a.r.json()["renamed"].toArray().size() == 1 && a.r.json().keys() == QStringList({"added", "dirs", "renamed", "replaced", "skipped"}),
              "json(): the command line's keys only");
    }

    printf(failures ? "FAILED: %d\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
