cmake_minimum_required(VERSION 3.20)
project(SomfyShadeUnitTests CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── GoogleTest ──────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# ── ArduinoJson (header-only, compiles fine on Linux) ──────────────────────
FetchContent_Declare(arduinojson
    GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
    GIT_TAG        v7.2.0
)
FetchContent_MakeAvailable(arduinojson)

# ── Source files under test ─────────────────────────────────────────────────
# We compile only the Somfy protocol/shade files; everything else is stubbed.
set(PRODUCTION_SOURCES
    ../../main/somfy/shade/SomfyFlagManager.cpp
    ../../main/somfy/shade/SomfyGPIOControl.cpp
    ../../main/somfy/shade/SomfyMQTTPublisher.cpp
    ../../main/somfy/shade/SomfyPersistence.cpp
    ../../main/somfy/shade/SomfyCommandTransmitter.cpp
    ../../main/somfy/shade/SomfyTargetSequencer.cpp
    ../../main/somfy/shade/SomfyCommandProcessor.cpp
    ../../main/somfy/shade/SomfyMovementTracker.cpp
    ../../main/somfy/shade/SomfyJSONSerializer.cpp
    ../../main/somfy/shade/SomfyShade.cpp
    ../../main/somfy/SomfyRemote.cpp
    ../../main/somfy/SomfyFrame.cpp
)

add_executable(shade_tests
    test_commandProcessor.cpp
    test_movementTracker.cpp
    test_targetSequencer.cpp
    test_gpioControl.cpp
    test_mqttPublisher.cpp
    test_persistence.cpp
    test_commandTransmitter.cpp
    test_jsonSerializer.cpp
    test_somfyShade.cpp
    stubs/globals_stub.cpp
    ${PRODUCTION_SOURCES}
)

# ── Include path order is critical ─────────────────────────────────────────
# 1. stubs/        — intercepts Arduino/ESP-IDF/heavy project headers
# 2. main/         — real project headers not in main/somfy/
# 3. main/somfy/   — core Somfy headers (SomfyController.h, SomfyRemote.h etc.)
# 4. main/somfy/shade/ — shade-specific headers (SomfyShade.h etc.)
# 5. ArduinoJson   — real library (header-only, works on Linux)
target_include_directories(shade_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${CMAKE_CURRENT_SOURCE_DIR}/../../main
    ${CMAKE_CURRENT_SOURCE_DIR}/../../main/somfy
    ${CMAKE_CURRENT_SOURCE_DIR}/../../main/somfy/shade
    ${arduinojson_SOURCE_DIR}/src
)

target_link_libraries(shade_tests
    GTest::gtest_main
    GTest::gmock
)

# Suppress warnings that fire due to stub headers or ESP-IDF code patterns.
target_compile_definitions(shade_tests PRIVATE USE_NVS)

target_compile_options(shade_tests PRIVATE
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-unused-variable
    -Wno-inconsistent-missing-override
    -Wno-non-pod-varargs
    -Wno-deprecated-declarations
    --coverage
)

target_link_options(shade_tests PRIVATE --coverage)

include(GoogleTest)
gtest_add_tests(TARGET shade_tests SOURCES test_commandProcessor.cpp test_movementTracker.cpp test_targetSequencer.cpp test_gpioControl.cpp test_mqttPublisher.cpp test_persistence.cpp test_commandTransmitter.cpp test_jsonSerializer.cpp test_somfyShade.cpp)
