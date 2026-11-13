# Part of DiskWranglerCore - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
# qmake: mkdir b && cd b && qmake ../tests/xfertest.pro && make && ./xfertest
QT = core
CONFIG += c++17 console
CONFIG -= app_bundle
TARGET = xfertest
INCLUDEPATH += $$PWD/..
SOURCES += xfertest.cpp ../diskwrangler/xfer/retrofiles.cpp ../diskwrangler/xfer/hostread.cpp
HEADERS += ../diskwrangler/xfer/hostname.h ../diskwrangler/xfer/hostread.h ../diskwrangler/xfer/mime.h ../diskwrangler/xfer/retrofiles.h ../diskwrangler/xfer/transferitem.h
