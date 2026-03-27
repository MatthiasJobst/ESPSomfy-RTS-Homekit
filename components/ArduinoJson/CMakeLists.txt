# ArduinoJson is header-only; no sources to compile.
set(LIB_DIR "$ENV{HOME}/Documents/Arduino/libraries/ArduinoJson/src")

idf_component_register(
    INCLUDE_DIRS
        "${LIB_DIR}"
)
