#pragma once
// WebAuth.h — HTTP handler module for authentication and security settings.
//
// Concrete WebHandler owning the login and security-config endpoints. Token
// generation and credential validation live in AuthService; these handlers just
// parse the request and serialize the response. /login is served on both the
// public and API servers; the rest are public only. begin() registers the
// routes; end() is a no-op (WebServer has no per-route removal).

#include <WebServer.h>
#include "WebHandler.h"

class WebAuth : public WebHandler {
  public:
    using WebHandler::WebHandler;

    /** @brief Register every auth/security route on the public and API servers. */
    void begin() override;

    /** @brief No-op; WebServer has no per-route removal. */
    void end() override;

  private:
    /**
     * @brief Authenticate a client (pin/password/none) and return an API token.
     * @param server The server the request arrived on.
     */
    void handleLogin(WebServer &server);

    /**
     * @brief Return the security type, permissions and identity context.
     * @param server The server the request arrived on.
     */
    void handleLoginContext(WebServer &server);

    /**
     * @brief Save security settings and return a refreshed API token.
     * @param server The server the request arrived on.
     */
    void handleSaveSecurity(WebServer &server);

    /**
     * @brief Return the current security settings.
     * @param server The server the request arrived on.
     */
    void handleGetSecurity(WebServer &server);
};
