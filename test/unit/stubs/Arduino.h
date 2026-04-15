#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>
#include <algorithm>

// itoa is non-standard; provide it for the test host.
inline char *itoa(int val, char *s, int base) {
    if      (base == 16) snprintf(s, 12, "%x", val);
    else if (base ==  2) { char *p = s; unsigned v = val; for (int i = 31; i >= 0; i--) *p++ = '0' + ((v >> i) & 1); *p = '\0'; }
    else                 snprintf(s, 12, "%d", val);
    return s;
}

using byte = uint8_t;

// Arduino digital I/O levels
#define HIGH 1
#define LOW  0
#define INPUT  0
#define OUTPUT 1

extern uint64_t test_clock_ms;
inline uint64_t millis() { return test_clock_ms; }
inline void delay(uint32_t) {}

struct EspClass {
    uint64_t getEfuseMac() const { return 0xDEADBEEF000ULL; }
};
inline EspClass ESP;

template<typename T> T min(T a, T b) { return a < b ? a : b; }
template<typename T> T max(T a, T b) { return a > b ? a : b; }

class String {
    std::string _s;
public:
    String() = default;
    String(const char *s) : _s(s ? s : "") {}
    String(int n) : _s(std::to_string(n)) {}
    String(float f) : _s(std::to_string(f)) {}
    const char *c_str() const { return _s.c_str(); }
    size_t length() const { return _s.length(); }
    bool isEmpty() const { return _s.empty(); }
    bool equals(const char *o) const { return _s == o; }
    String operator+(const String &o) const { return String((_s + o._s).c_str()); }
    String operator+(const char *s) const { return String((_s + std::string(s)).c_str()); }
    friend String operator+(const char *lhs, const String &rhs) { return String((std::string(lhs) + rhs._s).c_str()); }
    String &operator+=(const char *s) { _s += s; return *this; }
    bool operator==(const String &o) const { return _s == o._s; }
    bool operator==(const char *s) const { return _s == s; }
    bool operator!=(const char *s) const { return _s != s; }
    char operator[](size_t i) const { return _s[i]; }
    int indexOf(char c) const { auto p = _s.find(c); return p == std::string::npos ? -1 : (int)p; }
    String substring(size_t from, size_t to = std::string::npos) const {
        return String(_s.substr(from, to == std::string::npos ? std::string::npos : to - from).c_str());
    }
    void toCharArray(char *buf, size_t len) const { strncpy(buf, _s.c_str(), len - 1); buf[len - 1] = '\0'; }
    int toInt() const { try { return std::stoi(_s); } catch(...) { return 0; } }
    float toFloat() const { try { return std::stof(_s); } catch(...) { return 0.0f; } }
    String toLowerCase() const { String r; r._s = _s; for (auto &c : r._s) c = tolower(c); return r; }
    bool equalsIgnoreCase(const char *o) const {
        if (!o) return _s.empty();
        if (_s.length() != strlen(o)) return false;
        for (size_t i = 0; i < _s.length(); i++)
            if (tolower((unsigned char)_s[i]) != tolower((unsigned char)o[i])) return false;
        return true;
    }
    bool equalsIgnoreCase(const String &o) const { return equalsIgnoreCase(o._s.c_str()); }
    bool startsWith(const char *prefix) const { return _s.rfind(prefix, 0) == 0; }
    bool startsWith(const String &prefix) const { return startsWith(prefix._s.c_str()); }
    bool endsWith(const char *suffix) const {
        size_t sl = strlen(suffix);
        return _s.length() >= sl && _s.compare(_s.length() - sl, sl, suffix) == 0;
    }
    bool endsWith(const String &suffix) const { return endsWith(suffix._s.c_str()); }
    String &replace(char from, char to) { for (auto &c : _s) if (c == from) c = to; return *this; }
    void trim() { size_t s = _s.find_first_not_of(" \t\r\n"); size_t e = _s.find_last_not_of(" \t\r\n"); _s = (s == std::string::npos) ? "" : _s.substr(s, e - s + 1); }
};
