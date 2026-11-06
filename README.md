# DiskWranglerCore

The code shared by the Disk Wrangler programs, [Acorn Disk Wrangler](https://github.com/drdpj/AcornDiskWrangler)
and [CP/M Disk Wrangler](https://github.com/drdpj/CPMDiskWrangler): one copy instead of two that have to be kept
in step by hand. Qt 6, with Qt 5.15 still supported. GPL v3 or later, like the programs.

Version 0.1 holds what both programs already had in identical copies: the settings helper, the button bar, the
macOS "open from the Finder" filter, the Help > Licences viewer, the CI scripts, the shared documents and the
settings test. The disc-image, filesystem and transfer layers follow in later versions; the plan is in
[docs/design.md](docs/design.md) once that is added.

## Using it
Add it to a program as a git submodule named `core`, pinned to a version tag:

    git submodule add https://github.com/drdpj/DiskWranglerCore.git core
    git -C core checkout v0.1.0
    git add core .gitmodules

Clone such a program with `git clone --recurse-submodules`, or run `git submodule update --init` in a clone you
already have.

qmake (Qt 5 or 6) compiles the core's sources into the program; in its `.pro` file:

    !include(core/diskwranglercore.pri): error("core/ is empty: run 'git submodule update --init'")

CMake (Qt 6 or Qt 5) links a static library:

    add_subdirectory(core)
    target_link_libraries(myapp DiskWrangler::Core)

Headers are included as `<diskwrangler/ui/licences.h>`; everything is in `diskwrangler/<module>/`.

## Layout
| Folder | Holds |
|---|---|
| `diskwrangler/app/` | settings (`appsettings.h`), files opened from the Finder (`fileopen.h`) |
| `diskwrangler/ui/` | the button bar, the licences viewer |
| `ci/` | the build, licence-collecting, `.deb`, Windows installer and complete-source-archive scripts the programs' workflows run as `core/ci/...` |
| `docs/` | building and releasing; the retro-files drag format |
| `tests/` | `settingstest` (`cmake -S . -B build && cmake --build build && ctest --test-dir build`, or qmake `tests/settingstest.pro`) |

## Versions
Semantic-version tags. `v0.x` until the filesystem interface settles; a program pins one tag through its submodule,
and a major-version change means an interface break.
