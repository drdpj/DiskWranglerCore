// Part of DiskWranglerCore - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
// volumetest: the filesystem layer on its own (diskwrangler/fs). Path; a small filesystem written for this test on a
// raw file, as a Volume, in a Disc: transactions put the staging back and reload; validateAll vetoes a save, and
// validateTouched only for the mounts a change touches; the default retro-files mapping and placement; and detection:
// tiers, the probes' order, when to ask, when to hand the disc on. QtCore only.
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>
#include <vector>
#include <diskwrangler/device/container.h>
#include <diskwrangler/fs/detection.h>
#include <diskwrangler/fs/disc.h>
#include <diskwrangler/fs/volume.h>

using namespace dw;
using namespace dw::fs;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", qPrintable(QString(msg))); ++failures; } else printf("  ok: %s\n", qPrintable(QString(msg))); } while (0)

// A toy filesystem: sector 0 is a directory of 8 entries (16-byte name, 2-byte start sector, 4-byte length, 1-byte
// flags), files are contiguous from sector 1 on. Enough to show a volume that caches its directory.
class ToyNames : public NameRules {
public:
    bool valid(const QString &n, NodeKind k, QString *why) const override {
        if (k != NodeKind::File || n.isEmpty() || n.size() > 16) { if (why) *why = "bad name"; return false; }
        return true;
    }
    QString fromHost(const QString &h, NodeKind) const override { return h.left(16).toUpper(); }
};

class Toy : public Volume {
public:
    struct F { QString name; int start = 0; int len = 0; bool flag = false; };
    explicit Toy(device::Container *c) : m_c(c) {}
    bool failAfterData = false;                 // make write() fail half way, after it has staged the data
    bool corrupt = false;                       // make check() report an error
    QString fsId() const override { return "toy"; }
    Caps caps() const override { Caps c; c.maxEntriesPerDir = 8; return c; }
    QVector<AttrSpec> fileAttrs() const override {
        AttrSpec f; f.key = "flag"; f.label = "Flag"; f.type = AttrSpec::Bool; f.retroFamily = "toy"; f.retroKey = "flag";
        AttrSpec n; n.key = "num"; n.label = "Num"; n.type = AttrSpec::Hex; n.retroFamily = "toy"; n.retroKey = "num";
        return {f, n};
    }
    const NameRules &names() const override { return m_names; }
    VolumeInfo info() const override { VolumeInfo i; i.total = 64 * 1024; return i; }
    bool list(const Path &dir, QVector<Entry> *out, QString *err) const override {
        if (!dir.isRoot()) { *err = "flat"; return false; }
        out->clear();
        for (const F &f : m_files) { Entry e; e.name = f.name; e.size = f.len; e.attrs = {{"flag", f.flag}}; out->append(e); }
        return true;
    }
    bool read(const Path &p, QByteArray *out, QString *err) const override {
        for (const F &f : m_files)
            if (f.name == p.name()) return m_c->readStored(qint64(f.start) * 256, f.len, out, err);
        *err = "not found";
        return false;
    }
    bool write(const Path &, const QString &name, const QByteArray &data, const AttrValues &a, const QDateTime &, bool, QString *err) override {
        if (m_files.size() >= 8) { *err = "directory full"; return false; }
        int start = 1;
        for (const F &f : m_files) start = qMax(start, f.start + (f.len + 255) / 256);
        if (!m_c->writeStored(qint64(start) * 256, data, err)) return false;
        if (failAfterData) { *err = "failed half way"; return false; }
        F f; f.name = name; f.start = start; f.len = data.size(); f.flag = a.value("flag").toBool();
        m_files << f;
        return writeDir(err);
    }
    bool remove(const Path &p, QString *err) override {
        for (int i = 0; i < m_files.size(); ++i)
            if (m_files[i].name == p.name()) { m_files.removeAt(i); return writeDir(err); }
        *err = "not found";
        return false;
    }
    bool setAttrs(const Path &p, const AttrValues &a, QString *err) override {
        for (F &f : m_files)
            if (f.name == p.name()) { f.flag = a.value("flag", f.flag).toBool(); return writeDir(err); }
        *err = "not found";
        return false;
    }
    Report check() const override { Report r; if (corrupt) r.errors << "the toy is broken"; r.info << QString("%1 files").arg(m_files.size()); return r; }
    bool reload(QString *err) override {
        ++reloads;
        QByteArray d;
        if (!m_c->readStored(0, 256, &d, err)) return false;
        m_files.clear();
        for (int i = 0; i < 8; ++i) {
            const QByteArray e = d.mid(i * 32, 32);
            if (e[0] == 0) continue;
            F f;
            f.name = QString::fromLatin1(e.left(16)).trimmed();
            f.start = quint8(e[16]) | quint8(e[17]) << 8;
            f.len = quint8(e[18]) | quint8(e[19]) << 8 | quint8(e[20]) << 16;
            f.flag = e[22] != 0;
            m_files << f;
        }
        return true;
    }
    int reloads = 0;
private:
    bool writeDir(QString *err) {
        QByteArray d(256, 0);
        for (int i = 0; i < m_files.size(); ++i) {
            QByteArray e = m_files[i].name.toLatin1().leftJustified(16, ' ');
            e += char(m_files[i].start & 0xFF); e += char(m_files[i].start >> 8);
            e += char(m_files[i].len & 0xFF); e += char((m_files[i].len >> 8) & 0xFF); e += char(m_files[i].len >> 16);
            e += char(0); e += char(m_files[i].flag ? 1 : 0);
            d.replace(i * 32, e.size(), e);
        }
        return m_c->writeStored(0, d, err);
    }
    device::Container *m_c;
    QVector<F> m_files;
    ToyNames m_names;
};

class FixedProbe : public Probe {
public:
    FixedProbe(const QString &id, QVector<Candidate> c) : m_id(id), m_c(std::move(c)) {}
    QString id() const override { return m_id; }
    QVector<Candidate> probe(const ProbeContext &) const override { return m_c; }
private:
    QString m_id;
    QVector<Candidate> m_c;
};

static Candidate cand(const QString &fs, Tier t, bool foreign = false, const QString &desc = QString()) {
    Candidate c;
    c.fsId = fs;
    c.tier = t;
    c.description = desc.isEmpty() ? fs : desc;
    if (foreign) c.owner = cpmDiskWrangler();
    return c;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    const QString d = tmp.path();
    QString err;

    printf("Path\n");
    {
        const Path p = Path::fromString("/Games/Elite/");
        CHECK(p.parts == QStringList({"Games", "Elite"}) && p.name() == "Elite", "fromString drops empty parts");
        CHECK(p.parent() == Path::fromString("Games") && p.parent().parent().isRoot() && Path().parent().isRoot(), "parent");
        CHECK(Path().child("A").child("B").toString() == "A/B" && p.toString('.') == "Games.Elite", "child, toString");
    }

    printf("a volume in a Disc: transactions\n");
    {
        { QFile f(d + "/toy.img"); f.open(QIODevice::WriteOnly); f.write(QByteArray(64 * 1024, 0)); }
        device::Container c;
        CHECK(c.open(d + "/toy.img", &err), "open: " + err);
        auto toy = std::make_shared<Toy>(&c);
        CHECK(toy->reload(&err), "reload an empty disc");
        Disc disc(&c);
        disc.addMount({"toy", toy});
        CHECK(disc.transact([&](QString *e) { return toy->write(Path(), "ONE", "first file", {{"flag", true}}, QDateTime(), false, e); }, &err),
              "a write in a transaction");
        QVector<Entry> l;
        CHECK(toy->list(Path(), &l, &err) && l.size() == 1 && l[0].name == "ONE" && l[0].attrs["flag"].toBool(), "listed with its flag");
        const device::Container::Snapshot before = c.snapshot();
        const int reloads = toy->reloads;
        toy->failAfterData = true;
        CHECK(!disc.transact([&](QString *e) { return toy->write(Path(), "TWO", QByteArray(3000, 'x'), {}, QDateTime(), false, e); }, &err)
                  && err == "failed half way", "a failing write fails with its error: " + err);
        CHECK(c.snapshot() == before, "the staged writes are as they were before it");
        CHECK(toy->reloads == reloads + 1 && toy->list(Path(), &l, &err) && l.size() == 1, "the volume reloaded, still one file");
        toy->failAfterData = false;
        Entry e;
        CHECK(toy->stat(Path({"ONE"}), &e, &err) && e.size == 10, "stat by default lists the parent");
        CHECK(!toy->stat(Path({"NONE"}), &e, &err), "stat: not found");
        CHECK(toy->stat(Path(), &e, &err) && e.kind == NodeKind::Dir, "stat of the root is a directory");
        CHECK(!toy->mkdir(Path(), "D", &err) && !toy->rename(Path({"ONE"}), Path({"X"}), &err), "mkdir and rename: not supported by default");
        CHECK(toy->setVolumeAttrs({}, &err) && !toy->setVolumeAttrs({{"title", "x"}}, &err), "no volume attributes by default");

        printf("validateAll vetoes a save; commit; revert\n");
        toy->corrupt = true;
        Report r = disc.validateAll();
        CHECK(!r.ok() && r.errors.first() == "the toy is broken", "validateAll: " + r.errors.join("; "));
        bool verifyRan = false;
        device::Container::SaveOptions o;
        o.verify = [&](device::Container &, QString *) { verifyRan = true; return true; };
        CHECK(!disc.commit(o, &err) && err.contains("NOTHING was written") && !verifyRan, "commit refused by the check");
        QFile f(d + "/toy.img");
        f.open(QIODevice::ReadOnly);
        CHECK(f.readAll() == QByteArray(64 * 1024, 0), "the file is untouched");
        f.close();
        toy->corrupt = false;
        CHECK(disc.commit(o, &err) && verifyRan && !disc.dirty(), "commit: " + err);
        device::Container c2;
        CHECK(c2.open(d + "/toy.img", &err), "open the saved file");
        Toy again(&c2);
        QByteArray data;
        CHECK(again.reload(&err) && again.read(Path({"ONE"}), &data, &err) && data == "first file", "the file is in the saved image");
        CHECK(toy->remove(Path({"ONE"}), &err) && disc.dirty(), "a change");
        disc.revert();
        CHECK(!disc.dirty() && toy->list(Path(), &l, &err) && l.size() == 1, "revert drops it and reloads");

        printf("two mounts: validateAll names them\n");
        auto toy2 = std::make_shared<Toy>(&c);
        toy2->corrupt = true;
        disc.addMount({"other", toy2});
        r = disc.validateAll();
        CHECK(r.errors == QStringList({"other: the toy is broken"}) && r.info.size() == 2 && r.info[0].startsWith("toy: "), "labelled lines");

        printf("validateTouched: only a mount the staged writes touch can veto (step 5i)\n");
        bool otherTouched = false;
        disc.mounts()[1].touched = [&] { return otherTouched; };
        QVector<Report> per;
        r = disc.validateTouched(&per);
        CHECK(!disc.dirty() && r.ok() && r.info.isEmpty() && per.size() == 2 && per[0].info.isEmpty() && per[1].errors.isEmpty(),
              "nothing staged: no mount is checked");
        CHECK(disc.transact([&](QString *e) { return toy->write(Path(), "THREE", "x", {}, QDateTime(), false, e); }, &err) && disc.dirty(),
              "a change to the first mount");
        r = disc.validateTouched(&per);
        CHECK(r.ok() && r.info == QStringList({"toy: 2 files"}) && per.size() == 2 && per[0].info.size() == 1 && per[1].info.isEmpty(),
              "an untouched broken mount doesn't veto; the touched one (no touched function: the whole disc) is checked");
        otherTouched = true;
        r = disc.validateTouched(&per);
        CHECK(r.errors == QStringList({"other: the toy is broken"}) && per[1].errors == QStringList({"the toy is broken"}),
              "a touched broken mount vetoes: " + r.errors.join("; "));
        CHECK(disc.validateAll().errors == r.errors, "validateAll checks every mount, touched or not");
        otherTouched = false;
        toy->corrupt = true;
        r = disc.validateTouched();
        CHECK(r.errors == QStringList({"toy: the toy is broken"}), "the touched mount's errors, labelled");
        toy->corrupt = false;
        disc.revert();
    }

    printf("retro-files: the default mapping, placement\n");
    {
        device::Container c;
        Toy toy(&c);
        Entry e;
        e.attrs = {{"flag", true}, {"num", 0x1900u}};
        const QMap<QString, QJsonObject> m = toy.toRetro(e);
        CHECK(m.size() == 1 && m["toy"]["flag"].toBool() && m["toy"]["num"].toString() == "00001900", "toRetro by the specs");
        const AttrValues back = toy.fromRetro(m, NodeKind::File);
        CHECK(back.value("flag").toBool() && back.value("num").toUInt() == 0x1900, "fromRetro back");
        CHECK(toy.fromRetro({{"other", QJsonObject{{"flag", true}}}}, NodeKind::File).isEmpty(), "other families are ignored");
        TransferItem it;
        it.name = "long host file name.txt";
        const Placement p = toy.placeIncoming(it, Path());
        CHECK(p.dir.isRoot() && p.name == "LONG HOST FILE N" && p.warning.isEmpty(), "placeIncoming: target dir, name from the rules");
    }

    printf("detection: tiers and decisions\n");
    {
        auto run = [](std::vector<FixedProbe> probes) {
            QVector<const Probe *> ps;
            for (const FixedProbe &p : probes) ps << &p;
            return detect(ProbeContext(), ps);
        };
        Verdict v = run({});
        CHECK(v.action == Verdict::None && !v.bestCandidate(), "no candidates: none");
        v = run({FixedProbe("a", {cand("adfs", Tier::Certain)}), FixedProbe("b", {cand("dfs", Tier::Likely)})});
        CHECK(v.action == Verdict::Open && v.bestCandidate()->fsId == "adfs" && v.bestCandidate()->probe == "a", "certain beats likely");
        v = run({FixedProbe("a", {cand("fat", Tier::Likely)}), FixedProbe("b", {cand("dfs", Tier::Likely)})});
        CHECK(v.action == Verdict::Ask && v.bestCandidate()->fsId == "fat" && v.rivalCandidate()->fsId == "dfs", "two likely from two probes: ask (first probe first)");
        v = run({FixedProbe("a", {cand("cpm", Tier::Likely, false, "one"), cand("cpm", Tier::Likely, false, "two")})});
        CHECK(v.action == Verdict::Open && v.bestCandidate()->description == "one", "two from one probe: its own ranking decides");
        v = run({FixedProbe("a", {cand("x", Tier::Possible)}), FixedProbe("b", {cand("y", Tier::Possible)})});
        CHECK(v.action == Verdict::Open && v.bestCandidate()->fsId == "x", "two possible: no question, the first");
        v = run({FixedProbe("a", {cand("dfs", Tier::Likely)}), FixedProbe("b", {cand("torch", Tier::Certain, true)})});
        CHECK(v.action == Verdict::Handoff && v.bestCandidate()->owner.id == "cpmdiskwrangler", "a certain foreign disc: hand it on");
        v = run({FixedProbe("a", {cand("cpm", Tier::Certain)}), FixedProbe("b", {cand("fat", Tier::Likely, true)})});
        CHECK(v.action == Verdict::Open && v.bestCandidate()->fsId == "cpm", "ours certain, theirs likely: open here");
        v = run({FixedProbe("a", {cand("cpm", Tier::Likely)}), FixedProbe("b", {cand("fat", Tier::Likely, true)})});
        CHECK(v.action == Verdict::Ask && v.rivalCandidate()->foreign(), "ours likely, theirs likely: ask");
        v = run({FixedProbe("a", {cand("cpm", Tier::Possible)}), FixedProbe("b", {cand("dfs", Tier::Likely, true)})});
        CHECK(v.action == Verdict::Handoff, "theirs more likely than ours: hand it on");
        CHECK(tierName(Tier::Certain) == "certain" && tierName(Tier::Possible) == "possible", "tier names");
        CHECK(acornDiskWrangler().id == "acorndiskwrangler" && cpmDiskWrangler().folder == "CPM Disk Wrangler", "the family's programs");
    }

    printf(failures ? "\n%d FAILED\n" : "\nall ok\n", failures);
    return failures ? 1 : 0;
}
