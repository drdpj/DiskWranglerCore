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

HEADERS += $$PWD/diskwrangler/app/appsettings.h \
           $$PWD/diskwrangler/app/fileopen.h \
           $$PWD/diskwrangler/ui/buttonbar.h \
           $$PWD/diskwrangler/ui/licences.h
SOURCES += $$PWD/diskwrangler/ui/licences.cpp
