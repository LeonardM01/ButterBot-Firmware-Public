# Host (Linux target) Unity tests for the measurement build's pure logic. See README.md.
cmake_minimum_required(VERSION 3.16)
set(CMAKE_CXX_STANDARD 23)
set(IDF_TARGET linux)
# Keep the generated sdkconfig inside build/ so nothing here needs committing or ignoring.
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(measure_logic_test)
