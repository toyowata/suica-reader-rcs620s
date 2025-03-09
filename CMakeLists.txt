# Copyright (c) 2020 ARM Limited. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.19.0 FATAL_ERROR)
# enable_language(CXX)

set(MBED_APP_JSON_PATH mbed_app.json)
set(MBED_PATH ${CMAKE_CURRENT_SOURCE_DIR}/mbed-os CACHE INTERNAL "")
set(MBED_CONFIG_PATH ${CMAKE_CURRENT_BINARY_DIR} CACHE INTERNAL "")
set(APP_TARGET suica-reader-rcs620s)

include(mbed-os/tools/cmake/app.cmake)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/SB1602E)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/AS289R2)

add_subdirectory(${MBED_PATH})

add_executable(${APP_TARGET})

mbed_configure_app_target(${APP_TARGET})

project(${APP_TARGET})

target_sources(${APP_TARGET}
    PRIVATE
        main.cpp
)

target_link_libraries(${APP_TARGET}
    PRIVATE
        mbed-os
        mbed-usb
        mbed-events
)

mbed_set_post_build(${APP_TARGET})

option(VERBOSE_BUILD "Have a verbose build process")
if(VERBOSE_BUILD)
    set(CMAKE_VERBOSE_MAKEFILE ON)
endif()

if("raspberry_pi_pico" IN_LIST MBED_TARGET_LABELS)
    add_subdirectory(AS289R2)
    add_subdirectory(SB1602E)
    target_link_libraries(suica-reader-rcs620s
        PRIVATE
            AS289R2
            SB1602E
    )
endif()
