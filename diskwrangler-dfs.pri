# DiskWranglerCore - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
#
# qmake: the optional DFS module - Acorn DFS discs (with Watford 62-file catalogues), their detection, the Volume
# adapter, .inf sidecar files, and Acorn Z80 CP/M's DFS catalogue. QtCore only. In an application's .pro file:
#
#     !include(core/diskwrangler-dfs.pri): error("core/ is empty: run 'git submodule update --init'")
#
# (CMake: the target DiskWrangler::Dfs.)

!contains(DW_PRI_DONE, dfs) {
DW_PRI_DONE += dfs
include($$PWD/diskwrangler-fs.pri)

HEADERS += $$PWD/diskwrangler/fs/dfs/dfs.h \
           $$PWD/diskwrangler/fs/dfs/dfsprobe.h \
           $$PWD/diskwrangler/fs/dfs/dfsvolume.h \
           $$PWD/diskwrangler/fs/dfs/inf.h
SOURCES += $$PWD/diskwrangler/fs/dfs/dfs.cpp \
           $$PWD/diskwrangler/fs/dfs/dfsprobe.cpp \
           $$PWD/diskwrangler/fs/dfs/dfsvolume.cpp \
           $$PWD/diskwrangler/fs/dfs/inf.cpp
}
