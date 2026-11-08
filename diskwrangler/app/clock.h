/*
 * The clock - part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler
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

// The program's clock (docs/design.md, "Amendments from step 4b"). Everything that stamps a time on a disc - a FAT
// directory entry, an ImageDisk header, a CP/M 3 time stamp - asks dw::now(), never QDateTime::currentDateTime(),
// so that a command-line run can be made repeatable: dw::cli::runCli sets the override from SOURCE_DATE_EPOCH. The
// graphical programs never set it. QtCore only.

#include <QDateTime>

namespace dw {

QDateTime now();                         // the override if one is set, else QDateTime::currentDateTime()
void setNow(const QDateTime &t);         // an invalid time clears the override

} // namespace dw
