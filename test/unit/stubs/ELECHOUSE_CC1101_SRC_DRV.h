#pragma once
#include <cstdint>

// cc1101_init_ok controls getCC1101() return value. Default true so apply() succeeds.
extern bool cc1101_init_ok;

class ELECHOUSE_CC1101 {
  public:
    void Init() {}
    void SetTx() {}
    void SetRx() {}
    void setSidle() {}
    void SendData(uint8_t *, uint8_t) {}
    float getRssi() { return -70.0f; }
    uint8_t getLqi() { return 0; }
    bool getCC1101() { return cc1101_init_ok; }

    void setSpiPin(uint8_t, uint8_t, uint8_t, uint8_t) {}
    void setGDO(uint8_t, uint8_t) {}
    void setGDO0(uint8_t) {}
    void setCCMode(uint8_t) {}
    void setMHZ(float) {}
    void setRxBW(float) {}
    void setDeviation(float) {}
    void setPA(int8_t) {}
    void setModulation(uint8_t) {}
    void setManchester(uint8_t) {}
    void setPktFormat(uint8_t) {}
    void setDcFilterOff(uint8_t) {}
    void setCrc(uint8_t) {}
    void setCRC_AF(uint8_t) {}
    void setSyncMode(uint8_t) {}
    void setAdrChk(uint8_t) {}
};

extern ELECHOUSE_CC1101 ELECHOUSE_cc1101;
