# DiskWranglerCore - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
#
# CMake (Qt 6, or Qt 5.15): static library targets that an application links, with the core checked out as the
# submodule core/:
#
#     add_subdirectory(core)
#     target_link_libraries(myapp DiskWrangler::Core)
#
# Code then includes the core's headers as <diskwrangler/ui/licences.h> and so on. (The qmake build is
# diskwranglercore.pri.) Built on its own (cmake -S . -B build) it also builds and runs the core's tests.
cmake_minimum_required(VERSION 3.16)   # Qt 6 needs 3.16
project(DiskWranglerCore LANGUAGES CXX)

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)   # dragoutmime.h and viewerdialog.h have signals and slots

# An application has found Qt already; on its own, the core finds it the way the applications do.
if(NOT QT_VERSION_MAJOR)
    option(USE_QT5 "Build with Qt 5 instead of Qt 6" OFF)
    if(USE_QT5)
        find_package(QT NAMES Qt5 REQUIRED COMPONENTS Widgets)
    else()
        find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
    endif()
endif()
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

# The device layer (QtCore only): disc image containers, Linear views, checked saving.
add_library(diskwrangler-device STATIC
    diskwrangler/app/clock.cpp
    diskwrangler/device/container.cpp
    diskwrangler/device/container_formats.cpp
    diskwrangler/device/linear.cpp
    diskwrangler/device/lzhuf.cpp
    diskwrangler/device/safesave.cpp
    diskwrangler/device/sideorder.cpp
    diskwrangler/app/clock.h
    diskwrangler/device/container.h
    diskwrangler/device/linear.h
    diskwrangler/device/safesave.h
    diskwrangler/device/sideorder.h)
add_library(DiskWrangler::Device ALIAS diskwrangler-device)
target_include_directories(diskwrangler-device PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(diskwrangler-device PUBLIC Qt${QT_VERSION_MAJOR}::Core)

# The filesystem layer (QtCore only): Volume, Disc, detection by tiers.
add_library(diskwrangler-fs STATIC
    diskwrangler/fs/detection.cpp
    diskwrangler/fs/disc.cpp
    diskwrangler/fs/volume.cpp
    diskwrangler/fs/detection.h
    diskwrangler/fs/disc.h
    diskwrangler/fs/volume.h)
add_library(DiskWrangler::Fs ALIAS diskwrangler-fs)
target_include_directories(diskwrangler-fs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(diskwrangler-fs PUBLIC DiskWrangler::Device Qt${QT_VERSION_MAJOR}::Core)

# The transfer layer (QtCore only): transfer items, host names, retro-files (.inf / manifest / JSON), the Importer.
add_library(diskwrangler-xfer STATIC
    diskwrangler/xfer/importer.cpp
    diskwrangler/xfer/retrofiles.cpp
    diskwrangler/xfer/hostname.h
    diskwrangler/xfer/importer.h
    diskwrangler/xfer/mime.h
    diskwrangler/xfer/retrofiles.h
    diskwrangler/xfer/transferitem.h)
add_library(DiskWrangler::Xfer ALIAS diskwrangler-xfer)
target_link_libraries(diskwrangler-xfer PUBLIC DiskWrangler::Fs)

# The command-line layer (QtCore only): arguments, commands, JSON output, runCli().
add_library(diskwrangler-cli STATIC
    diskwrangler/cli/args.cpp
    diskwrangler/cli/commands.cpp
    diskwrangler/cli/output.cpp
    diskwrangler/cli/args.h
    diskwrangler/cli/cli.h
    diskwrangler/cli/output.h)
add_library(DiskWrangler::Cli ALIAS diskwrangler-cli)
target_link_libraries(diskwrangler-cli PUBLIC DiskWrangler::Xfer)

# Optional filesystem modules (QtCore only): an application links the ones it registers.
add_library(diskwrangler-dfs STATIC
    diskwrangler/fs/dfs/dfs.cpp
    diskwrangler/fs/dfs/dfsprobe.cpp
    diskwrangler/fs/dfs/dfsvolume.cpp
    diskwrangler/fs/dfs/inf.cpp
    diskwrangler/fs/dfs/dfs.h
    diskwrangler/fs/dfs/dfsprobe.h
    diskwrangler/fs/dfs/dfsvolume.h
    diskwrangler/fs/dfs/inf.h)
add_library(DiskWrangler::Dfs ALIAS diskwrangler-dfs)
target_link_libraries(diskwrangler-dfs PUBLIC DiskWrangler::Fs)

add_library(diskwrangler-fat STATIC
    diskwrangler/fs/fat/fat.cpp
    diskwrangler/fs/fat/fatprobe.cpp
    diskwrangler/fs/fat/fatvolume.cpp
    diskwrangler/fs/fat/fat.h
    diskwrangler/fs/fat/fatprobe.h
    diskwrangler/fs/fat/fatvolume.h)
add_library(DiskWrangler::Fat ALIAS diskwrangler-fat)
target_link_libraries(diskwrangler-fat PUBLIC DiskWrangler::Fs)

add_library(diskwrangler-core STATIC
    diskwrangler/app/handoff.cpp
    diskwrangler/text/textviews.cpp
    diskwrangler/text/viewers.cpp
    diskwrangler/ui/dragoutmime.cpp
    diskwrangler/ui/handoffdialog.cpp
    diskwrangler/ui/licences.cpp
    diskwrangler/ui/viewerdialog.cpp
    diskwrangler/app/appsettings.h
    diskwrangler/app/fileopen.h
    diskwrangler/app/handoff.h
    diskwrangler/text/textviews.h
    diskwrangler/text/viewers.h
    diskwrangler/ui/buttonbar.h
    diskwrangler/ui/dragoutmime.h
    diskwrangler/ui/handoffdialog.h
    diskwrangler/ui/licences.h
    diskwrangler/ui/treecolumns.h
    diskwrangler/ui/uihelpers.h
    diskwrangler/ui/viewerdialog.h)
add_library(DiskWrangler::Core ALIAS diskwrangler-core)
target_include_directories(diskwrangler-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(diskwrangler-core PUBLIC DiskWrangler::Device DiskWrangler::Fs DiskWrangler::Xfer Qt${QT_VERSION_MAJOR}::Widgets)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_executable(settingstest tests/settingstest.cpp)
    target_link_libraries(settingstest DiskWrangler::Core)
    add_test(NAME settingstest COMMAND settingstest)
    set_tests_properties(settingstest PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
    add_executable(devicetest tests/devicetest.cpp)
    target_link_libraries(devicetest DiskWrangler::Device)
    add_test(NAME devicetest COMMAND devicetest)
    add_executable(volumetest tests/volumetest.cpp)
    target_link_libraries(volumetest DiskWrangler::Fs)
    add_test(NAME volumetest COMMAND volumetest)
    add_executable(moduletest tests/moduletest.cpp)
    target_link_libraries(moduletest DiskWrangler::Dfs DiskWrangler::Fat)
    add_test(NAME moduletest COMMAND moduletest)
    add_executable(clitest tests/clitest.cpp)
    target_link_libraries(clitest DiskWrangler::Cli DiskWrangler::Fat)
    add_test(NAME clitest COMMAND clitest)
    foreach(t uitest xfertest)
        add_executable(${t} tests/${t}.cpp)
        target_link_libraries(${t} DiskWrangler::Core)
        add_test(NAME ${t} COMMAND ${t})
        set_tests_properties(${t} PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
    endforeach()
endif()
