// Part of DiskWranglerCore - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
// moduletest: the DFS and FAT modules on discs made here. DFS: the Volume adapter (paths with directory letters,
// attributes, rename, title and boot option, retro-files), a transaction that fails and is put back, the probe
// (single and double-sided, both side orders, Acorn Z80 CP/M's catalogue handed to CP/M Disk Wrangler), .inf files,
// addresses and directory letters. FAT: the adapter (directories, attributes, label), a failed transaction, the probe.
// QtCore only.
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>
#include <diskwrangler/device/container.h>
#include <diskwrangler/device/linear.h>
#include <diskwrangler/fs/dfs/dfsprobe.h>
#include <diskwrangler/fs/dfs/dfsvolume.h>
#include <diskwrangler/fs/dfs/inf.h>
#include <diskwrangler/fs/disc.h>
#include <diskwrangler/fs/fat/fatprobe.h>
#include <diskwrangler/fs/fat/fatvolume.h>

using namespace dw;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", qPrintable(QString(msg))); ++failures; } else printf("  ok: %s\n", qPrintable(QString(msg))); } while (0)

static void spit(const QString &p, const QByteArray &d) { QFile f(p); f.open(QIODevice::WriteOnly); f.write(d); }

// An empty DFS catalogue for a side of `sectors` sectors
static QByteArray catalogue(quint32 sectors, const char *title) {
    QByteArray c(512, 0);
    const QByteArray t = QByteArray(title).leftJustified(12, ' ');
    c.replace(0, 8, t.left(8));
    c.replace(256, 4, t.mid(8, 4));
    c[256 + 6] = char((sectors >> 8) & 3);
    c[256 + 7] = char(sectors & 0xFF);
    return c;
}

static fs::Verdict probeFile(const QString &path, int force = -1) {
    device::Container c;
    QString e;
    if (!c.open(path, &e)) return fs::Verdict();
    c.setRawPadding(0, 0);
    fs::ProbeContext ctx;
    ctx.path = path;
    ctx.suffix = path.section('.', -1).toLower();
    ctx.container = &c;
    ctx.forceSequential = force;
    dfs::Probe dp;
    fat::Probe fp;
    return fs::detect(ctx, {&fp, &dp});
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    const QString d = tmp.path();
    QString err;

    printf("DFS: the Volume adapter\n");
    {
        QByteArray img(400 * 256, 0);
        img.replace(0, 512, catalogue(400, "MODULE TEST"));
        spit(d + "/a.ssd", img);
        device::Container c;
        CHECK(c.open(d + "/a.ssd", &err), "open: " + err);
        device::Linear lin(&c);
        auto disc = std::make_shared<dfs::Disc>();
        dfs::Layout lay;
        lay.sectorsPerSide = 400;
        CHECK(disc->attach(&lin, quint64(img.size()), 0, lay, &err), "attach: " + err);
        auto vol = std::make_shared<dfs::Volume>(disc);
        fs::Disc fd(&c);
        fd.addMount({"drive 0", vol});
        CHECK(vol->fsId() == "dfs" && vol->caps().contiguousOnly && vol->caps().maxEntriesPerDir == 31 && !vol->caps().dirs, "caps");
        CHECK(vol->info().title == "MODULE TEST" && vol->info().total == 400 * 256 && vol->info().free == 398 * 256, "info");
        CHECK(vol->write(fs::Path(), "BOOT", "*CAT\r", {{"locked", true}}, QDateTime(), false, &err), "write $.BOOT: " + err);
        CHECK(vol->write(fs::Path(), "A.PROG", QByteArray(600, 'p'), {{"load", 0xFFFF1900u}, {"exec", 0x8023u}}, QDateTime(), false, &err),
              "write A.PROG (an I/O processor load address): " + err);
        QVector<fs::Entry> l;
        CHECK(vol->list(fs::Path(), &l, &err) && l.size() == 2, "two files listed");
        fs::Entry prog;
        CHECK(vol->stat(fs::Path({"A.PROG"}), &prog, &err) && prog.size == 600 && prog.attrs["load"].toUInt() == 0x31900u &&
                  !prog.attrs["locked"].toBool(), "A.PROG: 18-bit load address 31900");
        QByteArray data;
        CHECK(vol->read(fs::Path({"A.PROG"}), &data, &err) && data == QByteArray(600, 'p'), "read back");
        CHECK(!vol->read(fs::Path({"B.PROG"}), &data, &err), "another letter is another file");
        QMap<QString, QJsonObject> r = vol->toRetro(prog);
        CHECK(r["acorn"]["load"].toString() == "FFFF1900" && r["acorn"]["exec"].toString() == "00008023" && r["acorn"]["access"].toString() == "WR",
              "toRetro: 8 digits, access WR");
        fs::AttrValues back = vol->fromRetro(r, fs::NodeKind::File);
        CHECK(back["load"].toUInt() == 0x31900u && !back["locked"].toBool(), "fromRetro: back to 18 bits");
        CHECK(vol->fromRetro({{"fat", QJsonObject{{"readonly", true}}}}, fs::NodeKind::File)["locked"].toBool(), "fromRetro: a read-only FAT file is locked");
        CHECK(vol->setAttrs(fs::Path({"A.PROG"}), {{"locked", true}}, &err) && vol->stat(fs::Path({"A.PROG"}), &prog, &err) &&
                  prog.attrs["locked"].toBool() && prog.attrs["load"].toUInt() == 0x31900u, "setAttrs: only locked changes");
        CHECK(vol->rename(fs::Path({"A.PROG"}), fs::Path({"B.GAME"}), &err) && vol->stat(fs::Path({"B.GAME"}), &prog, &err), "rename to B.GAME: " + err);
        CHECK(vol->setVolumeAttrs({{"title", "RENAMED"}, {"boot", 3}}, &err) && vol->volumeValues()["title"] == "RENAMED" &&
                  vol->volumeValues()["boot"].toInt() == 3, "title and boot option");
        CHECK(vol->volumeAttrs()[1].choices.size() == 4 && vol->volumeAttrs()[1].choices[3] == dfs::bootOptionName(3), "boot option choices");
        CHECK(vol->names().fromHost("a.long name", fs::NodeKind::File) == "a.long_na" && vol->names().fromHost("plain", fs::NodeKind::File) == "plain",
              "names from the host keep a letter: " + vol->names().fromHost("a.long name", fs::NodeKind::File));
        QString why;
        CHECK(!vol->names().valid("TOOLONGNAME", fs::NodeKind::File, &why) && vol->names().valid("Z.OK", fs::NodeKind::File, &why), "name rules");
        CHECK(vol->check().ok(), "check");
        const device::Container::Snapshot before = c.snapshot();
        CHECK(!fd.transact([&](QString *e) {
                  return vol->write(fs::Path(), "SMALL", "x", {}, QDateTime(), false, e) &&
                         vol->write(fs::Path(), "HUGE", QByteArray(500 * 256, 'h'), {}, QDateTime(), false, e);
              }, &err), "a transaction whose second write doesn't fit fails: " + err);
        CHECK(c.snapshot() == before && vol->list(fs::Path(), &l, &err) && l.size() == 2, "and is put back: two files, SMALL gone");
        CHECK(vol->remove(fs::Path({"$.BOOT"}), &err) && vol->list(fs::Path(), &l, &err) && l.size() == 1, "remove $.BOOT");
        CHECK(fd.commit(device::Container::SaveOptions(), &err), "commit: " + err);
        device::Container c2;
        c2.open(d + "/a.ssd", &err);
        device::Linear l2(&c2);
        dfs::Disc d2;
        CHECK(d2.attach(&l2, quint64(img.size()), 0, lay, &err) && d2.files().size() == 1 && d2.title() == "RENAMED", "saved and read back");
    }

    printf("DFS: the probe\n");
    {
        QByteArray ssd(400 * 256, 0);
        ssd.replace(0, 512, catalogue(400, "SSD"));
        spit(d + "/p.ssd", ssd);
        fs::Verdict v = probeFile(d + "/p.ssd");
        CHECK(v.action == fs::Verdict::Open && v.bestCandidate()->fsId == "dfs" && v.bestCandidate()->tier == fs::Tier::Likely &&
                  v.bestCandidate()->description == "DFS single-sided 40-track (100K per side)", "SSD: " + (v.bestCandidate() ? v.bestCandidate()->description : QString()));
        QByteArray dsd(800 * 256, 0);                 // interleaved: side 1's catalogue at file sector 10
        dsd.replace(0, 512, catalogue(400, "SIDE0"));
        dsd.replace(10 * 256, 512, catalogue(400, "SIDE2"));
        spit(d + "/i.dsd", dsd);
        v = probeFile(d + "/i.dsd");
        dfs::Found f = std::any_cast<dfs::Found>(v.bestCandidate()->data);
        CHECK(f.layout.doubleSided && f.sides.doubleSided() && !f.sides.sequential && f.fullSize == 800 * 256 &&
                  v.bestCandidate()->description == "DFS double-sided 40-track (100K per side)", "DSD, interleaved");
        QByteArray seq(800 * 256, 0);                 // sequential: at sector 400
        seq.replace(0, 512, catalogue(400, "SIDE0"));
        seq.replace(400 * 256, 512, catalogue(400, "SIDE2"));
        spit(d + "/s.dsd", seq);
        v = probeFile(d + "/s.dsd");
        f = std::any_cast<dfs::Found>(v.bestCandidate()->data);
        CHECK(f.sides.sequential && v.bestCandidate()->description.endsWith(", sequential sides"), "DSD, sequential");
        v = probeFile(d + "/s.dsd", 0);
        CHECK(!std::any_cast<dfs::Found>(v.bestCandidate()->data).sides.sequential, "the side order forced");
        QByteArray trunc = ssd.left(20 * 256);
        spit(d + "/t.ssd", trunc);
        v = probeFile(d + "/t.ssd");
        f = std::any_cast<dfs::Found>(v.bestCandidate()->data);
        CHECK(f.fullSize == 400 * 256 && f.notes.size() == 1, "a truncated SSD is padded to its size");
        QByteArray cpm(800 * 256, char(0xE5));
        cpm.replace(0, 512, dfs::cpmFenceCatalogue(400));
        spit(d + "/z.dsd", cpm);
        v = probeFile(d + "/z.dsd");
        CHECK(v.action == fs::Verdict::Handoff && v.bestCandidate()->owner.id == "cpmdiskwrangler" && v.bestCandidate()->tier == fs::Tier::Certain,
              "Acorn Z80 CP/M: handed to CP/M Disk Wrangler");
        CHECK(dfs::isCpmFence(dfs::cpmFenceCatalogue(400)) && !dfs::isCpmFence(catalogue(400, "X")), "isCpmFence");
        quint32 sec = 0;
        CHECK(dfs::plausibleCatalogue(cpm.left(256), cpm.mid(256, 256), &sec) && sec == 400, "the CP/M catalogue is a plausible DFS one, 400 sectors");
        spit(d + "/junk.img", QByteArray(4096, 'j'));
        CHECK(probeFile(d + "/junk.img").action == fs::Verdict::None, "junk: nothing");
    }

    printf("DFS: .inf files, addresses, directory letters\n");
    {
        dfs::Inf i;
        CHECK(dfs::parseInfLine("$.ELITE   FF1900 FF8023 00004000 08", &i) && i.name == "ELITE" && i.dir == '$' &&
                  i.load == 0xFFFF1900u && i.exec == 0xFFFF8023u && i.hasAccess && i.access == dfs::InfL, "a DFS line (6-digit FFxxxx = FFFFxxxx)");
        CHECK(dfs::parseInfLine("\"My File\" 1900 8023 L", &i) && i.name == "My File" && i.dir.isNull() && i.access == dfs::InfL, "quoted, Locked");
        CHECK(dfs::parseInfLine("NAME 1900 8023", &i) && !i.hasAccess && i.access == (dfs::InfR | dfs::InfW), "no access: RW");
        spit(d + "/PROG", "x");
        spit(d + "/PROG.INF", "A.PROG 00001900 00008023 00000001 03\n");
        CHECK(dfs::infPathFor(d + "/PROG") == d + "/PROG.INF" && dfs::readInf(d + "/PROG", &i) && i.dir == 'A' && i.load == 0x1900, "readInf finds .INF");
        CHECK(dfs::infLineDfs('A', "PROG", 0xFFFF1900u, 0x8023, 600, true) == "A.PROG    FFFF1900 00008023 00000258 08\n", "DFS line written");
        CHECK(dfs::infLineAdfs("Elite", 0x1900, 0x8023, 600, 0x0B) == "Elite      00001900 00008023 00000258 0B\n", "ADFS line written");
        quint32 a = 0;
        CHECK(dfs::addrText(0x31900) == "FF1900" && dfs::addrText(0x11900) == "011900" && dfs::extendAddr(0x31900) == 0xFFFF1900u, "address text");
        CHECK(dfs::parseAddr("FFFF1900", &a) && a == 0x31900 && dfs::parseAddr("ff1900", &a) && a == 0x31900 && !dfs::parseAddr("123456", &a), "parseAddr");
        dfs::LetterPlace p = dfs::placeLetter('$', "BOOT");
        CHECK(p.how == dfs::LetterPlace::Target && p.name == "BOOT", "$ goes in the target directory");
        p = dfs::placeLetter('a', "PROG");
        CHECK(p.how == dfs::LetterPlace::Subdir && p.subdir == "A" && p.name == "PROG", "a letter: a subdirectory, upper case");
        p = dfs::placeLetter('^', "PROG");
        CHECK(p.how == dfs::LetterPlace::Flat && p.name == "X_PROG" && !p.warning.isEmpty(), "^: X_NAME with a warning");
        p = dfs::placeLetter('A', "PROG", true);
        CHECK(p.how == dfs::LetterPlace::Target && p.name == "PROG", "flattened");
        p = dfs::placeLetter('%', "PROG", false, [](QChar ch) { return ch != '%'; });
        CHECK(p.how == dfs::LetterPlace::Flat, "the target's own rule");
        CHECK(dfs::bcdInc(0x09) == 0x10 && dfs::bcdInc(0x99) == 0x00, "bcdInc");
    }

    printf("FAT: the Volume adapter and the probe\n");
    {
        QByteArray img(737280, 0);
        const unsigned char bpb[] = {0xEB, 0x3C, 0x90, 'M', 'S', 'D', 'O', 'S', '5', '.', '0', 0x00, 0x02, 2, 1, 0, 2, 112, 0,
                                     0xA0, 0x05, 0xF9, 3, 0, 9, 0, 2, 0};
        img.replace(0, int(sizeof(bpb)), QByteArray(reinterpret_cast<const char *>(bpb), int(sizeof(bpb))));
        img[510] = char(0x55); img[511] = char(0xAA);
        for (int fat : {1, 4}) { img[fat * 512] = char(0xF9); img[fat * 512 + 1] = char(0xFF); img[fat * 512 + 2] = char(0xFF); }
        spit(d + "/pc.img", img);
        fs::Verdict v = probeFile(d + "/pc.img");
        CHECK(v.action == fs::Verdict::Open && v.bestCandidate()->fsId == "fat" && v.bestCandidate()->description == "PC 720K FAT floppy",
              "probe: " + (v.bestCandidate() ? v.bestCandidate()->description : QString("nothing")));
        device::Container c;
        c.open(d + "/pc.img", &err);
        device::Linear lin(&c);
        auto fv = std::make_shared<fat::Volume>();
        CHECK(fv->attach(&lin, 0, 737280, &err), "attach: " + err);
        auto vol = std::make_shared<fat::FsVolume>(fv);
        fs::Disc fd(&c);
        fd.addMount({"DOS", vol});
        CHECK(vol->caps().dirs && vol->info().total > 700000 && vol->info().free == vol->info().total, "caps and info");
        CHECK(vol->mkdir(fs::Path(), "GAMES", &err) && vol->write(fs::Path({"GAMES"}), "ELITE.COM", "elite", {{"readonly", true}}, QDateTime(), false, &err),
              "mkdir and write in it: " + err);
        QVector<fs::Entry> l;
        CHECK(vol->list(fs::Path({"games"}), &l, &err) && l.size() == 1 && l[0].name == "ELITE.COM" && l[0].attrs["readonly"].toBool() &&
                  l[0].attrs["archive"].toBool() && l[0].modified.isValid(), "listed (no . and ..), read-only and archive, dated");
        CHECK(vol->list(fs::Path(), &l, &err) && l.size() == 1 && l[0].kind == fs::NodeKind::Dir, "the root holds the directory");
        QByteArray data;
        CHECK(vol->read(fs::Path({"GAMES", "ELITE.COM"}), &data, &err) && data == "elite", "read back");
        CHECK(vol->setAttrs(fs::Path({"GAMES", "ELITE.COM"}), {{"hidden", true}, {"archive", false}}, &err), "setAttrs");
        fs::Entry e;
        CHECK(vol->stat(fs::Path({"GAMES", "ELITE.COM"}), &e, &err) && e.attrs["hidden"].toBool() && e.attrs["readonly"].toBool() &&
                  !e.attrs["archive"].toBool(), "only the keys given change");
        CHECK(vol->toRetro(e)["fat"]["hidden"].toBool() && vol->toRetro(e)["fat"]["readonly"].toBool(), "toRetro: the fat family");
        CHECK(vol->rename(fs::Path({"GAMES", "ELITE.COM"}), fs::Path({"GAMES", "E.COM"}), &err) && vol->stat(fs::Path({"GAMES", "E.COM"}), &e, &err), "rename");
        CHECK(!vol->rename(fs::Path({"GAMES", "E.COM"}), fs::Path({"E.COM"}), &err), "no moves between directories");
        CHECK(vol->setVolumeAttrs({{"label", "beeb disk"}}, &err) && vol->volumeValues()["label"] == "BEEB DISK", "label");
        const device::Container::Snapshot before = c.snapshot();
        CHECK(!fd.transact([&](QString *x) {
                  return vol->write(fs::Path(), "A.TXT", "a", {}, QDateTime(), false, x) &&
                         vol->write(fs::Path({"NOWHERE"}), "B.TXT", "b", {}, QDateTime(), false, x);
              }, &err), "a transaction that fails half way: " + err);
        CHECK(c.snapshot() == before && vol->list(fs::Path(), &l, &err) && l.size() == 1 && vol->volumeValues()["label"] == "BEEB DISK" &&
                  vol->info().free < vol->info().total, "put back: A.TXT gone, the label and the FAT reloaded");
        CHECK(vol->check().ok(), "check: " + vol->check().errors.join("; "));
        CHECK(vol->remove(fs::Path({"GAMES"}), &err) && vol->list(fs::Path(), &l, &err) && l.isEmpty() && vol->info().free == vol->info().total,
              "removing the directory frees everything");
        CHECK(vol->names().fromHost("long file name.text", fs::NodeKind::File) == fat::Volume::fromHost("long file name.text"), "names from the host");
        // 800K Acorn DOS Plus: FAT at 0, media FD
        QByteArray a800(819200, 0);
        a800[0] = char(0xFD); a800[1] = char(0xFF); a800[2] = char(0xFF);
        spit(d + "/a800.img", a800);
        v = probeFile(d + "/a800.img");
        fat::Found f = std::any_cast<fat::Found>(v.bestCandidate()->data);
        CHECK(v.bestCandidate()->description == "Acorn DOS Plus 800K floppy" && f.acorn800 && f.fromTable && f.sides.doubleSided() &&
                  !f.sides.sequential && !f.sidesConfirmed, "Acorn 800K, blank: interleaved, not confirmed");
    }

    printf(failures ? "\n%d FAILED\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
