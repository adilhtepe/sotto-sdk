# ESP-IDF CI project: builds the VBAN SDK as an IDF component (the SDK's
# top-level CMakeLists.txt has an ESP_PLATFORM branch) and links it into an app.
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../..")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(vban_idf_smoke)
