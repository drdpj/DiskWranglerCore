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
#pragma once

// Detection by tiers (docs/design.md, "Detection"). QtCore only.
//
// Each filesystem (and each recogniser of another program's discs) is a Probe. It looks at the opened container
// and says what the disc could be, as Candidates with a Tier. Scores stay inside one probe: a probe ranks its own
// candidates, and the core compares only tiers. decide() then:
//  * takes the best tier; among equals, the probes' order (the order they were given in) and each probe's ranking;
//  * asks the user when the best two are equally likely (Certain or Likely) and come from different probes;
//  * hands the disc to its owner when the best candidate belongs to another program.

#include <QString>
#include <QStringList>
#include <QVector>
#include <any>

#include <diskwrangler/device/container.h>

namespace dw {
namespace fs {

enum class Tier { Possible = 1, Likely = 2, Certain = 3 };
QString tierName(Tier t);                // "certain", "likely", "possible"

// Another program of the family: who owns a disc this one doesn't handle.
struct OtherApp {
    QString id;                          // "cpmdiskwrangler": the program file and its settings name
    QString name;                        // "CP/M Disk Wrangler"
    QString folder;                      // Windows install folder name ("CPM Disk Wrangler")
    QString releases;                    // where to get it
    bool isNull() const { return id.isEmpty(); }
};
OtherApp acornDiskWrangler();
OtherApp cpmDiskWrangler();

struct Candidate {
    QString fsId;                        // "adfs", "dfs", "fat", "cpm" ...
    QString name;                        // the filesystem in a word or two, for buttons: "DFS", "PC FAT", "CP/M"
    QString description;                 // what the disc is, in words ("DFS double-sided 80-track (200K per side)")
    Tier tier = Tier::Possible;
    double score = 0;                    // only compared within one probe
    QStringList evidence, notes;
    bool safeToWrite = true;
    bool supported = true;               // false: recognised, but this program can't open it (ADFS new map)
    OtherApp owner;                      // set: the disc belongs to another program
    QString probe;                       // the probe that found it (set by detect())
    std::any data;                       // the probe's own result (geometry, layout ...), for the program to open it with
    bool foreign() const { return !owner.isNull(); }
};

// What a probe gets to look at.
struct ProbeContext {
    QString path;
    QString suffix;                      // lower case, no dot
    device::Container *container = nullptr;   // opened; a probe reads it and never writes (it may copy it)
    int forceSequential = -1;            // a double-sided disc's side order: -1 detect, 0 interleaved, 1 sequential
    // The disc's bytes as stored (see Container::readStored), zeros past the end.
    QByteArray stored(qint64 off, int len) const;
    qint64 size() const;                 // a raw file's size; the stored size of the others
};

class Probe {
public:
    virtual ~Probe() = default;
    virtual QString id() const = 0;
    virtual QVector<Candidate> probe(const ProbeContext &ctx) const = 0;
};

struct Verdict {
    enum Action {
        None,                            // nothing recognised the disc
        Open,                            // open ranked[best] here
        Ask,                             // ranked[best] and ranked[rival] are equally likely: ask which
        Handoff                          // ranked[best] belongs to another program
    };
    Action action = None;
    QVector<Candidate> ranked;           // best first
    int best = -1, rival = -1;
    const Candidate *bestCandidate() const { return best >= 0 ? &ranked[best] : nullptr; }
    const Candidate *rivalCandidate() const { return rival >= 0 ? &ranked[rival] : nullptr; }
};

// Ranks candidates (each carrying its probe, in probe order) and decides what to do.
Verdict decide(const QVector<Candidate> &candidates);
// Runs the probes in order, then decide().
Verdict detect(const ProbeContext &ctx, const QVector<const Probe *> &probes);

} // namespace fs
} // namespace dw
