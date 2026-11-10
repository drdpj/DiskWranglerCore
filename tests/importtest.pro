# Part of DiskWranglerCore - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# qmake: mkdir b && cd b && qmake ../tests/importtest.pro && make && ./importtest
QT = core
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = importtest
SOURCES += importtest.cpp
include(../diskwrangler-dfs.pri)
include(../diskwrangler-fat.pri)
include(../diskwrangler-xfer.pri)
