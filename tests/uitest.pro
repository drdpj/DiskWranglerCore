# Part of DiskWranglerCore - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# qmake: mkdir b && cd b && qmake ../tests/uitest.pro && make && QT_QPA_PLATFORM=offscreen ./uitest
QT += widgets
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = uitest
SOURCES += uitest.cpp
HEADERS += memvolume.h
include(../diskwranglercore.pri)          # the helpers, the viewers, the hand-over and (step 5c) the window kit
