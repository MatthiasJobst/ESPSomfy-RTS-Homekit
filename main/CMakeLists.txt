# CMakeLists.txt — ESP-IDF component registration for the main Somfy firmware.
# Lists every source file in main/ and declares dependencies on arduino-esp32,
# CC1101 driver, and esp-homekit-sdk components.
idf_component_register(
    SRCS
        "app_main.cpp"
        "ConfigFile.cpp"
        "ShadeConfigFile.cpp"
        "ConfigSettings.cpp"
        "GitOTA.cpp"
        "OtaService.cpp"
        "HomeKit.cpp"
        "MQTT.cpp"
        "ControllerNetwork.cpp"
        "Sockets.cpp"
        "somfy/SomfyFrame.cpp"
        "somfy/SomfyTransceiver.cpp"
        "somfy/SomfyRemote.cpp"
        "somfy/SomfyRoom.cpp"
        "somfy/SomfyGroup.cpp"
        "somfy/shade/SomfyFlagManager.cpp"
        "somfy/shade/SomfyGPIOControl.cpp"
        "somfy/shade/SomfyMQTTPublisher.cpp"
        "somfy/shade/SomfyCommandTransmitter.cpp"
        "somfy/shade/SomfyTargetSequencer.cpp"
        "somfy/shade/SomfyCommandProcessor.cpp"
        "somfy/shade/SomfyMovementTracker.cpp"
        "somfy/shade/SomfyJSONSerializer.cpp"
        "somfy/shade/SomfyShade.cpp"
        "somfy/SomfyShadeController.cpp"
        "Utils.cpp"
        "WResp.cpp"
        "web/Web.cpp"
        "web/WebResponder.cpp"
        "web/WebJsonResponder.cpp"
        "web/WebFiles.cpp"
        "web/WebSettings.cpp"
        "web/WebShades.cpp"
        "web/WebRooms.cpp"
        "web/WebGroups.cpp"
        "web/WebOTA.cpp"
        "web/WebUtils.cpp"
        "web/WebHomeKit.cpp"
        "web/WebShadesGroupsCommands.cpp"
        "web/WebAuth.cpp"
        "web/WebSystem.cpp"
    INCLUDE_DIRS
        "."
        "somfy"
        "somfy/shade"
        "web"
    REQUIRES
        arduino-esp32
        PubSubClient
        WebSockets
        ArduinoJson
        CC1101
        joltwallet__littlefs
        nvs_flash
        esp_app_format
        esp_hap_core
        esp_hap_apple_profiles
)

# Build a LittleFS image from the data/ directory and flash it automatically
# alongside the firmware image when running `idf.py flash`.
littlefs_create_partition_image(spiffs ../data FLASH_IN_PROJECT)

# Enable NVS-backed persistence for shade state
# (position, my-position, rolling codes).
target_compile_definitions(${COMPONENT_LIB} PRIVATE USE_NVS)

# Suppress deprecation warnings for ArduinoJson v6→v7 API changes.
# These are deferred to a later migration step; the deprecated APIs are
# still fully functional in v7.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-deprecated-declarations)
