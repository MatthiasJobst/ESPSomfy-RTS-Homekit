// Minimal stub — replaces main/WResp.h for unit tests.
#pragma once
#include <cstdint>
#include <cstddef>

class WebServer;  // forward decl

class JsonFormatter {
protected:
    char *buff = nullptr;
    size_t buffSize = 0;
public:
    void beginObject(const char * = nullptr) {}
    void endObject() {}
    void beginArray(const char * = nullptr) {}
    void endArray() {}
    void appendElem(const char * = nullptr) {}
    void addElem(const char *)              {}
    void addElem(float)                     {}
    void addElem(int8_t)                    {}
    void addElem(uint8_t)                   {}
    void addElem(int32_t)                   {}
    void addElem(uint32_t)                  {}
    void addElem(bool)                      {}
    void addElem(const char *, float)       {}
    void addElem(const char *, int8_t)      {}
    void addElem(const char *, uint8_t)     {}
    void addElem(const char *, int32_t)     {}
    void addElem(const char *, uint32_t)    {}
    void addElem(const char *, bool)        {}
    void addElem(const char *, const char *) {}
    void escapeString(const char *, char *) {}
    uint32_t calcEscapedLength(const char *) { return 0; }
};

class JsonResponse : public JsonFormatter {
public:
    WebServer *server = nullptr;
    void beginResponse(WebServer *, char *, size_t) {}
    void endResponse() {}
};

class JsonSockEvent : public JsonFormatter {
    char _buf[512] = {};
public:
    JsonSockEvent() { buff = _buf; buffSize = sizeof(_buf); }
    void begin(const char *) {}
    void end(uint8_t = 255) {}
    void endRoom(uint8_t) {}
    const char *getBuffer() const { return _buf; }
};
