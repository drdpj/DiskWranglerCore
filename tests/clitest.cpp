// Part of DiskWranglerCore - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
// clitest: the command-line layer (docs/cli.md) on a small FAT backend made here. The test runs itself again with
// "__cli ARGS" as the console program, so exit codes, stdout and stderr are the real ones. Covers: usage errors,
// --version, new, info/ls/check JSON (schema 1), put/get round trips, --dry-run, one transaction per run (a failing
// line leaves the image byte-identical), conflicts (--replace, --skip), the clash suffix, rm/mkdir/mv, scripts, copy,
// and SOURCE_DATE_EPOCH making two runs identical; and the text output (ls layout, info sections, read-only messages,
// exit 4 for a disc with errors). QtCore only.
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <algorithm>
#include <cstdio>
#include <unistd.h>
#include <diskwrangler/cli/cli.h>
#include <diskwrangler/device/linear.h>
#include <diskwrangler/fs/fat/fatprobe.h>
#include <diskwrangler/fs/fat/fatvolume.h>

using namespace dw;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", qPrintable(QString(msg))); ++failures; } else printf("  ok: %s\n", qPrintable(QString(msg))); } while (0)

static QByteArray slurp(const QString &p) { QFile f(p); f.open(QIODevice::ReadOnly); return f.readAll(); }
static void spit(const QString &p, const QByteArray &d) { QFile f(p); f.open(QIODevice::WriteOnly); f.write(d); }

// ------------------------------------------------------------------------------------------------ the test backend

static QByteArray blankPc720() {
    QByteArray img(737280, 0);
    const unsigned char bpb[] = {0xEB, 0x3C, 0x90, 'M', 'S', 'D', 'O', 'S', '5', '.', '0', 0x00, 0x02, 2, 1, 0, 2, 112, 0,
                                 0xA0, 0x05, 0xF9, 3, 0, 9, 0, 2, 0};
    img.replace(0, int(sizeof(bpb)), QByteArray(reinterpret_cast<const char *>(bpb), int(sizeof(bpb))));
    img[510] = char(0x55); img[511] = char(0xAA);
    for (int fat : {1, 4}) { img[fat * 512] = char(0xF9); img[fat * 512 + 1] = char(0xFF); img[fat * 512 + 2] = char(0xFF); }
    return img;
}

// A FAT volume that lists its entries in no particular order, in mixed case, with one size not exact, no dates, and
// shows its attribute letters in an order of its own: what a text listing has to sort, align and leave out.
class PlainSizeVolume;        // (below) as OddVolume, but a size known only to a record is shown as the plain number, as CP/M does
class OddVolume : public fat::FsVolume {
public:
    using fat::FsVolume::FsVolume;
    bool list(const fs::Path &dir, QVector<fs::Entry> *out, QString *err) const override {
        if (!fat::FsVolume::list(dir, out, err)) return false;
        std::reverse(out->begin(), out->end());
        for (fs::Entry &e : *out) {
            e.modified = e.created = e.accessed = QDateTime();
            if (e.name.startsWith('B')) e.name = e.name.toLower();
            if (e.name.startsWith('C')) e.sizeExact = false;
        }
        return true;
    }
    QStringList attrLetterOrder() const override { return {"archive", "system", "hidden", "readonly"}; }
};
class PlainSizeVolume : public OddVolume {
public:
    using OddVolume::OddVolume;
    bool marksInexactSizes() const override { return false; }
};

// The image's behaviour is chosen by the second suffix of its file name: x.odd.img, x.ro.img (the mount is read-only),
// x.whole.img (the whole image is, with a reason that says what to do), x.sdet.img / x.sasm.img / x.sfor.img (a
// double-sided disc whose side order was detected, assumed or forced) and x.cpm.img (the app() cpmdw gives for a
// layout that is not in the library).
class TestImage : public cli::OpenedImage {
public:
    QString mode;
    device::Container c;
    std::unique_ptr<device::Linear> lin;
    std::shared_ptr<fat::Volume> fv;
    fs::Disc d;
    bool open(const QString &path, QString *err) {
        if (!c.open(path, err)) return false;
        c.setRawPadding(0, 0);
        lin.reset(new device::Linear(&c));
        fv = std::make_shared<fat::Volume>();
        if (!fv->attach(lin.get(), 0, quint32(c.rawFileSize()), err)) return false;
        d.setContainer(&c);
        std::shared_ptr<fs::Volume> vol = mode == "recsize" ? std::make_shared<PlainSizeVolume>(fv) : mode == "odd" ? std::make_shared<OddVolume>(fv) : std::make_shared<fat::FsVolume>(fv);
        const bool ro = mode == "ro" || mode == "whole" || mode == "cpm";
        d.addMount({"DOS", vol, ro, mode == "ro" ? QString("reason of the volume") : ro ? readOnlyWhy() : QString()});
        return true;
    }
    fs::Disc &disc() override { return d; }
    bool save(bool backup, QString *err) override { return d.commit([&] { device::Container::SaveOptions o; o.backup = backup; return o; }(), err); }
    QString volumeNote(int) const override { return mode == "recsize" ? QString("detected - see info") : QString(); }
    QString readOnlyWhy() const override { return mode == "whole" || mode == "cpm" ? QString("the whole image says so; use --thing") : QString(); }
    QJsonObject app() const override {
        QJsonObject o{{"name", "clitest"}};
        if (mode == "sdet" || mode == "sasm" || mode == "sfor")
            o["sides"] = QJsonObject{{"doubleSided", true}, {"order", "interleaved"}, {"confirmed", mode == "sdet"}, {"forced", mode == "sfor"}};
        if (mode == "cpm") {
            o["format"] = QJsonObject{{"name", "detected"}, {"origin", "detected"}, {"summary", "26x128, 77 trk"},
                                      {"diskdef", "diskdef detected\n  seclen 128\nend"}};
            o["confidence"] = "low";
            o["safeToWrite"] = false;
            o["why"] = "The directory is empty.";
            o["fromLibrary"] = false;
        }
        return o;
    }
};

class TestBackend : public cli::Backend {
public:
    fs::OtherApp self() const override { return fs::OtherApp{"clitest", "CLI test", "", ""}; }
    cli::OpenOutcome open(const QString &path, const cli::OpenOptions &o) override {
        cli::OpenOutcome out;
        device::Container c;
        QString err;
        if (!c.open(path, &err)) { out.status = cli::OpenOutcome::Failed; out.message = err; return out; }
        c.setRawPadding(0, 0);
        out.container = cli::containerInfo(path, c);
        fs::ProbeContext ctx;
        ctx.path = path;
        ctx.suffix = path.section('.', -1).toLower();
        ctx.container = &c;
        fat::Probe fp;
        cli::resolveVerdict(fs::detect(ctx, {&fp}), o.fs, &out);
        if (out.status != cli::OpenOutcome::Ok) return out;
        std::unique_ptr<TestImage> img(new TestImage);
        img->mode = QFileInfo(path).completeBaseName().section('.', -1);
        if (!img->open(path, &err)) { out.status = cli::OpenOutcome::Failed; out.message = err; return out; }
        out.image = std::move(img);
        return out;
    }
    QJsonArray formats() const override { return QJsonArray{QJsonObject{{"id", "pc720"}, {"name", "PC 720K"}}}; }
    bool create(const QString &file, const cli::NewOptions &n, QString *err, int *exit) override {
        if (n.format != "pc720") { *err = "unknown format " + n.format; *exit = cli::ExitUsage; return false; }
        spit(file, blankPc720());
        return true;
    }
    std::shared_ptr<xfer::HostPolicy> hostPolicy(const cli::GetOptions &, QString *, int *) override { return std::make_shared<xfer::HostPolicy>(); }
    bool readHost(const QStringList &paths, const cli::PutOptions &, QVector<TransferItem> *items, QStringList *, QString *err, int *exit) override {
        return cli::readHostTree(paths, {}, items, err, exit);
    }
};

static int runConsole(int argc, char **argv) {
    TestBackend be;
    cli::AppInfo app;
    app.tool = "clitest";
    app.appName = "CLI test";
    app.appVersion = "0";
    app.backend = &be;
    return runCli(argc - 1, argv + 1, app);          // argv[1] was "__cli"
}

// ------------------------------------------------------------------------------------------------ the driver

struct Run { int exit = -1; QByteArray out, err; QJsonObject json; };

static QString g_self;
static QString g_cwd;

static QStringList g_prefix;             // a command that runs the program as someone else (see "read-only messages")

static Run dwcli(const QStringList &args, const QByteArray &in = QByteArray(), bool epoch = true) {
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (epoch) { env.insert("SOURCE_DATE_EPOCH", "946684800"); }
    env.insert("TZ", "UTC");
    p.setProcessEnvironment(env);
    p.setWorkingDirectory(g_cwd);
    if (g_prefix.isEmpty()) p.start(g_self, QStringList{"__cli"} + args);
    else p.start(g_prefix.first(), g_prefix.mid(1) + QStringList{g_self, "__cli"} + args);
    if (!in.isEmpty()) p.write(in);
    p.closeWriteChannel();
    p.waitForFinished(60000);
    Run r;
    r.exit = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -2;
    r.out = p.readAllStandardOutput();
    r.err = p.readAllStandardError();
    if (args.contains("--json")) r.json = QJsonDocument::fromJson(r.out).object();
    return r;
}

static QByteArray digest(const QString &p) { return QCryptographicHash::hash(slurp(p), QCryptographicHash::Sha256); }

int main(int argc, char **argv) {
    if (argc > 1 && QByteArray(argv[1]) == "__cli") {
        QCoreApplication app(argc, argv);
        return runConsole(argc, argv);
    }
    QCoreApplication app(argc, argv);
    g_self = QCoreApplication::applicationFilePath();
    QTemporaryDir tmp;
    g_cwd = tmp.path();
    const QString d = tmp.path();
    const QString img = d + "/a.img";

    printf("usage, version, new\n");
    {
        Run r = dwcli({"--version"});
        CHECK(r.exit == 0 && r.out.startsWith("clitest 0 (DiskWranglerCore 0.5.0)"), "--version: " + QString(r.out.trimmed()));
        r = dwcli({});
        CHECK(r.exit == 2, "no command: exit 2");
        r = dwcli({"frobnicate", img});
        CHECK(r.exit == 2 && r.err.contains("unknown command"), "unknown command: exit 2");
        r = dwcli({"--json", "ls"});
        CHECK(r.exit == 2 && r.json["ok"] == false && r.json["error"].toObject()["kind"] == "usage", "usage error in JSON");
        r = dwcli({"ls", d + "/nothing.img"});
        CHECK(r.exit == 3, "missing image: exit 3");
        r = dwcli({"new", "--list", "--json"});
        CHECK(r.exit == 0 && r.json["result"].toObject()["formats"].toArray().size() == 1, "new --list");
        r = dwcli({"new", img, "--format=pc720", "--json"});
        CHECK(r.exit == 0 && r.json["ok"] == true && r.json["saved"] == true && QFileInfo::exists(img), "new makes the image: " + QString(r.err));
        r = dwcli({"new", img, "--format=pc720"});
        CHECK(r.exit == 5, "new over a file: exit 5 (conflict)");
        r = dwcli({"new", img, "--format=pc720", "--replace"});
        CHECK(r.exit == 0, "new --replace");
        const QString dry = d + "/dry.img";
        r = dwcli({"--dry-run", "new", dry, "--format=pc720"});
        CHECK(r.exit == 0 && !QFileInfo::exists(dry), "new --dry-run writes nothing");
    }

    printf("info, ls, check: the JSON envelope\n");
    {
        Run r = dwcli({"--json", "info", img});
        const QJsonObject o = r.json;
        CHECK(r.exit == 0 && o["schema"] == 1 && o["tool"] == "clitest" && o["command"] == "info" && o["ok"] == true && o["exit"] == 0 &&
                  o["error"].isNull() && o["dryRun"] == false && o["saved"] == false && o["image"] == img, "envelope fields: " + QString(r.out.left(300)));
        CHECK(!o["result"].toObject()["volumes"].toArray().isEmpty(), "info lists the volumes");
        r = dwcli({"--json", "ls", img});
        CHECK(r.exit == 0 && r.json["result"].toObject()["entries"].toArray().isEmpty(), "an empty disc lists nothing");
        r = dwcli({"--json", "check", img});
        CHECK(r.exit == 0 && r.json["ok"] == true, "check passes");
    }

    printf("put, get, round trips\n");
    {
        QDir().mkpath(d + "/h");
        spit(d + "/h/hello.txt", "hello\r\n");
        spit(d + "/h/big.bin", QByteArray(5000, 'b'));
        QDir().mkpath(d + "/h/tree/sub");
        spit(d + "/h/tree/sub/deep.dat", "deep");
        Run r = dwcli({"--json", "put", img, d + "/h/hello.txt", d + "/h/big.bin", d + "/h/tree", "/"});
        CHECK(r.exit == 0 && r.json["saved"] == true && !r.json["changes"].toArray().isEmpty(), "put files and a folder: " + QString(r.err));
        r = dwcli({"--json", "ls", img, "-R"});
        CHECK(r.exit == 0 && r.out.contains("HELLO.TXT") && r.out.contains("DEEP.DAT"), "ls -R shows them");
        QDir().mkpath(d + "/out");
        r = dwcli({"get", img, "HELLO.TXT", "BIG.BIN", "TREE", d + "/out"});
        CHECK(r.exit == 0 && slurp(d + "/out/HELLO.TXT") == "hello\r\n" && slurp(d + "/out/BIG.BIN") == QByteArray(5000, 'b') &&
                  slurp(d + "/out/TREE/SUB/DEEP.DAT") == "deep", "get round trip is byte-identical: " + QString(r.err));
        r = dwcli({"get", img, "HELLO.TXT", "-"});
        CHECK(r.exit == 0 && r.out == "hello\r\n", "get to - writes the bytes to stdout");
        r = dwcli({"get", img, "NOPE.TXT", d + "/out"});
        CHECK(r.exit == 3, "get of a missing file: exit 3");
    }

    printf("one transaction, one save\n");
    {
        const QByteArray before = digest(img);
        Run r = dwcli({"--dry-run", "put", img, d + "/h/hello.txt", "/"});
        CHECK(digest(img) == before, "--dry-run leaves the image byte-identical (put onto an existing name: " + QString::number(r.exit) + ")");
        r = dwcli({"--dry-run", "rm", img, "BIG.BIN"});
        CHECK(r.exit == 0 && digest(img) == before && r.json.isEmpty(), "--dry-run rm changes nothing");
        r = dwcli({"rm", img, "BIG.BIN", "MISSING.TXT"});
        CHECK(r.exit == 3 && digest(img) == before, "a missing second file abandons the run: the first rm isn't saved");
        r = dwcli({"put", img, d + "/h/hello.txt", "/"});
        CHECK(r.exit == 5 && digest(img) == before, "put over an existing file: exit 5, nothing written");
        r = dwcli({"put", img, d + "/h/hello.txt", "/", "--skip"});
        CHECK(r.exit == 0 && digest(img) == before, "--skip leaves the existing file and saves nothing new");
        spit(d + "/h/hello.txt", "hello again\r\n");
        r = dwcli({"put", img, d + "/h/hello.txt", "/", "--replace"});
        CHECK(r.exit == 0 && digest(img) != before, "--replace");
        r = dwcli({"get", img, "HELLO.TXT", "-"});
        CHECK(r.out == "hello again\r\n", "the new contents");
    }

    printf("mkdir, mv, rm, script\n");
    {
        Run r = dwcli({"mkdir", img, "A/B/C", "-p"});
        CHECK(r.exit == 0, "mkdir -p: " + QString(r.err));
        r = dwcli({"mv", img, "HELLO.TXT", "A/HI.TXT"});
        CHECK(r.exit == 0 || r.exit == 6, "mv (FAT has no moves between directories: exit 6 is the answer there): " + QString::number(r.exit));
        r = dwcli({"mv", img, "A", "Z"});
        CHECK(r.exit == 0, "mv renames a directory: " + QString(r.err));
        r = dwcli({"rm", img, "Z"});
        CHECK(r.exit == 6, "rm of a directory needs -r (exit 6): " + QString::number(r.exit));
        r = dwcli({"rm", img, "Z", "-r"});
        CHECK(r.exit == 0, "rm -r");
        const QByteArray before = digest(img);
        const QByteArray bad = "mkdir NEW1\nrm NOPE.TXT\nmkdir NEW2\n";
        r = dwcli({"--json", "script", img, "-"}, bad);
        CHECK(r.exit == 3 && digest(img) == before, "a failing script line abandons the whole run, image untouched (exit " + QString::number(r.exit) + ")");
        CHECK(r.json["error"].toObject()["message"].toString().contains("script line 2"), "and the message names the line");
        const QByteArray good = "mkdir NEW1\n# comment\n\nmkdir NEW2\n";
        r = dwcli({"--json", "script", img, "-"}, good);
        CHECK(r.exit == 0 && r.json["saved"] == true && r.json["result"].toObject()["lines"].toArray().size() == 2, "a good script is one save");
        r = dwcli({"script", img, "-"}, "new foo\n");
        CHECK(r.exit == 2, "new can't be in a script");
    }

    printf("repeatable output: SOURCE_DATE_EPOCH\n");
    {
        const QString a = d + "/r1.img", b = d + "/r2.img";
        for (const QString &f : {a, b}) {
            dwcli({"new", f, "--format=pc720"});
            dwcli({"put", f, d + "/h/hello.txt", "/"});
        }
        CHECK(digest(a) == digest(b), "two runs on the same input give the same image");
    }

    printf("text output: the ls layout\n");
    {
        // three files of 5, 12 and 123 bytes, a directory with a file in it, and attributes in all combinations
        const QString t = d + "/txt.img";
        spit(d + "/h/a.txt", "12345");
        spit(d + "/h/b.txt", "123456789012");
        spit(d + "/h/c.txt", QByteArray(123, 'x'));
        spit(d + "/h/z.txt", "zz");
        dwcli({"new", t, "--format=pc720"});
        dwcli({"put", t, d + "/h/a.txt", d + "/h/b.txt", d + "/h/c.txt", "/"});
        Run r = dwcli({"ls", t});
        QStringList l = QString::fromUtf8(r.out).split('\n', Qt::SkipEmptyParts);
        const QJsonObject vi = dwcli({"--json", "info", t}).json["result"].toObject()["volumes"].toArray()[0].toObject()["info"].toObject();
        const qint64 total = qint64(vi["total"].toDouble()), freeB = qint64(vi["free"].toDouble());
        CHECK(r.exit == 0 && l.value(0) == QString("dos: DOS, %1 used, %2 free").arg(total - freeB).arg(freeB), "the header names the volume, its label, used and free: " + l.value(0));
        CHECK(l.size() == 4 && l[1] == "/A.TXT  A       5  2000-01-01 00:00", "no directory: no TYPE column (FAT sets the archive bit on new files): " + l.value(1));
        const QString plain = d + "/plain.img";
        QFile::copy(t, plain);
        dwcli({"attr", plain, "A.TXT", "B.TXT", "C.TXT", "--set", "archive=0"});
        r = dwcli({"ls", plain});
        l = QString::fromUtf8(r.out).split('\n', Qt::SkipEmptyParts);
        CHECK(l.size() == 4 && l[1] == "/A.TXT          5  2000-01-01 00:00" && l[3] == "/C.TXT        123  2000-01-01 00:00",
              "no directory and no attribute set: no TYPE column, but the ATTRIBUTES column stays (blank, as wide as the letters): " + l.value(1));

        dwcli({"mkdir", t, "DIR"});
        dwcli({"put", t, d + "/h/z.txt", "/DIR"});
        dwcli({"attr", t, "A.TXT", "--set", "readonly=1", "--set", "archive=1"});
        dwcli({"attr", t, "B.TXT", "--set", "hidden=1"});
        dwcli({"attr", t, "C.TXT", "--set", "system=1", "--set", "readonly=1", "--set", "hidden=1", "--set", "archive=1"});
        r = dwcli({"ls", t});
        l = QString::fromUtf8(r.out).split('\n', Qt::SkipEmptyParts);
        CHECK(l.size() == 5, "a header and four entries: " + QString::number(l.size()));
        CHECK(l.value(1) == "/A.TXT     RA      5  2000-01-01 00:00", "name, blank type, attributes in the volume's order (FAT: R H S A), size right-aligned, date: " + l.value(1));
        CHECK(l.value(2) == "/B.TXT     HA     12  2000-01-01 00:00", "hidden and archive: " + l.value(2));
        CHECK(l.value(3) == "/C.TXT     RHSA  123  2000-01-01 00:00", "all four letters: " + l.value(3));
        CHECK(l.value(4) == "/DIR    D             2000-01-01 00:00", "a directory has D, a blank size and no attributes set: " + l.value(4));
        r = dwcli({"ls", t, "-R"});
        l = QString::fromUtf8(r.out).split('\n', Qt::SkipEmptyParts);
        CHECK(l.size() == 6 && l.value(4).startsWith("/DIR  ") && l.value(5) == "/DIR/Z.TXT     A       2  2000-01-01 00:00",
              "-R lists a directory and then its contents, one header, names padded to the widest: " + l.value(5));
        r = dwcli({"ls", t, "-d", "DIR"});
        CHECK(r.exit == 0 && QString::fromUtf8(r.out) == "/DIR  D        2000-01-01 00:00\n", "-d: the node itself, no header, no SIZE column, the attribute column kept: " + QString(r.out));
        r = dwcli({"--quiet", "ls", t});
        CHECK(r.exit == 0 && r.out.isEmpty(), "-q: no text");
        r = dwcli({"--json", "ls", t});
        CHECK(r.exit == 0 && r.json["result"].toObject()["entries"].toArray().size() == 4 &&
                  r.json["result"].toObject()["entries"].toArray()[0].toObject()["attrs"].toObject()["readonly"] == true, "the JSON is as before");

        // volume order is not name order, case is ignored, a size that is only to a record has ~ (counted in the width),
        // no dates, the volume's own letter order
        const QString odd = d + "/x.odd.img";
        QFile::copy(t, odd);
        r = dwcli({"ls", odd});
        l = QString::fromUtf8(r.out).split('\n', Qt::SkipEmptyParts);
        CHECK(l.size() == 5, "odd: a header and four entries");
        CHECK(l.value(1) == "/A.TXT     AR       5", "odd: sorted by name, the letters in the order the volume asks for (A S H R), no DATE column: " + l.value(1));
        CHECK(l.value(2) == "/b.txt     AH      12", "odd: lower case sorts with upper case: " + l.value(2));
        CHECK(l.value(3) == "/C.TXT     ASHR  ~123", "odd: ~ marks a size known only to a record, and counts in the width: " + l.value(3));
        CHECK(l.value(4) == "/DIR    D", "odd: trailing spaces are trimmed: " + l.value(4));
        const QString plainSize = d + "/x.recsize.img";
        QFile::copy(t, plainSize);
        r = dwcli({"ls", plainSize});
        l = QString::fromUtf8(r.out).split('\n', Qt::SkipEmptyParts);
        CHECK(l.value(3) == "/C.TXT     ASHR  123", "a volume that says so shows a size known only to a record as the plain number: " + l.value(3));
        CHECK(l.value(0).startsWith("dos: DOS (detected - see info), "), "the header carries the volume's note: " + l.value(0));
        r = dwcli({"--json", "ls", odd});
        const QJsonArray en = r.json["result"].toObject()["entries"].toArray();
        CHECK(en.size() == 4 && en[0].toObject()["display"] == "/DIR" && en[3].toObject()["display"] == "/A.TXT",
              "odd: the JSON keeps the order the volume lists them in");
    }

    printf("text output: info\n");
    {
        const QString t = d + "/txt.img";
        Run r = dwcli({"info", t});
        const QStringList l = QString::fromUtf8(r.out).split('\n', Qt::SkipEmptyParts);
        const QJsonObject o = dwcli({"--json", "info", t}).json["result"].toObject();
        const QJsonObject vi = o["volumes"].toArray()[0].toObject()["info"].toObject();
        const qint64 total = qint64(vi["total"].toDouble()), freeB = qint64(vi["free"].toDouble());
        const QString tier = o["chosen"].toObject()["tier"].toString();
        CHECK(r.exit == 0 && l.size() == 5, "five lines: " + QString::number(l.size()));
        CHECK(l.value(0) == "container: raw, 737280 bytes", "the container: " + l.value(0));
        CHECK(l.value(1) == QString("detection: open: %1 (%2)").arg(o["chosen"].toObject()["description"].toString(), tier), "the detection: " + l.value(1));
        CHECK(l.value(2) == QString("reading: %1, safe to write").arg(tier), "the reading (no format, no sides line from a backend that has neither): " + l.value(2));
        CHECK(l.value(3) == "volume dos: DOS (fat)", "the volume: " + l.value(3));
        CHECK(l.value(4) == QString("  %1 used, %2 free, %3 bytes").arg(total - freeB).arg(freeB).arg(total), "its space: " + l.value(4));

        for (const QString &mode : {"sdet", "sasm", "sfor"}) {
            const QString m = d + "/s." + mode + ".img";
            QFile::copy(t, m);
            r = dwcli({"info", m});
            const QStringList x = QString::fromUtf8(r.out).split('\n', Qt::SkipEmptyParts);
            const QString how = mode == "sdet" ? "detected" : mode == "sasm" ? "assumed" : "forced";
            CHECK(r.exit == 0 && x.value(2) == "sides: interleaved, " + how && x.value(3).startsWith("reading: "), "sides line, " + how + ": " + x.value(2));
        }

        const QString c = d + "/c.cpm.img";
        QFile::copy(t, c);
        r = dwcli({"info", c});
        QStringList x = QString::fromUtf8(r.out).split('\n', Qt::SkipEmptyParts);
        CHECK(x.value(2) == "format: custom, not in the library" && x.value(3) == "  26x128, 77 trk", "format with its origin and summary indented: " + x.value(2));
        CHECK(x.value(4) == "reading: low, read-only" && x.value(5) == "  The directory is empty." && x.value(6) == "  the whole image says so; use --thing",
              "a read-only reading: the reason, then the instruction: " + x.value(4));
        CHECK(x.value(7) == "definition:" && x.value(8) == "  diskdef detected" && x.value(10) == "  end" && x.value(11) == "  To change this disc, save the definition to a file, give it a name," && x.value(12) == "  and pass --diskdefs=FILE --format=NAME with each command.",
              "a layout that is not in the library: its definition, then what to do: " + x.value(8));
        CHECK(x.value(13) == "volume dos: DOS (fat), read-only", "the volume says it is read-only (the reading has said why): " + x.value(13));
    }

    printf("text output: read-only messages, exit 4\n");
    {
        const QString t = d + "/txt.img";
        auto msg = [](const Run &r) { return r.json["error"].toObject()["message"].toString(); };
        const QString ro = d + "/v.ro.img", whole = d + "/w.whole.img";
        QFile::copy(t, ro);
        QFile::copy(t, whole);
        Run r = dwcli({"--json", "put", ro, d + "/h/z.txt", "/"});
        CHECK(r.exit == 6 && msg(r) == "read-only: dos: reason of the volume", "a volume the disc mounts read-only: its name and its reason: " + msg(r));
        r = dwcli({"ls", ro});
        CHECK(r.exit == 0 && QString::fromUtf8(r.out).startsWith("dos: DOS") && QString::fromUtf8(r.out).section('\n', 0, 0).endsWith(", read-only"), "ls: the header says read-only: " + QString(r.out).section('\n', 0, 0));
        r = dwcli({"--json", "put", whole, d + "/h/z.txt", "/"});
        CHECK(r.exit == 6 && msg(r) == "read-only: the whole image says so; use --thing", "the whole image: the reason alone, it says what to do: " + msg(r));
        r = dwcli({"--json", "rm", whole, "A.TXT"});
        CHECK(r.exit == 6 && msg(r) == "read-only: the whole image says so; use --thing", "every command that changes the disc says so: " + msg(r));
        r = dwcli({"--json", "get", whole, "A.TXT", d + "/out"});
        CHECK(r.exit == 0, "a read command works on a read-only disc");

        // an image file that can't be written: someone else's file when this runs as root, else a file with no w bit
        const QString un = d + "/u.img";
        QFile::copy(t, un);
        QFile f(un);
        if (geteuid() == 0) {
            QFile::setPermissions(d, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadOther | QFile::ExeOther);
            f.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadOther);
            g_prefix = QStringList{"setpriv", "--reuid=65534", "--regid=65534", "--clear-groups"};
        } else {
            f.setPermissions(QFile::ReadOwner);
        }
        r = dwcli({"--version"});
        if (r.exit == 0) {
            r = dwcli({"--json", "put", un, d + "/h/z.txt", "/"});
            CHECK(r.exit == 6 && msg(r) == "read-only: the image file can't be written", "the image file can't be written: " + msg(r));
            r = dwcli({"ls", un});
            CHECK(r.exit == 0 && QString::fromUtf8(r.out).section('\n', 0, 0).endsWith(", read-only"), "ls: the header says read-only: " + QString(r.out).section('\n', 0, 0));
            r = dwcli({"info", un});
            CHECK(r.exit == 0 && QString::fromUtf8(r.out).section('\n', 0, 0) == "container: raw, 737280 bytes, read-only", "info: the container is read-only: " + QString(r.out).section('\n', 0, 0));
        } else {
            printf("  skipped: the program can't be run as someone else here (%d)\n", r.exit);
        }
        g_prefix.clear();

        // a disc with errors: a file that has a size and no clusters
        const QString bad = d + "/bad.img";
        dwcli({"new", bad, "--format=pc720"});
        dwcli({"put", bad, d + "/h/a.txt", "/"});
        QByteArray raw = slurp(bad);
        const int rootDir = 7 * 512;                                 // 1 reserved sector, two FATs of 3 sectors
        const bool found = raw.mid(rootDir, 11) == "A       TXT";
        raw[rootDir + 26] = 0; raw[rootDir + 27] = 0;                // its first cluster
        spit(bad, raw);
        const QByteArray before = digest(bad);
        r = dwcli({"--json", "check", bad});
        CHECK(found && r.exit == 4, "check finds the error: exit " + QString::number(r.exit));
        r = dwcli({"--json", "put", bad, d + "/h/z.txt", "/"});
        CHECK(r.exit == 4 && r.json["error"].toObject()["kind"] == "check" && msg(r) == "dos has errors (see check), so nothing was changed" && digest(bad) == before,
              "a change to a disc with errors: exit 4 and nothing written: " + msg(r));
        r = dwcli({"ls", bad});
        CHECK(r.exit == 0 && !QString::fromUtf8(r.out).section('\n', 0, 0).endsWith(", read-only"), "a read command still works, and the disc isn't called read-only");
    }

    printf(failures ? "\n%d FAILED\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
