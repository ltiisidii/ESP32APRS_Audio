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
    strlcpy(c.tlm0_comment, "tlm note", sizeof(c.tlm0_comment));
    c.obj[2].enable = true;
    strlcpy(c.obj[2].name, "147.030xx", sizeof(c.obj[2].name));
    c.obj[2].lat = -41.1440f;
    c.obj[2].lon = -71.3717f;
    strlcpy(c.obj[2].symbol, "/r", sizeof(c.obj[2].symbol));
    strlcpy(c.obj[2].comment, "T088 +060 Cerro Otto", sizeof(c.obj[2].comment));
    c.obj[2].interval = 20;
    c.obj[2].path = 3;
    c.obj[2].rf = true;
    c.obj[2].inet = false;
    c.obj[2].expire = 48;
    c.obj[2].start = 1791600000;
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
    CHECK_EQ_STR(b.tlm0_comment, "tlm note"); // system telemetry comment too
    // APRS objects: every field of every slot survives a save/load
    CHECK(b.obj[2].enable);
    CHECK_EQ_STR(b.obj[2].name, "147.030xx");
    CHECK(b.obj[2].lat > -41.1441f && b.obj[2].lat < -41.1439f);
    CHECK(b.obj[2].lon > -71.3718f && b.obj[2].lon < -71.3716f);
    CHECK_EQ_STR(b.obj[2].symbol, "/r");
    CHECK_EQ_STR(b.obj[2].comment, "T088 +060 Cerro Otto");
    CHECK_EQ_INT(b.obj[2].interval, 20);
    CHECK_EQ_INT(b.obj[2].path, 3);
    CHECK(b.obj[2].rf && !b.obj[2].inet);
    CHECK_EQ_INT(b.obj[2].expire, 48);
    CHECK(b.obj[2].start == 1791600000u);
    CHECK(!b.obj[0].enable);
}

TEST(config_old_file_has_objects_off)
{
    // a configuration saved before objects existed: all off, usual defaults for when one is turned on
    Configuration b;
    memset(&b, 0, sizeof(b));
    JsonDocument doc;
    deserializeJson(doc, "{\"aprsMycall\":\"LU1ABC\"}");
    configFromJson(doc, b);
    for (int i = 0; i < OBJ_COUNT; i++)
    {
        CHECK(!b.obj[i].enable);
        CHECK_EQ_INT(b.obj[i].interval, 30);
        CHECK_EQ_INT(b.obj[i].path, 2); // WIDE2-1
        CHECK_EQ_STR(b.obj[i].symbol, "/r");
    }
}

TEST(config_api_patch_one_object)
{
    // the Objects page sends whole arrays; only the edited slot may change
    Configuration c = sample();
    JsonDocument patch;
    deserializeJson(patch, "{\"objName\":[\"EVENT\",\"\",\"147.030xx\",\"\",\"\"],\"objEn\":[true,false,true,false,false]}");
    CHECK(configApplyPatch(c, patch) == 2);
    CHECK(c.obj[0].enable);
    CHECK_EQ_STR(c.obj[0].name, "EVENT");
    CHECK_EQ_STR(c.obj[2].name, "147.030xx");
    CHECK_EQ_STR(c.obj[2].comment, "T088 +060 Cerro Otto"); // untouched fields kept
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
