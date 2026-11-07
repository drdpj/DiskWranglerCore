# Part of DiskWranglerCore - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# qmake: mkdir b && cd b && qmake ../tests/moduletest.pro && make && ./moduletest
QT = core
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = moduletest
SOURCES += moduletest.cpp
include(../diskwrangler-dfs.pri)
include(../diskwrangler-fat.pri)
