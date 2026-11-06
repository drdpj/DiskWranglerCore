# Part of DiskWranglerCore - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# qmake: mkdir b && cd b && qmake ../tests/settingstest.pro && make && QT_QPA_PLATFORM=offscreen ./settingstest
QT += widgets
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = settingstest
INCLUDEPATH += $$PWD/..
SOURCES += settingstest.cpp
HEADERS += ../diskwrangler/app/appsettings.h
