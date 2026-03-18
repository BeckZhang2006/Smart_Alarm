cmake_minimum_required(VERSION 3.16)

# Include esp-hosted-mcu component
set(EXTRA_COMPONENT_DIRS 
    $ENV{IDF_PATH}/examples/common_components/protocol_examples_common
    $ENV{IDF_PATH}/../esp-hosted-mcu-main/common
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(smart_alarm_c5_sdio_slave)
