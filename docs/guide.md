# Building a program on DiskWranglerCore

This is the guide for writing a Disk Wrangler: a program that opens disc images of some old machine, shows their
files, and copies files in and out. Acorn Disk Wrangler and CP/M Disk Wrangler are built this way, and they are the
worked examples throughout. The reference for each class is its header (every header starts with a comment saying
what it is for); [design.md](design.md) says why things are as they are, and [cli.md](cli.md) describes the console
programs' commands. This guide says how the pieces fit and which ones a new program writes.

## The shape of a program

A program on the core has two front ends over one set of filesystem code:

    the window  (QtWidgets)          the console program  (QtCore only)
    MainWindow : dw::ui::DiscWindow   main() -> dw::runCli(argc, argv, info)
            \                                 /
             the program's Document / OpenedImage   <- one class: opens a file, saves it
                          |
                   dw::fs::Disc  (one container, its mounted volumes, transactions)
                     /        \
         dw::fs::Volume  ...  dw::fs::Volume       <- one adapter per filesystem
               |                    |
       the program's filesystem classes, or a core module (DFS, FAT)
                          |
          dw::device::Container (+ Linear / Physical)   <- the image file, staged writes, checked save

Everything above the filesystem classes is the core's, apart from the hooks a program fills. What the program
writes is what makes it that program: its filesystems, how it recognises its discs, how it makes new ones, the
host-file rules of its machine (Acorn's `.inf` files, CP/M's `^Z` padding), its dialogs, its icon and its texts.

| Layer | Folder | Qt | What it gives |
| --- | --- | --- | --- |
| Device | `diskwrangler/device/` | Core | `Container` (raw, ImageDisk, DSK, extended DSK, TeleDisk read-only), the physical view (cylinder / head / sector, by index or by sector ID), `Linear` (the disc as one run of bytes in its own numbering, through a `SideOrder`, with `slice`), staging, and the checked save (`Container::commit`, `SaveOptions`) |
| Filesystem | `diskwrangler/fs/` | Core | `Volume`, `Path`, `Entry`, `AttrSpec`, `NameRules` (`volume.h`); `Disc` (`disc.h`); detection by tiers (`detection.h`); new-disc presets (`presets.h`) |
| Modules | `diskwrangler/fs/dfs/`, `fs/fat/` | Core | Acorn DFS (with Watford 62-file discs, `.inf` lines, DFS letters copied to directories) and FAT12/16, each with its classes, its `Probe`, its `Volume` adapter and its presets. Take them if your machine's discs can hold them |
| Transfer | `diskwrangler/xfer/` | Core | `TransferItem` (a file in flight), the retro-files format and `HostPolicy` (writing items as host files), `readHost` (reading host files and folders), the `Importer` (putting items onto a volume: conflicts, placement, directories) |
| Text | `diskwrangler/text/` | Core | the file-viewer registry with Plain and Hex viewers |
| App | `diskwrangler/app/` | Core | `AppInfo` (who the program is), `Document` (the image a window shows), `AppSettings`, the clock `dw::now()`, files opened from the macOS Finder (`fileopen.h`), handing a disc to another program of the family (`handoff.h`) |
| Command line | `diskwrangler/cli/` | Core | `runCli`, `Backend`, `OpenedImage`: every command, option, exit code and the JSON output |
| UI | `diskwrangler/ui/` | Widgets | the window kit (`DiscWindow`, `VolumePane`, `ImportSession`), the viewer dialog, drag-out data (`DragOutMime`), the hand-over messages, the button bar, the licences viewer, small helpers |

The QtCore-only layers are what keep the console program free of QtGui and QtWidgets; the programs' CI checks that
with `ldd`. Never include a `ui/` header from a class the console program uses.

## Adding the core to a program

The core is a git submodule at `core/`, pinned to a tag:

    git submodule add https://github.com/drdpj/DiskWranglerCore.git core
    git -C core checkout v0.15.0
    git add core .gitmodules

qmake compiles the core's sources into the program. The window's `.pro`:

    !include(core/diskwranglercore.pri): error("core/ is empty: run 'git submodule update --init'")
    include(core/diskwrangler-dfs.pri)            # only the modules the program uses
    include(core/diskwrangler-fat.pri)

The console program has its own `.pro` beside it, named to end in `dw.pro` (the CI scripts find it by that), with
`QT = core`, `CONFIG += console`, `include(core/diskwrangler-cli.pri)` and the same modules. Each `.pri` brings in
what it stands on and is added once however often it is included.

CMake: `add_subdirectory(core)`, then link `DiskWrangler::Core` (the window), `DiskWrangler::Cli` and `Qt::Core` (the
console program), and `DiskWrangler::Dfs` / `DiskWrangler::Fat` for the modules. `DiskWrangler::Device`,
`DiskWrangler::Fs` and `DiskWrangler::Xfer` are there for tests that need only part of it.

Headers are included as `<diskwrangler/fs/volume.h>`. Everything is in namespace `dw` (`dw::device`, `dw::fs`,
`dw::xfer`, `dw::app`, `dw::ui`, `dw::cli`). One rule from qmake: **no source file of the program may have the
name of one of the core's** (qmake puts every object file in one folder, so two `detection.o` overwrite each other).

## The filesystem: classes and a Volume

### The filesystem classes

Write the filesystem as plain classes on the device layer, with no knowledge of windows or of the core's
interfaces: Acorn's `Adfs::Image`, CP/M's `Cpm::Volume` and `Torch::Fs`, the core's `dw::dfs::Disc` and
`dw::fat::Volume` are the models. They read and write through the container, never through a file:

- A filesystem that thinks in logical sectors or byte offsets works on a `dw::device::Linear` (DFS, FAT, ADFS). A
  side or partition is a `Linear::slice`. A double-sided disc's order is a `SideOrder` under the `Linear`, so the
  filesystem only ever sees the disc in its own numbering, whatever the file's track order.
- A filesystem that addresses sectors by track and sector ID works on the container's physical view: `read`,
  `write` and `indexOfId(cyl, head, sectorId)` (CP/M through its format's mapping).

Every write is **staged** in the container (in memory, per sector, or per 128-byte block of a raw file), and every
read sees the staged writes. Nothing reaches the file until the disc is saved. A raw file is read as needed, never
loaded, so a 512 MB hard-disc image costs nothing.

Two rules for these classes:

1. **No rollback of their own.** A change that fails says why (`QString *err`) and returns false where it is, even
   half way through. Undoing it is the caller's transaction's job (below). Refuse a read-only disc up front.
2. **A faithful `reload()`.** The class may cache what it decodes (a free-space map, a catalogue, a FAT, a
   directory). `reload()` reads all of it again from the staged disc, and after any change that succeeded it must
   give exactly the state the change left in memory, since that is what a failed transaction falls back to. Test it:
   the core's `moduletest` and both programs' `voltest` compare, after every kind of change, the state in memory with
   a reload of the same staging, and the bytes the next write makes from each.

Anything that writes a date to the disc asks `dw::now()` (`app/clock.h`), never `QDateTime::currentDateTime()`: the
console program fixes the clock from `SOURCE_DATE_EPOCH`, which is what makes its golden tests repeatable.

### The Volume adapter

A `dw::fs::Volume` (`fs/volume.h`) is one filesystem on one disc, seen the same way whatever it is: files by `Path`
(the names from the root down), attributes described by `AttrSpec`s, changes by path. The window kit, the importer
and every command of the console program use only this interface, so a program never writes a file list, an
extract or a `put` of its own. The adapter is usually thin (`dw::dfs::Volume`, `dw::fat::FsVolume`, Acorn's
`AdfsVolume`, CP/M's `CpmVolume`): resolve the path to what the class takes, call it.

What to fill in:

- `fsId()` (a short lower-case word, also the retro-files source filesystem), `caps()`, `names()` (a `NameRules`:
  what is a valid name, and a valid name made from a host name), `info()`.
- `fileAttrs()` and `volumeAttrs()`: each attribute's key, label, type, and its `retroFamily` / `retroKey` if it
  travels with a file. The columns, the Properties dialogs a program builds, `ls` and `attr`, and the retro-files
  mapping (`toRetro` / `fromRetro`) all come from them. Override the mapping only when your attributes don't map one
  to one (Acorn's access letters).
- `list`, `read`, `write`, `remove`, and as the filesystem allows `mkdir`, `rename`, `setAttrs`, `setVolumeAttrs`.
- `check()`: errors make the volume read-only and veto a save; warnings are shown.
- `reload()`: forward to the class's.
- Where the filesystem has its own path syntax, `parsePath` / `displayPath` (DFS: `A.PROG`). Where an incoming item
  needs placing by rules of its own, `placeIncoming` (ADFS puts DFS letters in subdirectories). Levels that aren't
  directories (CP/M user areas) are `NodeKind::Namespace`.

A new machine's attributes need a family in the retro-files format ([retro-files-format.md](retro-files-format.md)):
add it there first, so that other programs know what your files carry.

### The Disc and transactions

A `dw::fs::Disc` (`fs/disc.h`) is one opened image: a container and the volumes mounted on it, each a
`Disc::Mount` with a label, a read-only flag and reason, a `group` (mounts in one group share a pane; groups are
tabs) and, if it holds only part of the disc, a `touched` test. Acorn's hybrid disc is two mounts, ADFS and DOS;
a DFS disc is drive 0 and drive 2; a CP/M disc is one, or one per side.

**Every change is made inside `Disc::transact`.** It takes the container's snapshot, runs the change, and if the
change fails puts the snapshot back and has every mount `reload()`. That is the only rollback there is: the
filesystem classes keep no snapshot of their own. The window kit, the import session (one transaction per file and
per folder made) and the programs' own changes all go through it; a program that changes a volume from its own
code (Acorn's New directory, CP/M's Rename) does the same:

    QString err;
    bool ok = document()->disc().transact([&](QString *e) { return vol->mkdir(dir, name, e); }, &err);

The console program needs none: its first error abandons the run, unsaved.

`Disc::validateAll()` runs every mount's `check()`; `validateTouched()` only those the staged writes touch (a
window lets only a changed volume veto a save); `commit()` validates, then saves; `revert()` drops the staged
writes.

### Saving

`Container::commit(SaveOptions)` never writes over the file in place. It validates, writes a complete new file
beside the original, opens that again and reads every staged write back (and `verify` lets the program check it
with its own filesystem code), makes a `.bak` if asked, and only then replaces the original. The original survives
any failure before the last step. Use it through the program's document class (below), which adds the program's
own checks (Acorn: the ADFS map and the DOS and DFS volumes of the written copy; CP/M: each side's directory read
back).

## Recognising discs: probes

Detection is by tiers (`fs/detection.h`). The program gives one `Probe` per filesystem it knows, plus one for each
kind of disc it recognises but belongs to another program. A probe looks at the opened container (`ProbeContext`:
the path, the suffix, the container, the stored bytes) and returns `Candidate`s, each with a `Tier`: `Certain`,
`Likely` or `Possible`. Scores only order one probe's own candidates; probes are compared only by tier, so a new
filesystem never has to agree a scoring scale with the others. `decide()` then opens the best, asks the user when
the best two from different probes are equally likely, or hands the disc to its `owner` (another program).

Put whatever the program needs to open the disc in `Candidate::data` (a `std::any`: Acorn's `DiskInfo`, CP/M's
detection result). The DFS and FAT modules bring their probes (`dw::dfs::Probe`, `dw::fat::Probe`).

## New discs: presets

Each kind of blank disc is a `dw::fs::Preset` in the program's `dw::fs::presets()` registry: id, name, filesystem,
geometry, the containers it can be written in, the title field. The modules add theirs (`dw::dfs::addPresets`,
`dw::fat::addPresets`); the program adds its own and decides what its New-disc dialog offers. Making the disc stays
the program's, by preset id (Acorn's `NewDisc::create`, CP/M's `makeBlank`); `new --list` and the dialog both read
the registry.

## The document

One class per program opens an image and saves it, used by both front ends so that they save through exactly the
same checks: Acorn's `Adfs::OpenedDisc`, CP/M's `Cpm::OpenedDisc`. It implements `dw::app::Document` for the window
(`disc()`, `isOpen()`, `path()`, `save()`, `saveAs()`, `readOnlyWhy(mount)`) and stands behind
`dw::cli::OpenedImage` for the console program. It opens the container, attaches the filesystem classes for the
candidate detection chose, mounts their adapters on its `Disc`, and decides which mounts are read-only and why.

## The window

`MainWindow` derives from `dw::ui::DiscWindow` (`ui/discwindow.h`; its header comment lists the steps). The kit
makes the panes, the rows, the selection (as mount and `Path`), the standard actions and their enabling, the title
and status, Save / Save As / Revert / Close, Delete, Extract, Properties, disc attributes, Check, the open pipeline
(detection, the "which reading?" question, hand-over with Locate...) and every import. The program:

1. Calls `setupKit()` first in its constructor, then builds its menus and button bar from `action(Act::...)` and its
   own actions, in its own order, then `refresh()`.
2. Fills the hooks it needs, all optional except the first group:
   - `document()`, `appInfo()`, `actionSpec(act)` (texts, keys, button text; an empty text leaves an action out),
     `paneLayout(group)` (columns and expansion rules);
   - rows: `decorateRow` (cells from the program's own classes), `rootLabel`, and on `PaneHost` `orderRows` and
     `listChildren`; panes: `paneGroups`, `paneShown`, `paneTitle`, `createPane` (a `VolumePane` subclass with the
     program's drag code);
   - opening: `detectImage` (the program's probes, a `Verdict`), `openDocument`, `afterOpen`, `handOverMessage`,
     `createWindow`;
   - changes: `propertiesEditor` and `volumeEditor` (the program's own dialogs, returning what to change; the kit
     applies it in a transaction), `deleteItems`, `confirmDelete`, `deleteEachOnItsOwn`, `canRemove`;
   - imports: `importHooks(mount)` (where items go and with what attributes when the program's rules aren't the
     volume's, the question for a folder on a volume without directories, ...), `askReplace`, `importSummary`;
   - Extract: `hostPolicy(items)` (how items become host files);
   - status and messages: `statusVariants`, `presentStatus`, `message`.
3. Overrides the slots the kit leaves empty: `newImageDialog`, `openDialog`, `viewSelected`, `addFilesDialog`,
   `copySelected`, `paste`, `renameSelected`, `newDirectory`, `about`.

The kit never tests a filesystem's id: anything that names a filesystem, a character set, a format or a side order
is a hook the program fills. Each program keeps its own `QSettings` names (`AppSettings` in place of `QSettings`).

Every Add files, drop, Paste and drop from another window ends in `DiscWindow::importItems(mount, dir, items, ...)`:
read host files with `dw::xfer::readHost` (the program's `HostReadOptions`: its size limit and a per-file hook for
sidecar files), or retro-files JSON with `dw::xfer::readJson`, then hand the items over. Copying out (Extract,
drags, Copy) is `exportItems`, then `dw::xfer::writeHost` with the program's `HostPolicy`, or a `dw::DragOutMime`
built from a `DragSpec` (the files are written only when a drop target asks for them). Drags and the clipboard
carry the private MIME types of `xfer/mime.h` between the program's own windows.

The tests reach a window through the kit too (`document()`, `pane(group)`, `openFile`, `DiscWindow::all()`); set
`DiscWindow::autoAnswer` so that questions take their first answer and messages go to the status line.

## The console program

A few lines (`src/cli/main.cpp` in either program): fill a `dw::cli::AppInfo` (the tool's name, the program's
`app::AppInfo`, the extra options the tool has) with a `Backend`, and return `dw::runCli(argc, argv, info)`. The
`Backend` (`cli/cli.h`) is what differs between programs: `open` (detection, then the document as an `OpenedImage`;
`resolveVerdict` does the `--fs` choice), `create` (a new disc from a preset), `formats` (`new --list`),
`hostPolicy` and `readHost` (host files out and in). The commands, options, exit codes, `--json` envelope,
`--dry-run` and `script` are the core's ([cli.md](cli.md)), so every tool behaves alike.

## Other programs of the family

`AppInfo::handsTo` lists the programs this one hands discs to, each an `fs::OtherApp` (id, name, Windows folder,
releases page; `fs::acornDiskWrangler()` and `fs::cpmDiskWrangler()` are the existing two: add a function beside
them for a new program, so every program can name it). A probe that recognises another program's disc sets
`Candidate::owner`; the kit then finds that program (`app/handoff.h`: a stored `otherApps/<id>` setting, the same
folder, the system's records), starts it with the image, and says so, with Locate... when it can't be found.

## Testing

- The core's tests (`ctest`, or `tests/*.pro`) cover each layer. `tests/memvolume.h` is an in-memory volume and
  document for testing window-kit behaviour without a real filesystem.
- Each program has `voltest` (its discs through `Volume` and `Disc`: transactions, faithful reload, the checked
  save), a GUI test on the kit, and golden-image tests of its console program (`tests/cli/run_cli.sh`: commands run
  on fixed images with `SOURCE_DATE_EPOCH` set, outputs and image hashes compared).
- Test data is made by the tests themselves, deterministically: random bytes now and then look like a disc.

## Versions and changes to the core

Semantic-version tags; a program pins one through its submodule. `DW_CORE_VERSION` (`cli/cli.h`, printed by
`--version`) is raised with every tag, and `tests/clitest.cpp` with it. Never change the core from inside a program's
`core/` folder: change DiskWranglerCore, tag it, and move each program's submodule to the tag. Anything two programs
would write alike belongs in the core, as a hook-driven piece rather than a branch on which program it is.
