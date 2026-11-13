/*
 * The import session - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include "importsession.h"

namespace dw {
namespace ui {

static xfer::ImportError sentence(const QString &text) {
    xfer::ImportError e;
    e.kind = xfer::ImportError::Failed;
    e.message = e.why = text;
    return e;
}

xfer::ImportResult ImportSession::run(QVector<TransferItem> items, const QStringList &froms, const fs::Path &dir,
                                      const QString &readOnlyWhy, const QVector<xfer::ImportError> &readErrors) {
    xfer::ImportResult r;
    r.errors = readErrors;
    if (m_h.prepare) m_h.prepare(&items);
    if (!readOnlyWhy.isEmpty()) { r.errors << sentence("The disc is read-only: " + readOnlyWhy); return r; }
    fs::Volume *v = m_disc ? m_disc->volume(m_mount) : nullptr;
    if (!v) { r.errors << sentence("no disc"); return r; }
    if (m_h.beforeRun && !m_h.beforeRun(items)) { r.cancelled = true; return r; }

    xfer::Importer imp(v, m_disc->mounts().value(m_mount).label, m_h.options);
    imp.setOnError(xfer::OnError::Continue);
    if (m_h.conflict) imp.setConflictCallback(m_h.conflict);
    if (m_h.dirHook) imp.setDirHook(m_h.dirHook);
    if (m_h.place) imp.setPlaceHook(m_h.place);
    if (m_h.attrs) imp.setAttrsHook(m_h.attrs);
    if (m_h.errorHook) imp.setErrorHook(m_h.errorHook);
    fs::Disc *disc = m_disc;
    imp.setItemGuard([disc](const std::function<bool(QString *)> &change, QString *err) { return disc->transact(change, err); });
    xfer::ImportError e;
    imp.import(items, froms, dir, &r, &e);          // under Continue it always finishes (or is cancelled): errors in r
    return r;
}

} // namespace ui
} // namespace dw
