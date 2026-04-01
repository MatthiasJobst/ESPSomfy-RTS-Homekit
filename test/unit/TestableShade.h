#pragma once
#include "../../main/somfy/SomfyShade.h"
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

    uint64_t getSunStart()    const { return sunStart; }
    uint64_t getNoSunStart()  const { return noSunStart; }
    uint64_t getWindStart()   const { return windStart; }
    uint64_t getNoWindStart() const { return noWindStart; }
    bool     getSunDone()     const { return sunDone; }
    bool     getNoSunDone()   const { return noSunDone; }
    bool     getWindDone()    const { return windDone; }
    bool     getNoWindDone()  const { return noWindDone; }
    uint32_t getLastFrameAwait() const { return lastFrame.await; }

    // ── State writes (test setup helpers) ───────────────────────────────────
    void setFlags(uint8_t f)        { flags = f; }
    void setWindLast(uint64_t t)    { windLast = t; }
    void setDirection(int8_t d)     { direction = d; }
    void setSunDone(bool v)         { sunDone = v; }
    void setNoSunDone(bool v)       { noSunDone = v; }
    void setWindDone(bool v)        { windDone = v; }
    void setNoWindDone(bool v)      { noWindDone = v; }
    void setSunStart(uint64_t t)    { sunStart = t; }
    void setNoSunStart(uint64_t t)  { noSunStart = t; }
    void setWindStart(uint64_t t)   { windStart = t; }
    void setNoWindStart(uint64_t t) { noWindStart = t; }
    void setMoveStart(uint64_t t)   { moveStart = t; }
    void setTiltStart(uint64_t t)   { tiltStart = t; }
    void setStartPos(float v)       { startPos = v; }
    void setStartTiltPos(float v)   { startTiltPos = v; }
    void setSettingPos(bool v)      { settingPos = v; }
    void setSettingTiltPos(bool v)  { settingTiltPos = v; }
    void setSettingMyPos(bool v)    { settingMyPos = v; }

    // ── Additional state reads ───────────────────────────────────────────────
    int8_t   getLastMovement()  const { return lastMovement; }
    int8_t   getDirection()     const { return direction; }
    int8_t   getTiltDirection() const { return tiltDirection; }
    uint64_t getMoveStart()     const { return moveStart; }
    bool     getSettingMyPos()  const { return settingMyPos; }
};
