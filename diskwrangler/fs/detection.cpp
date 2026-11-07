/*
 * Detection - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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
#include "detection.h"

#include <algorithm>

namespace dw {
namespace fs {

QString tierName(Tier t) {
    switch (t) {
    case Tier::Certain: return "certain";
    case Tier::Likely: return "likely";
    default: return "possible";
    }
}

OtherApp acornDiskWrangler() {
    return {"acorndiskwrangler", "Acorn Disk Wrangler", "Acorn Disk Wrangler", "https://github.com/drdpj/AcornDiskWrangler/releases"};
}

OtherApp cpmDiskWrangler() {
    return {"cpmdiskwrangler", "CP/M Disk Wrangler", "CPM Disk Wrangler", "https://github.com/drdpj/CPMDiskWrangler/releases"};
}

QByteArray ProbeContext::stored(qint64 off, int len) const {
    QByteArray b;
    QString e;
    if (!container || !container->readStored(off, len, &b, &e)) b.clear();
    if (b.size() < len) b.append(QByteArray(len - b.size(), 0));
    return b;
}

qint64 ProbeContext::size() const {
    if (!container) return 0;
    return container->kind() == device::Kind::Raw ? container->rawFileSize() : container->storedSize();
}

Verdict decide(const QVector<Candidate> &candidates) {
    Verdict v;
    v.ranked = candidates;
    // best tier first; equal tiers keep the probes' order and each probe's own ranking
    std::stable_sort(v.ranked.begin(), v.ranked.end(),
                     [](const Candidate &a, const Candidate &b) { return int(a.tier) > int(b.tier); });
    if (v.ranked.isEmpty()) return v;
    v.best = 0;
    const Candidate &best = v.ranked[0];
    // the next candidate from another probe
    for (int i = 1; i < v.ranked.size(); ++i)
        if (v.ranked[i].probe != best.probe) { v.rival = i; break; }
    const Candidate *rival = v.rival >= 0 ? &v.ranked[v.rival] : nullptr;
    if (rival && rival->tier == best.tier && best.tier != Tier::Possible) {
        v.action = Verdict::Ask;
        return v;
    }
    v.rival = -1;
    v.action = best.foreign() ? Verdict::Handoff : Verdict::Open;
    return v;
}

Verdict detect(const ProbeContext &ctx, const QVector<const Probe *> &probes) {
    QVector<Candidate> all;
    for (const Probe *p : probes) {
        if (!p) continue;
        for (Candidate c : p->probe(ctx)) {
            c.probe = p->id();
            all.append(c);
        }
    }
    return decide(all);
}

} // namespace fs
} // namespace dw
