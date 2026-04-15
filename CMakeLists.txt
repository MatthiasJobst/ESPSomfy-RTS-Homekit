cmake_minimum_required(VERSION 3.16)

# Third-party Arduino library wrappers live here.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components"
    "${CMAKE_CURRENT_LIST_DIR}/components/esp-homekit-sdk/components"
    "${CMAKE_CURRENT_LIST_DIR}/components/esp-homekit-sdk/components/homekit")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(SomfyController)

# The default arduino_events task stack (4096) overflows: setConnected() runs
# in this task context and calls MDNS.begin() + addServiceTxt() + SSDP.begin()
# which together need far more than 4096 bytes.
# idf_build_set_property is the correct IDF API to propagate a compile
# definition into managed components (add_compile_definitions does not work).
idf_build_set_property(COMPILE_DEFINITIONS "ARDUINO_NETWORK_EVENT_TASK_STACK_SIZE=16384" APPEND)

# Replace the bit-bang RF TX path with the hardware RMT peripheral so the
# main task is free during the ~0.5–1.5 s transmission window.
idf_build_set_property(COMPILE_DEFINITIONS "SOMFY_TX_RMT" APPEND)

# ── Unit Tests (host-based, separate from firmware build) ──────────────────
set(test_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/test/unit")
set(test_build_dir "${test_source_dir}/build")

add_custom_target(build_unit_tests
    COMMAND cmake -S ${test_source_dir} -B ${test_build_dir} -DCMAKE_BUILD_TYPE=Debug
    COMMAND cmake --build ${test_build_dir} --parallel
    COMMENT "Building unit tests"
)

add_custom_target(run_unit_tests
    COMMAND find ${test_build_dir} -name "*.gcda" -delete && ${test_build_dir}/shade_tests
    DEPENDS build_unit_tests
    COMMENT "Running unit tests"
)