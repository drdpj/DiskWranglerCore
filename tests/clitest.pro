# Part of DiskWranglerCore - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# qmake: mkdir b && cd b && qmake ../tests/clitest.pro && make && ./clitest
QT = core
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = clitest
SOURCES += clitest.cpp
include(../diskwrangler-fat.pri)
include(../diskwrangler-cli.pri)
