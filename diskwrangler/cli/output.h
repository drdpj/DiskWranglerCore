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

// The command line's JSON (docs/cli.md, "Output"): how entries, attributes, candidates and capabilities are encoded,
// and how an attribute value typed on the command line is read. QtCore only.

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVariant>

#include <diskwrangler/cli/cli.h>
#include <diskwrangler/fs/detection.h>
#include <diskwrangler/fs/volume.h>

namespace dw {
namespace cli {

QJsonValue dateJson(const QDateTime &d);                              // "yyyy-MM-ddTHH:mm:ss", or null
QJsonValue attrValueJson(const fs::AttrSpec &s, const QVariant &v);   // by type: bool, hex string, number, {index,name}, ...
QJsonObject attrsJson(const QVector<fs::AttrSpec> &specs, const fs::AttrValues &values, bool isDir);
QJsonObject attrSpecJson(const fs::AttrSpec &s);
QJsonArray attrSpecsJson(const QVector<fs::AttrSpec> &specs);
QJsonObject capsJson(const fs::Caps &c);
QJsonObject volumeInfoJson(const fs::VolumeInfo &i);
// An entry of the directory dir of volume v.
QJsonObject entryJson(const fs::Volume &v, const fs::Path &dir, const fs::Entry &e);
QJsonObject candidateJson(const fs::Candidate &c);
QJsonObject otherAppJson(const fs::OtherApp &o);

// Reads an attribute value as typed ("1", "on", "&1900", "run", "2024-01-01T00:00:00"): false and err if it isn't one.
bool parseAttrValue(const fs::AttrSpec &s, const QString &text, QVariant *out, QString *err);

QString hexDigits(quint32 v, int bits);                               // upper case, ceil(bits/4) digits

} // namespace cli
} // namespace dw
