/*
 * Discs and transactions - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include "disc.h"

namespace dw {
namespace fs {

bool Disc::reloadAll(QString *err) {
    bool ok = true;
    for (const Mount &m : m_mounts) {
        QString e;
        if (m.vol && !m.vol->reload(&e)) {
            if (ok && err) *err = m.label + ": " + e;
            ok = false;
        }
    }
    return ok;
}

bool Disc::transact(const std::function<bool(QString *)> &f, QString *err) {
    if (!m_c) { *err = "no disc"; return false; }
    const device::Container::Snapshot before = m_c->snapshot();
    QString e;
    if (f(&e)) return true;
    m_c->restore(before);
    QString re;
    if (!reloadAll(&re)) e += " (and reading the disc again failed: " + re + ")";
    *err = e;
    return false;
}

Report Disc::validateAll() const {
    Report r;
    for (const Mount &m : m_mounts) {
        if (!m.vol) continue;
        const Report v = m.vol->check();
        const QString p = m_mounts.size() > 1 ? m.label + ": " : QString();
        for (const QString &s : v.errors) r.errors << p + s;
        for (const QString &s : v.warnings) r.warnings << p + s;
        for (const QString &s : v.info) r.info << p + s;
    }
    return r;
}

bool Disc::commit(device::Container::SaveOptions o, QString *err) {
    if (!m_c) { *err = "no disc"; return false; }
    const std::function<bool(QString *)> also = o.validate;
    o.validate = [this, also](QString *e) {
        const Report r = validateAll();
        if (!r.ok()) { *e = "The disc failed its check, so NOTHING was written.\n\n" + r.errors.join("\n"); return false; }
        return !also || also(e);
    };
    const bool ok = m_c->commit(o, err);
    QString re;
    if (!reloadAll(&re) && ok) { *err = "saved, but reading the disc again failed: " + re; return false; }
    return ok;
}

void Disc::revert() {
    if (!m_c) return;
    m_c->discardPending();
    reloadAll(nullptr);
}

} // namespace fs
} // namespace dw
