// SomfyJSONSerializer.h — JSON validation, deserialization, and serialization for
// SomfyShade: validateJSON, fromJSON, toJSONRef, toJSON.
#pragma once
#include "SomfyFrame.h"

class SomfyShade;

class SomfyJSONSerializer {
  public:
    SomfyShade *shade = nullptr;
    int8_t validateJSON(JsonObject &obj);
    int8_t fromJSON(JsonObject &obj);
    void toJSONRef(JsonResponse &json);
    void toJSON(JsonResponse &json);
};
