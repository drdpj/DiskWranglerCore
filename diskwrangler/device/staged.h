/*
 * Staged changes - part of DiskWranglerCore
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
// Something whose changes are held back until it is saved: a disc image (device::Container) or a tape image
// (tape::TapeFile). What fs::Disc needs to run a transaction over it: take the staged state, put it back after a
// change that failed. Header only, QtCore only.
#include <memory>

namespace dw {
namespace device {

class Staged {
public:
    virtual ~Staged() = default;
    virtual bool hasPending() const = 0;                 // anything a save would write
    virtual void discardPending() = 0;                   // back to the file as it was opened or last saved
    // The staged changes as they are now, to hand back to restoreStaged (and only to this object).
    virtual std::shared_ptr<const void> stagedState() const = 0;
    virtual void restoreStaged(const std::shared_ptr<const void> &state) = 0;
};

} // namespace device
} // namespace dw
