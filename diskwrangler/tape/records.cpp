/*
 * What a tape's chunks mean - part of DiskWranglerCore
 * Copyright (C) 2026 Daniel Jameson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "records.h"
#include <QtEndian>
#include <cstring>

namespace dw {
namespace tape {

namespace {

const double TStates = 3500000.0;                        // TZX timings are in T-states of a 3.5 MHz Z80

quint32 le(const QByteArray &b, int at, int n) {
    quint32 v = 0;
    for (int i = 0; i < n && at + i < b.size(); ++i) v |= quint32(uchar(b[at + i])) << (8 * i);
    return v;
}

void putLe(QByteArray *b, quint32 v, int n) {
    for (int i = 0; i < n; ++i) b->append(char((v >> (8 * i)) & 0xFF));
}

// &0102: bits as stored, least significant first, framed here into 8N1 bytes. The first byte says how many bits of
// the chunk are unused: (length * 8) - first are used (UEF 0.10), never more than follow the first byte. Leading and
// trailing 1s are idle line; a start bit without a stop bit is a framing error.
QByteArray frameBits(const QByteArray &body, QString *problem) {
    QByteArray out;
    if (body.isEmpty()) return out;
    const qint64 avail = qint64(body.size() - 1) * 8;
    const qint64 n = qMin(avail, qint64(body.size()) * 8 - uchar(body[0]));
    auto bit = [&](qint64 i) { return (uchar(body.constData()[1 + i / 8]) >> (i % 8)) & 1; };
    qint64 i = 0;
    while (i < n) {
        if (bit(i)) { ++i; continue; }                   // idle
        if (i + 10 > n) { *problem = QString("a byte cut short at bit %1").arg(i); break; }
        int v = 0;
        for (int k = 0; k < 8; ++k) v |= bit(i + 1 + k) << k;
        if (!bit(i + 9)) { *problem = QString("no stop bit at bit %1").arg(i + 9); break; }
        out.append(char(v));
        i += 10;
    }
    return out;
}

// &0114: the cycles' bits, the first cycle the most significant bit, 0 a cycle of the base frequency and 1 of twice
// it (the specification's text). Its worked example (LSLLLSSLSSLLSL as 9D 2C) only fits the other way round -
// least significant first, 1 a slow cycle - so the two disagree; the text is followed. Only a tone's length depends
// on it, never data.
double securitySeconds(const QByteArray &b, double base) {
    const qint64 n = le(b, 0, 3);
    double s = 0;
    for (qint64 i = 0; i < n && 5 + i / 8 < b.size(); ++i) {
        const int bit = (uchar(b[5 + int(i / 8)]) >> (7 - i % 8)) & 1;   // most significant first
        s += bit ? 1 / (2 * base) : 1 / base;
    }
    return s;
}

bool isModal(int id) { return id == 0x0113 || id == 0x0115 || id == 0x0117 || id == 0x0101 || id == 0x0103; }

QVector<Piece> uefPieces(const QVector<Chunk> &ch) {
    QVector<Piece> out;
    double base = 1200;
    int baud = 1200;
    bool open = false;                                   // the last piece is a Data run that may go on
    auto addData = [&](int i, const QByteArray &bytes, const Coding &c, const QString &problem) {
        if (open && out.last().coding == c && out.last().problem.isEmpty() && problem.isEmpty()) {
            out.last().bytes += bytes;
            out.last().last = i;
            return;
        }
        Piece p;
        p.type = Piece::Data;
        p.first = p.last = i;
        p.bytes = bytes;
        p.coding = c;
        p.problem = problem;
        out.append(p);
        open = true;
    };
    for (int i = 0; i < ch.size(); ++i) {
        const Chunk &k = ch[i];
        const QByteArray &b = k.body;
        Coding c;
        c.kind = Coding::Serial;
        c.baud = baud;
        c.baseHz = base;
        if (k.id == 0x0113 && b.size() >= 4) base = uef::getFloat(b, 0);
        else if (k.id == 0x0117 && b.size() >= 2) baud = int(le(b, 0, 2));
        if (isModal(k.id)) {
            if (open) { out.last().last = i; continue; }
            Piece p;
            p.type = Piece::Info;
            p.first = p.last = i;
            out.append(p);
            continue;
        }
        switch (k.id) {
        case 0x0100: addData(i, b, c, QString()); continue;
        case 0x0102: {
            c.explicitBits = true;
            QString problem;
            const QByteArray bytes = frameBits(b, &problem);
            addData(i, bytes, c, problem);
            continue;
        }
        case 0x0104: {
            if (b.size() < 3) break;
            c.dataBits = uchar(b[0]);
            c.parity = b[1];
            const int stop = qint8(b[2]);
            c.stopBits = stop < 0 ? -stop : stop;
            c.extraWave = stop < 0;
            addData(i, b.mid(3), c, QString());
            continue;
        }
        case 0x0105: c.atom = true; addData(i, b, c, QString()); continue;
        default: break;
        }
        open = false;
        Piece p;
        p.first = p.last = i;
        const int bitCycles = baud == 300 ? 4 : 1;       // cycles of the base frequency in a 0 bit
        switch (k.id) {
        case 0x0110: p.type = Piece::Tone; p.seconds = le(b, 0, 2) / (2 * base); break;
        case 0x0111:
            p.type = Piece::Tone;
            p.seconds = (le(b, 0, 2) + le(b, 2, 2)) / (2 * base) + 10.0 * bitCycles / base;
            break;
        case 0x0114: p.type = Piece::Tone; p.seconds = securitySeconds(b, base); break;
        // &0112: n in units of 1/(2 * base) s. (The specification's text says 1/(2n * base), which gives
        // microseconds for the 600 and 2000 real tapes hold; read as n / (2 * base) they are 0.25 and 0.83 s.)
        case 0x0112: p.type = Piece::Gap; p.seconds = le(b, 0, 2) / (2 * base); break;
        case 0x0116: p.type = Piece::Gap; p.seconds = b.size() >= 4 ? uef::getFloat(b, 0) : 0; break;
        case Chunk::Tail: p.type = Piece::Info; p.problem = "the damaged end of the file"; break;
        default: p.type = Piece::Info; break;
        }
        out.append(p);
    }
    return out;
}

QVector<Piece> tzxPieces(Kind kind, const QVector<Chunk> &ch) {
    QVector<Piece> out;
    for (int i = 0; i < ch.size(); ++i) {
        const Chunk &k = ch[i];
        const QByteArray &b = k.body;
        Piece p;
        p.first = p.last = i;
        Coding c;
        c.kind = Coding::PulseWidth;
        if (kind == Kind::Tap && k.id == 0) {
            p.type = Piece::Data;
            p.bytes = b;
            p.coding = tzx::standardCoding(b, 1000);
        } else if (k.id == Chunk::Tail) {
            p.type = Piece::Info;
            p.problem = "the damaged end of the file";
        } else {
            switch (k.id) {
            case 0x10:
                p.type = Piece::Data;
                p.bytes = b.mid(4);
                p.coding = tzx::standardCoding(p.bytes, int(le(b, 0, 2)));
                break;
            case 0x11:
                p.type = Piece::Data;
                c.pilot = int(le(b, 0, 2)); c.sync1 = int(le(b, 2, 2)); c.sync2 = int(le(b, 4, 2));
                c.zero = int(le(b, 6, 2)); c.one = int(le(b, 8, 2)); c.pilotPulses = int(le(b, 0x0A, 2));
                c.usedBits = uchar(b[0x0C]); c.pauseMs = int(le(b, 0x0D, 2));
                p.bytes = b.mid(0x12);
                p.coding = c;
                break;
            case 0x14:
                p.type = Piece::Data;
                c.zero = int(le(b, 0, 2)); c.one = int(le(b, 2, 2)); c.usedBits = uchar(b[4]); c.pauseMs = int(le(b, 5, 2));
                p.bytes = b.mid(0x0A);
                p.coding = c;
                break;
            case 0x12: p.type = Piece::Tone; p.seconds = double(le(b, 0, 2)) * le(b, 2, 2) / TStates; break;
            case 0x13: {
                p.type = Piece::Tone;
                double t = 0;
                for (int n = 0; n < uchar(b[0]); ++n) t += le(b, 1 + 2 * n, 2);
                p.seconds = t / TStates;
                break;
            }
            case 0x20:
                p.type = Piece::Gap;
                p.seconds = le(b, 0, 2) / 1000.0;
                p.stop = le(b, 0, 2) == 0;
                break;
            case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: p.type = Piece::Signal; break;
            default: p.type = Piece::Info; break;
            }
        }
        out.append(p);
    }
    return out;
}

} // namespace

bool Coding::operator==(const Coding &o) const {
    return kind == o.kind && dataBits == o.dataBits && parity == o.parity && stopBits == o.stopBits &&
           extraWave == o.extraWave && atom == o.atom && explicitBits == o.explicitBits && baud == o.baud &&
           baseHz == o.baseHz && standard == o.standard && pilot == o.pilot && pilotPulses == o.pilotPulses &&
           sync1 == o.sync1 && sync2 == o.sync2 && zero == o.zero && one == o.one && usedBits == o.usedBits &&
           pauseMs == o.pauseMs;
}

QString Coding::describe() const {
    if (kind == Serial) {
        QString s = QString("%1%2%3%4 %5 baud").arg(dataBits).arg(QChar(parity)).arg(stopBits)
                        .arg(extraWave ? "+" : "").arg(baud);
        if (baseHz != 1200) s += QString(", %1 Hz").arg(baseHz, 0, 'f', 1);
        if (atom) s += ", Atom";
        if (explicitBits) s += ", from bits";
        return s;
    }
    if (kind == PulseWidth) {
        if (standard) return QString("Spectrum ROM speed, pause %1 ms").arg(pauseMs);
        return QString("pulse width %1/%2 T-states, pause %3 ms").arg(zero).arg(one).arg(pauseMs);
    }
    return "none";
}

QString typeName(Piece::Type t) {
    switch (t) {
    case Piece::Data: return "data";
    case Piece::Tone: return "tone";
    case Piece::Gap: return "gap";
    case Piece::Signal: return "signal";
    case Piece::Info: return "info";
    }
    return "info";
}

QVector<Piece> pieces(Kind kind, const QVector<Chunk> &chunks) {
    if (kind == Kind::Uef) return uefPieces(chunks);
    if (kind == Kind::Tzx || kind == Kind::Tap) return tzxPieces(kind, chunks);
    return {};
}

QVector<Piece> pieces(const TapeFile &t) { return pieces(t.kind(), t.chunks()); }

QString chunkName(Kind kind, int id) {
    if (id == Chunk::Tail) return "damaged end";
    if (kind == Kind::Tap) return "data block";
    if (kind == Kind::Uef) {
        switch (id) {
        case 0x0000: return "origin";
        case 0x0001: return "instructions";
        case 0x0003: return "inlay scan";
        case 0x0005: return "target machine";
        case 0x0006: return "bit multiplexing";
        case 0x0007: return "extra palette";
        case 0x0008: return "ROM hint";
        case 0x0009: return "short title";
        case 0x000A: return "visible area";
        case 0x0100: return "implicit data";
        case 0x0101: case 0x0103: return "multiplexed data";
        case 0x0102: return "explicit data";
        case 0x0104: return "defined data";
        case 0x0105: return "Atom data";
        case 0x0110: return "carrier tone";
        case 0x0111: return "carrier tone with dummy byte";
        case 0x0112: return "integer gap";
        case 0x0113: return "base frequency";
        case 0x0114: return "security cycles";
        case 0x0115: return "phase change";
        case 0x0116: return "floating point gap";
        case 0x0117: return "data encoding";
        case 0x0120: return "position marker";
        case 0x0130: return "tape set info";
        case 0x0131: return "start of tape side";
        case 0xFF00: return "emulator id";
        default: break;
        }
        if ((id & 0xFF00) == 0x0200) return "disc";
        if ((id & 0xFF00) == 0x0300) return "ROM";
        if ((id & 0xFF00) == 0x0400) return "state snapshot";
        return "unknown";
    }
    switch (id) {
    case 0x10: return "standard speed data";
    case 0x11: return "turbo speed data";
    case 0x12: return "pure tone";
    case 0x13: return "pulse sequence";
    case 0x14: return "pure data";
    case 0x15: return "direct recording";
    case 0x16: return "C64 ROM data";
    case 0x17: return "C64 turbo data";
    case 0x18: return "CSW recording";
    case 0x19: return "generalized data";
    case 0x20: return "pause";
    case 0x21: return "group start";
    case 0x22: return "group end";
    case 0x23: return "jump";
    case 0x24: return "loop start";
    case 0x25: return "loop end";
    case 0x26: return "call sequence";
    case 0x27: return "return";
    case 0x28: return "select block";
    case 0x2A: return "stop if 48K";
    case 0x2B: return "signal level";
    case 0x30: return "text description";
    case 0x31: return "message";
    case 0x32: return "archive info";
    case 0x33: return "hardware type";
    case 0x34: return "emulation info";
    case 0x35: return "custom info";
    case 0x40: return "snapshot";
    case 0x5A: return "glue";
    default: return "unknown";
    }
}

namespace uef {

QByteArray putFloat(double v) {
    const float f = float(v);
    quint32 bits;
    std::memcpy(&bits, &f, 4);
    QByteArray b;
    putLe(&b, bits, 4);
    return b;
}

double getFloat(const QByteArray &b, int at) {
    const quint32 bits = le(b, at, 4);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

Chunk data(const QByteArray &bytes) { return Chunk(0x0100, bytes); }

Chunk data(const QByteArray &bytes, int dataBits, char parity, int stopBits) {
    QByteArray b;
    b.append(char(dataBits));
    b.append(parity);
    b.append(char(stopBits));
    return Chunk(0x0104, b + bytes);
}

Chunk carrier(int cycles) {
    QByteArray b;
    putLe(&b, quint32(cycles), 2);
    return Chunk(0x0110, b);
}

Chunk carrierWithDummy(int before, int after) {
    QByteArray b;
    putLe(&b, quint32(before), 2);
    putLe(&b, quint32(after), 2);
    return Chunk(0x0111, b);
}

Chunk gap(double seconds) { return Chunk(0x0116, putFloat(seconds)); }

Chunk baseFrequency(double hz) { return Chunk(0x0113, putFloat(hz)); }

Chunk text(int id, const QString &s) {
    QByteArray b = s.toLatin1();
    b.replace('\r', "");
    b.append('\0');
    return Chunk(id, b);
}

} // namespace uef

namespace tzx {

Coding standardCoding(const QByteArray &bytes, int pauseMs) {
    Coding c;
    c.kind = Coding::PulseWidth;
    c.standard = true;
    c.pilot = 2168; c.sync1 = 667; c.sync2 = 735; c.zero = 855; c.one = 1710;
    c.pilotPulses = !bytes.isEmpty() && uchar(bytes[0]) < 128 ? 8063 : 3223;
    c.usedBits = 8;
    c.pauseMs = pauseMs;
    return c;
}

Chunk standard(const QByteArray &bytes, int pauseMs) {
    QByteArray b;
    putLe(&b, quint32(pauseMs), 2);
    putLe(&b, quint32(bytes.size()), 2);
    return Chunk(0x10, b + bytes);
}

Chunk turbo(const QByteArray &bytes, const Coding &c) {
    QByteArray b;
    putLe(&b, quint32(c.pilot), 2);
    putLe(&b, quint32(c.sync1), 2);
    putLe(&b, quint32(c.sync2), 2);
    putLe(&b, quint32(c.zero), 2);
    putLe(&b, quint32(c.one), 2);
    putLe(&b, quint32(c.pilotPulses), 2);
    b.append(char(c.usedBits));
    putLe(&b, quint32(c.pauseMs), 2);
    putLe(&b, quint32(bytes.size()), 3);
    return Chunk(0x11, b + bytes);
}

Chunk pause(int ms) {
    QByteArray b;
    putLe(&b, quint32(ms), 2);
    return Chunk(0x20, b);
}

} // namespace tzx

} // namespace tape
} // namespace dw
