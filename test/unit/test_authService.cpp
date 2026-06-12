// test_authService.cpp — tests for AuthService (HMAC token generation +
// credential validation), the security logic extracted out of the web layer.
//
// Covered:
//   token()           — HMAC-SHA256 keyed by settings.serverId (format,
//                        determinism, key/payload sensitivity, known vector)
//   tokenForClient()  — payload construction per security type
//   login()           — None / PinEntry / Password validation, all arms

#include "AuthService.h"
#include "ConfigSettings.h"
#include <gtest/gtest.h>

extern ConfigSettings settings;

namespace {

void setServerId(const char *id)
{
    strncpy(settings.serverId, id, sizeof(settings.serverId) - 1);
    settings.serverId[sizeof(settings.serverId) - 1] = '\0';
}

void setCredentials(security_types type, const char *user, const char *pass, const char *pin)
{
    settings.Security.type = type;
    strncpy(settings.Security.username, user, sizeof(settings.Security.username) - 1);
    strncpy(settings.Security.password, pass, sizeof(settings.Security.password) - 1);
    strncpy(settings.Security.pin, pin, sizeof(settings.Security.pin) - 1);
}

class AuthServiceTest : public ::testing::Test {
  protected:
    AuthService auth;
    IPAddress ip{1, 2, 3, 4};

    void SetUp() override
    {
        setServerId("secret");
        setCredentials(security_types::None, "", "", "");
    }
};

// ── token() ───────────────────────────────────────────────────────────────────

TEST_F(AuthServiceTest, Token_Returns64LowercaseHexChars)
{
    String t = auth.token("anything");
    EXPECT_EQ(t.length(), 64u);
    for (size_t i = 0; i < t.length(); ++i) {
        char c = t[i];
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
            << "non-lowercase-hex char at " << i << ": '" << c << "'";
    }
}

TEST_F(AuthServiceTest, Token_MatchesKnownHmacSha256Vector)
{
    // HMAC-SHA256(key="secret", msg="1.2.3.4") — confirms the digest is a real,
    // standards-compliant HMAC and not just any deterministic hash.
    EXPECT_EQ(auth.token("1.2.3.4"), "40c586f5d87dd34c97e0331962b709a6cb9ece888b5b57b6051e29b44f8cad98");
}

TEST_F(AuthServiceTest, Token_IsDeterministic)
{
    EXPECT_EQ(auth.token("payload"), auth.token("payload"));
}

TEST_F(AuthServiceTest, Token_DifferentPayload_ProducesDifferentDigest)
{
    EXPECT_NE(auth.token("payloadA"), auth.token("payloadB"));
}

TEST_F(AuthServiceTest, Token_DifferentServerIdKey_ProducesDifferentDigest)
{
    String a = auth.token("payload");
    setServerId("other-key");
    String b = auth.token("payload");
    EXPECT_NE(a, b);
}

// ── tokenForClient() — payload construction per security type ──────────────────

TEST_F(AuthServiceTest, TokenForClient_None_KeysOnIpOnly)
{
    setCredentials(security_types::None, "", "", "");
    EXPECT_EQ(auth.tokenForClient(ip), auth.token("1.2.3.4"));
}

TEST_F(AuthServiceTest, TokenForClient_Password_KeysOnUserPassIp)
{
    setCredentials(security_types::Password, "user", "pass", "");
    EXPECT_EQ(auth.tokenForClient(ip), auth.token("user:pass:1.2.3.4"));
    EXPECT_EQ(auth.tokenForClient(ip), "5cf290068b8c6f7df89edb4723949a7693b1b086e720020da863b706ee04b34f");
}

TEST_F(AuthServiceTest, TokenForClient_PinEntry_KeysOnPinIp)
{
    setCredentials(security_types::PinEntry, "", "", "1234");
    EXPECT_EQ(auth.tokenForClient(ip), auth.token("1234:1.2.3.4"));
}

TEST_F(AuthServiceTest, TokenForClient_IsIpSpecific)
{
    setCredentials(security_types::None, "", "", "");
    IPAddress other{10, 0, 0, 1};
    EXPECT_NE(auth.tokenForClient(ip), auth.tokenForClient(other));
}

// ── login() — None ────────────────────────────────────────────────────────────

TEST_F(AuthServiceTest, Login_None_AlwaysSucceedsAndIssuesToken)
{
    setCredentials(security_types::None, "", "", "");
    auto r = auth.login(ip, "", "", "");
    EXPECT_TRUE(r.success);
    EXPECT_STREQ(r.msg, "Success");
    EXPECT_EQ(r.apiKey, auth.tokenForClient(ip));
}

// ── login() — PinEntry ────────────────────────────────────────────────────────

TEST_F(AuthServiceTest, Login_PinEntry_CorrectPin_Succeeds)
{
    setCredentials(security_types::PinEntry, "", "", "1234");
    auto r = auth.login(ip, "", "", "1234");
    EXPECT_TRUE(r.success);
    EXPECT_STREQ(r.msg, "Login successful");
    EXPECT_EQ(r.apiKey, auth.tokenForClient(ip));
}

TEST_F(AuthServiceTest, Login_PinEntry_WrongPin_Fails)
{
    setCredentials(security_types::PinEntry, "", "", "1234");
    auto r = auth.login(ip, "", "", "9999");
    EXPECT_FALSE(r.success);
    EXPECT_STREQ(r.msg, "Invalid Pin Entry");
    EXPECT_TRUE(r.apiKey.isEmpty());
}

TEST_F(AuthServiceTest, Login_PinEntry_EmptyPin_Fails)
{
    setCredentials(security_types::PinEntry, "", "", "1234");
    auto r = auth.login(ip, "", "", "");
    EXPECT_FALSE(r.success);
    EXPECT_STREQ(r.msg, "Invalid Pin Entry");
    EXPECT_TRUE(r.apiKey.isEmpty());
}

// ── login() — Password ────────────────────────────────────────────────────────

TEST_F(AuthServiceTest, Login_Password_CorrectCredentials_Succeeds)
{
    setCredentials(security_types::Password, "admin", "secretpw", "");
    auto r = auth.login(ip, "admin", "secretpw", "");
    EXPECT_TRUE(r.success);
    EXPECT_STREQ(r.msg, "Login successful");
    EXPECT_EQ(r.apiKey, auth.tokenForClient(ip));
}

TEST_F(AuthServiceTest, Login_Password_WrongPassword_Fails)
{
    setCredentials(security_types::Password, "admin", "secretpw", "");
    auto r = auth.login(ip, "admin", "wrong", "");
    EXPECT_FALSE(r.success);
    EXPECT_STREQ(r.msg, "Invalid username or password");
    EXPECT_TRUE(r.apiKey.isEmpty());
}

TEST_F(AuthServiceTest, Login_Password_WrongUsername_Fails)
{
    setCredentials(security_types::Password, "admin", "secretpw", "");
    auto r = auth.login(ip, "root", "secretpw", "");
    EXPECT_FALSE(r.success);
    EXPECT_STREQ(r.msg, "Invalid username or password");
    EXPECT_TRUE(r.apiKey.isEmpty());
}

TEST_F(AuthServiceTest, Login_Password_EmptyUsername_Fails)
{
    setCredentials(security_types::Password, "admin", "secretpw", "");
    auto r = auth.login(ip, "", "secretpw", "");
    EXPECT_FALSE(r.success);
    EXPECT_STREQ(r.msg, "Invalid username or password");
}

TEST_F(AuthServiceTest, Login_Password_EmptyPassword_Fails)
{
    setCredentials(security_types::Password, "admin", "secretpw", "");
    auto r = auth.login(ip, "admin", "", "");
    EXPECT_FALSE(r.success);
    EXPECT_STREQ(r.msg, "Invalid username or password");
}

} // namespace
