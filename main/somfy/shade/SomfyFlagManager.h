// SomfyFlagManager.h — Encapsulates the runtime sensor-flag state for a SomfyShade:
// the flags bitfield, sun/wind debounce timers, and done-flags.
// Pure data + logic — no MQTT, no emitState. Callers handle side effects.
#pragma once
#include "SomfyFrame.h"

class SomfyFlagManager {
public:
    // Timer/debounce state only — the flags bitfield lives in SomfyRemote::flags.
    uint64_t sunStart    = 0;
    uint64_t noSunStart  = 0;
    uint64_t windStart   = 0;
    uint64_t noWindStart = 0;
    uint64_t windLast    = 0;
    bool     sunDone     = true;
    bool     noSunDone   = true;
    bool     windDone    = true;
    bool     noWindDone  = true;

    // Update debounce timers based on sunny/windy transitions.
    // Call after flag changes, passing old and new state.
    void updateTimers(bool wasSunny, bool wasWindy,
                      bool isSunny,  bool isWindy,
                      uint64_t curTime, uint8_t shadeId = 255);

    // Fire expired sun/wind timers during checkMovement.
    // flags is SomfyRemote::flags passed in by the caller.
    // Returns which targets SomfyShade should now set.
    struct TimerTick {
        bool setSunTarget   = false; // p_target → myPos/100
        bool setNoSunTarget = false; // p_target → 0  (+ tiltTarget if tiltonly)
        bool setWindTarget  = false; // p_target → 0  (+ tiltTarget if tiltonly)
    };
    TimerTick tickTimers(uint8_t flags, uint64_t curTime, uint8_t shadeId = 255);
};
