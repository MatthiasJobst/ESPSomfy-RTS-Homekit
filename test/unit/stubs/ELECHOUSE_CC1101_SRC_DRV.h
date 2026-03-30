#pragma once
#include <cstdint>

class ELECHOUSE_CC1101 {
public:
    void Init() {}
    void SetTx() {}
    void SetRx() {}
    void SendData(uint8_t *, uint8_t) {}
    float getRssi() { return -70.0f; }
    uint8_t getLqi() { return 0; }
};

extern ELECHOUSE_CC1101 ELECHOUSE_cc1101;
