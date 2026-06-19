// SomfyRemote.h — SomfyRemote: rolling-code remote base class with address,
// flags, GPIO triggers and command dispatch. SomfyLinkedRemote: paired secondary
// remote (thin subclass).
#pragma once
#include "SomfyFrame.h"
#include "SomfyFlag.h"

class SomfyRemote {
    // These sizes for the data have been
    // confirmed.  The address is actually 24bits
    // and the rolling code is 16 bits.
  protected:
    char m_remotePrefId[11] = "";
    uint32_t m_remoteAddress = 0;

  public:
    radio_proto proto = radio_proto::RTS;
    // GPIO pin/relay state is owned by SomfyGPIOControl (see SomfyShade::gpioControl),
    // not the remote base — the former duplicate fields here were write-only dead state.
    somfy_frame_t lastFrame = {};
    bool flipCommands = false;
    uint16_t lastRollingCode = 0;
    SomfyFlag flags = SomfyFlag();
    uint8_t bitLength = 0;
    uint8_t repeats = 1;
    virtual bool isLastCommand(somfy_commands cmd);
    char *getRemotePrefId() { return m_remotePrefId; }
    virtual void toJSON(JsonResponse &json);
    virtual void setRemoteAddress(uint32_t address);
    virtual uint32_t getRemoteAddress();
    virtual uint16_t getNextRollingCode();
    virtual uint16_t setRollingCode(uint16_t code);
    bool hasSunSensor();
    bool hasLight();
    bool simMy();
    void setSunSensor(bool bHasSensor);
    void setLight(bool bHasLight);
    void setSimMy(bool bSimMy);
    virtual void sendCommand(somfy_commands cmd);
    virtual void sendCommand(somfy_commands cmd, uint8_t repeat, uint8_t stepSize = 0);
    void sendSensorCommand(int8_t isWindy, int8_t isSunny, uint8_t repeat);
    void repeatFrame(uint8_t repeat);

    /**
     * @brief Send @p cmd as a fresh command, or repeat the last frame if it
     *        matches the last command sent.
     *
     * Folds the web-handler "new vs repeat" dispatch into the domain.
     *
     * @param cmd The command to send.
     * @param repeat Number of repeats; < 0 uses this remote's default `repeats`.
     * @param stepSize Optional step size for stepped commands.
     */
    virtual void sendOrRepeat(somfy_commands cmd, int16_t repeat = -1, uint8_t stepSize = 0);
    virtual uint16_t p_lastRollingCode(uint16_t code);
    somfy_commands transformCommand(somfy_commands cmd);
    virtual void triggerGPIOs(somfy_frame_t &frame);
};
