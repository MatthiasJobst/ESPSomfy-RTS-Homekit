#pragma once
#include <cstdint>
#include <cstring>

class IPAddress {
    uint8_t _addr[4] = {};
public:
    IPAddress() = default;
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) { _addr[0]=a; _addr[1]=b; _addr[2]=c; _addr[3]=d; }
    uint8_t operator[](int i) const { return _addr[i]; }
    bool operator==(const IPAddress &o) const { return memcmp(_addr, o._addr, 4) == 0; }
    bool operator!=(const IPAddress &o) const { return !(*this == o); }
};
