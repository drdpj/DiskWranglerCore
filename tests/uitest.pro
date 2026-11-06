# Part of DiskWranglerCore - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# qmake: mkdir b && cd b && qmake ../tests/uitest.pro && make && QT_QPA_PLATFORM=offscreen ./uitest
QT += widgets
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = uitest
INCLUDEPATH += $$PWD/..
SOURCES += uitest.cpp ../diskwrangler/text/textviews.cpp ../diskwrangler/text/viewers.cpp ../diskwrangler/ui/viewerdialog.cpp
HEADERS += ../diskwrangler/text/textviews.h ../diskwrangler/text/viewers.h ../diskwrangler/ui/treecolumns.h ../diskwrangler/ui/uihelpers.h ../diskwrangler/ui/viewerdialog.h
