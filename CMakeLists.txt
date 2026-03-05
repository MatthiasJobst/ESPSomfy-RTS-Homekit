cmake_minimum_required(VERSION 3.16)

# Third-party Arduino library wrappers live here.
# esp-homekit-sdk components will be added in a later migration step.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(SomfyController)
