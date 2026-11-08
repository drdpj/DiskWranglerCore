// Part of DiskWranglerCore - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
// clitest: the command-line layer (docs/cli.md) on a small FAT backend made here. The test runs itself again with
// "__cli ARGS" as the console program, so exit codes, stdout and stderr are the real ones. Covers: usage errors,
// --version, new, info/ls/check JSON (schema 1), put/get round trips, --dry-run, one transaction per run (a failing
// line leaves the image byte-identical), conflicts (--replace, --skip), the clash suffix, rm/mkdir/mv, scripts, copy,
// and SOURCE_DATE_EPOCH making two runs identical. QtCore only.
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <cstdio>
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

class TestImage : public cli::OpenedImage {
public:
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
        d.addMount({"DOS", std::make_shared<fat::FsVolume>(fv)});
        return true;
    }
    fs::Disc &disc() override { return d; }
    bool save(bool backup, QString *err) override { return d.commit([&] { device::Container::SaveOptions o; o.backup = backup; return o; }(), err); }
    QJsonObject app() const override { return {{"name", "clitest"}}; }
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

static Run dwcli(const QStringList &args, const QByteArray &in = QByteArray(), bool epoch = true) {
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (epoch) { env.insert("SOURCE_DATE_EPOCH", "946684800"); }
    env.insert("TZ", "UTC");
    p.setProcessEnvironment(env);
    p.setWorkingDirectory(g_cwd);
    p.start(g_self, QStringList{"__cli"} + args);
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

    printf(failures ? "\n%d FAILED\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
