/*
 * Tape image files - part of DiskWranglerCore
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
// A tape image file: UEF (gzip or not), TZX (and CDT, the same format for the Amstrad CPC), or the Spectrum's TAP.
// QtCore only. The file is read into a list of chunks, in order, every byte kept: a chunk this code doesn't
// understand is carried as it is, so a tape opened and saved unchanged gives the same file (a gzip file: the same
// tape, recompressed). What the chunks mean - tones, gaps, data - is records.h; what the data means is the program's.
//
// Sources: UEF 0.10 (Thomas Harte, Fraser Ross, Greg Cook; document draft 28, 2006); TZX 1.20 (World of Spectrum,
// 19 Dec 2006) and the CPCWiki CDT page (TZX 1.13 with the Amstrad notes).
//
//   - UEF: "UEF File!\0", minor, major; then chunks: id (16 bits), length (32 bits), body. Little-endian.
//   - TZX / CDT: "ZXTape!" 1A, major, minor; then blocks: an id byte and a body whose length comes from the block's
//     own fields (one rule per id; an id not in the specification has its length in its first 4 bytes, the
//     specification's extension rule). A Chunk's id is the block id and its body everything after it.
//   - TAP: blocks of a 16-bit length and that many bytes; a Chunk's id is 0 and its body the bytes, without the length.
//
// A file whose end can't be read as chunks (a UEF chunk longer than the rest of the file, a TZX block cut short) keeps
// the rest as one Chunk with id Tail, written back as it was; such a tape is read-only (readOnlyWhy()).
//
// The tape is changed by giving it a new list of chunks (setChunks, replaceChunks); the changes are staged until
// commit() or saveAs(), as a disc's are in device::Container, and a fs::Disc can run transactions over it.
#include <QByteArray>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>
#include <diskwrangler/device/staged.h>

namespace dw {
namespace tape {

enum class Kind { None, Uef, Tzx, Tap };
QString kindName(Kind k, bool gzip = false);              // "UEF", "UEF (gzip)", "TZX", "TAP"

struct Chunk {
    static const int Tail = -1;                          // the unreadable end of a file, kept as it was
    int id = 0;
    QByteArray body;
    Chunk() = default;
    Chunk(int i, const QByteArray &b) : id(i), body(b) {}
    bool operator==(const Chunk &o) const { return id == o.id && body == o.body; }
    bool operator!=(const Chunk &o) const { return !(*this == o); }
};

class TapeFile : public device::Staged {
public:
    TapeFile() = default;

    // What a file is, from its contents: UEF (gzip or not) and TZX by their headers; TAP (which has no header) only
    // when suffix is "tap" and its block lengths run exactly to the end of the file. None otherwise.
    static Kind identify(const QByteArray &file, const QString &suffix, bool *gzip = nullptr);

    bool open(const QString &path, QString *err);
    // The same from bytes already read (path is only remembered, for saving).
    bool openBytes(const QByteArray &file, const QString &path, const QString &suffix, QString *err);
    // A new, empty tape in memory, saved with saveAs(): UEF 0.10, or TZX 1.20.
    void createBlank(Kind kind, bool gzip);
    void close();

    Kind kind() const { return m_kind; }
    bool isOpen() const { return m_kind != Kind::None; }
    bool gzip() const { return m_gzip; }                 // UEF only: the file is (or will be saved) compressed
    QString path() const { return m_path; }
    int majorVersion() const { return m_major; }
    int minorVersion() const { return m_minor; }
    qint64 fileSize() const { return m_fileSize; }       // as stored (compressed, for gzip)
    // Why the tape can't be changed (a damaged end), empty if it can.
    QString readOnlyWhy() const { return m_readOnlyWhy; }
    // Writes are refused while this is off; the program turns it off for a file it can't write.
    void setWritable(bool on) { m_writable = on; }
    bool writable() const { return m_writable && m_readOnlyWhy.isEmpty(); }

    // The chunks with the staged changes.
    const QVector<Chunk> &chunks() const { return m_chunks; }
    // The chunks as in the file.
    const QVector<Chunk> &savedChunks() const { return m_saved; }

    // Changes, staged. replaceChunks puts with in place of count chunks from first. In a TZX file the blocks that
    // point at other blocks by a count (23 jump, 26 call, 28 select) are corrected for the blocks added or taken
    // away; a change that would split a loop (24 ... 25), or leave a jump pointing into chunks that are gone, is
    // refused and says why. Nothing is changed on a refusal.
    bool setChunks(const QVector<Chunk> &c, QString *err);
    bool replaceChunks(int first, int count, const QVector<Chunk> &with, QString *err);

    // ----- Staged
    bool hasPending() const override { return m_chunks != m_saved || m_gzip != m_savedGzip || m_forceSave; }
    void discardPending() override { m_chunks = m_saved; m_gzip = m_savedGzip; m_forceSave = false; }
    std::shared_ptr<const void> stagedState() const override;
    void restoreStaged(const std::shared_ptr<const void> &s) override;
    void markForSave() { m_forceSave = true; }
    void setGzip(bool on) { if (m_kind == Kind::Uef) m_gzip = on; }   // staged: the next save writes it so

    // The file's bytes with the staged changes, as kind would hold them (UEF: compressed if gz). A tape with
    // nothing changed, written as it was opened, gives the file's own bytes.
    QByteArray serialize(bool gz) const;
    QByteArray serialize() const { return serialize(m_gzip); }

    // Saving, as device::Container does it: validate() first; the new file written beside the original and
    // flushed; read back and compared, chunk for chunk, with what was meant; verify() on it; .bak if backup; then
    // the atomic rename. On success the staged changes become the tape's contents.
    struct SaveOptions {
        bool backup = false;
        std::function<bool(QString *err)> validate;
        std::function<bool(TapeFile &written, QString *err)> verify;
    };
    bool commit(const SaveOptions &o, QString *err);
    // Writes the tape to path (UEF: compressed or not, by gz), by the same steps (no backup); on success the tape
    // is the new file from then on.
    bool saveAs(const QString &path, bool gz, QString *err, const std::function<bool(TapeFile &, QString *)> &verify = {});

    // The length of the TZX block with this id whose body starts at body[0] of bytes from at on, or -1 if the
    // bytes end first. (For the tests and a program's own TZX code.)
    static qint64 tzxBodyLength(int id, const QByteArray &bytes, qint64 at);
    // A TZX block that points at others: the signed block counts it holds (23: one, 26: each call, 28: each
    // selection). Empty for any other block.
    static QVector<int> tzxOffsets(const Chunk &c);
    static Chunk tzxWithOffsets(const Chunk &c, const QVector<int> &offsets);

private:
    struct State {
        QVector<Chunk> chunks;
        bool gzip, forceSave;
    };
    bool parse(const QByteArray &plain, QString *err);
    bool writeChecked(const QString &path, bool gz, bool backup, const std::function<bool(TapeFile &, QString *)> &verify,
                      QString *err);

    Kind m_kind = Kind::None;
    QString m_path, m_suffix;
    int m_major = 0, m_minor = 0;
    bool m_gzip = false, m_savedGzip = false;
    bool m_forceSave = false;
    bool m_writable = true;
    qint64 m_fileSize = 0;
    QByteArray m_file;                                   // the file as stored (an unchanged save writes this)
    QString m_readOnlyWhy;
    QVector<Chunk> m_chunks, m_saved;
};

} // namespace tape
} // namespace dw
