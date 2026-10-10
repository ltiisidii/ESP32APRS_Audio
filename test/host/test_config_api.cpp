// Web API helpers in config.cpp: JSON round trip, secret masking, partial updates.
#include <Arduino.h>
#include "test.h"
#include "config.h"

static Configuration sample()
{
    Configuration c;
    memset(&c, 0, sizeof(c));
    strlcpy(c.aprs_mycall, "LU1ABC", sizeof(c.aprs_mycall));
    strlcpy(c.aprs_host, "rotate.aprs2.net", sizeof(c.aprs_host));
    strlcpy(c.http_password, "secretpw", sizeof(c.http_password));
    strlcpy(c.wifi_ap_pass, "appass123", sizeof(c.wifi_ap_pass));
    strlcpy(c.wifi_sta[0].wifi_ssid, "Home", sizeof(c.wifi_sta[0].wifi_ssid));
    strlcpy(c.wifi_sta[0].wifi_pass, "homepass", sizeof(c.wifi_sta[0].wifi_pass));
    c.wifi_sta[0].enable = true;
    strlcpy(c.msg_key, "00112233445566778899AABBCCDDEEFF", sizeof(c.msg_key));
    c.aprs_port = 14580;
    c.timeZone = -3;
    c.igate_en = false;
    c.igate_timestamp = true;
    return c;
}

TEST(config_json_roundtrip)
{
    Configuration a = sample(), b;
    memset(&b, 0, sizeof(b));
    JsonDocument doc;
    configToJson(a, doc);
    configFromJson(doc, b);
    CHECK_EQ_STR(b.aprs_mycall, "LU1ABC");
    CHECK_EQ_STR(b.wifi_sta[0].wifi_pass, "homepass");
    CHECK_EQ_INT(b.aprs_port, 14580);
    CHECK(b.timeZone == -3);
    CHECK(b.igate_timestamp); // iGate time stamp switch survives a save/load
}

TEST(config_api_masks_every_secret)
{
    Configuration a = sample();
    JsonDocument doc;
    configToJson(a, doc);
    configMaskSecrets(doc);
    std::string out;
    serializeJson(doc, out);
    CHECK(out.find("secretpw") == std::string::npos);
    CHECK(out.find("appass123") == std::string::npos);
    CHECK(out.find("homepass") == std::string::npos);
    CHECK(out.find("00112233445566778899") == std::string::npos);
    CHECK(out.find("LU1ABC") != std::string::npos); // non-secret data still there
    CHECK(out.find("Home") != std::string::npos);
}

TEST(config_api_patch_changes_fields_keeps_masked_secrets)
{
    Configuration c = sample();
    JsonDocument patch;
    deserializeJson(patch, R"({"igateMycall":"LU9XYZ","igatePort":10152,"httpPass":"********",)"
                           R"("WiFiSTA":[true,"Home","********",true,"Second","newpass2"]})");
    int n = configApplyPatch(c, patch);
    CHECK(n >= 3);
    CHECK_EQ_STR(c.aprs_mycall, "LU9XYZ");
    CHECK_EQ_INT(c.aprs_port, 10152);
    CHECK_EQ_STR(c.http_password, "secretpw");          // masked -> unchanged
    CHECK_EQ_STR(c.wifi_sta[0].wifi_pass, "homepass");   // masked -> unchanged
    CHECK_EQ_STR(c.wifi_sta[1].wifi_ssid, "Second");
    CHECK_EQ_STR(c.wifi_sta[1].wifi_pass, "newpass2");
}

TEST(config_api_patch_ignores_unknown_and_wrong_types)
{
    Configuration c = sample();
    JsonDocument patch;
    deserializeJson(patch, R"({"noSuchKey":1,"igatePort":"not a number","igateMycall":42,"igateEn":"yes"})");
    configApplyPatch(c, patch);
    CHECK_EQ_INT(c.aprs_port, 14580);
    CHECK_EQ_STR(c.aprs_mycall, "LU1ABC");
    CHECK(!c.igate_en);
}

TEST(config_api_patch_rejects_non_object)
{
    Configuration c = sample();
    JsonDocument patch;
    deserializeJson(patch, "[1,2,3]");
    CHECK_EQ_INT(configApplyPatch(c, patch), -1);
}
