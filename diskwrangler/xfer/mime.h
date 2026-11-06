/*
 * Private drag types - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// The private MIME types a Disk Wrangler window puts on its drags and on Edit > Copy, the same in every program of
// the family. They only ever mean "an item from a window of this very process" (the source tag in the second type
// names the process and window and is checked before the first is read), so windows of different programs, which
// are different processes, never take them for each other's; they use the retro-files type instead.
//   ITEMS   the dragged items, as references into the source window
//   SOURCE  "pid:window:pane" of the window they came from
//   CLIP    "pid:n", what Edit > Copy put on the clipboard
// application/x-retro-files+json (xfer/retrofiles.h) is unchanged.

#define DW_MIME_ITEMS  "application/x-diskwrangler-items"
#define DW_MIME_SOURCE "application/x-diskwrangler-source"
#define DW_MIME_CLIP   "application/x-diskwrangler-clip"
