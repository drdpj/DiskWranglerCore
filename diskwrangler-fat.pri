# DiskWranglerCore - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
#
# qmake: the optional FAT module - FAT12/16 volumes (whole floppies, or inside a file such as DOS Plus's
# $.DOS.DRIVE_C), their detection and the Volume adapter. QtCore only. In an application's .pro file:
#
#     !include(core/diskwrangler-fat.pri): error("core/ is empty: run 'git submodule update --init'")
#
# (CMake: the target DiskWrangler::Fat.)

!contains(DW_PRI_DONE, fat) {
DW_PRI_DONE += fat
include($$PWD/diskwrangler-fs.pri)

HEADERS += $$PWD/diskwrangler/fs/fat/fat.h \
           $$PWD/diskwrangler/fs/fat/fatprobe.h \
           $$PWD/diskwrangler/fs/fat/fatvolume.h
SOURCES += $$PWD/diskwrangler/fs/fat/fat.cpp \
           $$PWD/diskwrangler/fs/fat/fatprobe.cpp \
           $$PWD/diskwrangler/fs/fat/fatvolume.cpp
}
