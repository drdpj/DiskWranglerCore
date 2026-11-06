/*
 * File viewers - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include "viewers.h"
#include "textviews.h"

#include <algorithm>

namespace dw {

void ViewerRegistry::add(std::shared_ptr<FileViewer> v) {
    remove(v->id());
    m_viewers.append(v);
    std::stable_sort(m_viewers.begin(), m_viewers.end(),
                     [](const std::shared_ptr<FileViewer> &a, const std::shared_ptr<FileViewer> &b) { return a->order() < b->order(); });
}

void ViewerRegistry::remove(const QString &id) {
    for (int i = 0; i < m_viewers.size(); ++i)
        if (m_viewers[i]->id() == id) { m_viewers.removeAt(i); return; }
}

std::shared_ptr<FileViewer> ViewerRegistry::find(const QString &id) const {
    for (const auto &v : m_viewers) if (v->id() == id) return v;
    return nullptr;
}

std::shared_ptr<FileViewer> ViewerRegistry::best(const QByteArray &data, const QVariantMap &ctx) const {
    std::shared_ptr<FileViewer> pick;
    int top = -1;
    for (const auto &v : m_viewers) {
        int s = v->accepts(data, ctx);
        if (s > top) { top = s; pick = v; }
    }
    return pick;
}

namespace {

class PlainViewer : public FileViewer {
public:
    QString id() const override { return "plain"; }
    QString name() const override { return "Text"; }
    int order() const override { return 0; }
    int accepts(const QByteArray &data, const QVariantMap &) const override { return text::looksLikeText(data) ? 2 : 0; }
    ViewResult render(const QByteArray &data, const QVariantMap &) const override { return {text::plain(data), QString()}; }
};

class HexViewer : public FileViewer {
public:
    QString id() const override { return "hex"; }
    QString name() const override { return "Hex dump"; }
    int order() const override { return 10; }
    int accepts(const QByteArray &, const QVariantMap &) const override { return 1; }     // anything can be shown as hex
    QString saveSuffix() const override { return ".hex.txt"; }
    ViewResult render(const QByteArray &data, const QVariantMap &) const override { return {text::hexDump(data), QString()}; }
};

} // namespace

void addDefaultViewers(ViewerRegistry &r) {
    r.add(std::make_shared<PlainViewer>());
    r.add(std::make_shared<HexViewer>());
}

} // namespace dw
