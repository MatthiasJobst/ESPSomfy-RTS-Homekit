#pragma once
// WebJsonResponder.h — Per-request JSON facade for web handlers.
//
// Owns the JSON concerns of a single HTTP request in both directions, so a
// handler never touches the raw response buffer, JsonResponse, or
// deserialization errors:
//   - Inbound:  parseBody() deserializes the request body (server.arg("plain"))
//               into a JsonObject/JsonArray, sending a 500 error on failure.
//   - Outbound: object()/array() open an RAII JSON body; error()/success()/…
//               send {"status":..,"desc":..} status messages.
//
// Construct one per handler from the request server — it is deliberately NOT
// part of the WebHandler routing interface:
//
//   void handleGroup(WebServer &server) {
//       WebJsonResponder json(server);
//       JsonObject obj;
//       if (!json.parseBody(obj)) return;        // 500 already sent on failure
//       ...
//       auto o = json.respondJson().object();    // begins object + response
//       group->toJSON(o);                        // JsonBody -> JsonResponse&
//   }                                            // o's dtor ends the response
//
// The inbound side owns a JsonDocument, so a parsed JsonObject/JsonArray view
// stays valid for as long as the responder is in scope (the whole handler).
// respondJson() exposes the WebResponder (the RAII JSON writer) for the
// outbound side: .object()/.array()/.error()/.success()/.ok()/… .

#include <WebServer.h>
#include <ArduinoJson.h>
#include "WebResponder.h"

class WebJsonResponder {
  public:
    /** @brief Bind a responder to a request server. */
    explicit WebJsonResponder(WebServer &server)
        : _server(server), _resp(server, s_buff, sizeof(s_buff))
    {
    }

    /**
     * @brief Deserialize the request body into a JsonObject.
     *
     * On parse failure a 500 JSON error is sent and false returned. The view is
     * valid while this responder is in scope.
     *
     * @param obj Set to the parsed object on success.
     * @return true on success, false if deserialization fails.
     */
    bool parseBody(JsonObject &obj);

    /**
     * @brief Deserialize the request body into a JsonArray.
     *
     * Array counterpart of the JsonObject overload (e.g. sort-order payloads).
     *
     * @param arr Set to the parsed array on success.
     * @return true on success, false if deserialization fails.
     */
    bool parseBody(JsonArray &arr);

    /**
     * @brief Deserialize a named request arg into a JsonObject.
     *
     * Generalisation of parseBody() (which is parseArg("plain", obj)) for the
     * rare handler that reads its JSON from a different arg (e.g. "data").
     *
     * @param argName The request arg to deserialize.
     * @param obj Set to the parsed object on success.
     * @return true on success, false if deserialization fails.
     */
    bool parseArg(const char *argName, JsonObject &obj);

    /**
     * @brief The outbound JSON writer for this request.
     *
     * Use it to build responses: respondJson().object()/array() open an RAII
     * JSON body; respondJson().error()/success()/ok()/invalidMethod()/notFound()
     * send status messages.
     *
     * @return The WebResponder bound to this request and the shared buffer.
     */
    WebResponder &respondJson() { return _resp; }

  private:
    /** @brief Send a 500 JSON error describing a failed deserialization. */
    void sendDeserializationError(DeserializationError &err);

    WebServer &_server;
    WebResponder _resp;
    JsonDocument _doc;

    /**
     * @brief Shared scratch buffer for building HTTP responses.
     *
     * @note Safe only because the Arduino WebServer is synchronous — handlers
     *       run sequentially on one task, never concurrently.
     */
    static constexpr size_t MAX_RESPONSE = 4096;
    inline static char s_buff[MAX_RESPONSE];
};
