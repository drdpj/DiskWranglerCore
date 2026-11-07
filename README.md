# DiskWranglerCore

The code shared by the Disk Wrangler programs, [Acorn Disk Wrangler](https://github.com/drdpj/AcornDiskWrangler)
and [CP/M Disk Wrangler](https://github.com/drdpj/CPMDiskWrangler): one copy instead of two that have to be kept
in step by hand. Qt 6, with Qt 5.15 still supported. GPL v3 or later, like the programs.

Version 0.1 held what both programs already had in identical copies: the settings helper, the button bar, the
macOS "open from the Finder" filter, the Help > Licences viewer, the CI scripts, the shared documents and the
settings test. Version 0.2 adds the small UI helpers, the transfer item and the retro-files reader and writer, and
the file-viewer registry with its dialog. Version 0.3 adds the device layer: the disc image containers (raw,
ImageDisk, DSK, extended DSK, TeleDisk) with staged writes, the `Linear` view of a disc in its own sector numbering
(side orders, slices), and the checked save every program uses. The filesystem layer follows in later versions;
the plan is in [docs/design.md](docs/design.md).

## Using it
Add it to a program as a git submodule named `core`, pinned to a version tag:

    git submodule add https://github.com/drdpj/DiskWranglerCore.git core
    git -C core checkout v0.3.0
    git add core .gitmodules

Clone such a program with `git clone --recurse-submodules`, or run `git submodule update --init` in a clone you
already have.

qmake (Qt 5 or 6) compiles the core's sources into the program; in its `.pro` file:

    !include(core/diskwranglercore.pri): error("core/ is empty: run 'git submodule update --init'")

CMake (Qt 6 or Qt 5) links a static library:

    add_subdirectory(core)
    target_link_libraries(myapp DiskWrangler::Core)

Headers are included as `<diskwrangler/ui/licences.h>`; everything is in `diskwrangler/<module>/`. A test or tool
that needs only the device layer (QtCore) can include `core/diskwrangler-device.pri`, or link `DiskWrangler::Device`.

## Layout
| Folder | Holds |
|---|---|
| `diskwrangler/app/` | settings (`appsettings.h`), files opened from the Finder (`fileopen.h`) |
| `diskwrangler/ui/` | the button bar, the licences viewer, `treecolumns.h` (`setupTreeColumns`), `uihelpers.h` (`fmtBytes`, `anyCase`, `deleteKeys`, `rootLabel`), the viewer dialog, the drag-out MIME helpers |
| `diskwrangler/xfer/` | `TransferItem`, the retro-files reader and writer with its `HostPolicy`, `hostSafeName`, the private MIME types (QtCore only) |
| `diskwrangler/text/` | the file-viewer registry with the Plain and Hex viewers, and the text helpers (QtCore only) |
| `diskwrangler/device/` | `Container` (the image file: raw, ImageDisk, DSK / extended DSK, TeleDisk; physical and stored-bytes views; staging; `commit` and `saveAs`), `Linear` (a disc as one run of bytes, through a `SideOrder`), `SafeSave` (temporary copy, check, `.bak`, atomic replace) (QtCore only) |
| `ci/` | the build, licence-collecting, `.deb`, Windows installer and complete-source-archive scripts the programs' workflows run as `core/ci/...` |
| `docs/` | building and releasing; the retro-files drag format |
| `tests/` | `settingstest`, `xfertest` (names, retro-files round trips, policy hooks), `uitest` (helpers, viewers, the dialog) and `devicetest` (containers, staging, `Linear`, the checked save): `cmake -S . -B build && cmake --build build && ctest --test-dir build`, or the qmake `tests/*.pro` |

## Versions
Semantic-version tags. `v0.x` until the filesystem interface settles; a program pins one tag through its submodule,
and a major-version change means an interface break.
