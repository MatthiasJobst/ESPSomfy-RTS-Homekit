idf_component_register(
    SRCS
        "ELECHOUSE_CC1101_SRC_DRV.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        arduino-esp32
)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-misleading-indentation
    -Wno-maybe-uninitialized
)
