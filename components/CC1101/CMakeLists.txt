set(LIB_DIR "$ENV{HOME}/Documents/Arduino/libraries/SmartRC-CC1101-Driver-Lib")

idf_component_register(
    SRCS
        "${LIB_DIR}/ELECHOUSE_CC1101_SRC_DRV.cpp"
    INCLUDE_DIRS
        "${LIB_DIR}"
    REQUIRES
        arduino-esp32
)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-misleading-indentation
    -Wno-maybe-uninitialized
)
