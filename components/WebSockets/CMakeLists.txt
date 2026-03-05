set(LIB_DIR "$ENV{HOME}/Documents/Arduino/libraries/WebSockets/src")

idf_component_register(
    SRCS
        "${LIB_DIR}/WebSockets.cpp"
        "${LIB_DIR}/WebSocketsServer.cpp"
        "${LIB_DIR}/WebSocketsClient.cpp"
        "${LIB_DIR}/SocketIOclient.cpp"
        "${LIB_DIR}/libb64/cdecode.c"
        "${LIB_DIR}/libb64/cencode.c"
        "${LIB_DIR}/libsha1/libsha1.c"
    INCLUDE_DIRS
        "${LIB_DIR}"
        "${LIB_DIR}/libb64"
        "${LIB_DIR}/libsha1"
    REQUIRES
        arduino-esp32
)
