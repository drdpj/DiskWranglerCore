# DiskWranglerCore - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
#
# qmake: the command-line layer (QtCore only): argument parsing, the commands, JSON output, runCli().
# A console program's .pro file includes this plus the filesystem modules it registers, and sets QT = core.
!contains(DW_PRI_DONE, cli) {
DW_PRI_DONE += cli
!exists($$PWD/diskwrangler/cli/commands.cpp) {
    error("DiskWranglerCore is missing: run 'git submodule update --init' in the application's folder")
}
include($$PWD/diskwrangler-xfer.pri)

HEADERS += $$PWD/diskwrangler/app/appinfo.h \
           $$PWD/diskwrangler/cli/args.h \
           $$PWD/diskwrangler/cli/cli.h \
           $$PWD/diskwrangler/cli/output.h
SOURCES += $$PWD/diskwrangler/cli/args.cpp \
           $$PWD/diskwrangler/cli/commands.cpp \
           $$PWD/diskwrangler/cli/output.cpp
}
