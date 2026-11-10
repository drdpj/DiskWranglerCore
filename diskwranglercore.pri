# DiskWranglerCore - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
#
# qmake: the core's sources are compiled straight into the program, so no library has to be shipped.
# In an application's .pro file, with the core checked out as the submodule core/:
#
#     !include(core/diskwranglercore.pri): error("core/ is empty: run 'git submodule update --init'")
#
# (qmake's include() quietly ignores a file that isn't there, so the "!include ... error" form is what gives an
# empty core/ a message.)
#
# Code then includes the core's headers as <diskwrangler/ui/licences.h> and so on. This file adds QT += widgets
# and C++17 itself. (The CMake build is CMakeLists.txt: the target DiskWrangler::Core.)

!exists($$PWD/diskwrangler/ui/licences.cpp) {
    error("DiskWranglerCore is missing: run 'git submodule update --init' in the application's folder")
}

QT += widgets
CONFIG += c++17
INCLUDEPATH += $$PWD

include($$PWD/diskwrangler-device.pri)          # the device layer: containers, Linear, checked saving
include($$PWD/diskwrangler-fs.pri)              # the filesystem layer: Volume, Disc, detection
include($$PWD/diskwrangler-xfer.pri)            # transfer items and retro-files (QtCore only)
# (the optional modules: diskwrangler-dfs.pri, diskwrangler-fat.pri, and for a console program diskwrangler-cli.pri)

HEADERS += $$PWD/diskwrangler/app/appinfo.h \
           $$PWD/diskwrangler/app/appsettings.h \
           $$PWD/diskwrangler/app/document.h \
           $$PWD/diskwrangler/app/fileopen.h \
           $$PWD/diskwrangler/app/handoff.h \
           $$PWD/diskwrangler/text/textviews.h \
           $$PWD/diskwrangler/text/viewers.h \
           $$PWD/diskwrangler/ui/buttonbar.h \
           $$PWD/diskwrangler/ui/discwindow.h \
           $$PWD/diskwrangler/ui/dragoutmime.h \
           $$PWD/diskwrangler/ui/handoffdialog.h \
           $$PWD/diskwrangler/ui/licences.h \
           $$PWD/diskwrangler/ui/treecolumns.h \
           $$PWD/diskwrangler/ui/uihelpers.h \
           $$PWD/diskwrangler/ui/viewerdialog.h \
           $$PWD/diskwrangler/ui/volumepane.h \
           $$PWD/diskwrangler/xfer/hostname.h \
           $$PWD/diskwrangler/xfer/mime.h \
           $$PWD/diskwrangler/xfer/transferitem.h
SOURCES += $$PWD/diskwrangler/app/handoff.cpp \
           $$PWD/diskwrangler/text/textviews.cpp \
           $$PWD/diskwrangler/text/viewers.cpp \
           $$PWD/diskwrangler/ui/discwindow.cpp \
           $$PWD/diskwrangler/ui/dragoutmime.cpp \
           $$PWD/diskwrangler/ui/handoffdialog.cpp \
           $$PWD/diskwrangler/ui/licences.cpp \
           $$PWD/diskwrangler/ui/viewerdialog.cpp \
           $$PWD/diskwrangler/ui/volumepane.cpp
