// Part of DiskWranglerCore - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
// tapetest: the tape module. gzip (stored, fixed and dynamic blocks, several members, damage); UEF, TZX / CDT and TAP
// files read chunk for chunk and written back byte for byte, every UEF chunk and TZX block kind, damaged ends; the
// pieces (data, tone, gap, signal, info) and the codings; the encoders; replaceChunks and TZX's jumps and loops;
// staging under fs::Disc transactions; the checked save (.bak, gzip kept, read back, verify veto). QtCore only.
//
//   tapetest [SAMPLE ...]           the tests, then every sample file or folder given (real tapes: each opened, saved
//                                   unchanged byte for byte, and through plain and gzip back to the same chunks)
//   tapetest --dump FILE            the tape as tests/tape_check.py prints it (the independent reader compares)
//   tapetest --write-samples DIR    the hand-made tapes of these tests, for tape_check.py
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdio>
#include <diskwrangler/fs/disc.h>
#include <diskwrangler/tape/gzip.h>
#include <diskwrangler/tape/records.h>
#include <diskwrangler/tape/tapefile.h>

using namespace dw::tape;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", qPrintable(QString(msg))); ++failures; } else printf("  ok: %s\n", qPrintable(QString(msg))); } while (0)

static QByteArray slurp(const QString &p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<missing>"); }
static void spit(const QString &p, const QByteArray &d) { QFile f(p); if (f.open(QIODevice::WriteOnly)) f.write(d); }
static QByteArray le(quint32 v, int n) { QByteArray b; for (int i = 0; i < n; ++i) b.append(char((v >> (8 * i)) & 0xFF)); return b; }
static QByteArray hex(const QByteArray &b) { return QCryptographicHash::hash(b, QCryptographicHash::Sha1).toHex().left(16); }
static QByteArray pattern(int n, int seed) { QByteArray b(n, 0); for (int i = 0; i < n; ++i) b[i] = char((i * 7 + seed * 13 + i / 251) & 0xFF); return b; }

// ----- hand-made tapes, every chunk and block kind

static QByteArray uefChunk(int id, const QByteArray &body) { return le(quint32(id), 2) + le(quint32(body.size()), 4) + body; }

static QByteArray handUef() {
    QByteArray f("UEF File!", 10);
    f.append(char(10)).append(char(0));                                  // 0.10
    f += uefChunk(0x0000, QByteArray("Made by tapetest\0", 17));
    f += uefChunk(0x0005, QByteArray(1, '\x20'));                         // BBC B
    f += uefChunk(0x0110, le(1500, 2));                                   // carrier
    f += uefChunk(0x0100, "HELLO");                                       // data
    f += uefChunk(0x0100, " WORLD");                                      // continues the same run
    f += uefChunk(0x0111, le(220, 2) + le(64, 2));                         // carrier, dummy byte, carrier
    f += uefChunk(0x0104, QByteArray("\x08" "E" "\x01", 3) + "8E1");     // 8E1
    f += uefChunk(0x0113, dw::tape::uef::putFloat(1300.0));               // base frequency: breaks the run (coding differs)
    f += uefChunk(0x0104, QByteArray("\x08" "E" "\x01", 3) + "more");
    f += uefChunk(0x0104, QByteArray("\x07" "O" "\xFE", 3) + "7O2+");     // negative stop count: extra wave
    f += uefChunk(0x0113, dw::tape::uef::putFloat(1200.0));
    f += uefChunk(0x0112, le(600, 2));                                    // 0.25 s
    f += uefChunk(0x0116, dw::tape::uef::putFloat(1.5f));
    f += uefChunk(0x0114, le(14, 3) + "WW" + QByteArray("\x9D\x2C", 2));  // the specification's example
    f += uefChunk(0x0115, le(90, 2));                                     // phase: not a run of its own after a gap
    // &0102: the byte 'A' framed 8N1 between idle 1s: 1111 0 10000010 1 111 -> 16 bits, first byte says 8+0 unused
    {
        // bits in order: 1,1,1,1, 0, 1,0,0,0,0,0,1,0, 1, 1,1  (A = 0x41, least significant first: 1,0,0,0,0,0,1,0)
        const int bits[16] = {1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1};
        QByteArray b(2, 0);
        for (int i = 0; i < 16; ++i) if (bits[i]) b[i / 8] = char(uchar(b[i / 8]) | (1 << (i % 8)));
        f += uefChunk(0x0102, QByteArray(1, char(8)) + b);
    }
    f += uefChunk(0x0117, le(300, 2));
    f += uefChunk(0x0100, "slow");
    f += uefChunk(0x0101, "slow-multiplexed");                            // belongs to the run before it
    f += uefChunk(0x0105, "ATOM");
    f += uefChunk(0x0120, QByteArray("marker\0", 7));
    f += uefChunk(0x0201, QByteArray("\x00" "disc side", 10));
    f += uefChunk(0xFF00, QByteArray("tapetest\0", 9));
    return f;
}

static QByteArray tzxBlock(int id, const QByteArray &body) { return QByteArray(1, char(id)) + body; }

static QByteArray handTzx(int minor = 20) {
    QByteArray f("ZXTape!\x1A", 8);
    f.append(char(1)).append(char(minor));
    f += tzxBlock(0x32, le(9, 2) + QByteArray(1, 1) + QByteArray(1, 0) + QByteArray(1, 6) + "Hand-m");   // archive info
    f += tzxBlock(0x33, QByteArray(1, 1) + QByteArray("\x00\x15\x00", 3));                            // hardware: CPC 464
    f += tzxBlock(0x10, le(1000, 2) + le(19, 2) + QByteArray(1, 0) + QByteArray("\x03HELLO     ", 11) + QByteArray(7, '\x01'));
    f += tzxBlock(0x11, le(2168, 2) + le(667, 2) + le(735, 2) + le(855, 2) + le(1710, 2) + le(3223, 2) + QByteArray(1, 8) + le(0, 2) + le(5, 3) + "\xFFturb");
    f += tzxBlock(0x14, le(855, 2) + le(1710, 2) + QByteArray(1, 6) + le(500, 2) + le(3, 3) + "pur");
    f += tzxBlock(0x12, le(2168, 2) + le(4000, 2));
    f += tzxBlock(0x13, QByteArray(1, 2) + le(667, 2) + le(735, 2));
    f += tzxBlock(0x15, le(79, 2) + le(0, 2) + QByteArray(1, 8) + le(2, 3) + "\xAA\x55");
    f += tzxBlock(0x18, le(10 + 3, 4) + le(0, 2) + le(44100, 3) + QByteArray(1, 1) + le(3, 4) + "\x01\x02\x03");
    f += tzxBlock(0x19, le(14, 4) + le(0, 2) + le(0, 4) + QByteArray(2, 0) + le(0, 4) + QByteArray(2, 0));
    f += tzxBlock(0x20, le(0, 2));                                         // stop the tape
    f += tzxBlock(0x21, QByteArray(1, 5) + "group");
    f += tzxBlock(0x23, le(2, 2));                                         // jump: skip one block (-> the 24)
    f += tzxBlock(0x22, QByteArray());
    f += tzxBlock(0x24, le(3, 2));                                         // loop 3 times
    f += tzxBlock(0x12, le(1000, 2) + le(10, 2));
    f += tzxBlock(0x25, QByteArray());
    f += tzxBlock(0x26, le(1, 2) + le(quint32(qint16(3)), 2));             // call the block 3 on (the 27)
    f += tzxBlock(0x28, le(8, 2) + QByteArray(1, 1) + le(quint32(qint16(-14)), 2) + QByteArray(1, 4) + "Back");   // select
    f += tzxBlock(0x2A, le(0, 4));
    f += tzxBlock(0x27, QByteArray());
    f += tzxBlock(0x2B, le(1, 4) + QByteArray(1, 1));
    f += tzxBlock(0x30, QByteArray(1, 4) + "text");
    f += tzxBlock(0x31, QByteArray(1, 5) + QByteArray(1, 3) + "msg");
    f += tzxBlock(0x34, QByteArray(8, '\x01'));
    f += tzxBlock(0x35, QByteArray("Instructions    ", 16) + le(2, 4) + "hi");   // CHAR[10] in hex: 16
    f += tzxBlock(0x40, QByteArray(1, 1) + le(3, 3) + "SNA");
    f += tzxBlock(0x16, le(4 + 3, 4) + "c64");                             // the whole block's length first
    f += tzxBlock(0x17, le(4 + 2, 4) + "64");
    f += tzxBlock(0x5A, QByteArray("XTape!\x1A\x01\x14", 9));
    f += tzxBlock(0x4B, le(3, 4) + "new");                                 // a later block: the extension rule
    return f;
}

static QByteArray handTap() {
    QByteArray f;
    const QByteArray header = QByteArray(1, 0) + QByteArray(1, 3) + "HELLO     " + le(5, 2) + le(32768, 2) + le(32768, 2);
    QByteArray h = header;
    char x = 0;
    for (char c : h) x ^= c;
    h.append(x);
    f += le(quint32(h.size()), 2) + h;
    QByteArray d = QByteArray(1, char(0xFF)) + "hello";
    x = 0;
    for (char c : d) x ^= c;
    d.append(x);
    f += le(quint32(d.size()), 2) + d;
    return f;
}

// ----- the dump tape_check.py compares

static QString codingFields(const Coding &c) {
    if (c.kind == Coding::Serial)
        return QString("serial %1%2%3 extra=%4 atom=%5 bits=%6 baud=%7 base=%8").arg(c.dataBits).arg(QChar(c.parity))
            .arg(c.stopBits).arg(int(c.extraWave)).arg(int(c.atom)).arg(int(c.explicitBits)).arg(c.baud).arg(c.baseHz, 0, 'f', 3);
    if (c.kind == Coding::PulseWidth)
        return QString("pulse std=%1 pilot=%2x%3 sync=%4,%5 bit=%6,%7 used=%8 pause=%9").arg(int(c.standard)).arg(c.pilot)
            .arg(c.pilotPulses).arg(c.sync1).arg(c.sync2).arg(c.zero).arg(c.one).arg(c.usedBits).arg(c.pauseMs);
    return "none";
}

static QStringList dump(const TapeFile &t) {
    QStringList l;
    l << QString("kind %1 gzip %2 version %3.%4 chunks %5 readonly %6").arg(kindName(t.kind())).arg(int(t.gzip()))
             .arg(t.majorVersion()).arg(t.minorVersion()).arg(t.chunks().size()).arg(int(!t.readOnlyWhy().isEmpty()));
    for (int i = 0; i < t.chunks().size(); ++i) {
        const Chunk &c = t.chunks()[i];
        l << QString("chunk %1 id %2 len %3 %4").arg(i).arg(c.id).arg(c.body.size()).arg(QString(hex(c.body)));
    }
    const QVector<Piece> ps = pieces(t);
    for (int i = 0; i < ps.size(); ++i) {
        const Piece &p = ps[i];
        QString s = QString("piece %1 %2 %3-%4").arg(i).arg(typeName(p.type)).arg(p.first).arg(p.last);
        if (p.type == Piece::Data)
            s += QString(" bytes %1 %2 %3%4").arg(p.bytes.size()).arg(QString(hex(p.bytes))).arg(codingFields(p.coding))
                     .arg(p.problem.isEmpty() ? "" : " problem");
        if (p.type == Piece::Tone || p.type == Piece::Gap) s += QString(" seconds %1").arg(p.seconds, 0, 'f', 6);
        if (p.stop) s += " stop";
        l << s;
    }
    return l;
}

// ----- real samples

static void sample(const QString &path) {
    TapeFile t;
    QString err;
    const QByteArray file = slurp(path);
    if (!t.open(path, &err)) { CHECK(false, path + ": " + err); return; }
    const QString n = QFileInfo(path).fileName();
    CHECK(t.serialize() == file, n + ": opened and written unchanged, byte for byte");
    CHECK(t.readOnlyWhy().isEmpty(), n + ": the whole file reads as chunks");
    // Through the other kind (plain <-> gzip) and back: the same chunks.
    if (t.kind() == Kind::Uef) {
        TapeFile o;
        CHECK(o.openBytes(t.serialize(!t.gzip()), path, "uef", &err) && o.chunks() == t.chunks() && o.gzip() != t.gzip(),
              n + ": the same tape " + (t.gzip() ? "uncompressed" : "compressed by this program"));
    }
    const QVector<Piece> ps = pieces(t);
    int covered = 0, data = 0;
    bool contiguous = true;
    for (const Piece &p : ps) {
        if (p.first != covered) contiguous = false;
        covered = p.last + 1;
        if (p.type == Piece::Data) ++data;
    }
    CHECK(contiguous && covered == t.chunks().size(), n + ": the pieces cover every chunk, in order");
    CHECK(data > 0, n + QString(": %1 data pieces").arg(data));
}

static void samples(const QString &arg) {
    QFileInfo fi(arg);
    if (fi.isDir()) {
        const QStringList names = QDir(arg).entryList(QStringList() << "*.uef" << "*.UEF" << "*.tzx" << "*.TZX" << "*.cdt" << "*.CDT" << "*.tap" << "*.TAP", QDir::Files, QDir::Name);
        printf("samples in %s: %d\n", qPrintable(arg), int(names.size()));
        for (const QString &n : names) sample(QDir(arg).filePath(n));
    } else sample(arg);
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    if (args.size() == 2 && args[0] == "--dump") {
        TapeFile t;
        QString err;
        if (!t.open(args[1], &err)) { printf("error %s\n", qPrintable(err)); return 1; }
        for (const QString &s : dump(t)) printf("%s\n", qPrintable(s));
        return 0;
    }
    if (args.size() == 2 && args[0] == "--write-samples") {
        QDir().mkpath(args[1]);
        const QString d = args[1];
        spit(d + "/hand.uef", handUef());
        spit(d + "/hand-gzip.uef", gzip(handUef()));
        spit(d + "/hand-two-members.uef", gzip(handUef().left(400)) + gzip(handUef().mid(400)));
        spit(d + "/hand.tzx", handTzx());
        spit(d + "/hand-113.cdt", handTzx(13));
        spit(d + "/hand.tap", handTap());
        spit(d + "/hand-cut.uef", handUef().left(handUef().size() - 3));
        spit(d + "/hand-cut.tzx", handTzx().left(handTzx().size() - 2));
        return 0;
    }

    QTemporaryDir tmp;
    tmp.setAutoRemove(qEnvironmentVariableIsEmpty("TAPETEST_KEEP"));
    const QString d = tmp.path();
    if (!tmp.autoRemove()) printf("files in %s\n", qPrintable(d));
    QString err;

    printf("gzip\n");
    {
        for (const QByteArray &x : {QByteArray(), QByteArray("a"), pattern(100000, 3), QByteArray(70000, 'z'), handUef()}) {
            QByteArray out;
            const QByteArray g = gzip(x);
            CHECK(isGzip(g) && gunzip(g, &out, &err) && out == x, QString("gzip of %1 bytes and back").arg(x.size()));
        }
        CHECK(gzip(pattern(5000, 1)) == gzip(pattern(5000, 1)), "the same data gives the same bytes (no time stamp)");
        // A stored block, and fixed codes ("a" deflates to 4B 04 00), wrapped by hand.
        auto wrap = [](const QByteArray &deflate, const QByteArray &plain) {
            return QByteArray("\x1F\x8B\x08\x00\x00\x00\x00\x00\x00\xFF", 10) + deflate + le(crc32(plain), 4) + le(quint32(plain.size()), 4);
        };
        QByteArray out;
        CHECK(gunzip(wrap(QByteArray("\x01\x05\x00\xFA\xFF", 5) + "hello", "hello"), &out, &err) && out == "hello", "a stored block");
        CHECK(gunzip(wrap(QByteArray("\x4B\x04\x00", 3), "a"), &out, &err) && out == "a", "fixed codes");
        CHECK(crc32("123456789") == 0xCBF43926u, "CRC-32 check value");
        // A file name and comment in the header are skipped.
        QByteArray named = gzip("named");
        named[3] = char(8 | 16);
        named.insert(10, QByteArray("x.uef\0note\0", 11));
        CHECK(gunzip(named, &out, &err) && out == "named", "a header with a name and a comment");
        CHECK(gunzip(gzip("ab") + gzip("cd") + QByteArray(8, '\0'), &out, &err) && out == "abcd", "two members, zeros after");
        QByteArray bad = gzip(pattern(20000, 5));
        bad[bad.size() - 6] = char(bad[bad.size() - 6] ^ 1);              // the CRC
        CHECK(!gunzip(bad, &out, &err) && err.contains("CRC"), "a wrong CRC is refused: " + err);
        bad = gzip(pattern(20000, 5));
        CHECK(!gunzip(bad.left(bad.size() / 2), &out, &err), "a file cut in half is refused: " + err);
        bad = gzip(pattern(20000, 5));
        for (int i = 40; i < 60; ++i) bad[i] = char(0xFF);
        CHECK(!gunzip(bad, &out, &err), "damaged compressed data is refused: " + err);
        CHECK(!gunzip(gzip(QByteArray(5000, 'q')), &out, &err, 1000) && err.contains("more than"), "a size limit: " + err);
        CHECK(!gunzip("plain", &out, &err), "not gzip");
    }

    printf("identify\n");
    {
        bool gz = true;
        CHECK(TapeFile::identify(handUef(), "uef", &gz) == Kind::Uef && !gz, "UEF");
        CHECK(TapeFile::identify(gzip(handUef()), "", &gz) == Kind::Uef && gz, "UEF in gzip, whatever the suffix");
        CHECK(TapeFile::identify(handTzx(), "cdt") == Kind::Tzx, "TZX (a .cdt)");
        CHECK(TapeFile::identify(handTap(), "tap") == Kind::Tap, "TAP with its suffix");
        CHECK(TapeFile::identify(handTap(), "img") == Kind::None, "TAP without it: nothing says it is one");
        CHECK(TapeFile::identify(handTap() + "x", "tap") == Kind::None, "TAP whose blocks don't end at the end");
        CHECK(TapeFile::identify(gzip(handTzx()), "tzx") == Kind::None, "gzip of something other than UEF");
        CHECK(TapeFile::identify(QByteArray(400, '\0'), "uef") == Kind::None, "zeros");
    }

    printf("UEF: every chunk, written back\n");
    {
        TapeFile t;
        CHECK(t.openBytes(handUef(), d + "/h.uef", "uef", &err), "open: " + err);
        CHECK(t.kind() == Kind::Uef && t.majorVersion() == 0 && t.minorVersion() == 10, "version 0.10");
        CHECK(t.chunks().size() == 23 && t.readOnlyWhy().isEmpty(), QString("23 chunks (%1)").arg(t.chunks().size()));
        CHECK(t.serialize() == handUef(), "unchanged: the same bytes");
        TapeFile g;
        CHECK(g.openBytes(gzip(handUef()), d + "/g.uef", "uef", &err) && g.gzip() && g.chunks() == t.chunks(), "gzip: the same chunks");
        const QVector<Piece> ps = pieces(t);
        QStringList types;
        for (const Piece &p : ps) types << typeName(p.type) + QString("%1-%2").arg(p.first).arg(p.last);
        printf("    %s\n", qPrintable(types.join(' ')));
        CHECK(types.join(' ') == "info0-0 info1-1 tone2-2 data3-4 tone5-5 data6-7 data8-8 data9-10 gap11-11 gap12-12 tone13-13 "
                                 "info14-14 data15-16 data17-18 data19-19 info20-20 info21-21 info22-22",
              "the pieces: runs, modal chunks inside them, everything covered");
        CHECK(ps[3].bytes == "HELLO WORLD" && ps[3].coding.describe() == "8N1 1200 baud", "&0100 run: " + ps[3].coding.describe());
        CHECK(ps[5].bytes == "8E1" && ps[5].coding.parity == 'E' && ps[5].coding.baseHz == 1200, "&0104 8E1");
        CHECK(ps[6].bytes == "more" && ps[6].coding.baseHz == 1300, "a base frequency change starts a new run: " + ps[6].coding.describe());
        CHECK(ps[7].coding.dataBits == 7 && ps[7].coding.stopBits == 2 && ps[7].coding.extraWave, "&0104 7O2 with the extra wave: " + ps[7].coding.describe());
        CHECK(qAbs(ps[8].seconds - 0.25) < 1e-9, QString("&0112 600: %1 s").arg(ps[8].seconds));
        CHECK(qAbs(ps[9].seconds - 1.5) < 1e-9, "&0116 1.5 s");
        // 9D 2C, 14 cycles, the first the most significant bit, 0 slow (1/1200 s) and 1 fast (1/2400 s), as the
        // specification's text says: 1001110100101100 -> 6 slow, 8 fast. (Its example reads it the other way round.)
        CHECK(qAbs(ps[10].seconds - (6 / 1200.0 + 8 / 2400.0)) < 1e-9, QString("&0114 by the specification's text: %1 s").arg(ps[10].seconds));
        CHECK(qAbs(ps[4].seconds - (284 / 2400.0 + 10 / 1200.0)) < 1e-9, "&0111: carrier, dummy byte, carrier");
        CHECK(ps[12].bytes == "A" && ps[12].coding.explicitBits && ps[12].problem.isEmpty(), "&0102 framed into bytes");
        CHECK(ps[13].bytes == "slow" && ps[13].coding.baud == 300 && ps[13].last == 18, "&0117 300 baud; &0101 belongs to the run");
        CHECK(ps[14].coding.atom, "&0105 Atom");
        CHECK(chunkName(Kind::Uef, 0x0114) == "security cycles" && chunkName(Kind::Uef, 0x0201) == "disc", "chunk names");
        CHECK(qAbs(uef::getFloat(uef::putFloat(1201.5), 0) - 1201.5) < 1e-3, "UEF floats");

        // A file cut inside its last chunk: the rest kept, read-only.
        TapeFile c;
        const QByteArray cut = handUef().left(handUef().size() - 3);
        CHECK(c.openBytes(cut, d + "/c.uef", "uef", &err) && !c.readOnlyWhy().isEmpty() && c.chunks().last().id == Chunk::Tail,
              "cut short: a damaged end, read-only (" + c.readOnlyWhy() + ")");
        CHECK(c.serialize() == cut, "... written back as it was");
        CHECK(!c.replaceChunks(0, 1, {}, &err), "... and refuses changes: " + err);
    }

    printf("TZX / CDT: every block, written back\n");
    {
        TapeFile t;
        CHECK(t.openBytes(handTzx(), d + "/h.tzx", "tzx", &err), "open: " + err);
        CHECK(t.chunks().size() == 31 && t.readOnlyWhy().isEmpty(), QString("31 blocks (%1)").arg(t.chunks().size()));
        CHECK(t.serialize() == handTzx(), "unchanged: the same bytes");
        TapeFile old;
        CHECK(old.openBytes(handTzx(13), d + "/h.cdt", "cdt", &err) && old.minorVersion() == 13, "TZX 1.13 (an old CDT)");
        const QVector<Piece> ps = pieces(t);
        CHECK(ps.size() == 31, "one piece per block");
        CHECK(ps[2].type == Piece::Data && ps[2].coding.standard && ps[2].coding.pilotPulses == 8063 && ps[2].bytes.size() == 19, "10: standard, header pilot");
        CHECK(ps[3].type == Piece::Data && ps[3].coding.zero == 855 && ps[3].coding.pilotPulses == 3223 && ps[3].bytes == "\xFFturb", "11: turbo, its timings");
        CHECK(ps[4].type == Piece::Data && ps[4].coding.usedBits == 6 && ps[4].coding.pauseMs == 500, "14: pure data");
        CHECK(ps[5].type == Piece::Tone && qAbs(ps[5].seconds - 2168.0 * 4000 / 3500000) < 1e-9, "12: pure tone");
        CHECK(ps[6].type == Piece::Tone && qAbs(ps[6].seconds - 1402.0 / 3500000) < 1e-12, "13: pulses");
        CHECK(ps[7].type == Piece::Signal && ps[8].type == Piece::Signal && ps[9].type == Piece::Signal, "15, 18, 19: signal");
        CHECK(ps[10].type == Piece::Gap && ps[10].stop, "20 with 0: stop the tape");
        CHECK(ps[27].type == Piece::Signal && ps[28].type == Piece::Signal, "16, 17 (C64): signal");
        CHECK(ps[30].type == Piece::Info && t.chunks()[30].body == le(3, 4) + "new", "a block the specification doesn't have: by the extension rule");
        CHECK(TapeFile::tzxOffsets(t.chunks()[12]) == QVector<int>{2}, "23: its jump");
        CHECK(TapeFile::tzxOffsets(t.chunks()[17]) == QVector<int>{3}, "26: its call");
        CHECK(TapeFile::tzxOffsets(t.chunks()[18]) == QVector<int>{-14}, "28: its selection");

        // Changes before the blocks that point, between them and their targets, and refusals.
        TapeFile e;
        e.openBytes(handTzx(), d + "/e.tzx", "tzx", &err);
        CHECK(e.replaceChunks(2, 0, {tzx::standard("new"), tzx::pause(100)}, &err), "two blocks put in near the start");
        CHECK(TapeFile::tzxOffsets(e.chunks()[14]) == QVector<int>{2} && TapeFile::tzxOffsets(e.chunks()[20]) == QVector<int>{-14},
              "jumps across nothing changed keep their counts");
        e.discardPending();
        CHECK(e.replaceChunks(13, 1, {tzx::pause(5), tzx::pause(6)}, &err), "the block a jump skips becomes two");
        CHECK(TapeFile::tzxOffsets(e.chunks()[12]) == QVector<int>{3}, "... the jump still lands on the loop start");
        CHECK(TapeFile::tzxOffsets(e.chunks()[19]) == QVector<int>{-15}, "... and the selection on its block");
        e.discardPending();
        CHECK(!e.replaceChunks(15, 1, {}, &err), "a block inside a loop can't go: " + err);
        CHECK(!e.replaceChunks(14, 1, {}, &err), "a loop start without its end can't go: " + err);
        CHECK(!e.replaceChunks(16, 0, {tzx::pause(1)}, &err), "nothing can be put inside a loop: " + err);
        CHECK(e.replaceChunks(14, 3, {}, &err), "a whole loop can go: " + err);
        CHECK(TapeFile::tzxOffsets(e.chunks()[12]) == QVector<int>{2}, "... a jump to its start lands where it was");
        e.discardPending();
        CHECK(!e.replaceChunks(3, 2, {}, &err), "the block a selection points into can't go from the middle of a change: " + err);
        CHECK(!e.hasPending(), "a refusal changes nothing");

        TapeFile c;
        const QByteArray cut = handTzx().left(handTzx().size() - 2);
        CHECK(c.openBytes(cut, d + "/c.tzx", "tzx", &err) && c.chunks().last().id == Chunk::Tail && c.serialize() == cut,
              "a block cut short: kept as it was, read-only (" + c.readOnlyWhy() + ")");
        QByteArray v2 = handTzx();
        v2[8] = 2;
        CHECK(!c.openBytes(v2, d + "/v2.tzx", "tzx", &err), "TZX 2.x is refused: " + err);
    }

    printf("TAP\n");
    {
        TapeFile t;
        CHECK(t.openBytes(handTap(), d + "/h.tap", "tap", &err) && t.chunks().size() == 2 && t.serialize() == handTap(), "two blocks, written back");
        const QVector<Piece> ps = pieces(t);
        CHECK(ps.size() == 2 && ps[0].coding.pilotPulses == 8063 && ps[1].coding.pilotPulses == 3223 && ps[1].bytes.startsWith('\xFF'),
              "the standard coding: header and data pilots");
        CHECK(!t.openBytes(handTap(), d + "/h.img", "img", &err), "not a TAP without its suffix: " + err);
    }

    printf("encoders\n");
    {
        TapeFile t;
        t.createBlank(Kind::Uef, true);
        CHECK(t.hasPending() && t.minorVersion() == 10, "a new UEF is 0.10 and is saved even when empty");
        CHECK(t.setChunks({uef::text(0, "origin"), uef::carrierWithDummy(4919, 600), uef::data("ABC"), uef::carrier(600),
                           uef::data("DEF", 8, 'E', 1), uef::gap(2.0), uef::baseFrequency(1250)}, &err), "set chunks: " + err);
        const QVector<Piece> ps = pieces(t);
        CHECK(ps.size() == 7 && ps[2].bytes == "ABC" && ps[4].bytes == "DEF" && ps[4].coding.parity == 'E' && qAbs(ps[5].seconds - 2) < 1e-9,
              "what the encoders made reads back");
        CHECK(t.chunks()[0].body == QByteArray("origin\0", 7), "text is zero-terminated");
        TapeFile z;
        z.createBlank(Kind::Tzx, false);
        Coding c = tzx::standardCoding("\xFFx", 0);
        c.standard = false;
        c.zero = 700; c.one = 1400;
        CHECK(z.setChunks({tzx::standard(QByteArray("\x00" "head", 5), 1000), tzx::turbo("\xFF" "data", c), tzx::pause(0)}, &err), "TZX blocks");
        TapeFile back;
        CHECK(back.openBytes(z.serialize(), d + "/z.tzx", "tzx", &err) && back.chunks() == z.chunks(), "TZX made here reads back");
        const QVector<Piece> zp = pieces(back);
        CHECK(zp[1].coding.zero == 700 && zp[1].coding.pilotPulses == 3223 && zp[1].bytes == "\xFF" "data", "turbo coding kept");
    }

    printf("staging under fs::Disc\n");
    {
        spit(d + "/s.uef", gzip(handUef()));
        TapeFile t;
        CHECK(t.open(d + "/s.uef", &err), "open");
        dw::fs::Disc disc;
        disc.setStaged(&t);
        CHECK(disc.staged() == &t && !disc.container() && !disc.dirty(), "a disc on a tape, nothing staged");
        CHECK(!disc.transact([&](QString *e) { t.replaceChunks(0, 1, {}, e); *e = "failed later"; return false; }, &err) &&
                  !t.hasPending() && t.chunks().size() == 23, "a failed transaction puts the chunks back");
        CHECK(disc.transact([&](QString *e) { return t.replaceChunks(0, 1, {uef::text(0, "changed")}, e); }, &err) && disc.dirty(),
              "a transaction that worked stays staged");
        t.setGzip(false);
        auto st = t.stagedState();
        t.setGzip(true);
        t.restoreStaged(st);
        CHECK(!t.gzip(), "the gzip choice is staged too");
        disc.revert();
        CHECK(!t.hasPending() && t.gzip(), "revert drops everything");
    }

    printf("saving\n");
    {
        const QString p = d + "/save.uef";
        spit(p, gzip(handUef()));
        const QByteArray orig = slurp(p);
        TapeFile t;
        CHECK(t.open(p, &err), "open");
        TapeFile::SaveOptions o;
        o.backup = true;
        CHECK(t.replaceChunks(0, 1, {uef::text(0, "saved")}, &err), "a change");
        int verified = 0;
        o.verify = [&](TapeFile &w, QString *) { ++verified; return w.chunks()[0].body == QByteArray("saved\0", 6); };
        CHECK(t.commit(o, &err), "commit: " + err);
        CHECK(verified == 1 && slurp(p + ".bak") == orig, "verify saw the new file; .bak is the original");
        TapeFile r;
        CHECK(r.open(p, &err) && r.gzip() && r.chunks() == t.chunks() && !t.hasPending(), "saved compressed, as it came");
        CHECK(QDir(d).entryList(QStringList() << "*.dwsave-*", QDir::Files | QDir::Hidden).isEmpty(), "no temporary file left");
        // A veto leaves the file alone.
        const QByteArray before = slurp(p);
        t.replaceChunks(0, 1, {uef::text(0, "vetoed")}, &err);
        o.backup = false;
        o.verify = [](TapeFile &, QString *e) { *e = "no"; return false; };
        CHECK(!t.commit(o, &err) && slurp(p) == before && t.hasPending(), "a verify veto: nothing written, still staged");
        o.verify = {};
        o.validate = [](QString *e) { *e = "invalid"; return false; };
        CHECK(!t.commit(o, &err) && err == "invalid" && slurp(p) == before, "a validate veto");
        // Save As plain, then gzip again.
        CHECK(t.saveAs(d + "/plain.uef", false, &err) && !isGzip(slurp(d + "/plain.uef")) && t.path() == d + "/plain.uef", "Save As uncompressed");
        CHECK(slurp(p) == before, "... the original left alone");
        TapeFile q;
        CHECK(q.open(d + "/plain.uef", &err) && !q.gzip() && q.chunks()[0].body == QByteArray("vetoed\0", 7), "... holds the tape");
        q.setGzip(true);
        CHECK(q.hasPending() && q.commit(TapeFile::SaveOptions(), &err) && isGzip(slurp(d + "/plain.uef")), "setGzip then save: compressed");
        // An unchanged plain file saved is byte for byte the same.
        spit(d + "/same.uef", handUef());
        TapeFile s;
        s.open(d + "/same.uef", &err);
        s.markForSave();
        CHECK(s.commit(TapeFile::SaveOptions(), &err) && slurp(d + "/same.uef") == handUef(), "an unchanged save: the same bytes");
        // Read-only.
        spit(d + "/cut.uef", handUef().left(handUef().size() - 3));
        TapeFile c;
        c.open(d + "/cut.uef", &err);
        c.markForSave();
        CHECK(!c.commit(TapeFile::SaveOptions(), &err) && !c.saveAs(d + "/cut2.uef", false, &err), "a damaged tape can't be saved: " + err);
        // fs::Disc::commitWith: checks first.
        TapeFile w;
        w.open(d + "/same.uef", &err);
        dw::fs::Disc disc;
        disc.setStaged(&w);
        w.replaceChunks(0, 1, {uef::text(0, "with")}, &err);
        CHECK(disc.commitWith([&](QString *e) { return w.commit(TapeFile::SaveOptions(), e); }, &err) && !w.hasPending(), "Disc::commitWith");
    }

    for (const QString &a : args) samples(a);

    printf(failures ? "\n%d FAILED\n" : "\nALL PASSED\n", failures);
    return failures ? 1 : 0;
}
