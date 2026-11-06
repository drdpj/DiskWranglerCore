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
#pragma once

// The registry of file viewers. A viewer turns a file's bytes into text; the viewer dialog (ui/viewerdialog.h)
// offers every viewer in the program's registry and opens on the one that suits the file best. The core ships Plain
// text and Hex dump (addDefaultViewers); a program adds its own (Acorn: BBC BASIC; CP/M: its text view, which
// replaces Plain). QtCore only.

#include <QByteArray>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <memory>

namespace dw {

// A check box the viewer wants beside the view menu while it is selected.
struct ViewOption {
    QString id;               // the key in the options handed to render()
    QString label, tip;
    bool checked = false;     // its state when the dialog opens
};

struct ViewResult {
    QString text;
    QString note;             // shown under the text: "BBC BASIC (II/IV) program, detokenised"
};

class FileViewer {
public:
    virtual ~FileViewer() = default;
    virtual QString id() const = 0;                 // "plain", "hex", "basic"
    virtual QString name() const = 0;               // what the view menu shows: "Text", "Hex dump", "BBC BASIC listing"
    virtual int order() const { return 100; }       // position in the menu: lower first (Plain 0, Hex 10, the programs' own after)
    // How well the viewer suits the bytes: 0 = not at all, higher = better. The dialog opens on the highest score
    // (the earliest in the menu on a tie). ctx carries what the program knows about the file (later: its
    // filesystem), and is the same map render() gets as part of its options.
    virtual int accepts(const QByteArray &data, const QVariantMap &ctx) const = 0;
    // Check boxes for this view, with their state for these bytes (CP/M: "Ignore bit 7" starts on for WordStar text).
    virtual QVector<ViewOption> options(const QByteArray &data) const { Q_UNUSED(data); return {}; }
    // Does it need the whole file? Otherwise the dialog hands it the first 1 MiB and says so.
    virtual bool wantsAllData() const { return false; }
    // Suffix of the file offered by "Save as text...": ".txt", or ".hex.txt".
    virtual QString saveSuffix() const { return ".txt"; }
    // opts holds the ctx map and one bool per option id.
    virtual ViewResult render(const QByteArray &data, const QVariantMap &opts) const = 0;
};

class ViewerRegistry {
public:
    // Adds a viewer; one with the same id is replaced.
    void add(std::shared_ptr<FileViewer> v);
    void remove(const QString &id);
    // In menu order.
    const QVector<std::shared_ptr<FileViewer>> &viewers() const { return m_viewers; }
    // The viewer to open on: highest accepts(), earliest in the menu on a tie; null if there are none.
    std::shared_ptr<FileViewer> best(const QByteArray &data, const QVariantMap &ctx) const;
    std::shared_ptr<FileViewer> find(const QString &id) const;
private:
    QVector<std::shared_ptr<FileViewer>> m_viewers;
};

// Plain text (id "plain", "Text") and Hex dump (id "hex").
void addDefaultViewers(ViewerRegistry &r);

} // namespace dw
