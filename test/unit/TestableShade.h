#pragma once
#include "../../main/somfy/shade/SomfyShade.h"
#include <gmock/gmock.h>

// TestableShade overrides the two virtual seams added to SomfyShade so that
// tests can verify whether emitCommand / emitState were fired and with what
// arguments, without needing MQTT, sockets, or HomeKit.
//
// All other side effects (p_target, p_tiltTarget, setMovement, publish, etc.)
// run through their real implementations against the stub globals, so state
// fields (target, tiltTarget, flags, etc.) are directly observable.
class TestableShade : public SomfyShade {
public:
    MOCK_METHOD(void, emitState,
        (const char *evt),
        (override));

    MOCK_METHOD(void, emitState,
        (uint8_t num, const char *evt),
        (override));

    MOCK_METHOD(void, emitCommand,
        (somfy_commands cmd, const char *source, uint32_t sourceAddress, const char *evt),
        (override));

    MOCK_METHOD(void, emitCommand,
        (uint8_t num, somfy_commands cmd, const char *source, uint32_t sourceAddress, const char *evt),
        (override));

    // ── State reads ─────────────────────────────────────────────────────────
    float   getTarget()     const { return target; }
    float   getTiltTarget() const { return tiltTarget; }
    uint8_t getFlags()      const { return flags; }

    bool isSunny()   const { return flags & static_cast<uint8_t>(somfy_flags_t::Sunny); }
    bool isWindy()   const { return flags & static_cast<uint8_t>(somfy_flags_t::Windy); }
    bool hasSunFlag()const { return flags & static_cast<uint8_t>(somfy_flags_t::SunFlag); }

    uint64_t getSunStart()    const { return flagManager.sunStart; }
    uint64_t getNoSunStart()  const { return flagManager.noSunStart; }
    uint64_t getWindStart()   const { return flagManager.windStart; }
    uint64_t getNoWindStart() const { return flagManager.noWindStart; }
    bool     getSunDone()     const { return flagManager.sunDone; }
    bool     getNoSunDone()   const { return flagManager.noSunDone; }
    bool     getWindDone()    const { return flagManager.windDone; }
    bool     getNoWindDone()  const { return flagManager.noWindDone; }
    uint32_t getLastFrameAwait() const { return lastFrame.await; }
    const somfy_frame_t &getLastFrame() const { return lastFrame; }
    uint8_t  getRepeats()    const { return repeats; }
    uint8_t  getBitLength()  const { return bitLength; }

    // ── State writes (test setup helpers) ───────────────────────────────────
    void setFlags(uint8_t f)        { flags = f; }
    void setWindLast(uint64_t t)    { flagManager.windLast = t; }
    void setDirection(int8_t d)     { direction = d; }
    void setSunDone(bool v)         { flagManager.sunDone = v; }
    void setNoSunDone(bool v)       { flagManager.noSunDone = v; }
    void setWindDone(bool v)        { flagManager.windDone = v; }
    void setNoWindDone(bool v)      { flagManager.noWindDone = v; }
    void setSunStart(uint64_t t)    { flagManager.sunStart = t; }
    void setNoSunStart(uint64_t t)  { flagManager.noSunStart = t; }
    void setWindStart(uint64_t t)   { flagManager.windStart = t; }
    void setNoWindStart(uint64_t t) { flagManager.noWindStart = t; }
    void setMoveStart(uint64_t t)   { moveStart = t; }
    void setTiltStart(uint64_t t)   { tiltStart = t; }
    void setStartPos(float v)       { startPos = v; }
    void setStartTiltPos(float v)   { startTiltPos = v; }
    void setSettingPos(bool v)      { motionState.settingPos = v; }
    void setSettingTiltPos(bool v)  { motionState.settingTiltPos = v; }
    void setSettingMyPos(bool v)    { motionState.settingMyPos = v; }

    // ── Additional state reads ───────────────────────────────────────────────
    int8_t   getLastMovement()  const { return lastMovement; }
    int8_t   getDirection()     const { return direction; }
    int8_t   getTiltDirection() const { return tiltDirection; }
    uint64_t getMoveStart()     const { return moveStart; }
    bool     getSettingPos()    const { return motionState.settingPos; }
    bool     getSettingTiltPos()const { return motionState.settingTiltPos; }
    bool     getSettingMyPos()  const { return motionState.settingMyPos; }

    // ── Real emitCommand (bypasses mock for MQTT side-effect tests) ─────────
    void callRealEmitCommand(somfy_commands cmd, const char *source,
                             uint32_t sourceAddress, const char *evt = "shadeCommand") {
        SomfyShade::emitCommand(255, cmd, source, sourceAddress, evt);
    }
    void callRealEmitState(const char *evt = "shadeState") {
        SomfyShade::emitState(255, evt);
    }
    // ── Delegate wrappers (cover the 1-arg / 4-arg dispatch lines) ──────────
    void callRealEmitState1(const char *evt = "shadeState") {
        SomfyShade::emitState(evt);   // hits line 497: emitState(const char*)
    }
    void callRealEmitCommand4(somfy_commands cmd, const char *source,
                              uint32_t sourceAddress, const char *evt = "shadeCommand") {
        SomfyShade::emitCommand(cmd, source, sourceAddress, evt); // hits line 500
    }

    // ── GPIO state reads ────────────────────────────────────────────────────
    int8_t   getGpioDir()       const { return gpioControl.gpioDir; }
    uint32_t getGpioRelease()   const { return gpioControl.gpioRelease; }

    // ── GPIO state writes (test setup helpers) ──────────────────────────────
    void setProto(radio_proto p)      { proto = p; }
    void setGpioUp(uint8_t v)         { gpioControl.gpioUp = v; }
    void setGpioDown(uint8_t v)       { gpioControl.gpioDown = v; }
    void setGpioMy(uint8_t v)         { gpioControl.gpioMy = v; }
    void setGpioFlags(uint8_t v)      { gpioControl.gpioFlags = v; }
    void setGpioRelease(uint32_t v)   { gpioControl.gpioRelease = v; }
};
