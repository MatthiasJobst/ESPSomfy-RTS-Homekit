#pragma once

#include <cstdint>

using byte = std::uint8_t;

/**
 * @brief Sensor and mode flag bitfield for a Somfy shade.
 *
 * Wraps a single byte of packed boolean flags with typed accessors.
 */
class SomfyFlag {
  protected:
    byte flags = 0; /**< Packed flag byte. */

    enum class somfy_flags_t : byte {
        SunFlag = 0x01,   /**< Shade has a sun flag configured. */
        SunSensor = 0x02, /**< Shade has a sun sensor. */
        DemoMode = 0x04,  /**< Demo mode active. */
        Light = 0x08,     /**< Light sensor triggered. */
        Windy = 0x10,     /**< Wind sensor active. */
        Sunny = 0x20,     /**< Sun sensor active. */
        Lighted = 0x40,   /**< Lighted flag. */
        SimMy = 0x80      /**< Simulate My button press. */
    };

  public:
    /** @brief Return the raw flags byte. */
    byte getFlags() const { return flags; }
    /** @brief Overwrite all flags with @p newFlags. */
    void setFlags(byte newFlags) { flags = newFlags; }
    /** @brief Return the upper nibble of flags (rolling-code field). */
    byte getRollingCode() const { return (flags >> 4) & 0x0F; }
    /** @brief Clear all flags. */
    void resetFlags() { flags = 0; }

    /**
     * @brief Set or clear a single flag bit.
     *
     * @param flag Flag bit to modify.
     * @param val  True to set, false to clear.
     */
    void setFlag(somfy_flags_t flag, bool val)
    {
        if (val) {
            flags |= static_cast<byte>(flag);
        } else {
            flags &= ~static_cast<byte>(flag);
        }
    }

    /**
     * @brief Set or clear a flag bit and return its previous value.
     *
     * @param flag Flag bit to modify.
     * @param val  True to set, false to clear.
     * @return Previous state of the flag.
     */
    bool setFlagReturnOld(somfy_flags_t flag, bool val)
    {
        bool old = (flags & static_cast<byte>(flag)) != 0;
        setFlag(flag, val);
        return old;
    }

    /** @brief Return true if this flag differs from @p other. */
    bool operator!=(const SomfyFlag &other) const { return this->flags != other.flags; }

    /**
     * @brief OR all bits from @p other into this flag.
     *
     * @param other Source flags to merge in.
     * @return Reference to this object.
     */
    SomfyFlag &operator|=(const SomfyFlag &other)
    {
        this->flags |= other.flags;
        return *this;
    }

    /** @brief Return true if the SunFlag bit is set. */
    bool hasSunFlag() const { return (this->flags & static_cast<byte>(somfy_flags_t::SunFlag)) > 0; }
    /** @brief Set or clear the SunFlag bit. */
    void setSunFlag(bool bSunFlag) { setFlag(somfy_flags_t::SunFlag, bSunFlag); }
    /** @brief Set the SunFlag bit and return its previous value. */
    bool setSunFlagReturnOld(bool bSunFlag) { return setFlagReturnOld(somfy_flags_t::SunFlag, bSunFlag); }

    /** @brief Return true if the SunSensor bit is set. */
    bool hasSunSensor() const { return (this->flags & static_cast<byte>(somfy_flags_t::SunSensor)) > 0; }
    /** @brief Set or clear the SunSensor bit. */
    void setSunSensor(bool bSunSensor) { setFlag(somfy_flags_t::SunSensor, bSunSensor); }

    /** @brief Return true if demo mode is active. */
    bool isDemoMode() const { return (this->flags & static_cast<byte>(somfy_flags_t::DemoMode)) > 0; }
    /** @brief Set or clear demo mode. */
    void setDemoMode(bool bDemoMode) { setFlag(somfy_flags_t::DemoMode, bDemoMode); }
    /** @brief Set demo mode and return its previous value. */
    bool setDemoFlagReturnOld(bool bDemoMode) { return setFlagReturnOld(somfy_flags_t::DemoMode, bDemoMode); }

    /** @brief Return true if the Sunny bit is set. */
    bool isSunny() const { return (this->flags & static_cast<byte>(somfy_flags_t::Sunny)) > 0; }
    /** @brief Set or clear the Sunny bit. */
    void setSunny(bool bSunny) { setFlag(somfy_flags_t::Sunny, bSunny); }
    /** @brief Set the Sunny bit and return its previous value. */
    bool setSunnyReturnOld(bool bSunny) { return setFlagReturnOld(somfy_flags_t::Sunny, bSunny); }

    /** @brief Return true if the Windy bit is set. */
    bool isWindy() const { return (this->flags & static_cast<byte>(somfy_flags_t::Windy)) > 0; }
    /** @brief Set or clear the Windy bit. */
    void setWindy(bool bWindy) { setFlag(somfy_flags_t::Windy, bWindy); }
    /** @brief Set the Windy bit and return its previous value. */
    bool setWindyReturnOld(bool bWindy) { return setFlagReturnOld(somfy_flags_t::Windy, bWindy); }

    /** @brief Return true if the Light bit is set. */
    bool hasLight() const { return (this->flags & static_cast<byte>(somfy_flags_t::Light)) > 0; }
    /** @brief Set or clear the Light bit. */
    void setLight(bool bLight) { setFlag(somfy_flags_t::Light, bLight); }

    /** @brief Return true if the SimMy bit is set. */
    bool simMy() const { return (this->flags & static_cast<byte>(somfy_flags_t::SimMy)) > 0; }
    /** @brief Set or clear the SimMy bit. */
    void setSimMy(bool bSimMy) { setFlag(somfy_flags_t::SimMy, bSimMy); }

    /** @brief Return true if @p mask has the Sunny bit set. */
    static bool isSunny(byte mask) { return (mask & static_cast<byte>(somfy_flags_t::Sunny)) > 0; }
    /** @brief Return true if @p mask has the Windy bit set. */
    static bool isWindy(byte mask) { return (mask & static_cast<byte>(somfy_flags_t::Windy)) > 0; }
    /** @brief Return true if @p mask has the DemoMode bit set. */
    static bool isDemoMode(byte mask) { return (mask & static_cast<byte>(somfy_flags_t::DemoMode)) > 0; }
};
