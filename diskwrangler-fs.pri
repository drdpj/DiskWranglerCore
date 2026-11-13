# DiskWranglerCore - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
#
# qmake: the filesystem layer (Volume, Disc, detection by tiers, new-disc presets), QtCore only, with the device layer under it:
#
#     !include(core/diskwrangler-fs.pri): error("core/ is empty: run 'git submodule update --init'")
#
# diskwranglercore.pri, diskwrangler-dfs.pri and diskwrangler-fat.pri include it, and each of these files is added
# only once however often it is included. (CMake: the target DiskWrangler::Fs.)

!contains(DW_PRI_DONE, fs) {
DW_PRI_DONE += fs
!exists($$PWD/diskwrangler/fs/volume.cpp) {
    error("DiskWranglerCore is missing: run 'git submodule update --init' in the application's folder")
}
include($$PWD/diskwrangler-device.pri)

HEADERS += $$PWD/diskwrangler/fs/detection.h \
           $$PWD/diskwrangler/fs/disc.h \
           $$PWD/diskwrangler/fs/presets.h \
           $$PWD/diskwrangler/fs/volume.h
SOURCES += $$PWD/diskwrangler/fs/detection.cpp \
           $$PWD/diskwrangler/fs/disc.cpp \
           $$PWD/diskwrangler/fs/presets.cpp \
           $$PWD/diskwrangler/fs/volume.cpp
}
