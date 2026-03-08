cmake_minimum_required(VERSION 3.16)

# Third-party Arduino library wrappers live here.
# esp-homekit-sdk components will be added in a later migration step.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/components")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(SomfyController)

# The default arduino_events task stack (4096) overflows: setConnected() runs
# in this task context and calls MDNS.begin() + addServiceTxt() + SSDP.begin()
# which together need far more than 4096 bytes.
# idf_build_set_property is the correct IDF API to propagate a compile
# definition into managed components (add_compile_definitions does not work).
idf_build_set_property(COMPILE_DEFINITIONS "ARDUINO_NETWORK_EVENT_TASK_STACK_SIZE=16384" APPEND)
