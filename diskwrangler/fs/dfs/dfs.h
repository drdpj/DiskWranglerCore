/*
 * Acorn DFS - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
// dfs.h - Acorn DFS (single density, 256-byte sectors, 10 per track) discs, read and written through a
// dw::device::Linear (the disc in its own numbering). The optional DFS module of DiskWranglerCore
// (diskwrangler-dfs.pri, CMake DiskWrangler::Dfs); moved from Acorn Disk Wrangler. QtCore only.
// One Disc object handles one side: a double-sided image gives two independent discs
// (side 0 = drive 0, side 1 = drive 2), each with its own catalogue.
// Watford DFS's 62-file discs are handled too: a second catalogue in sectors 2-3, marked by eight &AA bytes
// where its title would be (see Acorn Disk Wrangler's docs/disc-formats.md).
#pragma once

#include <QChar>
#include <QString>
#include <QVector>
#include <functional>

#include <diskwrangler/device/linear.h>
#include <diskwrangler/fs/volume.h>

namespace dw {
namespace dfs {

typedef fs::Report Report;

const int SectorSize = 256;
const int SectorsPerTrack = 10;
const int MaxFiles = 31;            // one catalogue
const int MaxFilesWatford = 62;     // Watford's 62-file disc: two catalogues
const int MaxName = 7;
const int MaxTitle = 12;

struct File {
    QString name;            // up to 7 characters
    QChar dir = '$';         // single-letter "directory"
    bool locked = false;
    quint32 load = 0, exec = 0, length = 0, start = 0;
    int cat = 0;             // Watford 62-file disc: 0 = first catalogue (sectors 0-1), 1 = second (sectors 2-3)
    quint32 sectors() const { return (length + SectorSize - 1) / SectorSize; }
    QString fullName() const { return QString(dir) + "." + name; }
};

// How the sides are laid out in the image file.
struct Layout {
    bool doubleSided = false;
    bool interleaved = true;          // DSD: track 0 side 0, track 0 side 1, track 1 side 0 ...
    quint32 sectorsPerSide = 0;       // physical capacity of one side (tracks * 10)
};

// Boot option (*OPT 4,n) names, shared with ADFS: 0 off, 1 *LOAD !BOOT, 2 *RUN !BOOT, 3 *EXEC !BOOT.
QString bootOptionName(int opt);
quint8 bcdInc(quint8 v);         // the catalogue's cycle number (BCD), one up

bool validNameChar(QChar c);
bool validName(const QString &name, QString *why);
bool validDir(QChar c);
QString sanitizeName(const QString &hostName);
// Catalogue sanity check used for format detection (sectors = disc size the catalogue claims).
bool plausibleCatalogue(const QByteArray &sector0, const QByteArray &sector1, quint32 *sectors = nullptr);
// Is track0 (at least sectors 0-3 of one side) a Watford 62-file catalogue? sectors = the disc's true size (the
// larger of the two catalogues' claims: Watford lowers the first one's, see Acorn Disk Wrangler's docs/disc-formats.md).
bool watfordCatalogue(const QByteArray &track0, quint32 *sectors = nullptr);

// ----- load and exec addresses
// DFS keeps 18-bit addresses; values with bits 16 and 17 set stand for 0xFFFFxxxx (I/O processor).
inline quint32 extendAddr(quint32 v) { return (v & 0x30000u) == 0x30000u ? (v | 0xFFFC0000u) : v; }
// An address as *INFO shows it (Acorn 1770 DFS, Watford DFS 1.44): 6 hex digits, FF1900 for one with both high bits
// set (the I/O processor), else 001900 / 011900 / 021900. .inf files and the retro-files JSON use 8 digits instead
// (extendAddr: FFFF1900, 00001900).
QString addrText(quint32 v);
// What a user typed for an address: up to 3FFFF, or the I/O processor's FFxxxx (as shown) / FFFFxxxx (as in an .inf
// file), which mean 3xxxx. False for anything else.
bool parseAddr(const QString &text, quint32 *out);

// ----- Acorn's Z80 CP/M discs
// Acorn's CP/M writes a DFS catalogue holding one locked file, $.CPMDISC, from sector 2 to the end of side 0, so that
// DFS leaves the disc alone. Such a disc is CP/M Disk Wrangler's. (Moved from CP/M Disk Wrangler's
// acornDfsCatalogue.)
QByteArray cpmFenceCatalogue(int sectorsPerSide);       // sectors 0-1 (512 bytes)
bool isCpmFence(const QByteArray &first512);           // one file, $.CPMDISC

// ----- copying DFS files to a filesystem with directories (ADFS)
// What a directory letter becomes there (docs/design.md, "DFS directory letters"): $ is the target directory itself;
// another letter a subdirectory of the target named after it (made on demand), so *RUN A.PROG still finds it; the
// letters ADFS can't have in a name (^ @ & * # ", from the ADFS 2.10 ROM) give a flat "X_NAME" in the target
// directory, with a warning. Flattened (the "flatten" setting): every file goes in the target directory as NAME.
// canName: whether the target filesystem allows the letter as a name (by default the ROM's rule above).
struct LetterPlace {
    enum How { Target, Subdir, Flat } how = Target;
    QString subdir;                  // Subdir: the directory's name (the letter, upper case)
    QString name;                    // the file's name there
    QString warning;                 // Flat: what to tell the user
};
LetterPlace placeLetter(QChar letter, const QString &name, bool flatten = false,
                        const std::function<bool(QChar)> &canName = {});

class Disc {
public:
    // disc: the whole disc (both sides), size bytes long. It must outlive the Disc.
    bool attach(dw::device::Linear *disc, quint64 size, int side, const Layout &lay, QString *err);
    // An application's image class that offers linear() and fullSize() (Acorn Disk Wrangler's Adfs::Image).
    template <class Img> bool attach(Img *img, int side, const Layout &lay, QString *err) {
        return attach(&img->linear(), img->fullSize(), side, lay, err);
    }
    bool reload(QString *err) { return m_lin && load(err); }   // read the catalogue again (after the staging was put back)
    void detach() { m_lin = nullptr; }
    bool attached() const { return m_lin != nullptr; }
    int side() const { return m_side; }

    QString title() const { return m_title; }
    int bootOption() const { return m_boot; }
    bool watford() const { return m_watford; }          // a Watford 62-file disc (two catalogues)
    int maxFiles() const { return m_watford ? MaxFilesWatford - (m_guard ? 1 : 0) : MaxFiles; }
    quint32 sectors() const { return m_sectors; }       // disc size according to the catalogue
    quint32 freeSectors() const;
    quint32 largestFree() const;
    const QVector<File> &files() const { return m_files; }
    int indexOf(QChar dir, const QString &name) const;

    bool readFile(const File &f, QByteArray *out, QString *err) const;
    bool addFile(QChar dir, const QString &name, const QByteArray &data, quint32 load, quint32 exec,
                 bool locked, bool replace, QString *err);
    bool remove(QChar dir, const QString &name, QString *err);
    bool modify(QChar oldDir, const QString &oldName, QChar newDir, const QString &newName, bool locked,
                quint32 load, quint32 exec, QString *err);
    bool setTitle(const QString &title, QString *err);
    bool setBootOption(int opt, QString *err);       // 0 off, 1 *LOAD, 2 *RUN, 3 *EXEC (catalogue sector 1, byte 6 bits 4-5)

    Report check();     // reads the staged state

private:
    quint32 phys(quint32 n) const;
    bool rd(quint32 n, quint32 count, QByteArray *out, QString *err) const;
    bool wr(quint32 n, const QByteArray &data, QString *err);
    bool load(QString *err);
    bool writeCatalogue(QString *err);
    void placeWatford(int idx);                                 // choose the catalogue of a new file (m_files[idx])
    quint32 firstData() const { return m_watford ? 4 : 2; }    // the first sector after the catalogue(s)
    bool findGap(quint32 n, quint32 *start) const;
    bool addInner(QChar dir, const QString &name, const QByteArray &data, quint32 load, quint32 exec, bool locked,
                  bool replace, QString *err);

    dw::device::Linear *m_lin = nullptr;
    int m_side = 0;
    Layout m_lay;
    quint32 m_len = 0;
    QString m_title;
    int m_boot = 0;
    quint8 m_cycle = 0;
    // Watford 62-file discs
    bool m_watford = false;
    bool m_guard = false;        // the first catalogue holds Watford's "!.!!!!!!!" entry (sectors 2-3, hidden here)
    File m_guardFile;
    quint8 m_cycle2 = 0;         // sector 3 byte 4
    quint8 m_s3b6 = 0;           // sector 3 byte 6 as found (its sector-count bits are rewritten, the rest kept)
    quint32 m_sectors = 0;
    QVector<File> m_files;
};

} // namespace dfs
} // namespace dw
