# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(CMAKE_CXX_STANDARD 23)

set(IDF_TARGET esp32s3)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main/secrets.h")
    message(FATAL_ERROR
        "Missing main/secrets.h. Copy main/secrets.h.example to main/secrets.h "
        "and replace every placeholder before configuring the firmware.")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ButterBot-Firmware)
