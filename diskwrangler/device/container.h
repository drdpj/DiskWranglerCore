/*
 * Disc image containers - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// The disc image file: a raw sector dump, ImageDisk (.IMD), CPCEMU DSK / extended DSK, or TeleDisk (.TD0, read
// only). QtCore only.
//
// A Container is one opened file. It offers the disc two ways:
//  * Physical: cylinder / head / sector, the sector addressed by its index on the track in ascending order of sector
//    ID (never its position in the file: ImageDisk and TeleDisk store a track in the order the sectors passed the
//    head) or by the ID itself (indexOfId). A raw file has this only once it has been given a RawLayout.
//  * Stored bytes (readStored / writeStored): the disc as one run of bytes in the order a raw file holds it -
//    cylinder by cylinder, side 0 then side 1, the sectors of a track in ID order. For a raw file that is simply the
//    file; for the other containers it needs every track to have the same shape. Linear (linear.h) builds on it.
//
// A raw file is read from the file as it is needed, not loaded, so a 512 MB hard disc image costs no memory.
//
// Nothing is written to the file except by commit() and saveAs(): every write is staged in memory (per sector, or
// for a raw file per 128-byte block of the file) and every read overlays the staged writes. commit() checks the
// result before anything is replaced: see the comment above SaveOptions.
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>

class QFile;
class QIODevice;

namespace dw {
namespace device {

enum class Kind { None, Raw, Imd, Dsk, Edsk, Td0 };
QString kindName(Kind k);           // "raw sector image", "ImageDisk (.IMD)", ...
QString kindExtension(Kind k);      // "img", "imd", "dsk", "dsk", "td0"

struct Sector {
    quint8 c = 0, h = 0, r = 0, n = 0;   // the ID field as recorded
    int size = 0;                        // data length in bytes
    QByteArray data;                     // empty if the sector had no data ("unavailable"); a raw file's sectors
                                         // keep no copy here (the file is read when needed)
    bool deleted = false;                // deleted-data address mark
    bool crcError = false;               // data read with an error
    bool missing = false;                // no data recorded
    // container-specific, to write the sector back the way it was
    quint8 imdType = 1;                  // IMD record type 0..8 as read
    quint8 st1 = 0, st2 = 0;             // DSK FDC status bytes
    qint64 fileOffset = -1;              // DSK/EDSK/raw: where the data lives in the original file
};

struct Track {
    int cyl = 0, head = 0;               // where the track is (from the container, not the sector IDs)
    int mode = -1;                       // ImageDisk mode 0-5 (data rate and FM/MFM), -1 if unknown
    QVector<Sector> secs;                // in recorded (rotational) order
    QVector<int> byId;                   // logical index -> position in secs, ascending sector ID
    bool duplicateIds = false;           // two sectors with the same ID: copy protection, never written
    QByteArray imdRaw;                   // IMD: the track's bytes exactly as in the file (re-emitted if unchanged)
    int imdHeadFlags = 0;                // IMD: bits 7/6 of the head byte (cylinder / head maps present)
    // DSK track header bytes kept for writing back
    quint8 dskGap = 0x4E, dskFill = 0xE5;
};

// How a raw image's bytes are cut into tracks: always cylinder by cylinder (cyl 0 head 0, cyl 0 head 1, ...) unless
// sideBySide. A raw image written in "logical" (cpmtools) order is read with the CP/M format's side order "alt".
struct RawLayout {
    int seclen = 0, sectrk = 0, cyls = 0, heads = 1;
    int firstId = 1;                     // sector IDs given to the sectors (only matters for Save As)
    int mode = -1;                       // IMD mode for Save As
    int t0sectrk = 0, t0seclen = 0;      // cylinder 0 (on each side) has its own shape, e.g. FM 16 x 128
    bool sideBySide = false;             // the file holds all of side 0, then all of side 1
    bool valid() const { return seclen > 0 && sectrk > 0 && cyls > 0 && heads > 0; }
    qint64 trackBytes() const { return qint64(seclen) * sectrk; }
    qint64 track0Bytes() const { return t0sectrk > 0 ? qint64(t0sectrk) * t0seclen : trackBytes(); }
    qint64 sideBytes() const { return track0Bytes() + (cyls - 1) * trackBytes(); }
    qint64 bytes() const { return sideBytes() * heads; }
    // where track (cyl, head) starts in the file
    qint64 trackOffset(int c, int h) const {
        if (sideBySide) return h * sideBytes() + (c == 0 ? 0 : track0Bytes() + (c - 1) * trackBytes());
        return c == 0 ? h * track0Bytes() : heads * track0Bytes() + (qint64(c - 1) * heads + h) * trackBytes();
    }
    bool sameShape(const RawLayout &o) const {
        return seclen == o.seclen && sectrk == o.sectrk && cyls == o.cyls && heads == o.heads && t0sectrk == o.t0sectrk &&
               t0seclen == o.t0seclen && sideBySide == o.sideBySide;
    }
};

// A summary of the physical shape, for detection and the Disc info box.
struct Shape {
    int cyls = 0, heads = 0;             // heads that carry tracks of the common shape
    int allHeads = 0;                    // heads with any track at all
    int seclen = 0, sectrk = 0;          // the most common track shape (by bytes)
    int firstId = 0;                     // lowest sector ID on the common tracks
    int uniformTracks = 0;               // tracks with the common shape
    int oddTracks = 0;                   // tracks with another shape (e.g. an FM boot track)
    int badSectors = 0;                  // CRC errors, missing data, duplicate IDs
    QVector<int> idsOfFirstTrack;        // sector IDs of cyl 0 head 0, sorted
};

// Cylinder / head / sector access.
class Physical {
public:
    virtual ~Physical() = default;
    virtual int cylinders() const = 0;
    virtual int heads() const = 0;
    virtual const Track *track(int cyl, int head) const = 0;
    virtual int sectorCount(int cyl, int head) const = 0;
    virtual const Sector *sector(int cyl, int head, int idx) const = 0;   // idx in ascending-ID order
    virtual bool sectorUsable(int cyl, int head, int idx) const = 0;     // has good data and can be written
    // The index (in ascending-ID order) of the sector with this ID, or -1. FLEX, for one, addresses sectors by ID.
    virtual int indexOfId(int cyl, int head, int sectorId) const = 0;
    virtual Shape shape() const = 0;
    // Reads overlay staged writes. A sector that is missing reads as nothing (returns false).
    virtual bool read(int cyl, int head, int idx, QByteArray *out) const = 0;
    // Staged; data is padded/truncated to the sector size. Fails on a missing/bad/duplicate sector.
    virtual bool write(int cyl, int head, int idx, const QByteArray &data, QString *err) = 0;
};

class Container : public Physical {
public:
    // The staged writes, to put back after a failed change (Volume transactions).
    struct Snapshot {
        QMap<quint32, QByteArray> sectors;   // key(c,h,idx) -> data
        QMap<qint64, QByteArray> blocks;     // raw files: file offset / RawBlock -> 128 bytes
        bool forceSave = false;
        qint64 rawEnd = 0;                   // raw files: where the last staged byte ends
        bool operator==(const Snapshot &o) const {
            return sectors == o.sectors && blocks == o.blocks && forceSave == o.forceSave && rawEnd == o.rawEnd;
        }
        bool operator!=(const Snapshot &o) const { return !(*this == o); }
    };
    static const int RawBlock = 128;         // staging unit of a raw file (every sector size is a multiple of it)

    Container();
    ~Container() override;
    Container(const Container &o);
    Container &operator=(const Container &o);

    // Opens any supported container. A raw image has no tracks until setRawLayout() is called (detection does
    // that); its bytes as stored are always available. err is set on failure.
    bool open(const QString &path, QString *err);
    // An image made in memory (a new blank disc), saved with saveAs().
    void createBlank(Kind kind, const RawLayout &l, const QString &comment);
    void close();

    Kind kind() const { return m_kind; }
    QString path() const { return m_path; }
    bool isOpen() const { return m_kind != Kind::None; }
    bool readOnlyContainer() const { return m_kind == Kind::Td0; }      // TeleDisk: Save As only
    QString comment() const { return m_comment; }                     // IMD / TD0 comment, DSK creator
    QString headerLine() const { return m_imdHeader; }
    // Writes are refused while this is off ("image is read-only"). The program turns it off for a file it can't
    // write or a disc that failed its checks.
    void setWritable(bool on) { m_writable = on; }
    bool writable() const { return m_writable; }

    // The physical view: this container, or nullptr for a raw file that has no layout yet.
    Physical *physical() { return hasPhysical() ? this : nullptr; }
    const Physical *physical() const { return hasPhysical() ? this : nullptr; }
    bool hasPhysical() const { return m_kind != Kind::None && (m_kind != Kind::Raw || m_layout.valid()); }

    // ----- raw images
    qint64 rawFileSize() const { return m_rawFileSize; }
    QByteArray rawBytes() const;                             // the whole file as stored (no staged writes)
    QByteArray rawFileBytes(qint64 off, qint64 len) const;   // part of it; short if the file ends first
    // Gives a raw file its tracks. A short image reads as filled with E5 (CP/M's blank) up to the layout's size.
    bool setRawLayout(const RawLayout &l, QString *err);
    RawLayout rawLayout() const { return m_layout; }
    // A raw file shorter than size reads as if filled with fill to that size, and is padded out when saved.
    // (setRawLayout sets this to the layout's size and E5.)
    void setRawPadding(qint64 size, char fill);
    qint64 rawPaddedSize() const { return m_padSize; }
    bool needsPadding() const { return m_kind == Kind::Raw && m_rawFileSize < m_padSize; }
    // A raw image longer than its layout keeps its tail untouched (it is never truncated by a save).

    // ----- the bytes as stored (see the top of this file)
    // How many bytes; 0 (and why) if the container can't be read this way (tracks of different shapes).
    qint64 storedSize(QString *why = nullptr) const;
    // Reads overlay staged writes. A raw file reads its padding byte beyond its end.
    bool readStored(qint64 off, qint64 len, QByteArray *out, QString *err) const;
    // Staged. On a raw file a block written with the bytes it already holds stays staged (the image counts as
    // changed); through the physical view it is dropped, as Physical::write does.
    bool writeStored(qint64 off, const QByteArray &data, QString *err);
    bool storedTouched(qint64 off, qint64 len) const;        // anything staged in this range
    void discardStored(qint64 off, qint64 len);              // drop staged writes lying wholly inside the range

    // ----- Physical
    int cylinders() const override { return m_cyls; }
    int heads() const override { return m_heads; }
    const Track *track(int cyl, int head) const override;
    int sectorCount(int cyl, int head) const override;
    const Sector *sector(int cyl, int head, int idx) const override;
    bool sectorUsable(int cyl, int head, int idx) const override;
    int indexOfId(int cyl, int head, int sectorId) const override;
    Shape shape() const override;
    bool read(int cyl, int head, int idx, QByteArray *out) const override;
    bool write(int cyl, int head, int idx, const QByteArray &data, QString *err) override;

    bool hasPending() const { return !m_pending.isEmpty() || !m_blocks.isEmpty() || m_forceSave; }
    void discardPending() { m_pending.clear(); m_blocks.clear(); m_forceSave = false; m_rawEnd = 0; }
    Snapshot snapshot() const { return Snapshot{m_pending, m_blocks, m_forceSave, m_rawEnd}; }
    void restore(const Snapshot &s) { m_pending = s.sectors; m_blocks = s.blocks; m_forceSave = s.forceSave; m_rawEnd = s.rawEnd; }
    void markForSave() { m_forceSave = true; }
    // New discs only: give one track another shape (e.g. a single-density track 0), filled with E5.
    bool reshapeTrack(int cyl, int head, int sectrk, int seclen, int firstId, int mode, QString *err);

    // Builds the file contents with the staged writes applied, in this image's own container (or another, for
    // Save As). Unchanged tracks of an IMD file and every byte outside changed sectors of a DSK/raw file are
    // reproduced exactly. (A raw file is built in memory here; commit() and saveAs() stream it.)
    bool serialize(Kind as, QByteArray *out, QString *err) const;

    // Saving. Nothing is changed until the new contents have been checked as they will be stored:
    //  1. validate(), if set: any error stops the save;
    //  2. the new file is written beside the original under a temporary name (a raw file is streamed: unchanged
    //     ranges copied from the original, staged blocks over them), and flushed to disc;
    //  3. the temporary file is opened again as a container and every staged write is read back from it; then
    //     verify(), if set, checks it with the program's own filesystem code;
    //  4. backup: the original becomes <name>.bak (a hard link where the system allows, else a copy);
    //  5. the temporary file atomically replaces the original.
    // The original is untouched by any failure before step 5. Free space is checked first ("not enough room").
    // A file another program holds open can't be replaced on Windows: "in use by another program".
    struct SaveOptions {
        bool backup = false;
        std::function<bool(QString *err)> validate;
        std::function<bool(Container &written, QString *err)> verify;
    };
    // On success the staged writes become the image's contents (the file is re-opened).
    bool commit(const SaveOptions &o, QString *err);
    bool commit(QString *err, bool backup) { SaveOptions o; o.backup = backup; return commit(o, err); }
    // Writes the disc to path in the given container, by the same steps (no backup); on success the image is the
    // new file from then on.
    bool saveAs(const QString &path, Kind as, QString *err, const std::function<bool(Container &, QString *)> &verify = {});

private:
    static quint32 key(int c, int h, int i) { return quint32(c) << 16 | quint32(h & 0xFF) << 8 | quint32(i & 0xFF); }
    bool parseImd(const QByteArray &b, QString *err);
    bool parseDsk(const QByteArray &b, QString *err);
    bool parseTd0(const QByteArray &b, QString *err);
    void index();                                        // fills m_cyls/m_heads, byId, the track lookup
    bool writeImd(QByteArray *out, QString *err) const;
    bool writeDsk(QByteArray *out, bool extended, QString *err) const;
    bool writeStandardDsk(QByteArray *out, QString *err) const;
    bool writeRawFromTracks(QByteArray *out, QString *err) const;
    bool streamRaw(QIODevice *dev, QString *err) const;  // raw file -> raw file, staged blocks applied
    qint64 rawOutputSize() const;
    QByteArray sectorData(int ti, int si) const;         // data with pending writes applied
    QByteArray rawRange(qint64 off, qint64 len) const;   // raw: file + padding + staged blocks
    void stageRaw(qint64 off, const QByteArray &data, bool dropIdentical);
    bool uniformTracks(int *trackBytes, int *seclen, QString *why) const;
    bool reopenAs(const QString &path, Kind as, const RawLayout &l, Container *out, QString *err) const;
    bool checkWritten(const Container &w, Kind as, QString *err) const;
    bool writeChecked(const QString &path, Kind as, const RawLayout &l, bool backup,
                      const std::function<bool(Container &, QString *)> &verify, Container *fresh, QString *err);

    Kind m_kind = Kind::None;
    QString m_path;
    QString m_comment, m_imdHeader;
    QByteArray m_file;                                   // the file as opened (not for raw files)
    std::shared_ptr<QFile> m_rawFile;                    // raw files: open read-only; null for a new blank disc
    qint64 m_rawFileSize = 0;                            // raw: the file's size (a new blank disc: its size)
    qint64 m_fileBacked = 0;                             // raw: how many of those bytes are in the file
    qint64 m_padSize = 0;
    char m_padByte = char(0xE5);
    RawLayout m_layout;
    QVector<Track> m_tracks;
    QMap<quint32, int> m_trackIndex;                     // (cyl,head) -> m_tracks index
    int m_cyls = 0, m_heads = 0;
    QMap<quint32, QByteArray> m_pending;                 // key(c,h,idx) -> new data (not raw)
    QMap<qint64, QByteArray> m_blocks;                   // raw: block number -> 128 bytes
    qint64 m_rawEnd = 0;                                 // raw: end of the furthest staged write
    bool m_forceSave = false;
    bool m_writable = true;
    int m_td0Rate = -1;                                  // TeleDisk data rate / FM flag, for converting to IMD
    bool m_td0Fm = false;
};

// TeleDisk "advanced compression" (LZSS + adaptive Huffman, as in Okumura's LZHUF). Exposed for tests.
QByteArray lzhufDecode(const QByteArray &in, int maxOut);

} // namespace device
} // namespace dw
