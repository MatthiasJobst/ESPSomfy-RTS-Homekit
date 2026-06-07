#pragma once
// WebHandler.h — Abstract interface for domain-specific web handler modules.
//
// Each concrete handler (shades, groups, rooms, settings, OTA, …) owns a slice
// of the routing table. begin()/end() form the lifecycle; registerHandler() and
// registerApiHandler() are thin forwarders onto the public HTTP server (port 80)
// and the REST API server (port 8081) supplied at construction.
//
// WebHandler is a pure interface; the web layer (Web) owns the set of handlers
// and drives their begin()/end().

#include <functional>
#include <WebServer.h>

class WebHandler {
  public:
    /** @brief Route callback type, matching WebServer::THandlerFunction. */
    using Handler = std::function<void(void)>;

    /**
     * @brief Bind the handler to the public and API servers.
     *
     * @param server The public HTTP server (port 80).
     * @param apiServer The REST API server (port 8081).
     */
    WebHandler(WebServer &server, WebServer &apiServer) : server(server), apiServer(apiServer) {}
    virtual ~WebHandler() = default;

    /**
     * @brief Register every route owned by this handler.
     *
     * @note Called once during web layer startup, before the servers begin
     *       accepting clients.
     */
    virtual void begin() = 0;

    /**
     * @brief Release any resources owned by this handler.
     */
    virtual void end() = 0;

  protected:
    /**
     * @brief Register a route on the public HTTP server.
     *
     * @param uri The request path (e.g. "/shades").
     * @param fn The callback invoked when the route is requested.
     */
    void registerHandler(const Uri &uri, Handler fn) { server.on(uri, std::move(fn)); }

    /**
     * @brief Register a method-specific route on the public HTTP server.
     *
     * @param uri The request path (e.g. "/restore").
     * @param method The HTTP method to match (HTTP_GET, HTTP_POST, …).
     * @param fn The callback invoked when the route is requested.
     */
    void registerHandler(const Uri &uri, HTTPMethod method, Handler fn) { server.on(uri, method, std::move(fn)); }

    /**
     * @brief Register a route with a file-upload callback on the public HTTP server.
     *
     * @param uri The request path (e.g. "/updateFirmware").
     * @param method The HTTP method to match (HTTP_GET, HTTP_POST, …).
     * @param fn The callback invoked once the request completes.
     * @param ufn The callback invoked for each upload chunk.
     */
    void registerHandler(const Uri &uri, HTTPMethod method, Handler fn, Handler ufn)
    {
        server.on(uri, method, std::move(fn), std::move(ufn));
    }

    /**
     * @brief Register a route on the REST API server.
     *
     * @param uri The request path (e.g. "/shades").
     * @param fn The callback invoked when the route is requested.
     */
    void registerApiHandler(const Uri &uri, Handler fn) { apiServer.on(uri, std::move(fn)); }

    /**
     * @brief Register a method-specific route on the REST API server.
     *
     * @param uri The request path (e.g. "/shade").
     * @param method The HTTP method to match (HTTP_GET, HTTP_POST, …).
     * @param fn The callback invoked when the route is requested.
     */
    void registerApiHandler(const Uri &uri, HTTPMethod method, Handler fn) { apiServer.on(uri, method, std::move(fn)); }

    // Common content-type and status strings.
    static constexpr const char *ENCODING_TEXT = "text/plain";       /**< Content-Type: text/plain. */
    static constexpr const char *ENCODING_HTML = "text/html";        /**< Content-Type: text/html. */
    static constexpr const char *ENCODING_JSON = "application/json"; /**< Content-Type: application/json. */
    static constexpr const char *RESPONSE_404 = "404: Service Not Found"; /**< Default 404 body. */

    WebServer &server;    /**< Public HTTP server (port 80). */
    WebServer &apiServer; /**< REST API server (port 8081). */
};
