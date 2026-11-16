// Part of DiskWranglerCore - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
// devicetest: the device layer on its own. Raw files read from the file and staged in blocks; the physical view of a
// raw file and of ImageDisk / DSK files; indexOfId; the stored-bytes view and Linear (side orders, slices, staged
// ranges); and the checked save: temporary file, read-back, verify veto, .bak as a hard link, padding, permissions,
// Save As between containers. QtCore only.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdio>
#include <diskwrangler/device/container.h>
#include <diskwrangler/device/linear.h>
#include <diskwrangler/device/safesave.h>
#include <diskwrangler/device/sideorder.h>
#ifndef Q_OS_WIN
#include <sys/stat.h>
#endif

using namespace dw::device;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", qPrintable(QString(msg))); ++failures; } else printf("  ok: %s\n", qPrintable(QString(msg))); } while (0)

static QByteArray slurp(const QString &p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<missing>"); }
static void spit(const QString &p, const QByteArray &d) { QFile f(p); f.open(QIODevice::WriteOnly); f.write(d); }
static QByteArray pattern(int n, int seed) { QByteArray b(n, 0); for (int i = 0; i < n; ++i) b[i] = char((i * 7 + seed * 13 + i / 251) & 0xFF); return b; }
static QStringList leftovers(const QString &dir) { return QDir(dir).entryList(QStringList() << "*.dwsave-*", QDir::Files | QDir::Hidden); }

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir tmp;
    tmp.setAutoRemove(qEnvironmentVariableIsEmpty("DEVICETEST_KEEP"));      // set it to look at the files afterwards
    const QString d = tmp.path();
    if (!tmp.autoRemove()) printf("files in %s\n", qPrintable(d));
    QString err;

    printf("raw file, no layout: read from the file, staged in blocks\n");
    {
        const QByteArray orig = pattern(300000, 1);
        spit(d + "/a.img", orig);
        Container c;
        CHECK(c.open(d + "/a.img", &err), "open raw: " + err);
        CHECK(c.kind() == Kind::Raw && !c.physical() && c.rawFileSize() == orig.size(), "raw, no physical view yet");
        CHECK(c.rawBytes() == orig, "rawBytes is the file");
        CHECK(c.storedSize() == orig.size(), "stored size is the file's");
        QByteArray b;
        CHECK(c.readStored(1000, 5000, &b, &err) && b == orig.mid(1000, 5000), "readStored");
        CHECK(!c.hasPending(), "nothing staged");
        CHECK(c.writeStored(1001, "hello", &err), "writeStored (unaligned)");
        CHECK(c.hasPending() && c.storedTouched(1000, 10) && !c.storedTouched(2000, 10), "staged range");
        CHECK(c.readStored(999, 8, &b, &err) && b == orig.mid(999, 2) + "hello" + orig.mid(1006, 1), "reads overlay the staging");
        CHECK(slurp(d + "/a.img") == orig, "the file is untouched");
        Container::Snapshot s = c.snapshot();
        CHECK(c.writeStored(5000, "XX", &err), "a second write");
        c.restore(s);
        CHECK(!c.storedTouched(5000, 2), "restore drops it");
        CHECK(c.writeStored(1001, orig.mid(1001, 5), &err) && c.hasPending(), "rewriting the original bytes still counts as a change");
        c.discardStored(1024 - 128, 256);
        CHECK(!c.storedTouched(896, 256) && c.storedTouched(1001, 1) == false, "discard drops blocks wholly inside");
        CHECK(c.writeStored(299990, QByteArray(20, 'Z'), &err), "a write past the end of the file");
        CHECK(c.writeStored(1001, "hello", &err), "staged again");
        Container::SaveOptions o;
        o.backup = true;
        int verified = 0;
        o.verify = [&](Container &w, QString *) { ++verified; QByteArray x; w.readStored(1001, 5, &x, &err); return x == "hello"; };
        CHECK(c.commit(o, &err), "commit: " + err);
        QByteArray want = orig;
        want.replace(1001, 5, "hello");
        want.resize(300010);
        want.replace(299990, 20, QByteArray(20, 'Z'));
        CHECK(slurp(d + "/a.img") == want, "the file holds the changes (and grew)");
        CHECK(slurp(d + "/a.img.bak") == orig, ".bak is the original");
        CHECK(verified == 1, "verify saw the new file");
        CHECK(!c.hasPending() && c.rawBytes() == want, "re-opened: nothing staged");
        CHECK(leftovers(d).isEmpty(), "no temporary file left");
#ifndef Q_OS_WIN
        struct stat a, bk;
        stat(QFile::encodeName(d + "/a.img").constData(), &a);
        stat(QFile::encodeName(d + "/a.img.bak").constData(), &bk);
        CHECK(a.st_ino != bk.st_ino && bk.st_nlink == 1, "the .bak is the old file itself (hard link), the image a new one");
#endif
    }

    printf("a failed check changes nothing\n");
    {
        const QByteArray orig = pattern(4096, 2);
        spit(d + "/b.img", orig);
        QFile::setPermissions(d + "/b.img", QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup);
        const QFile::Permissions perm = QFile::permissions(d + "/b.img");
        Container c;
        CHECK(c.open(d + "/b.img", &err), "open");
        CHECK(c.writeStored(0, "XYZ", &err), "write");
        Container::SaveOptions o;
        o.verify = [](Container &, QString *e) { *e = "verify says no"; return false; };
        CHECK(!c.commit(o, &err) && err == "verify says no", "verify veto: " + err);
        CHECK(slurp(d + "/b.img") == orig && !QFile::exists(d + "/b.img.bak") && leftovers(d).isEmpty(), "original untouched, nothing left behind");
        CHECK(c.hasPending(), "the changes are still staged");
        o.verify = nullptr;
        o.validate = [](QString *e) { *e = "validate says no"; return false; };
        CHECK(!c.commit(o, &err) && err == "validate says no" && slurp(d + "/b.img") == orig, "validate veto");
        c.setWritable(false);
        CHECK(!c.writeStored(10, "q", &err) && err.contains("read-only"), "not writable: " + err);
        c.setWritable(true);
        CHECK(c.commit(&err, false), "commit: " + err);
        CHECK(slurp(d + "/b.img").left(3) == "XYZ", "written");
        CHECK(QFile::permissions(d + "/b.img") == perm, "the new file has the original's permissions");
        CHECK(!QFile::exists(d + "/b.img.bak"), "no .bak unless asked");
    }

    printf("padding a short raw file\n");
    {
        spit(d + "/c.img", pattern(1000, 3));
        Container c;
        CHECK(c.open(d + "/c.img", &err), "open");
        c.setRawPadding(2048, 0);
        CHECK(c.needsPadding() && c.storedSize() == 2048, "short: needs padding");
        QByteArray b;
        CHECK(c.readStored(990, 20, &b, &err) && b == pattern(1000, 3).mid(990) + QByteArray(10, 0), "reads the padding");
        CHECK(c.commit(&err, false), "commit");
        CHECK(slurp(d + "/c.img") == pattern(1000, 3) + QByteArray(1048, 0), "padded with zeros");
        CHECK(!c.needsPadding(), "no longer short");
    }

    printf("raw file with a layout: the physical view\n");
    {
        RawLayout l;
        l.seclen = 256; l.sectrk = 16; l.cyls = 4; l.heads = 2; l.firstId = 0;
        QByteArray img = pattern(int(l.bytes()) - 512, 4);          // one sector short of a full disc... and one more
        spit(d + "/p.img", img);
        Container c;
        CHECK(c.open(d + "/p.img", &err) && c.setRawLayout(l, &err), "open, layout");
        CHECK(c.physical() && c.cylinders() == 4 && c.heads() == 2 && c.needsPadding(), "physical view; short file");
        QByteArray s;
        CHECK(c.read(1, 1, 3, &s) && s == img.mid(int(l.trackOffset(1, 1)) + 3 * 256, 256), "read cyl 1 head 1 sector 3");
        CHECK(c.read(3, 1, 15, &s) && s == QByteArray(256, char(0xE5)), "past the end of the file: E5");
        CHECK(c.indexOfId(2, 0, 5) == 5 && c.indexOfId(2, 0, 16) == -1, "indexOfId");
        CHECK(c.write(2, 1, 4, QByteArray(256, 'q'), &err) && c.hasPending(), "write a sector");
        CHECK(c.write(2, 1, 4, img.mid(int(l.trackOffset(2, 1)) + 4 * 256, 256), &err) && !c.hasPending(), "writing back what the file holds drops it");
        CHECK(c.write(0, 0, 0, QByteArray(256, 'w'), &err), "write sector 0");
        CHECK(c.commit(&err, false), "commit: " + err);
        QByteArray f = slurp(d + "/p.img");
        CHECK(f.size() == l.bytes() && f.left(256) == QByteArray(256, 'w') && f.right(512) == QByteArray(512, char(0xE5)), "saved, padded with E5");
        CHECK(c.rawLayout().sameShape(l) && c.physical(), "keeps its layout after the save");
    }

    printf("ImageDisk and DSK: Save As, the stored view, Linear\n");
    {
        RawLayout l;
        l.seclen = 256; l.sectrk = 16; l.cyls = 80; l.heads = 2; l.firstId = 0; l.mode = 5;
        Container blank;
        blank.createBlank(Kind::Imd, l, "devicetest");
        QByteArray disc = pattern(int(l.bytes()), 5);
        CHECK(blank.writeStored(0, disc, &err), "fill a blank ImageDisk through the stored view: " + err);
        CHECK(blank.saveAs(d + "/l.imd", Kind::Imd, &err), "Save As ImageDisk: " + err);
        CHECK(blank.kind() == Kind::Imd && blank.path() == d + "/l.imd" && !blank.hasPending(), "now the new file");
        CHECK(blank.saveAs(d + "/l.img", Kind::Raw, &err), "Save As raw: " + err);
        CHECK(slurp(d + "/l.img") == disc, "the raw file is the stored bytes");
        CHECK(blank.kind() == Kind::Raw && blank.physical() && blank.rawLayout().sameShape(l), "raw now, with the layout");
        Container imd, raw;
        CHECK(imd.open(d + "/l.imd", &err) && raw.open(d + "/l.img", &err), "open both");
        CHECK(imd.storedSize() == disc.size() && raw.storedSize() == disc.size(), "same stored size");
        SideOrder adfsL;                                         // ADFS L: numbered side 0 first, stored interleaved
        adfsL.trackBytes = 4096; adfsL.cylinders = 80; adfsL.sideFirst = true;
        Linear li(&imd, adfsL), lr(&raw, adfsL);
        QByteArray a, b;
        CHECK(li.read(0, li.size(), &a, &err) && lr.read(0, lr.size(), &b, &err) && a == b, "Linear reads the same disc from either");
        CHECK(a.mid(80 * 4096, 4096) == disc.mid(4096, 4096), "side 1 track 0 is stored second");
        CHECK(li.write(81 * 4096 + 100, "side one", &err) && li.touched(81 * 4096, 4096) && !li.touched(0, 4096), "write through Linear");
        const int c1h1 = 3 * 4096;                                // cylinder 1 head 1 is stored fourth
        QByteArray s;
        CHECK(imd.read(1, 1, 0, &s) && s.mid(100, 8) == "side one", "landed on cylinder 1 head 1");
        Linear part = li.slice(81 * 4096, 512);
        CHECK(part.read(100, 8, &s, &err) && s == "side one", "slice");
        CHECK(!part.read(510, 4, &s, &err), "reads outside the slice fail");
        CHECK(imd.commit(&err, false), "commit ImageDisk: " + err);
        Container again;
        CHECK(again.open(d + "/l.imd", &err) && again.readStored(c1h1 + 100, 8, &s, &err) && s == "side one", "the ImageDisk holds it");
        CHECK(again.saveAs(d + "/l.dsk", Kind::Edsk, &err) && again.kind() == Kind::Edsk, "Save As extended DSK: " + err);
        QByteArray x, y;
        Container dsk;
        CHECK(dsk.open(d + "/l.dsk", &err) && dsk.readStored(0, dsk.storedSize(), &x, &err) && imd.readStored(0, imd.storedSize(), &y, &err) && x == y,
              "the DSK holds the same disc");
        CHECK(dsk.write(5, 0, 2, QByteArray(256, 'd'), &err) && dsk.commit(&err, false), "write and commit DSK: " + err);
        Container dsk2;
        CHECK(dsk2.open(d + "/l.dsk", &err) && dsk2.read(5, 0, 2, &s) && s == QByteArray(256, 'd'), "the DSK holds the write");
        SideOrder seq = adfsL;
        seq.sequential = true;
        QByteArray stored;
        raw.readStored(0, raw.storedSize(), &stored, &err);
        spit(d + "/seq.img", reorderSides(stored, adfsL, true));
        Container sc;
        CHECK(sc.open(d + "/seq.img", &err), "open a sequential copy");
        Linear ls(&sc, seq);
        CHECK(ls.read(0, ls.size(), &b, &err) && lr.read(0, lr.size(), &a, &err) && a == b, "a sequential file reads as the same disc");
        CHECK(layoutFile(a, seq) == slurp(d + "/seq.img"), "layoutFile builds the sequential file");
        Container shaped;
        RawLayout odd = l;
        odd.cyls = 2;
        shaped.createBlank(Kind::Imd, odd, QString());
        CHECK(shaped.reshapeTrack(0, 0, 10, 512, 1, 5, &err), "an odd track 0");
        CHECK(shaped.storedSize(&err) == 0 && !err.isEmpty(), "no stored view: " + err);
        CHECK(!shaped.saveAs(d + "/odd.img", Kind::Raw, &err) && leftovers(d).isEmpty(), "can't be raw, nothing left behind: " + err);
        CHECK(shaped.saveAs(d + "/odd.imd", Kind::Imd, &err), "but saves as ImageDisk");
        Container td;
        spit(d + "/x.td0", QByteArray("TD\0\0\x15\0\0\0\0\0\0\0", 12) + QByteArray(1, char(0xFF)));
        if (td.open(d + "/x.td0", &err)) CHECK(!td.commit(&err, false) && err.contains("TeleDisk"), "TeleDisk is read-only: " + err);
    }

    printf("cylinder 0 of its own shape (setOwnTrack0): a double-density disc's single-density track 0\n");
    {
        // FLEX's 5.25" double-sided double density: 18 sectors of 256 a side, IDs 1-18 and 19-36; cylinder 0 single
        // density, 10 a side, IDs 1-10 and 11-20
        RawLayout dd;
        dd.seclen = 256; dd.sectrk = 18; dd.cyls = 4; dd.heads = 2; dd.mode = 5;
        Container m;
        m.createBlank(Kind::Imd, dd, "own track 0");
        CHECK(m.reshapeTrack(0, 0, 10, 256, 1, 2, &err) && m.reshapeTrack(0, 1, 10, 256, 11, 2, &err), "cylinder 0 single density");
        for (int c = 1; c < 4; ++c) CHECK(m.reshapeTrack(c, 1, 18, 256, 19, 5, &err), "side 1 numbered on");
        QByteArray want;
        for (int c = 0; c < 4; ++c)
            for (int h = 0; h < 2; ++h)
                for (int i = 0; i < m.sectorCount(c, h); ++i) {
                    const QByteArray s = pattern(256, c * 100 + h * 50 + i);
                    m.write(c, h, i, s, &err);
                    want += s;
                }
        CHECK(m.saveAs(d + "/own0a.imd", Kind::Imd, &err) && !m.hasPending(), "saved, so nothing is staged: " + err);
        const qint64 size = (20 + 3 * 36) * 256;
        CHECK(!m.ownTrack0() && m.storedSize(&err) == 0, "off: no stored view (" + err + ")");
        QByteArray b;
        CHECK(!m.readStored(0, 256, &b, &err) && !m.writeStored(0, "x", &err), "off: no reads or writes");
        m.setOwnTrack0(true);
        RawLayout sl;
        CHECK(m.storedLayout(&sl, &err) && sl.t0sectrk == 10 && sl.t0seclen == 256 && sl.sectrk == 18 && sl.heads == 2 &&
                  sl.cyls == 4 && sl.firstId == 1, "on: the layout of a raw file with track 0 at its own size");
        CHECK(m.storedSize() == size && sl.bytes() == size, "on: the stored size");
        CHECK(m.readStored(0, size, &b, &err) && b == want, "on: the stored bytes, track 0 then the rest, sides in turn");
        Linear lin(&m);
        CHECK(lin.size() == size && lin.read(19 * 256, 2 * 256, &b, &err) && b == want.mid(19 * 256, 512), "a Linear across cylinder 0's end");
        const Container::Snapshot before = m.snapshot();
        CHECK(lin.write(20 * 256 - 10, QByteArray(20, 'Z'), &err), "a write across cylinder 0's end");
        QByteArray s0, s1;
        CHECK(m.read(0, 1, 9, &s0) && s0.right(10) == QByteArray(10, 'Z') && m.read(1, 0, 0, &s1) && s1.left(10) == QByteArray(10, 'Z'),
              "lands in cylinder 0 head 1 ID 20 and cylinder 1 head 0 ID 1");
        CHECK(m.storedTouched(20 * 256 - 1, 1) && m.storedTouched(20 * 256, 1) && !m.storedTouched(21 * 256, 256), "storedTouched");
        m.discardStored(19 * 256, 256);
        CHECK(!m.storedTouched(19 * 256, 256) && m.storedTouched(20 * 256, 1), "discardStored drops the whole sector only");
        m.restore(before);
        CHECK(m.readStored(0, size, &b, &err) && b == want, "restore");
        CHECK(m.saveAs(d + "/own0.imd", Kind::Imd, &err) && m.ownTrack0(), "saved as ImageDisk, still on: " + err);
        Container r;
        CHECK(r.open(d + "/own0.imd", &err) && !r.ownTrack0() && r.storedSize() == 0, "opened again: off");
        r.setOwnTrack0(true);
        CHECK(r.readStored(0, size, &b, &err) && b == want, "on: the same disc");
        const Track *t0 = r.track(0, 1), *t1 = r.track(1, 1);
        CHECK(t0 && t0->mode == 2 && t1 && t1->mode == 5 && r.indexOfId(0, 1, 11) == 0 && r.indexOfId(1, 1, 36) == 17,
              "the file keeps each track's mode and IDs");
        bool seen = false;
        Container::SaveOptions o;
        o.verify = [&](Container &w, QString *) { QByteArray x; seen = w.ownTrack0() && w.readStored(0, size, &x, &err) && x.left(3) == "abc"; return seen; };
        CHECK(r.writeStored(0, "abc", &err) && r.commit(o, &err) && seen, "commit: the copy read back is on too: " + err);
        CHECK(r.ownTrack0() && r.readStored(0, 3, &b, &err) && b == "abc", "and the image after it");
        CHECK(r.saveAs(d + "/own0.dsk", Kind::Edsk, &err), "saved as extended DSK: " + err);
        Container e;
        CHECK(e.open(d + "/own0.dsk", &err), "opened");
        e.setOwnTrack0(true);
        QByteArray x;
        CHECK(e.readStored(0, size, &x, &err) && x == want.mid(3).prepend("abc"), "the DSK holds the same disc");
        CHECK(r.saveAs(d + "/own0.img", Kind::Raw, &err) && r.kind() == Kind::Raw, "saved as raw: " + err);
        CHECK(slurp(d + "/own0.img") == x, "the raw file is the stored bytes, track 0 at its own size");
        RawLayout rl = r.rawLayout();
        CHECK(rl.t0sectrk == 10 && rl.t0seclen == 256 && rl.heads == 2 && r.read(0, 1, 9, &b) && b == x.mid(19 * 256, 256),
              "and reads back with that layout");

        // Research Machines' CP/M: an FM track 0 of 128-byte sectors on a double-density disc of 512-byte ones
        RawLayout rm;
        rm.seclen = 512; rm.sectrk = 9; rm.cyls = 3; rm.heads = 1; rm.mode = 5;
        Container q;
        q.createBlank(Kind::Imd, rm, QString());
        CHECK(q.reshapeTrack(0, 0, 16, 128, 1, 2, &err), "an FM track 0 of 16 x 128");
        q.setOwnTrack0(true);
        CHECK(q.storedSize() == 16 * 128 + 2 * 9 * 512, "another sector size on track 0");
        CHECK(q.writeStored(16 * 128 - 1, "AB", &err) && q.read(0, 0, 15, &b) && b.endsWith('A') && q.read(1, 0, 0, &b) && b.startsWith('B'),
              "a write across it");

        Container bad;
        bad.createBlank(Kind::Imd, dd, QString());
        bad.reshapeTrack(0, 0, 10, 256, 1, 2, &err);
        bad.setOwnTrack0(true);
        CHECK(bad.storedSize(&err) == 0, "cylinder 0's sides of different shapes: none (" + err + ")");
        bad.reshapeTrack(0, 1, 10, 256, 11, 2, &err);
        bad.reshapeTrack(2, 0, 10, 256, 1, 2, &err);
        CHECK(bad.storedSize(&err) == 0, "another odd track: none (" + err + ")");
        CHECK(!bad.saveAs(d + "/bad.img", Kind::Raw, &err) && leftovers(d).isEmpty(), "and no raw file: " + err);
        RawLayout one = dd;
        one.cyls = 1;
        Container single;
        single.createBlank(Kind::Imd, one, QString());
        single.setOwnTrack0(true);
        CHECK(single.storedLayout(&sl, &err) && sl.t0sectrk == 0 && single.storedSize() == 2 * 18 * 256, "one cylinder: its own shape");
    }

    printf("SafeSave on its own\n");
    {
        spit(d + "/s.bin", "old");
        SafeSave s(d + "/s.bin");
        CHECK(s.begin(3, &err), "begin");
        s.device()->write("new");
        CHECK(s.finishWriting(&err) && QFile::exists(s.tempPath()), "written beside it");
        CHECK(slurp(d + "/s.bin") == "old", "original still there");
        CHECK(s.replace(true, &err), "replace");
        CHECK(slurp(d + "/s.bin") == "new" && slurp(d + "/s.bin.bak") == "old" && leftovers(d).isEmpty(), "replaced, .bak kept");
        {
            SafeSave t(d + "/s.bin");
            CHECK(t.begin(3, &err), "begin again");
            t.device()->write("abandoned");
        }
        CHECK(slurp(d + "/s.bin") == "new" && leftovers(d).isEmpty(), "an abandoned save leaves nothing");
        SafeSave u(d + "/nofolder/s.bin");
        CHECK(!u.begin(3, &err), "a missing folder: " + err);
        SafeSave v(d + "/s.bin");
        CHECK(!v.begin(qint64(1) << 60, &err) && err.contains("free space"), "no room: " + err);
    }

    printf(failures ? "%d FAILED\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
