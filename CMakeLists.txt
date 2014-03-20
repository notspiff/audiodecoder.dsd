project(audiodecoder.dsd)

cmake_minimum_required(VERSION 2.6)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

enable_language(CXX)

find_package(xbmc REQUIRED)

include_directories(${XBMC_INCLUDE_DIR})

set(DSD_SOURCES src/dsd2pcm.c
                src/DSDCodec.cpp)

set(DEPLIBS)

build_addon(audiodecoder.dsd DSD DEPLIBS)
include(CPack)
