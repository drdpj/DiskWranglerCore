/*
 * UI helpers - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// Small helpers both programs' windows use. Header only.

#include <QKeySequence>
#include <QList>
#include <QString>
#include <QStringList>

namespace dw {

// "$   [BeebSCSI0]", "C:\   [Master 512]", "Side 1   [WORK]": the same shape for every pane's root row.
inline QString rootLabel(const QString &base, const QString &title) {
    return title.isEmpty() ? base : base + "   [" + title + "]";
}

// "*.dsk *.img" -> "*.dsk *.DSK *.img *.IMG": file dialogs match case-sensitively on Linux, and discs come as
// DISK.DSK too. Every pattern gets an upper-case twin ("dosboot*" -> "DOSBOOT*"), which a pattern that is
// already upper case or has no letters (a bare "*") doesn't need.
inline QString anyCase(const QString &patterns) {
    QStringList out;
    for (const QString &p : patterns.split(' ', Qt::SkipEmptyParts)) {
        out << p;
        if (p.toUpper() != p) out << p.toUpper();
    }
    return out.join(' ');
}

// How fmtBytes() shows a size under 1 KB: Acorn Disk Wrangler has always shown "0.5 KB" (it only ever formats
// disc and partition sizes), CP/M Disk Wrangler shows "300 bytes" (it formats free space, which can be tiny).
enum class SmallSizes { AsKb, AsBytes };

// 1.4 MB, 360.0 KB, and below 1 KB either "0.5 KB" or "300 bytes" (see SmallSizes).
inline QString fmtBytes(qint64 b, SmallSizes small = SmallSizes::AsKb) {
    if (b >= 1024LL * 1024) return QString("%1 MB").arg(double(b) / (1024.0 * 1024.0), 0, 'f', 1);
    if (b >= 1024 || small == SmallSizes::AsKb) return QString("%1 KB").arg(double(b) / 1024.0, 0, 'f', 1);
    return QString("%1 bytes").arg(b);
}

// Delete: the platform's Delete key(s), and on macOS Finder's Cmd+Backspace too (most Mac keyboards have no
// forward-delete key).
inline QList<QKeySequence> deleteKeys() {
    QList<QKeySequence> k = QKeySequence::keyBindings(QKeySequence::Delete);
#ifdef Q_OS_MACOS
    k << QKeySequence("Ctrl+Backspace");               // Ctrl is the Command key on macOS
#endif
    return k;
}

} // namespace dw
