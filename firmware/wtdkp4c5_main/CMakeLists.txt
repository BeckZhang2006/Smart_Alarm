cmake_minimum_required(VERSION 3.16)

# Set target board
set(IDF_TARGET esp32p4)

# Include project
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(smart_alarm_main)
