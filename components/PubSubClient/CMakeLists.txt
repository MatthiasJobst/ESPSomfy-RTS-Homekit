set(LIB_DIR "$ENV{HOME}/Documents/Arduino/libraries/PubSubClient/src")

idf_component_register(
    SRCS
        "${LIB_DIR}/PubSubClient.cpp"
    INCLUDE_DIRS
        "${LIB_DIR}"
    REQUIRES
        arduino-esp32
)
