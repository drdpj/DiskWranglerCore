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
#include "args.h"

#include <QRegularExpression>

namespace dw {
namespace cli {

static const OptSpec *findLong(const QVector<OptSpec> &specs, const QString &n) {
    for (const OptSpec &s : specs)
        if (s.name == n) return &s;
    return nullptr;
}

static const OptSpec *findShort(const QVector<OptSpec> &specs, QChar c) {
    for (const OptSpec &s : specs)
        if (!s.shortName.isNull() && s.shortName == c) return &s;
    return nullptr;
}

static bool addOpt(Parsed *out, const OptSpec &s, const QString &value, QString *err) {
    if (out->opts.contains(s.name) && !s.repeat) { *err = QString("--%1 given twice").arg(s.name); return false; }
    out->opts[s.name] << value;
    return true;
}

// Reads one option starting at args[i] (which starts with '-' and isn't "-" or "--"); advances i past its value.
static bool readOption(const QStringList &args, int *i, const QVector<OptSpec> &specs, Parsed *out, QString *err, bool *unknown) {
    const QString a = args[*i];
    *unknown = false;
    if (a.startsWith("--")) {
        QString name = a.mid(2), value;
        bool hasValue = false;
        const int eq = name.indexOf('=');
        if (eq >= 0) { value = name.mid(eq + 1); name = name.left(eq); hasValue = true; }
        const OptSpec *s = findLong(specs, name);
        if (!s) { *unknown = true; *err = "unknown option --" + name; return false; }
        if (s->takesValue) {
            if (!hasValue) {
                if (*i + 1 >= args.size()) { *err = QString("--%1 needs a value").arg(name); return false; }
                value = args[++*i];
            }
        } else if (hasValue) { *err = QString("--%1 takes no value").arg(name); return false; }
        ++*i;
        return addOpt(out, *s, value, err);
    }
    // short flags
    int k = 1;
    for (; k < a.size(); ++k) {
        const OptSpec *s = findShort(specs, a[k]);
        if (!s) { *unknown = true; *err = QString("unknown option -%1").arg(a[k]); return false; }
        if (s->takesValue) {
            QString value = a.mid(k + 1);
            if (value.isEmpty()) {
                if (*i + 1 >= args.size()) { *err = QString("-%1 needs a value").arg(a[k]); return false; }
                value = args[++*i];
            }
            ++*i;
            return addOpt(out, *s, value, err);
        }
        if (!addOpt(out, *s, QString(), err)) return false;
    }
    ++*i;
    return true;
}

bool parseArgs(const QStringList &args, const QVector<OptSpec> &specs, Parsed *out, QString *err) {
    *out = Parsed();
    bool dashdash = false;
    for (int i = 0; i < args.size();) {
        const QString &a = args[i];
        if (dashdash || a == "-" || !a.startsWith('-') || a.size() == 1) { out->positional << a; ++i; continue; }
        if (a == "--") { dashdash = true; ++i; continue; }
        bool unknown;
        if (!readOption(args, &i, specs, out, err, &unknown)) return false;
    }
    return true;
}

bool parseLeading(const QStringList &args, const QVector<OptSpec> &specs, Parsed *out, QStringList *rest, QString *err) {
    *out = Parsed();
    int i = 0;
    while (i < args.size()) {
        const QString &a = args[i];
        if (a == "-" || !a.startsWith('-')) break;
        if (a == "--") { ++i; break; }
        bool unknown;
        if (!readOption(args, &i, specs, out, err, &unknown)) {
            if (unknown) { err->clear(); break; }        // not a global option: the command's own (or an error there)
            return false;
        }
    }
    *rest = args.mid(i);
    return true;
}

bool tokenise(const QString &line, QStringList *out, QString *err) {
    out->clear();
    QString cur;
    bool inWord = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (c == '#' ) break;
        if (c.isSpace()) { if (inWord) { out->append(cur); cur.clear(); inWord = false; } continue; }
        inWord = true;
        if (c == '\'') {
            const int j = line.indexOf('\'', i + 1);
            if (j < 0) { *err = "unterminated ' quote"; return false; }
            cur += line.mid(i + 1, j - i - 1);
            i = j;
        } else if (c == '"') {
            int j = i + 1;
            for (; j < line.size() && line[j] != '"'; ++j) {
                if (line[j] == '\\' && j + 1 < line.size() && (line[j + 1] == '"' || line[j + 1] == '\\')) ++j;
                cur += line[j];
            }
            if (j >= line.size()) { *err = "unterminated \" quote"; return false; }
            i = j;
        } else {
            cur += c;
        }
    }
    if (inWord) out->append(cur);
    return true;
}

bool hasWildcard(const QString &s) { return s.contains('*') || s.contains('?'); }

bool wildcardMatch(const QString &pattern, const QString &name, Qt::CaseSensitivity cs) {
    QString re = "^";
    for (const QChar c : pattern) {
        if (c == '*') re += ".*";
        else if (c == '?') re += ".";
        else re += QRegularExpression::escape(QString(c));
    }
    re += "$";
    QRegularExpression rx(re, cs == Qt::CaseInsensitive ? QRegularExpression::CaseInsensitiveOption : QRegularExpression::NoPatternOption);
    return rx.match(name).hasMatch();
}

} // namespace cli
} // namespace dw
