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
#include "output.h"

namespace dw {
namespace cli {

QString exitKind(int code) {
    switch (code) {
    case ExitOk: return "ok";
    case ExitFailed: return "failed";
    case ExitUsage: return "usage";
    case ExitNotFound: return "not-found";
    case ExitCheck: return "check";
    case ExitConflict: return "conflict";
    case ExitUnsupported: return "unsupported";
    case ExitForeign: return "foreign";
    case ExitAmbiguous: return "ambiguous";
    case ExitUnrecognised: return "unrecognised";
    case ExitSave: return "save";
    }
    return "failed";
}

QJsonValue dateJson(const QDateTime &d) {
    return d.isValid() ? QJsonValue(d.toString("yyyy-MM-ddTHH:mm:ss")) : QJsonValue(QJsonValue::Null);
}

QString hexDigits(quint32 v, int bits) {
    const int n = (bits + 3) / 4;
    return QString("%1").arg(v, n, 16, QChar('0')).toUpper();
}

QJsonValue attrValueJson(const fs::AttrSpec &s, const QVariant &v) {
    switch (s.type) {
    case fs::AttrSpec::Bool: return v.toBool();
    case fs::AttrSpec::Hex: return hexDigits(v.toUInt(), s.bits);
    case fs::AttrSpec::Int: return v.toInt();
    case fs::AttrSpec::Choice: {
        const int i = v.toInt();
        QJsonObject o;
        o["index"] = i;
        o["name"] = i >= 0 && i < s.choices.size() ? QJsonValue(s.choices[i]) : QJsonValue(QJsonValue::Null);
        return o;
    }
    case fs::AttrSpec::Text: return v.toString();
    case fs::AttrSpec::Date: return dateJson(v.toDateTime());
    }
    return QJsonValue();
}

QJsonObject attrsJson(const QVector<fs::AttrSpec> &specs, const fs::AttrValues &values, bool isDir) {
    QJsonObject o;
    for (const fs::AttrSpec &s : specs)
        if (values.contains(s.key) && (!isDir || s.dirs)) o[s.key] = attrValueJson(s, values.value(s.key));
    return o;
}

QJsonObject attrSpecJson(const fs::AttrSpec &s) {
    static const char *const types[] = {"bool", "hex", "int", "choice", "text", "date"};
    QJsonObject o;
    o["key"] = s.key;
    o["label"] = s.label;
    o["type"] = types[int(s.type)];
    o["bits"] = s.bits;
    o["choices"] = QJsonArray::fromStringList(s.choices);
    o["editable"] = s.editable;
    o["dirs"] = s.dirs;
    return o;
}

QJsonArray attrSpecsJson(const QVector<fs::AttrSpec> &specs) {
    QJsonArray a;
    for (const fs::AttrSpec &s : specs) a.append(attrSpecJson(s));
    return a;
}

QJsonObject capsJson(const fs::Caps &c) {
    QJsonObject o;
    o["dirs"] = c.dirs;
    o["namespaces"] = c.namespaces;
    o["rename"] = c.rename;
    o["renameAcrossDirs"] = c.renameAcrossDirs;
    o["title"] = c.title;
    o["dates"] = c.dates;
    o["settableDates"] = c.settableDates;
    o["contiguousOnly"] = c.contiguousOnly;
    o["maxEntriesPerDir"] = c.maxEntriesPerDir;
    return o;
}

QJsonObject volumeInfoJson(const fs::VolumeInfo &i) {
    QJsonObject o;
    o["title"] = i.title;
    o["total"] = double(i.total);
    o["free"] = double(i.free);
    o["largestFree"] = double(i.largestFree);
    o["entriesFree"] = i.entriesFree;
    return o;
}

QJsonObject entryJson(const fs::Volume &v, const fs::Path &dir, const fs::Entry &e) {
    static const char *const kinds[] = {"file", "dir", "namespace"};
    const fs::Path p = dir.child(e.name);
    QJsonObject o;
    o["name"] = e.name;
    o["path"] = QJsonArray::fromStringList(p.parts);
    o["display"] = v.displayPath(p);
    o["kind"] = kinds[int(e.kind)];
    o["size"] = double(e.size);
    o["sizeExact"] = e.sizeExact;
    o["modified"] = dateJson(e.modified);
    o["created"] = dateJson(e.created);
    o["accessed"] = dateJson(e.accessed);
    o["attrs"] = attrsJson(v.fileAttrs(), e.attrs, e.isDir());
    o["problem"] = e.problem.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(e.problem);
    return o;
}

QJsonObject otherAppJson(const fs::OtherApp &a) {
    QJsonObject o;
    o["id"] = a.id;
    o["name"] = a.name;
    o["releases"] = a.releases;
    return o;
}

QJsonObject candidateJson(const fs::Candidate &c) {
    QJsonObject o;
    o["fsId"] = c.fsId;
    o["name"] = c.name;
    o["description"] = c.description;
    o["tier"] = fs::tierName(c.tier);
    o["probe"] = c.probe;
    o["evidence"] = QJsonArray::fromStringList(c.evidence);
    o["notes"] = QJsonArray::fromStringList(c.notes);
    o["safeToWrite"] = c.safeToWrite;
    o["supported"] = c.supported;
    return o;
}

bool parseAttrValue(const fs::AttrSpec &s, const QString &text, QVariant *out, QString *err) {
    const QString t = text.trimmed();
    switch (s.type) {
    case fs::AttrSpec::Bool: {
        const QString l = t.toLower();
        if (l == "1" || l == "on" || l == "yes" || l == "true") { *out = true; return true; }
        if (l == "0" || l == "off" || l == "no" || l == "false") { *out = false; return true; }
        *err = QString("%1: '%2' is not a yes/no value (1, 0, on, off, yes, no, true, false)").arg(s.key, text);
        return false;
    }
    case fs::AttrSpec::Hex: {
        QString h = t;
        if (h.startsWith('&') || h.startsWith('$')) h = h.mid(1);
        else if (h.startsWith("0x", Qt::CaseInsensitive)) h = h.mid(2);
        bool ok = false;
        const qulonglong n = h.isEmpty() ? 0 : h.toULongLong(&ok, 16);
        if (h.isEmpty() || !ok || (s.bits < 32 && n >> s.bits)) {
            *err = QString("%1: '%2' is not a hexadecimal number of at most %3 bits").arg(s.key, text).arg(s.bits);
            return false;
        }
        *out = quint32(n);
        return true;
    }
    case fs::AttrSpec::Int: {
        bool ok = false;
        const int n = t.toInt(&ok);
        if (!ok) { *err = QString("%1: '%2' is not a number").arg(s.key, text); return false; }
        *out = n;
        return true;
    }
    case fs::AttrSpec::Choice: {
        bool ok = false;
        const int n = t.toInt(&ok);
        if (ok && n >= 0 && n < s.choices.size()) { *out = n; return true; }
        for (int i = 0; i < s.choices.size(); ++i)
            if (s.choices[i].compare(t, Qt::CaseInsensitive) == 0) { *out = i; return true; }
        *err = QString("%1: '%2' is not one of %3").arg(s.key, text, s.choices.join(", "));
        return false;
    }
    case fs::AttrSpec::Text: *out = text; return true;
    case fs::AttrSpec::Date: {
        const QDateTime d = QDateTime::fromString(t, Qt::ISODate);
        if (!d.isValid()) { *err = QString("%1: '%2' is not an ISO 8601 date").arg(s.key, text); return false; }
        *out = d;
        return true;
    }
    }
    return false;
}

} // namespace cli
} // namespace dw
