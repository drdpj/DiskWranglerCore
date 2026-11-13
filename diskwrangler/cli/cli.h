/*
 * Command-line layer - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// The command-line layer (docs/cli.md; docs/design.md, "Amendments from step 4b"). QtCore only.
//
// A program's console binary (acorndw, cpmdw) is a few lines: it builds its Backend and calls dw::runCli. The
// commands, the options, the JSON and the exit codes are this layer's, so the two tools behave as one; the Backend is
// what differs between them: how an image is opened (its detection and its filesystem classes), how a new one is
// made, how host files become items and back.

#include <QByteArray>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

#include <diskwrangler/app/appinfo.h>
#include <diskwrangler/device/container.h>
#include <diskwrangler/fs/detection.h>
#include <diskwrangler/fs/disc.h>
#include <diskwrangler/xfer/hostread.h>
#include <diskwrangler/xfer/importer.h>
#include <diskwrangler/xfer/retrofiles.h>

#define DW_CORE_VERSION "0.13.1"      // the core tag this source is; raise it with every tag

namespace dw {
namespace cli {

// Exit codes (docs/cli.md, "Exit codes").
enum Exit {
    ExitOk = 0, ExitFailed = 1, ExitUsage = 2, ExitNotFound = 3, ExitCheck = 4, ExitConflict = 5,
    ExitUnsupported = 6, ExitForeign = 7, ExitAmbiguous = 8, ExitUnrecognised = 9, ExitSave = 10
};
QString exitKind(int code);              // "ok", "failed", "usage", "not-found", "check", "conflict", ...

struct Error {
    int code = 0;
    QString message, path;
    bool failed() const { return code != 0; }
};

// ----- the options a Backend is told about

struct OpenOptions {
    QString fs;                          // --fs
    QString format;                      // --format (cpmdw: a library diskdef)
    QString charset;                     // --charset
    QString sides;                       // --sides: "", "auto", "interleaved", "sequential" (acorndw)
    QStringList diskdefs;                // --diskdefs
};

struct NewOptions {
    QString format, title, sides, container, dosboot, label;
    bool stamps = false, separateSides = false;
};

struct ConvertOptions {
    QString container;                   // "" = the image's own
    QString toSides;                     // "", "interleaved", "sequential"
};

struct GetOptions {
    QString sidecars;                    // "inf" or "none"
    QString userFolders = "auto";        // cpmdw: auto, on, off
    bool trim = false;                   // cpmdw
};

struct PutOptions {
    QString sidecars;                    // "inf" or "none"
};

// put and copy: --replace, --skip, --force, --letters (the Importer's options, diskwrangler/xfer/importer.h)
using ImportOptions = xfer::ImportOptions;

// ----- an opened image

class OpenedImage {
public:
    virtual ~OpenedImage() = default;
    virtual fs::Disc &disc() = 0;
    // Anything staged that a save would write (default: the disc's staged writes).
    virtual bool dirty() { return disc().dirty(); }
    // The checked save (every mount has been validated by the caller already). false: err says why (exit 10).
    virtual bool save(bool backup, QString *err) = 0;
    // "app" in info's result.
    virtual QJsonObject app() const = 0;
    // The --vol name of mount i. Default: the label in lower case with the spaces taken out ("drive 0" -> "drive0").
    virtual QString volName(int mount) const;
    // convert with an OUT file: write this image to out, in container (default its own), the source untouched; the
    // copy is checked. exit: 6 unsupported, 10 failed. result gets the details.
    virtual bool convertTo(const QString &out, const ConvertOptions &o, QJsonObject *result, QString *err, int *exit);
    // convert without OUT: stage a change of side order, saved with the run's single save.
    virtual bool convertSides(const QString &toSides, QString *err, int *exit);
    // Applies the --charset the run was given to the volumes (cpmdw).
    virtual void setCharset(const QString &) {}
    // Why the whole image can't be changed, when it is for a reason of the reading and not of one volume: cpmdw's
    // "the format is not confirmed; use --format=NAME". It is already a full sentence that says what to do; a refused
    // write reports it as "read-only: " plus this. Default: none (a mount's own reason names its volume instead).
    virtual QString readOnlyWhy() const { return QString(); }
    // A few words for the header of mount i in a text listing, after the file system's name: cpmdw's
    // "detected - see info" for a layout that is not in its library. Default: none.
    virtual QString volumeNote(int mount) const { Q_UNUSED(mount); return QString(); }
};

struct OpenOutcome {
    enum Status { Ok, NotFound, Failed, Usage, Foreign, Ambiguous, Unrecognised, Unsupported };
    Status status = Failed;
    QString message;
    QJsonObject container;               // {kind, name, size, readOnly} where it could be read
    fs::Verdict verdict;
    int chosen = -1;                     // index in verdict.ranked of the reading used, or -1
    std::unique_ptr<OpenedImage> image;  // set when status is Ok
    int exitCode() const;
    QString verdictName() const;         // "open", "ask", "handoff", "none"
};

// What every Backend does with a detection verdict: --fs picks a candidate from the ranking (so it resolves Ask and
// opens what would be None); otherwise the verdict decides. Sets status and message (and chosen) in *out.
void resolveVerdict(const fs::Verdict &v, const QString &fsOption, OpenOutcome *out);

class Backend {
public:
    virtual ~Backend() = default;
    virtual fs::OtherApp self() const = 0;
    // Opens path as the best reading of its verdict allows; nothing is written. Fills what it found even when it
    // can't open the image (status Foreign, Ambiguous, Unrecognised), so that info can show it.
    virtual OpenOutcome open(const QString &path, const OpenOptions &o) = 0;
    virtual QJsonArray formats() const = 0;                         // new --list
    // The --diskdefs files (cpmdw), before new --list, new and open use the format library. Default: nothing to load.
    virtual bool loadDiskdefs(const QStringList &, QString *, int *) { return true; }
    // Writes a blank image at file (a path the caller chose; it replaces anything there).
    virtual bool create(const QString &file, const NewOptions &o, QString *err, int *exit) = 0;
    virtual std::shared_ptr<xfer::HostPolicy> hostPolicy(const GetOptions &o, QString *err, int *exit) = 0;
    // Host files and folders (folders recursively) as items; for put. Names, dates, sidecars and folder layout as
    // this program's GUI reads them. warnings: things worth saying (a flattened folder).
    virtual bool readHost(const QStringList &paths, const PutOptions &o, QVector<TransferItem> *items,
                          QStringList *warnings, QString *err, int *exit) = 0;
    // Before importing: cpmdw takes the clash suffixes the retro-files writer added off the names.
    virtual void prepareIncoming(QVector<TransferItem> *, fs::Volume *) {}
    virtual void configureTarget(fs::Volume *, const ImportOptions &) {}
};

// The console program: its own name and options, beside the program of the family it belongs to (dw::app::AppInfo:
// name and version, as the window's Help > About shows them).
struct AppInfo {
    QString tool;                        // "acorndw"
    app::AppInfo program;                // name "Acorn Disk Wrangler", version as Help > About shows it, ...
    Backend *backend = nullptr;
    // The options this tool has beyond the common ones: "sides", "diskdefs", "charset", "sidecars", "user-folders",
    // "trim", "title", "dosboot", "container-new", "stamps", "label", "separate-sides", "to-sides", "format-open".
    QStringList options;
};

// {kind, name, size, readOnly} for info's "container" (path: the file as given; size is the file's).
QJsonObject containerInfo(const QString &path, const device::Container &c);

// Helper for readHost: the host files and folders at paths as items, read by the one host reader (xfer::readHost,
// <diskwrangler/xfer/hostread.h>; both console programs call that directly since step 5l). accept may skip a file
// (return false) or change its item (name, attributes); it is asked after the file is read. The first problem ends
// it: hostReadFailed's message and exit code.
typedef std::function<bool(const QString &hostFile, TransferItem *item)> HostFileFilter;
bool readHostTree(const QStringList &paths, const HostFileFilter &accept, QVector<TransferItem> *items, QString *err,
                  int *exit);
// The console programs' words for the host reader's first error: "<path>: no such file or folder" (exit 3, not
// found) or "<path>: cannot read (<reason>)" (exit 1). False if there is none. (A file too large is the program's.)
bool hostReadFailed(const QVector<xfer::HostReadError> &errors, QString *err, int *exit);

} // namespace cli

// Runs one invocation: argv as main gets it. Returns the exit code, which main returns.
int runCli(int argc, char **argv, const cli::AppInfo &app);

} // namespace dw
