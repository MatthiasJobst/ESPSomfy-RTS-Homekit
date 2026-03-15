# CMakeLists.txt — ESP-IDF component registration for the main Somfy firmware.
# Lists every source file in main/ and declares dependencies on the arduino-esp32,
# CC1101 driver, and esp-homekit-sdk components.
idf_component_register(
    SRCS
        "main.cpp"
        "ConfigFile.cpp"
        "ConfigSettings.cpp"
        "GitOTA.cpp"
        "HomeKit.cpp"
        "MQTT.cpp"
        "somfy/SomfyNetwork.cpp"
        "Sockets.cpp"
        "somfy/SomfyFrame.cpp"
        "somfy/SomfyTransceiver.cpp"
        "somfy/SomfyRemote.cpp"
        "somfy/SomfyShade.cpp"
        "somfy/SomfyController.cpp"
        "SSDP.cpp"
        "Utils.cpp"
        "WResp.cpp"
        "web/Web.cpp"
        "web/WebSettings.cpp"
        "web/WebShades.cpp"
        "web/WebRooms.cpp"
        "web/WebGroups.cpp"
        "web/WebOTA.cpp"
    INCLUDE_DIRS
        "."
        "somfy"
        "web"
    REQUIRES
        arduino-esp32
        PubSubClient
        WebSockets
        ArduinoJson
        CC1101
        joltwallet__littlefs
        esp_hap_core
        esp_hap_apple_profiles
)

# Build a LittleFS image from the data/ directory and flash it automatically
# alongside the firmware image when running `idf.py flash`.
littlefs_create_partition_image(spiffs ../data FLASH_IN_PROJECT)

# Suppress deprecation warnings for ArduinoJson v6→v7 API changes.
# These are deferred to a later migration step; the deprecated APIs are
# still fully functional in v7.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-deprecated-declarations)
