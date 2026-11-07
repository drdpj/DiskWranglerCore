/*
 * Acorn .inf sidecar files - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include "inf.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace dw {
namespace dfs {

static quint32 parseInfAddr(const QString &tok, bool *ok) {
    quint32 v = tok.toUInt(ok, 16);
    if (*ok && tok.size() <= 6 && v >= 0xFF0000) v |= 0xFF000000u;   // 6-digit FFxxxx = I/O processor address
    return v;
}

QString infPathFor(const QString &hostPath) {
    for (const QString &ext : {QString(".inf"), QString(".INF")})
        if (QFileInfo::exists(hostPath + ext)) return hostPath + ext;
    return QString();
}

bool parseInfLine(const QString &text, Inf *out) {
    const QString line = text.trimmed();
    QString name, rest;
    if (line.startsWith('"')) {
        int q = line.indexOf('"', 1);
        if (q < 0) return false;
        name = line.mid(1, q - 1);
        rest = line.mid(q + 1).trimmed();
    } else {
        int sp = line.indexOf(QRegularExpression("\\s"));
        name = sp < 0 ? line : line.left(sp);
        rest = sp < 0 ? QString() : line.mid(sp + 1).trimmed();
    }
    QStringList t = rest.split(QRegularExpression("\\s+"));
    t.removeAll(QString());
    Inf d;
    QStringList parts = name.split('.');
    d.name = parts.last();
    if (parts.size() >= 2 && parts[parts.size() - 2].size() == 1) d.dir = parts[parts.size() - 2][0];   // "D.NAME"
    bool ok = false;
    if (t.size() > 0) d.load = parseInfAddr(t[0], &ok);
    if (t.size() > 1) d.exec = parseInfAddr(t[1], &ok);
    quint32 attr = 0;
    for (int i = 2; i < t.size(); ++i) {
        const QString &k = t[i];
        if (k.compare("L", Qt::CaseInsensitive) == 0 || k.compare("Locked", Qt::CaseInsensitive) == 0) {
            attr |= InfL; d.hasAccess = true;
        } else if (i == 3) {
            quint32 a = k.toUInt(&ok, 16);
            if (ok) { attr |= a; d.hasAccess = true; }
        }
    }
    if (d.hasAccess) d.access = quint8(attr & 0x0F);
    *out = d;
    return true;
}

bool readInf(const QString &hostPath, Inf *out) {
    const QString p = infPathFor(hostPath);
    if (p.isEmpty()) return false;
    QFile f(p);
    if (!f.open(QIODevice::ReadOnly)) return false;
    return parseInfLine(QString::fromLatin1(f.readLine()), out);
}

QString infLineDfs(QChar dir, const QString &name, quint32 load, quint32 exec, quint32 length, bool locked) {
    return QString::asprintf("%c.%-7s %08X %08X %08X %02X\n", dir.toLatin1(), qPrintable(name), load, exec, length,
                             locked ? 8 : 3);
}

QString infLineAdfs(const QString &name, quint32 load, quint32 exec, quint32 length, quint8 access) {
    return QString::asprintf("%-10s %08X %08X %08X %02X\n", qPrintable(name), load, exec, length, unsigned(access));
}

} // namespace dfs
} // namespace dw
