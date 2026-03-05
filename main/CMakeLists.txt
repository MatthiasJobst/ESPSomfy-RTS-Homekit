idf_component_register(
    SRCS
        "main.cpp"
        "../ConfigFile.cpp"
        "../ConfigSettings.cpp"
        "../GitOTA.cpp"
        "../MQTT.cpp"
        "../SomfyNetwork.cpp"
        "../Sockets.cpp"
        "../Somfy.cpp"
        "../SSDP.cpp"
        "../Utils.cpp"
        "../WResp.cpp"
        "../Web.cpp"
    INCLUDE_DIRS
        "."
        ".."
    REQUIRES
        arduino-esp32
        PubSubClient
        WebSockets
        ArduinoJson
        CC1101
)

# Suppress deprecation warnings for ArduinoJson v6→v7 API changes.
# These are deferred to a later migration step; the deprecated APIs are
# still fully functional in v7.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-deprecated-declarations)
