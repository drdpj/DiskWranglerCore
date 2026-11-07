# Building and releasing

This file is part of [DiskWranglerCore](https://github.com/drdpj/DiskWranglerCore) and covers both
[Acorn Disk Wrangler](https://github.com/drdpj/AcornDiskWrangler) and
[CP/M Disk Wrangler](https://github.com/drdpj/CPMDiskWrangler). Below, `<app>` is `acorndiskwrangler` or
`cpmdiskwrangler`.

## What it needs
The programs share DiskWranglerCore, which each has as a git submodule in its `core/` folder. Clone with it:

    git clone --recurse-submodules https://github.com/drdpj/AcornDiskWrangler.git

or, in a clone that has an empty `core/`, `git submodule update --init`. (After a `git pull` that moves the
submodule to a new version, run `git submodule update --init` again.) GitHub's *Download ZIP* and the
"Source code" archives on a release leave `core/` empty (GitHub leaves submodules out of archives), so clone
instead. qmake and CMake stop with a message when `core/` is empty.

Qt 6 (or Qt 5.15) with its widgets module, and a C++17 compiler. qmake builds with whichever Qt its `qmake`
belongs to; CMake uses Qt 6 if it finds it, and `-DUSE_QT5=ON` makes it use Qt 5.

    qmake <app>.pro && make                         # or: cmake -S . -B build && cmake --build build

The version shown in Help > About is `git describe --tags --always --dirty`, taken when qmake or CMake runs (re-run
it after a new commit or tag). Without git history, as in a source zip, it is "unknown" unless you give it:
`qmake APP_VERSION=v1.0.0 ...` or `cmake -DAPP_VERSION=v1.0.0 ...`.

## Linux
Ubuntu/Debian: `sudo apt install qt6-base-dev qmake6 build-essential` (Qt 5: `qtbase5-dev`), then build with
`qmake6` (or `qmake` for Qt 5).

Linux desktops take an application's name and icon from a `.desktop` file, not from the running program, so a
program run straight from the build folder shows a generic icon in the dock. `make install` puts the program, its
`.desktop` file and its icons where the desktop looks:

    qmake6 PREFIX=$HOME/.local <app>.pro && make && make install      # just for you (~/.local/bin on PATH)
    qmake6 <app>.pro && make && sudo make install                     # everyone (/usr/local)

CMake: `cmake -S . -B build -DCMAKE_INSTALL_PREFIX=$HOME/.local && cmake --build build && cmake --install build`.
If the dock still shows a generic icon, log out and in again.

## Windows (MSYS2)
1. Install [MSYS2](https://www.msys2.org/), open the **MSYS2 UCRT64** shell, and install the compiler and Qt 6:

       pacman -Syu
       pacman -S --needed mingw-w64-ucrt-x86_64-toolchain mingw-w64-ucrt-x86_64-qt6-base git

   For Qt 5, install `mingw-w64-ucrt-x86_64-qt5-base mingw-w64-ucrt-x86_64-qt5-tools` instead and put `QT=5` in
   front of each `core/ci/build.sh` below. (The older MINGW64 shell works too, with `mingw-w64-x86_64-` packages.)

2. From the source folder:

       bash core/ci/build.sh windows                      # build, and copy Qt's DLLs and plugins into deploy/
       bash core/ci/collect-licences.sh windows deploy    # gather the licences of everything in deploy/
       bash core/ci/build.sh windows                      # build again, with those licences compiled in
       cp -r LICENSE THIRD-PARTY-NOTICES.md licences deploy/

   `deploy/` then runs on any 64-bit Windows 10 or later; zip it to share it. For a quick build to run from the
   UCRT64 shell, the first step's `build/<app>.exe` is enough. qmake's warning about a missing `_resource_res.o`
   is harmless.

   `core/ci/build.sh` reuses `build/` (the release workflow runs it twice). After switching Qt version or toolchain
   (Qt 5 and 6, MINGW64 and UCRT64), delete `build/` and `deploy/` first: objects left by the other one fail to
   link with `undefined reference to __imp__...` errors.

For 32-bit Windows (Windows 7 SP1 and later), see [building-windows-32bit.md](building-windows-32bit.md): Qt 5.15
and MinGW 8.1 from Qt's own downloads, in a Command Prompt.

## macOS
With Qt's official Qt 6 (the [online installer](https://www.qt.io/download-qt-installer-oss) or
[aqtinstall](https://github.com/miurahr/aqtinstall)) on your PATH:

    QT=6 bash core/ci/build.sh macos                            # build/<app>.app, built for Intel and Apple Silicon
    bash core/ci/collect-licences.sh macos build/<app>.app
    QT=6 bash core/ci/build.sh macos

For Qt 5, use Homebrew's `qt@5` (Apple Silicon only, and Homebrew drops it in May 2027) and `QT=5`.

## Release builds
`.github/workflows/build.yml` builds Windows, macOS and Linux packages on GitHub:

| Platform | Built with | Package |
|---|---|---|
| Windows 10+, x64 | MSYS2 UCRT64, Qt 6 | `.zip` of the program with its DLLs and plugins; for a release, also `...-setup.exe`, an Inno Setup installer of the same files (`core/ci/installer.iss`) |
| macOS 12+, Intel and Apple Silicon | Qt's official Qt 6.8 LTS (universal) | `.dmg`, signed ad hoc only (right-click > Open the first time) |
| Linux x86_64 | Ubuntu 22.04, Qt 6, linuxdeploy | AppImage (carries its own Qt) |
| Ubuntu 22.04+ / Debian 12+, amd64 | the same build, `core/ci/make-deb.sh` | `.deb` using the system's Qt (about 300 KB) |

* **Every push to `main` and every pull request** builds all three; the packages are kept for 90 days as the
  run's artifacts (at the bottom of the run's summary page).
* **Pushing a tag starting with `v`** also publishes them as a GitHub release. Its notes are
  `release-notes/<tag>.md` when the repository has that file (commit it before tagging), otherwise GitHub lists the
  pull requests merged since the last release. A tag with a suffix (`v1.1.0-beta.1`, `v1.1.0-rc.1`) makes a
  pre-release.

      git checkout main && git pull
      git tag v1.0.0
      git push origin v1.0.0

  To undo a tag before anyone has the release: `git tag -d <tag>`, `git push origin :refs/tags/<tag>`, and delete
  the release on GitHub.
* **The Windows installer** is built only for a tag and for Run workflow (so it can be tried without a release),
  and the workflow checks that it installs, starts, sees itself when run again, and uninstalls. It installs into Program Files for all users by
  default (Windows asks for an administrator), or only for the current user without one. Setup first says what
  is already installed: an upgrade, a downgrade (allowed, but No is the default button), the same version again,
  or a copy installed the other way (all users / only for me) that the new one will sit beside. It isn't signed, so Windows SmartScreen warns about it ("More info" > "Run
  anyway"). Inno Setup comes from Chocolatey (or the runner image's, when that is current). Each program has its own fixed AppId in `core/ci/installer.iss`: never
  change it, or Windows treats the next version as a different program.
* **Qt 5 builds**: Actions > Build > Run workflow, Qt major version 5. Their packages are named `...-qt5`; the
  macOS one comes from Homebrew's `qt@5`, for Apple Silicon only.

## Licences in the builds
Every build links to Qt's **shared** libraries, as the LGPL expects (people can swap in their own), and carries
the licence of everything it ships twice: compiled into the program (Help > Licences) and as files beside it
(`LICENSE`, `THIRD-PARTY-NOTICES.md`, `licences/`).

To know what ships, each platform is built twice. The first build is deployed (`windeployqt`, `macdeployqt`,
`linuxdeploy`); `core/ci/collect-licences.sh` then traces every library in it to the package it came from - MSYS2 on
Windows, Ubuntu for Linux, Homebrew for a Qt 5 Mac build - and copies that package's licence texts into
`licences/bundled/` with an index, writing `licences-bundled.qrc`. The second build compiles them in. If a library
can't be traced to a licence, the collector fails, so a release can't go out without one.

Qt's official Mac build has no package manager behind it, and it contains its own copies of libraries such as
libpng, HarfBuzz and PCRE2. Its licences come from Qt's source at the same version: Qt's LGPL and GPL texts, and
every third-party component Qt records in a `qt_attribution.json` (`core/ci/qt-attributions.py`).

## Shared files
`ci/`, `docs/building.md`, `docs/building-windows-32bit.md`, `docs/retro-files-format.md`, the settings helper, the
button bar, the Finder-open filter and the licences viewer (`diskwrangler/`) are in DiskWranglerCore; change them
there and move each program's `core/` to the new version tag. These are still identical copies in both
repositories, so change them in both: `.github/workflows/build.yml`, `src/licences.qrc`, `licences/`,
`version.pri` and `version.cmake`.
