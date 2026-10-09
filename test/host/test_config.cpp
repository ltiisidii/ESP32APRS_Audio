// Tests of src/config.cpp (PR3): atomic save with backup, recovery after power cuts, missing keys.
#include <Arduino.h>
#include <LITTLEFS.h>
#include "test.h"
#include "config.h"

// ---- what config.cpp links against
HostFsState &hostFs()
{
    static HostFsState s;
    return s;
}
fs::LITTLEFSFS LITTLEFS;
volatile int8_t adcEn = 0;
volatile int8_t dacEn = 0;

static const char *CFG = "/default.cfg";
static const char *BAK = "/default.cfg.bak";

static Configuration makeConfig(const char *call, const char *host)
{
    Configuration c;
    memset(&c, 0, sizeof(c));
    strlcpy(c.aprs_mycall, call, sizeof(c.aprs_mycall));
    strlcpy(c.aprs_host, host, sizeof(c.aprs_host));
    strlcpy(c.ntp_host, "pool.ntp.org", sizeof(c.ntp_host));
    strlcpy(c.wifi_sta[0].wifi_ssid, "MiRed", sizeof(c.wifi_sta[0].wifi_ssid));
    strlcpy(c.wifi_sta[0].wifi_pass, "clave1234", sizeof(c.wifi_sta[0].wifi_pass));
    c.wifi_sta[0].enable = true;
    c.aprs_ssid = 10;
    c.aprs_port = 14580;
    c.timeZone = -3;
    c.igate_lat = -34.6037;
    c.igate_lon = -58.3816;
    return c;
}

static void checkSame(const Configuration &a, const Configuration &b)
{
    CHECK_EQ_STR(a.aprs_mycall, b.aprs_mycall);
    CHECK_EQ_STR(a.aprs_host, b.aprs_host);
    CHECK_EQ_STR(a.wifi_sta[0].wifi_ssid, b.wifi_sta[0].wifi_ssid);
    CHECK_EQ_STR(a.wifi_sta[0].wifi_pass, b.wifi_sta[0].wifi_pass);
    CHECK_EQ_INT(a.aprs_ssid, b.aprs_ssid);
    CHECK_EQ_INT(a.aprs_port, b.aprs_port);
    CHECK(a.timeZone == b.timeZone);
    CHECK(std::fabs(a.igate_lat - b.igate_lat) < 1e-4);
}

TEST(config_save_load_roundtrip)
{
    hostFsReset();
    Configuration a = makeConfig("LU1ABC", "rotate.aprs2.net");
    CHECK(saveConfiguration(CFG, a));
    Configuration b;
    memset(&b, 0, sizeof(b));
    CHECK(loadConfiguration(CFG, b));
    checkSame(a, b);
}

TEST(config_second_save_keeps_backup)
{
    hostFsReset();
    Configuration a = makeConfig("LU1AAA", "rotate.aprs2.net");
    Configuration b = makeConfig("LU2BBB", "soam.aprs2.net");
    CHECK(saveConfiguration(CFG, a));
    CHECK(saveConfiguration(CFG, b));
    Configuration r;
    CHECK(loadConfiguration(CFG, r));
    CHECK_EQ_STR(r.aprs_mycall, "LU2BBB");
    CHECK(loadConfiguration(BAK, r));
    CHECK_EQ_STR(r.aprs_mycall, "LU1AAA");
}

// Power cut at every byte position while writing: the station must come back with either the
// old or the new configuration, never with factory defaults.
TEST(config_power_cut_while_writing)
{
    Configuration a = makeConfig("LU1OLD", "rotate.aprs2.net");
    Configuration b = makeConfig("LU2NEW", "soam.aprs2.net");
    hostFsReset();
    CHECK(saveConfiguration(CFG, a));
    size_t full = hostFs().files[CFG]->size();
    int bad = 0;
    for (long cut = 0; cut <= (long)full + 1; cut += 37)
    {
        hostFsReset();
        saveConfiguration(CFG, a); // known good state (file + backup)
        saveConfiguration(CFG, a);
        hostFsCutAfterBytes(cut);
        saveConfiguration(CFG, b); // interrupted (or not, for the last iterations)
        hostFsPowerOn();
        Configuration r;
        memset(&r, 0, sizeof(r));
        bool ok = loadConfigurationWithBackup(CFG, r);
        if (!ok || (strcmp(r.aprs_mycall, "LU1OLD") != 0 && strcmp(r.aprs_mycall, "LU2NEW") != 0))
        {
            bad++;
            std::printf("    power cut after %ld bytes -> lost configuration\n", cut);
        }
    }
    CHECK_EQ_INT(bad, 0);
}

// Power cut between "cfg -> bak" and "tmp -> cfg": main file missing, backup has the old config
TEST(config_power_cut_between_renames)
{
    Configuration a = makeConfig("LU1OLD", "rotate.aprs2.net");
    Configuration b = makeConfig("LU2NEW", "soam.aprs2.net");
    hostFsReset();
    CHECK(saveConfiguration(CFG, a));
    hostFsCutAfterRenames(1);
    CHECK(!saveConfiguration(CFG, b));
    hostFsPowerOn();
    CHECK(!LITTLEFS.exists(CFG));
    Configuration r;
    CHECK(loadConfigurationWithBackup(CFG, r));
    CHECK_EQ_STR(r.aprs_mycall, "LU1OLD");
    CHECK(LITTLEFS.exists(CFG)); // main file restored from the backup
}

// Corrupt main file: load from backup, restore the main file, and keep the good backup
TEST(config_corrupt_main_file_uses_backup)
{
    Configuration a = makeConfig("LU1OLD", "rotate.aprs2.net");
    Configuration b = makeConfig("LU2NEW", "soam.aprs2.net");
    hostFsReset();
    CHECK(saveConfiguration(CFG, a));
    CHECK(saveConfiguration(CFG, b)); // bak = a, cfg = b
    *hostFs().files[CFG] = "{\"cpuFreq\":160,\"aprsMy"; // truncated JSON
    Configuration r;
    CHECK(loadConfigurationWithBackup(CFG, r));
    CHECK_EQ_STR(r.aprs_mycall, "LU1OLD");
    Configuration bak;
    CHECK(loadConfiguration(BAK, bak));
    CHECK_EQ_STR(bak.aprs_mycall, "LU1OLD"); // backup not overwritten by the corrupt file
    CHECK(loadConfiguration(CFG, r));
    CHECK_EQ_STR(r.aprs_mycall, "LU1OLD");
}

// Old or hand-edited files may lack keys: loading must not crash (strlcpy of NULL) and must
// fall back to defaults. "{}" exercises every key's fallback at once.
TEST(config_load_empty_json_does_not_crash)
{
    hostFsReset();
    LITTLEFS.open(CFG, FILE_WRITE).write((const uint8_t *)"{}", 2);
    Configuration r;
    memset(&r, 0x55, sizeof(r));
    CHECK(loadConfiguration(CFG, r));
    CHECK_EQ_STR(r.ntp_host, "pool.ntp.org");
}

TEST(config_missing_files)
{
    hostFsReset();
    Configuration r;
    CHECK(!loadConfiguration(CFG, r));
    CHECK(!loadConfigurationWithBackup(CFG, r));
}
