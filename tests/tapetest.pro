# Part of DiskWranglerCore - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# qmake: mkdir b && cd b && qmake ../tests/tapetest.pro && make && ./tapetest
# ./tapetest --dump FILE prints a tape as tape_check.py does; ./tapetest --write-samples DIR writes the hand-made tapes.
QT = core
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = tapetest
SOURCES += tapetest.cpp
include(../diskwrangler-tape.pri)
include(../diskwrangler-fs.pri)
