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
#pragma once
// The signal layer: a tape's chunks as what they put on the tape - tones, gaps, and data - and the chunks a program
// writes to put data back. QtCore only. It knows the two ways tapes code their bits, not any machine's files:
//
//   - Serial (UEF; Kansas City / CUTS): each byte a start bit (0), its data bits least significant first, maybe a
//     parity bit, and stop bits (1). A 0 is one cycle of the base frequency (1200 Hz), a 1 two cycles of twice it,
//     at "1200 baud"; four and eight cycles at "300 baud". The BBC Micro, Electron and Atom save this way.
//   - Pulse width (TZX, CDT, TAP): a stream of bits, most significant first, no start or stop bits; a bit is one wave
//     of two equal pulses, a 1 twice as long as a 0; a pilot tone and two sync pulses before the data. Lengths in
//     T-states of a 3.5 MHz Z80. The Spectrum, Amstrad CPC, SAM Coupe, Jupiter ACE and Enterprise save this way.
//
// pieces() reads a tape as a list of Pieces covering every chunk in order, each a run of chunks:
//   Data   - bytes, with their coding (and in a UEF, the chunks that only change the base frequency, baud or phase
//            between data chunks belong to the run, as do the multiplexed copies &0101 / &0103);
//   Tone   - carrier or pilot (UEF &0110, &0111, &0114; TZX 12, 13);
//   Gap    - silence (UEF &0112, &0116; TZX 20, whose 0 means "stop the tape" on a Spectrum and no pause on a CPC);
//   Signal - sound this layer can't turn into bytes yet (TZX 15, 16, 17, 18, 19);
//   Info   - everything else: text, archive information, a UEF's origin, a TZX's groups, jumps and loops, and any
//            chunk this code doesn't know. A tape's damaged end (Chunk::Tail) is Info with a problem.
// A program finds its files in the Data pieces and, to change one, replaces the chunks of its pieces (TapeFile::
// replaceChunks) with chunks made by the encoders below.
#include <QByteArray>
#include <QString>
#include <QVector>
#include <diskwrangler/tape/tapefile.h>

namespace dw {
namespace tape {

struct Coding {
    enum Kind { None, Serial, PulseWidth };
    Kind kind = None;
    // Serial
    int dataBits = 8;
    char parity = 'N';                                   // 'N', 'E' or 'O'
    int stopBits = 1;
    bool extraWave = false;                              // &0104 with a negative stop count: a short wave after them
    bool atom = false;                                   // &0105: a short wave after every stop bit but the last
    bool explicitBits = false;                           // &0102: the bits were stored, and framed into bytes here
    int baud = 1200;                                     // 1200 or 300
    double baseHz = 1200;
    // Pulse width (T-states)
    bool standard = false;                               // TZX 10 / TAP: the Spectrum ROM's timings
    int pilot = 0, pilotPulses = 0, sync1 = 0, sync2 = 0, zero = 0, one = 0;
    int usedBits = 8;                                    // bits used in the last byte
    int pauseMs = 0;                                     // silence after the block
    bool operator==(const Coding &o) const;
    bool operator!=(const Coding &o) const { return !(*this == o); }
    QString describe() const;                            // "8N1 1200 baud", "8E1 1200 baud, 1201.6 Hz", "pulse width 855/1710"
};

struct Piece {
    enum Type { Data, Tone, Gap, Signal, Info };
    Type type = Info;
    int first = 0, last = 0;                             // chunks [first, last] of the tape
    QByteArray bytes;                                    // Data
    Coding coding;                                       // Data
    double seconds = 0;                                  // Tone and Gap: how long (a pilot inside a TZX data block isn't counted)
    bool stop = false;                                   // Gap: TZX 20 with 0, "stop the tape"
    QString problem;                                     // something wrong with it (a bit stream that doesn't frame)
};
QString typeName(Piece::Type t);                         // "data", "tone", "gap", "signal", "info"

QVector<Piece> pieces(const TapeFile &t);
// The same for a list of chunks of a tape of this kind.
QVector<Piece> pieces(Kind kind, const QVector<Chunk> &chunks);

// A UEF chunk's name ("implicit data", "carrier tone", ...) or a TZX block's ("standard speed data", ...).
QString chunkName(Kind kind, int id);

// ----- writing chunks
namespace uef {
Chunk data(const QByteArray &bytes);                     // &0100: 8N1
Chunk data(const QByteArray &bytes, int dataBits, char parity, int stopBits);   // &0104
Chunk carrier(int cycles);                               // &0110: cycles of twice the base frequency
Chunk carrierWithDummy(int before, int after);           // &0111: carrier, the dummy byte &AA, carrier
Chunk gap(double seconds);                               // &0116
Chunk baseFrequency(double hz);                          // &0113
Chunk text(int id, const QString &s);                    // &0000 origin, &0001 instructions, &0009 title, &0120 marker
// UEF's 4-byte float (IEEE 754 single, little-endian).
QByteArray putFloat(double v);
double getFloat(const QByteArray &b, int at);
} // namespace uef

namespace tzx {
Chunk standard(const QByteArray &bytes, int pauseMs = 1000);   // block 10
Chunk turbo(const QByteArray &bytes, const Coding &c);         // block 11
Chunk pause(int ms);                                           // block 20
Coding standardCoding(const QByteArray &bytes, int pauseMs);   // what block 10 means
} // namespace tzx

} // namespace tape
} // namespace dw
