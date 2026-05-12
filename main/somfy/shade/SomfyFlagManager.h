#pragma once

#include "SomfyFlag.h"
#include "SomfyFrame.h"

#define SECS_TO_MILLIS(x) ((x) * 1000)
#define MINS_TO_MILLIS(x) SECS_TO_MILLIS((x) * 60)

#define SOMFY_SUN_TIMEOUT MINS_TO_MILLIS(static_cast<uint64_t>(2))
#define SOMFY_NO_SUN_TIMEOUT MINS_TO_MILLIS(static_cast<uint64_t>(20))
#define SOMFY_WIND_TIMEOUT SECS_TO_MILLIS(static_cast<uint64_t>(2))
#define SOMFY_NO_WIND_TIMEOUT MINS_TO_MILLIS(static_cast<uint64_t>(12))
#define SOMFY_NO_WIND_REMOTE_TIMEOUT SECS_TO_MILLIS(static_cast<uint64_t>(30))

/**
 * @brief Encapsulates the runtime sensor-flag state for a SomfyShade.
 *
 * Holds sun/wind debounce timers and done-flags. Pure data + logic —
 * no MQTT, no emitState. Callers handle side effects.
 */
class SomfyFlagManager {
  public:
    uint64_t sunStart = 0;    /**< Epoch ms when sun exposure began. */
    uint64_t noSunStart = 0;  /**< Epoch ms when sun absence began. */
    uint64_t windStart = 0;   /**< Epoch ms when wind began. */
    uint64_t noWindStart = 0; /**< Epoch ms when wind absence began. */
    uint64_t windLast = 0;    /**< Epoch ms of the last wind event. */
    bool sunDone = true;      /**< True once the sun-exposure timer has fired. */
    bool noSunDone = true;    /**< True once the no-sun timer has fired. */
    bool windDone = true;     /**< True once the wind timer has fired. */
    bool noWindDone = true;   /**< True once the no-wind timer has fired. */

    /**
     * @brief Result of a timer tick — which position targets to apply.
     */
    struct TimerTick {
        bool setSunTarget = false;   /**< Move to shade's sun position. */
        bool setNoSunTarget = false; /**< Move to 0 (+ tiltTarget if tiltonly). */
        bool setWindTarget = false;  /**< Move to 0 (+ tiltTarget if tiltonly). */
    };

    /**
     * @brief Update debounce timers when sunny/windy state transitions.
     *
     * @param wasSunny Previous sunny state.
     * @param wasWindy Previous windy state.
     * @param isSunny  Current sunny state.
     * @param isWindy  Current windy state.
     * @param curTime  Current epoch time in ms.
     * @param shadeId  Shade ID for log messages.
     */
    void updateTimers(bool wasSunny, bool wasWindy, bool isSunny, bool isWindy, uint64_t curTime,
                      uint8_t shadeId = 255);

    /**
     * @brief Fire any expired sun/wind timers and return which targets to set.
     *
     * @param flags   Current sensor flags (from SomfyRemote::flags).
     * @param curTime Current epoch time in ms.
     * @param shadeId Shade ID for log messages.
     * @return Struct indicating which targets SomfyShade should now apply.
     */
    TimerTick tickTimers(SomfyFlag flags, uint64_t curTime, uint8_t shadeId = 255);

  protected:
    /** @brief Return true if the sun-exposure timer has expired. */
    bool isSunDone(uint64_t curTime) const;
    /** @brief Return true if the no-wind timer has expired. */
    bool isNoWindDone(uint64_t curTime) const;
    /** @brief Return true if the no-sun timer has expired. */
    bool isNoSunDone(uint64_t curTime) const;
    /** @brief Return true if the wind timer has expired. */
    bool isWindDone(uint64_t curTime) const;

    /**
     * @brief Advance sun/no-sun timers and update @p result accordingly.
     *
     * @param flags   Current sensor flags.
     * @param curTime Current epoch time in ms.
     * @param shadeId Shade ID for log messages.
     * @param result  Output tick result to modify in place.
     */
    void tickSunTimers(SomfyFlag flags, uint64_t curTime, uint8_t shadeId, TimerTick &result);

    /**
     * @brief Advance the wind timer and update @p result accordingly.
     *
     * @param curTime Current epoch time in ms.
     * @param shadeId Shade ID for log messages.
     * @param result  Output tick result to modify in place.
     */
    void tickWindTimer(uint64_t curTime, uint8_t shadeId, TimerTick &result);
};
