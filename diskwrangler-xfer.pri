# DiskWranglerCore - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
#
# qmake: the transfer layer (QtCore only): transfer items, host names, retro-files (.inf / manifest / JSON), and the
# Importer (the rules for putting items into a volume, for the command line and the windows).
!contains(DW_PRI_DONE, xfer) {
DW_PRI_DONE += xfer
!exists($$PWD/diskwrangler/xfer/retrofiles.cpp) {
    error("DiskWranglerCore is missing: run 'git submodule update --init' in the application's folder")
}
include($$PWD/diskwrangler-fs.pri)

HEADERS += $$PWD/diskwrangler/xfer/hostname.h \
           $$PWD/diskwrangler/xfer/importer.h \
           $$PWD/diskwrangler/xfer/mime.h \
           $$PWD/diskwrangler/xfer/retrofiles.h \
           $$PWD/diskwrangler/xfer/transferitem.h
SOURCES += $$PWD/diskwrangler/xfer/importer.cpp \
           $$PWD/diskwrangler/xfer/retrofiles.cpp
}
