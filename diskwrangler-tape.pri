# DiskWranglerCore - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
#
# qmake: the optional tape module - tape image files (UEF, gzip or not; TZX and CDT; the Spectrum's TAP), read and
# written chunk for chunk, and what their chunks mean (tones, gaps, data and how it is coded). It knows no machine's
# files: a program reads those from the data. QtCore only. In an application's .pro file:
#
#     !include(core/diskwrangler-tape.pri): error("core/ is empty: run 'git submodule update --init'")
#
# (CMake: the target DiskWrangler::Tape.)

!contains(DW_PRI_DONE, tape) {
DW_PRI_DONE += tape
include($$PWD/diskwrangler-device.pri)

HEADERS += $$PWD/diskwrangler/tape/gzip.h \
           $$PWD/diskwrangler/tape/records.h \
           $$PWD/diskwrangler/tape/tapefile.h
SOURCES += $$PWD/diskwrangler/tape/gzip.cpp \
           $$PWD/diskwrangler/tape/records.cpp \
           $$PWD/diskwrangler/tape/tapefile.cpp
}
