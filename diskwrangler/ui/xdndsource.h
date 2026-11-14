/*
 * Serving a drag's data on X11 - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// Dragging files out on Linux when Qt runs on X11 (xcb), as it does on an X11 desktop and, under XWayland, on a GNOME
// Wayland session (dw::ui::beforeApplication): series 6c-2.
//
// A drop target asks the drag's source for the data with ConvertSelection on XdndSelection; the source answers with a
// SelectionNotify for the target (the format) it was asked for. Qt's xcb drag code (qxcbdrag.cpp: the same in Qt 5.15,
// 6.4, 6.8 and the development branch) answers a format it doesn't have with target None instead of the format asked
// for, and has no answer for TARGETS at all. GTK 4 (Nautilus on X11) and mutter's XWayland bridge (which hands an X11 drag to Wayland programs:
// Nautilus, terminals) only take a SelectionNotify whose target is the one they asked for, so they wait for ever:
// GTK 4 asks first for its portal formats (application/vnd.portal.filetransfer, ...files), which nothing offers, and
// never gets to text/uri-list; mutter asks for TARGETS and never learns what the drag holds. Either way no file is
// asked for and nothing arrives. Older targets (GTK 3, Qt) ask for an offered format and were never affected.
//
// serveXdnd(data) has this process answer XdndSelection requests itself while the drag's data exist (Qt keeps them
// until the target has finished, after QDrag::exec() returns): TARGETS with the formats offered, each offered format
// with its data (text/x-moz-url as Qt writes it), anything else refused as X expects (target as asked, property None).
// Elsewhere (Wayland, Windows, macOS, offscreen) it does nothing. It needs no X11 headers or libraries to build: the
// few xcb calls are looked up at run time in the libxcb that Qt's xcb plugin has already loaded, and the replies go
// out on a connection of its own (any client may answer a selection request).

#include <QMimeData>

namespace dw {
namespace ui {

// The drag that is about to start (its data, which the QDrag owns). The previous drag's are no longer served.
void serveXdnd(QMimeData *data);

} // namespace ui
} // namespace dw
