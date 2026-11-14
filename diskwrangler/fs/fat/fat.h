/*
 * FAT12 / FAT16 - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
// fat.h - a FAT12/FAT16 volume: a whole floppy, or one stored inside an ADFS file (the DOS Plus partition,
// $.DOS.DRIVE_C). The optional FAT module of DiskWranglerCore (diskwrangler-fat.pri, CMake DiskWrangler::Fat); moved
// from Acorn Disk Wrangler, where it was Dos::Volume. QtCore only.
// All access goes through a dw::device::Linear, so every change is staged and is only written when the image is
// saved (after validation).
#pragma once

#include <QDateTime>
#include <QSet>
#include <QVector>

#include <diskwrangler/device/linear.h>
#include <diskwrangler/fs/volume.h>

namespace dw {
namespace fs { class PresetRegistry; }
namespace fat {

typedef fs::Report Report;

struct DEntry {
    QString name;               // "NAME.EXT"
    quint8 attr = 0;
    quint16 time = 0, date = 0;
    quint32 cluster = 0;
    quint32 size = 0;
    bool isDir() const { return (attr & 0x10) != 0; }
    QString attrString() const;  // e.g. "DR  A" style letters: D R H S A
    QDateTime when() const;      // invalid if the stamp is unset/garbage
};

enum { AttrR = 0x01, AttrH = 0x02, AttrS = 0x04, AttrA = 0x20 };

// Volume geometry. Sector counts are in units of bps (the volume's own sector size).
struct Params {
    quint32 bps, spc, res, nfats, rootEnt, spf, total;
    quint8 media;
    bool bpb;                    // true if read from a boot sector BPB
    // (How a floppy's sides map onto the image file - e.g. DOS Plus 640K numbering side 0 first - is Image's job:
    // see dw::device::SideOrder. A Volume only ever sees the disc in its own sector numbering.)
    Params(quint32 b = 0, quint32 s = 0, quint32 r = 0, quint32 n = 0, quint32 re = 0, quint32 sf = 0,
           quint32 t = 0, quint8 m = 0, bool bp = false)
        : bps(b), spc(s), res(r), nfats(n), rootEnt(re), spf(sf), total(t), media(m), bpb(bp) {}
};

// The fixed layouts from the format table (PC floppies and the two Acorn DOS Plus floppy formats).
enum Format { FmtPc360, FmtPc720, FmtPc1200, FmtPc1440, FmtAcorn800, FmtAcorn640 };

// The FAT module's presets (fs/presets.h), added to r in this order: Acorn DOS Plus 800K (dos-800: a side order to
// choose), then the PC floppies pc-360, pc-720, pc-1200, pc-1440 (always interleaved); title "Volume label", 11
// characters; extra "fatFormat" = the Format above. (Moved from Acorn Disk Wrangler's NewDisc. The DOS Plus 640K
// disc is not here: it is an ADFS disc too, and needs Acorn's DOSBOOT, so it is Acorn Disk Wrangler's.)
void addPresets(fs::PresetRegistry &r);

class Volume {
public:
    // Geometry from the boot sector BPB.
    // disc: the whole disc in its own numbering (it must outlive the Volume); the volume is the fileLen bytes from
    // sector sin (the ADFS file $.DOS.DRIVE_C, or 0 and the whole disc for a floppy).
    bool attach(dw::device::Linear *disc, quint32 sin, quint32 fileLen, QString *err);
    bool attachFormat(dw::device::Linear *disc, quint32 sin, quint32 fileLen, Format f, QString *err);
    bool attachParams(dw::device::Linear *disc, quint32 sin, quint32 fileLen, const Params &p, QString *err);
    // The same on an application's image class that offers linear() (Acorn Disk Wrangler's Adfs::Image).
    template <class Img> bool attach(Img *img, quint32 adfsSin, quint32 fileLen, QString *err) { return attach(&img->linear(), adfsSin, fileLen, err); }
    // Geometry from the table of known floppy layouts (no usable boot sector needed).
    template <class Img> bool attachFormat(Img *img, quint32 sin, quint32 fileLen, Format f, QString *err) { return attachFormat(&img->linear(), sin, fileLen, f, err); }
    template <class Img> bool attachParams(Img *img, quint32 sin, quint32 fileLen, const Params &p, QString *err) { return attachParams(&img->linear(), sin, fileLen, p, err); }
    bool reload(QString *err);       // read the FAT and the label again (after the staging was put back)
    dw::device::Linear *linear() const { return m_lin; }
    static Params standard(Format f);
    static bool readBpb(const QByteArray &boot, Params *out);   // field extraction only
    static bool plausible(const Params &p);                      // sanity check of a BPB'd geometry
    quint32 bytesPerSector() const { return m_bps; }
    void detach() { m_lin = nullptr; }
    bool attached() const { return m_lin != nullptr; }
    quint32 sin() const { return m_sin; }
    quint32 length() const { return m_len; }
    QString label() const { return m_label; }
    bool isFat16() const { return m_fat16; }
    quint32 clusterBytes() const { return m_spc * m_bps; }
    quint32 totalClusters() const { return m_clusters; }
    quint32 freeClusters() const;
    quint32 rootEntries() const { return m_rootEnt; }

    // dirCluster 0 = root directory. listDir includes "." and ".." for subdirectories.
    bool listDir(quint32 dirCluster, QVector<DEntry> *out, QString *err) const;
    bool readFile(const DEntry &e, QByteArray *out, QString *err) const;

    // Changes, staged in the container. One that fails may leave part of itself staged and the FAT in memory changed:
    // make changes inside dw::fs::Disc::transact, which puts the staging back and calls reload() (since step 6b the
    // class keeps no snapshot of its own).
    bool addFile(quint32 dirCluster, const QString &name, const QByteArray &data, bool replace, QString *err);
    bool makeDir(quint32 dirCluster, const QString &name, QString *err, quint32 *newCluster = nullptr);
    bool remove(quint32 dirCluster, const QString &name, QString *err);          // recursive for directories
    // Rename and/or set R/H/S/A (AttrR|AttrH|AttrS|AttrA mask). newName may equal oldName.
    bool modify(quint32 dirCluster, const QString &oldName, const QString &newName, quint8 attr, QString *err);

    // Volume label (max 11 characters, upper-cased; empty removes it).
    bool setLabel(const QString &label, QString *err);

    Report check();     // reads the staged state
    quint32 lostClusters() const { return m_lost; }   // from the last check(): in use in the FAT but in no file

    static bool validName(const QString &name, QString *why);
    static QString fromHost(const QString &hostName, bool directory = false);   // -> valid 8.3 name

private:
    struct Region { quint64 off; quint32 len; };

    bool rd(quint64 off, quint32 len, QByteArray *out, QString *err) const;
    bool wr(quint64 off, const QByteArray &d, QString *err);
    bool loadFat(QString *err);
    void loadLabel();
    bool flushFat(QString *err);
    bool canWrite(QString *err) const;          // false ("image is read-only") on a read-only volume
    quint32 fatGet(quint32 c) const;
    void fatSet(quint32 c, quint32 v);
    bool isEoc(quint32 v) const { return v >= (m_fat16 ? 0xFFF8u : 0xFF8u); }
    bool isBad(quint32 v) const { return v == (m_fat16 ? 0xFFF7u : 0xFF7u); }
    quint32 maxCluster() const { return m_clusters + 1; }
    quint32 eocValue() const { return m_fat16 ? 0xFFFF : 0xFFF; }
    quint64 clusterOff(quint32 c) const { return quint64(m_dataStart) * m_bps + quint64(c - 2) * clusterBytes(); }
    quint64 rootOff() const { return quint64(m_res + m_nfats * m_spf) * m_bps; }

    bool chain(quint32 start, QVector<quint32> *out, QString *err) const;
    bool alloc(quint32 n, QVector<quint32> *out);
    bool freeChain(quint32 start, QString *err);
    bool writeChain(const QVector<quint32> &ch, const QByteArray &data, QString *err);
    bool dirRegions(quint32 dir, QVector<Region> *out, QString *err) const;
    bool readRegions(const QVector<Region> &rg, QByteArray *out, QString *err) const;
    static quint64 slotOffset(const QVector<Region> &rg, int idx);
    bool freeSlot(quint32 dir, QVector<Region> &rg, QByteArray &data, int *idx, QString *err);
    static int findActive(const QByteArray &data, const QByteArray &raw11);
    bool removeTree(quint32 cluster, int depth, QString *err);
    bool addFileInner(quint32 dir, const QString &name, const QByteArray &data, bool replace, QString *err);
    bool makeDirInner(quint32 dir, const QString &name, QString *err, quint32 *newCluster);
    bool removeInner(quint32 dir, const QString &name, QString *err);
    bool setLabelInner(const QString &label, QString *err);
    bool modifyInner(quint32 dir, const QString &oldName, const QString &newName, quint8 attr, QString *err);
    static bool toRaw(const QString &name, QByteArray *raw11, QString *why);

    dw::device::Linear *m_lin = nullptr;
    quint32 m_sin = 0, m_len = 0;
    quint32 m_bps = 0, m_spc = 0, m_res = 0, m_nfats = 0, m_rootEnt = 0, m_spf = 0, m_total = 0;
    quint32 m_rootSectors = 0, m_dataStart = 0, m_clusters = 0;
    bool m_fat16 = false;
    quint8 m_media = 0;
    bool m_hasBpb = false;
    quint32 m_lost = 0;
    QByteArray m_fat;
    QString m_label;
};

} // namespace fat
} // namespace dw
