/*
 * Tree column layout - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// Every pane is laid out by setupTreeColumns() so they look the same, in both programs: the Name column takes the
// spare width (so the last column always meets the right-hand edge), every other column is sized from the font
// (never fixed pixels - those break on scaled displays), and numbers are right-aligned, header included, with the
// same trailing pad (setNumCell) so they never touch the next column. Give a column that appears in several panes
// the same sample so it lines up across them. Header only.

#include <QFontMetrics>
#include <QHeaderView>
#include <QStringList>
#include <QStyle>
#include <QTreeWidget>
#include <initializer_list>

namespace dw {

struct ColSpec {
    const char *label;
    const char *sample;      // widest text the column normally shows (ignored for the Name column)
    bool numeric;
};

inline constexpr const char *NumPad = "  ";

inline void setupTreeColumns(QTreeWidget *t, std::initializer_list<ColSpec> cols) {
    const QFontMetrics fm(t->font());
    const int margin = 2 * t->style()->pixelMetric(QStyle::PM_HeaderMargin, nullptr, t) + fm.averageCharWidth();
    QStringList labels;
    for (const ColSpec &c : cols) labels << (c.numeric ? QString(c.label) + NumPad : QString(c.label));
    t->setColumnCount(labels.size());
    t->setHeaderLabels(labels);
    QHeaderView *h = t->header();
    h->setStretchLastSection(false);
    h->setSectionResizeMode(QHeaderView::Interactive);
    h->setSectionResizeMode(0, QHeaderView::Stretch);
    int fixed = 0, i = 0;
    for (const ColSpec &c : cols) {
        if (i > 0) {
            QString pad = c.numeric ? NumPad : "";
            int w = qMax(fm.horizontalAdvance(c.sample + pad), fm.horizontalAdvance(labels[i])) + margin;
            t->setColumnWidth(i, w);
            fixed += w;
            if (c.numeric) t->headerItem()->setTextAlignment(i, Qt::AlignRight | Qt::AlignVCenter);
        }
        ++i;
    }
    // Keep a readable Name column: the window can't be made so narrow that the columns squash it.
    int sb = t->style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, t);
    t->setMinimumWidth(fixed + fm.averageCharWidth() * 22 + sb + 2 * t->frameWidth());
    t->setSelectionMode(QAbstractItemView::ExtendedSelection);
    t->setUniformRowHeights(true);
    t->setContextMenuPolicy(Qt::CustomContextMenu);
}

inline void setNumCell(QTreeWidgetItem *it, int col, const QString &text) {
    it->setText(col, text + NumPad);
    it->setTextAlignment(col, Qt::AlignRight | Qt::AlignVCenter);
}

} // namespace dw
