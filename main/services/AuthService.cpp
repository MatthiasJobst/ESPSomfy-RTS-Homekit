// AuthService.cpp — HMAC token generation + credential validation.

#include "AuthService.h"

#include <cstring>
#include <esp_log.h>
#include <mbedtls/md.h>
#include "ConfigSettings.h"

extern ConfigSettings settings;

static const char *s_TAG = "AuthService";

String AuthService::token(const String &payload) const
{
    byte hmacResult[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char *)settings.serverId, strlen(settings.serverId));
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)payload.c_str(), payload.length());
    mbedtls_md_hmac_finish(&ctx, hmacResult);
    mbedtls_md_free(&ctx);

    String out;
    out.reserve(sizeof(hmacResult) * 2);
    for (size_t i = 0; i < sizeof(hmacResult); i++) {
        char hex[3];
        sprintf(hex, "%02x", (int)hmacResult[i]);
        out += hex;
    }
    return out;
}

String AuthService::tokenForClient(const IPAddress &ip) const
{
    switch (settings.Security.type) {
    case security_types::Password:
        return token(String(settings.Security.username) + ":" + String(settings.Security.password) + ":" +
                     ip.toString());
    case security_types::PinEntry:
        return token(String(settings.Security.pin) + ":" + ip.toString());
    default:
        return token(ip.toString());
    }
}

AuthService::LoginResult AuthService::login(const IPAddress &ip, const char *username, const char *password,
                                           const char *pin) const
{
    LoginResult result;
    String apiKey = tokenForClient(ip);
    switch (settings.Security.type) {
    case security_types::None:
        result.success = true;
        result.msg = "Success";
        result.apiKey = apiKey;
        break;
    case security_types::PinEntry:
        ESP_LOGI(s_TAG, "Validating pin");
        if (strlen(pin) == 0 || strcmp(pin, settings.Security.pin) != 0) {
            result.msg = "Invalid Pin Entry";
        } else {
            result.success = true;
            result.msg = "Login successful";
            result.apiKey = apiKey;
        }
        break;
    case security_types::Password:
        ESP_LOGI(s_TAG, "Validating username and password");
        if (strlen(username) == 0 || strlen(password) == 0 || strcmp(username, settings.Security.username) != 0 ||
            strcmp(password, settings.Security.password) != 0) {
            result.msg = "Invalid username or password";
        } else {
            result.success = true;
            result.msg = "Login successful";
            result.apiKey = apiKey;
        }
        break;
    }
    return result;
}
