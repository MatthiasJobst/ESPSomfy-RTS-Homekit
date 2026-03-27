set(LIB_DIR "${CMAKE_CURRENT_LIST_DIR}/src/src")

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

# The default WEBSOCKETS_TCP_TIMEOUT is 5000 ms.  It controls the per-character
# timeout inside readStringUntil('\n') during the WS HTTP-upgrade header parse.
# 200 ms is plenty for local WiFi and keeps the worst-case stall short.
target_compile_definitions(__idf_WebSockets PRIVATE "WEBSOCKETS_TCP_TIMEOUT=200")
