// WebResponder.cpp — Implementation of the JsonResponse facade.

#include "WebResponder.h"

#include <cstdio>

namespace {
constexpr const char *ENCODING_TEXT = "text/plain";
constexpr const char *ENCODING_JSON = "application/json";
constexpr const char *RESPONSE_404 = "404: Service Not Found";
} // namespace

WebResponder::JsonBody::JsonBody(WebServer &server, char *buff, size_t buffSize, Kind kind) : _kind(kind)
{
    _resp.beginResponse(&server, buff, buffSize);
    if (_kind == Kind::Object)
        _resp.beginObject();
    else
        _resp.beginArray();
}

WebResponder::JsonBody::~JsonBody()
{
    if (_kind == Kind::Object)
        _resp.endObject();
    else
        _resp.endArray();
    _resp.endResponse();
}

void WebResponder::status(int code, const char *status, const char *desc)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "{\"status\":\"%s\",\"desc\":\"%s\"}", status, desc);
    _server.send(code, ENCODING_JSON, buf);
}

void WebResponder::notFound()
{
    _server.send(404, ENCODING_TEXT, RESPONSE_404);
}
