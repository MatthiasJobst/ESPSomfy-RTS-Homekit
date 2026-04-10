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
    void setSunFlag(bool bSunFlag) { setFlag(somfy_flags_t::SunFlag, bSunFlag); };
    bool setSunFlagReturnOld(bool bSunFlag) { return setFlagReturnOld(somfy_flags_t::SunFlag, bSunFlag); }
    bool hasSunSensor() const { return (this->flags & static_cast<byte>(somfy_flags_t::SunSensor)) > 0; };
    void setSunSensor(bool bSunSensor) { setFlag(somfy_flags_t::SunSensor, bSunSensor); };
    bool isDemoMode() const { return (this->flags & static_cast<byte>(somfy_flags_t::DemoMode)) > 0; };
    void setDemoMode(bool bDemoMode) { setFlag(somfy_flags_t::DemoMode, bDemoMode); };
    bool setDemoFlagReturnOld(bool bDemoMode) { return setFlagReturnOld(somfy_flags_t::DemoMode, bDemoMode); }
    bool isSunny() const { return (this->flags & static_cast<byte>(somfy_flags_t::Sunny)) > 0; };
    void setSunny(bool bSunny) { setFlag(somfy_flags_t::Sunny, bSunny); };
    bool setSunnyReturnOld(bool bSunny) { return setFlagReturnOld(somfy_flags_t::Sunny, bSunny); }
    bool isWindy() const { return (this->flags & static_cast<byte>(somfy_flags_t::Windy)) > 0; };
    void setWindy(bool bWindy) { setFlag(somfy_flags_t::Windy, bWindy); };
    bool setWindyReturnOld(bool bWindy) { return setFlagReturnOld(somfy_flags_t::Windy, bWindy); }
    bool hasLight() const { return (this->flags & static_cast<byte>(somfy_flags_t::Light)) > 0; };
    void setLight(bool bLight) { setFlag(somfy_flags_t::Light, bLight); };
    bool simMy() const { return (this->flags & static_cast<byte>(somfy_flags_t::SimMy)) > 0; };
    void setSimMy(bool bSimMy) { setFlag(somfy_flags_t::SimMy, bSimMy); };
// Functions to evaluate flags
    static bool isSunny(byte mask) { return (mask & static_cast<byte>(somfy_flags_t::Sunny)) > 0; }
    static bool isWindy(byte mask) { return (mask & static_cast<byte>(somfy_flags_t::Windy)) > 0; }
    static bool isDemoMode(byte mask) { return (mask & static_cast<byte>(somfy_flags_t::DemoMode)) > 0; }
};