#pragma once
// WebAuth.h — HTTP handler module for authentication and security settings.
//
// Concrete WebHandler owning the login and security-config endpoints, plus the
// private API-token (HMAC-SHA256) helpers they depend on. /login is served on
// both the public and API servers; the rest are public only. begin() registers
// the routes; end() is a no-op (WebServer has no per-route removal).

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

    /**
     * @brief Build the API token for a client from the active security type.
     * @param ipAddress The client's IP address.
     * @param token Output buffer (>= 65 bytes) for the hex token.
     * @return true always.
     */
    bool createAPIToken(const IPAddress &ipAddress, char *token);

    /**
     * @brief Compute the HMAC-SHA256 token of a payload, keyed by serverId.
     * @param payload The string to sign.
     * @param token Output buffer (>= 65 bytes) for the hex token.
     * @return true always.
     */
    bool createAPIToken(const char *payload, char *token);

    /**
     * @brief Build a pin-based token payload and sign it.
     * @param ipAddress The client's IP address.
     * @param pin The configured pin.
     * @param token Output buffer (>= 65 bytes) for the hex token.
     * @return true always.
     */
    bool createAPIPinToken(const IPAddress &ipAddress, const char *pin, char *token);

    /**
     * @brief Build a username/password token payload and sign it.
     * @param ipAddress The client's IP address.
     * @param username The configured username.
     * @param password The configured password.
     * @param token Output buffer (>= 65 bytes) for the hex token.
     * @return true always.
     */
    bool createAPIPasswordToken(const IPAddress &ipAddress, const char *username, const char *password, char *token);
};
