/*
 * New-disc presets - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include "presets.h"

namespace dw {
namespace fs {

void PresetRegistry::add(const Preset &p) {
    for (Preset &q : m_presets)
        if (q.id.compare(p.id, Qt::CaseInsensitive) == 0) { q = p; return; }
    m_presets.append(p);
}

void PresetRegistry::removeSource(const QString &source) {
    for (int i = m_presets.size() - 1; i >= 0; --i)
        if (m_presets[i].source == source) m_presets.remove(i);
}

QVector<Preset> PresetRegistry::fromSource(const QString &source) const {
    QVector<Preset> out;
    for (const Preset &p : m_presets)
        if (p.source == source) out.append(p);
    return out;
}

const Preset *PresetRegistry::find(const QString &id) const {
    for (const Preset &p : m_presets)
        if (p.id.compare(id, Qt::CaseInsensitive) == 0) return &p;
    return nullptr;
}

PresetRegistry &presets() {
    static PresetRegistry r;
    return r;
}

} // namespace fs
} // namespace dw
