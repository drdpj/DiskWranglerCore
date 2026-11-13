// Part of DiskWranglerCore - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
// xfertest: the transfer layer without any program on top: host file names, writing items to a host folder with a
// retro-files document, reading such a document back (and refusing unsafe ones), a program's HostPolicy
// (folders, planned names, content transform, sidecar files), and the drag temporary folders. QtCore only.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdio>
#ifdef Q_OS_UNIX
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif
#include <QElapsedTimer>
#include <diskwrangler/xfer/hostname.h>
#include <diskwrangler/xfer/hostread.h>
#include <diskwrangler/xfer/mime.h>
#include <diskwrangler/xfer/retrofiles.h>

using namespace dw;
using namespace dw::xfer;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", msg); ++failures; } else printf("  ok: %s\n", msg); } while (0)

static QByteArray slurp(const QString &p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<missing>"); }
static void spit(const QString &p, const QByteArray &d) { QDir().mkpath(QFileInfo(p).absolutePath()); QFile f(p); f.open(QIODevice::WriteOnly); f.write(d); }

static TransferItem file(const QString &name, const QByteArray &data, const QString &fam = QString(), const QJsonObject &a = QJsonObject()) {
    TransferItem t;
    t.name = name; t.data = data;
    if (!fam.isEmpty()) t.attrs[fam] = a;
    return t;
}

// A policy like CP/M's in miniature: user folders, planned names (lower case wins), trimmed content, error labels.
struct FolderPolicy : HostPolicy {
    QString topFolder(const TransferItem &it) const override { return QString("user%1").arg(it.attrs["cpm"]["user"].toInt()); }
    QByteArray content(const TransferItem &it) const override { return it.data.left(it.data.indexOf('\x1A') < 0 ? it.data.size() : it.data.indexOf('\x1A')); }
    QString errorLabel(const TransferItem &it) const override { return QString("%1:%2").arg(it.attrs["cpm"]["user"].toInt()).arg(it.name); }
    bool planNames(const QVector<TransferItem> &items, const QString &, QVector<QString> *names) const override {
        for (const TransferItem &i : items) names->append(i.name.toLower());
        return true;
    }
};

// A policy like Acorn's in miniature: a sidecar file for every file, and extensions kept apart for "fat" files.
struct SidecarPolicy : HostPolicy {
    bool keepExtension(const TransferItem &it) const override { return it.sourceFs == "fat"; }
    QString listedName(const TransferItem &it) const override { return it.sourceFs == "dfs" ? "$." + it.name : it.name; }
    void afterFile(const TransferItem &it, const QString &absDir, const QString &relDir, const QString &host, UsedNames &used,
                   QStringList *topLevel, QStringList *errors) const override {
        QString inf = used.unique(host + ".inf", false);
        QFile f(absDir + "/" + inf);
        if (!f.open(QIODevice::WriteOnly) || f.write(it.name.toLatin1() + "\n") < 0) errors->append(inf + ": " + f.errorString());
        else if (topLevel) topLevel->append(relDir.isEmpty() ? inf : relDir + "/" + inf);
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;

    printf("== host names\n");
    CHECK(hostSafeName("a/b\\c:d*e?f\"g<h>i|j") == "a_b_c_d_e_f_g_h_i_j", "characters no host allows become _");
    CHECK(hostSafeName("Aux") == "Aux_" && hostSafeName("aux.txt") == "aux_.txt" && hostSafeName("COM3") == "COM3_", "device names get a _");
    CHECK(hostSafeName("Auxiliary") == "Auxiliary", "a longer name that only starts like a device is left alone");
    CHECK(hostSafeName("") == "_" && hostSafeName(".") == "__" && hostSafeName("..") == "_._", "empty, . and ..");
    CHECK(hostSafeName("NAME. ") == "NAME._" && hostSafeName("NAME..") == "NAME._" && hostSafeName("a.") == "a_", "a trailing dot or space (Windows drops them) becomes _");
    CHECK(hostSafeName(QString("a\x01") + "b") == "a_b", "control characters");

    printf("== unique names\n");
    { UsedNames u; CHECK(u.unique("A.BAT", true) == "A.BAT" && u.unique("a.bat", true) == "a_2.bat" && u.unique("A.BAT", true) == "A_3.BAT", "clashes ignore case; the extension stays last");
      CHECK(u.unique("noext", true) == "noext" && u.unique("NOEXT", true) == "NOEXT_2", "no extension");
      CHECK(u.unique("x.y", false) == "x.y" && u.unique("X.Y", false) == "X.Y_2", "extension not kept apart");
      UsedNames s(true); CHECK(s.unique("a", false) == "a" && s.unique("A", false) == "A" && s.unique("a", false) == "a_2", "a case-sensitive host keeps both"); }

    printf("== write, then read back\n");
    QString out = tmp.path() + "/out";
    {
        QVector<TransferItem> items;
        QJsonObject acorn; acorn["load"] = "FFFF1900"; acorn["exec"] = "FFFF8023"; acorn["access"] = "LR";
        TransferItem a = file("Elite", QByteArray(3000, 'E'), "acorn", acorn);
        a.when = QDateTime(QDate(1987, 3, 4), QTime(5, 6, 7));
        TransferItem dir; dir.name = "Games"; dir.isDir = true; dir.attrs["acorn"] = QJsonObject{{"access", "R"}};
        dir.kids << file("ReadMe", "hi\r");
        dir.kids << file("readme", "again");
        items << a << dir << file("elite", "second") << file("Aux", "dev");
        QStringList top, errors; QJsonArray j;
        writeHost(items, out, HostPolicy(), &top, &j, &errors);
        CHECK(errors.isEmpty(), "no errors");
        CHECK(top == QStringList({"Elite", "Games", "elite_2", "Aux_"}), qPrintable("top-level paths: " + top.join(",")));
        CHECK(slurp(out + "/Elite").size() == 3000 && slurp(out + "/Games/ReadMe") == "hi\r" && slurp(out + "/Games/readme_2") == "again", "files and a folder, a clash inside it made unique");
        CHECK(QFileInfo(out + "/Elite").lastModified() == QDateTime(QDate(1987, 3, 4), QTime(5, 6, 7)), "the file date is set");
        CHECK(j.size() == 6, "one JSON item per file and folder");
        QByteArray doc = makeJson(j, out, "test.img", "adfs", "Test");
        QJsonObject o = QJsonDocument::fromJson(doc).object();
        CHECK(o["format"] == "retro-files" && o["version"] == 1 && o["generator"] == "Test" && o["source"].toObject()["filesystem"] == "adfs" && o["source"].toObject()["image"] == "test.img", "document header");
        CHECK(o["items"].toArray()[0].toObject()["attributes"].toObject()["acorn"].toObject()["access"] == "LR", "attributes by family");
        CHECK(!o["items"].toArray()[3].toObject().contains("attributes"), "an item without attributes has none listed");
        QVector<TransferItem> back; QString err;
        CHECK(readJson(doc, &back, &err), qPrintable("reads it back: " + err));
        CHECK(back.size() == 4 && back[0].name == "Elite" && back[0].data == items[0].data && back[0].sourceFs == "adfs" && back[0].when == items[0].when, "top-level items, data, date, filesystem");
        CHECK(back[0].attrs["acorn"]["load"] == "FFFF1900" && back[1].isDir && back[1].kids.size() == 2 && back[1].kids[1].data == "again", "attributes and the folder's kids");
        CHECK(back[2].name == "elite" && back[2].data == "second", "listed names, not host names");
    }

    printf("== a program's policy\n");
    {
        QVector<TransferItem> items;
        QJsonObject u0{{"user", 0}}, u3{{"user", 3}};
        items << file("README.TXT", QByteArray("text\x1A\x1A\x1A", 7), "cpm", u0) << file("readme.txt", "lower", "cpm", u3) << file("PROG.COM", "bin", "cpm", u3);
        QStringList top, errors; QJsonArray j;
        writeHost(items, tmp.path() + "/folders", FolderPolicy(), &top, &j, &errors);
        CHECK(errors.isEmpty() && top == QStringList({"user0", "user3"}), qPrintable("top folders only: " + top.join(",")));
        CHECK(slurp(tmp.path() + "/folders/user0/readme.txt") == "text" && slurp(tmp.path() + "/folders/user3/prog.com") == "bin", "planned names, content transform");
        CHECK(j.size() == 5 && j[0].toObject()["type"] == "directory" && j[1].toObject()["path"] == "user0/readme.txt" && j[1].toObject()["size"] == 4, "folder items, then files in them with their written size");
        QVector<TransferItem> items2 = {file("A.COM", "x", "cpm", u0)};
        spit(tmp.path() + "/blocked2/user0", "a file where the folder should go");
        top.clear(); errors.clear(); j = QJsonArray();
        writeHost(items2, tmp.path() + "/blocked2", FolderPolicy(), &top, &j, &errors);
        CHECK(errors.size() == 1 && errors[0] == "cannot create folder user0" && top.isEmpty() && j.isEmpty(), qPrintable("a folder that can't be made: " + errors.join(";")));

        QVector<TransferItem> side;
        TransferItem d1 = file("BOOT", "*RUN\r"); d1.name = "BOOT"; d1.sourceFs = "dfs";
        TransferItem f1 = file("AUTOEXEC.BAT", "x"); f1.sourceFs = "fat";
        TransferItem f2 = file("autoexec.bat", "y"); f2.sourceFs = "fat";
        TransferItem sub; sub.name = "Sub"; sub.isDir = true; sub.sourceFs = "fat"; sub.kids << file("in.txt", "z");
        side << d1 << f1 << f2 << sub;
        top.clear(); errors.clear(); j = QJsonArray();
        writeHost(side, tmp.path() + "/side", SidecarPolicy(), &top, &j, &errors);
        CHECK(errors.isEmpty(), "no errors with sidecars");
        CHECK(top == QStringList({"BOOT", "BOOT.inf", "AUTOEXEC.BAT", "AUTOEXEC.BAT.inf", "autoexec_2.bat", "autoexec_2.bat.inf", "Sub"}), qPrintable("sidecars are listed and kept unique: " + top.join(",")));
        CHECK(j[0].toObject()["name"] == "$.BOOT" && slurp(tmp.path() + "/side/Sub/in.txt") == "z" && QFileInfo::exists(tmp.path() + "/side/Sub/in.txt.inf"), "listed names; a sidecar inside a folder");
    }

    printf("== refusing unsafe or damaged lists\n");
    {
        QString dir = tmp.path() + "/drop"; spit(dir + "/f.txt", "12345"); spit(tmp.path() + "/secret.txt", "secret");
        QJsonObject item{{"path", "f.txt"}, {"type", "file"}, {"name", "F"}, {"size", 5}};
        auto doc = [&](QJsonArray items, int version = 1, QString format = "retro-files") {
            return QJsonDocument(QJsonObject{{"format", format}, {"version", version}, {"base", QUrl::fromLocalFile(dir).toString()}, {"items", items}}).toJson();
        };
        QVector<TransferItem> x; QString err;
        CHECK(readJson(doc({item}), &x, &err) && x.size() == 1 && x[0].name == "F" && x[0].data == "12345", "a good list");
        CHECK(!readJson("not json", &x, &err), "not JSON");
        CHECK(!readJson(doc({item}, 1, "other"), &x, &err), "another format");
        CHECK(!readJson(doc({item}, 2), &x, &err) && err.contains("version 2"), "another version");
        QJsonObject big = item; big["size"] = 99;
        CHECK(!readJson(doc({big}), &x, &err) && err.contains("size"), "a size that isn't the file's");
        for (const QString &p : {QString("../secret.txt"), QString("/etc/passwd"), QString("a/../../secret.txt"), QString("./f.txt"), QString("a//b"), QString("")}) {
            QJsonObject bad = item; bad["path"] = p; bad.remove("size");
            CHECK(!readJson(doc({bad}), &x, &err), qPrintable("refuses the path '" + p + "'"));
        }
        QJsonObject missing = item; missing["path"] = "nothere.txt";
        CHECK(!readJson(doc({missing}), &x, &err), "a missing file");
        CHECK(!readJson(doc({}), &x, &err) && err.contains("no files"), "no items");
        QFile::link(tmp.path() + "/secret.txt", dir + "/link.txt");
        QJsonObject lnk = item; lnk["path"] = "link.txt"; lnk.remove("size");
        CHECK(!readJson(doc({lnk}), &x, &err), "a link that leaves the folder");
        QJsonObject orphan{{"path", "nosuchdir/g.txt"}, {"type", "file"}}; spit(dir + "/nosuchdir/g.txt", "g");
        CHECK(readJson(doc({orphan}), &x, &err) && x.size() == 1 && x[0].name == "g.txt", "an item whose parent isn't listed becomes top-level, named by its path when it has no name");
    }

    printf("== drag temporary folders\n");
    {
        QString prefix = tmp.path() + "/dw-drag-", err;
        QString a = newDragDir(prefix, &err);
        CHECK(!a.isEmpty() && a.startsWith(prefix) && QFileInfo(a).isDir(), "a folder named from the prefix");
        QString old = prefix + "oldone"; QDir().mkpath(old);
        CHECK(cleanStale(prefix, 24 * 3600) == 0 && QFileInfo::exists(old), "recent folders stay");
        CHECK(cleanStale(prefix, -1) >= 2 && !QFileInfo::exists(old), "stale ones go");
        CHECK(newDragDir(tmp.path() + "/no/such/place/x-", &err).isEmpty() && err.contains("temporary folder"), "an error when it can't be made");
    }


    printf("== the host reader (step 5l: one reader for both windows and both console programs)\n");
    {
        const QString h = tmp.path() + "/host";
        auto spit = [](const QString &p, const QByteArray &b) { QDir().mkpath(QFileInfo(p).absolutePath()); QFile f(p); f.open(QIODevice::WriteOnly); f.write(b); };
        spit(h + "/b.txt", "b"); spit(h + "/A.txt", "a"); spit(h + "/c/in.txt", "in"); spit(h + "/B/x.txt", "x");
        spit(h + "/a.inf", "inf");
        QString deep = h + "/D";
        for (int i = 1; i <= 12; ++i) deep += QString("/d%1").arg(i);
        spit(deep + "/f.txt", "deep");
        QFile::link(h + "/b.txt", h + "/zlink.txt");               // links inside a folder: skipped
        QFile::link(h + "/c", h + "/Zlinkdir");
        QFile::link(h + "/c", tmp.path() + "/named-link");          // a path given: read as what it points to

        QVector<HostReadError> errors;
        QVector<TransferItem> it = readHost({h}, HostReadOptions(), &errors);
        CHECK(errors.isEmpty() && it.size() == 1 && it[0].isDir && it[0].name == "host" && it[0].hostPath == h, "a folder: one item named after it, its host path kept");
        QStringList names;
        for (const TransferItem &k : it.value(0).kids) names << k.name;
        CHECK(names == QStringList({"B", "c", "D", "a.inf", "A.txt", "b.txt"}), qPrintable("folders first, then files, names ignoring case: " + names.join(" ")));
        CHECK(!names.contains("zlink.txt") && !names.contains("Zlinkdir"), "linked files and folders inside a folder are skipped");
        const TransferItem *d = it.isEmpty() ? nullptr : &it[0];
        int depth = 0;
        for (const TransferItem *x = d; x;) {
            const TransferItem *next = nullptr;
            for (const TransferItem &k : x->kids) if (k.isDir && (k.name == "D" || k.name.startsWith('d'))) next = &k;
            if (!next) {
                CHECK(depth == 13 && x->kids.size() == 1 && x->kids[0].name == "f.txt" && x->kids[0].data == "deep",
                      "folders read to any depth (13 levels; CP/M's window stopped at 8)");
                break;
            }
            x = next; ++depth;
        }
        const TransferItem *A = nullptr;
        for (const TransferItem &k : it.value(0).kids) if (k.name == "A.txt") A = &k;
        CHECK(A && A->data == "a" && A->hostPath == QFileInfo(h + "/A.txt").absoluteFilePath() && A->when == QFileInfo(h + "/A.txt").lastModified(),
              "a file inside: its data, absolute host path and date");

        it = readHost({tmp.path() + "/named-link", h + "/b.txt", h + "/c/"}, HostReadOptions(), &errors);
        CHECK(errors.isEmpty() && it.size() == 3 && it[0].isDir && it[0].kids.size() == 1 && it[1].data == "b" && it[1].hostPath == h + "/b.txt" &&
              it[2].name == "c", "paths given: a link named is followed; a file's host path as given; \"dir/\" is named after the folder");

        // the per-file hook: before the size check and the read; may leave a file out or fill in its item
        QStringList seen;
        HostReadOptions o;
        o.fileHook = [&](const QString &p, bool named, TransferItem *t) {
            seen << QFileInfo(p).fileName() + (named ? "*" : "") + (t->data.isEmpty() ? "" : "+data");
            if (p.endsWith(".inf")) return false;
            if (t->name == "A.txt") { t->name = "RENAMED"; t->sourceFs = "adfs"; }
            return true;
        };
        it = readHost({h + "/A.txt", h + "/a.inf", h}, o, &errors);
        CHECK(it.size() == 2 && it[0].name == "RENAMED" && it[0].sourceFs == "adfs" && it[0].data == "a", "the hook fills in an item (and the file is read after it)");
        bool infIn = false;
        for (const TransferItem &k : it.value(1).kids) if (k.name.endsWith(".inf")) infIn = true;
        CHECK(!infIn && seen.contains("a.inf*") && seen.contains("a.inf") && seen.contains("A.txt*") && !seen.join(" ").contains("+data"),
              qPrintable("the hook leaves files out, named or inside, and is told which were named: " + seen.join(" ")));

        // the size limit, checked before the file is read: a sparse file of 6 GB
        const QString big = h + "/big/huge.bin";
        QDir().mkpath(h + "/big");
        { QFile f(big); f.open(QIODevice::WriteOnly); f.resize(qint64(6) * 1024 * 1024 * 1024); }
        spit(h + "/big/small.bin", "s");
        o = HostReadOptions();
        o.sizeLimit = 16 * 1024 * 1024;
        QElapsedTimer t;
        t.start();
        errors.clear();
        it = readHost({h + "/big", big}, o, &errors);
        CHECK(t.elapsed() < 5000 && errors.size() == 2 && errors[0].kind == HostReadError::TooLarge && errors[0].path == QFileInfo(big).absoluteFilePath() &&
              !errors[0].named && errors[1].named && errors[1].path == big && it.size() == 1 && it[0].kids.size() == 1 && it[0].kids[0].name == "small.bin",
              "a file over the limit is refused before it is read (6 GB sparse, at once), inside a folder and named; the rest is read");
        QFile::remove(big);

        // a missing path and a file that can't be read: listed, the rest read
        errors.clear();
        it = readHost({h + "/nothere.txt", h + "/b.txt"}, HostReadOptions(), &errors);
        CHECK(errors.size() == 1 && errors[0].kind == HostReadError::NotFound && errors[0].path == h + "/nothere.txt" && !errors[0].reason.isEmpty() &&
              it.size() == 1 && it[0].name == "b.txt", qPrintable("a missing path: not found, with the system's reason (" + errors.value(0).reason + "); the rest read"));
#ifdef Q_OS_UNIX
        {   // a socket can't be opened as a file, not even by root
            const QString sock = tmp.path() + "/s.sock";
            int fd = socket(AF_UNIX, SOCK_STREAM, 0);
            sockaddr_un a{};
            a.sun_family = AF_UNIX;
            qstrncpy(a.sun_path, QFile::encodeName(sock).constData(), sizeof(a.sun_path));
            const bool made = fd >= 0 && bind(fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0;
            errors.clear();
            it = readHost({sock, h + "/b.txt"}, HostReadOptions(), &errors);
            CHECK(made && errors.size() == 1 && errors[0].kind == HostReadError::CannotRead && errors[0].path == sock && !errors[0].reason.isEmpty() &&
                  it.size() == 1, qPrintable("a file that can't be read: listed with the system's reason (" + errors.value(0).reason + "); the rest read"));
            if (fd >= 0) ::close(fd);
            QFile::remove(sock);
            if (geteuid() != 0) {          // (root reads anything: only checked as another user)
                spit(h + "/locked/no.txt", "no");
                QFile::setPermissions(h + "/locked/no.txt", QFileDevice::Permissions());
                errors.clear();
                it = readHost({h + "/locked"}, HostReadOptions(), &errors);
                CHECK(errors.size() == 1 && errors[0].kind == HostReadError::CannotRead && it.size() == 1 && it[0].kids.isEmpty(),
                      "a file inside a folder that can't be read: listed, the folder still read");
                QFile::setPermissions(h + "/locked/no.txt", QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            }
        }
#endif
    }

    printf("== private MIME types\n");
    CHECK(QByteArray(DW_MIME_ITEMS) == "application/x-diskwrangler-items" && QByteArray(DW_MIME_SOURCE) == "application/x-diskwrangler-source" &&
          QByteArray(DW_MIME_CLIP) == "application/x-diskwrangler-clip" && QByteArray(RetroFilesMime) == "application/x-retro-files+json", "names");

    printf(failures ? "%d FAILURES\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
