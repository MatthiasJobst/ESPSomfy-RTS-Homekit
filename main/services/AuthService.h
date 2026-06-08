#pragma once
// AuthService.h — Authentication & API-token logic, isolated from the web layer.
//
// Owns the security-sensitive bits that used to live in the WebAuth handler:
//   - HMAC-SHA256 API-token generation, keyed by settings.serverId.
//   - Credential validation (pin / username+password / none) for login.
//
// Stateless (reads settings.Security / settings.serverId); a single global
// instance `auth` is fine. WebAuth parses the request and serializes the
// response; the decision + crypto live here (and are now unit-testable).

#include <Arduino.h> // String, IPAddress

class AuthService {
  public:
    /**
     * @brief HMAC-SHA256 of @p payload keyed by settings.serverId, as hex.
     * @return 64-character lowercase hex digest.
     */
    String token(const String &payload) const;

    /**
     * @brief Build the API token for a client per the active security type.
     *
     * Password: "user:pass:ip"; PinEntry: "pin:ip"; None: "ip".
     *
     * @param ip The client's IP address.
     */
    String tokenForClient(const IPAddress &ip) const;

    /** @brief Outcome of a login attempt. */
    struct LoginResult {
        bool success = false;  /**< Whether the credentials were accepted. */
        const char *msg = "";  /**< Human-readable status message. */
        String apiKey;         /**< Granted token (empty when not authenticated). */
    };

    /**
     * @brief Validate credentials against the active security type and, on
     *        success (or when security is None), issue an API token.
     *
     * @param ip The client's IP address (used to key the token).
     * @param username Supplied username (Password mode).
     * @param password Supplied password (Password mode).
     * @param pin Supplied pin (PinEntry mode).
     */
    LoginResult login(const IPAddress &ip, const char *username, const char *password, const char *pin) const;
};
