#pragma once
// WebResponder.h — Facade over JsonResponse for HTTP handler responses.
//
// Absorbs the two boilerplate patterns that pepper every Web*.cpp handler:
//
//   1. JSON body plumbing — beginResponse / beginObject|beginArray /
//      <stream the body> / endObject|endArray / endResponse.
//   2. Status messages    — server.send(code, JSON, {"status":..,"desc":..}).
//
// A WebResponder binds a request server to the shared response buffer. Body
// responses use an RAII JsonBody: object()/array() open the bracket in the
// constructor and close it (plus the response) in the destructor, so a handler
// just writes the body and lets the JsonBody fall off the stack.
//
//   void handleGroup(WebServer &server) {
//       WebJsonResponder json(server);
//       SomfyGroup *g = somfy.groupController.getGroupById(id);
//       if (!g) return json.respondJson().error("Group Id not found.");
//       auto o = json.respondJson().object();  // begins object + response
//       g->toJSON(o);                          // JsonBody -> JsonResponse&
//   }                                          // dtor ends object + response
//
// Handlers don't construct a WebResponder directly: WebJsonResponder owns one
// (with the shared buffer) and exposes it via respondJson().

#include <utility>
#include <WebServer.h>
#include "WResp.h"

class WebResponder {
  public:
    /**
     * @brief Bind a responder to a request server and the response buffer.
     *
     * @param server The server the request arrived on.
     * @param buff Scratch buffer the JSON body is streamed through.
     * @param buffSize Size of @p buff in bytes.
     */
    WebResponder(WebServer &server, char *buff, size_t buffSize) : _server(server), _buff(buff), _buffSize(buffSize) {}

    /**
     * @brief RAII handle for a streamed JSON object or array response body.
     *
     * The constructor begins the response and opens the bracket; the destructor
     * closes the bracket and ends the response. Convert to JsonResponse& (or
     * call json()) to stream the body in between. Non-copyable and non-movable;
     * returned by value relying on C++17 guaranteed copy elision.
     */
    class JsonBody {
      public:
        ~JsonBody();

        JsonBody(const JsonBody &) = delete;
        JsonBody &operator=(const JsonBody &) = delete;
        JsonBody(JsonBody &&) = delete;
        JsonBody &operator=(JsonBody &&) = delete;

        /**
         * @brief Append a named or bare value to the current container.
         *
         * Forwards to every JsonFormatter::addElem overload (numbers, bools,
         * strings, with or without a key) so handlers never touch the underlying
         * response type.
         */
        template <typename... Args> void addElem(Args &&...args) { _resp.addElem(std::forward<Args>(args)...); }

        /** @brief Open a nested object, optionally keyed. Balance with endObject(). */
        void beginObject(const char *name = nullptr) { _resp.beginObject(name); }

        /** @brief Close the nested object opened by beginObject(). */
        void endObject() { _resp.endObject(); }

        /** @brief Open a nested array, optionally keyed. Balance with endArray(). */
        void beginArray(const char *name = nullptr) { _resp.beginArray(name); }

        /** @brief Close the nested array opened by beginArray(). */
        void endArray() { _resp.endArray(); }

        /** @brief Implicit access to the underlying response for toJSON()-style calls. */
        operator JsonResponse &() { return _resp; }

        /** @brief Explicit escape hatch to the underlying response. */
        JsonResponse &json() { return _resp; }

      private:
        friend class WebResponder;
        enum class Kind : std::uint8_t { Object, Array };
        JsonBody(WebServer &server, char *buff, size_t buffSize, Kind kind);

        JsonResponse _resp;
        Kind _kind;
    };

    /** @brief Begin a `{ … }` object response. Stream the body, then let the handle expire. */
    JsonBody object() { return JsonBody(_server, _buff, _buffSize, JsonBody::Kind::Object); }

    /** @brief Begin a `[ … ]` array response. Stream the body, then let the handle expire. */
    JsonBody array() { return JsonBody(_server, _buff, _buffSize, JsonBody::Kind::Array); }

    /**
     * @brief Send a `{"status":..,"desc":..}` status message.
     *
     * @param code HTTP status code.
     * @param status Status token (e.g. "ERROR", "OK", "SUCCESS").
     * @param desc Human-readable description. Must be a plain string with no
     *             characters needing JSON escaping (matches existing handlers).
     */
    void status(int code, const char *status, const char *desc);

    /** @brief Send an ERROR status (default HTTP 500). */
    void error(const char *desc, int code = 500) { status(code, "ERROR", desc); }

    /** @brief Send a SUCCESS status (default HTTP 200). */
    void success(const char *desc, int code = 200) { status(code, "SUCCESS", desc); }

    /** @brief Send an OK status (HTTP 200). */
    void ok(const char *desc) { status(200, "OK", desc); }

    /** @brief Send the standard "Invalid Http method" ERROR (HTTP 500). */
    void invalidMethod() { error("Invalid Http method"); }

    /** @brief Send the standard 404 plain-text response. */
    void notFound();

  private:
    WebServer &_server;
    char *_buff;
    size_t _buffSize;
};
