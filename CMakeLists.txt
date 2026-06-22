cmake_minimum_required(VERSION 3.16)

# Third-party Arduino library wrappers live here.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/components"
    "${CMAKE_CURRENT_LIST_DIR}/components/esp-homekit-sdk/components"
    "${CMAKE_CURRENT_LIST_DIR}/components/esp-homekit-sdk/components/homekit")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(SomfyController)

# The default arduino_events task stack (4096) overflows: setConnected() runs
# in this task context and calls MDNS.begin() + addServiceTxt() which together
# need far more than 4096 bytes.
# idf_build_set_property is the correct IDF API to propagate a compile
# definition into managed components (add_compile_definitions does not work).
idf_build_set_property(COMPILE_DEFINITIONS
    "ARDUINO_NETWORK_EVENT_TASK_STACK_SIZE=16384" APPEND)

# The LittleFS image (built by littlefs_create_partition_image in
# main/CMakeLists.txt) is named after its partition: build/spiffs.bin. Mirror it
# under the canonical *.littlefs.bin name so it can be uploaded via the web UI /
# GitOTA without a manual rename, and so release CI can publish the IDF-generated
# image directly. The web upload guard (data/extras.js) requires ".littlefs" in
# the filename; GitOTA downloads SomfyController.littlefs.bin from releases.
add_custom_command(TARGET app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CMAKE_BINARY_DIR}/spiffs.bin"
        "${CMAKE_BINARY_DIR}/SomfyController.littlefs.bin"
    BYPRODUCTS "${CMAKE_BINARY_DIR}/SomfyController.littlefs.bin"
    COMMENT "Mirroring LittleFS image to SomfyController.littlefs.bin")

#── Unit Tests (host-based, separate from firmware build) ──────────────────
set(test_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/test/unit")
set(test_build_dir "${test_source_dir}/build")

add_custom_target(build_unit_tests
    COMMAND cmake -S ${test_source_dir} -B ${test_build_dir}
                  -DCMAKE_BUILD_TYPE=Debug
    COMMAND cmake --build ${test_build_dir} --parallel
    COMMENT "Building unit tests"
)

add_custom_target(run_unit_tests
    COMMAND ctest --test-dir ${test_build_dir} --output-on-failure
    DEPENDS build_unit_tests
    COMMENT "Running unit tests"
)

#── clang-format (main/ and test/ only; skips components/) ──────────────────
# Prefer the ESP-IDF clang-format so the version matches the esp-clang toolchain
# and clang-tidy; fall back to one on PATH.
file(GLOB esp_clang_format
    "$ENV{HOME}/.espressif/tools/esp-clang/esp-19.1.2_*/esp-clang/bin/clang-format")
if(esp_clang_format)
    list(GET esp_clang_format 0 CLANG_FORMAT_BIN)
else()
    find_program(CLANG_FORMAT_BIN clang-format)
endif()

file(GLOB_RECURSE format_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/main/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/main/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/test/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/test/*.h")
# Exclude generated unit-test build trees.
list(FILTER format_sources EXCLUDE REGEX "/build/")

add_custom_target(format
    COMMAND ${CLANG_FORMAT_BIN} -i --style=file ${format_sources}
    COMMENT "Formatting main/ and test/ sources (clang-format -i)"
    VERBATIM
)

add_custom_target(format-check
    COMMAND ${CLANG_FORMAT_BIN} --dry-run --Werror --style=file ${format_sources}
    COMMENT "Checking formatting of main/ and test/ sources"
    VERBATIM
)