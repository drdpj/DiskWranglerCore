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

// Command-line arguments: the option tables, the parser and the script tokeniser. QtCore only.

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>

namespace dw {
namespace cli {

struct OptSpec {
    QString name;                        // long name without the dashes ("dry-run")
    QChar shortName;                     // '\0' if none
    bool takesValue = false;
    bool repeat = false;                 // may be given more than once
};

struct Parsed {
    QStringList positional;
    QMap<QString, QStringList> opts;     // by long name; a flag has one empty value per use
    bool has(const QString &n) const { return opts.contains(n); }
    QString value(const QString &n, const QString &def = QString()) const {
        return opts.contains(n) && !opts[n].isEmpty() ? opts[n].last() : def;
    }
    QStringList values(const QString &n) const { return opts.value(n); }
};

// Parses args against specs. Options may be anywhere; "--" ends them; "--opt=value" and "--opt value" (and "-f NAME",
// "-fNAME"); short flags may be joined ("-Rd"). A lone "-" is a positional. Unknown option, missing value, a value
// given to a flag or a repeated option that doesn't repeat: false and err.
bool parseArgs(const QStringList &args, const QVector<OptSpec> &specs, Parsed *out, QString *err);

// Splits off the leading options of args that are in specs (and their values), stopping at the first positional:
// used to find the command word. rest gets the arguments from that positional on (the command word first).
bool parseLeading(const QStringList &args, const QVector<OptSpec> &specs, Parsed *out, QStringList *rest, QString *err);

// One script line as words: whitespace separates, '...' is literal, "..." takes \" and \\ escapes, # starts a comment
// outside quotes. false (and err) for an unterminated quote.
bool tokenise(const QString &line, QStringList *out, QString *err);

// Matches a name against a pattern with * and ? (the whole name).
bool wildcardMatch(const QString &pattern, const QString &name, Qt::CaseSensitivity cs);
bool hasWildcard(const QString &s);

} // namespace cli
} // namespace dw
