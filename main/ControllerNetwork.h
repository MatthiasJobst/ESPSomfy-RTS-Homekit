/*
 * ControllerNetwork
 * -----------------
 * This class manages network connectivity for the SomfyController project, handling both Ethernet and WiFi connections.
 * It provides logic for connection management, failover between interfaces, SoftAP setup, and network status reporting.
 * The class also integrates with mDNS and other network services to support device discovery and communication.
 */
#ifndef CONTROLLERNETWORK_H
#define CONTROLLERNETWORK_H

#include <Arduino.h>
#include <WiFi.h>           // WiFiEvent_t
#include "ConfigSettings.h" // conn_types_t

#define CONNECT_TIMEOUT 20000
#define SSID_SCAN_INTERVAL 60000
class ControllerNetwork {
  protected:
    unsigned long lastEmit = 0;
    unsigned long lastMDNS = 0;
    int lastRSSI = 0;
    int lastChannel = 0;
    int linkSpeed = 0;
    bool _connecting = false;

  public:
    unsigned long lastWifiScan = 0;
    bool ethStarted = false;
    bool wifiFallback = false;
    bool softAPOpened = false;
    bool openingSoftAP = false;
    bool needsBroadcast = true;
    conn_types_t connType = conn_types_t::unset;
    conn_types_t connTarget = conn_types_t::unset;
    bool connected();
    bool connecting();
    void clearConnecting();
    conn_types_t preferredConnType();
    String ssid;
    String mac;
    int channel;
    int strength;
    int disconnected = 0;
    int connectAttempts = 0;
    uint32_t disconnectTime = 0;
    uint32_t connectStart = 0;
    uint32_t connectTime = 0;
    bool openSoftAP();
    bool connect(conn_types_t ctype);
    bool connectWiFi(const uint8_t *bssid = nullptr, const int32_t channel = -1);
    bool connectWired();
    void setConnected(conn_types_t connType);
    bool getStrongestAP(const char *ssid, uint8_t *bssid, int32_t *channel);
    bool changeAP(const uint8_t *bssid, const int32_t channel);
    void updateHostname();

    /**
     * @brief Apply posted network settings (connType / wifi / ethernet) and save.
     *
     * Encapsulates the reboot policy: a connType change always needs a reboot; a
     * wifi ssid/passphrase change needs one only when no SoftAP client is
     * connected; an ethernet change needs one when ethernet is the active type.
     *
     * @param obj The posted network settings object.
     * @return true if the change requires a reboot to take effect.
     */
    bool applyNetworkConfig(JsonObject &obj);

    /** @brief Result of applyWifiCredentials(). */
    enum class WifiApply:std::uint8_t { NotFound, Applied };

    /**
     * @brief Validate and apply a Wi-Fi SSID/passphrase.
     *
     * @param ssid The SSID to connect to.
     * @param passphrase The passphrase.
     * @param needsReboot Set to true if the credentials changed (reboot to apply).
     * @return Applied, or NotFound if the SSID is non-empty and not in range.
     */
    WifiApply applyWifiCredentials(const char *ssid, const char *passphrase, bool &needsReboot);

    bool setup();
    void loop();
    void end();
    void emitSockets();
    void emitSockets(uint8_t num);
    void emitHeap(uint8_t num = 255);
    uint32_t getChipId();
    static void networkEvent(WiFiEvent_t event);
};

#endif