#pragma once
#include <cstdint>

struct SPISettings {
    SPISettings(uint32_t, uint8_t, uint8_t) {}
};

class SPIClass {
public:
    void begin(int, int, int, int) {}
    void beginTransaction(SPISettings) {}
    void endTransaction() {}
    uint8_t transfer(uint8_t) { return 0; }
    uint16_t transfer16(uint16_t) { return 0; }
    void end() {}
};

inline SPIClass SPI;
