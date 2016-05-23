project(audiodecoder.dsd)

cmake_minimum_required(VERSION 2.6)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

enable_language(CXX)

find_package(Kodi REQUIRED)

include_directories(${KODI_INCLUDE_DIR})

set(DSD_SOURCES src/bit_reverse.c
                src/dsd2pcm.c
                src/DSDCodec.cpp
                src/DSDiffParser.cpp
                src/DSFParser.cpp)

set(DEPLIBS)

build_addon(audiodecoder.dsd DSD DEPLIBS)
include(CPack)
