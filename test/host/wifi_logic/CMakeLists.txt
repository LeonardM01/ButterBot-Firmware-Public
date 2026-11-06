# Host (laptop) unit tests for the ESP-IDF-free Wi-Fi logic. See README.md.
cmake_minimum_required(VERSION 3.16)
set(CMAKE_CXX_STANDARD 23)
set(IDF_TARGET linux)
# Keep the generated sdkconfig inside build/ so there is nothing to commit or ignore.
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(wifi_logic_test)
