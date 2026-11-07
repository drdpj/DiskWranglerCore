# DiskWranglerCore - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
#
# qmake: the device layer alone (disc image containers, Linear views, checked saving). QtCore only, so a test or a
# command-line program can use it without widgets:
#
#     !include(core/diskwrangler-device.pri): error("core/ is empty: run 'git submodule update --init'")
#
# diskwranglercore.pri includes this file, so an application needs only that one; it is added only once however often
# it is included. (CMake: the target DiskWrangler::Device, which DiskWrangler::Core links.)

!contains(DW_PRI_DONE, device) {
DW_PRI_DONE += device
!exists($$PWD/diskwrangler/device/container.cpp) {
    error("DiskWranglerCore is missing: run 'git submodule update --init' in the application's folder")
}

CONFIG += c++17
INCLUDEPATH += $$PWD

HEADERS += $$PWD/diskwrangler/device/container.h \
           $$PWD/diskwrangler/device/linear.h \
           $$PWD/diskwrangler/device/safesave.h \
           $$PWD/diskwrangler/device/sideorder.h
SOURCES += $$PWD/diskwrangler/device/container.cpp \
           $$PWD/diskwrangler/device/container_formats.cpp \
           $$PWD/diskwrangler/device/linear.cpp \
           $$PWD/diskwrangler/device/lzhuf.cpp \
           $$PWD/diskwrangler/device/safesave.cpp \
           $$PWD/diskwrangler/device/sideorder.cpp
}
