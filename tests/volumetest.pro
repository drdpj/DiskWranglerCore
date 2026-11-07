# Part of DiskWranglerCore - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# qmake: mkdir b && cd b && qmake ../tests/volumetest.pro && make && ./volumetest
QT = core
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = volumetest
SOURCES += volumetest.cpp
include(../diskwrangler-fs.pri)
