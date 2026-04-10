//SomfyFlag.h - Handles sun and wind and other flags
#pragma once
#include <cstdint>

using byte = std::uint8_t;

class SomfyFlag {
  protected:
    byte flags = 0;
    enum class somfy_flags_t : byte {
      SunFlag = 0x01,
      SunSensor = 0x02,
      DemoMode = 0x04,
      Light = 0x08,
      Windy = 0x10,
      Sunny = 0x20,
      Lighted = 0x40,
      SimMy = 0x80
    };
  public:
    byte getFlags() const { return flags; }
    void setFlags(byte newFlags) { flags = newFlags; }
    byte getRollingCode() const { return (flags >> 4) & 0x0F; }
    void resetFlags() { flags = 0; }
    void setFlag(somfy_flags_t flag, bool val) {
      if(val) {
        flags |= static_cast<byte>(flag);
      } else {
        flags &= ~static_cast<byte>(flag);
      }
    }
    bool setFlagReturnOld(somfy_flags_t flag, bool val) {
      bool old = (flags & static_cast<byte>(flag)) == 0;
      setFlag(flag, val);
      return old;
    }
    bool operator !=( const SomfyFlag& other ) const {
      return this->flags != other.flags;
    }
    SomfyFlag& operator |=( const SomfyFlag& other ) {
      this->flags |= other.flags;
      return *this;
    }
    bool hasSunFlag() const { return (this->flags & static_cast<byte>(somfy_flags_t::SunFlag)) > 0; };
    void setSunFlag(bool bSunFlag) { bSunFlag ? this->flags |= static_cast<byte>(somfy_flags_t::SunFlag) : this->flags &= ~(static_cast<byte>(somfy_flags_t::SunFlag)); };
    bool setSunFlagReturnOld(bool val) { return setFlagReturnOld(somfy_flags_t::SunFlag, val); }
    bool hasSunSensor() const { return (this->flags & static_cast<byte>(somfy_flags_t::SunSensor)) > 0; };
    void setSunSensor(bool bSunSensor) { bSunSensor ? this->flags |= static_cast<byte>(somfy_flags_t::SunSensor) : this->flags &= ~(static_cast<byte>(somfy_flags_t::SunSensor)); };
    bool isDemoMode() const { return (this->flags & static_cast<byte>(somfy_flags_t::DemoMode)) > 0; };
    void setDemoMode(bool bDemoMode) { bDemoMode ? this->flags |= static_cast<byte>(somfy_flags_t::DemoMode) : this->flags &= ~(static_cast<byte>(somfy_flags_t::DemoMode)); };
    bool setDemoFlagReturnOld(bool val) { return setFlagReturnOld(somfy_flags_t::DemoMode, val); }
    bool isSunny() const { return (this->flags & static_cast<byte>(somfy_flags_t::Sunny)) > 0; };
    void setSunny(bool bSunny) { bSunny ? this->flags |= static_cast<byte>(somfy_flags_t::Sunny) : this->flags &= ~(static_cast<byte>(somfy_flags_t::Sunny)); };
    bool setSunnyReturnOld(bool val) { return setFlagReturnOld(somfy_flags_t::Sunny, val); }
    bool isWindy() const { return (this->flags & static_cast<byte>(somfy_flags_t::Windy)) > 0; };
    void setWindy(bool bWindy) { bWindy ? this->flags |= static_cast<byte>(somfy_flags_t::Windy) : this->flags &= ~(static_cast<byte>(somfy_flags_t::Windy)); };
    bool setWindyReturnOld(bool val) { return setFlagReturnOld(somfy_flags_t::Windy, val); }
    bool hasLight() const { return (this->flags & static_cast<byte>(somfy_flags_t::Light)) > 0; };
    void setLight(bool bLight) { bLight ? this->flags |= static_cast<byte>(somfy_flags_t::Light) : this->flags &= ~(static_cast<byte>(somfy_flags_t::Light)); };
    bool simMy() const { return (this->flags & static_cast<byte>(somfy_flags_t::SimMy)) > 0; };
    void setSimMy(bool bSimMy) { bSimMy ? this->flags |= static_cast<byte>(somfy_flags_t::SimMy) : this->flags &= ~(static_cast<byte>(somfy_flags_t::SimMy)); };
// Functions to evaluate flags
    static bool isSunny(byte mask) { return (mask & static_cast<byte>(somfy_flags_t::Sunny)) > 0; }
    static bool isWindy(byte mask) { return (mask & static_cast<byte>(somfy_flags_t::Windy)) > 0; }
    static bool isDemoMode(byte mask) { return (mask & static_cast<byte>(somfy_flags_t::DemoMode)) > 0; }
};