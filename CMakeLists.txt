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

add_library(diskwrangler-core STATIC
    diskwrangler/text/textviews.cpp
    diskwrangler/text/viewers.cpp
    diskwrangler/ui/licences.cpp
    diskwrangler/ui/viewerdialog.cpp
    diskwrangler/app/appsettings.h
    diskwrangler/app/fileopen.h
    diskwrangler/text/textviews.h
    diskwrangler/text/viewers.h
    diskwrangler/ui/buttonbar.h
    diskwrangler/ui/licences.h
    diskwrangler/ui/treecolumns.h
    diskwrangler/ui/uihelpers.h
    diskwrangler/ui/viewerdialog.h
    diskwrangler/xfer/hostname.h)
add_library(DiskWrangler::Core ALIAS diskwrangler-core)
target_include_directories(diskwrangler-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(diskwrangler-core PUBLIC Qt${QT_VERSION_MAJOR}::Widgets)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_executable(settingstest tests/settingstest.cpp)
    target_link_libraries(settingstest DiskWrangler::Core)
    add_test(NAME settingstest COMMAND settingstest)
    set_tests_properties(settingstest PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
    foreach(t uitest)
        add_executable(${t} tests/${t}.cpp)
        target_link_libraries(${t} DiskWrangler::Core)
        add_test(NAME ${t} COMMAND ${t})
        set_tests_properties(${t} PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
    endforeach()
endif()
