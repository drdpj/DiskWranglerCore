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
// The commands and runCli (docs/cli.md). One invocation = one transaction = at most one save: every change is staged
// on the opened disc; the first error abandons the run (nothing is written); a run that staged something saves once
// through the program's OpenedImage after the whole disc has been checked.

#include "cli.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cstdio>

#include <diskwrangler/app/clock.h>
#include <diskwrangler/cli/args.h>
#include <diskwrangler/cli/importer.h>
#include <diskwrangler/cli/output.h>

namespace dw {
namespace cli {

namespace {

using fs::NodeKind;
using fs::Path;

// ---------------------------------------------------------------------------------------------- options

QVector<OptSpec> globalSpecs(const AppInfo &app) {
    QVector<OptSpec> s = {
        {"json", QChar(), false, false}, {"dry-run", QChar(), false, false}, {"vol", QChar(), true, false},
        {"fs", QChar(), true, false}, {"backup", QChar(), false, false}, {"quiet", 'q', false, false},
        {"help", 'h', false, false}, {"version", QChar(), false, false}};
    auto has = [&](const char *n) { return app.options.contains(n); };
    if (has("sides")) s.append({"sides", QChar(), true, false});
    s.append({"format", 'f', true, false});                    // cpmdw: a diskdef; acorndw: a preset id, on new only
    if (has("diskdefs")) s.append({"diskdefs", QChar(), true, true});
    if (has("charset")) s.append({"charset", QChar(), true, false});
    return s;
}

struct CmdDef {
    QString name;
    QString synopsis;                    // after the command word, for help and usage errors
    QString summary;
    QVector<OptSpec> opts;               // the command's own options (the tool's, filtered)
    int minPos = 0, maxPos = -1;         // positionals after the image (for new: after the file)
};

QVector<CmdDef> commandTable(const AppInfo &app) {
    auto has = [&](const char *n) { return app.options.contains(n); };
    auto keep = [&](QVector<OptSpec> v, const QStringList &toolOnly) {      // drop options this tool doesn't have
        QVector<OptSpec> o;
        for (const OptSpec &s : v)
            if (!toolOnly.contains(s.name) || has(s.name.toUtf8().constData())) o << s;
        return o;
    };
    QVector<CmdDef> t;
    t << CmdDef{"info", "IMAGE", "what the image is", {}, 0, 0};
    t << CmdDef{"check", "IMAGE [--strict]", "check every filesystem on the image", {{"strict", QChar(), false, false}}, 0, 0};
    t << CmdDef{"ls", "IMAGE [PATH] [-R] [-d]", "list a directory", {{"recursive", 'R', false, false}, {"directory", 'd', false, false}}, 0, -1};
    t << CmdDef{"get", "IMAGE PATH... HOSTDIR", "extract files to the host",
                keep({{"sidecars", QChar(), true, false}, {"user-folders", QChar(), true, false}, {"trim", QChar(), false, false},
                      {"manifest", QChar(), true, false}, {"replace", QChar(), false, false}, {"skip", QChar(), false, false}},
                     {"sidecars", "user-folders", "trim"}), 2, -1};
    t << CmdDef{"put", "IMAGE HOST... DIR", "add host files and folders",
                keep({{"sidecars", QChar(), true, false}, {"retro", QChar(), true, true}, {"replace", QChar(), false, false},
                      {"skip", QChar(), false, false}, {"letters", QChar(), true, false}, {"force", QChar(), false, false}},
                     {"sidecars"}), 1, -1};
    t << CmdDef{"rm", "IMAGE PATH... [-r] [--force]", "delete files and directories",
                {{"recursive", 'r', false, false}, {"force", QChar(), false, false}}, 1, -1};
    t << CmdDef{"mkdir", "IMAGE PATH... [-p]", "make directories", {{"parents", 'p', false, false}}, 1, -1};
    t << CmdDef{"mv", "IMAGE FROM TO", "rename or move", {}, 2, 2};
    t << CmdDef{"attr", "IMAGE PATH... [--set KEY=VALUE]...", "show or change attributes", {{"set", QChar(), true, true}}, 1, -1};
    t << CmdDef{"title", "IMAGE [TEXT]", "show or set the title or label", {}, 0, 1};
    t << CmdDef{"boot", "IMAGE [OPTION]", "show or set the boot option", {}, 0, 1};
    t << CmdDef{"new", "FILE --format=ID [...]", "make a blank image",
                keep({{"title", QChar(), true, false}, {"dosboot", QChar(), true, false}, {"container", QChar(), true, false},
                      {"stamps", QChar(), false, false}, {"label", QChar(), true, false}, {"separate-sides", QChar(), false, false},
                      {"replace", QChar(), false, false}, {"list", QChar(), false, false}},
                     {"title", "dosboot", "stamps", "label", "separate-sides"}), 0, 1};
    t << CmdDef{"convert", "IMAGE [OUT] [--container=KIND] [--to-sides=ORDER]", "write the image in another container",
                keep({{"container", QChar(), true, false}, {"to-sides", QChar(), true, false}, {"replace", QChar(), false, false}},
                     {"to-sides"}), 0, 1};
    t << CmdDef{"copy", "SRC DEST PATH... [--into=DIR] [--to-vol=NAME]", "copy between images",
                {{"into", QChar(), true, false}, {"to-vol", QChar(), true, false}, {"replace", QChar(), false, false},
                 {"skip", QChar(), false, false}, {"letters", QChar(), true, false}, {"force", QChar(), false, false}}, 1, -1};
    t << CmdDef{"script", "IMAGE [FILE|-]", "run many commands on one image, one save", {}, 0, 1};
    return t;
}

struct GlobalOpts {
    bool json = false, dryRun = false, backup = false, quiet = false;
    QString vol, fs, sides, format, charset;
    QStringList diskdefs;
};

// ---------------------------------------------------------------------------------------------- context

struct Ctx {
    const AppInfo &app;
    GlobalOpts g;
    QString imagePath;                   // as typed
    std::unique_ptr<OpenedImage> img, srcImg;
    OpenOutcome outcome;
    QJsonArray changes, warnings;
    Error err;
    QString volLabel;                    // the mount the command acted on
    bool volSet = false;
    QMap<int, bool> checked;             // mount -> its check() found no errors (cached for the run)
    explicit Ctx(const AppInfo &a) : app(a) {}
    Backend *be() const { return app.backend; }
    void fail(int code, const QString &msg, const QString &path = QString()) { err.code = code; err.message = msg; err.path = path; }
    void warn(const QString &w) { warnings.append(w); }
    void addChanges(const QVector<Change> &v) { for (const Change &c : v) changes.append(c.json()); }
    OpenOptions openOptions() const {
        OpenOptions o;
        o.fs = g.fs; o.format = g.format; o.charset = g.charset; o.sides = g.sides; o.diskdefs = g.diskdefs;
        return o;
    }
};

typedef QJsonObject Result;

QString toolPrefix(const Ctx &c) { return c.app.tool + ": "; }

// ---------------------------------------------------------------------------------------------- volumes

// --vol name -> mount index; -1 and err if none matches. "" is the default: the first writable mount, else the first.
int pickMount(Ctx &c, OpenedImage *img, const QString &opt, bool preferWritable, QString *err) {
    fs::Disc &d = img->disc();
    const int n = d.mounts().size();
    if (opt.isEmpty()) {
        if (preferWritable)
            for (int i = 0; i < n; ++i) if (!d.mounts()[i].readOnly) return i;
        return n > 0 ? 0 : -1;
    }
    const QString o = opt.toLower();
    QStringList names;
    for (int i = 0; i < n; ++i) {
        const QString v = img->volName(i);
        names << v;
        if (v == o) return i;
    }
    static const QRegularExpression tail("^[a-z]+(\\d+)$");
    for (int i = 0; i < n; ++i) {
        const QRegularExpressionMatch m = tail.match(img->volName(i));
        if (m.hasMatch() && m.captured(1) == o) return i;
    }
    *err = QString("no volume called '%1' on this disc; it has: %2").arg(opt, names.join(", "));
    Q_UNUSED(c);
    return -1;
}

struct VolRef {
    int mount = -1;
    fs::Volume *v = nullptr;
    QString label, name;
    bool readOnly = false;
    QString why;
};

// Why a mount can't be written, if it can't (docs/cli.md, "Read-only messages"), in the order the reasons are tried:
// the whole image (cpmdw: the format isn't confirmed), the container (TeleDisk), the image file, then the mount's own (resolveVol tries the check before that last one).
// `why` is empty when nothing stops a write. `ofVolume` is true only for the mount's own reason, which a message
// names the volume for; the others are about the whole image.
struct WriteBlock {
    QString why;
    bool ofVolume = false;
    bool blocked() const { return !why.isEmpty() || ofVolume; }
};

WriteBlock writeBlock(OpenedImage *img, int mount, bool withMount = true) {
    WriteBlock w;
    const fs::Disc::Mount &m = img->disc().mounts()[mount];
    const QString whole = img->readOnlyWhy();
    device::Container *k = img->disc().container();
    if (!whole.isEmpty()) w.why = whole;
    else if (k && k->readOnlyContainer()) w.why = "TeleDisk files can't be written; use convert to make a writable copy";
    else if (k && !k->path().isEmpty() && !QFileInfo(k->path()).isWritable()) w.why = "the image file can't be written";
    else if (withMount && m.readOnly) { w.why = m.readOnlyWhy; w.ofVolume = true; }
    return w;
}

// The text after "read-only: " for a refused write.
QString readOnlyReason(OpenedImage *img, int mount, const WriteBlock &w) {
    return w.ofVolume ? (w.why.isEmpty() ? img->volName(mount) : img->volName(mount) + ": " + w.why) : w.why;
}

bool resolveVol(Ctx &c, OpenedImage *img, const QString &opt, bool forWrite, VolRef *out) {
    QString err;
    const int i = pickMount(c, img, opt, forWrite, &err);
    if (i < 0) { c.fail(err.isEmpty() ? ExitUnsupported : ExitUsage, err.isEmpty() ? "the disc has no filesystem to work on" : err); return false; }
    const fs::Disc::Mount &m = img->disc().mounts()[i];
    out->mount = i;
    out->v = m.vol.get();
    out->label = m.label;
    out->name = img->volName(i);
    out->readOnly = m.readOnly;
    out->why = m.readOnlyWhy;
    if (!c.volSet) { c.volLabel = m.label; c.volSet = true; }
    if (forWrite) {
        // the whole image first, then the check (a disc with errors is exit 4 even where the volume is read-only
        // because of them), then the volume's own reason
        const WriteBlock w = writeBlock(img, i, false);
        if (w.blocked()) { c.fail(ExitUnsupported, "read-only: " + readOnlyReason(img, i, w)); return false; }
        if (!c.checked.contains(i)) c.checked[i] = m.vol->check().ok();
        if (!c.checked[i]) { c.fail(ExitCheck, QString("%1 has errors (see check), so nothing was changed").arg(out->name)); return false; }
        const WriteBlock wv = writeBlock(img, i);
        if (wv.blocked()) { c.fail(ExitUnsupported, "read-only: " + readOnlyReason(img, i, wv)); return false; }
    }
    return true;
}

bool parseVolPath(Ctx &c, const VolRef &r, const QString &text, Path *out) {
    QString why;
    if (!r.v->parsePath(text, out, &why)) { c.fail(ExitUsage, QString("%1: %2").arg(text, why)); return false; }
    return true;
}

bool statNode(const VolRef &r, const Path &p, fs::Entry *e, QString *why) { return r.v->stat(p, e, why); }

// Paths for a user's PATH text: the node itself, or every match of a wildcard in a name. A pattern with no match, or
// a literal that isn't there, is exit 3.
bool expandPaths(Ctx &c, const VolRef &r, const QString &text, QVector<Path> *out) {
    Path p;
    if (!parseVolPath(c, r, text, &p)) return false;
    bool wild = false;
    for (const QString &s : p.parts) wild = wild || hasWildcard(s);
    const Qt::CaseSensitivity cs = r.v->names().caseSensitivity();
    if (!wild) {
        fs::Entry e;
        QString why;
        if (!statNode(r, p, &e, &why)) { c.fail(ExitNotFound, QString("%1: not found").arg(r.v->displayPath(p)), r.v->displayPath(p)); return false; }
        out->append(p);
        return true;
    }
    QVector<Path> cur = {Path()};
    for (const QString &part : p.parts) {
        QVector<Path> next;
        for (const Path &base : cur) {
            if (!hasWildcard(part)) {
                fs::Entry e;
                QString why;
                if (r.v->stat(base.child(part), &e, &why)) next << base.child(part);
                continue;
            }
            QVector<fs::Entry> l;
            QString why;
            if (!r.v->list(base, &l, &why)) continue;
            for (const fs::Entry &e : l)
                if (wildcardMatch(part, e.name, cs)) next << base.child(e.name);
        }
        cur = next;
    }
    if (cur.isEmpty()) { c.fail(ExitNotFound, QString("%1: nothing matches").arg(text), text); return false; }
    *out += cur;
    return true;
}

bool isLockedEntry(const VolRef &r, const fs::Entry &e) {
    for (const fs::AttrSpec &s : r.v->fileAttrs())
        if (s.type == fs::AttrSpec::Bool && (s.key == "locked" || s.key == "readonly") && e.attrs.value(s.key).toBool()) return true;
    return false;
}

bool anyLockedBelow(const VolRef &r, const Path &dir) {
    QVector<fs::Entry> l;
    QString why;
    if (!r.v->list(dir, &l, &why)) return false;
    for (const fs::Entry &e : l) {
        if (isLockedEntry(r, e)) return true;
        if (e.isDir() && anyLockedBelow(r, dir.child(e.name))) return true;
    }
    return false;
}

Change makeChange(const QString &op, const VolRef &r, const QString &path, const QString &from = QString(), const QString &note = QString()) {
    Change c;
    c.op = op; c.vol = r.label; c.path = path; c.from = from; c.note = note;
    return c;
}

// An item (with its kids) for a node, as a copy or an extract carries it.
bool exportItem(Ctx &c, const VolRef &r, const Path &dir, const fs::Entry &e, TransferItem *out) {
    const Path p = dir.child(e.name);
    TransferItem t;
    t.name = e.name;
    t.hostName = r.v->hostNameFor(e);
    t.isDir = e.isDir();
    t.when = e.modified;
    t.attrs = r.v->toRetroIn(dir, e);
    t.sourceFs = r.v->fsId();
    QString why;
    if (e.isDir()) {
        QVector<fs::Entry> l;
        if (!r.v->list(p, &l, &why)) { c.fail(ExitFailed, QString("%1: %2").arg(r.v->displayPath(p), why), r.v->displayPath(p)); return false; }
        for (const fs::Entry &k : l) {
            TransferItem kid;
            if (!exportItem(c, r, p, k, &kid)) return false;
            t.kids.append(kid);
        }
    } else if (!r.v->read(p, &t.data, &why)) {
        c.fail(ExitFailed, QString("%1: %2").arg(r.v->displayPath(p), why), r.v->displayPath(p));
        return false;
    }
    *out = t;
    return true;
}

bool exportPath(Ctx &c, const VolRef &r, const Path &p, TransferItem *out) {
    fs::Entry e;
    QString why;
    if (!statNode(r, p, &e, &why)) { c.fail(ExitNotFound, QString("%1: not found").arg(r.v->displayPath(p))); return false; }
    if (p.isRoot()) e.name = QString();
    return exportItem(c, r, p.parent(), e, out);
}

// The root as a path to extract or copy is everything in it (its entries, not an unnamed folder).
bool exportNode(Ctx &c, const VolRef &r, const Path &p, QVector<TransferItem> *out) {
    if (!p.isRoot()) {
        TransferItem t;
        if (!exportPath(c, r, p, &t)) return false;
        out->append(t);
        return true;
    }
    QVector<fs::Entry> l;
    QString why;
    if (!r.v->list(Path(), &l, &why)) { c.fail(ExitFailed, why); return false; }
    for (const fs::Entry &e : l) {
        TransferItem t;
        if (!exportItem(c, r, Path(), e, &t)) return false;
        out->append(t);
    }
    return true;
}

// The attribute letters of an entry for a text listing: each yes/no attribute that is a list column, has a letter and
// is set on the entry, in the volume's letter order (the order of its specs unless it says otherwise).
QString attrSummary(const VolRef &r, const fs::Entry &e) {
    QVector<fs::AttrSpec> specs = r.v->fileAttrs();
    const QStringList order = r.v->attrLetterOrder();
    if (!order.isEmpty()) {
        QVector<fs::AttrSpec> sorted;
        for (const QString &k : order)
            for (const fs::AttrSpec &a : specs) if (a.key == k) sorted << a;
        for (const fs::AttrSpec &a : specs) if (!order.contains(a.key)) sorted << a;
        specs = sorted;
    }
    QString s;
    for (const fs::AttrSpec &a : specs)
        if (a.type == fs::AttrSpec::Bool && a.column && !a.letter.isEmpty() && e.attrs.value(a.key).toBool() && (!e.isDir() || a.dirs))
            s += a.letter;
    return s;
}

// ---------------------------------------------------------------------------------------------- info, check, ls

void fillInfo(Ctx &c, const Parsed &p, Result *res) {
    const OpenOutcome &o = c.outcome;
    Result r;
    r["container"] = o.container.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(o.container);
    r["verdict"] = o.verdictName();
    QJsonArray cands;
    for (const fs::Candidate &k : o.verdict.ranked) cands.append(candidateJson(k));
    r["candidates"] = cands;
    if (o.chosen >= 0 && o.chosen < o.verdict.ranked.size()) r["chosen"] = candidateJson(o.verdict.ranked[o.chosen]);
    else r["chosen"] = QJsonValue(QJsonValue::Null);
    const fs::Candidate *bc = o.verdict.bestCandidate();
    const fs::Candidate *own = o.chosen >= 0 && o.chosen < o.verdict.ranked.size() ? &o.verdict.ranked[o.chosen] : bc;
    r["owner"] = own && own->foreign() && o.verdict.action != fs::Verdict::Open ? QJsonValue(otherAppJson(own->owner)) : QJsonValue(QJsonValue::Null);
    QJsonArray vols;
    if (c.img) {
        const QString opt = p.value("vol", c.g.vol);
        QVector<int> which;
        if (opt.isEmpty() || opt.toLower() == "all") for (int i = 0; i < c.img->disc().mounts().size(); ++i) which << i;
        else {
            QString err;
            const int i = pickMount(c, c.img.get(), opt, false, &err);
            if (i < 0) { c.fail(ExitUsage, err); return; }
            which << i;
        }
        for (int i : which) {
            const fs::Disc::Mount &m = c.img->disc().mounts()[i];
            QJsonObject v;
            v["vol"] = c.img->volName(i);
            v["label"] = m.label;
            v["fsId"] = m.vol->fsId();
            v["readOnly"] = m.readOnly;
            v["readOnlyWhy"] = m.readOnlyWhy.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(m.readOnlyWhy);
            v["caps"] = capsJson(m.vol->caps());
            v["info"] = volumeInfoJson(m.vol->info());
            v["volumeAttrs"] = attrsJson(m.vol->volumeAttrs(), m.vol->volumeValues(), false);
            v["volumeAttrSpecs"] = attrSpecsJson(m.vol->volumeAttrs());
            v["fileAttrSpecs"] = attrSpecsJson(m.vol->fileAttrs());
            vols.append(v);
        }
        r["app"] = c.img->app();
    } else {
        r["app"] = QJsonValue(QJsonValue::Null);
    }
    r["volumes"] = vols;
    *res = r;
}

// info as text (docs/cli.md, "info text layout"), made from the JSON fillInfo built: the container, the detection, the
// format (cpmdw) or side order (acorndw), the reading, the definition (cpmdw, a layout not in the library) and the
// volumes. A disc that could not be opened has only the first two: what was found, and the other readings --fs chooses
// between.
void infoText(Ctx &c, const Result &res) {
    const QJsonObject k = res["container"].toObject(), ch = res["chosen"].toObject(), app = res["app"].toObject();
    auto line = [](const QString &s) { printf("%s\n", qPrintable(s)); };
    auto indented = [&](const QString &s, int n = 2) { line(QString(n, ' ') + s); };

    if (!k.isEmpty()) {
        device::Container *ct = c.img ? c.img->disc().container() : nullptr;
        const bool ro = k["readOnly"].toBool() || !QFileInfo(c.imagePath).isWritable() || (ct && !ct->writable());
        line(QString("container: %1, %2 bytes%3").arg(k["kind"].toString()).arg(qint64(k["size"].toDouble())).arg(ro ? ", read-only" : ""));
    }

    QString detection = "detection: " + res["verdict"].toString();
    if (!ch.isEmpty()) detection += QString(": %1 (%2)").arg(ch["description"].toString(), ch["tier"].toString());
    line(detection);
    QStringList notes;
    for (const QJsonValue &n : ch["notes"].toArray()) if (!n.toString().isEmpty()) { notes << n.toString(); indented(n.toString()); }
    const QJsonArray cands = res["candidates"].toArray();
    for (int i = 0; i < cands.size(); ++i) {
        if (i == c.outcome.chosen) continue;
        const QJsonObject o = cands[i].toObject();
        indented(QString("also: %1 (%2)").arg(o["description"].toString(), o["tier"].toString()));
    }
    if (!c.img) return;

    const QJsonObject fmt = app["format"].toObject(), sides = app["sides"].toObject();
    if (!fmt.isEmpty()) {
        line(QString("format: %1 (%2)").arg(fmt["name"].toString(), fmt["origin"].toString()));
        if (!fmt["summary"].toString().isEmpty()) indented(fmt["summary"].toString());
    } else if (sides["doubleSided"].toBool()) {
        line(QString("sides: %1, %2").arg(sides["order"].toString(), sides["forced"].toBool() ? "forced" : sides["confirmed"].toBool() ? "detected" : "assumed"));
    }

    // cpmdw reports its own confidence and the reason for it; acorndw's reading is the chosen candidate's tier
    const bool own = app.contains("confidence");
    const QString confidence = own ? app["confidence"].toString() : ch["tier"].toString();
    const bool safe = own ? app["safeToWrite"].toBool() : ch.value("safeToWrite").toBool(true);
    line(QString("reading: %1, %2").arg(confidence, safe ? "safe to write" : "read-only"));
    const QString why = own ? app["why"].toString() : QString();
    if (!why.isEmpty() && !notes.contains(why)) indented(why);            // (cpmdw's candidate notes already say it)
    if (!safe && !c.img->readOnlyWhy().isEmpty()) indented(c.img->readOnlyWhy());

    if (app.contains("fromLibrary") && !app["fromLibrary"].toBool() && !fmt["diskdef"].toString().isEmpty()) {
        line("definition:");
        for (const QString &l : fmt["diskdef"].toString().split('\n')) indented(l);
        indented("Save this definition to a file, give it a name, and pass --diskdefs=FILE --format=NAME with each command that changes the disc.");
    }

    for (const QJsonValue &v : res["volumes"].toArray()) {
        const QJsonObject o = v.toObject(), i = o["info"].toObject();
        QString head = QString("volume %1: %2 (%3)").arg(o["vol"].toString(), o["label"].toString(), o["fsId"].toString());
        for (int m = 0; m < c.img->disc().mounts().size(); ++m) {
            if (c.img->volName(m) != o["vol"].toString()) continue;
            const WriteBlock w = writeBlock(c.img.get(), m);
            if (w.blocked()) head += w.why.isEmpty() ? ", read-only" : ", read-only: " + w.why;
        }
        line(head);
        if (!i["title"].toString().isEmpty()) indented(QString("title: \"%1\"").arg(i["title"].toString()));
        const qint64 total = qint64(i["total"].toDouble()), free = qint64(i["free"].toDouble());
        indented(QString("%1 used, %2 free, %3 bytes").arg(total - free).arg(free).arg(total));
    }
}

bool cmdInfo(Ctx &c, const Parsed &p, const QStringList &, Result *res) {
    fillInfo(c, p, res);
    if (c.err.failed()) return false;
    if (!c.g.json && !c.g.quiet) infoText(c, *res);
    return true;
}

bool cmdCheck(Ctx &c, const Parsed &p, const QStringList &, Result *res) {
    const fs::Report r = c.img->disc().validateAll();
    Result o;
    o["errors"] = QJsonArray::fromStringList(r.errors);
    o["warnings"] = QJsonArray::fromStringList(r.warnings);
    o["info"] = QJsonArray::fromStringList(r.info);
    *res = o;
    if (!r.ok()) { c.fail(ExitCheck, QString("%1 error(s) found").arg(r.errors.size())); return false; }
    if (p.has("strict") && !r.warnings.isEmpty()) { c.fail(ExitCheck, QString("%1 warning(s) found (--strict)").arg(r.warnings.size())); return false; }
    return true;
}

// ---- the text of ls (docs/cli.md, "ls text layout"). The entries are collected first, with the JSON in the order the
// volume lists them, and formatted in a second pass: the columns, their widths and which of them show at all depend on
// every entry of the listing.

struct LsRow {
    QStringList key;                     // sort key: the levels of the path, lower case (user areas in number order)
    QString name, type, attrs, size, date;
};

struct LsBlock {                         // what is listed of one volume: one header, then its entries
    QString header;
    QVector<LsRow> rows;
};

struct LsOut {
    QJsonArray entries;                  // JSON: in the order the volume lists them
    QVector<LsBlock> blocks;             // text
    LsBlock &block() { return blocks.last(); }
};

// A level's sort key where it is a user area: "user 10" after "user 2".
QString numberedKey(const QString &s) {
    QString out;
    for (int i = 0; i < s.size();) {
        if (!s[i].isDigit()) { out += s[i++].toLower(); continue; }
        int j = i;
        while (j < s.size() && s[j].isDigit()) ++j;
        out += s.mid(i, j - i).rightJustified(12, '0');
        i = j;
    }
    return out;
}

// By name, ignoring case, level by level, so a directory comes before its contents and the entries of each directory
// are in name order; CP/M: user area first (numerically), then name.
bool rowLess(const LsRow &a, const LsRow &b) {
    const int n = qMin(a.key.size(), b.key.size());
    for (int i = 0; i < n; ++i) {
        const int c = a.key[i].compare(b.key[i]);
        if (c != 0) return c < 0;
    }
    if (a.key.size() != b.key.size()) return a.key.size() < b.key.size();
    return a.name < b.name;              // names that differ only in case: a fixed order
}

void emitEntry(const VolRef &r, const Path &dir, const fs::Entry &e, bool withVol, LsOut *out) {
    QJsonObject o = entryJson(*r.v, dir, e);
    if (withVol) o["vol"] = r.name;
    out->entries.append(o);
    const Path p = dir.child(e.name);
    const bool users = r.v->caps().namespaces;
    LsRow row;
    for (int i = 0; i < p.parts.size(); ++i) row.key << (users && i == 0 ? numberedKey(p.parts[i]) : p.parts[i].toLower());
    row.name = (withVol ? r.name + ":" : QString()) + r.v->displayPath(p);
    row.type = e.isDir() ? "D" : "";
    row.attrs = attrSummary(r, e);
    if (!e.isDir()) row.size = (e.sizeExact ? QString() : QString("~")) + QString::number(e.size);
    if (e.modified.isValid()) row.date = e.modified.toString("yyyy-MM-dd HH:mm");
    out->block().rows.append(row);
}

// NAME  TYPE  ATTRIBUTES  SIZE  DATE: a column that is empty in every row is left out, the others are as wide as their
// widest cell, two spaces between, nothing trailing.
QStringList lsLines(QVector<LsRow> rows) {
    std::stable_sort(rows.begin(), rows.end(), rowLess);
    int wName = 0, wAttrs = 0, wSize = 0;
    bool anyType = false, anyDate = false;
    for (const LsRow &r : rows) {
        wName = qMax(wName, int(r.name.size()));
        wAttrs = qMax(wAttrs, int(r.attrs.size()));
        wSize = qMax(wSize, int(r.size.size()));
        anyType = anyType || !r.type.isEmpty();
        anyDate = anyDate || !r.date.isEmpty();
    }
    QStringList lines;
    for (const LsRow &r : rows) {
        QStringList cols = {r.name.leftJustified(wName)};
        if (anyType) cols << r.type.leftJustified(1);
        if (wAttrs) cols << r.attrs.leftJustified(wAttrs);
        if (wSize) cols << r.size.rightJustified(wSize);
        if (anyDate) cols << r.date;
        QString line = cols.join("  ");
        while (line.endsWith(' ')) line.chop(1);
        lines << line;
    }
    return lines;
}

// VOL: LABEL "TITLE", USED used, FREE free[, read-only]
QString lsHeader(OpenedImage *img, const VolRef &r) {
    const fs::VolumeInfo vi = r.v->info();
    QString h = QString("%1: %2").arg(r.name, r.label);
    if (!vi.title.isEmpty()) h += QString(" \"%1\"").arg(vi.title);
    h += QString(", %1 used, %2 free").arg(vi.total - vi.free).arg(vi.free);
    if (writeBlock(img, r.mount).blocked()) h += ", read-only";
    return h;
}

bool listChildren(Ctx &c, const VolRef &r, const Path &dir, bool recursive, bool withVol, LsOut *out) {
    QVector<fs::Entry> l;
    QString why;
    if (!r.v->list(dir, &l, &why)) { c.fail(ExitFailed, QString("%1: %2").arg(r.v->displayPath(dir), why), r.v->displayPath(dir)); return false; }
    const fs::Caps caps = r.v->caps();
    for (const fs::Entry &e : l) {
        if (dir.isRoot() && caps.namespaces && !caps.dirs && e.kind == NodeKind::Namespace) {      // CP/M: every user's files
            if (!listChildren(c, r, dir.child(e.name), recursive, withVol, out)) return false;
            continue;
        }
        emitEntry(r, dir, e, withVol, out);
        if (recursive && e.kind == NodeKind::Dir && !listChildren(c, r, dir.child(e.name), true, withVol, out)) return false;
    }
    return true;
}

bool cmdLs(Ctx &c, const Parsed &p, const QStringList &pos, Result *res) {
    const QString opt = p.value("vol", c.g.vol);
    QVector<int> which;
    const bool all = opt.toLower() == "all";
    if (all) for (int i = 0; i < c.img->disc().mounts().size(); ++i) which << i;
    else {
        QString err;
        const int i = pickMount(c, c.img.get(), opt, false, &err);
        if (i < 0) { c.fail(err.isEmpty() ? ExitUnsupported : ExitUsage, err.isEmpty() ? "nothing to list" : err); return false; }
        which << i;
    }
    LsOut out;
    for (int i : which) {
        VolRef r;
        if (!resolveVol(c, c.img.get(), c.img->volName(i), false, &r)) return false;
        const bool rec = p.has("recursive");
        out.blocks.append(LsBlock());
        if (!p.has("directory")) out.block().header = lsHeader(c.img.get(), r);        // -d: no header
        if (pos.isEmpty()) {
            if (!listChildren(c, r, Path(), rec, all, &out)) return false;
            continue;
        }
        for (const QString &text : pos) {
            QVector<Path> nodes;
            if (!expandPaths(c, r, text, &nodes)) return false;
            for (const Path &n : nodes) {
                fs::Entry e;
                QString why;
                if (!statNode(r, n, &e, &why)) { c.fail(ExitNotFound, QString("%1: not found").arg(text)); return false; }
                if (p.has("directory") || !e.isDir()) emitEntry(r, n.parent(), e, all, &out);
                else if (!listChildren(c, r, n, rec, all, &out)) return false;
            }
        }
    }
    Result o;
    o["entries"] = out.entries;
    *res = o;
    if (!c.g.json && !c.g.quiet)
        for (const LsBlock &b : out.blocks) {
            if (!b.header.isEmpty()) printf("%s\n", qPrintable(b.header));
            for (const QString &line : lsLines(b.rows)) printf("%s\n", qPrintable(line));
        }
    return true;
}

// ---------------------------------------------------------------------------------------------- get

bool collectFiles(const QString &dir, const QString &rel, QStringList *out) {
    const QFileInfoList l = QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name);
    for (const QFileInfo &f : l) {
        const QString r = rel.isEmpty() ? f.fileName() : rel + "/" + f.fileName();
        if (f.isDir()) collectFiles(f.absoluteFilePath(), r, out);
        else out->append(r);
    }
    return true;
}

bool cmdGet(Ctx &c, const Parsed &p, const QStringList &pos, Result *res) {
    VolRef r;
    if (!resolveVol(c, c.img.get(), p.value("vol", c.g.vol), false, &r)) return false;
    const QString hostDir = pos.last();
    QVector<TransferItem> items;
    QStringList froms;
    for (int i = 0; i + 1 < pos.size(); ++i) {
        QVector<Path> nodes;
        if (!expandPaths(c, r, pos[i], &nodes)) return false;
        for (const Path &n : nodes) {
            if (!exportNode(c, r, n, &items)) return false;
            froms << r.v->displayPath(n);
        }
    }
    if (hostDir == "-") {
        if (c.g.json) { c.fail(ExitUsage, "get to '-' (stdout) can't be combined with --json"); return false; }
        if (items.size() != 1 || items[0].isDir) { c.fail(ExitUsage, "get to '-' needs exactly one file"); return false; }
        const QByteArray d = items[0].data;
        fwrite(d.constData(), 1, size_t(d.size()), stdout);
        fflush(stdout);
        *res = Result();
        return true;
    }
    GetOptions go;
    go.sidecars = p.value("sidecars", "inf");
    go.userFolders = p.value("user-folders", "auto");
    go.trim = p.has("trim");
    if (go.sidecars != "inf" && go.sidecars != "none") { c.fail(ExitUsage, "--sidecars is inf or none"); return false; }
    if (go.userFolders != "auto" && go.userFolders != "on" && go.userFolders != "off") { c.fail(ExitUsage, "--user-folders is auto, on or off"); return false; }
    QString err;
    int code = ExitFailed;
    if (go.userFolders == "auto") {          // a folder per user when the files come from more than one user area
        QSet<int> users;
        for (const TransferItem &t : items) if (t.attrs.contains("cpm")) users.insert(t.attrs.value("cpm").value("user").toInt());
        go.userFolders = users.size() > 1 ? "on" : "off";
    }
    std::shared_ptr<xfer::HostPolicy> policy = c.be()->hostPolicy(go, &err, &code);
    if (!policy) { c.fail(code, err); return false; }

    // written into a staging folder inside hostDir, then moved, so that nothing is touched when a name is taken
    const bool replace = p.has("replace"), skip = p.has("skip");
    QDir().mkpath(hostDir);
    const QString stage = QDir(hostDir).filePath(".dwcli-" + QUuid::createUuid().toString(QUuid::Id128).left(10));
    QStringList top, errors;
    QJsonArray jsonItems;
    xfer::writeHost(items, stage, *policy, &top, &jsonItems, &errors);
    if (!errors.isEmpty()) { QDir(stage).removeRecursively(); c.fail(ExitFailed, errors.join("; ")); return false; }
    QStringList files;
    collectFiles(stage, QString(), &files);
    QSet<QString> skipped;
    for (const QString &f : files) {
        const QString dest = QDir(hostDir).filePath(f);
        if (!QFileInfo::exists(dest) && !QFileInfo(dest).isSymLink()) continue;
        if (skip) { skipped.insert(f); continue; }
        if (!replace) {
            QDir(stage).removeRecursively();
            c.fail(ExitConflict, QString("%1 exists: use --replace or --skip").arg(QDir::toNativeSeparators(dest)), f);
            return false;
        }
    }
    for (const QString &f : files)                             // an .inf goes with its file
        if (f.endsWith(".inf", Qt::CaseInsensitive) && skipped.contains(f.left(f.size() - 4))) skipped.insert(f);
    QJsonArray written;
    QVector<Change> changes;
    int nth = 0;
    for (const QString &f : files) {
        const QString dest = QDir(hostDir).filePath(f);
        const bool sidecar = f.endsWith(".inf", Qt::CaseInsensitive) && files.contains(f.left(f.size() - 4));
        if (skipped.contains(f)) { changes << makeChange("skip", r, f, QString(), "exists"); continue; }
        if (!c.g.dryRun) {
            QDir().mkpath(QFileInfo(dest).absolutePath());
            if (QFileInfo::exists(dest)) QFile::remove(dest);
            if (!QFile::rename(QDir(stage).filePath(f), dest)) {
                QDir(stage).removeRecursively();
                c.fail(ExitFailed, QString("cannot write %1").arg(dest));
                return false;
            }
        }
        QJsonObject w;
        w["host"] = f;
        QString from;
        for (const QJsonValue &ji : jsonItems)                 // the listed name of the item written as this file
            if (ji.toObject()["path"].toString() == f) from = ji.toObject()["name"].toString();
        w["from"] = from.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(from);
        QFileInfo fi(c.g.dryRun ? QDir(stage).filePath(f) : dest);
        w["size"] = double(fi.size());
        w["sidecar"] = sidecar;
        written.append(w);
        changes << makeChange("add", r, f, from, sidecar ? "sidecar" : QString());
        ++nth;
    }
    QString manifest;
    if (p.has("manifest") && !c.g.dryRun) {
        QJsonArray kept;
        for (const QJsonValue &ji : jsonItems)
            if (!skipped.contains(ji.toObject()["path"].toString())) kept.append(ji);
        const QByteArray doc = xfer::makeJson(kept, hostDir, c.imagePath, r.v->fsId(), c.app.appName);
        manifest = p.value("manifest");
        QFile f(manifest);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(doc) != doc.size()) {
            QDir(stage).removeRecursively();
            c.fail(ExitFailed, QString("cannot write %1: %2").arg(manifest, f.errorString()));
            return false;
        }
    }
    QDir(stage).removeRecursively();
    c.addChanges(changes);
    Result o;
    o["written"] = written;
    o["manifest"] = manifest.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(manifest);
    *res = o;
    Q_UNUSED(nth);
    Q_UNUSED(top);
    if (!c.g.json && !c.g.quiet) for (const QJsonValue &w : written) printf("%s\n", qPrintable(w.toObject()["host"].toString()));
    return true;
}

// ---------------------------------------------------------------------------------------------- put, rm, mkdir, mv

bool lettersOption(Ctx &c, const Parsed &p, ImportOptions *o) {
    o->replace = p.has("replace");
    o->skip = p.has("skip");
    o->force = p.has("force");
    o->letters = p.value("letters", "dirs");
    if (o->replace && o->skip) { c.fail(ExitUsage, "--replace and --skip can't be combined"); return false; }
    if (o->letters != "dirs" && o->letters != "flatten") { c.fail(ExitUsage, "--letters is dirs or flatten"); return false; }
    return true;
}

bool runImport(Ctx &c, const VolRef &dest, QVector<TransferItem> &items, const QStringList &froms, const Path &dir,
               const ImportOptions &io, Result *res) {
    c.be()->prepareIncoming(&items, dest.v);
    c.be()->configureTarget(dest.v, io);
    Importer imp(dest.v, dest.label, io);
    ImportResult ir;
    Error err;
    const bool ok = imp.import(items, froms, dir, &ir, &err);
    c.addChanges(ir.changes);
    for (const QString &w : ir.warnings) c.warn(w);
    *res = ir.json();
    if (!ok) { c.err = err; return false; }
    if (!c.g.json && !c.g.quiet)
        for (const Change &ch : ir.changes) printf("%s %s\n", qPrintable(ch.op), qPrintable(ch.path));
    return true;
}

bool cmdPut(Ctx &c, const Parsed &p, const QStringList &pos, Result *res) {
    VolRef r;
    if (!resolveVol(c, c.img.get(), p.value("vol", c.g.vol), true, &r)) return false;
    ImportOptions io;
    if (!lettersOption(c, p, &io)) return false;
    PutOptions po;
    po.sidecars = p.value("sidecars", "inf");
    if (po.sidecars != "inf" && po.sidecars != "none") { c.fail(ExitUsage, "--sidecars is inf or none"); return false; }
    const QStringList hostPaths = pos.mid(0, pos.size() - 1);
    const QStringList retro = p.values("retro");
    if (hostPaths.isEmpty() && retro.isEmpty()) { c.fail(ExitUsage, "put needs host files or --retro"); return false; }
    Path dir;
    if (!parseVolPath(c, r, pos.last(), &dir)) return false;
    {
        fs::Entry e;
        QString why;
        if (!dir.isRoot() && r.v->stat(dir, &e, &why) && !e.isDir()) { c.fail(ExitFailed, QString("%1 is a file, not a directory").arg(pos.last())); return false; }
    }
    QVector<TransferItem> items;
    QStringList froms, warns;
    if (!hostPaths.isEmpty()) {
        QString err;
        int code = ExitFailed;
        if (!c.be()->readHost(hostPaths, po, &items, &warns, &err, &code)) { c.fail(code, err); return false; }
        for (const QString &w : warns) c.warn(w);
        for (int i = 0; i < items.size(); ++i) froms << (items.size() == hostPaths.size() ? hostPaths[i] : items[i].name);
    }
    for (const QString &doc : retro) {
        QFile f(doc);
        if (!f.open(QIODevice::ReadOnly)) { c.fail(ExitNotFound, QString("%1: %2").arg(doc, f.errorString()), doc); return false; }
        QVector<TransferItem> di;
        QString err;
        if (!xfer::readJson(f.readAll(), &di, &err)) { c.fail(ExitFailed, QString("%1: %2").arg(doc, err), doc); return false; }
        for (const TransferItem &t : di) { items.append(t); froms << doc + ":" + t.name; }
    }
    return runImport(c, r, items, froms, dir, io, res);
}

bool unlockBelow(Ctx &c, const VolRef &r, const Path &node, const fs::Entry &e) {
    QString why;
    for (const fs::AttrSpec &s : r.v->fileAttrs())
        if (s.type == fs::AttrSpec::Bool && (s.key == "locked" || s.key == "readonly") && e.attrs.value(s.key).toBool())
            if (!r.v->setAttrs(node, {{s.key, false}}, &why)) { c.fail(ExitFailed, QString("%1: %2").arg(r.v->displayPath(node), why)); return false; }
    if (e.isDir()) {
        QVector<fs::Entry> l;
        if (r.v->list(node, &l, &why))
            for (const fs::Entry &k : l) if (!unlockBelow(c, r, node.child(k.name), k)) return false;
    }
    return true;
}

bool cmdRm(Ctx &c, const Parsed &p, const QStringList &pos, Result *res) {
    VolRef r;
    if (!resolveVol(c, c.img.get(), p.value("vol", c.g.vol), true, &r)) return false;
    QVector<Path> nodes;
    for (const QString &t : pos) if (!expandPaths(c, r, t, &nodes)) return false;
    int removed = 0;
    for (const Path &n : nodes) {
        fs::Entry e;
        QString why;
        if (!statNode(r, n, &e, &why)) continue;                // gone with a directory removed before it
        const QString shown = r.v->displayPath(n);
        if (n.isRoot()) { c.fail(ExitUnsupported, "the root can't be removed"); return false; }
        if (e.isDir() && !p.has("recursive")) { c.fail(ExitUnsupported, QString("%1 is a directory: use -r").arg(shown), shown); return false; }
        const bool locked = isLockedEntry(r, e) || (e.isDir() && anyLockedBelow(r, n));
        if (locked && !p.has("force")) { c.fail(ExitUnsupported, QString("%1 is locked or read-only: use --force").arg(shown), shown); return false; }
        if (locked && !unlockBelow(c, r, n, e)) return false;
        if (!r.v->remove(n, &why)) { c.fail(ExitFailed, QString("%1: %2").arg(shown, why), shown); return false; }
        c.changes.append(makeChange("remove", r, shown).json());
        ++removed;
        if (!c.g.json && !c.g.quiet) printf("remove %s\n", qPrintable(shown));
    }
    Result o;
    o["removed"] = removed;
    *res = o;
    return true;
}

bool cmdMkdir(Ctx &c, const Parsed &p, const QStringList &pos, Result *res) {
    VolRef r;
    if (!resolveVol(c, c.img.get(), p.value("vol", c.g.vol), true, &r)) return false;
    if (!r.v->caps().dirs) { c.fail(ExitUnsupported, QString("%1 has no directories").arg(r.v->fsId().toUpper())); return false; }
    const bool parents = p.has("parents");
    int made = 0;
    for (const QString &t : pos) {
        Path path;
        if (!parseVolPath(c, r, t, &path)) return false;
        if (path.isRoot()) {
            if (parents) continue;
            c.fail(ExitConflict, "the root exists already", t);
            return false;
        }
        Path cur;
        for (int k = 0; k < path.parts.size(); ++k) {
            const QString part = path.parts[k];
            const Path next = cur.child(part);
            const bool last = k == path.parts.size() - 1;
            fs::Entry e;
            QString why;
            if (r.v->stat(next, &e, &why)) {
                if (last) {
                    if (parents && e.isDir()) break;
                    c.fail(ExitConflict, QString("%1 exists").arg(r.v->displayPath(next)), r.v->displayPath(next));
                    return false;
                }
                if (!e.isDir()) { c.fail(ExitFailed, QString("%1 is a file").arg(r.v->displayPath(next)), r.v->displayPath(next)); return false; }
            } else {
                if (!last && !parents) { c.fail(ExitNotFound, QString("%1: not found (use -p)").arg(r.v->displayPath(next)), r.v->displayPath(next)); return false; }
                if (!r.v->names().valid(part, NodeKind::Dir, &why)) { c.fail(ExitUsage, QString("%1: %2").arg(part, why), part); return false; }
                if (!r.v->mkdir(cur, part, &why)) { c.fail(ExitFailed, QString("%1: %2").arg(r.v->displayPath(next), why), r.v->displayPath(next)); return false; }
                c.changes.append(makeChange("mkdir", r, r.v->displayPath(next)).json());
                ++made;
                if (!c.g.json && !c.g.quiet) printf("mkdir %s\n", qPrintable(r.v->displayPath(next)));
            }
            cur = next;
        }
    }
    Result o;
    o["made"] = made;
    *res = o;
    return true;
}

bool cmdMv(Ctx &c, const Parsed &p, const QStringList &pos, Result *res) {
    VolRef r;
    if (!resolveVol(c, c.img.get(), p.value("vol", c.g.vol), true, &r)) return false;
    Path from, to;
    if (!parseVolPath(c, r, pos[0], &from) || !parseVolPath(c, r, pos[1], &to)) return false;
    fs::Entry e, t;
    QString why;
    if (from.isRoot() || !statNode(r, from, &e, &why)) { c.fail(ExitNotFound, QString("%1: not found").arg(pos[0]), pos[0]); return false; }
    if (to.isRoot() || (r.v->stat(to, &t, &why) && t.isDir())) to = to.child(from.name());
    if (r.v->stat(to, &t, &why)) { c.fail(ExitConflict, QString("%1 exists").arg(r.v->displayPath(to)), r.v->displayPath(to)); return false; }
    if (from.parent() != to.parent() && !r.v->caps().renameAcrossDirs) {
        c.fail(ExitUnsupported, QString("%1 can't move a file to another directory").arg(r.v->fsId().toUpper()));
        return false;
    }
    if (!r.v->names().valid(to.name(), e.isDir() ? NodeKind::Dir : NodeKind::File, &why)) { c.fail(ExitUsage, QString("%1: %2").arg(to.name(), why), to.name()); return false; }
    if (!r.v->rename(from, to, &why)) { c.fail(ExitFailed, QString("%1: %2").arg(r.v->displayPath(from), why), r.v->displayPath(from)); return false; }
    c.changes.append(makeChange("rename", r, r.v->displayPath(to), r.v->displayPath(from)).json());
    Result o;
    o["from"] = r.v->displayPath(from);
    o["to"] = r.v->displayPath(to);
    *res = o;
    if (!c.g.json && !c.g.quiet) printf("rename %s %s\n", qPrintable(r.v->displayPath(from)), qPrintable(r.v->displayPath(to)));
    return true;
}

// ---------------------------------------------------------------------------------------------- attr, title, boot

bool cmdAttr(Ctx &c, const Parsed &p, const QStringList &pos, Result *res) {
    const QStringList sets = p.values("set");
    VolRef r;
    if (!resolveVol(c, c.img.get(), p.value("vol", c.g.vol), !sets.isEmpty(), &r)) return false;
    QVector<Path> nodes;
    for (const QString &t : pos) if (!expandPaths(c, r, t, &nodes)) return false;
    const QVector<fs::AttrSpec> specs = r.v->fileAttrs();
    if (sets.isEmpty()) {
        QJsonArray entries;
        for (const Path &n : nodes) {
            fs::Entry e;
            QString why;
            if (!statNode(r, n, &e, &why)) { c.fail(ExitNotFound, QString("%1: not found").arg(r.v->displayPath(n))); return false; }
            entries.append(entryJson(*r.v, n.parent(), e));
            if (!c.g.json && !c.g.quiet) printf("%s %s\n", qPrintable(r.v->displayPath(n)), qPrintable(attrSummary(r, e)));
        }
        Result o;
        o["entries"] = entries;
        *res = o;
        return true;
    }
    fs::AttrValues values;
    QStringList keys;
    for (const QString &s : sets) {
        const int eq = s.indexOf('=');
        if (eq <= 0) { c.fail(ExitUsage, QString("--set needs KEY=VALUE, not '%1'").arg(s)); return false; }
        const QString key = s.left(eq).trimmed(), text = s.mid(eq + 1);
        const fs::AttrSpec *spec = nullptr;
        for (const fs::AttrSpec &a : specs) if (a.key.compare(key, Qt::CaseInsensitive) == 0) spec = &a;
        if (!spec) {
            QStringList have;
            for (const fs::AttrSpec &a : specs) have << a.key;
            c.fail(ExitUsage, QString("unknown attribute '%1' (this filesystem has: %2)").arg(key, have.join(", ")));
            return false;
        }
        if (!spec->editable) { c.fail(ExitUsage, QString("attribute '%1' can't be changed").arg(spec->key)); return false; }
        QVariant v;
        QString err;
        if (!parseAttrValue(*spec, text, &v, &err)) { c.fail(ExitUsage, err); return false; }
        values[spec->key] = v;
        keys << spec->key + "=" + text;
    }
    for (const Path &n : nodes) {
        fs::Entry e;
        QString why;
        if (!statNode(r, n, &e, &why)) { c.fail(ExitNotFound, QString("%1: not found").arg(r.v->displayPath(n))); return false; }
        fs::AttrValues use = values;
        if (e.isDir())
            for (const fs::AttrSpec &a : specs)
                if (!a.dirs && values.contains(a.key)) { c.fail(ExitUsage, QString("%1: a directory has no attribute '%2'").arg(r.v->displayPath(n), a.key)); return false; }
        if (!r.v->setAttrs(n, use, &why)) { c.fail(ExitFailed, QString("%1: %2").arg(r.v->displayPath(n), why), r.v->displayPath(n)); return false; }
        c.changes.append(makeChange("attr", r, r.v->displayPath(n), QString(), keys.join(" ")).json());
        if (!c.g.json && !c.g.quiet) printf("attr %s %s\n", qPrintable(r.v->displayPath(n)), qPrintable(keys.join(" ")));
    }
    Result o;
    o["changed"] = nodes.size();
    *res = o;
    return true;
}

const fs::AttrSpec *volumeSpec(const fs::Volume *v, const QStringList &keys) {
    static QVector<fs::AttrSpec> hold;
    hold = v->volumeAttrs();
    for (const QString &k : keys)
        for (const fs::AttrSpec &s : hold) if (s.key == k) return &s;
    return nullptr;
}

bool cmdTitle(Ctx &c, const Parsed &p, const QStringList &pos, Result *res) {
    VolRef r;
    if (!resolveVol(c, c.img.get(), p.value("vol", c.g.vol), !pos.isEmpty(), &r)) return false;
    const fs::AttrSpec *s = volumeSpec(r.v, {"title", "label"});
    if (!s) { c.fail(ExitUnsupported, QString("%1 has no title or label").arg(r.v->fsId().toUpper())); return false; }
    if (!pos.isEmpty()) {
        QString why;
        if (!r.v->setVolumeAttrs({{s->key, pos[0]}}, &why)) { c.fail(ExitFailed, why); return false; }
        c.changes.append(makeChange("title", r, QString(), QString(), pos[0]).json());
    }
    Result o;
    o["title"] = r.v->volumeValues().value(s->key).toString();
    o["key"] = s->key;
    *res = o;
    if (!c.g.json && !c.g.quiet && pos.isEmpty()) printf("%s\n", qPrintable(o["title"].toString()));
    return true;
}

bool cmdBoot(Ctx &c, const Parsed &p, const QStringList &pos, Result *res) {
    VolRef r;
    if (!resolveVol(c, c.img.get(), p.value("vol", c.g.vol), !pos.isEmpty(), &r)) return false;
    const fs::AttrSpec *s = volumeSpec(r.v, {"boot"});
    if (!s) { c.fail(ExitUnsupported, QString("%1 has no boot option").arg(r.v->fsId().toUpper())); return false; }
    if (!pos.isEmpty()) {
        QString t = pos[0].trimmed().toLower();
        static const QStringList words = {"off", "load", "run", "exec"};
        QVariant v;
        QString err;
        if (words.contains(t)) v = words.indexOf(t);
        else if (!parseAttrValue(*s, t, &v, &err)) { c.fail(ExitUsage, err); return false; }
        QString why;
        if (!r.v->setVolumeAttrs({{"boot", v}}, &why)) { c.fail(ExitFailed, why); return false; }
        c.changes.append(makeChange("boot", r, QString(), QString(), pos[0]).json());
    }
    Result o;
    o["boot"] = attrValueJson(*s, r.v->volumeValues().value("boot"));
    *res = o;
    if (!c.g.json && !c.g.quiet && pos.isEmpty()) printf("%s\n", qPrintable(o["boot"].toObject()["name"].toString()));
    return true;
}

// ---------------------------------------------------------------------------------------------- new, convert, copy

QString tempBeside(const QString &file, const char *tag) {
    const QFileInfo fi(file);
    return fi.absoluteDir().filePath(QString(".%1-%2-%3").arg(tag, QUuid::createUuid().toString(QUuid::Id128).left(8), fi.fileName()));
}

int exitFor(const OpenOutcome &o) { return o.exitCode(); }

bool cmdNew(Ctx &c, const Parsed &p, const QStringList &pos, Result *res) {
    if (!c.g.diskdefs.isEmpty()) {
        QString why;
        int code = ExitFailed;
        if (!c.be()->loadDiskdefs(c.g.diskdefs, &why, &code)) { c.fail(code, why); return false; }
    }
    if (p.has("list")) {
        Result o;
        o["formats"] = c.be()->formats();
        *res = o;
        if (!c.g.json && !c.g.quiet)
            for (const QJsonValue &f : c.be()->formats()) printf("%s\n", qPrintable(f.toObject().contains("id") ? f.toObject()["id"].toString() : f.toObject()["name"].toString()));
        return true;
    }
    if (pos.isEmpty()) { c.fail(ExitUsage, "new needs the file to make"); return false; }
    const QString format = p.value("format", c.g.format);
    if (format.isEmpty()) { c.fail(ExitUsage, "new needs --format (see new --list)"); return false; }
    NewOptions n;
    n.format = format;
    n.title = p.value("title");
    n.sides = c.g.sides;
    n.container = p.value("container");
    n.dosboot = p.value("dosboot");
    n.label = p.value("label");
    n.stamps = p.has("stamps");
    n.separateSides = p.has("separate-sides");
    if (!n.sides.isEmpty() && n.sides != "interleaved" && n.sides != "sequential") { c.fail(ExitUsage, "--sides for new is interleaved or sequential"); return false; }
    const QString file = pos[0];
    if (QFileInfo::exists(file) && !p.has("replace")) { c.fail(ExitConflict, QString("%1 exists: use --replace").arg(file), file); return false; }
    const QString tmp = tempBeside(file, "dwnew");
    QString err;
    int code = ExitFailed;
    if (!c.be()->create(tmp, n, &err, &code)) { QFile::remove(tmp); c.fail(code, err); return false; }
    OpenOptions oo = c.openOptions();
    oo.fs.clear();
    if (c.app.options.contains("format-open")) oo.format = n.format;
    OpenOutcome out = c.be()->open(tmp, oo);
    if (out.status != OpenOutcome::Ok) { QFile::remove(tmp); c.fail(exitFor(out), "the new image can't be opened again: " + out.message); return false; }
    const fs::Report rep = out.image->disc().validateAll();
    QJsonArray vols;
    for (int i = 0; i < out.image->disc().mounts().size(); ++i) vols.append(out.image->volName(i));
    out.image.reset();
    if (!rep.ok()) { QFile::remove(tmp); c.fail(ExitCheck, "the new image fails its check: " + rep.errors.join("; ")); return false; }
    if (c.g.dryRun) QFile::remove(tmp);
    else {
        if (QFileInfo::exists(file)) QFile::remove(file);
        if (!QFile::rename(tmp, file)) { QFile::remove(tmp); c.fail(ExitSave, QString("cannot write %1").arg(file), file); return false; }
    }
    Change ch;
    ch.op = "new"; ch.vol = QString(); ch.path = file; ch.note = format;
    c.changes.append(ch.json());
    Result o;
    o["file"] = file;
    o["format"] = format;
    o["volumes"] = vols;
    *res = o;
    if (!c.g.json && !c.g.quiet) printf("new %s\n", qPrintable(file));
    return true;
}

bool cmdConvert(Ctx &c, const Parsed &p, const QStringList &pos, Result *res) {
    ConvertOptions o;
    o.container = p.value("container");
    o.toSides = p.value("to-sides");
    if (!o.container.isEmpty() && o.container != "raw" && o.container != "imd" && o.container != "dsk" && o.container != "edsk" && o.container != "td0") {
        c.fail(ExitUsage, "--container is raw, imd, dsk or edsk");
        return false;
    }
    if (o.container == "td0") { c.fail(ExitUnsupported, "TeleDisk files can be read but not written"); return false; }
    if (!o.toSides.isEmpty() && o.toSides != "interleaved" && o.toSides != "sequential") { c.fail(ExitUsage, "--to-sides is interleaved or sequential"); return false; }
    QString err;
    int code = ExitFailed;
    if (pos.isEmpty()) {
        if (!o.container.isEmpty() || o.toSides.isEmpty()) { c.fail(ExitUsage, "convert without OUT only changes the side order (--to-sides); name OUT to change the container"); return false; }
        if (!c.img->convertSides(o.toSides, &err, &code)) { c.fail(code, err); return false; }
        Result r;
        r["toSides"] = o.toSides;
        *res = r;
        c.changes.append(Change{"convert", QString(), c.imagePath, QString(), "side order: " + o.toSides}.json());
        return true;
    }
    const QString out = pos[0];
    if (QFileInfo(out).exists() && QFileInfo(out).canonicalFilePath() == QFileInfo(c.imagePath).canonicalFilePath()) { c.fail(ExitUsage, "OUT is the image itself"); return false; }
    if (QFileInfo::exists(out) && !p.has("replace")) { c.fail(ExitConflict, QString("%1 exists: use --replace").arg(out), out); return false; }
    const QString target = c.g.dryRun ? tempBeside(out, "dwconv") : out;
    Result r;
    const bool ok = c.img->convertTo(target, o, &r, &err, &code);
    if (c.g.dryRun) QFile::remove(target);
    if (!ok) { c.fail(code, err); return false; }
    r["out"] = out;
    *res = r;
    c.changes.append(Change{"convert", QString(), out, c.imagePath, o.container}.json());
    if (!c.g.json && !c.g.quiet) printf("convert %s\n", qPrintable(out));
    return true;
}

bool cmdCopy(Ctx &c, const Parsed &p, const QStringList &pos, Result *res) {
    ImportOptions io;
    if (!lettersOption(c, p, &io)) return false;
    VolRef dest;
    if (!resolveVol(c, c.img.get(), p.value("to-vol"), true, &dest)) return false;
    const QString srcPath = pos[0];
    const bool same = QFileInfo(srcPath).exists() && QFileInfo(c.imagePath).exists() &&
                      QFileInfo(srcPath).canonicalFilePath() == QFileInfo(c.imagePath).canonicalFilePath();
    OpenedImage *src = c.img.get();
    if (!same) {
        if (!QFileInfo::exists(srcPath)) { c.fail(ExitNotFound, QString("%1: no such file").arg(srcPath), srcPath); return false; }
        OpenOutcome o = c.be()->open(srcPath, c.openOptions());
        if (o.status != OpenOutcome::Ok) { c.fail(exitFor(o), srcPath + ": " + o.message, srcPath); return false; }
        c.srcImg = std::move(o.image);
        src = c.srcImg.get();
    }
    VolRef sr;
    const bool keepSet = c.volSet;
    c.volSet = true;                                         // "vol" in the envelope is the destination
    const bool okSrc = resolveVol(c, src, p.value("vol", c.g.vol), false, &sr);
    c.volSet = keepSet || c.volSet;
    if (!okSrc) return false;
    QVector<TransferItem> items;
    QStringList froms;
    if (pos.size() == 1) {
        QVector<fs::Entry> l;
        QString why;
        if (!sr.v->list(Path(), &l, &why)) { c.fail(ExitFailed, why); return false; }
        for (const fs::Entry &e : l) {
            TransferItem t;
            if (!exportItem(c, sr, Path(), e, &t)) return false;
            items.append(t);
            froms << sr.v->displayPath(Path().child(e.name));
        }
    } else {
        for (int i = 1; i < pos.size(); ++i) {
            QVector<Path> nodes;
            if (!expandPaths(c, sr, pos[i], &nodes)) return false;
            for (const Path &n : nodes) {
                if (!exportNode(c, sr, n, &items)) return false;
                froms << sr.v->displayPath(n);
            }
        }
    }
    Path into;
    if (p.has("into") && !parseVolPath(c, dest, p.value("into"), &into)) return false;
    return runImport(c, dest, items, froms, into, io, res);
}

// ---------------------------------------------------------------------------------------------- the dispatcher

bool dispatch(Ctx &c, const QString &cmd, const Parsed &p, const QStringList &pos, Result *res) {
    if (cmd == "info") return cmdInfo(c, p, pos, res);
    if (cmd == "check") return cmdCheck(c, p, pos, res);
    if (cmd == "ls") return cmdLs(c, p, pos, res);
    if (cmd == "get") return cmdGet(c, p, pos, res);
    if (cmd == "put") return cmdPut(c, p, pos, res);
    if (cmd == "rm") return cmdRm(c, p, pos, res);
    if (cmd == "mkdir") return cmdMkdir(c, p, pos, res);
    if (cmd == "mv") return cmdMv(c, p, pos, res);
    if (cmd == "attr") return cmdAttr(c, p, pos, res);
    if (cmd == "title") return cmdTitle(c, p, pos, res);
    if (cmd == "boot") return cmdBoot(c, p, pos, res);
    if (cmd == "new") return cmdNew(c, p, pos, res);
    if (cmd == "convert") return cmdConvert(c, p, pos, res);
    if (cmd == "copy") return cmdCopy(c, p, pos, res);
    c.fail(ExitUsage, "unknown command " + cmd);
    return false;
}

const CmdDef *findCmd(const QVector<CmdDef> &t, const QString &n) {
    for (const CmdDef &d : t) if (d.name == n) return &d;
    return nullptr;
}

QString usageText(const AppInfo &app, const QVector<CmdDef> &t) {
    QString s = QString("usage: %1 [GLOBAL OPTIONS] COMMAND [OPTIONS] ARGS...\n\ncommands:\n").arg(app.tool);
    for (const CmdDef &d : t) s += QString("  %1 %2\n      %3\n").arg(d.name, d.synopsis, d.summary);
    s += "\nglobal options: --json --dry-run --vol=NAME --fs=ID --backup -q --help --version";
    for (const char *o : {"sides", "diskdefs", "charset"}) if (app.options.contains(QString::fromLatin1(o))) s += QString(" --") + o + "=...";
    s += " -f|--format=NAME\nSee docs/cli.md in DiskWranglerCore for the full reference.\n";
    return s;
}

bool globalsFrom(const Parsed &p, GlobalOpts *g) {
    g->json = p.has("json"); g->dryRun = p.has("dry-run"); g->backup = p.has("backup"); g->quiet = p.has("quiet");
    g->vol = p.value("vol"); g->fs = p.value("fs"); g->sides = p.value("sides"); g->format = p.value("format");
    g->charset = p.value("charset"); g->diskdefs = p.values("diskdefs");
    return true;
}

Parsed mergeParsed(const Parsed &a, const Parsed &b) {
    Parsed m = a;
    for (auto it = b.opts.begin(); it != b.opts.end(); ++it) m.opts[it.key()] = it.value();
    m.positional = b.positional;
    return m;
}

// ---------------------------------------------------------------------------------------------- script

struct ScriptLine {
    int line = 0;
    QString text, cmd;
    Parsed parsed;
    QStringList pos;
};

bool parseScript(Ctx &c, const QVector<CmdDef> &table, const QString &text, QVector<ScriptLine> *out) {
    static const QStringList forbidden = {"new", "convert", "script"};
    const QStringList lines = text.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        QString l = lines[i];
        if (l.endsWith('\r')) l.chop(1);
        QStringList words;
        QString err;
        if (!tokenise(l, &words, &err)) { c.fail(ExitUsage, QString("script line %1: %2").arg(i + 1).arg(err)); return false; }
        if (words.isEmpty()) continue;
        ScriptLine sl;
        sl.line = i + 1;
        sl.text = l.trimmed();
        sl.cmd = words[0];
        const CmdDef *d = findCmd(table, sl.cmd);
        if (!d) { c.fail(ExitUsage, QString("script line %1: unknown command '%2'").arg(sl.line).arg(sl.cmd)); return false; }
        if (forbidden.contains(sl.cmd)) { c.fail(ExitUsage, QString("script line %1: %2 can't be used in a script").arg(sl.line).arg(sl.cmd)); return false; }
        QVector<OptSpec> specs = d->opts;
        specs.append({"vol", QChar(), true, false});
        if (!parseArgs(words.mid(1), specs, &sl.parsed, &err)) {
            const bool glob = err.contains("--json") || err.contains("--dry-run") || err.contains("--backup") || err.contains("--fs") ||
                              err.contains("--format") || err.contains("--sides") || err.contains("--diskdefs") || err.contains("--charset");
            c.fail(ExitUsage, QString("script line %1: %2%3").arg(sl.line).arg(err, glob ? " (give it on the script command, not on a line)" : QString()));
            return false;
        }
        sl.pos = sl.parsed.positional;
        // positionals: copy SRC PATH...; everything else as on the command line without the image
        int minPos = d->minPos, maxPos = d->maxPos;
        if (sl.cmd == "put") minPos = 1;                       // HOST... DIR (or just DIR with --retro)
        if (sl.pos.size() < minPos || (maxPos >= 0 && sl.pos.size() > maxPos)) {
            c.fail(ExitUsage, QString("script line %1: usage: %2 %3").arg(sl.line).arg(d->name, d->synopsis.section(' ', 1)));
            return false;
        }
        out->append(sl);
    }
    return true;
}

bool cmdScript(Ctx &c, const Parsed &, const QStringList &pos, Result *res, const QVector<CmdDef> &table) {
    QByteArray text;
    if (pos.isEmpty() || pos[0] == "-") {
        QFile in;
        if (!in.open(stdin, QIODevice::ReadOnly)) { c.fail(ExitFailed, QString("cannot read standard input: %1").arg(in.errorString())); return false; }
        text = in.readAll();
    } else {
        QFile f(pos[0]);
        if (!f.open(QIODevice::ReadOnly)) { c.fail(ExitNotFound, QString("%1: %2").arg(pos[0], f.errorString()), pos[0]); return false; }
        text = f.readAll();
    }
    QVector<ScriptLine> lines;
    if (!parseScript(c, table, QString::fromUtf8(text), &lines)) return false;
    QJsonArray results;
    bool ok = true;
    for (const ScriptLine &sl : lines) {
        QJsonArray outerChanges = c.changes, outerWarnings = c.warnings;
        c.changes = QJsonArray();
        c.warnings = QJsonArray();
        const bool volWas = c.volSet;
        Result r;
        c.err = Error();
        Parsed p = sl.parsed;
        if (!p.has("vol") && !c.g.vol.isEmpty()) p.opts["vol"] << c.g.vol;
        const bool good = dispatch(c, sl.cmd, p, sl.pos, &r);
        QJsonObject lr;
        lr["line"] = sl.line;
        lr["text"] = sl.text;
        lr["command"] = sl.cmd;
        lr["exit"] = good ? 0 : c.err.code;
        lr["result"] = r;
        lr["changes"] = c.changes;
        lr["warnings"] = c.warnings;
        if (good) lr["error"] = QJsonValue(QJsonValue::Null);
        else {
            QJsonObject e;
            e["code"] = c.err.code;
            e["kind"] = exitKind(c.err.code);
            e["message"] = c.err.message;
            e["path"] = c.err.path.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(c.err.path);
            lr["error"] = e;
        }
        results.append(lr);
        for (const QJsonValue &v : c.changes) outerChanges.append(v);
        for (const QJsonValue &v : c.warnings) outerWarnings.append(v);
        c.changes = outerChanges;
        c.warnings = outerWarnings;
        (void)volWas;
        if (!good) { c.err.message = QString("script line %1: %2").arg(sl.line).arg(c.err.message); ok = false; break; }
    }
    Result o;
    o["lines"] = results;
    *res = o;
    return ok;
}

} // namespace

// ---------------------------------------------------------------------------------------------- public pieces

QString OpenedImage::volName(int mount) const {
    const fs::Disc &d = const_cast<OpenedImage *>(this)->disc();
    if (mount < 0 || mount >= d.mounts().size()) return QString();
    QString s = d.mounts()[mount].label.toLower();
    s.remove(' ');
    return s;
}

bool OpenedImage::convertTo(const QString &, const ConvertOptions &, QJsonObject *, QString *err, int *exit) {
    *err = "this program can't convert images";
    *exit = ExitUnsupported;
    return false;
}

bool OpenedImage::convertSides(const QString &, QString *err, int *exit) {
    *err = "this image has no side order to change";
    *exit = ExitUnsupported;
    return false;
}

int OpenOutcome::exitCode() const {
    switch (status) {
    case Ok: return ExitOk;
    case NotFound: return ExitNotFound;
    case Failed: return ExitFailed;
    case Usage: return ExitUsage;
    case Foreign: return ExitForeign;
    case Ambiguous: return ExitAmbiguous;
    case Unrecognised: return ExitUnrecognised;
    case Unsupported: return ExitUnsupported;
    }
    return ExitFailed;
}

QString OpenOutcome::verdictName() const {
    switch (verdict.action) {
    case fs::Verdict::Open: return "open";
    case fs::Verdict::Ask: return "ask";
    case fs::Verdict::Handoff: return "handoff";
    case fs::Verdict::None: break;
    }
    return "none";
}

void resolveVerdict(const fs::Verdict &v, const QString &fsOption, OpenOutcome *out) {
    out->verdict = v;
    if (!fsOption.isEmpty()) {
        for (int i = 0; i < v.ranked.size(); ++i) {
            if (v.ranked[i].fsId != fsOption) continue;
            out->chosen = i;
            if (v.ranked[i].foreign()) {
                out->status = OpenOutcome::Foreign;
                out->message = QString("this disc belongs to %1 (%2)").arg(v.ranked[i].owner.name, v.ranked[i].owner.releases);
            } else {
                out->status = OpenOutcome::Ok;
            }
            return;
        }
        out->status = OpenOutcome::Unrecognised;
        out->message = QString("nothing read this disc as '%1'").arg(fsOption);
        return;
    }
    switch (v.action) {
    case fs::Verdict::Open:
        out->chosen = v.best;
        out->status = OpenOutcome::Ok;
        break;
    case fs::Verdict::Ask:
        out->status = OpenOutcome::Ambiguous;
        out->message = QString("this disc could equally be %1 or %2: choose with --fs=%3 or --fs=%4")
                           .arg(v.ranked[v.best].name, v.ranked[v.rival].name, v.ranked[v.best].fsId, v.ranked[v.rival].fsId);
        break;
    case fs::Verdict::Handoff:
        out->chosen = v.best;
        out->status = OpenOutcome::Foreign;
        out->message = QString("this disc belongs to %1 (%2)").arg(v.ranked[v.best].owner.name, v.ranked[v.best].owner.releases);
        break;
    case fs::Verdict::None:
        out->status = OpenOutcome::Unrecognised;
        out->message = "nothing recognised this disc (--fs=ID tries a reading anyway)";
        break;
    }
}

QJsonObject containerInfo(const QString &path, const device::Container &c) {
    QString k;
    switch (c.kind()) {
    case device::Kind::Raw: k = "raw"; break;
    case device::Kind::Imd: k = "imd"; break;
    case device::Kind::Dsk: k = "dsk"; break;
    case device::Kind::Edsk: k = "edsk"; break;
    case device::Kind::Td0: k = "td0"; break;
    case device::Kind::None: k = "none"; break;
    }
    QJsonObject o;
    o["kind"] = k;
    o["name"] = device::kindName(c.kind());
    o["size"] = double(QFileInfo(path).size());
    o["readOnly"] = c.readOnlyContainer();
    return o;
}

static bool readHostOne(const QString &path, const HostFileFilter &accept, TransferItem *out, QString *err, int *exit) {
    const QFileInfo fi(path);
    if (!fi.exists()) { *err = QString("%1: no such file or folder").arg(path); *exit = ExitNotFound; return false; }
    TransferItem t;
    t.name = fi.fileName().isEmpty() ? QDir(path).dirName() : fi.fileName();
    t.when = fi.lastModified();
    if (fi.isDir()) {
        t.isDir = true;
        const QFileInfoList l = QDir(path).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::Name | QDir::IgnoreCase | QDir::DirsFirst);
        for (const QFileInfo &k : l) {
            TransferItem kid;
            const QString p = k.absoluteFilePath();
            if (k.isDir()) {
                if (!readHostOne(p, accept, &kid, err, exit)) return false;
            } else {
                QFile f(p);
                if (!f.open(QIODevice::ReadOnly)) { *err = QString("%1: cannot read (%2)").arg(p, f.errorString()); *exit = ExitFailed; return false; }
                kid.name = k.fileName();
                kid.when = k.lastModified();
                kid.data = f.readAll();
                if (accept && !accept(p, &kid)) continue;
            }
            t.kids.append(kid);
        }
    } else {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) { *err = QString("%1: cannot read (%2)").arg(path, f.errorString()); *exit = ExitFailed; return false; }
        t.data = f.readAll();
        if (accept && !accept(fi.absoluteFilePath(), &t)) { out->name.clear(); *out = TransferItem(); out->hostName = "\x01skip"; return true; }
    }
    *out = t;
    return true;
}

bool readHostTree(const QStringList &paths, const HostFileFilter &accept, QVector<TransferItem> *items, QString *err, int *exit) {
    for (const QString &p : paths) {
        TransferItem t;
        if (!readHostOne(p, accept, &t, err, exit)) return false;
        if (t.hostName == "\x01skip") continue;
        items->append(t);
    }
    return true;
}

} // namespace cli

// ---------------------------------------------------------------------------------------------- runCli

namespace {
using namespace cli;

QByteArray envelope(const Ctx &c, const QString &cmd, bool ok, bool saved, const Result &result, const QString &imageArg) {
    QJsonObject o;
    o["schema"] = 1;
    o["tool"] = c.app.tool;
    o["command"] = cmd;
    o["image"] = imageArg.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(imageArg);
    o["vol"] = c.volSet ? QJsonValue(c.volLabel) : QJsonValue(QJsonValue::Null);
    o["ok"] = ok;
    o["exit"] = c.err.code;
    o["dryRun"] = c.g.dryRun;
    o["saved"] = saved;
    o["result"] = result;
    o["changes"] = c.changes;
    o["warnings"] = c.warnings;
    if (ok) o["error"] = QJsonValue(QJsonValue::Null);
    else {
        QJsonObject e;
        e["code"] = c.err.code;
        e["kind"] = exitKind(c.err.code);
        e["message"] = c.err.message;
        e["path"] = c.err.path.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(c.err.path);
        o["error"] = e;
    }
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

} // namespace

int runCli(int argc, char **argv, const cli::AppInfo &app) {
    using namespace cli;
    QStringList args;
    for (int i = 1; i < argc; ++i) args << QString::fromLocal8Bit(argv[i]);
    {   // repeatable output: the clock the programs stamp discs with
        bool ok = false;
        const qint64 epoch = qEnvironmentVariable("SOURCE_DATE_EPOCH").toLongLong(&ok);
        if (ok) dw::setNow(QDateTime::fromSecsSinceEpoch(epoch));
    }
    const QVector<CmdDef> table = commandTable(app);
    const QVector<OptSpec> gspecs = globalSpecs(app);
    Ctx c(app);
    auto usageFail = [&](const QString &msg, const QString &cmd, bool json, const QString &image) {
        c.err.code = ExitUsage; c.err.message = msg;
        if (json) printf("%s", envelope(c, cmd, false, false, Result(), image).constData());
        fprintf(stderr, "%s%s\n", qPrintable(toolPrefix(c)), qPrintable(msg));
        return int(ExitUsage);
    };
    bool jsonWanted = args.contains("--json");
    Parsed lead;
    QStringList rest;
    QString perr;
    if (!parseLeading(args, gspecs, &lead, &rest, &perr)) return usageFail(perr, QString(), jsonWanted, QString());
    if (lead.has("version")) {
        printf("%s %s (DiskWranglerCore %s)\n", qPrintable(app.tool), qPrintable(app.appVersion), DW_CORE_VERSION);
        return 0;
    }
    if (rest.isEmpty()) {
        if (lead.has("help")) { printf("%s", qPrintable(usageText(app, table))); return 0; }
        fprintf(stderr, "%s", qPrintable(usageText(app, table)));
        return usageFail("no command given", QString(), jsonWanted, QString());
    }
    const QString cmd = rest[0];
    const CmdDef *def = findCmd(table, cmd);
    if (!def) return usageFail(QString("unknown command '%1' (try --help)").arg(cmd), cmd, jsonWanted, QString());
    QVector<OptSpec> specs = gspecs + def->opts;
    Parsed after;
    if (!parseArgs(rest.mid(1), specs, &after, &perr)) return usageFail(perr, cmd, jsonWanted, QString());
    Parsed p = mergeParsed(lead, after);
    if (p.has("help")) {
        printf("usage: %s %s %s\n  %s\n", qPrintable(app.tool), qPrintable(def->name), qPrintable(def->synopsis), qPrintable(def->summary));
        return 0;
    }
    globalsFrom(p, &c.g);
    const QStringList positional = p.positional;
    if (!c.g.format.isEmpty() && cmd != "new" && !app.options.contains("format-open"))
        return usageFail(QString("%1 takes --format only on new").arg(app.tool), cmd, c.g.json, QString());
    if (!c.g.sides.isEmpty() && cmd != "new" && c.g.sides != "auto" && c.g.sides != "interleaved" && c.g.sides != "sequential")
        return usageFail("--sides is auto, interleaved or sequential", cmd, c.g.json, QString());
    if (c.g.vol.toLower() == "all" && cmd != "ls" && cmd != "info")
        return usageFail("--vol=all is only for ls and info", cmd, c.g.json, QString());
    if (cmd == "get" && positional.size() >= 1 && positional.last() == "-" && c.g.json)
        return usageFail("get to '-' (stdout) can't be combined with --json", cmd, true, positional.value(0));

    // the argument count, and which positional is the image
    QStringList pos;
    QString imageArg;
    const bool isNew = cmd == "new", isCopy = cmd == "copy";
    int need = def->minPos + (isNew ? 0 : 1);
    if (isCopy) need = 2;                                      // SRC DEST [PATH...]
    if (isNew && p.has("list")) need = 0;
    const int count = positional.size();
    if (count < need || (def->maxPos >= 0 && count > def->maxPos + (isNew ? 0 : 1) + (isCopy ? 1 : 0)))
        return usageFail(QString("usage: %1 %2 %3").arg(app.tool, def->name, def->synopsis), cmd, c.g.json, positional.value(0));
    if (isNew) { pos = positional; imageArg = positional.value(0); }
    else if (isCopy) { imageArg = positional[1]; pos = QStringList{positional[0]} + positional.mid(2); }
    else { imageArg = positional[0]; pos = positional.mid(1); }
    c.imagePath = imageArg;

    Result result;
    bool ok = true, saved = false;
    if (!isNew) {
        if (!QFileInfo::exists(imageArg)) {
            c.fail(ExitNotFound, QString("%1: no such file").arg(imageArg), imageArg);
            ok = false;
        } else {
            c.outcome = app.backend->open(imageArg, c.openOptions());
            if (c.outcome.status == OpenOutcome::Ok) c.img = std::move(c.outcome.image);
            else {
                c.fail(c.outcome.exitCode(), c.outcome.message);
                ok = false;
                if (cmd == "info") {
                    fillInfo(c, p, &result);
                    if (!c.g.json && !c.g.quiet) infoText(c, result);          // what was found, and the readings --fs chooses between
                }
                if (c.outcome.status == OpenOutcome::Foreign && c.outcome.chosen >= 0) {
                    result["owner"] = otherAppJson(c.outcome.verdict.ranked[c.outcome.chosen].owner);
                } else if (c.outcome.status == OpenOutcome::Foreign) {
                    result["owner"] = QJsonValue(QJsonValue::Null);
                }
                if (c.outcome.status == OpenOutcome::Ambiguous) {
                    QJsonArray two;
                    two.append(candidateJson(c.outcome.verdict.ranked[c.outcome.verdict.best]));
                    two.append(candidateJson(c.outcome.verdict.ranked[c.outcome.verdict.rival]));
                    result["candidates"] = two;
                }
            }
        }
    }
    if (ok && c.img && !c.g.charset.isEmpty()) c.img->setCharset(c.g.charset);
    if (ok) {
        if (cmd == "script") ok = cmdScript(c, p, pos, &result, table);
        else ok = dispatch(c, cmd, p, pos, &result);
    }
    if (ok && c.img && cmd != "convert" && c.img->dirty()) {
        const fs::Report rep = c.img->disc().validateAll();
        if (!rep.ok()) {
            c.fail(ExitCheck, "the disc failed its check, so nothing was written: " + rep.errors.join("; "));
            ok = false;
        } else if (!c.g.dryRun) {
            QString err;
            if (!c.img->save(c.g.backup, &err)) { c.fail(ExitSave, err); ok = false; }
            else saved = true;
        }
    } else if (ok && c.img && cmd == "convert" && c.img->dirty()) {            // an in-place change of side order
        if (!c.g.dryRun) {
            QString err;
            if (!c.img->save(c.g.backup, &err)) { c.fail(ExitSave, err); ok = false; } else saved = true;
        }
    }
    if (!ok && c.img) c.img->disc().revert();
    if (isNew && ok && !c.g.dryRun) saved = true;
    if (c.g.json) printf("%s", envelope(c, cmd, ok, saved, result, imageArg).constData());
    if (!ok) fprintf(stderr, "%s%s\n", qPrintable(toolPrefix(c)), qPrintable(c.err.message));
    if (!c.g.quiet || !ok)
        for (const QJsonValue &w : c.warnings) if (!c.g.json) fprintf(stderr, "%swarning: %s\n", qPrintable(toolPrefix(c)), qPrintable(w.toString()));
    fflush(stdout);
    return ok ? 0 : c.err.code;
}

} // namespace dw
