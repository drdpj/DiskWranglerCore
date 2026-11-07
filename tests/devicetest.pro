# Part of DiskWranglerCore - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# qmake: mkdir b && cd b && qmake ../tests/devicetest.pro && make && ./devicetest
QT = core
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = devicetest
SOURCES += devicetest.cpp
include(../diskwrangler-device.pri)
