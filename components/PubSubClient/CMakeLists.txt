idf_component_register(
    SRCS
        "${CMAKE_CURRENT_LIST_DIR}/src/src/PubSubClient.cpp"
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/src/src"
    REQUIRES
        arduino-esp32
)
