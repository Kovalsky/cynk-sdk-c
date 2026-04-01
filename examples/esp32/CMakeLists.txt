# Cynk ESP32 Example — ESP-IDF project file.
#
# The SDK root (two levels up) is registered as an ESP-IDF component.
# When copying this example to your own project, clone the SDK into
# components/cynk-sdk-c/ instead and remove the EXTRA_COMPONENT_DIRS line.

cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../..")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cynk_esp32_example)
