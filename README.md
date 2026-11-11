# DiskWranglerCore

The code shared by the Disk Wrangler programs, [Acorn Disk Wrangler](https://github.com/drdpj/AcornDiskWrangler)
and [CP/M Disk Wrangler](https://github.com/drdpj/CPMDiskWrangler): one copy instead of two that have to be kept
in step by hand. Qt 6, with Qt 5.15 still supported. GPL v3 or later, like the programs.

Version 0.1 held what both programs already had in identical copies: the settings helper, the button bar, the
macOS "open from the Finder" filter, the Help > Licences viewer, the CI scripts, the shared documents and the
settings test. Version 0.2 adds the small UI helpers, the transfer item and the retro-files reader and writer, and
the file-viewer registry with its dialog. Version 0.3 adds the device layer: the disc image containers (raw,
ImageDisk, DSK, extended DSK, TeleDisk) with staged writes, the `Linear` view of a disc in its own sector numbering
(side orders, slices), and the checked save every program uses. Version 0.4 adds the filesystem layer: the `Volume`
interface (files by path, attributes described by `AttrSpec`), `Disc` (one image, several mounted volumes,
transactions), detection by tiers with the hand-over of a disc to the program it belongs to, and the first two
optional filesystem modules, DFS (with `.inf` files) and FAT. Version 0.5 adds the command-line layer that the
console programs `acorndw` and `cpmdw` are built on (one invocation, one checked save; `--json`, `--dry-run`, a
`script` command; see [docs/cli.md](docs/cli.md)), makes the transfer items and retro-files a module of their own
that needs only QtCore, adds a clock that can be fixed for repeatable output (`SOURCE_DATE_EPOCH`), and gives
`Volume` path parsing and display in each filesystem's own syntax. The design, and how each version changed it, is
in [docs/design.md](docs/design.md).

## Using it
Add it to a program as a git submodule named `core`, pinned to a version tag:

    git submodule add https://github.com/drdpj/DiskWranglerCore.git core
    git -C core checkout v0.5.1
    git add core .gitmodules

Clone such a program with `git clone --recurse-submodules`, or run `git submodule update --init` in a clone you
already have.

qmake (Qt 5 or 6) compiles the core's sources into the program; in its `.pro` file:

    !include(core/diskwranglercore.pri): error("core/ is empty: run 'git submodule update --init'")

CMake (Qt 6 or Qt 5) links a static library:

    add_subdirectory(core)
    target_link_libraries(myapp DiskWrangler::Core)

Headers are included as `<diskwrangler/ui/licences.h>`; everything is in `diskwrangler/<module>/`. A test or tool
that needs only the device layer (QtCore) can include `core/diskwrangler-device.pri`, or link `DiskWrangler::Device`;
the filesystem layer alone is `core/diskwrangler-fs.pri` / `DiskWrangler::Fs`; the transfer items and retro-files
are `core/diskwrangler-xfer.pri` / `DiskWrangler::Xfer`. The filesystem modules are optional:
a program takes the ones it registers, `core/diskwrangler-dfs.pri` / `DiskWrangler::Dfs` and
`core/diskwrangler-fat.pri` / `DiskWrangler::Fat` (each `.pri` brings what it stands on, and adds it only once).

A program's console program (QtCore only, no display needed) has its own `.pro` beside the program's, named to end
in `dw.pro` (`acorndw.pro`, `cpmdw.pro`), with `QT = core` and `CONFIG += console`, including
`core/diskwrangler-cli.pri` and the filesystem modules it registers; in CMake it links `DiskWrangler::Cli` and
`Qt::Core` only. It supplies a `dw::cli::Backend` (how an image is opened, how a blank one is made, how host files
become items and back) and its `main` calls `dw::runCli`; [docs/cli.md](docs/cli.md) describes the commands and how
to write a `Backend`. The CI scripts find the `*dw.pro`, build it and ship it with the program
([docs/building.md](docs/building.md)).

## Layout
| Folder | Holds |
|---|---|
| `diskwrangler/app/` | who a program is (`appinfo.h`: `AppInfo`), the image a window shows (`document.h`: `Document`), settings (`appsettings.h`), files opened from the Finder (`fileopen.h`), the clock everything that writes a date to a disc asks (`clock.h`: `dw::now`, fixed by `SOURCE_DATE_EPOCH` in the console programs) |
| `diskwrangler/ui/` | the window kit (`discwindow.h`: `DiscWindow`; `volumepane.h`: `VolumePane`, `PaneLayout`; `importsession.h`: `ImportSession`, the windows' imports on the `Importer`), the button bar, the licences viewer, `treecolumns.h` (`setupTreeColumns`), `uihelpers.h` (`fmtBytes`, `anyCase`, `deleteKeys`, `rootLabel`), the viewer dialog, the drag-out MIME helpers |
| `diskwrangler/xfer/` | a module of its own: `TransferItem`, the retro-files reader and writer with its `HostPolicy`, `hostSafeName`, the private MIME types, the `Importer` that the command line's `put` and `copy` and the windows' imports share (QtCore only) |
| `diskwrangler/text/` | the file-viewer registry with the Plain and Hex viewers, and the text helpers (QtCore only) |
| `diskwrangler/device/` | `Container` (the image file: raw, ImageDisk, DSK / extended DSK, TeleDisk; physical and stored-bytes views; staging; `commit` and `saveAs`), `Linear` (a disc as one run of bytes, through a `SideOrder`), `SafeSave` (temporary copy, check, `.bak`, atomic replace) (QtCore only) |
| `diskwrangler/fs/` | `Volume`, `Path`, `Entry`, `AttrSpec`, `NameRules` (`volume.h`), `Disc` with its mounts, transactions and checked save (`disc.h`), detection by tiers: `Probe`, `Candidate`, `decide` (`detection.h`) (QtCore only) |
| `diskwrangler/fs/dfs/`, `fs/fat/` | the optional modules: Acorn DFS (Watford 62-file catalogues, `.inf` lines, addresses, directory letters copied to ADFS, Acorn Z80 CP/M's catalogue) and FAT12/16, each with its probe and its `Volume` adapter (QtCore only) |
| `diskwrangler/cli/` | the command-line layer: `Backend`, `OpenedImage` and `runCli` (`cli.h`), the argument and script parser, the JSON output and exit codes, and the commands (QtCore only) |
| `diskwrangler/app/handoff.h`, `ui/handoffdialog.h` | opening an image another program of the family owns: finding and starting it, the messages, and the question when two readings are equally likely |
| `ci/` | the build, licence-collecting, `.deb`, Windows installer and complete-source-archive scripts the programs' workflows run as `core/ci/...`; they also build and ship each program's console program (`AppRun.in` is the AppImage's start script for it) |
| `docs/` | the design and its amendments; building and releasing; the command-line programs (`cli.md`); the retro-files drag format |
| `tests/` | `settingstest`, `xfertest` (names, retro-files round trips, policy hooks), `uitest` (helpers, viewers, the dialog, the window kit and its import session on `tests/memvolume.h`), `devicetest` (containers, staging, `Linear`, the checked save), `volumetest` (`Volume`, `Disc` transactions, detection by tiers, on a toy filesystem) `moduletest` (the DFS and FAT modules, with their path syntax) and `clitest` (argument and script parsing, paths, the `Importer`, the JSON encodings): `cmake -S . -B build && cmake --build build && ctest --test-dir build`, or the qmake `tests/*.pro` |

## Versions
Semantic-version tags. `v0.x` until the filesystem interface settles; a program pins one tag through its submodule,
and a major-version change means an interface break.
