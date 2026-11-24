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
#include <diskwrangler/fs/presets.h>

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

// Step 6b: a DFS side's catalogue as kept in memory, as text (every field the next catalogue write uses).
static QString describe(const dfs::Disc &d) {
    QStringList o;
    o << QString("title '%1' boot %2 sectors %3 watford %4 max %5").arg(d.title()).arg(d.bootOption()).arg(d.sectors())
             .arg(d.watford()).arg(d.maxFiles());
    for (const dfs::File &f : d.files())
        o << QString("%1 %2 %3 %4 %5 %6 %7").arg(f.fullName()).arg(f.locked).arg(f.load, 0, 16).arg(f.exec, 0, 16).arg(f.length)
                 .arg(f.start).arg(f.cat);
    return o.join('\n');
}

// Is what a change left in memory exactly what reload() reads from the staging? The catalogue as listed, and the bytes
// the next catalogue write makes from it (a boot-option write: it writes the title, the cycle numbers, the size and
// every entry from memory). Both copies are written, compared and put back; d itself is not changed.
static bool dfsFaithful(const dfs::Disc &d, device::Container &c, device::Linear &lin, QString *why) {
    const device::Container::Snapshot snap = c.snapshot();
    dfs::Disc mem = d, fresh = d;
    QString e;
    if (!fresh.reload(&e)) { *why = "reload: " + e; return false; }
    if (describe(mem) != describe(fresh)) { *why = "in memory:\n" + describe(mem) + "\nreloaded:\n" + describe(fresh); return false; }
    QByteArray a, b;
    const bool wa = mem.setBootOption(mem.bootOption(), &e) && lin.read(0, 4 * 256, &a, &e);
    c.restore(snap);
    const bool wb = fresh.setBootOption(fresh.bootOption(), &e) && lin.read(0, 4 * 256, &b, &e);
    c.restore(snap);
    if (!wa || !wb || a != b) { *why = "the next catalogue write differs" + (e.isEmpty() ? QString() : ": " + e); return false; }
    return true;
}

// The same for a FAT volume: its FAT and label in memory against reload(), and the bytes the next FAT and label write
// makes (a label write writes the label entry and every copy of the FAT from memory).
static bool fatFaithful(const fat::Volume &v, device::Container &c, device::Linear &lin, quint32 bytes, QString *why) {
    const device::Container::Snapshot snap = c.snapshot();
    fat::Volume mem = v, fresh = v;
    QString e;
    if (!fresh.reload(&e)) { *why = "reload: " + e; return false; }
    if (mem.label() != fresh.label() || mem.freeClusters() != fresh.freeClusters()) {
        *why = QString("label '%1' / '%2', free %3 / %4").arg(mem.label(), fresh.label()).arg(mem.freeClusters()).arg(fresh.freeClusters());
        return false;
    }
    QByteArray a, b;
    const bool wa = mem.setLabel(mem.label(), &e) && lin.read(0, bytes, &a, &e);
    c.restore(snap);
    const bool wb = fresh.setLabel(fresh.label(), &e) && lin.read(0, bytes, &b, &e);
    c.restore(snap);
    if (!wa || !wb || a != b) { *why = "the next FAT and label write differs" + (e.isEmpty() ? QString() : ": " + e); return false; }
    return true;
}

static QByteArray pattern(int n, int seed) {
    QByteArray b(n, 0);
    for (int i = 0; i < n; ++i) b[i] = char((i * 31 + seed * 7) & 0xFF);
    return b;
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
        {
            fs::Entry be;
            CHECK(vol->stat(fs::Path({"boot"}), &be, &err) && be.name == "$.BOOT" && be.attrs["locked"].toBool() &&
                      !vol->stat(fs::Path({"B.BOOT"}), &be, &err), "stat: a name without a letter is in $ (BOOT is $.BOOT)");
        }
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
        {   // the command-line hooks: paths as people type them, and as they are shown
            fs::Path pp;
            QString pe;
            CHECK(vol->parsePath("A.PROG", &pp, &pe) && vol->displayPath(pp) == "A.PROG", "parsePath D.NAME, displayPath D.NAME");
            CHECK(vol->parsePath("BOOT", &pp, &pe) && vol->displayPath(pp) == "$.BOOT", "parsePath NAME is in $, shown as $.NAME");
            CHECK(vol->parsePath("/", &pp, &pe) && pp.parts.isEmpty() && vol->displayPath(pp) == "$" && vol->parsePath("$", &pp, &pe) && pp.parts.isEmpty(), "the root is /, $ or empty");
            CHECK(!vol->parsePath("a/b", &pp, &pe) && !pe.isEmpty(), "a slash is not a DFS path");
        }
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

    printf("DFS: Acorn Atom DOS discs (series atom)\n");
    {
        // as the Atom DOS writes them: the default directory a space, byte 4 of sector 1 not a cycle number, byte 6's
        // high nibble kept (testdata/ATOM's sample has 0x03 there; garbage is made up here to see it kept)
        QByteArray img(400 * 256, 0);
        img.replace(0, 512, catalogue(400, "ATOM TEST"));
        img[256 + 4] = 0x42;
        img[256 + 6] = char(0x50 | 0x01);
        img.replace(8, 8, QByteArray("BASIC  ") + ' ');           // entry 1: BASIC, directory space
        img.replace(16, 8, QByteArray("OLD    ") + char('$' | 0x80));   // entry 2: a real '$', locked
        img[256 + 5] = 16;
        const unsigned char e1[8] = {0x00, 0xC0, 0xB2, 0xC2, 0x00, 0x10, 0x00, 0x12};   // C000 C2B2 1000 at 18
        const unsigned char e2[8] = {0x00, 0x29, 0xB2, 0xC2, 0x00, 0x02, 0x00, 0x02};   // 2900 C2B2 200 at 2
        for (int i = 0; i < 8; ++i) { img[256 + 8 + i] = char(e1[i]); img[256 + 16 + i] = char(e2[i]); }
        spit(d + "/atom.ssd", img);
        fs::Verdict v = probeFile(d + "/atom.ssd");
        CHECK(v.action == fs::Verdict::Open && v.ranked.size() == 1 && v.bestCandidate()->fsId == "atom" &&
                  v.bestCandidate()->tier == fs::Tier::Likely &&
                  v.bestCandidate()->description == "Acorn Atom DOS single-sided 40-track (100K per side)" &&
                  std::any_cast<dfs::Found>(v.bestCandidate()->data).layout.atom,
              "a space directory: Atom DOS, the only reading: " + (v.bestCandidate() ? v.bestCandidate()->description : QString()));
        CHECK(dfs::atomCatalogue(img.left(256), img.mid(256, 256)) && !dfs::atomCatalogue(catalogue(400, "X").left(256), catalogue(400, "X").mid(256)),
              "atomCatalogue");
        device::Container c;
        CHECK(c.open(d + "/atom.ssd", &err), "open: " + err);
        device::Linear lin(&c);
        auto disc = std::make_shared<dfs::Disc>();
        dfs::Layout lay;
        lay.sectorsPerSide = 400;
        lay.atom = true;
        CHECK(disc->attach(&lin, quint64(img.size()), 0, lay, &err) && disc->atom() && disc->files().size() == 2, "attach: " + err);
        auto vol = std::make_shared<dfs::Volume>(disc);
        fs::Disc fd(&c);
        fd.addMount({"drive 0", vol});
        fs::Entry e;
        CHECK(vol->stat(fs::Path({"BASIC"}), &e, &err) && e.name == "$.BASIC" && e.attrs["load"].toUInt() == 0xC000u && e.size == 0x1000,
              "the space directory is $ here: " + err);
        CHECK(vol->volumeAttrs().size() == 1 && !vol->volumeValues().contains("boot") && !vol->setVolumeAttrs({{"boot", 3}}, &err),
              "no boot option");
        CHECK(vol->fileAttrs()[1].bits == 16, "16-bit addresses");
        CHECK(vol->write(fs::Path(), "NEW", QByteArray(300, 'n'), {{"load", 0xFFFF1900u}, {"exec", 0x12345u}}, QDateTime(), false, &err) &&
                  vol->write(fs::Path(), "A.QUAL", "q", {}, QDateTime(), false, &err), "write: " + err);
        CHECK(vol->stat(fs::Path({"NEW"}), &e, &err) && e.attrs["load"].toUInt() == 0x1900u && e.attrs["exec"].toUInt() == 0x2345u,
              "load and exec cut to 16 bits");
        QByteArray cat;
        lin.read(0, 512, &cat, &err);
        int newAt = -1, oldAt = -1, qualAt = -1;
        for (int i = 1; i <= quint8(cat[256 + 5]) / 8; ++i) {
            const QByteArray n = cat.mid(i * 8, 7);
            if (n == "NEW    ") newAt = i;
            if (n == "OLD    ") oldAt = i;
            if (n == "QUAL   ") qualAt = i;
        }
        CHECK(newAt > 0 && cat[newAt * 8 + 7] == ' ' && (quint8(cat[256 + newAt * 8 + 6]) & 0xF0) == 0 && quint8(cat[256 + newAt * 8 + 6]) <= 1,
              "a new file: directory space, entry byte 6 holds only the start sector's high bits (no load or exec bits)");
        CHECK(oldAt > 0 && quint8(cat[oldAt * 8 + 7]) == ('$' | 0x80) && qualAt > 0 && cat[qualAt * 8 + 7] == 'A', "a real $ kept, locked; A.QUAL");
        CHECK(quint8(cat[256 + 4]) == 0x42 && quint8(cat[256 + 6]) == 0x51 && quint8(cat[256 + 7]) == 0x90,
              "sector 1 byte 4 not counted on, byte 6's high nibble kept");
        CHECK(vol->setVolumeAttrs({{"title", "ATOM"}}, &err) && lin.read(0, 512, &cat, &err) && cat.left(8) == "ATOM    " &&
                  cat.mid(256, 5) == "     " && disc->title() == "ATOM", "*TITLE: 13 bytes of spaces, then the characters");
        {
            dfs::Disc fresh = *disc;
            CHECK(fresh.reload(&err) && describe(fresh) == describe(*disc), "reload is faithful");
        }
        CHECK(vol->check().ok() && vol->check().info.join(' ').startsWith("Atom DOS drive 0"), "check: " + vol->check().info.join(' '));
        // a DFS catalogue with no space directory: DFS first, Atom DOS possible
        QByteArray plain(400 * 256, 0);
        plain.replace(0, 512, catalogue(400, "PLAIN"));
        spit(d + "/plain.ssd", plain);
        v = probeFile(d + "/plain.ssd");
        CHECK(v.action == fs::Verdict::Open && v.ranked.size() == 2 && v.ranked[0].fsId == "dfs" && v.ranked[1].fsId == "atom" &&
                  v.ranked[1].tier == fs::Tier::Possible && std::any_cast<dfs::Found>(v.ranked[1].data).layout.atom &&
                  !std::any_cast<dfs::Found>(v.ranked[0].data).layout.atom, "an unmarked catalogue: DFS, then Atom DOS (possible)");
        // the presets
        fs::PresetRegistry reg;
        dfs::addPresets(reg);
        const fs::Preset *ap = reg.find("atom-ds40");
        CHECK(ap && ap->extra.value("atom").toBool() && ap->doubleSided && ap->bytes == 2 * 400 * 256, "preset atom-ds40");
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
        CHECK(p.how == dfs::LetterPlace::Subdir && p.subdir == "a" && p.name == "PROG", "a letter: a subdirectory, its case kept");
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
        {
            fs::Path pp;
            QString pe;
            CHECK(vol->parsePath("GAMES\\ELITE.COM", &pp, &pe) && pp.parts == QStringList({"GAMES", "ELITE.COM"}) && vol->displayPath(pp) == "/GAMES/ELITE.COM", "FAT: a backslash is a slash");
            CHECK(vol->parsePath("/", &pp, &pe) && pp.parts.isEmpty() && vol->displayPath(pp) == "/", "FAT: the root");
        }
        // 800K Acorn DOS Plus: FAT at 0, media FD
        QByteArray a800(819200, 0);
        a800[0] = char(0xFD); a800[1] = char(0xFF); a800[2] = char(0xFF);
        spit(d + "/a800.img", a800);
        v = probeFile(d + "/a800.img");
        fat::Found f = std::any_cast<fat::Found>(v.bestCandidate()->data);
        CHECK(v.bestCandidate()->description == "Acorn DOS Plus 800K floppy" && f.acorn800 && f.fromTable && f.sides.doubleSided() &&
                  !f.sides.sequential && !f.sidesConfirmed, "Acorn 800K, blank: interleaved, not confirmed");
    }

    // Step 6b: the DFS and FAT classes keep no snapshot of their own. A change that fails half way leaves what it did;
    // Disc::transact puts the staging back and has the volume reload, and reload() gives exactly what the changes left
    // in memory.
    for (const bool watford : {false, true}) {
        printf("Step 6b: a %s DFS disc: Disc::transact is the only rollback, and reload() is faithful\n", watford ? "Watford 62-file" : "31-file");
        QByteArray img(800 * 256, 0);
        img.replace(0, 512, catalogue(800, "STAGING"));
        if (watford) {
            QByteArray c2 = catalogue(800, "");
            for (int i = 0; i < 8; ++i) c2[i] = char(0xAA);
            img.replace(512, 512, c2);
        }
        const QString path = d + (watford ? "/w6b.ssd" : "/a6b.ssd");
        spit(path, img);
        device::Container c;
        CHECK(c.open(path, &err), "open: " + err);
        device::Linear lin(&c);
        auto disc = std::make_shared<dfs::Disc>();
        dfs::Layout lay;
        lay.sectorsPerSide = 800;
        CHECK(disc->attach(&lin, quint64(img.size()), 0, lay, &err) && disc->watford() == watford, "attach: " + err);
        auto vol = std::make_shared<dfs::Volume>(disc);
        fs::Disc fd(&c);
        fd.addMount({"drive 0", vol});
        QString why;
        const int many = watford ? 40 : 20;
        bool added = true;
        for (int i = 0; i < many && added; ++i)
            added = disc->addFile(i % 3 ? '$' : 'A', QString("F%1").arg(i), pattern(100 + 300 * (i % 4), i), 0x1900 + i, 0x8023, i % 5 == 0, false, &err);
        CHECK(added, QString("%1 files added: ").arg(many) + err);
        CHECK(dfsFaithful(*disc, c, lin, &why), "reload() after the adds: " + why);
        CHECK(disc->addFile('$', "EMPTY", QByteArray(), 0, 0, false, false, &err) && dfsFaithful(*disc, c, lin, &why), "an empty file: " + err + why);
        CHECK(disc->addFile('$', "F1", pattern(2000, 99), 0x3000, 0x3000, false, true, &err) && dfsFaithful(*disc, c, lin, &why),
              "a replace with a larger file: " + err + why);
        CHECK(disc->remove('$', "F2", &err) && disc->remove('A', "F3", &err) && dfsFaithful(*disc, c, lin, &why), "removes: " + err + why);
        CHECK(disc->modify('$', "F4", 'B', "MOVED", true, 0xFFFF1900, 0x8023, &err) && dfsFaithful(*disc, c, lin, &why),
              "a rename into another letter, locked: " + err + why);
        CHECK(disc->setTitle("NEW TITLE", &err) && disc->setBootOption(3, &err) && dfsFaithful(*disc, c, lin, &why), "title and boot option: " + err + why);
        if (watford) CHECK(disc->addFile('$', "LATE", pattern(700, 7), 0, 0, false, false, &err) && dfsFaithful(*disc, c, lin, &why),
                           "a file in the second catalogue's range: " + err + why);
        // a replace that doesn't fit: the old file is taken out of the catalogue in memory, then there is no room
        const device::Container::Snapshot snap = c.snapshot();
        const QString before = describe(*disc);
        const QByteArray f1 = pattern(2000, 99), huge(int(disc->freeSectors() + 40) * 256, 'h');
        CHECK(!fd.transact([&](QString *x) { return vol->write(fs::Path(), "$.F1", huge, {}, QDateTime(), true, x); }, &err),
              "a replace that doesn't fit fails: " + err);
        QByteArray back;
        CHECK(c.snapshot() == snap && describe(*disc) == before && vol->read(fs::Path({"$.F1"}), &back, &err) && back == f1,
              "the transaction put back the staging and the catalogue: $.F1 is there as it was");
        QString e2;
        CHECK(!disc->addFile('$', "F1", huge, 0, 0, false, true, &e2) && disc->indexOf('$', "F1") < 0,
              "without a transaction the catalogue in memory has lost $.F1 (" + e2 + ")");
        CHECK(disc->reload(&err) && describe(*disc) == before && c.snapshot() == snap, "reload() gives it back: " + err);
        // several changes, the last one failing (Delete and Properties make several in one transaction)
        CHECK(!fd.transact([&](QString *x) {
                  return vol->remove(fs::Path({"$.F5"}), x) && vol->write(fs::Path(), "$.NEW", "new", {}, QDateTime(), false, x) &&
                         vol->setVolumeAttrs({{"title", "CHANGED"}}, x) && vol->rename(fs::Path({"$.NOSUCH"}), fs::Path({"$.X"}), x);
              }, &err) && c.snapshot() == snap && describe(*disc) == before, "a remove, a write, a title, then a failure: all put back: " + err);
        CHECK(disc->check().ok() && fd.commit(device::Container::SaveOptions(), &err), "checked and saved: " + err);
        device::Container c2;
        c2.open(path, &err);
        device::Linear l2(&c2);
        dfs::Disc d2;
        CHECK(d2.attach(&l2, quint64(img.size()), 0, lay, &err) && describe(d2) == before, "read back as it was in memory");
    }

    // Step 6c (deliberate change 11): a DFS title's 12 bytes are kept as read, as DFS 2.45 does, and written back
    // unchanged by every catalogue write; only setTitle writes new ones (the characters, then zeros, as 2.45's *TITLE).
    // title() shows them up to the first NUL, trimmed. So reload() is faithful for a space-padded title too.
    for (const bool watford : {false, true}) {
        printf("Step 6c: a %s DFS disc whose title is padded with spaces: the title's bytes are kept\n", watford ? "Watford 62-file" : "31-file");
        QByteArray img(800 * 256, 0);
        const QByteArray padded(" PAD TITLE  ");             // a leading space, two trailing: 12 bytes, no NUL
        img.replace(0, 512, catalogue(800, ""));
        img.replace(0, 8, padded.left(8));
        img.replace(256, 4, padded.mid(8, 4));
        if (watford) {
            QByteArray c2 = catalogue(800, "");
            for (int i = 0; i < 8; ++i) c2[i] = char(0xAA);
            img.replace(512, 512, c2);
        }
        const QString path = d + (watford ? "/w6c.ssd" : "/a6c.ssd");
        spit(path, img);
        device::Container c;
        CHECK(c.open(path, &err), "open: " + err);
        device::Linear lin(&c);
        auto disc = std::make_shared<dfs::Disc>();
        dfs::Layout lay;
        lay.sectorsPerSide = 800;
        CHECK(disc->attach(&lin, quint64(img.size()), 0, lay, &err) && disc->watford() == watford, "attach: " + err);
        CHECK(disc->title() == "PAD TITLE" && disc->titleBytes() == padded, "shown trimmed ('" + disc->title() + "'), kept as read");
        auto vol = std::make_shared<dfs::Volume>(disc);
        fs::Disc fd(&c);
        fd.addMount({"drive 0", vol});
        CHECK(vol->volumeValues()["title"] == "PAD TITLE" && vol->info().title == "PAD TITLE", "the volume shows it trimmed");
        auto titleOnDisc = [&]() {
            QByteArray b;
            QString e;
            return lin.read(0, 512, &b, &e) ? b.left(8) + b.mid(256, 4) : QByteArray();
        };
        QString why;
        err.clear();
        CHECK(disc->addFile('$', "ONE", pattern(700, 1), 0x1900, 0x8023, false, false, &err) && titleOnDisc() == padded &&
                  dfsFaithful(*disc, c, lin, &why), "an add writes the title's bytes back unchanged: " + err + why);
        CHECK(disc->addFile('A', "TWO", pattern(300, 2), 0, 0, true, false, &err) && disc->addFile('$', "ONE", pattern(900, 3), 0, 0, false, true, &err) &&
                  titleOnDisc() == padded && dfsFaithful(*disc, c, lin, &why), "another add and a replace: " + err + why);
        CHECK(disc->modify('A', "TWO", 'B', "MOVED", false, 0x3000, 0x3000, &err) && titleOnDisc() == padded && dfsFaithful(*disc, c, lin, &why),
              "a rename and new attributes: " + err + why);
        CHECK(disc->setBootOption(2, &err) && titleOnDisc() == padded && dfsFaithful(*disc, c, lin, &why), "the boot option: " + err + why);
        CHECK(disc->remove('B', "MOVED", &err) && titleOnDisc() == padded && dfsFaithful(*disc, c, lin, &why), "a remove: " + err + why);
        // through the Volume, in a transaction (the window's way), then saved and read back
        CHECK(fd.transact([&](QString *x) { return vol->write(fs::Path(), "C.THREE", pattern(400, 4), {}, QDateTime(), false, x) &&
                                                   vol->setVolumeAttrs({{"boot", 3}}, x); }, &err) && titleOnDisc() == padded,
              "a write and the boot option through the volume: " + err);
        CHECK(fd.commit(device::Container::SaveOptions(), &err), "saved: " + err);
        {
            QFile f(path);
            f.open(QIODevice::ReadOnly);
            const QByteArray b = f.readAll();
            CHECK(b.left(8) + b.mid(256, 4) == padded, "the saved file keeps the title's bytes");
            device::Container c2;
            c2.open(path, &err);
            device::Linear l2(&c2);
            dfs::Disc d2;
            CHECK(d2.attach(&l2, quint64(b.size()), 0, lay, &err) && describe(d2) == describe(*disc) && d2.titleBytes() == padded,
                  "read back as it was in memory");
        }
        // a title set: the characters as given, then zeros (DFS 2.45's *TITLE), shown trimmed; faithful too (step 6b's
        // one unfaithful case: a title set with spaces used to be kept as given in memory but read back trimmed)
        CHECK(disc->setTitle(" NEW ", &err) && disc->titleBytes() == QByteArray(" NEW ").leftJustified(12, '\0') &&
                  titleOnDisc() == disc->titleBytes() && disc->title() == "NEW" && dfsFaithful(*disc, c, lin, &why),
              "setTitle(' NEW '): zero-padded, shown trimmed, faithful: " + err + why);
        CHECK(disc->setTitle("TWELVE CHARS", &err) && titleOnDisc() == "TWELVE CHARS" && disc->setTitle("", &err) &&
                  titleOnDisc() == QByteArray(12, '\0') && disc->title().isEmpty() && dfsFaithful(*disc, c, lin, &why),
              "twelve characters, then none: " + err + why);
        // bytes after a NUL are kept as well (a title shorter than 12 with something left behind it)
        QByteArray odd("SHORT");
        odd.append('\0'); odd.append("JUNK!!");
        {
            QByteArray cat;
            lin.read(0, 512, &cat, &err);
            cat.replace(0, 8, odd.left(8));
            cat.replace(256, 4, odd.mid(8, 4));
            CHECK(lin.write(0, cat, &err) && disc->reload(&err) && disc->title() == "SHORT" && disc->titleBytes() == odd,
                  "a NUL in the title: shown up to it, every byte kept: " + err);
        }
        CHECK(disc->addFile('$', "FOUR", pattern(100, 5), 0, 0, false, false, &err) && titleOnDisc() == odd && dfsFaithful(*disc, c, lin, &why),
              "and written back as it was: " + err + why);
    }

    printf("Step 6b: a FAT floppy: Disc::transact is the only rollback, and reload() is faithful\n");
    {
        QByteArray img(737280, 0);
        const unsigned char bpb[] = {0xEB, 0x3C, 0x90, 'M', 'S', 'D', 'O', 'S', '5', '.', '0', 0x00, 0x02, 2, 1, 0, 2, 112, 0,
                                     0xA0, 0x05, 0xF9, 3, 0, 9, 0, 2, 0};
        img.replace(0, int(sizeof(bpb)), QByteArray(reinterpret_cast<const char *>(bpb), int(sizeof(bpb))));
        img[510] = char(0x55); img[511] = char(0xAA);
        for (int fat : {1, 4}) { img[fat * 512] = char(0xF9); img[fat * 512 + 1] = char(0xFF); img[fat * 512 + 2] = char(0xFF); }
        const QString path = d + "/f6b.img";
        spit(path, img);
        device::Container c;
        CHECK(c.open(path, &err), "open: " + err);
        device::Linear lin(&c);
        auto fv = std::make_shared<fat::Volume>();
        CHECK(fv->attach(&lin, 0, 737280, &err), "attach: " + err);
        auto vol = std::make_shared<fat::FsVolume>(fv);
        fs::Disc fd(&c);
        fd.addMount({"DOS", vol});
        QString why;
        quint32 sub = 0, deep = 0;
        CHECK(fv->addFile(0, "ONE.TXT", pattern(5000, 1), false, &err) && fv->makeDir(0, "SUB", &err, &sub) &&
                  fv->makeDir(sub, "DEEP", &err, &deep) && fatFaithful(*fv, c, lin, 737280, &why), "adds and directories: " + err + why);
        bool added = true;
        for (int i = 0; i < 40 && added; ++i) added = fv->addFile(sub, QString("F%1.DAT").arg(i), pattern(700 + 100 * i, i), false, &err);
        CHECK(added && fatFaithful(*fv, c, lin, 737280, &why), "40 files: the directory grows: " + err + why);
        CHECK(fv->addFile(0, "ONE.TXT", pattern(20000, 2), true, &err) && fatFaithful(*fv, c, lin, 737280, &why), "a replace: " + err + why);
        CHECK(fv->modify(sub, "F3.DAT", "RENAMED.DAT", fat::AttrR | fat::AttrH, &err) && fv->remove(sub, "F4.DAT", &err) &&
                  fatFaithful(*fv, c, lin, 737280, &why), "a rename with attributes, a remove: " + err + why);
        CHECK(fv->setLabel("beeb disk", &err) && fatFaithful(*fv, c, lin, 737280, &why), "a label: " + err + why);
        CHECK(fv->remove(0, "SUB", &err) && fatFaithful(*fv, c, lin, 737280, &why), "a directory removed with its files: " + err + why);
        // a replace that doesn't fit: the old file's clusters are freed in memory, then there is no room
        const device::Container::Snapshot snap = c.snapshot();
        const quint32 freeBefore = fv->freeClusters();
        const QByteArray one = pattern(20000, 2), huge(int(quint64(freeBefore + 50) * fv->clusterBytes()), 'h');
        CHECK(!fd.transact([&](QString *x) { return vol->write(fs::Path(), "ONE.TXT", huge, {}, QDateTime(), true, x); }, &err),
              "a replace that doesn't fit fails: " + err);
        QByteArray back;
        CHECK(c.snapshot() == snap && fv->freeClusters() == freeBefore && vol->read(fs::Path({"ONE.TXT"}), &back, &err) && back == one,
              "the transaction put back the staging and the FAT: ONE.TXT is there as it was");
        QString e2;
        CHECK(!fv->addFile(0, "ONE.TXT", huge, true, &e2) && fv->freeClusters() != freeBefore,
              "without a transaction the FAT in memory has ONE.TXT's clusters freed (" + e2 + ")");
        CHECK(fv->reload(&err) && fv->freeClusters() == freeBefore && c.snapshot() == snap && fatFaithful(*fv, c, lin, 737280, &why),
              "reload() gives it back: " + err + why);
        // a full root directory: a new directory's cluster is taken and written before there turns out to be no entry for it
        int n = 0;
        while (n < 200 && fv->addFile(0, QString("R%1").arg(n), QByteArray(), false, &e2)) ++n;
        CHECK(n > 0 && fv->rootEntries() == 112, QString("the root directory filled with %1 more entries (%2)").arg(n).arg(e2));
        const device::Container::Snapshot full = c.snapshot();
        const quint32 freeFull = fv->freeClusters();
        CHECK(!fd.transact([&](QString *x) { return vol->mkdir(fs::Path(), "NEWDIR", x); }, &err) && c.snapshot() == full &&
                  fv->freeClusters() == freeFull, "a directory in a full root: refused, its cluster put back: " + err);
        CHECK(fv->check().ok() && fd.commit(device::Container::SaveOptions(), &err), "checked and saved: " + err);
    }

    printf("New-disc presets: the registry and the module presets (step 5m)\n");
    {
        fs::PresetRegistry r;
        dfs::addPresets(r);
        fat::addPresets(r);
        QStringList ids;
        for (const fs::Preset &p : r.all()) ids << p.id;
        CHECK(ids == QStringList({"dfs-ss40", "dfs-ss80", "dfs-ds40", "dfs-ds80", "wat-ss40", "wat-ss80", "wat-ds40", "wat-ds80",
                                  "atom-ss40", "atom-ss80", "atom-ds40", "atom-ds80", "dos-800", "pc-360", "pc-720", "pc-1200", "pc-1440"}), "DFS then FAT, in order: " + ids.join(' '));
        dfs::addPresets(r);
        CHECK(r.all().size() == 17 && r.all()[0].id == "dfs-ss40" && r.all()[12].id == "dos-800", "adding a module's presets again changes nothing");
        const fs::Preset *p = r.find("DFS-DS80");
        CHECK(p && p->id == "dfs-ds80" && p->name == "DFS - 2 x 200K, 80 track, double-sided (.dsd)" && p->fsId == "dfs" && p->source == "dfs",
              "find ignores case; the DFS 80-track double-sided preset");
        CHECK(p && p->cylinders == 80 && p->heads == 2 && p->sectors == 10 && p->sectorSize == 256 && p->bytes == 409600 &&
                  p->doubleSided && p->sideOrder && p->suffix == "dsd" && p->titleWhat == "Disc title" && p->titleMax == 12 &&
                  !p->extra.value("watford").toBool(), "its size, shape, side order and title");
        p = r.find("wat-ss40");
        CHECK(p && p->name == "Watford DFS 62-file - 100K, 40 track, single-sided (.ssd)" && p->bytes == 102400 && p->heads == 1 &&
                  !p->sideOrder && p->suffix == "ssd" && p->extra.value("watford").toBool(), "Watford 40-track single-sided");
        p = r.find("dos-800");
        CHECK(p && p->name == "Acorn DOS Plus 800K" && p->fsId == "fat" && p->bytes == 819200 && p->cylinders == 80 && p->sectors == 5 &&
                  p->sectorSize == 1024 && p->sideOrder && p->suffix == "img" && p->titleWhat == "Volume label" && p->titleMax == 11 &&
                  p->extra.value("fatFormat").toInt() == fat::FmtAcorn800, "Acorn DOS Plus 800K: a side order to choose");
        p = r.find("pc-1440");
        CHECK(p && p->name == "PC 1.44M - 80 track, double-sided, high density" && p->bytes == 1474560 && p->heads == 2 && p->sectors == 18 &&
                  p->doubleSided && !p->sideOrder && p->extra.value("fatFormat").toInt() == fat::FmtPc1440, "PC 1.44M: always interleaved");
        bool sizes = true;
        for (const fs::Preset &q : r.all())
            sizes = sizes && q.bytes == quint64(q.cylinders) * quint64(q.heads) * quint64(q.sectors) * quint64(q.sectorSize) &&
                    q.containers == QStringList({"raw", "imd", "edsk", "dsk"});
        CHECK(sizes, "every module preset's size is its geometry; every one can be raw, ImageDisk, extended or standard DSK");
        fs::Preset own;
        own.id = "pc-720";
        own.name = "mine";
        own.source = "app";
        r.add(own);
        CHECK(r.all().size() == 17 && r.all()[14].name == "mine" && r.fromSource("app").size() == 1, "the same id replaces in place");
        r.removeSource("dfs");
        CHECK(r.all().size() == 5 && !r.find("dfs-ss40") && r.find("pc-360"), "removeSource takes out one source's presets");
        CHECK(!r.find("adfs-s"), "an unknown id: null");
        CHECK(&fs::presets() == &fs::presets() && fs::presets().all().isEmpty(), "the program's registry: one, empty until something adds to it");
    }

    printf(failures ? "\n%d FAILED\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
