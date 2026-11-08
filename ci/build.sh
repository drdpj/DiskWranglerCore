#!/usr/bin/env bash
# Part of DiskWranglerCore, used by Acorn Disk Wrangler and CP/M Disk Wrangler.
# Copyright (C) 2026 Daniel Jameson. GPL v3 or later.
#
# Builds a release and deploys it with Qt's shared libraries, ready to run on another machine:
#
#   core/ci/build.sh windows    MSYS2 UCRT64 (or MINGW64) shell      -> deploy/          (exe, DLLs, plugins)
#   core/ci/build.sh macos      Qt's qmake and macdeployqt on PATH   -> build/<app>.app  (frameworks inside)
#   core/ci/build.sh linux      Debian/Ubuntu Qt dev packages,       -> build/AppDir     (for an AppImage)
#                          linuxdeploy + its Qt plugin on PATH
#
# Qt 6 by default; QT=5 builds with Qt 5.15 instead (QT=5 core/ci/build.sh linux). Run from the program's source root
# (with the core checked out as its submodule core/). The program is whichever *.pro is there that doesn't end in
# dw.pro; that one, if there, is its console program (acorndw, cpmdw: QtCore only), built beside it in build-cli/ and
# shipped with it: beside the program's exe on Windows, in the bundle's Contents/MacOS on macOS, in usr/bin of the
# AppDir on Linux, where a custom AppRun (build/AppRun.custom) runs it when the AppImage is started under its name.
# Running it again after core/ci/collect-licences.sh rebuilds with the bundled libraries' licences compiled in (see
# .github/workflows/build.yml).
set -euo pipefail

platform=${1:?usage: [QT=5] build.sh windows|macos|linux}
qt=${QT:-6}
case "$qt" in 5|6) ;; *) echo "build.sh: QT must be 5 or 6" >&2; exit 1 ;; esac
app=$(basename "$(ls ./*.pro | grep -v 'dw\.pro$')" .pro)
cli=$(ls ./*dw.pro 2>/dev/null | head -1 | xargs -r basename -s .pro)       # empty when the program has no console program
mkdir -p build

case "$platform" in
windows)
    if [ "$qt" = 6 ]; then
        qmake=qmake6; deploy=windeployqt6
        extra=(--no-system-dxc-compiler)            # Qt 6 also looks for the Windows SDK's shader compiler
    else
        qmake="qmake-qt5"; deploy="windeployqt-qt5"
        extra=(--no-angle)
    fi
    (cd build && "$qmake" "../$app.pro" CONFIG+=release CONFIG-=debug_and_release && mingw32-make -j"$(nproc)")
    mkdir -p deploy
    cp "build/$app.exe" deploy/
    if [ -n "$cli" ]; then
        mkdir -p build-cli
        (cd build-cli && "$qmake" "../$cli.pro" CONFIG+=release CONFIG-=debug_and_release && mingw32-make -j"$(nproc)")
        cp "build-cli/$cli.exe" deploy/                  # QtCore, the one Qt library it needs, is deployed for the program already
    fi
    "$deploy" --release --compiler-runtime --no-translations --no-system-d3d-compiler --no-opengl-sw \
        "${extra[@]}" "deploy/$app.exe"
    # windeployqt leaves out what Qt itself links to (ICU, HarfBuzz, FreeType, zlib ...): copy the whole closure
    prefix=${MINGW_PREFIX:-/ucrt64}
    for _ in 1 2 3 4; do
        for f in deploy/*.exe deploy/*.dll deploy/*/*.dll; do
            ldd "$f" | awk -v p="$prefix/" 'index($3, p) == 1 { print $3 }'
        done | sort -u | xargs -r cp -n -t deploy/
    done
    ;;
macos)
    # Qt's official Qt 6 build is universal, so build the program for both Intel and Apple Silicon too
    archs=()
    [ "$qt" = 6 ] && archs=("QMAKE_APPLE_DEVICE_ARCHS=x86_64 arm64")
    # ${archs[@]+...}: an empty array is "unbound" to macOS's bash 3.2 under set -u
    (cd build && qmake "../$app.pro" CONFIG+=release ${archs[@]+"${archs[@]}"} && make -j"$(sysctl -n hw.ncpu)")
    exe=()
    if [ -n "$cli" ]; then
        mkdir -p build-cli
        (cd build-cli && qmake "../$cli.pro" CONFIG+=release ${archs[@]+"${archs[@]}"} && make -j"$(sysctl -n hw.ncpu)")
        cp "build-cli/$cli" "build/$app.app/Contents/MacOS/"
        exe=("-executable=build/$app.app/Contents/MacOS/$cli")         # macdeployqt points it at the bundle's Qt too
    fi
    macdeployqt "build/$app.app" -always-overwrite ${exe[@]+"${exe[@]}"}
    ;;
linux)
    if [ "$qt" = 6 ]; then qmake=$(command -v qmake6); else qmake=$(command -v qmake); fi
    (cd build && "$qmake" "../$app.pro" CONFIG+=release PREFIX=/usr && make -j"$(nproc)" &&
     make install INSTALL_ROOT="$PWD/AppDir")
    extra=()
    if [ -n "$cli" ]; then
        mkdir -p build-cli
        (cd build-cli && "$qmake" "../$cli.pro" CONFIG+=release PREFIX=/usr && make -j"$(nproc)" &&
         make install INSTALL_ROOT="$PWD/../build/AppDir")
        # started as acorndw / cpmdw (a symlink to the AppImage: the runtime puts the name in $ARGV0) it is the console program
        sed -e "s/@APP@/$app/g" -e "s/@CLI@/$cli/g" "$(dirname "$0")/AppRun.in" > build/AppRun.custom
        chmod +x build/AppRun.custom
        extra=(--custom-apprun AppRun.custom --executable "AppDir/usr/bin/$cli")
    fi
    (cd build && QMAKE=$qmake linuxdeploy-x86_64.AppImage --appdir AppDir --plugin qt ${extra[@]+"${extra[@]}"})
    ;;
*) echo "build.sh: unknown platform '$platform'" >&2; exit 1 ;;
esac
