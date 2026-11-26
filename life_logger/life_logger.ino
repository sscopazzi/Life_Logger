// SPDX-FileCopyrightText: 2026 Aspect Wave LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/*
  Life Logger  v2.3   life_logger.ino
  Sophie Scopazzi / Aspect Wave LLC

  A carry-around box: work orders, daily checklist, calendar, GPS track, house climate,
  expiring documents, kitchen timers, find-my-box and a BNWAS-style move reminder.

  Board : Adafruit ESP32-S2 Feather with w.FL (6282; LiPo + fuel gauge on board), Wi-Fi on an external
          antenna: w.FL to an RP-SMA bulkhead (5444) on the right-hand side
          + Adalogger FeatherWing (PCF8523 RTC + microSD, CS 10)   RTC, climate + GPS archive, log copy
          + Mini GPS PA1010D (Adafruit 4415) on I2C, 0x10           track + clock away from home
          + 1.5" 128x128 grayscale OLED (4741, SSD1327, STEMMA QT, I2C 0x3D)   home menu, checklist, today, notes, work orders, job timer, expiring, map, cards, join
          + piezo (1739) between pins 5 and 6                        timers, find-my-box, BNWAS
          + knob: I2C rotary encoder with push switch (5880, seesaw), STEMMA QT, 0x37 (A0 jumper closed).
            Turn: move. Its push is ACTION: a press runs a shortcut set on the dashboard, a hold opens the
            quick menu (it stays open: turn to a row, press to run it, BACK to close)
            optional: its INT pad to pin 12 (set ENC_INT_PIN) so the box only reads it when it moves
          + two lit buttons on the lid, left of the screen (558 each); the knob comes out of the right-hand side:
              BACK (lower left): pin 11 to GND, its LED ring + on A0 (pin 18)
              SELECT (upper right): pin 9 to GND, its LED ring + on A1 (pin 17); a press opens, ticks or starts what
                is highlighted; a hold (1.5 s) turns the screen off
              hold BACK + turn the knob: screen brightness · hold BACK + SELECT 3 s: a third shortcut (find my phone, …)
              their lights follow the screen's brightness; only the button used blinks; Settings can turn them off
          + I2C EEPROM breakout (24LC32 or larger, 0x50)             on/off record
          + BMP388 pressure sensor (Adafruit 3966, STEMMA QT, 0x77)   BAROGRAPH, its own pressure track (/baro).
            It sits inside the box, so the case needs a small vent (a 1 mm hole away from the electronics, or a
            vent plug); its temperature is the inside of the box and is only ever shown as that
          + haptic driver DRV2605L (Adafruit 2305, STEMMA QT, 0x5A) + Vibrating Mini Motor Disc (Adafruit 1201)
            alerts by sound, vibration or both (Settings)
          + USB power sense: the Feather's USB pin (5 V when plugged in) through 100k to A2 (pin 16), 100k from
            A2 to GND. Plugged in = on USB; no guessing from the battery voltage
  Power switch: SS12F15 micro slide switch (SPDT, 3 pins) in the top side: middle pin to EN, one end pin to
    GND, the other end pin unused. Slid toward the GND pin = off (EN held low, regulator off); the LiPo still
    charges on USB. (Terminal FeatherWing: leave its own slide switch ON.)
  Partitions: partitions.csv in this folder (two 1.5 MB app slots for .lbk updates, 896 KB flash storage) is
  used whatever the menu says. In the Arduino IDE pick Tools > Partition Scheme > "Minimal SPIFFS (1.9MB APP
  with OTA)" only so its size check allows more than 1.25 MB; the real limit is 1,572,864 bytes.

  Wi-Fi: joins the house Wi-Fi whenever it is in range (scans for it every minute or so while away)
    and runs its own hotspot (Settings can turn the hotspot off while on the house Wi-Fi; the quick menu's
    Hotspot on brings it back). Away from home, connect the phone to the hotspot.

  Storage:
    SD card: the work-order log /hwo/log.jsonl — one JSON line per change, append-only, so it is also
    the full audit trail — plus climate CSVs (/env), GPS track CSVs (/gps), attachments (/att), checklist
    archive and imported calendars.
    Internal flash (LittleFS, 896 KB, see partitions.csv): the dashboard, checklist, notes, settings, and a
    copy of the log while it fits, so the box keeps working with the card out (it carries on in flash and
    the card catches up when it's back). Once the log outgrows flash the copy is dropped and the box
    needs its card to save. Without a card, climate and GPS keep ENV_FLASH_DAYS in flash.

  House nodes: while the box is away they keep their own readings and send them in when it is back.
    That fill-in needs the SD card in the box.

  Write safety: every log line ends with its CRC-32 ("k"). At start-up each line is checked; damaged ones
    (a line cut by a power cut, a bad card sector) are moved to /hwo/log.bad, and when the card and flash
    copies differ, the one with more good lines wins. Rewritten files (settings, checklist, notes) are
    written to <name>.tmp and swapped in; start-up finishes or discards an interrupted swap.

  Low battery: below 3.30 V for 30 s and not on USB, the box finishes writing and switches itself off
    (deep sleep with no wake source, STEMMA QT power off). It stays off until the power switch is slid
    off and on again; charge it first.

  GPS: asleep (PMTK standby) while the box is on the home Wi-Fi, on elsewhere; the quick menu can set it
    on or off by hand until the box restarts. A work trip keeps it on. Sleeps and wakes go in the log.
    The last fix is kept in flash (/hwo/fix.json), so SUN & MOON works from it while the GPS sleeps or has no fix.

  SUN & MOON: sunrise, sunset, daylight, civil and nautical twilight, solar noon and how high the sun gets,
    the moon's phase, how much is lit, its rise and set and the next full or new moon, worked out on the box
    (astro.h) from the GPS fix, or the last fix it had (the screen says which). No fix ever: no times.

  BAROGRAPH: the BMP388 is read every minute; every 10 minutes the average is saved to /baro/YYYY-MM-DD.csv
    (utc,hpa,box_c,n). The screen shows the pressure, the change over the last 3 hours with its marine word
    (steady, rising slowly, ... falling very rapidly) and a graph; turn the knob for 24 h, 48 h or 7 days.
    Times the box was off stay empty on the graph.

  Dashboard: kept in flash as /www/index.html.gz, not in the firmware. Updates are one .lbk file
    (dashboard + firmware) uploaded from Settings > Firmware, or from the page the box shows when it has
    no dashboard yet (first install over USB).

  Documents: the daily checklist is one JSON file of up to 96 kB (/hwo/doc/checklist.json, flash, copied to SD)
    rewritten on each change, so typing and checking never grow the work-order log. Archived days are
    /hwo/cl/YYYY-MM-DD.json, their search index one file a month /hwo/cx/YYYY-MM.json (the dashboard
    keeps it up to date), and imported calendars /hwo/ics/<id>.json, on the SD card when there is one.
    Notes: /hwo/nt/0.json (categories) and /hwo/nt/1.json … (notes packed into files under 32 kB), in
    flash with a copy on the card; archived notes /hwo/na/1.json … and earlier versions /hwo/nh/1.json …
    on the card (flash without one). Pinned notes reach the box's screen inside /hwo/doc/oled.json.

  EEPROM: a once-a-minute "still on" heartbeat, rotated across 240 slots for wear. After a
    power-on the box knows when it was switched off and logs the off period.

  Clock: Automatic: NTP on the house Wi-Fi, else GPS, else the PCF8523, else set from the phone/laptop or by
    hand (Settings, or the quick menu's Set clock). Manual: only the RTC and what you set; GPS and NTP never change it.
  Update: while a .lbk or .bin comes in, the OLED shows the update (old --> new version, progress), whatever it was on.

  Libraries: RTClib, Adafruit MAX1704X, Adafruit LC709203F, Adafruit BusIO,
             Adafruit SSD1327, Adafruit GFX, Adafruit seesaw, Adafruit BMP3XX, Adafruit Unified Sensor,
             Adafruit DRV2605, ArduinoJson (7.x).
  Core: esp32 by Espressif 3.x
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_sntp.h>
#include <esp_system.h>
#include <esp_idf_version.h>
#include <FS.h>
#include <LittleFS.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <RTClib.h>
#include <Adafruit_MAX1704X.h>
#include <Adafruit_seesaw.h>
#include <Adafruit_LC709203F.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1327.h>
#include <Adafruit_BMP3XX.h>
#include <Adafruit_DRV2605.h>
#include <ArduinoJson.h>
#include <vector>
#include <algorithm>
#include <time.h>
#include <sys/time.h>
#include <mbedtls/sha256.h>
#include <esp_sleep.h>
#include "astro.h"
#define HUB_AP_SSID "Life Logger"         // the starting hotspot, the same on every new box
#define HUB_AP_PASS "lifelogger-setup"   // shown on the box's JOIN screen; first-boot setup makes you choose your own

#define FW_VERSION "2.3"

// ===================== USER SETTINGS =====================
const uint8_t AP_CHANNEL   = 1;             // hotspot channel when NOT on house Wi-Fi
const char* MDNS_DEFAULT   = "lifelogger";  // http://lifelogger.local until Settings names the box something else
const char* TZ_POSIX       = "UTC0";        // until first-boot setup, which starts from the phone's time zone
const uint8_t  SD_CS          = 10;         // Adalogger FeatherWing SD chip select
const uint8_t  EE_ADDR        = 0x50;       // I2C EEPROM address
const uint8_t  ENV_FLASH_DAYS = 10;         // climate days kept in flash when there is no SD card
const uint32_t OUTAGE_MIN_S   = 120;        // shorter off periods aren't logged (USB flashing, quick resets)
const uint8_t  GPS_ADDR       = 0x10;       // PA1010D on I2C
// track detail (Settings > The box): a point at most every GPS_LOG_S while moving, and only after moving
// GPS_MOVE_M; standing still, one every GPS_STILL_S, so a gap means no fix.   Fine / Normal / Coarse
const uint32_t GPS_LOG_S[3]   = { 10, 30, 120 };
const float    GPS_MOVE_M[3]  = { 5.0f, 15.0f, 50.0f };
const uint32_t GPS_STILL_S[3] = { 120, 300, 900 };
const float    GPS_MAX_HDOP   = 5.0f;       // poorer fixes are not logged
const uint32_t BATT_READ_S    = 30;
// Low battery: below BATT_OFF_V for 30 s, and not charging, the box finishes what it is writing and switches
// itself off (deep sleep with nothing set to wake it, the STEMMA QT parts unpowered). It stays off: charge it,
// then slide the power switch off and on again. The LiPo's own protection cuts at 3.0 V, but the Feather's
// 3.3 V regulator sags well before that, so the box would brown out mid-write; below about 3.3 V a LiPo has
// only a few percent left anyway.
const float    BATT_OFF_V     = 3.30f;
const int8_t   USB_PIN        = 16;         // A2: the Feather's USB pin through a 100k/100k divider (2.5 V plugged in).
                                            // -1 if it isn't wired: then being on USB is guessed from the battery voltage rising
const uint16_t USB_MV         = 1500;       // above this at the pin = plugged in
const uint32_t BARO_READ_S    = 60;         // BMP388 read
const uint32_t BARO_SAVE_S    = 600;        // one saved pressure (the average) per 10 minutes, aligned to the clock
// =========================================================

// ---------- ESP-NOW packets: keep identical in life_logger_node ----------
#define HWO_MAGIC 0x31574F48UL   // "HOW1" node -> box: live reading (v1 37 bytes, v2 45 bytes)
#define HWO_TIME  0x54574F48UL   // "HOWT" box -> node: clock, and whether it can take buffered readings
#define HWO_BFILL 0x42574F48UL   // "HOWB" node -> box: readings kept while the box was away
#define HWO_BACK  0x41574F48UL   // "HOWA" box -> node: that batch is written
typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint8_t  ver;
  char     name[12];     // null-terminated, up to 11 characters
  uint32_t seq;
  float    tempC;
  float    rh;
  float    vbat;
  float    soc;
  uint16_t intervalS;
  uint8_t  flags;        // bit0 sensor ok, bit1 battery monitor ok
} EnvPacket;
typedef struct __attribute__((packed)) {
  EnvPacket b;
  uint32_t clockS;       // node's own clock (epoch once it has been set by the box)
  uint32_t pending;      // readings waiting to be sent in
} EnvPacketV2;
typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint32_t epoch;
  uint8_t  flags;        // bit0 epoch valid, bit1 box can store buffered readings (SD card in)
} TimePacket;
typedef struct __attribute__((packed)) {
  uint32_t epoch;
  int16_t  t100;         // degC x100, INT16_MIN = no reading
  uint16_t rh100;        // %RH x100, 0xFFFF = no reading
  uint8_t  soc;          // battery %, 255 = unknown
} BfRec;
#define BF_MAX 24
typedef struct __attribute__((packed)) {
  uint32_t magic;
  char     name[12];
  uint16_t bseq;
  uint8_t  n;
  BfRec    r[BF_MAX];    // only n are sent
} BfPacket;
#define BF_HEAD 19
typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint16_t bseq;
  uint8_t  ok;
} BfAck;
// ---------------------------------------------------------------------

WebServer server(80);
RTC_PCF8523 rtc;
Adafruit_MAX17048 gaugeMax;
Adafruit_LC709203F gaugeLc;

bool rtcOK = false;
bool timeValid = false;
struct Sess { String h; uint32_t exp; bool keep; };   // a login: hash of its token; keep: exp in epoch s (0 = no end), else millis
const char* timeSrc = "none";
volatile bool ntpSynced = false;
bool staUp = false, apUp = false;
volatile bool staLost = false;
uint32_t lastScan = 0, scanEvery = 45000;
bool scanning = false;
uint32_t lastNtpSync = 0;

// ---------- storage ----------
bool flashOK = false, sdOK = false;
fs::FS* store = nullptr;                 // where the work-order log lives
const char* storeName = "none";
const char* LOG_PATH = "/hwo/log.jsonl";
// ---- types used by functions further down (Arduino puts the function prototypes near the top) ----
struct TodayEv { String t1, t2, title, label; std::vector<String> det; };   // as the dashboard sends them: times in the clock's format; det = title, place and notes, already wrapped
struct ExpRow { String date, name; };
struct TmE { String id, prj, desc, wo; bool bill = false, paused = false, del = false; int64_t start = 0, end = 0; };   // ms; end 0 = running
struct LogMark { uint32_t off, seq; bool undo; };
struct JobSt { int run = -1, pz = -1, last = -1; uint32_t todayS = 0; };
struct JobOpt { char a; int k; String t; };              // a: P pause, S stop, R resume, G go (k = jobIds index), A start the last one again
struct Dev { String n; uint32_t ms; };
enum AdK : uint8_t { AK_NONE, AK_BMP3, AK_DPS310, AK_BME280, AK_BMP280, AK_LPS22, AK_LPS22DF, AK_SHT3, AK_SHT4, AK_AHT20, AK_LSM6, AK_BNO085, AK_BH1750, AK_TSL2591, AK_PCF8574, AK_AW9523, AK_N };
struct AddOn { uint8_t k, a; bool on; };
struct GDay { String d; uint32_t own = 0, x = 0; };
void jobScanLine(const char* head, uint32_t off);   // JOB TIMER: lines of the log worth reading (time entries, undos)
void jobNote(const String& line, uint32_t off, uint32_t seq);
void jobScanDone();
void jobScanStart();
uint32_t logNextSeq = 1;
uint32_t logBytes = 0;
struct LogPass { uint32_t good, bad, maxSeq, bytes; bool torn; };   // one pass over a log (see logPass)
uint32_t lastSdRetry = 0;
uint32_t lastEnvSlot = 0;
String   lastPruneDay = "";
uint32_t docVer = 0;                       // bumps on every document write; the dashboard watches it
bool otaAuthorized = false;
// .lbk bundle: "LOGBOOK1 {...,"ui":N,"bin":M}\n" + N bytes of gzipped dashboard + M bytes of firmware
uint8_t upKind = 0;             // 0 unknown, 1 plain firmware, 2 bundle header, 3 bundle dashboard, 4 bundle firmware, 9 error
String upHead, otaErr;
size_t upUiLeft = 0, upBinSize = 0;
File upUi;
bool uiAuthorized = false;
File attFile;
String attPath = "";
const char* attWhere = "";
size_t attBytes = 0;
bool attOK = false;
String attErr = "";
size_t uiBytes = 0;
File uiFile;

// ---------- EEPROM heartbeat ----------
const uint16_t HB_BASE = 0x100, HB_SLOT = 16, HB_SLOTS = 240;   // 0x100-0xFFF, fits a 24LC32
const uint16_t HB_MAGIC = 0xB7A5;
const uint16_t HB_MAGIC_LOW = 0xB7A6;     // the last heartbeat before the box switched itself off for a low battery
typedef struct __attribute__((packed)) {
  uint32_t seq;
  uint32_t epoch;
  uint32_t upS;
  uint16_t magic;
  uint16_t crc;
} HbSlot;
bool eeOK = false;
uint32_t hbSeq = 0;
uint16_t hbNext = 0;
uint32_t lastAlive = 0;          // epoch of the newest heartbeat found at boot
uint32_t lastHbMs = 0;
bool outageHandled = false;
bool lowWoke = false;                                     // the last heartbeat before this start was a low-battery switch-off
void heartbeatNow(bool lowOff);
esp_reset_reason_t resetReason;

// ---------- sensor nodes ----------
struct Node {
  bool     used;
  char     name[12];
  uint8_t  mac[6];
  float    tempC, rh, vbat, soc;
  int8_t   rssi;
  uint32_t seq;
  uint16_t interval;
  uint8_t  flags;
  uint32_t lastMs;
  uint32_t lastEpoch;    // wall-clock time of the newest reading (0 = unknown)
  uint32_t count;
  bool     live;         // heard since this boot (false = last reading recovered from the archive)
  uint16_t bfSeq;        // last backfill batch written (resent batches are skipped)
  bool     bfAny;
  uint32_t bfRows;
};
const int MAX_NODES = 6;
Node nodes[MAX_NODES];

struct RxItem { uint8_t d[240]; uint8_t len; uint8_t mac[6]; int8_t rssi; };
RxItem rxq[8];
volatile uint8_t rxHead = 0, rxTail = 0;
portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;

// ======================================================================
//  Settings kept on the box (first-boot setup / Settings > Network). Nothing personal is compiled in: the starting
//  hotspot is public, and there is no default home Wi-Fi and no default box password.
// ======================================================================
struct Cfg { String apSsid, apPass, homeSsid, homePass, tz, passSalt, passHash; bool setupDone; int contrast; bool bnwasOn; int bnwasMin; int scrOffMin; bool clHideDone; uint8_t alert; bool btnLed;
             bool clkMan; bool apHomeOff; String mdns; uint8_t gpsDet; int tripAskMin; int envMin; String hwPins, hwAdd, ind; } cfg;   // alert: 0 sound, 1 vibrate, 2 both; btnLed: the button lights on
const char* CFG_PATH = "/hwo/config.json";
volatile bool cfgApplyAp = false, cfgApplySta = false, cfgApplyMdns = false;
uint32_t cfgApplyAt = 0;

bool mdnsOk(const String& n) {             // a network name: 1-32 of a-z 0-9 -, not starting or ending with -
  if (!n.length() || n.length() > 32 || n[0] == '-' || n[n.length() - 1] == '-') return false;
  for (size_t i = 0; i < n.length(); i++) { char c = n[i]; if (!(islower((unsigned char)c) || isdigit((unsigned char)c) || c == '-')) return false; }
  return true;
}
bool envMinOk(int m) { return m == 1 || m == 2 || m == 5 || m == 10 || m == 15; }
uint32_t envLogS() { return (uint32_t)cfg.envMin * 60; }   // climate log interval (s), aligned to the clock
void cfgDefaults() {
  cfg.apSsid = HUB_AP_SSID; cfg.apPass = HUB_AP_PASS; cfg.homeSsid = ""; cfg.homePass = ""; cfg.passSalt = ""; cfg.passHash = "";
  cfg.tz = TZ_POSIX; cfg.setupDone = false; cfg.contrast = 0x7F; cfg.bnwasOn = false; cfg.bnwasMin = 30; cfg.scrOffMin = 5; cfg.clHideDone = false; cfg.alert = 2; cfg.btnLed = true;
  cfg.clkMan = false; cfg.apHomeOff = false; cfg.mdns = MDNS_DEFAULT; cfg.gpsDet = 1; cfg.tripAskMin = 60; cfg.envMin = 5;
}
void cfgLoad() {
  cfgDefaults();
  File f = LittleFS.open(CFG_PATH, FILE_READ);
  if (!f) return;
  JsonDocument d; DeserializationError e = deserializeJson(d, f); f.close();
  if (e) return;
  cfg.apSsid = d["apSsid"] | cfg.apSsid; cfg.apPass = d["apPass"] | cfg.apPass;
  cfg.homeSsid = d["homeSsid"] | cfg.homeSsid; cfg.homePass = d["homePass"] | cfg.homePass;
  cfg.tz = d["tz"] | cfg.tz; cfg.setupDone = d["setupDone"] | false; cfg.contrast = d["contrast"] | cfg.contrast;
  cfg.bnwasOn = d["bnwasOn"] | false; cfg.bnwasMin = constrain((int)(d["bnwasMin"] | 30), 1, 240);
  cfg.scrOffMin = constrain((int)(d["scrOffMin"] | 5), 0, 240);   // 0 = never sleeps
  cfg.clHideDone = d["clHideDone"] | false;
  cfg.alert = constrain((int)(d["alert"] | 2), 0, 2);
  cfg.btnLed = d["btnLed"] | true;
  cfg.hwPins = d["hwPins"] | ""; cfg.hwAdd = d["hwAdd"] | ""; cfg.ind = d["ind"] | "";   // ind: Settings > Indicator lights (JSON)        // Settings > Pin mapping, Add-ons (JSON)
  cfg.clkMan = d["clkMan"] | false;                                   // true: GPS and internet time never change the clock
  cfg.apHomeOff = d["apHomeOff"] | false;                             // true: the hotspot is off while on the house Wi-Fi
  cfg.mdns = d["mdns"] | MDNS_DEFAULT; if (!mdnsOk(cfg.mdns)) cfg.mdns = MDNS_DEFAULT;
  cfg.gpsDet = constrain((int)(d["gpsDet"] | 1), 0, 2);
  cfg.tripAskMin = constrain((int)(d["tripAskMin"] | 60), 10, 480);
  cfg.envMin = d["envMin"] | 5; if (!envMinOk(cfg.envMin)) cfg.envMin = 5;
  cfg.passSalt = d["passSalt"] | ""; cfg.passHash = d["passHash"] | "";
}
bool cfgSave() {
  JsonDocument d;
  d["apSsid"] = cfg.apSsid; d["apPass"] = cfg.apPass; d["homeSsid"] = cfg.homeSsid; d["homePass"] = cfg.homePass;
  d["tz"] = cfg.tz; d["setupDone"] = cfg.setupDone; d["contrast"] = cfg.contrast; d["bnwasOn"] = cfg.bnwasOn; d["bnwasMin"] = cfg.bnwasMin; d["scrOffMin"] = cfg.scrOffMin; d["clHideDone"] = cfg.clHideDone; d["alert"] = cfg.alert; d["btnLed"] = cfg.btnLed;
  d["passSalt"] = cfg.passSalt; d["passHash"] = cfg.passHash;
  d["hwPins"] = cfg.hwPins; d["hwAdd"] = cfg.hwAdd; d["ind"] = cfg.ind;
  d["clkMan"] = cfg.clkMan; d["apHomeOff"] = cfg.apHomeOff; d["mdns"] = cfg.mdns; d["gpsDet"] = cfg.gpsDet; d["tripAskMin"] = cfg.tripAskMin; d["envMin"] = cfg.envMin;
  String out; serializeJson(d, out);
  String tmp = String(CFG_PATH) + ".tmp";
  File f = LittleFS.open(tmp, FILE_WRITE);
  if (!f) return false;
  bool ok = f.print(out) == out.length(); f.close();
  if (!ok) { LittleFS.remove(tmp); return false; }
  return replaceFile(LittleFS, tmp, CFG_PATH);
}


// ======================================================================
//  The box password and logins
//  The box keeps a salted SHA-256 of the password, never the password. Over the home Wi-Fi the dashboard
//  needs a login; the browser keeps a random token in a cookie, the box keeps the token's hash. "Remember
//  this device" keeps it for a year (in /hwo/sessions.json), otherwise until the box restarts or 12 h pass.
//  On the hotspot no login is asked. Before a password is set, nothing is asked anywhere.
// ======================================================================
String sha256hex(const String& in) {
  uint8_t h[32]; char o[65];
  mbedtls_sha256((const unsigned char*)in.c_str(), in.length(), h, 0);
  for (int i = 0; i < 32; i++) sprintf(o + 2 * i, "%02x", h[i]);
  return String(o);
}
String randHex(int bytes) { String o; char b[3]; for (int i = 0; i < bytes; i++) { sprintf(b, "%02x", (unsigned)(esp_random() & 0xFF)); o += b; } return o; }
bool passSet() { return cfg.passHash.length() == 64; }
void passSetTo(const String& p) { cfg.passSalt = randHex(8); cfg.passHash = sha256hex(cfg.passSalt + p); }
uint8_t pwFails = 0; uint32_t pwLockUntil = 0;
bool pwCheck(const String& p) {           // true if it's the box password (or none is set yet); five misses: 30 s pause
  if (!passSet()) return true;
  if (pwFails >= 5 && (int32_t)(millis() - pwLockUntil) < 0) return false;
  if (p.length() && sha256hex(cfg.passSalt + p) == cfg.passHash) { pwFails = 0; return true; }
  if (++pwFails >= 5) pwLockUntil = millis() + 30000;
  return false;
}
bool pwLocked() { return pwFails >= 5 && (int32_t)(millis() - pwLockUntil) < 0; }

std::vector<Sess> sess;
const char* SESS_PATH = "/hwo/sessions.json";
void sessLoad() {
  sess.clear();
  File f = LittleFS.open(SESS_PATH, FILE_READ); if (!f) return;
  JsonDocument d; DeserializationError e = deserializeJson(d, f); f.close();
  if (e) return;
  for (JsonArray x : d.as<JsonArray>()) sess.push_back({ String((const char*)(x[0] | "")), (uint32_t)(x[1] | 0UL), true });
}
void sessSave() {
  JsonDocument d; JsonArray a = d.to<JsonArray>();
  for (auto& x : sess) if (x.keep) { JsonArray r = a.add<JsonArray>(); r.add(x.h); r.add(x.exp); }
  String o; serializeJson(d, o);
  File f = LittleFS.open(SESS_PATH, FILE_WRITE); if (f) { f.print(o); f.close(); }
}
bool sessLive(const Sess& x) {
  if (x.keep) return !x.exp || !timeValid || (uint32_t)time(nullptr) < x.exp;
  return (int32_t)(x.exp - millis()) > 0;
}
String sessNew(bool keep) {
  for (size_t i = sess.size(); i-- > 0;) if (!sessLive(sess[i])) sess.erase(sess.begin() + i);
  while (sess.size() >= 24) sess.erase(sess.begin());
  String t = randHex(16);
  sess.push_back({ sha256hex(t), keep ? (timeValid ? (uint32_t)time(nullptr) + 365UL * 86400UL : 0UL) : millis() + 12UL * 3600000UL, keep });
  if (keep) sessSave();
  return t;
}
String cookieTok() {
  String c = server.header("Cookie"); int i = c.indexOf("lb_s="); if (i < 0) return "";
  String t = c.substring(i + 5); int e = t.indexOf(';'); if (e >= 0) t = t.substring(0, e); t.trim();
  return t.length() == 32 ? t : String("");
}
int sessFind() {
  String t = cookieTok(); if (!t.length()) return -1;
  String h = sha256hex(t);
  for (size_t i = 0; i < sess.size(); i++) if (sess[i].h == h) return sessLive(sess[i]) ? (int)i : -1;
  return -1;
}
int sessKept() { int n = 0; for (auto& x : sess) if (x.keep) n++; return n; }
bool viaHotspot() { return server.client().localIP() == WiFi.softAPIP(); }
bool needLogin() { return passSet() && !viaHotspot(); }
bool authed() { return !needLogin() || sessFind() >= 0; }
bool gate() { if (authed()) return true; sendErr(401, "Log in"); return false; }   // every API call over the home Wi-Fi
std::function<void()> G(std::function<void()> f) { return [f]() { if (gate()) f(); }; }
void setCookie(const String& t, bool keep) {
  server.sendHeader("Set-Cookie", "lb_s=" + t + "; Path=/; HttpOnly; SameSite=Strict" + (keep ? "; Max-Age=31536000" : ""));
}

// ======================================================================
//  Time
// ======================================================================
void timeSyncCb(struct timeval*) { ntpSynced = true; }

void setSystemUtc(time_t t) {
  struct timeval tv = { t, 0 };
  settimeofday(&tv, nullptr);
}

void writeRtc(time_t t) {
  if (!rtcOK) return;
  rtc.adjust(DateTime((uint32_t)t));
  rtc.start();
}

String isoLocal(time_t t) {
  struct tm tm; localtime_r(&t, &tm);
  char b[32]; strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%S%z", &tm);   // ...-0800
  size_t n = strlen(b);
  if (n >= 5) { memmove(b + n - 1, b + n - 2, 3); b[n - 2] = ':'; } // ...-08:00
  return String(b);
}

String isoUtc(time_t t) {
  struct tm tm; gmtime_r(&t, &tm);
  char b[24]; strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return String(b);
}

String localDate(time_t t) {
  struct tm tm; localtime_r(&t, &tm);
  char b[12]; strftime(b, sizeof b, "%Y-%m-%d", &tm);
  return String(b);
}
String trackDate(time_t t) { return localDate(t); }   // a track day is a calendar day in the box's time zone

// ======================================================================
//  Storage: flash (primary) + SD (mirror / climate archive)
// ======================================================================
size_t fileSize(fs::FS& fs, const char* path) {
  if (!fs.exists(path)) return 0;
  File f = fs.open(path, FILE_READ);
  if (!f) return 0;
  size_t n = f.size();
  f.close();
  return n;
}

// Write safety. Every file that is rewritten (settings, the checklist, notes, the log when it is cleaned,
// copies between flash and card) is written whole to <path>.tmp first, then swapped in. A power cut
// leaves either the old file or the new one, never half of one: at start-up recoverTmps() finishes a swap
// that was cut between its two steps (old file removed, new one not yet renamed) and throws away a .tmp
// that was cut while being written (the old file is still there).
bool replaceFile(fs::FS& fs, const String& tmp, const char* path) {
  fs.remove(path);                    // FAT can't rename over a file; LittleFS can, but one rule for both
  return fs.rename(tmp, path);
}
// a .tmp is only finished if it ends the way a whole file does: a JSON document with } or ], a log or CSV with a newline
bool looksWhole(fs::FS& fs, const String& p) {
  File f = fs.open(p, FILE_READ); if (!f) return false;
  size_t n = f.size(); int last = -1;
  for (size_t back = 1; back <= 4 && back <= n; back++) { f.seek(n - back); int c = f.read(); if (c != ' ' && c != '\r') { last = c; break; } }
  f.close();
  return last == '}' || last == ']' || last == '\n';
}
void recoverTmps(fs::FS& fs, const char* dir) {
  File d = fs.open(dir); if (!d) return;
  String names[24]; int n = 0;
  File e = d.openNextFile();
  while (e && n < 24) { String nm = e.name(); int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1); bool isDir = e.isDirectory(); e.close(); if (!isDir && nm.endsWith(".tmp")) names[n++] = nm; e = d.openNextFile(); }
  if (e) e.close();
  d.close();
  for (int i = 0; i < n; i++) {
    String t = String(dir) + "/" + names[i], base = t.substring(0, t.length() - 4);
    if (fs.exists(base) || !looksWhole(fs, t)) fs.remove(t);   // cut while writing the .tmp: the old file (if any) stands
    else { fs.rename(t, base); Serial.println("Finished an interrupted save: " + base); }
  }
}
void recoverAllTmps(fs::FS& fs) {
  static const char* dirs[] = { "/hwo", "/hwo/doc", "/hwo/nt", "/hwo/na", "/hwo/nh", "/hwo/cl", "/hwo/cx", "/hwo/ics", "/gps" };
  for (const char* d : dirs) recoverTmps(fs, d);
}

bool copyFile(fs::FS& from, fs::FS& to, const char* path) {
  File a = from.open(path, FILE_READ);
  if (!a) return false;
  String tmp = String(path) + ".tmp";
  File b = to.open(tmp, FILE_WRITE);
  if (!b) { a.close(); return false; }
  uint8_t buf[512];
  bool ok = true;
  while (true) {
    int n = a.read(buf, sizeof buf);
    if (n <= 0) break;
    if (b.write(buf, n) != (size_t)n) { ok = false; break; }
  }
  a.close(); b.close();
  if (!ok) { to.remove(tmp); return false; }
  return replaceFile(to, tmp, path);
}

// CRC-32 (IEEE 802.3, as in zip and PNG). Every log line ends with its checksum:
//   {"seq":12,"ts":"2026-11-20T19:22:00-08:00","e":{...},"k":"1a2b3c4d"}
// k is the CRC-32 of everything before ,"k": so the tail is always 16 characters.
uint32_t crcTab[256]; bool crcReady = false;
void crcInit() { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320UL ^ (c >> 1) : c >> 1; crcTab[i] = c; } crcReady = true; }
inline uint32_t crcUpd(uint32_t c, uint8_t x) { return crcTab[(c ^ x) & 0xFF] ^ (c >> 8); }
uint32_t crc32Of(const char* p, size_t n) { if (!crcReady) crcInit(); uint32_t c = 0xFFFFFFFFUL; while (n--) c = crcUpd(c, (uint8_t)*p++); return ~c; }
String logSeal(const String& body) {     // body = {"seq":..,"ts":..,"e":{...}   (no closing brace)
  char k[9]; snprintf(k, sizeof k, "%08lx", (unsigned long)crc32Of(body.c_str(), body.length()));
  return body + ",\"k\":\"" + k + "\"}";
}
bool tailOk(const uint8_t* t, uint32_t c) {   // t = a line's last 16 bytes, c = CRC of everything before them
  if (memcmp(t, ",\"k\":\"", 6) || t[14] != '"' || t[15] != '}') return false;
  uint32_t v = 0;
  for (int i = 6; i < 14; i++) { char h = t[i]; int d = h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : -1; if (d < 0) return false; v = (v << 4) | d; }
  return v == ~c;
}
bool lineOk(const String& L) {
  size_t n = L.length();
  if (n < 24 || !L.startsWith("{\"seq\":")) return false;
  if (!crcReady) crcInit();
  uint32_t c = 0xFFFFFFFFUL; for (size_t i = 0; i < n - 16; i++) c = crcUpd(c, (uint8_t)L[i]);
  return tailOk((const uint8_t*)L.c_str() + n - 16, c);
}
// checks a line as it streams past: the CRC runs 16 bytes behind, so at the newline the last 16 are the tail
struct LineCk {
  uint32_t c; uint8_t q[16]; uint8_t h; uint32_t len;
  void start() { if (!crcReady) crcInit(); c = 0xFFFFFFFFUL; h = 0; len = 0; }
  void put(uint8_t x) { if (len >= 16) c = crcUpd(c, q[h]); q[h] = x; h = (h + 1) & 15; len++; }
  bool ok() const { if (len < 24) return false; uint8_t t[16]; for (int i = 0; i < 16; i++) t[i] = q[(h + i) & 15]; return tailOk(t, c); }
};

// One pass over a log: counts the lines whose checksum holds and the ones that don't (torn by a power
// cut, damaged on the card), finds the highest seq, and (feed) hands good lines to the JOB TIMER scan.
LogPass logPass(fs::FS& fs, bool feed) {
  LogPass r = {0, 0, 0, 0, false};
  File f = fs.open(LOG_PATH, FILE_READ);
  if (!f) return r;
  r.bytes = f.size();
  uint8_t buf[512];
  uint32_t pos = 0, lineStart = 0;
  char head[128]; int hl = 0;                            // the start of each line: enough to see its seq, date and op
  LineCk ck; ck.start();
  while (true) {
    int n = f.read(buf, sizeof buf);
    if (n <= 0) break;
    for (int i = 0; i < n; i++) {
      uint8_t x = buf[i];
      if (x == '\n') {
        head[hl] = 0;
        if (ck.ok() && !strncmp(head, "{\"seq\":", 7)) {
          r.good++;
          uint32_t q = strtoul(head + 7, nullptr, 10); if (q > r.maxSeq) r.maxSeq = q;
          if (feed) jobScanLine(head, lineStart);
        } else if (ck.len) r.bad++;
        lineStart = pos + i + 1; hl = 0; ck.start();
      } else { if (hl < 127) head[hl++] = (char)x; ck.put(x); }
    }
    pos += n;
  }
  if (ck.len) { r.bad++; r.torn = true; }                // the last line never got its newline
  f.close();
  return r;
}

// Rewrites src into dst keeping only lines whose checksum holds. Damaged lines are not thrown away: they
// go to /hwo/log.bad on the same storage, as found. addSeal: a line with no checksum at all (an old or
// hand-made file being restored) gets one if it looks like a whole record.
const char* LOG_BAD = "/hwo/log.bad";
uint32_t logBadLines = 0;                                 // lines in log.bad, shown in Settings > Storage
bool normalizeLog(fs::FS& fs, const char* src, const String& dst, bool addSeal, uint32_t& kept, uint32_t& bad) {
  kept = bad = 0;
  File a = fs.open(src, FILE_READ); if (!a) return false;
  File b = fs.open(dst, FILE_WRITE); if (!b) { a.close(); return false; }
  File x;                                                 // log.bad, opened on the first damaged line
  String L; L.reserve(1024);
  uint8_t buf[512]; bool ok = true, over = false;
  auto flushLine = [&]() {
    if (L.length() || over) {
      String line = L;
      bool good = !over && lineOk(line);
      if (!good && !over && addSeal && line.startsWith("{\"seq\":") && line.endsWith("}") && line.indexOf(",\"k\":\"") < 0) { line = logSeal(line.substring(0, line.length() - 1)); good = true; }
      if (good) { if (b.print(line + "\n") != line.length() + 1) ok = false; else kept++; }
      else {
        bad++;
        if (!x) x = fs.open(LOG_BAD, FILE_APPEND);
        if (x) { x.print(over ? String("(a line over 64 kB)") : L); x.print('\n'); }
      }
    }
    L = ""; over = false;
  };
  while (ok) {
    int n = a.read(buf, sizeof buf);
    if (n <= 0) break;
    for (int i = 0; i < n; i++) {
      if (buf[i] == '\n') flushLine();
      else if (L.length() < 65536) L += (char)buf[i];
      else over = true;
    }
  }
  flushLine();
  a.close(); b.close(); if (x) x.close();
  if (!ok) fs.remove(dst);
  return ok;
}

uint32_t countLines(fs::FS& fs, const char* path) {
  File f = fs.open(path, FILE_READ); if (!f) return 0;
  uint8_t buf[512]; uint32_t n = 0; int k;
  while ((k = f.read(buf, sizeof buf)) > 0) for (int i = 0; i < k; i++) if (buf[i] == '\n') n++;
  f.close(); return n;
}
void countBadLines() { logBadLines = (sdOK ? countLines(SD, LOG_BAD) : 0) + (flashOK ? countLines(LittleFS, LOG_BAD) : 0); }

void refreshFlashCopy();
void scanLog() {
  logNextSeq = 1; logBytes = 0;
  if (!store) return;
  jobScanStart();
  LogPass r = logPass(*store, true);
  if (r.bad) {                                            // set the damaged lines aside and read the log again
    uint32_t kept, bad; String tmp = String(LOG_PATH) + ".tmp";
    if (normalizeLog(*store, LOG_PATH, tmp, false, kept, bad) && replaceFile(*store, tmp, LOG_PATH)) {
      Serial.printf("Log: %lu damaged line(s) moved to %s\n", (unsigned long)bad, LOG_BAD);
      jobScanStart();
      r = logPass(*store, true);
      if (store == &SD) refreshFlashCopy();
    } else if (r.torn) {                                  // couldn't rewrite: at least end the torn line so the next one starts clean
      File a = store->open(LOG_PATH, FILE_APPEND);
      if (a) { a.print('\n'); a.close(); r.bytes++; }
    }
  }
  jobScanDone();
  logBytes = r.bytes;
  logNextSeq = r.maxSeq + 1;
  countBadLines();
}

// The log lives on the SD card; flash keeps a copy while it has room (LOG_FLASH_KEEP free after it), so the
// box still works with the card out. Once the log outgrows flash, the copy is dropped and the marker
// /hwo/log.oncard says so: the box then needs its card to save.
const char* LOG_ONCARD = "/hwo/log.oncard";
const size_t LOG_FLASH_KEEP = 300 * 1024;                 // flash kept free for the dashboard, notes and updates
bool flashCopy = false;                                   // flash holds the same log as the card
size_t flashFree() { return flashOK ? LittleFS.totalBytes() - LittleFS.usedBytes() : 0; }
void dropFlashCopy() {
  if (!flashOK) return;
  LittleFS.remove(LOG_PATH);
  File m = LittleFS.open(LOG_ONCARD, FILE_WRITE); if (m) { m.print("the log is on the SD card"); m.close(); }
  flashCopy = false;
  Serial.println("Log outgrew flash: kept on the SD card only");
}
// flash takes a fresh copy of the card's log, if it fits
void refreshFlashCopy() {
  if (!flashOK || !sdOK || LittleFS.exists(LOG_ONCARD)) return;
  LittleFS.remove(LOG_PATH);
  size_t s = fileSize(SD, LOG_PATH);
  if (s && flashFree() >= s + LOG_FLASH_KEEP) { if (copyFile(SD, LittleFS, LOG_PATH)) LittleFS.remove(LOG_ONCARD); }
  else if (s) dropFlashCopy();
  flashCopy = s && fileSize(LittleFS, LOG_PATH) == s;
}
void chooseStore() {
  if (sdOK)  { store = &SD; storeName = "sd"; }
  else if (flashOK && !LittleFS.exists(LOG_ONCARD)) { store = &LittleFS; storeName = "flash"; }
  else       { store = nullptr; storeName = flashOK ? "card missing" : "none"; }
  scanLog();
}

void ensureDirs(fs::FS& fs) {
  if (!fs.exists("/hwo")) fs.mkdir("/hwo");
  if (!fs.exists("/env")) fs.mkdir("/env");
  if (!fs.exists("/hwo/doc")) fs.mkdir("/hwo/doc");
  if (!fs.exists("/hwo/cl")) fs.mkdir("/hwo/cl");
  if (!fs.exists("/hwo/cx")) fs.mkdir("/hwo/cx");
  if (!fs.exists("/hwo/ics")) fs.mkdir("/hwo/ics");
  if (!fs.exists("/hwo/nt")) fs.mkdir("/hwo/nt");
  if (!fs.exists("/hwo/na")) fs.mkdir("/hwo/na");
  if (!fs.exists("/hwo/nh")) fs.mkdir("/hwo/nh");
  if (!fs.exists("/hwo/pl")) fs.mkdir("/hwo/pl");
  if (!fs.exists("/gps")) fs.mkdir("/gps");
  if (!fs.exists("/baro")) fs.mkdir("/baro");
}

bool startFlash() {
  if (!LittleFS.begin(true)) return false;     // formats on first use
  ensureDirs(LittleFS);
  recoverAllTmps(LittleFS);
  return true;
}

void moveDirToSd(const char* dir, bool append) {
  File d = LittleFS.open(dir);
  if (!d) return;
  String names[40]; int n = 0;
  File e = d.openNextFile();
  while (e && n < 40) { String nm = e.name(); int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1); bool dirE = e.isDirectory(); e.close(); if (!dirE) names[n++] = nm; e = d.openNextFile(); }
  d.close();
  if (!SD.exists(dir)) SD.mkdir(dir);
  for (int i = 0; i < n; i++) {
    String p = String(dir) + "/" + names[i];
    bool ok;
    if (append && SD.exists(p)) {
      File a = LittleFS.open(p, FILE_READ), b = SD.open(p, FILE_APPEND);
      ok = a && b;
      if (ok) a.readStringUntil('\n');                            // skip the header line
      uint8_t buf[512];
      while (ok) { int k = a.read(buf, sizeof buf); if (k <= 0) break; if (b.write(buf, k) != (size_t)k) ok = false; }
      if (a) a.close(); if (b) b.close();
    } else ok = copyFile(LittleFS, SD, p.c_str());
    if (ok) LittleFS.remove(p);
  }
}

// file names (not paths) in one folder
int listNames(fs::FS& fs, const char* dir, String* names, int max) {
  File d = fs.open(dir); if (!d) return 0;
  int n = 0; File e = d.openNextFile();
  while (e && n < max) { String nm = e.name(); int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1); bool dirE = e.isDirectory(); e.close(); if (!dirE && nm.endsWith(".json")) names[n++] = nm; e = d.openNextFile(); }
  if (e) e.close();
  d.close();
  return n;
}
void syncNotes() {
  static String nf[48], ns[48];
  int a = listNames(LittleFS, "/hwo/nt", nf, 48);
  if (a == 0) { int b = listNames(SD, "/hwo/nt", ns, 48); for (int i = 0; i < b; i++) copyFile(SD, LittleFS, (String("/hwo/nt/") + ns[i]).c_str()); }
  else { if (!SD.exists("/hwo/nt")) SD.mkdir("/hwo/nt"); for (int i = 0; i < a; i++) copyFile(LittleFS, SD, (String("/hwo/nt/") + nf[i]).c_str()); }
}

// Card just mounted: make flash and card agree
void syncSdMirror() {
  if (!flashOK || !sdOK) return;
  // The two copies are written line for line together, so normally they match. When they don't (the card
  // was out, a fresh card, a power cut between the two writes, or one copy damaged), the copy with more
  // lines that check out is the whole one and the other takes a copy of it. Size alone can't decide:
  // a copy with rubbish on the end is longer, not better.
  size_t f = fileSize(LittleFS, LOG_PATH), s = fileSize(SD, LOG_PATH);
  bool onCard = LittleFS.exists(LOG_ONCARD);
  if (f != s && f > 0 && !onCard) {
    LogPass pf = logPass(LittleFS, false), ps = logPass(SD, false);
    if (pf.good > ps.good) { copyFile(LittleFS, SD, LOG_PATH); Serial.printf("Log: the card caught up from flash (%lu lines)\n", (unsigned long)pf.good); }
    else refreshFlashCopy();
  } else if (s > 0 && s != f) refreshFlashCopy();            // the card is ahead (or a new board): flash takes a copy if it fits
  flashCopy = fileSize(LittleFS, LOG_PATH) == fileSize(SD, LOG_PATH) && fileSize(SD, LOG_PATH) > 0;
  // the checklist, the box's screen settings and the Planner's plan: restore from the card if flash has none,
  // otherwise keep the card's copy current (it went stale if any of them changed while the card was out)
  for (const char* P : {"/hwo/doc/checklist.json", "/hwo/doc/oled.json", "/hwo/doc/plan.json"}) {
    if (!LittleFS.exists(P) && SD.exists(P)) copyFile(SD, LittleFS, P);
    else if (LittleFS.exists(P)) copyFile(LittleFS, SD, P);
  }
  syncNotes();
  // files kept in flash while there was no card: move them to the card
  moveDirToSd("/hwo/cl", false);
  moveDirToSd("/hwo/na", false);
  moveDirToSd("/hwo/nh", false);
  moveDirToSd("/hwo/ics", false);
  moveDirToSd("/hwo/pl", false);   // plans put away while the card was out
  moveDirToSd("/gps", true);       // a day's track may already be on the card: append
  moveDirToSd("/baro", true);
  moveDirToSd("/env", true);       // climate kept while the card was out: onto the card before flash prunes it after 10 days
}

bool startSD() {
  if (!SD.begin(SD_CS)) return false;
  ensureDirs(SD);
  recoverAllTmps(SD);
  return true;
}

// Append event lines (one JSON object per line). Returns how many were written.
int appendEvents(const String& body, String& tsOut, uint32_t& firstOut) {
  if (!store) return -1;
  if (store == &LittleFS && flashFree() < body.length() * 2 + 16 * 1024) return -2;   // full: refuse, don't tear
  File f = store->open(LOG_PATH, FILE_APPEND);
  if (!f) {
    if (store == &SD) { sdOK = false; chooseStore(); if (store) return appendEvents(body, tsOut, firstOut); }   // the card went: carry on in flash
    return -1;
  }
  File m;
  bool mirror = store == &SD && flashOK && flashCopy;
  if (mirror && flashFree() < body.length() * 2 + LOG_FLASH_KEEP) { dropFlashCopy(); mirror = false; }   // the log outgrew flash
  if (mirror) { m = LittleFS.open(LOG_PATH, FILE_APPEND); if (!m) { flashCopy = false; mirror = false; } }

  tsOut = isoLocal(time(nullptr));
  firstOut = logNextSeq;
  uint32_t off = f.size();
  int count = 0, start = 0; bool bad = false;
  while (start < (int)body.length()) {
    int nl = body.indexOf('\n', start);
    if (nl < 0) nl = body.length();
    String line = body.substring(start, nl);
    start = nl + 1;
    line.trim();
    line.replace("\r", " ");
    if (line.length() < 2 || line[0] != '{' || line[line.length() - 1] != '}') continue;
    String rec = logSeal("{\"seq\":" + String(logNextSeq) + ",\"ts\":\"" + tsOut + "\",\"e\":" + line) + "\n";
    if (f.print(rec) != rec.length()) { bad = true; break; }
    if (mirror) m.print(rec);
    jobNote(line, off, logNextSeq);
    off += rec.length();
    logNextSeq++;
    count++;
  }
  f.flush();
  logBytes = f.size();
  f.close();
  if (mirror) m.close();
  if (bad) { scanLog(); return -2; }                     // ends a torn line and recounts
  return count;
}

// ======================================================================
//  EEPROM heartbeat -> power-off record
// ======================================================================
uint16_t crc16(const uint8_t* d, size_t n) {
  uint16_t c = 0xFFFF;
  while (n--) { c ^= (uint16_t)(*d++) << 8; for (int i = 0; i < 8; i++) c = (c & 0x8000) ? (c << 1) ^ 0x1021 : c << 1; }
  return c;
}

bool eeRead(uint16_t a, uint8_t* b, size_t n) {
  while (n) {
    size_t c = n > 32 ? 32 : n;
    Wire.beginTransmission(EE_ADDR);
    Wire.write((uint8_t)(a >> 8)); Wire.write((uint8_t)(a & 0xFF));
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(EE_ADDR, c) != c) return false;
    for (size_t i = 0; i < c; i++) b[i] = Wire.read();
    a += c; b += c; n -= c;
  }
  return true;
}

// 16 bytes at a 16-byte-aligned address never cross an EEPROM page (32/64/128 B)
bool eeWrite16(uint16_t a, const uint8_t* b) {
  Wire.beginTransmission(EE_ADDR);
  Wire.write((uint8_t)(a >> 8)); Wire.write((uint8_t)(a & 0xFF));
  Wire.write(b, 16);
  if (Wire.endTransmission() != 0) return false;
  uint32_t t0 = millis();
  while (millis() - t0 < 20) {               // ACK polling: done when the chip answers again
    Wire.beginTransmission(EE_ADDR);
    if (Wire.endTransmission() == 0) return true;
    delay(1);
  }
  return false;
}

void eeStart() {
  Wire.beginTransmission(EE_ADDR);
  eeOK = (Wire.endTransmission() == 0);
  if (!eeOK) return;
  uint8_t buf[HB_SLOT * 16];
  int best = -1;
  for (uint16_t blk = 0; blk < HB_SLOTS; blk += 16) {
    if (!eeRead(HB_BASE + blk * HB_SLOT, buf, sizeof buf)) { eeOK = false; return; }
    for (int i = 0; i < 16; i++) {
      HbSlot s; memcpy(&s, buf + i * HB_SLOT, HB_SLOT);
      if ((s.magic != HB_MAGIC && s.magic != HB_MAGIC_LOW) || s.crc != crc16((uint8_t*)&s, 12)) continue;
      if (best < 0 || s.seq > hbSeq) { best = blk + i; hbSeq = s.seq; lastAlive = s.epoch; lowWoke = s.magic == HB_MAGIC_LOW; }
    }
  }
  hbNext = (best < 0) ? 0 : (best + 1) % HB_SLOTS;
}

void heartbeatTick() {
  if (!eeOK || !timeValid || !outageHandled) return;   // never overwrite an outage not yet logged
  if (lastHbMs && millis() - lastHbMs < 60000UL) return;
  heartbeatNow(false);
}
void heartbeatNow(bool lowOff) {
  if (!eeOK || !timeValid || !outageHandled) return;
  lastHbMs = millis();
  HbSlot s;
  s.seq = ++hbSeq; s.epoch = (uint32_t)time(nullptr); s.upS = millis() / 1000; s.magic = lowOff ? HB_MAGIC_LOW : HB_MAGIC;
  s.crc = crc16((uint8_t*)&s, 12);
  if (eeWrite16(HB_BASE + hbNext * HB_SLOT, (uint8_t*)&s)) hbNext = (hbNext + 1) % HB_SLOTS;
}

const char* resetName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:  return "power-on";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "crash";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_DEEPSLEEP:return "deep sleep";
    case ESP_RST_EXT:      return "reset pin";
    default:               return "other";
  }
}

// Runs once, the first time the clock is valid after boot
void outageCheck() {
  if (outageHandled || !timeValid) return;
  if (!store) return;                                  // wait until it can be written down
  outageHandled = true;
  if (!eeOK || lastAlive < 1735689600UL) return;       // nothing recorded yet
  uint32_t boot = (uint32_t)time(nullptr) - millis() / 1000;
  if (boot <= lastAlive || boot - lastAlive < OUTAGE_MIN_S) return;
  const char* op = nullptr;
  const char* cause = resetName(resetReason);
  if (lowWoke) { op = "power.out"; cause = "battery low"; }
  else if (resetReason == ESP_RST_POWERON || resetReason == ESP_RST_BROWNOUT) op = "power.out";
  else if (resetReason == ESP_RST_PANIC || resetReason == ESP_RST_INT_WDT ||
           resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_WDT) op = "hub.reset";
  if (!op) return;
  String ev = String("{\"op\":\"") + op + "\",\"from\":\"" + isoLocal(lastAlive) + "\",\"to\":\"" + isoLocal(boot) +
              "\",\"min\":" + String((boot - lastAlive + 30) / 60) + ",\"cause\":\"" + cause + "\"}";
  String ts; uint32_t first;
  appendEvents(ev, ts, first);
  Serial.println("Logged: " + ev);
}

// ======================================================================
//  Nodes
// ======================================================================
void cleanName(char* dst, const char* src) {
  int i = 0;
  for (; i < 11 && src[i]; i++) {
    char c = src[i];
    dst[i] = (isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_') ? c : '_';
  }
  dst[i] = 0;
  if (i == 0) strcpy(dst, "Node");
}

int findNode(const char* name, bool create) {
  int freeSlot = -1, oldest = -1;
  for (int i = 0; i < MAX_NODES; i++) {
    if (nodes[i].used && strcmp(nodes[i].name, name) == 0) return i;
    if (!nodes[i].used && freeSlot < 0) freeSlot = i;
    if (nodes[i].used && (oldest < 0 || nodes[i].lastEpoch < nodes[oldest].lastEpoch)) oldest = i;
  }
  if (!create) return -1;
  int slot = (freeSlot >= 0) ? freeSlot : oldest;
  memset(&nodes[slot], 0, sizeof(Node));
  nodes[slot].used = true;
  strncpy(nodes[slot].name, name, 11);
  return slot;
}

void updateNode(const char* name, const uint8_t* mac, float t, float h, float v, float soc,
                int8_t rssi, uint32_t seq, uint16_t interval, uint8_t flags) {
  Node& n = nodes[findNode(name, true)];
  memcpy(n.mac, mac, 6);
  n.tempC = t; n.rh = h; n.vbat = v; n.soc = soc;
  n.rssi = rssi; n.seq = seq; n.interval = interval ? interval : 60; n.flags = flags;
  n.lastMs = millis(); n.lastEpoch = timeValid ? (uint32_t)time(nullptr) : 0; n.count++; n.live = true;
}

bool nodeFresh(const Node& n) {
  uint32_t limit = max<uint32_t>(90, 3UL * n.interval) * 1000UL;
  return n.used && n.live && (millis() - n.lastMs) < limit;
}

uint32_t nodeAge(const Node& n) {
  if (n.live) return (millis() - n.lastMs) / 1000;
  uint32_t now = (uint32_t)time(nullptr);
  return (timeValid && n.lastEpoch && now > n.lastEpoch) ? now - n.lastEpoch : 0xFFFFFFFFUL;
}

#if ESP_IDF_VERSION_MAJOR >= 5
static void onEspNowRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  const uint8_t* mac = info->src_addr;
  int8_t rssi = info->rx_ctrl ? info->rx_ctrl->rssi : 0;
#else
static void onEspNowRecv(const uint8_t* mac, const uint8_t* data, int len) {
  int8_t rssi = 0;
#endif
  if (len < 4 || len > 240) return;
  uint32_t magic; memcpy(&magic, data, 4);
  if (magic != HWO_MAGIC && magic != HWO_BFILL) return;
  portENTER_CRITICAL(&rxMux);
  uint8_t next = (rxHead + 1) & 7;
  if (next != rxTail) {
    memcpy(rxq[rxHead].d, data, len);
    rxq[rxHead].len = len;
    memcpy(rxq[rxHead].mac, mac, 6);
    rxq[rxHead].rssi = rssi;
    rxHead = next;
  }
  portEXIT_CRITICAL(&rxMux);
}

// Reply to a node. Goes out on whichever interface is on the channel the node used.
void espNowSend(const uint8_t* mac, const void* d, size_t n) {
  wifi_interface_t want = staUp ? WIFI_IF_STA : WIFI_IF_AP;
  esp_now_peer_info_t p = {};
  if (esp_now_get_peer(mac, &p) == ESP_OK) {
    if (p.ifidx != want) { p.ifidx = want; p.channel = 0; esp_now_mod_peer(&p); }
  } else {
    memcpy(p.peer_addr, mac, 6); p.channel = 0; p.ifidx = want; p.encrypt = false;
    if (esp_now_add_peer(&p) != ESP_OK) return;
  }
  esp_now_send(mac, (const uint8_t*)d, n);
}

String csvNum(float v, int dec) { return isnan(v) ? String("") : String(v, dec); }
fs::FS* envFS() { return sdOK ? (fs::FS*)&SD : (flashOK ? (fs::FS*)&LittleFS : nullptr); }
const char* ENV_HEAD = "utc,local,node,temp_c,rh_pct,vbat_v,soc_pct,rssi_dbm";

// Readings a node kept while the box was away. Only with an SD card: months of them won't fit in flash.
bool storeBackfill(const char* name, const BfPacket& bp) {
  if (!sdOK || !timeValid) return false;
  String openDay; File f;
  for (int i = 0; i < bp.n; i++) {
    const BfRec& r = bp.r[i];
    if (r.epoch < 1735689600UL || r.epoch > (uint32_t)time(nullptr) + 600) continue;   // not a sane time: dropped
    String day = localDate(r.epoch);
    if (day != openDay) {
      if (f) f.close();
      String path = "/env/" + day + ".csv";
      bool isNew = !SD.exists(path);
      f = SD.open(path, FILE_APPEND);
      if (!f) { sdOK = false; return false; }
      if (isNew) f.println(ENV_HEAD);
      openDay = day;
    }
    f.print(isoUtc(r.epoch)); f.print(','); f.print(isoLocal(r.epoch)); f.print(','); f.print(name); f.print(',');
    if (r.t100 != INT16_MIN) f.print(String(r.t100 / 100.0f, 2)); f.print(',');
    if (r.rh100 != 0xFFFF) f.print(String(r.rh100 / 100.0f, 1)); f.print(",,");
    if (r.soc != 255) f.print(r.soc); f.println(',');
  }
  if (f) f.close();
  return true;
}

void processRx() {
  while (true) {
    RxItem it;
    portENTER_CRITICAL(&rxMux);
    if (rxTail == rxHead) { portEXIT_CRITICAL(&rxMux); return; }
    memcpy(&it, &rxq[rxTail], sizeof(RxItem));
    rxTail = (rxTail + 1) & 7;
    portEXIT_CRITICAL(&rxMux);

    uint32_t magic; memcpy(&magic, it.d, 4);
    if (magic == HWO_MAGIC && (it.len == sizeof(EnvPacket) || it.len == sizeof(EnvPacketV2))) {
      EnvPacket p; memcpy(&p, it.d, sizeof(EnvPacket));
      char raw[12]; memcpy(raw, p.name, 11); raw[11] = 0;
      char nm[12]; cleanName(nm, raw);
      updateNode(nm, it.mac, p.tempC, p.rh, p.vbat, p.soc, it.rssi, p.seq, p.intervalS, p.flags);
      if (it.len == sizeof(EnvPacketV2)) {               // v2 node: give it the time; say if it can send in its buffer
        TimePacket tp = { HWO_TIME, timeValid ? (uint32_t)time(nullptr) : 0, (uint8_t)((timeValid ? 1 : 0) | (sdOK && timeValid ? 2 : 0)) };
        espNowSend(it.mac, &tp, sizeof tp);
      }
    } else if (magic == HWO_BFILL && it.len >= BF_HEAD && it.len <= sizeof(BfPacket)) {
      BfPacket bp = {}; memcpy(&bp, it.d, it.len);
      if (bp.n > BF_MAX || it.len != BF_HEAD + bp.n * sizeof(BfRec)) continue;
      char raw[12]; memcpy(raw, bp.name, 11); raw[11] = 0;
      char nm[12]; cleanName(nm, raw);
      Node& n = nodes[findNode(nm, true)];
      bool ok = true;
      if (!(n.bfAny && n.bfSeq == bp.bseq)) {             // a resent batch (our ack got lost) is not written twice
        ok = storeBackfill(nm, bp);
        if (ok) { n.bfSeq = bp.bseq; n.bfAny = true; n.bfRows += bp.n; }
      }
      BfAck a = { HWO_BACK, bp.bseq, (uint8_t)(ok ? 1 : 0) };
      espNowSend(it.mac, &a, sizeof a);
    }
  }
}

// At boot the box has heard nobody yet: show each node's last archived reading, with its age
uint32_t parseUtc(const String& s) {   // 2026-11-17T16:40:00Z
  if (s.length() < 20) return 0;
  int y = s.substring(0, 4).toInt(), mo = s.substring(5, 7).toInt(), d = s.substring(8, 10).toInt();
  int h = s.substring(11, 13).toInt(), mi = s.substring(14, 16).toInt(), se = s.substring(17, 19).toInt();
  y -= mo <= 2;
  int era = y / 400, yoe = y - era * 400, doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long days = era * 146097L + yoe * 365L + yoe / 4 - yoe / 100 + doy - 719468L;
  return (uint32_t)(days * 86400L + h * 3600L + mi * 60L + se);
}

void seedNodesFrom(fs::FS& fs) {
  File d = fs.open("/env");
  if (!d) return;
  String newest; File e = d.openNextFile();
  while (e) {
    String nm = e.name(); int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1);
    e.close();
    if (nm.endsWith(".csv") && nm.length() == 14 && nm > newest) newest = nm;
    e = d.openNextFile();
  }
  d.close();
  if (!newest.length()) return;
  File f = fs.open("/env/" + newest, FILE_READ);
  if (!f) return;
  if (f.size() > 6000) f.seek(f.size() - 6000);
  String line;
  while (f.available()) {
    char c = f.read();
    if (c != '\n') { if (line.length() < 120) line += c; continue; }
    // utc,local,node,temp_c,rh_pct,vbat_v,soc_pct,rssi
    String col[8]; int k = 0, st = 0;
    for (int i = 0; i <= (int)line.length() && k < 8; i++) if (i == (int)line.length() || line[i] == ',') { col[k++] = line.substring(st, i); st = i + 1; }
    line = "";
    if (k < 5 || !col[0].endsWith("Z")) continue;
    uint32_t ep = parseUtc(col[0]);
    char nm[12]; cleanName(nm, col[2].c_str());
    int idx = findNode(nm, true);
    Node& n = nodes[idx];
    if (n.live || ep < n.lastEpoch) continue;
    n.lastEpoch = ep;
    n.tempC = col[3].length() ? col[3].toFloat() : NAN;
    n.rh = col[4].length() ? col[4].toFloat() : NAN;
    n.vbat = (k > 5 && col[5].length()) ? col[5].toFloat() : NAN;
    n.soc = (k > 6 && col[6].length()) ? col[6].toFloat() : NAN;
    n.flags = 1; n.interval = 60;
  }
  f.close();
}
void seedNodes() { if (sdOK) seedNodesFrom(SD); else if (flashOK) seedNodesFrom(LittleFS); }

// ======================================================================
//  Climate log
// ======================================================================
// Flash only: keep ENV_FLASH_DAYS of climate files
void pruneFlashDir(const char* dir);
void pruneFlashEnv(const String& today) { pruneFlashDir("/env"); pruneFlashDir("/gps"); pruneFlashDir("/baro"); }
void pruneFlashDir(const char* dir) {
  if (!flashOK) return;
  time_t cut = time(nullptr) - (time_t)ENV_FLASH_DAYS * 86400;
  String cutDay = localDate(cut);
  File d = LittleFS.open(dir);
  if (!d) return;
  String doomed[40]; int nd = 0;
  File e = d.openNextFile();
  while (e) {
    String nm = e.name();
    int sl = nm.lastIndexOf('/');
    if (sl >= 0) nm = nm.substring(sl + 1);
    e.close();
    if (nm.endsWith(".csv") && nm.substring(0, 10) < cutDay && nd < 40) doomed[nd++] = String(dir) + "/" + nm;
    e = d.openNextFile();
  }
  d.close();
  for (int i = 0; i < nd; i++) LittleFS.remove(doomed[i]);
}

void envLogTick() {
  if (!timeValid) return;
  fs::FS* fs = envFS();
  if (!fs) return;
  time_t t = time(nullptr);
  uint32_t slot = (uint32_t)t / envLogS();
  if (slot == lastEnvSlot) return;
  bool first = (lastEnvSlot == 0);
  lastEnvSlot = slot;
  if (first) return;                       // wait for the first clean boundary

  String day = localDate(t);
  if (day != lastPruneDay) { lastPruneDay = day; pruneFlashEnv(day); }

  String path = "/env/" + day + ".csv";
  bool isNew = !fs->exists(path);
  File f = fs->open(path, FILE_APPEND);
  if (!f) { if (fs == &SD) sdOK = false; return; }
  if (isNew) f.println(ENV_HEAD);
  String u = isoUtc(t), l = isoLocal(t);
  for (int i = 0; i < MAX_NODES; i++) {
    const Node& n = nodes[i];
    if (!nodeFresh(n)) continue;           // stale node = gap in the file, never filled
    f.print(u); f.print(','); f.print(l); f.print(','); f.print(n.name); f.print(',');
    f.print(csvNum(n.tempC, 2)); f.print(','); f.print(csvNum(n.rh, 1)); f.print(',');
    f.print(csvNum(n.vbat, 3)); f.print(','); f.print(csvNum(n.soc, 1)); f.print(',');
    f.println(n.rssi);
  }
  envBoxRow(f, u, l);                      // the box's own temperature and humidity (Add-ons), as a node called Box
  f.close();
}

// ======================================================================
//  Pins and add-ons (Settings > Pin mapping, Add-ons)
//  Pins: each button, light and the buzzer, and up to four extra buttons, on a Feather pin or on an I/O
//    expander (PCF8574 at the address you set; AW9523, found by its ID). A pin is written "" (none),
//    "f<GPIO>" (Feather), "p<0-7>" (PCF8574) or "a<0-15>" (AW9523: P0_0-7 = 0-7, P1_0-7 = 8-15).
//    Buttons connect the pin to GND (Feather pins have pull-ups; AW9523 pins need a 10k pull-up to 3V).
//    Lights: Feather pins dim with PWM, AW9523 pins drive LEDs with their own current (LED mode),
//    PCF8574 pins only switch, wired LED to 3V through a resistor (the pin sinks it: low = on).
//    The buzzer needs Feather pins (A, and B in opposite phase; B none drives the piezo from A to GND).
//  Add-ons: what a scan of the I2C bus finds at start-up (and on Scan); each can be turned off.
//    Pressure  BMP388/390, DPS310, BME280, BMP280, LPS22    -> the pressure track (/baro) and BAROGRAPH
//    Temp + RH SHT3x, SHT4x, AHT20, BME280                  -> a "Box" row in the climate files, like a node
//    IMU       LSM6DS (LSM6DSOX/DS3/DSL, ISM330), BNO085     -> wake the screen when picked up, auto-rotate it
//    Light     TSL2591, BH1750 (no ID: turned on at 0x23 or 0x5C) -> lux on the status and the screen's line
//    (A VEML7700 can't share the bus: its only address, 0x10, is the GPS's.)
//  Changes are saved in config.json (hwPins, hwAdd) and take effect after a restart.
// ======================================================================
const uint8_t FPINS[] = { 5, 6, 8, 9, 11, 12, 13, 14, 15, 17, 18, 38, 39 };   // free: not I2C, SPI, the SD card's CS or USB sense
const char* fpinName(uint8_t g) {
  switch (g) { case 18: return "A0"; case 17: return "A1"; case 15: return "A3"; case 14: return "A4"; case 8: return "A5";
               case 38: return "RX"; case 39: return "TX"; case 13: return "13 (red LED)"; default: return nullptr; }
}
struct HwPins { String back = "f11", sel = "f9", backLed = "f18", selLed = "f17", buzA = "f5", buzB = "f6", xbP[4], xbA[4], ind[2]; } hp;   // ind: the two indicator lights (none by default)
uint8_t pcfAddr = 0, awAddr = 0;                           // 0: none
bool pcfOK = false, awOK = false;
uint8_t pcfOut = 0xFF, pcfIn = 0xFF, awLast[16]; uint16_t awIn = 0xFFFF;
bool imuWoke = false;                                       // the IMU woke the screen (nothing touched since)
uint32_t lightTestMs = 0;                                   // Settings' Test lights: each light 1 s in turn (BACK, SELECT, then the indicators)
int8_t indCur[2] = { -1, -1 };                              // the rule showing now (-1: none)
uint32_t indMask = 0, indAt = 0;
const char* XB_ACT[] = { "home", "clock", "job", "mark", "trip", "off", "phone", "beep", "bnwas", "alert", "apon", "setclk", "gps" };

bool i2cPing(uint8_t a) { Wire.beginTransmission(a); return Wire.endTransmission() == 0; }
int rd8(uint8_t a, uint8_t r) { Wire.beginTransmission(a); Wire.write(r); if (Wire.endTransmission(false) != 0) return -1; if (Wire.requestFrom(a, (uint8_t)1) != 1) return -1; return Wire.read(); }
bool rdN(uint8_t a, uint8_t r, uint8_t* b, uint8_t n) { Wire.beginTransmission(a); Wire.write(r); if (Wire.endTransmission(false) != 0) return false; if (Wire.requestFrom(a, n) != n) return false; for (uint8_t i = 0; i < n; i++) b[i] = Wire.read(); return true; }
bool wr8(uint8_t a, uint8_t r, uint8_t v) { Wire.beginTransmission(a); Wire.write(r); Wire.write(v); return Wire.endTransmission() == 0; }
bool wrCmd(uint8_t a, const uint8_t* c, uint8_t n) { Wire.beginTransmission(a); Wire.write(c, n); return Wire.endTransmission() == 0; }
uint8_t crc8(const uint8_t* d, int n) { uint8_t c = 0xFF; for (int i = 0; i < n; i++) { c ^= d[i]; for (int b = 0; b < 8; b++) c = c & 0x80 ? (c << 1) ^ 0x31 : c << 1; } return c; }   // Sensirion

// ---- pins ----
char pinKind(const String& c) { return c.length() ? c[0] : 0; }
int pinNum(const String& c) { return c.length() > 1 ? c.substring(1).toInt() : -1; }
bool pinValid(const String& c, bool feathOnly) {
  if (!c.length()) return true;
  char k = pinKind(c); int n = pinNum(c);
  for (size_t i = 1; i < c.length(); i++) if (!isdigit((unsigned char)c[i])) return false;
  if (k == 'f') { for (uint8_t g : FPINS) if (g == n) return true; return false; }
  if (feathOnly) return false;
  if (k == 'p') return n >= 0 && n <= 7 && pcfAddr;
  if (k == 'a') return n >= 0 && n <= 15 && awAddr;
  return false;
}
void pcfWrite() { if (pcfOK) { Wire.beginTransmission(pcfAddr); Wire.write(pcfOut); Wire.endTransmission(); } }
void awPinMode(int n, bool input, bool led) {               // AW9523: input, GPIO output or LED (current) mode
  uint8_t cr = n < 8 ? 0x04 : 0x05, mr = n < 8 ? 0x12 : 0x13, bit = 1 << (n & 7);
  int c = rd8(awAddr, cr), m = rd8(awAddr, mr); if (c < 0 || m < 0) return;
  wr8(awAddr, cr, input ? (c | bit) : (c & ~bit));
  wr8(awAddr, mr, led ? (m & ~bit) : (m | bit));
}
uint8_t awDimReg(int n) { return n < 8 ? 0x24 + n : n < 12 ? 0x20 + (n - 8) : 0x2C + (n - 12); }
void pinInput(const String& c) {
  char k = pinKind(c); int n = pinNum(c);
  if (k == 'f') pinMode(n, INPUT_PULLUP);
  else if (k == 'p' && pcfOK) { pcfOut |= 1 << n; pcfWrite(); }   // high = input with the chip's weak pull-up
  else if (k == 'a' && awOK) awPinMode(n, true, false);
}
uint8_t ledCh = 4;                                          // LEDC channels from 4 up (0 and 1 are the buzzer's)
void pinLedStart(const String& c) {
  char k = pinKind(c); int n = pinNum(c);
  if (k == 'f') { ledcAttachChannel(n, 5000, 8, ledCh++); ledcWrite(n, 0); }
  else if (k == 'p' && pcfOK) { pcfOut |= 1 << n; pcfWrite(); }   // off (the pin sinks the LED: low = on)
  else if (k == 'a' && awOK) { awPinMode(n, false, true); wr8(awAddr, awDimReg(n), 0); awLast[n] = 0; }
}
void pinLevel(const String& c, uint8_t v) {                 // a light, 0-255
  char k = pinKind(c); int n = pinNum(c);
  if (k == 'f') ledcWrite(n, v);
  else if (k == 'p' && pcfOK) { uint8_t o = v ? (pcfOut & ~(1 << n)) : (pcfOut | (1 << n)); if (o != pcfOut) { pcfOut = o; pcfWrite(); } }
  else if (k == 'a' && awOK) { if (awLast[n] != v) { awLast[n] = v; wr8(awAddr, awDimReg(n), v); } }
}
bool pinDown(const String& c) {                             // a button: pressed connects the pin to GND
  char k = pinKind(c); int n = pinNum(c);
  if (k == 'f') return digitalRead(n) == LOW;
  if (k == 'p') return pcfOK && !(pcfIn >> n & 1);
  if (k == 'a') return awOK && !(awIn >> n & 1);
  return false;
}
bool usesExp(char k) { for (const String* s : { &hp.back, &hp.sel, &hp.xbP[0], &hp.xbP[1], &hp.xbP[2], &hp.xbP[3] }) if (pinKind(*s) == k) return true; return false; }
void expRead() {                                            // the expanders' inputs, once per screen tick
  if (pcfOK && usesExp('p') && Wire.requestFrom(pcfAddr, (uint8_t)1) == 1) pcfIn = Wire.read();
  uint8_t b[2]; if (awOK && usesExp('a') && rdN(awAddr, 0x00, b, 2)) awIn = b[0] | (b[1] << 8);
}
void hwPinsLoad(const String& js) {
  if (!js.length()) return;
  JsonDocument d; if (deserializeJson(d, js)) return;
  const char* K[] = { "back", "sel", "backLed", "selLed", "buzA", "buzB" }; String* R[] = { &hp.back, &hp.sel, &hp.backLed, &hp.selLed, &hp.buzA, &hp.buzB };
  for (int i = 0; i < 6; i++) if (d[K[i]].is<const char*>()) *R[i] = (const char*)d[K[i]];
  JsonArray x = d["xb"].as<JsonArray>(); int i = 0;
  for (JsonVariant v : x) { if (i >= 4) break; hp.xbP[i] = v[0] | ""; hp.xbA[i] = v[1] | ""; i++; }
  i = 0; for (JsonVariant v : d["ind"].as<JsonArray>()) { if (i >= 2) break; hp.ind[i++] = v | ""; }
}
String hwPinsJson() {
  String s = "{\"back\":" + jStr(hp.back) + ",\"sel\":" + jStr(hp.sel) + ",\"backLed\":" + jStr(hp.backLed) + ",\"selLed\":" + jStr(hp.selLed) +
             ",\"buzA\":" + jStr(hp.buzA) + ",\"buzB\":" + jStr(hp.buzB) + ",\"xb\":[";
  for (int i = 0; i < 4; i++) s += String(i ? "," : "") + "[" + jStr(hp.xbP[i]) + "," + jStr(hp.xbA[i]) + "]";
  return s + "],\"ind\":[" + jStr(hp.ind[0]) + "," + jStr(hp.ind[1]) + "]}";
}

// ---- add-ons ----
const char* AK_NAME[] = { "", "BMP388/390", "DPS310", "BME280", "BMP280", "LPS22", "LPS22DF", "SHT3x", "SHT4x", "AHT20", "LSM6DS", "BNO085", "BH1750", "TSL2591", "PCF8574", "AW9523" };
const char* AK_KEY[]  = { "", "bmp3", "dps310", "bme280", "bmp280", "lps22", "lps22df", "sht3", "sht4", "aht20", "lsm6", "bno085", "bh1750", "tsl2591", "pcf8574", "aw9523" };
const char* AK_WHAT[] = { "", "press", "press", "press+th", "press", "press", "press", "th", "th", "th", "imu", "imu", "light", "light", "io", "io" };
std::vector<AddOn> addons;
String addOff;                                              // "key@hex,..." turned off in Settings
uint8_t bhAddr = 0;                                         // the BH1750's address if you turned it on (it has no ID to find it by)
uint8_t imuMount = 0; bool imuFlip = false, imuWake = true, imuRot = true;   // X arrow: 0 to the screen's top, 1 right, 2 bottom, 3 left
String scanList;                                            // every address that answered, "0x10,0x36,…"
bool isOff(uint8_t k, uint8_t a) { char t[16]; snprintf(t, sizeof t, "%s@%02x", AK_KEY[k], a); return addOff.indexOf(t) >= 0; }
void addScan(bool soft = false) {                            // soft: Settings' Scan while running: what's in use keeps its name, nothing is reset
  std::vector<AddOn> was = addons;
  addons.clear(); scanList = "";
  for (uint8_t a = 0x08; a < 0x78; a++) if (i2cPing(a)) { char t[6]; snprintf(t, sizeof t, "0x%02X", a); scanList += (scanList.length() ? "," : "") + String(t); }
  auto add = [](uint8_t k, uint8_t a) { addons.push_back({ k, a, !isOff(k, a) }); };
  for (uint8_t a : { 0x77, 0x76 }) if (i2cPing(a)) {
    bool kept = false; if (soft) for (const AddOn& x : was) if (x.a == a && (x.k == AK_BMP3 || x.k == AK_DPS310)) { add(x.k, a); kept = true; }
    if (kept) continue;
    int id = rd8(a, 0xD0);
    if (id == 0x60) { add(AK_BME280, a); continue; }
    if (id == 0x58) { add(AK_BMP280, a); continue; }
    if (!soft) { wr8(a, 0x0C, 0x09); delay(40); }            // a DPS310's soft reset (ignored by the others): its data registers read 0 after it
    int b3 = rd8(a, 0x00);
    if (b3 == 0x50 || b3 == 0x60) add(AK_BMP3, a);
    else if (rd8(a, 0x0D) == 0x10) add(AK_DPS310, a);
  }
  for (uint8_t a : { 0x5D, 0x5C }) if (i2cPing(a)) {
    int w = rd8(a, 0x0F);
    if (w == 0xB1 || w == 0xB3) add(AK_LPS22, a); else if (w == 0xB4) add(AK_LPS22DF, a);
    else if (bhAddr == a) add(AK_BH1750, a);
  }
  for (uint8_t a : { 0x44, 0x45 }) if (i2cPing(a)) {
    uint8_t b[6]; const uint8_t s4[] = { 0x89 }, s3[] = { 0xF3, 0x2D };
    bool four = wrCmd(a, s4, 1) && (delay(2), Wire.requestFrom(a, (uint8_t)6) == 6);
    if (four) { for (int i = 0; i < 6; i++) b[i] = Wire.read(); four = crc8(b, 2) == b[2] && crc8(b + 3, 2) == b[5]; }
    if (four) { add(AK_SHT4, a); continue; }
    if (wrCmd(a, s3, 2) && (delay(2), Wire.requestFrom(a, (uint8_t)3) == 3)) { for (int i = 0; i < 3; i++) b[i] = Wire.read(); if (crc8(b, 2) == b[2]) add(AK_SHT3, a); }
  }
  if (i2cPing(0x38) && Wire.requestFrom((uint8_t)0x38, (uint8_t)1) == 1) { uint8_t st = Wire.read(); if (!(st & 0x80) && (st & 0x08)) add(AK_AHT20, 0x38); }   // (the knob can be at 0x38 too: its answer isn't an AHT20's)
  for (uint8_t a : { 0x6A, 0x6B }) if (i2cPing(a)) { int w = rd8(a, 0x0F); if (w == 0x69 || w == 0x6A || w == 0x6B || w == 0x6C) add(AK_LSM6, a); }
  for (uint8_t a : { 0x4A, 0x4B }) if (i2cPing(a)) add(AK_BNO085, a);
  if (i2cPing(0x29) && rd8(0x29, 0xA0 | 0x12) == 0x50) add(AK_TSL2591, 0x29);
  if (bhAddr == 0x23 && pcfAddr != 0x23 && i2cPing(0x23)) add(AK_BH1750, 0x23);
  for (uint8_t a = 0x58; a <= 0x5B; a++) if (i2cPing(a) && rd8(a, 0x10) == 0x23) { add(AK_AW9523, a); break; }
  if (pcfAddr && i2cPing(pcfAddr)) add(AK_PCF8574, pcfAddr);
}
const AddOn* addFirst(const char* what, std::initializer_list<uint8_t> order) {   // the first of these kinds that's on
  for (uint8_t k : order) for (const AddOn& x : addons) if (x.k == k && x.on) return &x;
  return nullptr;
}
void hwAddLoad(const String& js) {
  if (!js.length()) return;
  JsonDocument d; if (deserializeJson(d, js)) return;
  addOff = d["off"] | ""; bhAddr = d["bh"] | 0; pcfAddr = d["pcf"] | 0;
  if (bhAddr != 0x23 && bhAddr != 0x5C) bhAddr = 0;
  if (pcfAddr < 0x20 || pcfAddr > 0x27) pcfAddr = 0;
  imuMount = (uint8_t)constrain((int)(d["imuM"] | 0), 0, 3); imuFlip = d["imuF"] | false; imuWake = d["imuW"] | true; imuRot = d["imuR"] | true;
}

// ---- the sensors ----
// pressure (and its temperature): the first found of BMP3, DPS310, LPS22, BME280, BMP280
uint8_t prK = 0, prA = 0;
struct { uint16_t T1; int16_t T2, T3; uint16_t P1; int16_t P2, P3, P4, P5, P6, P7, P8, P9; uint8_t H1, H3; int16_t H2, H4, H5; int8_t H6; } bmeC;
uint8_t bmeA = 0; float bmeRH = NAN;
bool bmeStart(uint8_t a, bool hum) {
  uint8_t c[26], h[7];
  wr8(a, 0xE0, 0xB6); delay(5);
  if (!rdN(a, 0x88, c, 26)) return false;
  auto u16 = [&](int i) { return (uint16_t)(c[i] | c[i + 1] << 8); };
  bmeC.T1 = u16(0); bmeC.T2 = (int16_t)u16(2); bmeC.T3 = (int16_t)u16(4); bmeC.P1 = u16(6);
  int16_t* P[] = { &bmeC.P2, &bmeC.P3, &bmeC.P4, &bmeC.P5, &bmeC.P6, &bmeC.P7, &bmeC.P8, &bmeC.P9 };
  for (int i = 0; i < 8; i++) *P[i] = (int16_t)u16(8 + 2 * i);
  bmeC.H1 = c[25];
  if (hum && rdN(a, 0xE1, h, 7)) {
    bmeC.H2 = (int16_t)(h[0] | h[1] << 8); bmeC.H3 = h[2];
    bmeC.H4 = (int16_t)((int8_t)h[3] * 16 | (h[4] & 0x0F)); bmeC.H5 = (int16_t)((int8_t)h[5] * 16 | (h[4] >> 4)); bmeC.H6 = (int8_t)h[6];
    wr8(a, 0xF2, 0x01);                                      // humidity x1
  }
  wr8(a, 0xF5, 0xA8);                                        // 1 s standby, filter x4
  wr8(a, 0xF4, 0x57);                                        // temperature x2, pressure x16, normal mode
  bmeA = a; return true;
}
bool bmeRead(float& hpa, float& c, float* rh) {
  uint8_t b[8]; if (!rdN(bmeA, 0xF7, b, 8)) return false;
  int32_t aP = (b[0] << 12) | (b[1] << 4) | (b[2] >> 4), aT = (b[3] << 12) | (b[4] << 4) | (b[5] >> 4), aH = (b[6] << 8) | b[7];
  if (aT == 0x80000) return false;
  double v1 = (aT / 16384.0 - bmeC.T1 / 1024.0) * bmeC.T2, v2 = (aT / 131072.0 - bmeC.T1 / 8192.0); v2 = v2 * v2 * bmeC.T3;
  double tf = v1 + v2; c = tf / 5120.0;
  v1 = tf / 2.0 - 64000.0; v2 = v1 * v1 * bmeC.P6 / 32768.0; v2 = v2 + v1 * bmeC.P5 * 2.0; v2 = v2 / 4.0 + bmeC.P4 * 65536.0;
  v1 = (bmeC.P3 * v1 * v1 / 524288.0 + bmeC.P2 * v1) / 524288.0; v1 = (1.0 + v1 / 32768.0) * bmeC.P1;
  if (v1 == 0) return false;
  double p = 1048576.0 - aP; p = (p - v2 / 4096.0) * 6250.0 / v1;
  v1 = bmeC.P9 * p * p / 2147483648.0; v2 = p * bmeC.P8 / 32768.0; p = p + (v1 + v2 + bmeC.P7) / 16.0;
  hpa = p / 100.0;
  if (rh && aH != 0x8000) {
    double h = tf - 76800.0;
    h = (aH - (bmeC.H4 * 64.0 + bmeC.H5 / 16384.0 * h)) * (bmeC.H2 / 65536.0 * (1.0 + bmeC.H6 / 67108864.0 * h * (1.0 + bmeC.H3 / 67108864.0 * h)));
    h = h * (1.0 - bmeC.H1 * h / 524288.0);
    *rh = constrain((float)h, 0.0f, 100.0f);
  }
  return true;
}
struct { float c0, c1, c00, c10, c01, c11, c20, c21, c30; } dpsC;
bool dpsStart(uint8_t a) {
  for (int i = 0; i < 50 && (rd8(a, 0x08) & 0xC0) != 0xC0; i++) delay(10);   // coefficients and sensor ready
  uint8_t c[18]; if (!rdN(a, 0x10, c, 18)) return false;
  auto s = [](int32_t v, int bits) { return v & (1L << (bits - 1)) ? v - (1L << bits) : v; };
  dpsC.c0 = s((c[0] << 4) | (c[1] >> 4), 12); dpsC.c1 = s(((c[1] & 0x0F) << 8) | c[2], 12);
  dpsC.c00 = s(((int32_t)c[3] << 12) | (c[4] << 4) | (c[5] >> 4), 20); dpsC.c10 = s(((int32_t)(c[5] & 0x0F) << 16) | (c[6] << 8) | c[7], 20);
  dpsC.c01 = s((c[8] << 8) | c[9], 16); dpsC.c11 = s((c[10] << 8) | c[11], 16); dpsC.c20 = s((c[12] << 8) | c[13], 16);
  dpsC.c21 = s((c[14] << 8) | c[15], 16); dpsC.c30 = s((c[16] << 8) | c[17], 16);
  // the temperature quirk some DPS310s need for a sane reading
  wr8(a, 0x0E, 0xA5); wr8(a, 0x0F, 0x96); wr8(a, 0x62, 0x02); wr8(a, 0x0E, 0x00); wr8(a, 0x0F, 0x00);
  int src = rd8(a, 0x28); uint8_t ext = src >= 0 && (src & 0x80) ? 0x80 : 0;
  wr8(a, 0x06, 0x26);                                        // pressure: 4 per second, x64 oversampling
  wr8(a, 0x07, ext | 0x20);                                  // temperature: 4 per second, the sensor the coefficients came from
  wr8(a, 0x09, 0x04);                                        // pressure shift (oversampling > 8)
  wr8(a, 0x08, 0x07);                                        // continuous pressure and temperature
  return true;
}
bool dpsRead(uint8_t a, float& hpa, float& c) {
  uint8_t b[6]; if (!rdN(a, 0x00, b, 6)) return false;
  auto s24 = [](uint8_t* p) { int32_t v = ((int32_t)p[0] << 16) | (p[1] << 8) | p[2]; return v & 0x800000 ? v - 0x1000000 : v; };
  float t = s24(b + 3) / 524288.0f, pr = s24(b) / 1040384.0f;   // scale factors for x1 temperature and x64 pressure
  c = dpsC.c0 * 0.5f + dpsC.c1 * t;
  hpa = (dpsC.c00 + pr * (dpsC.c10 + pr * (dpsC.c20 + pr * dpsC.c30)) + t * dpsC.c01 + t * pr * (dpsC.c11 + pr * dpsC.c21)) / 100.0f;
  return true;
}
bool lpsRead(uint8_t a, float& hpa, float& c) {
  uint8_t b[5]; if (!rdN(a, 0x28, b, 5)) return false;
  int32_t p = (int32_t)((uint32_t)b[2] << 16 | b[1] << 8 | b[0]); if (p & 0x800000) p -= 0x1000000;
  hpa = p / 4096.0f; c = (int16_t)(b[4] << 8 | b[3]) / 100.0f; return true;
}
// temperature and humidity in the box: the first of SHT4x, SHT3x, AHT20, BME280; read every 30 s, started one tick and read the next
uint8_t thK = 0, thA = 0; float boxC = NAN, boxRH = NAN; uint32_t boxThMs = 0, thStartMs = 0; bool thBusy = false;
void thTick() {
  if (!thK) return;
  uint32_t now = millis();
  if (!thBusy) {
    if (boxThMs && now - boxThMs < 30000) return;
    if (thK == AK_BME280) { float h, c, rh; if (bmeRead(h, c, &rh)) { boxC = c; boxRH = rh; } boxThMs = now ? now : 1; return; }
    const uint8_t s4[] = { 0xFD }, s3[] = { 0x24, 0x00 }, aht[] = { 0xAC, 0x33, 0x00 };
    bool ok = thK == AK_SHT4 ? wrCmd(thA, s4, 1) : thK == AK_SHT3 ? wrCmd(thA, s3, 2) : wrCmd(thA, aht, 3);
    if (ok) { thBusy = true; thStartMs = now; } else boxThMs = now;
    return;
  }
  if (now - thStartMs < (thK == AK_AHT20 ? 90u : 25u)) return;
  thBusy = false; boxThMs = now ? now : 1;
  uint8_t b[6]; if (Wire.requestFrom(thA, (uint8_t)6) != 6) return;
  for (int i = 0; i < 6; i++) b[i] = Wire.read();
  if (thK == AK_AHT20) {
    if (b[0] & 0x80) return;                                 // still busy
    uint32_t h = ((uint32_t)b[1] << 12) | (b[2] << 4) | (b[3] >> 4), t = ((uint32_t)(b[3] & 0x0F) << 16) | (b[4] << 8) | b[5];
    boxRH = h * 100.0f / 1048576.0f; boxC = t * 200.0f / 1048576.0f - 50.0f; return;
  }
  if (crc8(b, 2) != b[2] || crc8(b + 3, 2) != b[5]) return;
  uint16_t t = b[0] << 8 | b[1], h = b[3] << 8 | b[4];
  boxC = -45.0f + 175.0f * t / 65535.0f;
  boxRH = thK == AK_SHT4 ? constrain(-6.0f + 125.0f * h / 65535.0f, 0.0f, 100.0f) : 100.0f * h / 65535.0f;
}
bool boxThFresh() { return thK && boxThMs && millis() - boxThMs < 120000 && !isnan(boxC); }
// light: TSL2591 or BH1750, every 5 s
uint8_t lxK = 0, lxA = 0; float boxLux = NAN; uint32_t lxMs = 0;
void lxStart() {
  if (lxK == AK_TSL2591) { wr8(lxA, 0xA0 | 0x00, 0x03); wr8(lxA, 0xA0 | 0x01, 0x10); }   // on, medium gain, 100 ms
  else if (lxK == AK_BH1750) { const uint8_t on[] = { 0x01 }, hr[] = { 0x10 }; wrCmd(lxA, on, 1); wrCmd(lxA, hr, 1); }   // continuous, 1 lx
}
void lxTick() {
  if (!lxK || millis() - lxMs < 5000) return;
  lxMs = millis();
  if (lxK == AK_TSL2591) {
    uint8_t b[4]; if (!rdN(lxA, 0xA0 | 0x14, b, 4)) return;
    float c0 = b[0] | b[1] << 8, c1 = b[2] | b[3] << 8;
    if (c0 >= 65535 || c1 >= 65535) { boxLux = NAN; return; }   // too bright for this gain
    boxLux = c0 <= 0 ? 0 : max(0.0f, (c0 - c1) * (1.0f - c1 / c0) / (100.0f * 25.0f / 408.0f));
  } else if (Wire.requestFrom(lxA, (uint8_t)2) == 2) { uint16_t r = Wire.read() << 8; r |= Wire.read(); boxLux = r / 1.2f; }
}
// IMU: gravity in its own frame (g), every 100 ms
uint8_t imK = 0, imA = 0; float imG[3] = { NAN, NAN, NAN }; uint32_t imMs = 0, imSeen = 0;
uint8_t bnoSeq[6];
bool bnoSend(uint8_t ch, const uint8_t* d, uint16_t n) {
  uint16_t L = n + 4; Wire.beginTransmission(imA);
  Wire.write(L & 0xFF); Wire.write(L >> 8); Wire.write(ch); Wire.write(bnoSeq[ch]++); Wire.write(d, n);
  return Wire.endTransmission() == 0;
}
int bnoRead(uint8_t& ch, uint8_t* p, int pmax) {            // one packet: its first pmax payload bytes (the rest read and dropped)
  if (Wire.requestFrom(imA, (uint8_t)4) != 4) return -1;
  uint8_t h[4]; for (int i = 0; i < 4; i++) h[i] = Wire.read();
  int L = (h[0] | h[1] << 8) & 0x7FFF; ch = h[2];
  if (L <= 4) return 0;
  int left = L - 4, got = 0;
  while (left > 0) {
    int n = min(left, 124);
    if (Wire.requestFrom(imA, (uint8_t)(n + 4)) != n + 4) return -1;
    for (int i = 0; i < 4; i++) Wire.read();                 // each piece starts with the header again
    for (int i = 0; i < n; i++) { uint8_t v = Wire.read(); if (got < pmax) p[got] = v; got++; }
    left -= n;
  }
  return min(got, pmax);
}
void bnoEnable() {                                          // gravity, 10 a second
  const uint32_t us = 100000;
  uint8_t c[17] = { 0xFD, 0x06, 0, 0, 0, (uint8_t)us, (uint8_t)(us >> 8), (uint8_t)(us >> 16), (uint8_t)(us >> 24) };
  bnoSend(2, c, 17);
}
void imStart() {
  if (imK == AK_LSM6) { wr8(imA, 0x12, 0x44); wr8(imA, 0x10, 0x40); }   // block update, auto-increment; accelerometer 104 Hz, +-2 g
  else if (imK == AK_BNO085) {
    uint8_t p[32], ch;
    for (int i = 0; i < 12; i++) { if (bnoRead(ch, p, 32) <= 0) delay(20); }   // its start-up messages
    bnoEnable();
  }
}
void imRead() {
  if (!imK || millis() - imMs < 100) return;
  imMs = millis();
  if (imK == AK_LSM6) {
    uint8_t b[6]; if (!rdN(imA, 0x28, b, 6)) return;
    for (int i = 0; i < 3; i++) imG[i] = (int16_t)(b[2 * i] | b[2 * i + 1] << 8) * 0.061e-3f;
    imSeen = millis(); return;
  }
  uint8_t p[32], ch;
  for (int k = 0; k < 4; k++) {
    int n = bnoRead(ch, p, 32); if (n <= 0) break;
    if (ch == 3 && n >= 15 && p[0] == 0xFB && (p[5] == 0x06 || p[5] == 0x01)) {
      for (int i = 0; i < 3; i++) imG[i] = (int16_t)(p[9 + 2 * i] | p[10 + 2 * i] << 8) / 256.0f / 9.80665f;
      imSeen = millis();
    }
  }
  if (imSeen == 0 || millis() - imSeen > 5000) { static uint32_t last = 0; if (millis() - last > 5000) { last = millis(); bnoEnable(); } }   // no reports: ask again
}

void hwStart() {
  memset(awLast, 0xFF, sizeof awLast);
  addScan();
  for (const AddOn& x : addons) if (x.k == AK_AW9523 && x.on) { awAddr = x.a; awOK = true; wr8(awAddr, 0x7F, 0x00); delay(2); wr8(awAddr, 0x11, 0x10); }   // reset; P0 push-pull
  for (const AddOn& x : addons) if (x.k == AK_PCF8574 && x.on) { pcfOK = true; pcfWrite(); }
  if (const AddOn* p = addFirst("press", { AK_BMP3, AK_DPS310, AK_LPS22, AK_LPS22DF, AK_BME280, AK_BMP280 })) {
    prK = p->k; prA = p->a;
    if (prK == AK_DPS310 && !dpsStart(prA)) prK = 0;
    else if ((prK == AK_BME280 || prK == AK_BMP280) && !bmeStart(prA, prK == AK_BME280)) prK = 0;
    else if (prK == AK_LPS22) wr8(prA, 0x10, 0x12);          // 1 a second, block update
    else if (prK == AK_LPS22DF) { wr8(prA, 0x10, 0x08); wr8(prA, 0x11, 0x08); }
  }
  if (const AddOn* t = addFirst("th", { AK_SHT4, AK_SHT3, AK_AHT20, AK_BME280 })) {
    thK = t->k; thA = t->a;
    if (thK == AK_AHT20) { const uint8_t init[] = { 0xBE, 0x08, 0x00 }; wrCmd(thA, init, 3); delay(10); }
    if (thK == AK_BME280 && bmeA != thA && !bmeStart(thA, true)) thK = 0;
  }
  if (const AddOn* l = addFirst("light", { AK_TSL2591, AK_BH1750 })) { lxK = l->k; lxA = l->a; lxStart(); }
  if (const AddOn* m = addFirst("imu", { AK_BNO085, AK_LSM6 })) { imK = m->k; imA = m->a; imStart(); }
  Serial.printf("I2C: %s\n", scanList.c_str());
  for (const AddOn& x : addons) Serial.printf("  %s at 0x%02X%s\n", AK_NAME[x.k], x.a, x.on ? "" : " (off)");
}
String hwJson() {                                           // for Settings: what's fitted, what each pin does, the choices
  String s = "{\"pins\":" + hwPinsJson() + ",\"feather\":[";
  for (size_t i = 0; i < sizeof FPINS; i++) { const char* n = fpinName(FPINS[i]); s += String(i ? "," : "") + "[" + String(FPINS[i]) + "," + jStr(n ? n : String(FPINS[i])) + "]"; }
  s += "],\"acts\":["; for (size_t i = 0; i < sizeof XB_ACT / sizeof XB_ACT[0]; i++) s += String(i ? "," : "") + jStr(XB_ACT[i]);
  s += "],\"pcf\":" + String(pcfAddr) + ",\"pcfOK\":" + jBool(pcfOK) + ",\"aw\":" + String(awAddr) + ",\"awOK\":" + jBool(awOK) + ",\"bh\":" + String(bhAddr) +
       ",\"imuM\":" + String(imuMount) + ",\"imuF\":" + jBool(imuFlip) + ",\"imuW\":" + jBool(imuWake) + ",\"imuR\":" + jBool(imuRot) +
       ",\"scan\":" + jStr(scanList) + ",\"parts\":[";
  for (size_t i = 0; i < addons.size(); i++) {
    const AddOn& x = addons[i];
    bool used = (x.k == prK && x.a == prA) || (x.k == thK && x.a == thA) || (x.k == lxK && x.a == lxA) || (x.k == imK && x.a == imA) || (x.k == AK_AW9523 && awOK) || (x.k == AK_PCF8574 && pcfOK);
    s += String(i ? "," : "") + "{\"k\":" + jStr(AK_KEY[x.k]) + ",\"n\":" + jStr(AK_NAME[x.k]) + ",\"w\":" + jStr(AK_WHAT[x.k]) + ",\"a\":" + String(x.a) + ",\"on\":" + jBool(x.on) + ",\"used\":" + jBool(used) + "}";
  }
  s += "],\"down\":[" + jBool(pinDown(hp.back)) + "," + jBool(pinDown(hp.sel));
  for (int i = 0; i < 4; i++) s += "," + jBool(hp.xbP[i].length() && pinDown(hp.xbP[i]));
  return s + "]}";
}

// ======================================================================
//  Pressure (BMP388 inside the box): read every minute, the 10-minute average saved to its own track,
//  /baro/YYYY-MM-DD.csv (a calendar day in the box's time zone): utc,hpa,box_c,n  (utc: the start of the
//  10 minutes; n: readings averaged). The last 7 days are also kept in memory for BAROGRAPH and reloaded
//  from the files after a restart. A 10 minutes with no reading (box off, sensor missing) has no row.
// ======================================================================
Adafruit_BMP3XX bmp;
bool bmpOK = false, baroLoaded = false;
float baroHpa = NAN, baroC = NAN;                         // the newest reading
uint32_t lastBaroMs = 0, baroSlot = 0; double baroSum = 0, baroTSum = 0; uint16_t baroN = 0;
const uint16_t BR_N = 7 * 144 + 1;                         // 10-minute slots: 7 days and one
struct BrPt { uint16_t tag, v; };                         // tag: the slot's low 16 bits; v: (hPa - 300) x 50, 0xFFFF = empty
BrPt* brRing = nullptr;                                    // 4 KB, taken from the heap only when there is a sensor
void brPut(uint32_t slot, float hpa) { if (!brRing) return; BrPt& b = brRing[slot % BR_N]; b.tag = (uint16_t)slot; b.v = (uint16_t)constrain(lroundf((hpa - 300.0f) * 50.0f), 0L, 65534L); }
float brGet(uint32_t slot) { if (!brRing) return NAN; const BrPt& b = brRing[slot % BR_N]; return b.v != 0xFFFF && b.tag == (uint16_t)slot ? 300.0f + b.v / 50.0f : NAN; }
uint32_t brLast() {                                        // the newest slot with a value (0: none)
  if (!timeValid) return 0;
  uint32_t s = (uint32_t)time(nullptr) / BARO_SAVE_S;
  for (uint32_t k = 0; k < 3; k++) if (!isnan(brGet(s - k))) return s - k;
  return 0;
}
fs::FS* baroFS() { return sdOK ? (fs::FS*)&SD : (flashOK ? (fs::FS*)&LittleFS : nullptr); }
bool pressRead(float& h, float& c) {                       // the pressure sensor Add-ons picked (hPa, its temperature)
  switch (prK) {
    case AK_BMP3: if (!bmp.performReading()) return false; h = bmp.pressure / 100.0f; c = bmp.temperature; return true;
    case AK_DPS310: return dpsRead(prA, h, c);
    case AK_LPS22: case AK_LPS22DF: return lpsRead(prA, h, c);
    case AK_BME280: case AK_BMP280: return bmeRead(h, c, nullptr);
  }
  return false;
}
void baroStart() {
  bmpOK = prK != 0;
  if (prK == AK_BMP3) {
    bmpOK = bmp.begin_I2C(prA);
    if (bmpOK) { bmp.setTemperatureOversampling(BMP3_OVERSAMPLING_2X); bmp.setPressureOversampling(BMP3_OVERSAMPLING_8X); bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3); }
  }
  if (!bmpOK) return;
  brRing = (BrPt*)malloc(sizeof(BrPt) * BR_N);
  if (brRing) for (uint16_t i = 0; i < BR_N; i++) brRing[i] = { 0, 0xFFFF };
}
void baroLoad() {                                          // the last 7 days from the files, once the clock is right
  baroLoaded = true;
  time_t t = time(nullptr);
  for (int back = 7; back >= 0; back--) {
    String p = "/baro/" + localDate(t - (time_t)back * 86400) + ".csv";
    fs::FS* fs = (sdOK && SD.exists(p)) ? (fs::FS*)&SD : (flashOK && LittleFS.exists(p)) ? (fs::FS*)&LittleFS : nullptr;
    if (!fs) continue;
    File f = fs->open(p, FILE_READ); if (!f) continue;
    while (f.available()) {
      String l = f.readStringUntil('\n');
      if (l.length() < 22 || l[0] == 'u') continue;
      uint32_t ep = parseUtc(l.substring(0, 20)); int c = l.indexOf(',', 21);
      float h = (c > 21 ? l.substring(21, c) : l.substring(21)).toFloat();
      if (ep && h > 300 && h < 1200 && (uint32_t)t - ep < 7UL * 86400UL + BARO_SAVE_S) brPut(ep / BARO_SAVE_S, h);
    }
    f.close();
  }
}
void baroSave(uint32_t slot, float hpa, float c, uint16_t n) {
  brPut(slot, hpa);
  fs::FS* fs = baroFS(); if (!fs) return;
  if (fs == &LittleFS && LittleFS.totalBytes() - LittleFS.usedBytes() < 200 * 1024) return;   // keep room for the log
  uint32_t ep = slot * BARO_SAVE_S;
  if (!fs->exists("/baro")) fs->mkdir("/baro");
  String path = "/baro/" + localDate(ep) + ".csv";
  bool isNew = !fs->exists(path);
  File f = fs->open(path, FILE_APPEND);
  if (!f) { if (fs == &SD) sdOK = false; return; }
  if (isNew) f.println("utc,hpa,box_c,n");
  f.print(isoUtc(ep)); f.print(','); f.print(String(hpa, 2)); f.print(','); f.print(csvNum(c, 1)); f.print(','); f.println(n);
  f.close();
}
void baroTick() {
  if (!bmpOK) return;
  if (timeValid && !baroLoaded) baroLoad();
  if (lastBaroMs && millis() - lastBaroMs < BARO_READ_S * 1000UL) return;
  lastBaroMs = millis();
  float h, c; if (!pressRead(h, c)) return;
  if (!(h > 300 && h < 1200)) return;                      // not a real reading
  baroHpa = h; baroC = c;
  if (!timeValid) return;
  uint32_t slot = (uint32_t)time(nullptr) / BARO_SAVE_S;
  if (slot != baroSlot) {
    if (baroN && baroSlot) baroSave(baroSlot, baroSum / baroN, baroTSum / baroN, baroN);
    baroSlot = slot; baroSum = 0; baroTSum = 0; baroN = 0;
  }
  baroSum += h; baroTSum += baroC; baroN++;
}
// the change over 3 hours, hPa: the newest saved 10 minutes against the one 3 hours before it (NAN if either is missing)
float baro3h() { uint32_t l = brLast(); if (!l) return NAN; float a = brGet(l), b = brGet(l - 18); return isnan(a) || isnan(b) ? NAN : a - b; }
// the shipping-forecast words for a 3-hour change in hPa
String baroWord(float d) {
  float a = fabsf(d);
  if (a < 0.1f) return "Steady";
  String w = d > 0 ? "Rising" : "Falling";
  if (a <= 1.5f) return w + " slowly";
  if (a <= 3.5f) return w;
  if (a <= 6.0f) return w + " quickly";
  return w + " very rapidly";
}

// ======================================================================
//  Battery (MAX17048 on newer Feathers, LC709203F on older ones)
// ======================================================================
uint8_t battType = 0;            // 0 none, 1 MAX17048, 2 LC709203F
float battV = NAN, battPct = NAN, battRate = NAN;           // battV: median of the last readings; battPct: from the curve below; battRate: % per hour
uint32_t lastBattMs = 0;

void battStart() {
  if (gaugeMax.begin()) battType = 1;
  else if (gaugeLc.begin()) { battType = 2; gaugeLc.setPackSize(LC709203F_APA_2000MAH); }
}
uint32_t battLowSince = 0;
bool i2cRest = false;                                     // PIN_I2C_POWER's rest level (STEMMA QT power off)
// Battery %. The fuel gauge's own % is far off on these cells (a 4-day run from full on a 2,500 mAh LiPo:
// the gauge read 12 % at 3.70 V with 43 % of the charge still to go, and 0 % with 5 hours left), so only its
// voltage is used. The % comes from that run's discharge curve: % of the charge left before BATT_OFF_V, where
// the box switches itself off, so 0 % here means "about to switch off". The run drew a steady 27 mA; the box
// draws more, which pulls the voltage down a little and reads a few % low, never high. While charging, the
// cell's voltage is raised by the charger and says nothing about its charge, so no % is shown then, nor for
// the first 2 minutes after it is unplugged, while the voltage settles.
const float BCURVE[][2] = { {4.15, 100.0}, {4.13, 97.4}, {4.11, 96.0}, {4.09, 94.4}, {4.07, 92.7}, {4.05, 90.8}, {4.03, 86.4}, {4.01, 82.9}, {3.99, 81.0}, {3.97, 79.1}, {3.95, 77.2}, {3.93, 74.8}, {3.91, 69.0}, {3.89, 63.3}, {3.87, 60.2}, {3.85, 57.9}, {3.83, 56.0}, {3.81, 54.1}, {3.79, 52.4}, {3.77, 50.5}, {3.75, 48.5}, {3.73, 46.6}, {3.71, 44.3}, {3.69, 41.7}, {3.67, 38.5}, {3.65, 34.7}, {3.63, 30.1}, {3.61, 25.6}, {3.59, 22.2}, {3.57, 19.2}, {3.55, 16.5}, {3.53, 14.0}, {3.51, 11.9}, {3.49, 9.9}, {3.47, 8.0}, {3.45, 6.0}, {3.43, 4.3}, {3.41, 3.1}, {3.39, 2.3}, {3.37, 1.6}, {3.35, 1.1}, {3.33, 0.6}, {3.30, 0.0} };
const int BCURVE_N = sizeof(BCURVE) / sizeof(BCURVE[0]);
float pctFromV(float v) {
  if (v >= BCURVE[0][0]) return 100.0f;
  for (int i = 1; i < BCURVE_N; i++) if (v >= BCURVE[i][0]) {
    float v0 = BCURVE[i][0], v1 = BCURVE[i - 1][0], p0 = BCURVE[i][1], p1 = BCURVE[i - 1][1];
    return p0 + (p1 - p0) * (v - v0) / (v1 - v0);
  }
  return 0.0f;
}
const uint8_t VB = 20;
float vBuf[VB]; uint8_t vN = 0, vI = 0;                   // the last 20 readings (10 minutes), newest last
float vMed(uint8_t from, uint8_t n) {                      // median of n readings, starting 'from' readings back
  if (from >= vN) return NAN;
  n = min<uint8_t>(n, vN - from);
  float a[VB]; for (uint8_t i = 0; i < n; i++) a[i] = vBuf[(vI + VB - 1 - from - i) % VB];
  std::sort(a, a + n); return n % 2 ? a[n / 2] : (a[n / 2 - 1] + a[n / 2]) / 2;
}
struct BSamp { uint32_t ms; float p; };
const uint16_t BH = 361;                                  // one % reading a minute, the last 6 hours: for the rate and the time left
BSamp bHist[BH]; uint16_t bHN = 0, bHI = 0; uint32_t bHLast = 0;
bool battChg = false; float chgPeak = 0, battLeftH = NAN; bool battSpanOk = false;   // battChg: on USB power
uint32_t usbOffMs = 0;                                    // when it was last unplugged (USB_PIN wired; 0: not since start-up)
extern bool redraw;
bool usbIn() { return USB_PIN >= 0 && analogReadMilliVolts(USB_PIN) > USB_MV; }
void battHistClear() { bHN = 0; bHI = 0; bHLast = 0; battRate = NAN; battLeftH = NAN; battSpanOk = false; }
void battRead() {
  float v = NAN;
  if (battType == 1) v = gaugeMax.cellVoltage(); else if (battType == 2) v = gaugeLc.cellVoltage();
  if (isnan(v) || v < 2.5f) { battV = v; battPct = NAN; battHistClear(); return; }   // no cell
  vBuf[vI] = v; vI = (vI + 1) % VB; if (vN < VB) vN++;
  if (USB_PIN >= 0) {                                      // the USB pin says it: no guessing
    battV = vMed(0, 10);
    battPct = battChg || (usbOffMs && millis() - usbOffMs < 120000UL) ? NAN : pctFromV(battV);
    if (isnan(battPct)) return;
  } else {
  // Not wired: charging is guessed. The median of the newest 3 readings 30 mV over the median of those 3 to 7 minutes before. Running
  // down, the cell never does that (4 days of 1-minute readings: at most 14 mV); a charger lifts it 30-100 mV.
  // Unplugged: 40 mV below the highest it reached while charging.
  float v3 = vMed(0, 3), vOld = vMed(6, 9);
  if (!battChg && !isnan(vOld) && v3 - vOld > 0.030f) { battChg = true; chgPeak = v3; battHistClear(); }
  else if (battChg) { chgPeak = max(chgPeak, v3); if (v3 < chgPeak - 0.040f) { battChg = false; vBuf[0] = v; vN = 1; vI = 1; battHistClear(); } }
  battV = vMed(0, 10);
  battPct = battChg ? NAN : pctFromV(battV);
  if (battChg) return;
  }
  // Rate: a straight line through up to the last 6 hours of readings, once there is an hour of them. On the
  // 4-day run, an hour gave the time left within about 15 % (median), 6 hours within about 3 %; the flat
  // stretch near 3.90 V, where the voltage hardly moves, is the hardest part.
  uint32_t now = millis();
  if (!bHN || now - bHLast >= 60000UL) { bHist[bHI] = { now, battPct }; bHI = (bHI + 1) % BH; if (bHN < BH) bHN++; bHLast = now; }
  double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0; uint32_t first = now;
  for (uint16_t i = 0; i < bHN; i++) { const BSamp& b = bHist[i]; if (now - b.ms > 6UL * 3600000UL) continue; double x = (double)(now - b.ms) / -3600000.0; n++; sx += x; sy += b.p; sxx += x * x; sxy += x * b.p; if (now - b.ms > now - first) first = b.ms; }
  battSpanOk = n >= 3 && now - first >= 60UL * 60000UL;
  if (!battSpanOk || n * sxx - sx * sx <= 0) { battRate = NAN; battLeftH = NAN; return; }
  battRate = (float)((n * sxy - sx * sy) / (n * sxx - sx * sx));       // % per hour
  battLeftH = battRate < -0.2f ? battPct / -battRate : NAN;
}
bool battCharging() { return battChg; }
bool battLow() { return !isnan(battV) && battV > 2.5f && battV < BATT_OFF_V && !battCharging(); }   // under 2.5 V: no cell, or no reading

uint32_t lastUsbMs = 0;
void usbTick() {                                          // once a second: plugged in or out shows straight away
  if (USB_PIN < 0 || millis() - lastUsbMs < 1000) return;
  lastUsbMs = millis();
  bool u = usbIn();
  if (u == battChg) return;
  battChg = u; redraw = true;
  if (!u) { usbOffMs = millis(); vN = 0; vI = 0; }         // the readings taken on the charger say nothing now
  battHistClear(); battPct = NAN;
}
void battTick() {
  usbTick();
  if (!battType || (lastBattMs && millis() - lastBattMs < BATT_READ_S * 1000UL)) return;
  lastBattMs = millis();
  battRead();
  if (battLow() && millis() > 20000) {                     // two readings 30 s apart, both low
    if (!battLowSince) battLowSince = millis();
    else if (millis() - battLowSince >= 25000) lowBattOff();
  } else battLowSince = 0;
}

// ======================================================================
//  GPS (Ultimate GPS FeatherWing on TX/RX) -> clock + track
// ======================================================================
struct GpsState {
  bool seen = false;             // NMEA arriving at all
  uint32_t lastNmeaMs = 0;
  bool fix = false;
  uint32_t lastFixMs = 0;
  double lat = 0, lon = 0;
  float alt = NAN, hdop = NAN, kn = NAN, cog = NAN;   // cog: course over ground, degrees true
  uint8_t sats = 0, q = 0;
  uint32_t fixEpoch = 0;
  // track logging
  uint32_t ptEpoch = 0;
  double ptLat = 0, ptLon = 0;
  bool newSeg = true;
  uint32_t ptsToday = 0;
  String ptsDay;
  uint32_t lastClockSet = 0;
} gps;
char nmea[100];
uint8_t nmeaLen = 0;
// the last good fix, kept in flash for SUN & MOON: saved when it has moved 1 km, or once an hour
struct LastFix { double lat = 0, lon = 0; uint32_t ep = 0; } lastFix, savedFix;
const char* FIX_PATH = "/hwo/fix.json";
void fixLoad() {
  File f = LittleFS.open(FIX_PATH, FILE_READ); if (!f) return;
  JsonDocument d; DeserializationError e = deserializeJson(d, f); f.close();
  if (e || !d["lat"].is<double>() || !d["lon"].is<double>()) return;
  lastFix.lat = d["lat"]; lastFix.lon = d["lon"]; lastFix.ep = d["ep"] | 0; savedFix = lastFix;
}
void fixKeep(uint32_t ep) {
  lastFix.lat = gps.lat; lastFix.lon = gps.lon; lastFix.ep = ep;
  if (!flashOK || (savedFix.ep && ep - savedFix.ep < 3600 && distM(savedFix.lat, savedFix.lon, lastFix.lat, lastFix.lon) < 1000)) return;
  String tmp = String(FIX_PATH) + ".tmp";
  File f = LittleFS.open(tmp, FILE_WRITE); if (!f) return;
  f.printf("{\"lat\":%.5f,\"lon\":%.5f,\"ep\":%lu}", lastFix.lat, lastFix.lon, (unsigned long)ep); f.close();
  if (replaceFile(LittleFS, tmp, FIX_PATH)) savedFix = lastFix;
}

bool gpsI2C = false;
uint32_t lastGpsPoll = 0, lastGpsProbe = 0;
// GPS power. Auto (the default): asleep while the box is on the home Wi-Fi, on everywhere else. By hand, from
// the quick menu: On or Off until changed or the box restarts. A work trip keeps it on regardless. Asleep is
// the PA1010D's standby (PMTK161,0); any byte on its bus wakes it, and it gets a fix again within seconds.
uint8_t gpsMode = 0;                       // 0 auto, 1 on by hand, 2 off by hand
bool gpsAsleep = false;
void gpsSend(const char* body) {               // adds $ and the checksum
  uint8_t c = 0; for (const char* p = body; *p; p++) c ^= *p;
  char b[100]; snprintf(b, sizeof b, "$%s*%02X\r\n", body, c);
  Wire.beginTransmission(GPS_ADDR); Wire.write((const uint8_t*)b, strlen(b)); Wire.endTransmission();
}
void gpsStart() {
  Wire.beginTransmission(GPS_ADDR);
  gpsI2C = (Wire.endTransmission() == 0);
  lastGpsProbe = millis();
  if (!gpsI2C) return;
  delay(20);
  gpsSend("PMTK314,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0");   // RMC + GGA only
  gpsSend("PMTK220,1000");                                    // 1 Hz
}

double nmeaDeg(const char* v, const char* hemi) {   // ddmm.mmmm -> degrees
  if (!*v) return NAN;
  double x = atof(v); int deg = (int)(x / 100); double d = deg + (x - deg * 100) / 60.0;
  return (*hemi == 'S' || *hemi == 'W') ? -d : d;
}
uint32_t civilEpoch(int y, int mo, int d, int h, int mi, int se) {
  y -= mo <= 2;
  int era = y / 400, yoe = y - era * 400, doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long days = era * 146097L + yoe * 365L + yoe / 4 - yoe / 100 + doy - 719468L;
  return (uint32_t)(days * 86400L + h * 3600L + mi * 60L + se);
}
double distM(double a1, double o1, double a2, double o2) {
  const double R = 6371000.0, k = PI / 180.0;
  double dA = (a2 - a1) * k, dO = (o2 - o1) * k;
  double h = sin(dA / 2) * sin(dA / 2) + cos(a1 * k) * cos(a2 * k) * sin(dO / 2) * sin(dO / 2);
  return 2 * R * asin(sqrt(h));
}

fs::FS* gpsFS() { return sdOK ? (fs::FS*)&SD : (flashOK ? (fs::FS*)&LittleFS : nullptr); }

void gpsLogPoint(uint32_t ep) {
  fs::FS* fs = gpsFS();
  if (!fs) return;
  if (fs == &LittleFS && LittleFS.totalBytes() - LittleFS.usedBytes() < 200 * 1024) return;   // keep room for the log
  String day = trackDate(ep);
  if (!fs->exists("/gps")) fs->mkdir("/gps");
  String path = "/gps/" + day + ".csv";
  bool isNew = !fs->exists(path);
  File f = fs->open(path, FILE_APPEND);
  if (!f) { if (fs == &SD) sdOK = false; return; }
  if (isNew) f.println("utc,lat,lon,alt_m,kn,sats,hdop,seg");
  tripAddPoint(ep, gps.lat, gps.lon);
  f.print(isoUtc(ep)); f.print(','); f.print(String(gps.lat, 6)); f.print(','); f.print(String(gps.lon, 6)); f.print(',');
  f.print(csvNum(gps.alt, 1)); f.print(','); f.print(csvNum(gps.kn, 1)); f.print(','); f.print(gps.sats); f.print(',');
  f.print(csvNum(gps.hdop, 1)); f.print(','); f.println(gps.newSeg ? 1 : 0);
  f.close();
  if (day != gps.ptsDay) { gps.ptsDay = day; gps.ptsToday = 0; }
  gps.ptsToday++;
  gps.ptEpoch = ep; gps.ptLat = gps.lat; gps.ptLon = gps.lon; gps.newSeg = false;
}

void gpsSentence(char* line) {
  // checksum
  char* star = strrchr(line, '*');
  if (!star || line[0] != '$') return;
  uint8_t c = 0; for (char* p = line + 1; p < star; p++) c ^= *p;
  if (strtol(star + 1, nullptr, 16) != c) return;
  *star = 0;
  char* f[20]; int n = 0;
  for (char* p = line; n < 20; ) { f[n++] = p; char* q = strchr(p, ','); if (!q) break; *q = 0; p = q + 1; }
  gps.seen = true; gps.lastNmeaMs = millis();
  const char* id = f[0] + 3;
  if (!strcmp(id, "GGA") && n >= 10) {
    gps.q = atoi(f[6]); gps.sats = atoi(f[7]);
    gps.hdop = *f[8] ? atof(f[8]) : NAN;
    gps.alt = *f[9] ? atof(f[9]) : NAN;
  } else if (!strcmp(id, "RMC") && n >= 10) {
    bool ok = f[2][0] == 'A' && strlen(f[1]) >= 6 && strlen(f[9]) == 6;
    if (!ok) { gps.fix = false; return; }
    if (millis() - gps.lastFixMs > 10000) gps.newSeg = true;      // fix was lost: the next point starts a new line
    gps.fix = true; gps.lastFixMs = millis();
    gps.lat = nmeaDeg(f[3], f[4]); gps.lon = nmeaDeg(f[5], f[6]);
    gps.kn = *f[7] ? atof(f[7]) : NAN;
    gps.cog = *f[8] ? atof(f[8]) : NAN;
    const char *t = f[1], *d = f[9];
    uint32_t ep = civilEpoch(2000 + (d[4] - '0') * 10 + (d[5] - '0'), (d[2] - '0') * 10 + (d[3] - '0'), (d[0] - '0') * 10 + (d[1] - '0'),
                             (t[0] - '0') * 10 + (t[1] - '0'), (t[2] - '0') * 10 + (t[3] - '0'), (t[4] - '0') * 10 + (t[5] - '0'));
    gps.fixEpoch = ep;
    // clock: GPS wins over the RTC and the browser; NTP (house Wi-Fi) wins over GPS
    bool ntpFresh = !strcmp(timeSrc, "ntp") && lastNtpSync && millis() - lastNtpSync < 6UL * 3600000UL;
    if (ep > 1735689600UL && !ntpFresh && !cfg.clkMan) {        // Manual: only you set the clock
      long diff = (long)ep - (long)time(nullptr);
      if (!timeValid || ((diff > 1 || diff < -1) && (!gps.lastClockSet || millis() - gps.lastClockSet > 600000UL)) || strcmp(timeSrc, "gps")) {
        setSystemUtc((time_t)ep); writeRtc((time_t)ep);
        if (!timeValid || diff > 60 || diff < -60) lastEnvSlot = 0;
        timeValid = true; timeSrc = "gps"; gps.lastClockSet = millis();
      }
    }
    // track
    if (!timeValid || isnan(gps.lat) || isnan(gps.lon) || gps.q == 0) return;
    if (!isnan(gps.hdop) && gps.hdop > GPS_MAX_HDOP) return;
    uint32_t now = (uint32_t)time(nullptr);
    fixKeep(now);
    double m = gps.ptEpoch ? distM(gps.ptLat, gps.ptLon, gps.lat, gps.lon) : 1e9;
    if (gps.newSeg || !gps.ptEpoch || (now - gps.ptEpoch >= GPS_LOG_S[cfg.gpsDet] && m >= GPS_MOVE_M[cfg.gpsDet]) || now - gps.ptEpoch >= GPS_STILL_S[cfg.gpsDet]) gpsLogPoint(now);
  }
}

// The PA1010D answers I2C reads with its NMEA stream, padded with 0x0A when it has nothing new
void gpsTick() {
  if (gpsAsleep) return;                   // standby: reading it would wake it
  if (!gpsI2C) { if (millis() - lastGpsProbe > 10000) gpsStart(); return; }
  if (millis() - lastGpsPoll < 80) return;
  lastGpsPoll = millis();
  for (int chunk = 0; chunk < 8; chunk++) {
    int n = Wire.requestFrom(GPS_ADDR, (uint8_t)32);
    if (n <= 0) { if (millis() - gps.lastNmeaMs > 15000) { gpsI2C = false; lastGpsProbe = millis(); } return; }
    bool any = false;
    while (Wire.available()) {
      char c = Wire.read();
      if (c != 0x0A) any = true;
      if (c == '\n' || c == '\r') { if (nmeaLen) { nmea[nmeaLen] = 0; gpsSentence(nmea); nmeaLen = 0; } }
      else if ((uint8_t)c >= 0x20 && nmeaLen < sizeof(nmea) - 1) nmea[nmeaLen++] = c;
      else if ((uint8_t)c >= 0x20) nmeaLen = 0;
    }
    if (!any) break;
  }
  if (gps.fix && millis() - gps.lastFixMs > 5000) gps.fix = false;
}

// ======================================================================
//  Wi-Fi / ESP-NOW
// ======================================================================
void onWiFiEvent(WiFiEvent_t ev) {
  if (ev == ARDUINO_EVENT_WIFI_STA_DISCONNECTED && staUp) staLost = true;
}

bool apForce = false;                    // the quick menu's Hotspot on: up until the box restarts, whatever Settings says
bool apWanted() { return !staUp || !cfg.apHomeOff || apForce; }
void apApply() {                         // the hotspot on or off as it should be now (on the house Wi-Fi's channel when joined)
  if (apWanted()) apUp = WiFi.softAP(cfg.apSsid.c_str(), cfg.apPass.c_str(), staUp ? WiFi.channel() : AP_CHANNEL);
  else if (apUp) { WiFi.softAPdisconnect(false); apUp = false; }
}
void ntpApply() {                        // internet time only while the clock source is Automatic
  if (staUp && !cfg.clkMan) { sntp_set_time_sync_notification_cb(timeSyncCb); configTzTime(cfg.tz.c_str(), "pool.ntp.org", "time.nist.gov"); }
  else { if (esp_sntp_enabled()) esp_sntp_stop(); setenv("TZ", cfg.tz.c_str(), 1); tzset(); }
}
void onStaConnected() {
  staUp = true;
  apApply();
  ntpApply();
}

bool tryHomeWiFi(uint32_t ms) {
  WiFi.begin(cfg.homeSsid.c_str(), cfg.homePass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < ms) { delay(20); server.handleClient(); processRx(); screenTick(); }   // knob, button and alarms keep working
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.disconnect(false);                  // stop retrying: retries hop channels and break the hotspot
  return false;
}

bool homeInScan(int n) {
  for (int i = 0; i < n; i++) if (WiFi.SSID(i) == cfg.homeSsid) return true;
  return false;
}

void startWiFi() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);                    // modem sleep drops ESP-NOW packets
  WiFi.setAutoReconnect(false);            // leaving home must not make it hop channels forever
  WiFi.onEvent(onWiFiEvent);
  apUp = WiFi.softAP(cfg.apSsid.c_str(), cfg.apPass.c_str(), AP_CHANNEL);   // hotspot first: usable within seconds anywhere
  staUp = false;
  if (cfg.homeSsid.length()) {
    int n = WiFi.scanNetworks(false, false, false, 120);
    bool home = n > 0 && homeInScan(n);
    WiFi.scanDelete();
    if (home && tryHomeWiFi(15000)) onStaConnected();
  }
  lastScan = millis();
  mdnsStart();
}

void mdnsStart() { MDNS.end(); if (MDNS.begin(cfg.mdns.c_str())) MDNS.addService("http", "tcp", 80); }

void startEspNow() {
  if (esp_now_init() != ESP_OK) { Serial.println("ESP-NOW init failed"); return; }
  esp_now_register_recv_cb(onEspNowRecv);
}

// Away from home: look for the house Wi-Fi now and then (less often while a phone is on the hotspot,
// since a scan briefly takes the radio off the hotspot's channel). Back in range: join it.
void wifiTick() {
  if (staLost) {
    staLost = false; staUp = false;
    WiFi.disconnect(false);
    Serial.println("Left house Wi-Fi - hotspot only");
    if (!apUp) apApply();                // the hotspot was off at home: back on, or the box can't be reached
    lastScan = millis(); scanEvery = 45000;
  }
  if (cfgApplyAt && millis() > cfgApplyAt) {          // settings changed from the dashboard (after its reply went out)
    cfgApplyAt = 0;
    setenv("TZ", cfg.tz.c_str(), 1); tzset();
    if (cfgApplyAp) { cfgApplyAp = false; apApply(); }
    if (cfgApplyMdns) { cfgApplyMdns = false; mdnsStart(); }
    if (cfgApplySta) { cfgApplySta = false; if (staUp) { staUp = false; WiFi.disconnect(false); } lastScan = 0; scanEvery = 45000; }
  }
  if (staUp || !cfg.homeSsid.length()) return;
  if (scanning) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    scanning = false;
    bool home = n > 0 && homeInScan(n);
    WiFi.scanDelete();
    lastScan = millis();
    if (home) {
      if (tryHomeWiFi(15000)) { onStaConnected(); scanEvery = 45000; Serial.println("Joined house Wi-Fi"); }
      else { apApply(); scanEvery = min<uint32_t>(scanEvery * 2, 1800000UL); }   // wrong password? back off
    }
    return;
  }
  uint32_t every = WiFi.softAPgetStationNum() > 0 ? max<uint32_t>(scanEvery, 180000UL) : scanEvery;
  if (millis() - lastScan < every) return;
  scanning = (WiFi.scanNetworks(true, false, false, 120) == WIFI_SCAN_RUNNING);
  if (!scanning) lastScan = millis();
}

// ======================================================================
//  Screen (1.5" 128x128 grayscale OLED, Adafruit 4741, SSD1327, STEMMA QT, I2C 0x3D) + knob + button
//  + buzzer (piezo 1739)
//  Screens: LIFE LOGGER (home: status lines, timers and alarms, then a menu of the other screens) and
//    CHECKLIST, TODAY, NOTES, WORK ORDERS, JOB TIMER, EXPIRING, MAP, BUSINESS / PERSONAL CARD and JOIN,
//    in the order picked on the dashboard (Settings > The box's screens).
//  Knob (rotary encoder 5880, 24 clicks a turn, with a push switch):
//    turn  = move one row per click
//    press = on LIFE LOGGER, open the screen picked in the menu; tick/untick the highlighted checklist item
//            (it ticks what's nested under it, unticking unticks what it's nested under, as on the phone);
//            open a note, work order or TODAY item (press again for the list); JOB TIMER: pause / stop /
//            start the picked job or pinned project; MAP: tick Work trip, or save a spot
//    turning while it's held is a turn, not a press; hold 1.5 s = screen off
//  Button (lit): press = back (an open note, work order or event to its list, then LIFE LOGGER). On LIFE
//    LOGGER, hold it and turn the knob to set the brightness (kept on the box).
//  Button lights: as bright as the screen, dim while it sleeps. When something is done (a tick, a start, a
//    saved spot) only the button that did it blinks; both flash for an alarm. Settings > The box can turn
//    them off altogether (alarms then only sound or vibrate).
//  The screen goes to sleep after cfg.scrOffMin minutes with nothing touched (0 = never; set from the
//  dashboard). When it is asleep, the first turn or press only wakes it. Alarms wake it.
//  Without the encoder: button press = next screen, hold = what a knob press does; the screen still
//  sleeps and wakes.
//  Alarms: kitchen timers (several at once, set from the dashboard), find-my-box, and the BNWAS move
//  reminder. While one is going, a press of the button or the knob silences it and does nothing else.
//  Every press, and every turn of the knob, also restarts the BNWAS period, like the reset button on a bridge.
// ======================================================================
class LbOled : public Adafruit_SSD1327 {           // exposes the frame buffer so only changed rows are sent
 public:
  LbOled() : Adafruit_SSD1327(128, 128, &Wire, -1, 400000, 400000) {}
  uint8_t* buf() { return buffer; }
  void win(int16_t y1, int16_t y2) { window_x1 = 0; window_x2 = WIDTH - 1; window_y1 = y1; window_y2 = y2; }
};
LbOled oled;
const uint8_t OLED_ADDR = 0x3D;
const uint8_t OLED_ROT  = 0;                      // 0-3: turns the picture to suit how the screen is mounted
const uint16_t C_ON = 15, C_DIM = 5, C_OFF = 0;    // grey levels (0-15)
// BACK (558, lower left) on pin 11, SELECT (558, upper right) on pin 9, their LED rings on A0 and A1: the
// defaults; Settings > Pin mapping can move them (hp, above)
const uint16_t BOTH_MS = 3000;                    // hold both buttons this long: the "both" shortcut
const uint8_t LED_DIM  = 10;                      // light while the screen sleeps (0-255)
const uint8_t ENC_ADDRS[] = { 0x37, 0x38, 0x39 }; // encoder 5880: A0 closed = 0x37, A1 = 0x38, both = 0x39
                                                  // (0x36, all open, is the Feather's MAX17048 fuel gauge: only tried without one)
const int8_t  ENC_INT_PIN = -1;                   // its INT pad, if wired (e.g. 12); -1 = just read it every 20 ms
const uint8_t ENC_SW = 24;                        // the encoder's push switch, on the seesaw chip
const bool    ENC_REVERSE = false;                // scrolls the wrong way? set true
const uint16_t SEL_OFF_MS = 1500;                 // hold SELECT this long: screen off
int8_t BUZ_A = 5, BUZ_B = 6;                      // piezo (1739) between pins 5 and 6, driven in opposite phase (Pin mapping; B -1: A to GND)
const uint16_t BUZ_HZ = 2650;                     // the piezo's loudest note
const uint16_t HOLD_MS = 600;
const int ROWS = 13;                              // text rows under the title bar

enum { S_STATUS, S_CL, S_TODAY, S_NOTES, S_TRIP, S_JOIN, S_WO, S_CARD, S_JOB, S_EXP, S_PCARD, S_CLOCK, S_SUN, S_BARO, S_COUNT };
const char* SCR_KEY[] = { "status", "cl", "today", "notes", "map", "join", "wo", "card", "job", "exp", "pcard", "clock", "sun", "baro" };   // names in the dashboard's list
const char* SCR_MENU[] = { "", "Checklist", "Today", "Notes", "Map today", "Join (Wi-Fi)", "Work orders", "Business card", "Job timer", "Expiring", "Personal card", "Clock", "Sun & Moon", "Barograph" };
uint8_t scrOrd[S_COUNT] = { S_STATUS, S_CL, S_TODAY, S_NOTES, S_WO, S_JOB, S_EXP, S_TRIP, S_SUN, S_BARO, S_JOIN, S_CLOCK };   // until the dashboard says otherwise
int scrN = 12;                                           // scrOrd[0] is always LIFE LOGGER; the rest are its menu
struct BoxCard { std::vector<String> lines; std::vector<uint8_t> logo; int w = 0, h = 0; };
BoxCard cards[2];                                        // BUSINESS and PERSONAL cards, already fitted to 21 characters; logo 4-bit grey, two pixels a byte
std::vector<String> lbKeys, mapKeys;                     // the lines on LIFE LOGGER and MAP (empty: the usual ones)
bool lbSet = false, mapSet = false;                      // the dashboard sent a list (it may be empty)
int yOff = 0;                                            // LIFE LOGGER pushes the rows after a dividing line down a little
String fSpd = "mph", fDist = "mi", fPos = "dd", fTemp = "F", fPres = "mmHg";
float presIn(float hpa) { return fPres == "hPa" ? hpa : fPres == "inHg" ? hpa * 0.0295300f : hpa * 0.750062f; }
int presDec() { return fPres == "inHg" ? 2 : 1; }
uint32_t mqT0 = 0; bool mqOn = false;                     // the sliding (marquee) highlighted line
bool oledOK = false, screenOn = false, bright = false;
Adafruit_seesaw enc;
bool encOK = false; uint8_t encAddr = 0;
struct EncSw { bool down, longDone, swallow, turned, woke; uint32_t t0; } encSw = {};   // the knob's push: ACTION
uint32_t lastInputMs = 0;   // last turn or press, for the sleep timer
uint8_t scr = S_STATUS;
uint32_t lastDrawMs = 0, msgUntil = 0, ledBlinkUntil = 0, lastEncMs = 0;
uint8_t inputSrc = 0, ledBlinkMask = 0;                    // the button being handled: 1 BACK, 2 SELECT, 3 both, 0 the knob or the dashboard
void ledBlink(uint32_t ms) { ledBlinkUntil = millis() + ms; ledBlinkMask = inputSrc; }   // blinks only the button that did it
String msg;
int sel = 0;
int selMem[S_COUNT] = { 0 };
String clSelId;   // the checklist item last highlighted
bool redraw = true;

struct Btn { uint8_t pin; bool down, held, raw, swallow, paired, woke; uint32_t t0, chg; };   // paired: the other button was down too; woke: its press only woke the screen
Btn btn = { 0 }, btn2 = { 0 }, xbtn[4] = {};                // xbtn: the extra buttons (Pin mapping)
// shortcuts, as the dashboard sets them: btnP for an ACTION (knob) press, btnB for BACK + SELECT held 3 s
String btnP = "job", btnB = "phone";
// ACTION's quick menu: hold the knob in and it opens (on Close) and stays open; turn to a row, press the knob
// (or SELECT) to run it, BACK to close. Which rows, in what order: Settings > The box's screen.
std::vector<String> qmKeys = { "screen", "gps", "job", "trip", "mark", "bnwas", "phone", "clock" };
bool qmOn = false; int qmSel = 0, qmTop = 0;
bool csOn = false; int csF = 0; int csV[5];              // SET CLOCK: the field being changed, and year month day hour minute
bool bothDone = false;                                   // both held long enough: their releases do nothing else
uint32_t findPhoneEp = 0;                                // when the box last asked the phones to sound (the dashboard polls it)

// ---- checklist rows (from the same file the dashboard edits) ----
struct ClRow { String id, text; uint8_t depth; bool done; };
std::vector<ClRow> clRows;
uint32_t clVer = 0xFFFFFFFF;
String clDay, clTitle;
int clTotal = 0, clDoneN = 0;   // the whole list, hidden items included
const char* CL_PATH = "/hwo/doc/checklist.json";
// ---- today's events and coming expiries (a summary the dashboard keeps on the box) ----
std::vector<String> todayRows;
uint32_t todayVer = 0xFFFFFFFF;
String todayFor, todayNote;
std::vector<TodayEv> todayEvs;
int todayOpen = -1, todayListSel = 0;                     // the event being read (-1: the list)
bool todayHaveDays = false;
std::vector<ExpRow> expRows;
// ---- JOB TIMER: what to start (projects pinned on the dashboard, then active jobs), by id ----
std::vector<String> jobIds, jobNames; std::vector<uint8_t> jobBill;
// ---- pinned notes (in the same file, already wrapped to the screen's 21 characters) ----
struct BoxNote { String title; std::vector<String> lines; };
std::vector<BoxNote> boxNotes;
int noteOpen = -1, noteListSel = 0;   // the note being read (-1: the list), and where the list was
struct BoxWo { String no, title; std::vector<String> lines; };   // description (wrapped) then numbered steps
std::vector<BoxWo> boxWos;
int woOpen = -1, woListSel = 0;
// ---- trip (today) ----
String tripDay;
int spotsToday = 0;
bool boxH12 = false;                                      // header clock: 12 h if the dashboard says so
double tripDistM = 0;
uint32_t tripFixS = 0;

String ascii(const String& in) {            // the OLED font is plain ASCII
  String o; o.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    uint8_t c = in[i];
    if (c < 0x80) { o += (c < 0x20) ? ' ' : (char)c; continue; }
    if ((c & 0xE0) == 0xC0) i += 1; else if ((c & 0xF0) == 0xE0) i += 2; else if ((c & 0xF8) == 0xF0) i += 3;
    o += '?';
  }
  return o;
}
String fit(const String& s, int n) { return (int)s.length() <= n ? s : s.substring(0, n - 1) + "~"; }
String shortDate(const String& d) {          // 2026-11-17 -> Nov 17
  static const char* M[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
  int m = d.substring(5, 7).toInt(); return String(m >= 1 && m <= 12 ? M[m - 1] : "?") + " " + String(d.substring(8, 10).toInt());
}
long dayNum(const String& d) {               // YYYY-MM-DD -> days since 1970 (civil calendar)
  int y = d.substring(0, 4).toInt(), m = d.substring(5, 7).toInt(), dd = d.substring(8, 10).toInt();
  y -= m <= 2; long era = (y >= 0 ? y : y - 399) / 400; unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + dd - 1; unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (long)doe - 719468;
}


// ======================================================================
//  JOB TIMER: the time entries (Clockify-style) the dashboard's Time tab keeps in the log.
//  The box keeps the recent ones in memory: the log is read once at start-up (only the lines about time
//  and undos from the last JOB_DAYS days), then each new line is applied as it is written. An undo
//  re-reads just those lines. A press on the JOB TIMER screen writes the same time.set events as the
//  dashboard's Start / Pause / Stop.
// ======================================================================
const int JOB_DAYS = 62;
std::vector<TmE> tms;
std::vector<LogMark> jobMarks;
bool jobDirty = true;
String jobCut;                                            // log lines dated before this aren't kept

int64_t nowMs() { struct timeval tv; gettimeofday(&tv, nullptr); return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000; }
String b36(int64_t v) { String o; if (v <= 0) return "0"; while (v) { int d = v % 36; o = (char)(d < 10 ? '0' + d : 'a' + d - 10) + o; v /= 36; } return o; }
String i64s(int64_t v) { char b[24]; snprintf(b, sizeof b, "%lld", (long long)v); return b; }
int64_t tmParse(JsonVariantConst v) {                     // ms, or "2026-11-17T09:00" (local), or ...Z / ...-08:00
  if (v.isNull()) return 0;
  if (v.is<double>() || v.is<long long>()) return (int64_t)v.as<double>();
  const char* s = v | ""; size_t n = strlen(s);
  if (n < 10 || s[4] != '-' || s[7] != '-') return 0;
  int y = atoi(s), mo = atoi(s + 5), d = atoi(s + 8), h = n >= 16 ? atoi(s + 11) : 0, mi = n >= 16 ? atoi(s + 14) : 0, se = n >= 19 && s[16] == ':' ? atoi(s + 17) : 0;
  const char* z = strlen(s) > 16 ? s + 16 : "";
  while (*z == ':' || isdigit((unsigned char)*z) || *z == '.') z++;
  if (*z == 'Z') return (int64_t)civilEpoch(y, mo, d, h, mi, se) * 1000;
  if (*z == '+' || *z == '-') { int oh = atoi(z + 1), om = strlen(z) >= 6 ? atoi(z + 4) : 0; int64_t off = (oh * 60 + om) * 60; return ((int64_t)civilEpoch(y, mo, d, h, mi, se) - (*z == '+' ? off : -off)) * 1000; }
  struct tm t = {}; t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_sec = se; t.tm_isdst = -1;
  return (int64_t)mktime(&t) * 1000;
}
TmE* tmFind(const String& id) { for (auto& x : tms) if (x.id == id) return &x; return nullptr; }
void jobApply(JsonObjectConst e) {                        // the same as the dashboard's time.set / time.del
  const char* op = e["op"] | ""; String id = e["id"] | "";
  if (!id.length()) return;
  TmE* x = tmFind(id);
  if (!strcmp(op, "time.del")) { if (x) x->del = true; return; }
  if (strcmp(op, "time.set")) return;
  if (!x) {
    if (tms.size() >= 400) {                              // full: drop the oldest finished one
      int k = -1; for (size_t i = 0; i < tms.size(); i++) if (tms[i].end && (k < 0 || tms[i].start < tms[k].start)) k = i;
      if (k < 0) return; tms.erase(tms.begin() + k);
    }
    tms.push_back(TmE()); x = &tms.back(); x->id = id;
  }
  for (JsonPairConst kv : e) {
    const char* k = kv.key().c_str(); JsonVariantConst v = kv.value();
    if (!strcmp(k, "desc")) x->desc = fit(ascii(v | ""), 60);
    else if (!strcmp(k, "prj")) x->prj = v | "";
    else if (!strcmp(k, "wo")) x->wo = v | "";
    else if (!strcmp(k, "bill")) x->bill = v | false;
    else if (!strcmp(k, "paused")) x->paused = v | false;
    else if (!strcmp(k, "start")) x->start = tmParse(v);
    else if (!strcmp(k, "end")) x->end = tmParse(v);
  }
  x->del = false;
}
void jobScanStart() {
  jobMarks.clear(); tms.clear(); jobDirty = true;
  jobCut = timeValid ? localDate(time(nullptr) - JOB_DAYS * 86400L) : String("");
}
void jobScanLine(const char* head, uint32_t off) {       // {"seq":12,"ts":"2026-11-20T19:22:00-08:00","e":{"op":"time.set",...
  bool tm = strstr(head, "\"op\":\"time.") != nullptr, un = !tm && strstr(head, "\"op\":\"undo\"") != nullptr;
  if (!tm && !un) return;
  const char* sq = strstr(head, "\"seq\":"); if (!sq) return;
  const char* ts = strstr(head, "\"ts\":\"");
  if (jobCut.length() && ts && strncmp(ts + 6, jobCut.c_str(), 10) < 0) return;   // older than JOB_DAYS
  if (jobMarks.size() >= 4000) jobMarks.erase(jobMarks.begin());
  jobMarks.push_back({ off, (uint32_t)strtoul(sq + 6, nullptr, 10), un });
}
void jobScanDone() { jobDirty = true; }
// a new line of the log, as it is written: time entries go straight in; an undo means reading them again
void jobNote(const String& line, uint32_t off, uint32_t seq) {
  bool tm = line.indexOf("\"op\":\"time.") >= 0, un = !tm && line.indexOf("\"op\":\"undo\"") >= 0;
  if (!tm && !un) return;
  if (jobMarks.size() >= 4000) jobMarks.erase(jobMarks.begin());
  jobMarks.push_back({ off, seq, un });
  if (un || jobDirty) { jobDirty = true; return; }
  JsonDocument d; if (!deserializeJson(d, line)) jobApply(d.as<JsonObjectConst>());
  redraw = true;
}
void jobRebuild() {
  jobDirty = false; tms.clear();
  if (!store) return;
  File f = store->open(LOG_PATH, FILE_READ); if (!f) return;
  std::vector<uint32_t> undone;                           // newest first: an undo that was itself undone (a redo) voids nothing
  auto isUndone = [&](uint32_t q) { for (uint32_t u : undone) if (u == q) return true; return false; };
  for (int i = (int)jobMarks.size() - 1; i >= 0; i--) {
    if (!jobMarks[i].undo || isUndone(jobMarks[i].seq)) continue;
    f.seek(jobMarks[i].off); JsonDocument d;
    if (deserializeJson(d, f)) continue;
    for (JsonVariant q : d["e"]["seqs"].as<JsonArray>()) if (undone.size() < 2000) undone.push_back(q.as<uint32_t>());
  }
  for (auto& m : jobMarks) {
    if (m.undo || isUndone(m.seq)) continue;
    f.seek(m.off); JsonDocument d;
    if (deserializeJson(d, f)) continue;
    jobApply(d["e"].as<JsonObjectConst>());
  }
  f.close();
}
JobSt jobState() {
  if (jobDirty) jobRebuild();
  JobSt j; int ended = -1; int64_t now = nowMs();
  time_t t = time(nullptr); struct tm tm; localtime_r(&t, &tm); tm.tm_hour = tm.tm_min = tm.tm_sec = 0; int64_t mid = (int64_t)mktime(&tm) * 1000;
  for (int i = 0; i < (int)tms.size(); i++) {
    const TmE& x = tms[i]; if (x.del || !x.start) continue;
    if (!x.end) { if (j.run < 0 || x.start > tms[j.run].start) j.run = i; }
    else if (ended < 0 || x.end > tms[ended].end) ended = i;
    if (x.start >= mid) { int64_t e = x.end ? x.end : now; if (e > x.start) j.todayS += (uint32_t)((e - x.start) / 1000); }
  }
  j.pz = (j.run < 0 && ended >= 0 && tms[ended].paused) ? ended : -1;
  j.last = j.run >= 0 ? j.run : j.pz >= 0 ? j.pz : ended;
  return j;
}
String jobName(const String& id) { for (size_t i = 0; i < jobIds.size(); i++) if (jobIds[i] == id) return jobNames[i]; return id.length() ? "Job" : "No project"; }
std::vector<JobOpt> jobOpts() {
  JobSt j = jobState(); std::vector<JobOpt> o;
  if (j.run >= 0) { o.push_back({ 'P', -1, "Pause" }); o.push_back({ 'S', -1, "Stop" }); }
  else if (j.pz >= 0) { o.push_back({ 'R', -1, "Resume" }); o.push_back({ 'S', -1, "Stop" }); }
  for (int i = 0; i < (int)jobIds.size(); i++) if (!(j.run >= 0 && tms[j.run].prj == jobIds[i])) o.push_back({ 'G', i, String(j.run >= 0 ? "Switch: " : "Start: ") + jobNames[i] });
  if (!jobIds.size() && j.run < 0 && j.pz < 0 && j.last >= 0) o.push_back({ 'A', -1, "Start: " + (tms[j.last].desc.length() ? tms[j.last].desc : jobName(tms[j.last].prj)) });
  return o;
}
void jobPress(int s) {
  std::vector<JobOpt> O = jobOpts();
  if (s < 0 || s >= (int)O.size()) return;
  if (!timeValid || !store) { msg = "Clock not set"; msgUntil = millis() + 1500; redraw = true; return; }
  JobSt j = jobState(); const JobOpt o = O[s]; int64_t t = nowMs(); String evs;
  auto add = [&](const String& e) { evs += (evs.length() ? "\n" : "") + e; };
  auto endRun = [&](bool pz) { add("{\"op\":\"time.set\",\"id\":" + jStr(tms[j.run].id) + ",\"end\":" + i64s(t) + (pz ? ",\"paused\":true}" : "}")); };
  auto start = [&](const String& prj, const String& desc, bool bill, const String& wo) {
    if (j.run >= 0) endRun(false);
    if (j.pz >= 0) add("{\"op\":\"time.set\",\"id\":" + jStr(tms[j.pz].id) + ",\"paused\":false}");
    add("{\"op\":\"time.set\",\"id\":\"tm-b" + b36(t) + "\",\"desc\":" + jStr(desc) + ",\"prj\":" + jStr(prj) + ",\"bill\":" + jBool(bill) + ",\"wo\":" + jStr(wo) + ",\"start\":" + i64s(t) + "}");
  };
  if (o.a == 'P' && j.run >= 0) endRun(true);
  else if (o.a == 'S') { if (j.run >= 0) endRun(false); else if (j.pz >= 0) add("{\"op\":\"time.set\",\"id\":" + jStr(tms[j.pz].id) + ",\"paused\":false}"); }
  else if ((o.a == 'R' && j.pz >= 0) || (o.a == 'A' && j.last >= 0)) { const TmE& x = tms[o.a == 'R' ? j.pz : j.last]; start(x.prj, x.desc, x.bill, x.wo); }
  else if (o.a == 'G') {                                  // the job's or project's last description carries on; billable as the dashboard has it
    const String& id = jobIds[o.k]; int pv = -1;
    for (int i = 0; i < (int)tms.size(); i++) if (!tms[i].del && tms[i].start && tms[i].prj == id && (pv < 0 || tms[i].start > tms[pv].start)) pv = i;
    String desc = pv >= 0 ? tms[pv].desc : String(""); bool bill = pv >= 0 ? tms[pv].bill : jobBill[o.k] != 0;
    start(id, desc, bill, "");
  }
  if (!evs.length()) return;
  String ts; uint32_t first;
  if (appendEvents(evs, ts, first) > 0) { msg = o.a == 'P' ? "Paused" : o.a == 'S' ? "Stopped" : "Started"; ledBlink(400); }
  else msg = "Not saved";
  msgUntil = millis() + 1000; redraw = true;
}

// ======================================================================
//  MAP: Work trip. Tick it (in the driveway, say): from then the box adds up the distance it moves until you
//  press End on MAP, and the trip goes to the dashboard as a business trip. A long red light doesn't end it.
//  After an hour without moving it beeps and asks whether you're still on it (turn to Keep going or End,
//  press); unanswered, it stops beeping after 2 minutes and keeps counting. The trip in progress is kept in
//  flash (/hwo/trip.json), so switching the box off doesn't lose it.
// ======================================================================
const float TRIP_KN = 3.0f;                               // moving: faster than this (about 3.5 mph)
const uint32_t TRIP_BEEP_MS = 120000UL;   // the "still on it?" question comes after Settings' tripAskMin stopped
struct WorkTrip { bool on = false, have1 = false; uint32_t t1 = 0, lastMoveEp = 0; double m = 0, lat1 = 0, lon1 = 0, pla = 0, plo = 0; };
WorkTrip WT;
uint32_t tripStillMs = 0, tripAskSince = 0, lastTripWatch = 0, lastTripSave = 0;
bool tripAsk = false; int tripAskSel = 0;                 // the "still on it?" question, and Keep going (0) / End (1)
const char* TRIP_PATH = "/hwo/trip.json";
void tripSave() {
  lastTripSave = millis();
  if (!flashOK) return;
  if (!WT.on) { LittleFS.remove(TRIP_PATH); return; }
  File f = LittleFS.open(TRIP_PATH, FILE_WRITE); if (!f) return;
  f.printf("{\"t1\":%lu,\"last\":%lu,\"m\":%.1f,\"have\":%d,\"lat1\":%.6f,\"lon1\":%.6f,\"la\":%.6f,\"lo\":%.6f}", (unsigned long)WT.t1, (unsigned long)WT.lastMoveEp,
           WT.m, WT.have1 ? 1 : 0, WT.lat1, WT.lon1, WT.pla, WT.plo);
  f.close();
}
void tripResume() {                                       // at start-up: a trip that was on when the box went off carries on
  if (!flashOK || !LittleFS.exists(TRIP_PATH)) return;
  File f = LittleFS.open(TRIP_PATH, FILE_READ); JsonDocument d; DeserializationError e = deserializeJson(d, f); f.close();
  if (e || !(d["t1"] | 0)) { LittleFS.remove(TRIP_PATH); return; }
  WT.on = true; WT.t1 = d["t1"]; WT.lastMoveEp = d["last"] | WT.t1; WT.m = d["m"] | 0.0; WT.have1 = (d["have"] | 0) != 0;
  WT.lat1 = d["lat1"] | 0.0; WT.lon1 = d["lon1"] | 0.0; WT.pla = d["la"] | 0.0; WT.plo = d["lo"] | 0.0;
  tripStillMs = millis();
}
bool gpsWanted() { return WT.on || gpsMode == 1 || (gpsMode == 0 && !staUp); }
void gpsNote(bool asleep) {                // the main log keeps when the GPS slept, so the map can say why a track has a gap
  if (!timeValid || !store) return;
  String ev = String("{\"op\":\"gps.") + (asleep ? "sleep" : "wake") + "\",\"t\":\"" + isoLocal(time(nullptr)) + "\"" +
              (asleep ? String(",\"why\":\"") + (gpsMode == 2 ? "hand" : "home") + "\"" : String("")) + "}";
  String ts; uint32_t first; appendEvents(ev, ts, first);
}
uint32_t lastGpsPower = 0;
void gpsPowerTick() {
  if (millis() - lastGpsPower < 1000) return;
  lastGpsPower = millis();
  bool want = gpsWanted();
  if (!want && !gpsAsleep && gpsI2C) {
    gpsSend("PMTK161,0");
    gpsAsleep = true; gps.fix = false; gps.newSeg = true; nmeaLen = 0;
    gpsNote(true); redraw = true;
  } else if (want && gpsAsleep) {
    gpsAsleep = false;
    Wire.beginTransmission(GPS_ADDR); Wire.write((uint8_t)' '); Wire.endTransmission();   // any byte wakes it
    delay(20);
    gpsStart(); gps.lastNmeaMs = millis(); gps.newSeg = true;
    gpsNote(false); redraw = true;
  }
}
void tripStart() {
  if (!timeValid) { msg = "Clock not set"; msgUntil = millis() + 1500; redraw = true; return; }
  WT = WorkTrip(); WT.on = true; WT.t1 = time(nullptr); WT.lastMoveEp = WT.t1;
  tripStillMs = millis(); tripAsk = false; tripSave();
  msg = "Work trip on"; msgUntil = millis() + 1000; redraw = true;
}
void tripEnd(bool atLastMove) {                           // atLastMove: ended from the "still on it?" question, so it ended when it stopped moving
  tripAsk = false;
  if (!WT.on) return;
  uint32_t t2 = atLastMove ? WT.lastMoveEp : (uint32_t)time(nullptr);
  if (!WT.have1) msg = "No GPS: not saved";
  else {
    String ev = "{\"op\":\"trip.set\",\"id\":\"b" + b36((int64_t)WT.t1 * 1000) + "\",\"date\":" + jStr(localDate(WT.t1)) + ",\"t1\":" + jStr(isoLocal(WT.t1)) +
                ",\"t2\":" + jStr(isoLocal(t2)) + ",\"mi\":" + String(round(WT.m / 1609.344 * 10) / 10, 1) + ",\"gps\":true,\"src\":\"box\",\"biz\":true" +
                ",\"lat1\":" + String(WT.lat1, 6) + ",\"lon1\":" + String(WT.lon1, 6) + ",\"lat2\":" + String(WT.pla, 6) + ",\"lon2\":" + String(WT.plo, 6) + "}";
    String ts; uint32_t first;
    if (store && appendEvents(ev, ts, first) == 1) { msg = "Work trip saved"; ledBlink(400); }
    else { msg = "Trip not saved"; msgUntil = millis() + 1500; redraw = true; return; }   // stays on, to try again
  }
  WT.on = false; tripSave();
  msgUntil = millis() + 1500; redraw = true;
}
void tripWatch() {                                        // once a second while a work trip is on: add up the distance
  if (!WT.on || millis() - lastTripWatch < 1000) return;
  lastTripWatch = millis();
  bool fresh = gps.fix && millis() - gps.lastFixMs < 5000;
  if (fresh) {
    if (!WT.have1) { WT.have1 = true; WT.lat1 = WT.pla = gps.lat; WT.lon1 = WT.plo = gps.lon; }
    else {
      double d = distM(WT.pla, WT.plo, gps.lat, gps.lon); bool mv = !isnan(gps.kn) && gps.kn >= 1.0f;
      if ((mv && d >= 5) || d >= 50) { WT.m += d; WT.pla = gps.lat; WT.plo = gps.lon; }   // standing still, the fix wanders: that isn't distance
    }
    if (!isnan(gps.kn) && gps.kn >= TRIP_KN) { tripStillMs = millis(); if (timeValid) WT.lastMoveEp = time(nullptr); }
  }
  if (!tripAsk && millis() - tripStillMs > (uint32_t)cfg.tripAskMin * 60000UL) { tripAsk = true; tripAskSel = 0; tripAskSince = millis(); redraw = true; }
  if (millis() - lastTripSave > 60000) tripSave();
}
bool tripBeeping() { return tripAsk && millis() - tripAskSince < TRIP_BEEP_MS; }
void tripAnswer() {
  if (tripAskSel == 1) tripEnd(true);
  else { tripAsk = false; tripStillMs = millis(); msg = "Still on it"; msgUntil = millis() + 1000; }
  redraw = true;
}

// ---- JOIN: the phones and computers with the dashboard open (each one checks in every few seconds) ----
std::vector<Dev> devs;
void devSeen(String n) {
  n = ascii(n); n.trim(); n = n.substring(0, 24); if (!n.length()) return;
  for (auto& d : devs) if (d.n == n) { d.ms = millis(); return; }
  if (devs.size() >= 8) { size_t k = 0; for (size_t i = 1; i < devs.size(); i++) if (devs[i].ms < devs[k].ms) k = i; devs.erase(devs.begin() + k); }
  devs.push_back({ n, millis() });
}

fs::FS* clFS() { return (flashOK && LittleFS.exists(CL_PATH)) ? (fs::FS*)&LittleFS : (sdOK && SD.exists(CL_PATH)) ? (fs::FS*)&SD : nullptr; }

// JSON for the checklist parses into PSRAM (2 MB on the Feather S2), so a long list doesn't eat the
// internal RAM the Wi-Fi needs. Without PSRAM it falls back to the normal heap.
struct PsAlloc : ArduinoJson::Allocator {
  void* allocate(size_t n) override { void* p = psramFound() ? heap_caps_malloc(n, MALLOC_CAP_SPIRAM) : nullptr; return p ? p : malloc(n); }
  void deallocate(void* p) override { free(p); }
  void* reallocate(void* p, size_t n) override { void* q = psramFound() ? heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM) : nullptr; return q ? q : realloc(p, n); }
} psAlloc;
bool clReadDoc(JsonDocument& doc) {
  fs::FS* fs = clFS(); if (!fs) return false;
  File f = fs->open(CL_PATH, FILE_READ); if (!f) return false;
  DeserializationError e = deserializeJson(doc, f); f.close();
  return !e && doc["items"].is<JsonArray>();
}
// The rows in tree order. Each item's parent is found once (ids sorted, then a binary search), so a list
// of hundreds builds in a few milliseconds instead of scanning the whole list for every row.
void clWalkK(const std::vector<JsonObject>& v, const std::vector<std::vector<int>>& kids, int at, uint8_t depth) {
  for (int i : kids[at]) {
    JsonObject it = v[i];
    if (cfg.clHideDone && (bool)(it["done"] | false)) continue;   // hidden, with everything under it
    clRows.push_back({ String((const char*)(it["id"] | "")), ascii(it["text"] | ""), depth, (bool)(it["done"] | false) });
    if (depth < 6) clWalkK(v, kids, i, depth + 1);
  }
}
void clWalk(JsonArray items) {
  std::vector<JsonObject> v; for (JsonObject x : items) v.push_back(x);
  int n = v.size();
  std::vector<int> byId(n); for (int i = 0; i < n; i++) byId[i] = i;
  auto idOf = [&](int i) -> const char* { return v[i]["id"] | ""; };
  std::sort(byId.begin(), byId.end(), [&](int a, int b) { return strcmp(idOf(a), idOf(b)) < 0; });
  std::vector<std::vector<int>> kids(n + 1);   // kids[n]: the top level
  for (int i = 0; i < n; i++) {
    const char* p = v[i]["parent"] | ""; int pi = n;
    if (*p) { int lo = 0, hi = n - 1; while (lo <= hi) { int m = (lo + hi) / 2, c = strcmp(idOf(byId[m]), p); if (!c) { pi = byId[m]; break; } if (c < 0) lo = m + 1; else hi = m - 1; } }
    kids[pi == i ? n : pi].push_back(i);
  }
  clWalkK(v, kids, n, 0);
}
void clLoad() {
  if (clVer == docVer) return;
  clVer = docVer; clRows.clear(); clDay = ""; clTitle = ""; clTotal = 0; clDoneN = 0;
  JsonDocument doc(&psAlloc);
  if (!clReadDoc(doc)) return;
  clDay = doc["day"] | ""; clTitle = ascii(doc["title"] | "");
  for (JsonObject x : doc["items"].as<JsonArray>()) { clTotal++; if ((bool)(x["done"] | false)) clDoneN++; }
  clWalk(doc["items"].as<JsonArray>());
  int k = -1;   // find the remembered item again (the list may have changed on the phone)
  for (size_t i = 0; clSelId.length() && i < clRows.size(); i++) if (clRows[i].id == clSelId) { k = i; break; }
  int n = clRows.size();
  selMem[S_CL] = k >= 0 ? k : constrain(selMem[S_CL], 0, max(0, n - 1));
  if (scr == S_CL) sel = selMem[S_CL];
}
// Same as ticking it on the phone: the item and everything nested under it change together; unticking
// also unticks the items it sits under. The file changes, and linked work orders get their steps / a note.
void clToggle(int row) {
  if (row < 0 || row >= (int)clRows.size()) return;
  JsonDocument doc(&psAlloc);
  if (!clReadDoc(doc)) { msg = "Checklist not found"; msgUntil = millis() + 2000; return; }
  JsonArray items = doc["items"].as<JsonArray>();
  auto findItem = [&](const String& id) -> JsonObject { for (JsonObject x : items) if (id == (const char*)(x["id"] | "")) return x; return JsonObject(); };
  String id0 = clRows[row].id;
  JsonObject it = findItem(id0);
  if (it.isNull()) { clVer = 0xFFFFFFFF; return; }
  bool done = !(bool)(it["done"] | false);
  String today = timeValid ? localDate(time(nullptr)) : String(""), day = doc["day"] | today;
  String stamp = timeValid ? isoUtc(time(nullptr)) : String("");
  std::vector<String> ids = { id0 };
  for (size_t k = 0; k < ids.size() && ids.size() < 1000; k++)
    for (JsonObject x : items) if (ids[k] == (const char*)(x["parent"] | "")) ids.push_back(x["id"] | "");
  if (!done) {
    String par = it["parent"] | "";
    for (int g = 0; par.length() && g < 10; g++) { JsonObject p = findItem(par); if (p.isNull()) break; if ((bool)(p["done"] | false)) ids.push_back(par); par = p["parent"] | ""; }
  }
  String evs;
  for (const String& id : ids) {
    JsonObject x = findItem(id); if (x.isNull()) continue;
    bool was = x["done"] | false;
    if (was == done && id != id0) continue;               // already right
    x["done"] = done;
    if (done && today.length()) x["doneAt"] = today; else x["doneAt"] = nullptr;
    if (stamp.length()) x["updated"] = stamp;
    String wo = x["wo"] | "", step = x["step"] | "";
    if (wo.length() && step.length())
      evs += (evs.length() ? "\n" : "") + String("{\"op\":\"wo.step\",\"id\":") + jStr(wo) + ",\"step\":" + jStr(step) + ",\"done\":" + jBool(done) + ",\"date\":" + jStr(day) + ",\"text\":" + jStr(x["text"] | "") + ",\"via\":\"cl\"}";
  }
  String text = it["text"] | "", wo0 = it["wo"] | "", step0 = it["step"] | "";
  if (done && !(wo0.length() && step0.length())) {        // a note on the work order the item belongs to
    String par = it["parent"] | ""; String lw = wo0;
    for (int guard = 0; !lw.length() && par.length() && guard < 10; guard++) { JsonObject p = findItem(par); if (p.isNull()) break; lw = p["wo"] | ""; par = p["parent"] | ""; }
    if (lw.length()) evs += (evs.length() ? "\n" : "") + String("{\"op\":\"wo.note\",\"id\":") + jStr(lw) + ",\"text\":" + jStr("Checklist \xE2\x9C\x93 " + text + " (" + shortDate(day) + ")") + "}";
  }
  String out; serializeJson(doc, out);
  bool ok = false;
  if (flashOK) { ok = writeDoc(LittleFS, CL_PATH, out); if (ok && sdOK) writeDoc(SD, CL_PATH, out); }
  else if (sdOK) ok = writeDoc(SD, CL_PATH, out);
  if (!ok) { msg = "Not saved"; msgUntil = millis() + 2000; return; }
  docVer++;
  if (evs.length() && timeValid && store) { String ts; uint32_t first; appendEvents(evs, ts, first); }
  ledBlink(400);                         // the light blinks; the row itself shows the tick
  clSelId = id0;
  clLoad();
}

void todayLoad() {
  String d = timeValid ? localDate(time(nullptr)) : String("");
  if (todayVer == docVer && todayFor == d) return;
  todayVer = docVer; todayFor = d; todayRows.clear(); todayEvs.clear(); todayNote = ""; expRows.clear(); boxNotes.clear();
  const char* P = "/hwo/doc/oled.json";
  fs::FS* fs = (flashOK && LittleFS.exists(P)) ? (fs::FS*)&LittleFS : (sdOK && SD.exists(P)) ? (fs::FS*)&SD : nullptr;
  if (!fs) { todayNote = d.length() ? "Open the dashboard once" : "Clock not set yet"; return; }
  File f = fs->open(P, FILE_READ);
  JsonDocument doc; DeserializationError e = deserializeJson(doc, f); f.close();
  if (e) { todayNote = "Calendar file unreadable"; return; }
  boxH12 = doc["h12"] | false;
  if (doc["btn"].is<JsonObject>()) {
    btnP = doc["btn"]["p"] | "job"; btnB = doc["btn"]["b"] | "phone";
    if (doc["btn"]["q"].is<JsonArray>()) { qmKeys.clear(); for (JsonVariant v : doc["btn"]["q"].as<JsonArray>()) if (qmKeys.size() < 16) qmKeys.push_back(v.as<String>()); }
  }
  if (doc["scr"].is<JsonArray>()) {                      // LIFE LOGGER first, then the menu in the dashboard's order
    int n = 1; scrOrd[0] = S_STATUS;
    for (const char* k : doc["scr"].as<JsonArray>()) for (int i = 1; i < S_COUNT && n < S_COUNT; i++) if (k && !strcmp(k, SCR_KEY[i])) {
      bool dup = false; for (int j = 0; j < n; j++) if (scrOrd[j] == i) dup = true;
      if (!dup) scrOrd[n++] = i;
    }
    scrN = n;
  }
  lbKeys.clear(); mapKeys.clear(); boxWos.clear();
  for (int c = 0; c < 2; c++) {                          // the business card, then the personal one
    BoxCard& bc = cards[c]; bc = BoxCard();
    JsonObject cj = doc[c ? "pcard" : "card"];
    if (cj.isNull()) continue;
    for (JsonVariant l : cj["lines"].as<JsonArray>()) if (bc.lines.size() < 13) bc.lines.push_back(fit(ascii(l | ""), 21));
    if (cj["logo"].is<JsonArray>()) {
      int w = cj["logo"][0] | 0, h = cj["logo"][1] | 0; const char* hx = cj["logo"][2] | "";
      if (w > 0 && w <= 128 && h > 0 && h <= 64 && (int)strlen(hx) == w * h) {
        bc.logo.reserve(w * h / 2);
        for (int i = 0; i + 1 < w * h; i += 2) { char b[3] = { hx[i], hx[i + 1], 0 }; bc.logo.push_back((uint8_t)strtoul(b, nullptr, 16)); }
        bc.w = w; bc.h = h;
      }
    }
  }
  lbSet = doc["lb"].is<JsonArray>(); mapSet = doc["map"].is<JsonArray>();
  for (const char* k : doc["lb"].as<JsonArray>()) if (lbKeys.size() < 12 && k && strcmp(k, "time")) lbKeys.push_back(k);   // the title bar has the time
  for (const char* k : doc["map"].as<JsonArray>()) if (mapKeys.size() < 13) mapKeys.push_back(k ? k : "-");
  jobIds.clear(); jobNames.clear(); jobBill.clear();
  for (JsonPair kv : doc["jobN"].as<JsonObject>()) {
    if (jobIds.size() >= 30) break;
    jobIds.push_back(kv.key().c_str()); jobNames.push_back(fit(ascii(kv.value() | ""), 40));
    jobBill.push_back((int)(doc["jobB"][kv.key().c_str()] | 0) != 0);
  }
  fSpd = doc["fmt"]["spd"] | "mph"; fDist = doc["fmt"]["dist"] | "mi"; fPos = doc["fmt"]["pos"] | "dd"; fTemp = doc["fmt"]["temp"] | "F"; fPres = doc["fmt"]["pres"] | "mmHg";
  for (JsonArray x : doc["wos"].as<JsonArray>()) {
    if (boxWos.size() >= 10) break;
    BoxWo w; w.no = ascii(x[0] | ""); w.title = ascii(x[1] | "");
    JsonArray xd = x[2], xs = x[3];
    for (JsonVariant l : xd) if (w.lines.size() < 60) w.lines.push_back(fit(ascii(l | ""), 21));
    int k = 0;
    for (JsonVariant l : xs) { if (w.lines.size() >= 100) break; if (!k) { if (w.lines.size()) w.lines.push_back(""); w.lines.push_back("STEPS"); } w.lines.push_back(String(++k) + ". " + ascii(l | "")); }
    boxWos.push_back(w);
  }
  if (woOpen >= (int)boxWos.size()) { woOpen = -1; if (scr == S_WO) sel = 0; }
  for (JsonArray x : doc["exp"].as<JsonArray>()) expRows.push_back({ String((const char*)(x[0] | "")), ascii(x[1] | "") });
  for (JsonArray x : doc["notes"].as<JsonArray>()) {
    if (boxNotes.size() >= 8) break;
    BoxNote b; b.title = fit(ascii(x[0] | ""), 40);
    JsonArray lines = x[1];
    for (JsonVariant l : lines) { if (b.lines.size() >= 80) break; b.lines.push_back(fit(ascii(l | ""), 21)); }
    boxNotes.push_back(b);
  }
  if (noteOpen >= (int)boxNotes.size()) { noteOpen = -1; if (scr == S_NOTES) sel = 0; }
  todayEvs.clear(); todayHaveDays = doc["days"].is<JsonObject>();
  if (!d.length()) { todayNote = "Clock not set yet"; return; }
  String to = doc["to"] | "";
  if (to.length() && d > to) { todayNote = "Open the dashboard to load"; return; }
  JsonArray day = doc["days"][d].as<JsonArray>();
  for (JsonArray ev : day) {
    if (todayEvs.size() >= 20) break;
    TodayEv e; e.t1 = ascii(ev[0] | ""); e.t2 = ascii(ev[1] | ""); e.title = ascii(ev[2] | ""); e.label = ascii(ev[5] | "");
    for (JsonVariant l : ev[6].as<JsonArray>()) if (e.det.size() < 40) e.det.push_back(fit(ascii(l | ""), 21));
    todayRows.push_back((e.t1.length() ? e.t1 : String("all  ")) + " " + e.title);     // same width as 09:00
    todayEvs.push_back(e);
  }
  if (todayOpen >= (int)todayEvs.size()) { todayOpen = -1; if (scr == S_TODAY) sel = 0; }
  if (!todayRows.size()) todayNote = "Nothing on today";
}

// distance and time with a fix today, from today's track file (once), then kept up as points are logged
void tripLoad() {
  if (!timeValid) return;
  String d = trackDate(time(nullptr));
  if (d == tripDay) return;
  tripDay = d; tripDistM = 0; tripFixS = 0;
  String path = "/gps/" + d + ".csv";
  fs::FS* fs = (sdOK && SD.exists(path)) ? (fs::FS*)&SD : (flashOK && LittleFS.exists(path)) ? (fs::FS*)&LittleFS : nullptr;
  uint32_t n = 0;
  if (fs) {
    File f = fs->open(path, FILE_READ);
    double la0 = 0, lo0 = 0; uint32_t t0 = 0;
    while (f && f.available()) {
      String l = f.readStringUntil('\n');
      int c1 = l.indexOf(','), c2 = l.indexOf(',', c1 + 1), c3 = l.indexOf(',', c2 + 1);
      if (c1 < 0 || c3 < 0 || !l.startsWith("20")) continue;
      uint32_t t = parseUtc(l.substring(0, c1));
      double la = l.substring(c1 + 1, c2).toDouble(), lo = l.substring(c2 + 1, c3).toDouble();
      l.trim(); bool seg = l.endsWith(",1");
      if (t0 && !seg && t > t0 && t - t0 <= 400) { tripDistM += distM(la0, lo0, la, lo); tripFixS += t - t0; }
      la0 = la; lo0 = lo; t0 = t; n++;
    }
    if (f) f.close();
  }
  gps.ptsDay = d; gps.ptsToday = n;
}
void tripAddPoint(uint32_t ep, double la, double lo) {     // called just before a point is logged
  String d = trackDate(ep);
  if (d != tripDay) { tripDay = d; tripDistM = 0; tripFixS = 0; gps.ptsDay = d; gps.ptsToday = 0; spotsToday = 0; return; }   // midnight
  if (!gps.newSeg && gps.ptEpoch && ep > gps.ptEpoch && ep - gps.ptEpoch <= 400) {
    tripDistM += distM(gps.ptLat, gps.ptLon, la, lo); tripFixS += ep - gps.ptEpoch;
  }
}

// ======================================================================
//  Buzzer, vibration, timers, find-my-box, BNWAS
//  Every alert has one pattern; Settings > The box picks where it goes: the piezo, the vibration motor or
//  both (cfg.alert). With no motor fitted it always sounds, whatever the setting; find my box always sounds.
// ======================================================================
bool buzOK = false, buzOn = false;
uint32_t buzHz = 0;
Adafruit_DRV2605 drv;
bool hapOK = false, hapOn = false;
void hapStart() {
  hapOK = drv.begin();
  if (!hapOK) return;
  drv.selectLibrary(1); drv.useERM();                      // the motor disc is an ERM
  drv.setMode(DRV2605_MODE_REALTIME); drv.setRealtimeValue(0);
}
void hapSet(bool on) {                                     // the motor follows the alert's pattern as it plays
  if (!hapOK || on == hapOn) return;
  drv.setRealtimeValue(on ? 0x7F : 0); hapOn = on;
}
bool alertSound() { return !hapOK || cfg.alert != 1; }
bool alertVibe() { return hapOK && cfg.alert != 0; }
const char* ALERT_NAME[] = { "Sound", "Vibrate", "Both" };
void buzSet(uint32_t hz) {
  if (!buzOK) return;
  if (!hz) { if (buzOn) { ledcWrite(BUZ_A, 0); if (BUZ_B >= 0) ledcWrite(BUZ_B, 256); buzOn = false; } return; }   // 256 inverted = both pins low
  if (hz != buzHz) { ledcChangeFrequency(BUZ_A, hz, 8); buzHz = hz; }
  if (!buzOn) { ledcWrite(BUZ_A, 128); if (BUZ_B >= 0) ledcWrite(BUZ_B, 128); buzOn = true; }
}
void buzStart() {
  // channels 0 and 1 share one timer, so the two pins stay in step; B is inverted: twice the swing across the piezo
  BUZ_A = pinKind(hp.buzA) == 'f' ? pinNum(hp.buzA) : -1; BUZ_B = pinKind(hp.buzB) == 'f' ? pinNum(hp.buzB) : -1;
  if (BUZ_A < 0) { buzOK = false; return; }                // no buzzer (Pin mapping)
  buzOK = ledcAttachChannel(BUZ_A, BUZ_HZ, 8, 0) && (BUZ_B < 0 || (ledcAttachChannel(BUZ_B, BUZ_HZ, 8, 1) && ledcOutputInvert(BUZ_B, true)));
  buzHz = BUZ_HZ;
  if (buzOK) { ledcWrite(BUZ_A, 0); if (BUZ_B >= 0) ledcWrite(BUZ_B, 256); }
}

struct KTimer {
  uint32_t id, secs, endMs, ringSince; String name; bool ringing;
  uint32_t left(uint32_t now) const { int32_t d = (int32_t)(endMs - now); return d <= 0 ? 0 : ((uint32_t)d + 999) / 1000; }
};
std::vector<KTimer> timers;
uint32_t timerNextId = 1;
const size_t MAX_TIMERS = 8;
const uint32_t TIMER_RING_MAX_MS = 10UL * 60UL * 1000UL;   // a ringing timer gives up after 10 minutes
bool findOn = false;
uint32_t findSince = 0;
const uint32_t FIND_MAX_MS = 2UL * 60UL * 1000UL;
uint8_t bnStage = 0;                 // 0 counting down, 1 visual warning, 2 buzzer
uint32_t bnT0 = 0;                   // start of the current stage (stage 0: of the period)
const uint32_t BN_VISUAL_MS = 30000;

bool timerRinging() { for (auto& t : timers) if (t.ringing) return true; return false; }
bool alarmActive() { return bnStage > 0 || findOn || timerRinging(); }
void bnwasRestart(uint32_t now) { bnStage = 0; bnT0 = now; }
// box = the button on the box. The dashboard can stop timers and find-my-box, but only the box resets BNWAS.
void silence(bool box, uint32_t now) {
  for (size_t i = 0; i < timers.size();) { if (timers[i].ringing) timers.erase(timers.begin() + i); else i++; }
  findOn = false;
  if (box && cfg.bnwasOn) bnwasRestart(now);
  buzSet(0); hapSet(false); redraw = true;
}
uint32_t bnLeftS(uint32_t now) {
  if (!cfg.bnwasOn) return 0;
  if (bnStage == 0) { uint32_t per = (uint32_t)cfg.bnwasMin * 60000UL, el = now - bnT0; return el >= per ? 0 : (per - el + 999) / 1000; }
  if (bnStage == 1) { uint32_t el = now - bnT0; return el >= BN_VISUAL_MS ? 0 : (BN_VISUAL_MS - el + 999) / 1000; }
  return 0;
}

uint32_t beepUntil = 0;
void beepOnce() { beepUntil = millis() + 700; }
void alarmsTick(uint32_t now) {
  for (size_t i = 0; i < timers.size();) {
    KTimer& t = timers[i];
    if (!t.ringing && (int32_t)(now - t.endMs) >= 0) { t.ringing = true; t.ringSince = now; redraw = true; }
    if (t.ringing && now - t.ringSince > TIMER_RING_MAX_MS) { timers.erase(timers.begin() + i); redraw = true; continue; }
    i++;
  }
  if (findOn && now - findSince > FIND_MAX_MS) { findOn = false; redraw = true; }
  if (!cfg.bnwasOn) bnStage = 0;
  else if (bnStage == 0 && now - bnT0 >= (uint32_t)cfg.bnwasMin * 60000UL) { bnStage = 1; bnT0 = now; redraw = true; }
  else if (bnStage == 1 && now - bnT0 >= BN_VISUAL_MS) { bnStage = 2; bnT0 = now; redraw = true; }
  // sound: BNWAS buzzer first, then timers, then find-my-box
  uint32_t hz = 0;
  if (bnStage == 2) {
    uint32_t s = now - bnT0, per = s < 30000 ? 2000 : s < 60000 ? 1000 : 400, on = s < 60000 ? 300 : 250, ph = s % per;
    if (ph < on) hz = (s >= 60000 && (s / per) % 2) ? 3100 : BUZ_HZ;            // gets faster the longer it goes
  } else if (timerRinging()) {
    uint32_t ph = now % 1500;
    if (ph < 120 || (ph >= 240 && ph < 360) || (ph >= 480 && ph < 600)) hz = BUZ_HZ;
  } else if (findOn) {
    uint32_t ph = now % 1200;
    if (ph < 150) hz = BUZ_HZ; else if (ph < 300) hz = 3200;
  } else if (tripBeeping()) {                             // "still on the work trip?": two short beeps every 3 s
    uint32_t ph = now % 3000;
    if (ph < 120 || (ph >= 240 && ph < 360)) hz = BUZ_HZ;
  } else if ((int32_t)(beepUntil - now) > 0) hz = BUZ_HZ;  // the alert check
  bool finding = findOn && bnStage != 2 && !timerRinging();   // find my box always sounds: you can't hear a vibration across a room
  buzSet(alertSound() || finding ? hz : 0); hapSet(alertVibe() && hz);
}

// ---- drawing ----
static uint8_t* prevFrame = nullptr;                      // the last frame sent (8 KB, from the heap)
bool frameStale = true;
void pushFrame() {                       // sends only the rows that changed (a full frame is 8 KB of I2C)
  uint8_t* b = oled.buf(); if (!b) return;
  if (!prevFrame) prevFrame = (uint8_t*)malloc(128 * 64);
  int y1 = -1, y2 = -1;
  if (frameStale || !prevFrame) { y1 = 0; y2 = 127; }
  else for (int y = 0; y < 128; y++) if (memcmp(b + y * 64, prevFrame + y * 64, 64)) { if (y1 < 0) y1 = y; y2 = y; }
  if (y1 < 0) return;
  oled.win(y1, y2); oled.display();
  if (prevFrame) { memcpy(prevFrame, b, 128 * 64); frameStale = false; }
}
// ---- the update screen (like the VMR's): over every screen, on and at full brightness while a .lbk or .bin comes in ----
uint8_t otaSt = 0;                                       // 0 none, 1 receiving, 2 done (restarting), 3 failed (shown 15 s)
uint32_t otaDone = 0, otaTot = 0, otaEndMs = 0, otaDrawMs = 0; String otaNew;
void ctr(int y, const String& t, uint16_t col = C_ON) { String f = fit(t, 21); oled.setTextColor(col); oled.setCursor((128 - 6 * (int)f.length()) / 2, y); oled.print(f); oled.setTextColor(C_ON); }
void otaDraw(bool now) {
  if (!oledOK || (!now && millis() - otaDrawMs < 300)) return;   // a whole frame is slow over I2C: a few a second is plenty
  otaDrawMs = millis();
  if (!screenOn) screenPower(true);
  oled.setContrast(255);
  oled.clearDisplay(); oled.setTextSize(1);
  int pct = otaSt == 2 ? 100 : otaTot ? (int)min<uint64_t>(100, 100ULL * otaDone / otaTot) : 0;
  oled.fillRect(0, 0, 128, 9, C_ON); oled.setTextColor(C_OFF);
  oled.setCursor(1, 1); oled.print(otaSt == 3 ? "UPDATE FAILED" : "FIRMWARE UPDATE");
  if (otaSt != 3) { String p = String(pct) + "%"; oled.setCursor(127 - 6 * p.length(), 1); oled.print(p); }
  oled.setTextColor(C_ON);
  if (otaSt != 3) {
    ctr(18, "Life Logger");
    ctr(30, "v" FW_VERSION " --> " + (otaNew.length() ? "v" + otaNew : String(otaSt == 2 ? "new" : "...")));
    ctr(46, otaSt == 2 ? String("Restarting...") : upKind == 3 ? String("Dashboard") : upKind == 1 || upKind == 4 ? String("Firmware") : String("Starting"));
    ctr(58, String(otaDone / 1024) + " / " + String(otaTot / 1024) + " kB");
    ctr(80, "Keep it switched on", C_DIM);
    oled.drawRect(0, 108, 128, 14, C_ON);
    oled.fillRect(2, 110, 124 * pct / 100, 10, C_ON);
  } else {
    String e = otaErr.length() ? otaErr : String("Upload incomplete");   // the reason, over up to three lines, broken at spaces
    int y = 18;
    while (e.length() && y <= 36) {
      int cut = e.length() <= 21 ? e.length() : 21;
      if (cut < (int)e.length()) { int sp = e.lastIndexOf(' ', 21); if (sp > 8) cut = sp; }
      ctr(y, e.substring(0, cut)); e = e.substring(cut); e.trim(); y += 10;
    }
    ctr(62, "Old firmware kept");
    ctr(74, "Nothing was lost", C_DIM);
  }
  frameStale = true; pushFrame();
}
void otaFailed() { otaSt = 3; otaEndMs = millis(); otaDraw(true); }
void otaTick() {                                         // the failure stays up 15 s (or until a press), then the screens come back
  if (otaSt == 3 && millis() - otaEndMs > 15000) { otaSt = 0; if (oledOK) oled.setContrast(cfg.contrast); frameStale = true; redraw = true; }
}

void head(const char* nm) {                             // the title, and the time (with seconds) and battery on the right
  String tm5, name = nm;
  if (timeValid) {
    todayLoad();                                         // cached: reads the file only when it changed
    struct tm tm; time_t t = time(nullptr); localtime_r(&t, &tm); char b[12];
    if (boxH12) snprintf(b, sizeof b, "%d:%02d:%02d%c", tm.tm_hour % 12 ? tm.tm_hour % 12 : 12, tm.tm_min, tm.tm_sec, tm.tm_hour < 12 ? 'a' : 'p');
    else strftime(b, sizeof b, "%H:%M:%S", &tm);
    tm5 = b;
  }
  name = fit(name, max(4, 21 - (int)tm5.length() - 1));  // never let the clock run into the title
  String r = tm5; if (battType && battChg) r += USB_PIN >= 0 ? " USB" : " chg"; else if (battType && !isnan(battPct)) r += " " + String((int)battPct) + "%";
  if ((int)r.length() > 21 - (int)name.length() - 1) r = tm5;
  if ((int)r.length() > 21 - (int)name.length() - 1) r = "";
  oled.fillRect(0, 0, 128, 9, C_ON);
  oled.setTextColor(C_OFF);
  oled.setCursor(1, 1); oled.print(name);
  oled.setCursor(127 - 6 * r.length(), 1); oled.print(r);
  oled.setTextColor(C_ON);
}
void row(int i, const String& s, bool inv = false, uint16_t col = C_ON) {
  int y = 11 + i * 9 + yOff;
  if (inv) { oled.fillRect(0, y - 1, 128, 9, C_ON); oled.setTextColor(C_OFF); }
  else oled.setTextColor(col);
  oled.setCursor(1, y); oled.print(fit(s, 21));
  oled.setTextColor(C_ON);
}
void rowLR(int i, const String& l, const String& r, uint16_t col = C_ON, bool inv = false) {     // left text, right-aligned value
  int room = 21 - (int)r.length() - 1;
  row(i, fit(l, max(room, 1)), inv, col);
  oled.setTextColor(inv ? C_OFF : col); oled.setCursor(127 - 6 * r.length(), 11 + i * 9 + yOff); oled.print(r); oled.setTextColor(C_ON);
}
// "In  68.4F 52%"; a reading the box hasn't heard again lately gets its age and is drawn grey
bool tempLine(const char* name, const char* lbl, String& out) {
  for (int i = 0; i < MAX_NODES; i++) {
    const Node& n = nodes[i];
    if (!n.used || strcmp(n.name, name) || isnan(n.tempC)) continue;
    out = String(lbl) + (fTemp == "C" ? String(n.tempC, 1) + "C" : String(n.tempC * 9.0f / 5.0f + 32.0f, 1) + "F");
    if (!isnan(n.rh)) out += " " + String((int)lroundf(n.rh)) + "%";
    if (nodeFresh(n)) return true;
    uint32_t ag = nodeAge(n);
    out += ag == 0xFFFFFFFFUL ? " old" : ag < 5400 ? " " + String(ag / 60) + "m" : ag < 172800 ? " " + String(ag / 3600) + "h" : " " + String(ag / 86400) + "d";
    return false;
  }
  out = String(lbl) + "--"; return true;
}
String clockLeft(uint32_t s) {                         // 4:32, or 1:05:00
  char b[24];
  if (s >= 3600) snprintf(b, sizeof b, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)((s / 60) % 60), (unsigned long)(s % 60));
  else snprintf(b, sizeof b, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
  return b;
}
// a scrolling list: the highlight moves down the screen and the list only scrolls once it reaches the
// bottom (or top) row; each screen keeps its own scroll position
int listTop[S_COUNT] = { 0 };
int fixLen(const String& s, int mode) {   // 0: none, 1: through the first space (time, number), 2: indent + "[ ] "
  if (mode == 1) { int k = s.indexOf(' '); return k > 0 && k < 9 ? k + 1 : 0; }
  if (mode == 2) { int k = 0; while (k < (int)s.length() && s[k] == ' ') k++; return min(k + 4, 12); }
  return 0;
}
void rowSlide(int i, const String& s, int fix) {
  int y = 11 + i * 9, over = ((int)s.length() - 21) * 6, off = 0;
  if (over > 0) {
    mqOn = true;
    uint32_t t = millis() - mqT0, run = over * 30;           // a pause, 1 px per 30 ms, a pause, again
    if (t >= 1500 + run + 1200) mqT0 = millis(); else if (t > 1500) off = min((int)((t - 1500) / 30), over);
  }
  oled.fillRect(0, y - 1, 128, 9, C_ON); oled.setTextColor(C_OFF);
  oled.setCursor(1 + 6 * fix - off, y); oled.print(s.substring(fix));
  if (fix) { oled.fillRect(0, y - 1, 1 + 6 * fix, 9, C_ON); oled.setCursor(1, y); oled.print(s.substring(0, fix)); }
  oled.setTextColor(C_ON);
}
void drawList(const std::vector<String>& L, int selRow, bool showSel, int fixMode = 0) {
  int n = L.size(); int& top = listTop[scr];
  if (showSel) { if (selRow < top) top = selRow; if (selRow > top + ROWS - 1) top = selRow - ROWS + 1; }
  top = constrain(top, 0, max(0, n - ROWS));
  for (int i = 0; i < ROWS && top + i < n; i++) {
    if (showSel && top + i == selRow) rowSlide(i, L[top + i], fixLen(L[top + i], fixMode));
    else row(i, L[top + i]);
  }
}
String coord(double v, bool lat) {   // 38.12345 N  /  38 07.407'N  /  38 07'24.4"N
  char h = lat ? (v >= 0 ? 'N' : 'S') : (v >= 0 ? 'E' : 'W'); v = fabs(v);
  int d = (int)v; double m = (v - d) * 60;
  char b[24];
  if (fPos == "dm") snprintf(b, sizeof b, "%d%c %06.3f'%c", d, 248, m, h);
  else if (fPos == "dms") snprintf(b, sizeof b, "%d%c %02d'%04.1f\"%c", d, 248, (int)m, (m - (int)m) * 60, h);
  else snprintf(b, sizeof b, "%.5f %c", v, h);
  return b;
}
String distTxt(double m) {
  double k = fDist == "km" ? 1000.0 : fDist == "nm" ? 1852.0 : 1609.344, v = m / k;
  return String(v, v < 10 ? 2 : 1) + " " + fDist;
}
// one line of LIFE LOGGER; col goes grey for a stale reading
String lbLine(const String& k, uint16_t& col) {
  col = C_ON;
  struct tm tm; time_t t = time(nullptr); localtime_r(&t, &tm); char b[28];
  if (k == "date") { if (!timeValid) return "Clock not set"; strftime(b, sizeof b, "%a %b %d %Y", &tm); return b; }
  if (k == "time") { if (!timeValid) return "Clock not set"; if (boxH12) snprintf(b, sizeof b, "%d:%02d:%02d %cm", tm.tm_hour % 12 ? tm.tm_hour % 12 : 12, tm.tm_min, tm.tm_sec, tm.tm_hour < 12 ? 'a' : 'p'); else strftime(b, sizeof b, "%H:%M:%S", &tm); return b; }
  if (k == "net") return staUp ? "WiFi home " + String(WiFi.RSSI()) + "dBm" : "Hotspot " + String(WiFi.softAPgetStationNum()) + " connected";
  if (k == "ip") return staUp ? "IP " + WiFi.localIP().toString() : "IP " + WiFi.softAPIP().toString();
  if (k == "gps" && gpsAsleep) return gpsMode == 2 ? String("GPS off (by hand)") : String("GPS asleep (home)");
  if (k == "gps") { bool g = gpsI2C && gps.seen && millis() - gps.lastNmeaMs < 5000; return !g ? String("GPS not found") : gps.fix ? "GPS fix " + String(gps.sats) + " sat" : "GPS no fix (" + String(gps.sats) + " sat)"; }
  if (k == "sd") return (sdOK ? String("SD ok") : String("SD none")) + "  " + String(gps.ptsDay == tripDay ? gps.ptsToday : 0) + " pts";
  if (k == "batt") return !battType || isnan(battV) ? String("Battery --") : battChg ? String(USB_PIN >= 0 ? "Battery USB " : "Battery chg ") + String(battV, 2) + "V" : isnan(battPct) ? "Battery " + String(battV, 2) + "V" : "Battery " + String((int)battPct) + "% " + String(battV, 2) + "V";
  if (k == "volt") return !isnan(battV) ? "Battery " + String(battV, 2) + " V" : String("Battery --");
  if (k == "left") {
    if (battChg) return battV >= 4.17f ? (USB_PIN >= 0 ? "On USB, charged" : "Battery charged") : (USB_PIN >= 0 ? "On USB, charging" : "Battery charging");
    if (isnan(battPct)) return "Time left --";
    if (!battSpanOk) return "Time left: measuring";
    if (isnan(battLeftH)) return "Time left: steady";
    float h = battLeftH; return "Time left ~" + (h < 48 ? String(h, h < 10 ? 1 : 0) + " h" : String(h / 24, 1) + " d");
  }
  if (k == "pres") {
    if (!bmpOK) return "Pressure: no sensor";
    if (isnan(baroHpa)) return "Pressure --";
    float d = baro3h(); String o = "Baro " + String(presIn(baroHpa), presDec()) + " " + fPres;
    if (!isnan(d)) { float v = presIn(baroHpa) - presIn(baroHpa - d); o += String(v >= 0 ? " +" : " ") + String(v, 1); }
    return o;
  }
  if (k == "sun") { String o; if (!sunLine(o)) col = C_DIM; return o; }
  if (k == "boxth") {                                     // Box 70.3F 45%
    if (!boxThFresh()) { col = C_DIM; return thK ? String("Box --") : String("No temp sensor"); }
    return "Box " + (fTemp == "C" ? String(boxC, 1) + "C " : String(boxC * 9 / 5 + 32, 1) + "F ") + String((int)lroundf(boxRH)) + "%";
  }
  if (k == "lux") {
    if (!lxK) { col = C_DIM; return String("No light sensor"); }
    if (isnan(boxLux)) { col = C_DIM; return String("Light --"); }
    return "Light " + String(boxLux < 10 ? String(boxLux, 1) : String((long)lroundf(boxLux))) + " lx";
  }
  if (k == "chip") { float c = boxThFresh() ? boxC : bmpOK && !isnan(baroC) ? baroC : temperatureRead(); return "Box " + (fTemp == "C" ? String(c, 1) + "C" : String(c * 9 / 5 + 32, 1) + "F"); }
  if (k == "up") { uint32_t u = millis() / 1000; snprintf(b, sizeof b, "Up %lud %02lu:%02lu", (unsigned long)(u / 86400), (unsigned long)(u / 3600 % 24), (unsigned long)(u / 60 % 60)); return b; }
  if (k.startsWith("n:")) { int p = k.indexOf('|'); String o; if (!tempLine(k.substring(2, p < 0 ? k.length() : p).c_str(), p < 0 ? "" : k.substring(p + 1).c_str(), o)) col = C_DIM; return o; }
  return "";
}
String mapLine(const String& k, uint16_t& col) {
  bool fresh = gps.fix && millis() - gps.lastFixMs < 5000;
  col = C_ON;
  if (k == "cog") { bool mv = fresh && !isnan(gps.kn) && gps.kn >= 1 && !isnan(gps.cog); if (!mv) { col = C_DIM; return "COG --- (still)"; } char b[16]; snprintf(b, sizeof b, "COG %03d%cT", ((int)lroundf(gps.cog)) % 360, 248); return b; }
  if (k == "spd") { if (!fresh || isnan(gps.kn)) return "Speed --"; float v = fSpd == "kn" ? gps.kn : fSpd == "kmh" ? gps.kn * 1.852f : gps.kn * 1.15078f; return "Speed " + String(v, 1) + " " + (fSpd == "kmh" ? "km/h" : fSpd); }
  if (k == "dist") return "Dist  " + distTxt(tripDistM);
  if (k == "fix") return "Fix   " + String(tripFixS / 3600) + "h " + String((tripFixS / 60) % 60) + "m today";
  if (k == "pts") return "Points " + String(gps.ptsDay == tripDay ? gps.ptsToday : 0) + " today";
  if (k == "spots") { col = C_DIM; return "Spots saved " + String(spotsToday); }
  if (k == "lat" || k == "lon" || k == "sats" || k == "alt") {
    if (gpsAsleep) return k == "lat" ? (gpsMode == 2 ? "GPS off (by hand)" : "GPS asleep (home)") : "";
    if (!gps.lastFixMs) return k == "lat" ? "No fix yet" : "";
    if (!fresh) col = C_DIM;
    if (k == "lat") return coord(gps.lat, true);
    if (k == "lon") return coord(gps.lon, false);
    if (k == "alt") return isnan(gps.alt) ? String("Alt --") : "Alt " + (fDist == "mi" ? String(gps.alt * 3.28084f, 0) + " ft" : String(gps.alt, 0) + " m");
    return fresh ? "Sats " + String(gps.sats) : "last fix " + String((millis() - gps.lastFixMs) / 60000) + " min ago";
  }
  return "";
}
// big centred box over the screen for alarms
void alarmBox(const char* title, const String& l1, const String& l2, bool inv) {
  uint16_t bg = inv ? C_ON : C_OFF, fg = inv ? C_OFF : C_ON;
  oled.fillRect(0, 28, 128, 74, bg); oled.drawRect(0, 28, 128, 74, C_ON); oled.drawRect(2, 30, 124, 70, C_ON);
  oled.setTextColor(fg); oled.setTextSize(2);
  oled.setCursor(64 - 6 * strlen(title), 40); oled.print(title);
  oled.setTextSize(1);
  String a = fit(l1, 18), b = fit(l2, 18);
  oled.setCursor(64 - 3 * a.length(), 64); oled.print(a);
  oled.setCursor(64 - 3 * b.length(), 80); oled.print(b);
  oled.setTextColor(C_ON);
}

// a compass rose, true north up, with an arrow along the course over ground
void rose(int cx, int cy, int r, float cog) {
  for (int a = 0; a < 360; a += 2) { double t = a * PI / 180; oled.drawPixel(lround(cx + r * sin(t)), lround(cy - r * cos(t)), C_DIM); }
  for (int a = 0; a < 360; a += 30) { double t = a * PI / 180; int k = a % 90 ? 3 : 6;
    oled.drawLine(lround(cx + (r - k) * sin(t)), lround(cy - (r - k) * cos(t)), lround(cx + r * sin(t)), lround(cy - r * cos(t)), a % 90 ? C_DIM : C_ON); }
  oled.setTextColor(C_ON); oled.setCursor(cx - 2, cy - r + 8); oled.print('N');
  oled.setTextColor(C_DIM); oled.setCursor(cx + r - 14, cy - 3); oled.print('E'); oled.setCursor(cx - 2, cy + r - 15); oled.print('S'); oled.setCursor(cx - r + 10, cy - 3); oled.print('W');
  oled.setTextColor(C_ON);
  double t = cog * PI / 180; float sn = sin(t), cs = cos(t), L = r - 9;
  float tx = cx + L * sn, ty = cy - L * cs, bx = cx + (L - 9) * sn, by = cy - (L - 9) * cs, ax = cx - L * 0.45f * sn, ay = cy + L * 0.45f * cs;
  oled.drawLine(lroundf(ax), lroundf(ay), lroundf(bx), lroundf(by), C_ON); oled.drawLine(lroundf(ax + cs), lroundf(ay + sn), lroundf(bx + cs), lroundf(by + sn), C_ON);
  oled.fillTriangle(lroundf(tx), lroundf(ty), lroundf(bx + 4.5f * cs), lroundf(by + 4.5f * sn), lroundf(bx - 4.5f * cs), lroundf(by - 4.5f * sn), C_ON);
  oled.fillRect(cx - 1, cy - 1, 3, 3, C_ON);
}
String hms(uint32_t s) { char b[16]; snprintf(b, sizeof b, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60)); return b; }
// an event opened from TODAY: when, what, where, and its notes
void todayDetail(const TodayEv& e, std::vector<String>& L) {
  L.push_back(e.t1.length() ? e.t1 + (e.t2.length() ? " - " + e.t2 : String("")) : (e.label.length() ? e.label : String("All day")));
  if (e.det.size()) for (auto& l : e.det) L.push_back(l); else L.push_back(e.title);
}
String expLine(const ExpRow& x, long dn) {               // EXPIRING: the name, and the days left on the right
  long d = dayNum(x.date) - dn; String r = d < 0 ? String("EXPIRED") : d == 0 ? String("today") : String(d) + "d";
  String l = fit(x.name, 21 - r.length() - 1); while ((int)l.length() < 21 - (int)r.length()) l += ' ';
  return l + r;
}
int menuN() { return max(0, scrN - 1); }                // LIFE LOGGER's menu: every other screen, in the order picked
int menuCount(int k, String& out) {                      // the number shown beside a menu line
  out = "";
  if (k == S_CL) { clLoad(); if (clTotal) out = String(clDoneN) + "/" + String(clTotal); }
  else if (k == S_TODAY && todayEvs.size()) out = String(todayEvs.size());
  else if (k == S_NOTES && boxNotes.size()) out = String(boxNotes.size());
  else if (k == S_WO && boxWos.size()) out = String(boxWos.size());
  else if (k == S_EXP && expRows.size()) out = String(expRows.size());
  else if (k == S_JOB) { JobSt j = jobState(); if (j.run >= 0) out = "on"; else if (j.pz >= 0) out = "paused"; }
  return out.length();
}

// CLOCK: the day, time and date in segment digits like a Casio watch (glyphs from tools/clock_glyphs.py, the same as the demo)
#include "clock_glyphs.h"
void clkGlyph(const uint8_t* px, const char* chars, uint8_t w, uint8_t h, char ch, int x, int y) {
  const char* p = ch ? strchr(chars, ch) : nullptr; if (!p) return;
  uint32_t o = (uint32_t)(p - chars) * w * h;
  for (int j = 0; j < h; j++) for (int k = 0; k < w; k++) {
    uint32_t i = o + (uint32_t)j * w + k; uint8_t b = pgm_read_byte(px + (i >> 1)), v = (i & 1) ? (b & 15) : (b >> 4);
    if (v) oled.drawPixel(x + k, y + j, v);
  }
}
void clkText(int x, int y, const String& s, uint16_t c) { oled.setTextColor(c); oled.setCursor(x, y); oled.print(s); }
void drawClock() {
  if (battType && (battChg || !isnan(battPct))) { String p = battChg ? String(USB_PIN >= 0 ? "USB" : "chg") : String((int)battPct) + "%"; clkText(126 - 6 * p.length(), 5, p, C_ON); }   // the battery, top right
  if (battType && !isnan(battV)) { String v = String(battV, 2) + "V"; clkText(126 - 6 * v.length(), 17, v, C_DIM); }
  if (!timeValid) { clkText(28, 60, "Clock not set", C_DIM); return; }
  time_t t = time(nullptr); struct tm tm; localtime_r(&t, &tm);
  static const char* DOW[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
  oled.drawRect(2, 4, 51, 27, C_DIM);                    // the day, in a box
  for (int i = 0; i < 3; i++) clkGlyph(CLK_DAY, CLK_DAY_C, CLK_DAY_W, CLK_DAY_H, DOW[tm.tm_wday][i], 8 + i * 15, 10);
  oled.drawFastHLine(60, 34, 67, C_DIM);
  char hm[5]; int H = tm.tm_hour;
  if (boxH12) { int h = H % 12 ? H % 12 : 12; hm[0] = h >= 10 ? '1' : ' '; hm[1] = '0' + h % 10; } else { hm[0] = '0' + H / 10; hm[1] = '0' + H % 10; }
  hm[2] = '0' + tm.tm_min / 10; hm[3] = '0' + tm.tm_min % 10; hm[4] = 0;
  const int y0 = 42; int x = 4;
  for (int i = 0; i < 4; i++) {
    clkGlyph(CLK_BIG, CLK_BIG_C, CLK_BIG_W, CLK_BIG_H, hm[i], x, y0); x += 20;
    if (i == 1) { oled.fillRect(x, y0 + 9, 3, 3, C_ON); oled.fillRect(x - 1, y0 + 22, 3, 3, C_ON); x += 6; }
  }
  x += 2;
  if (boxH12) clkText(x + 3, y0 + 2, H < 12 ? "A" : "P", C_ON);   // A or P above the seconds
  clkGlyph(CLK_SEC, CLK_SEC_C, CLK_SEC_W, CLK_SEC_H, '0' + tm.tm_sec / 10, x, y0 + 13);
  clkGlyph(CLK_SEC, CLK_SEC_C, CLK_SEC_W, CLK_SEC_H, '0' + tm.tm_sec % 10, x + 12, y0 + 13);
  oled.drawFastHLine(2, 84, 124, C_DIM);
  char ds[24]; snprintf(ds, sizeof ds, "%04d %02d-%02d", (tm.tm_year + 1900) % 10000, (tm.tm_mon + 1) % 100, tm.tm_mday % 100);   // 2026 11-21
  x = 6;
  for (const char* c = ds; *c; c++) { if (*c == ' ') { x += 7; continue; } clkGlyph(CLK_DATE, CLK_DATE_C, CLK_DATE_W, CLK_DATE_H, *c, x, 92); x += 12; }
}

// SUN & MOON: worked out on the box (astro.h) from the GPS fix, or the last fix kept in flash. The sun's day
// is worked out once a day (and again if the box moves a km); the moon's rise and set once it passes the one
// it was waiting for, every hour, or when the box moves.
struct SunC { String day; double lat = 999, lon = 999; astro::SunDay d, tm; } sunC;   // d: today, tm: tomorrow
struct MoonC { double lat = 999, lon = 999; uint32_t at = 0; astro::MoonNow m; } moonC;
bool sunWhere(double& la, double& lo, String& src) {
  bool fresh = gps.fix && millis() - gps.lastFixMs < 5000 && !isnan(gps.lat) && !isnan(gps.lon);
  if (fresh) { la = gps.lat; lo = gps.lon; src = "GPS fix"; return true; }
  if (!lastFix.ep) return false;
  la = lastFix.lat; lo = lastFix.lon;
  time_t f = lastFix.ep; struct tm tm; localtime_r(&f, &tm); char b[12];
  if (timeValid && localDate(f) == localDate(time(nullptr))) { if (boxH12) snprintf(b, sizeof b, "%d:%02d%c", tm.tm_hour % 12 ? tm.tm_hour % 12 : 12, tm.tm_min, tm.tm_hour < 12 ? 'a' : 'p'); else strftime(b, sizeof b, "%H:%M", &tm); }
  else strftime(b, sizeof b, "%b %d", &tm);
  src = String("Fix ") + b;
  return true;
}
time_t dayStart(time_t t, int plus) { struct tm tm; localtime_r(&t, &tm); tm.tm_hour = 0; tm.tm_min = 0; tm.tm_sec = 0; tm.tm_mday += plus; tm.tm_isdst = -1; return mktime(&tm); }
bool sunCalc(String& src) {                                 // false: no position, or no clock
  double la, lo;
  if (!timeValid || !sunWhere(la, lo, src)) return false;
  time_t now = time(nullptr); String d = localDate(now);
  if (d != sunC.day || distM(la, lo, sunC.lat, sunC.lon) > 1000) {
    time_t a = dayStart(now, 0), b = dayStart(now, 1), c = dayStart(now, 2);
    sunC.d = astro::sunDay(la, lo, a, b); sunC.tm = astro::sunDay(la, lo, b, c);
    sunC.day = d; sunC.lat = la; sunC.lon = lo;
  }
  uint32_t n = (uint32_t)now;
  if (!moonC.at || n - moonC.at >= 3600 || (moonC.m.next && (time_t)n >= moonC.m.next) || distM(la, lo, moonC.lat, moonC.lon) > 1000) {
    moonC.m = astro::moonNow(la, lo, now); moonC.at = n; moonC.lat = la; moonC.lon = lo;
  } else if (n - moonC.at >= 60) {                           // the phase moves slowly: once a minute is plenty
    astro::Pos m = astro::moonPos(now), sp = astro::sunPos(now);
    moonC.m.frac = (1.0 - astro::cosd(m.lat) * astro::cosd(m.lon - sp.lon)) / 2.0; moonC.m.elong = astro::rev(m.lon - sp.lon);
  }
  return true;
}
String hm(time_t t) {                                       // 06:56, or 6:56a in 12-hour time
  if (!t) return "--:--";
  struct tm tm; localtime_r(&t, &tm); char b[10];
  if (boxH12) snprintf(b, sizeof b, "%d:%02d%c", tm.tm_hour % 12 ? tm.tm_hour % 12 : 12, tm.tm_min, tm.tm_hour < 12 ? 'a' : 'p');
  else snprintf(b, sizeof b, "%02d:%02d", tm.tm_hour, tm.tm_min);
  return b;
}
String whenDay(time_t t) {                                  // " tmrw" / " yday" for an event not today
  long d = dayNum(localDate(t)) - dayNum(localDate(time(nullptr)));
  return d == 1 ? " tmrw" : d == -1 ? " yday" : "";
}
bool sunLine(String& o) {                                   // LIFE LOGGER: the next sunrise or sunset
  String src;
  if (!sunCalc(src)) { o = timeValid ? "Sun: no fix yet" : "Sun: clock not set"; return false; }
  time_t now = time(nullptr);
  if (sunC.d.rise && now < sunC.d.rise) o = "Sunrise " + hm(sunC.d.rise);
  else if (sunC.d.set && now < sunC.d.set) o = "Sunset " + hm(sunC.d.set);
  else if (sunC.tm.rise) o = "Sunrise " + hm(sunC.tm.rise) + " tmrw";
  else o = sunC.d.all > 0 ? "Sun up all day" : "Sun down all day";
  return true;
}
void drawSun() {
  head("SUN & MOON");
  String src;
  if (!sunCalc(src)) {
    if (!timeValid) { row(1, "Clock not set"); return; }
    row(1, "No position yet"); row(2, "The GPS needs one fix", false, C_DIM); row(3, "(outdoors, a few", false, C_DIM); row(4, "minutes); it is kept", false, C_DIM); row(5, "after that", false, C_DIM);
    return;
  }
  time_t now = time(nullptr); struct tm tm; localtime_r(&now, &tm); char ds[16]; strftime(ds, sizeof ds, "%a %b %d", &tm);
  rowLR(0, ds, src, C_DIM);
  const astro::SunDay& d = sunC.d;
  if (d.all) { row(1, d.all > 0 ? "Sun up all day" : "Sun down all day"); rowLR(3, "Daylight", d.all > 0 ? "24h 00m" : "0h 00m"); }
  else {
    rowLR(1, "Sunrise", hm(d.rise)); rowLR(2, "Sunset", hm(d.set));
    if (d.rise && d.set && d.set > d.rise) { long m = (long)(d.set - d.rise + 30) / 60; rowLR(3, "Daylight", String(m / 60) + "h " + (m % 60 < 10 ? "0" : "") + String(m % 60) + "m"); }
    else rowLR(3, "Daylight", "--");
  }
  rowLR(4, "Civil", hm(d.cDawn) + " " + hm(d.cDusk), C_DIM);
  rowLR(5, "Nautical", hm(d.nDawn) + " " + hm(d.nDusk), C_DIM);
  if (d.noon) { char a[12]; snprintf(a, sizeof a, "  %d%c up", (int)lroundf(d.noonAlt), 248); rowLR(6, "Noon", hm(d.noon) + a, C_DIM); }
  oled.drawFastHLine(0, 72, 128, C_DIM);
  const astro::MoonNow& m = moonC.m;
  bool wax = m.elong < 180; double f = m.frac;                 // the moon as it looks: lit on the right while it waxes (north of the equator)
  bool flip = sunC.lat < 0; const int cx = 14, cy = 95, R = 12;
  for (int y = -R; y <= R; y++) for (int x = -R; x <= R; x++) {
    if (x * x + y * y > R * R + R * 0.6) continue;
    double w = sqrt(max(0.0, 1.0 - (double)(y * y) / (R * R))), xt = (1 - 2 * f) * w, xx = (double)(flip ? -x : x) / R;
    bool lit = wax ? xx >= xt : xx <= -xt;
    oled.drawPixel(cx + x, cy + y, lit ? 14 : 2);
  }
  String L[5];
  L[0] = astro::phaseName(m.elong);
  L[1] = String((int)lround(f * 100)) + "% lit";
  if (m.up) { L[2] = m.prev ? "Rose " + hm(m.prev) + whenDay(m.prev) : "Up"; L[3] = m.next ? "Sets " + hm(m.next) + whenDay(m.next) : "Sets --"; }
  else { L[2] = m.prev ? "Set " + hm(m.prev) + whenDay(m.prev) : "Down"; L[3] = m.next ? "Rises " + hm(m.next) + whenDay(m.next) : "Rises --"; }
  time_t nx = wax ? m.full : m.nu; long s = (long)(nx - now);
  if (s < 0) s = 0;
  L[4] = String(wax ? "Full in " : "New in ") + (s < 86400 ? String((s + 1800) / 3600) + "h" : String((s + 43200) / 86400) + "d");
  for (int i = 0; i < 5; i++) { oled.setTextColor(i < 2 ? C_ON : C_DIM); oled.setCursor(32, 75 + i * 9); oled.print(fit(L[i], 16)); }
  oled.setTextColor(C_ON);
}

// BAROGRAPH: the pressure, its 3-hour change and a graph of the saved 10-minute values. The knob picks the
// span (sel: 0 = 24 h, 1 = 48 h, 2 = 7 days). A stretch with no values stays empty: nothing is drawn across it.
const uint16_t BARO_SPAN_H[] = { 24, 48, 168 };
void drawBaro() {
  int span = constrain(sel, 0, 2);
  head(span == 2 ? "BARO 7d" : span ? "BARO 48h" : "BARO 24h");
  if (!bmpOK) { row(1, "No pressure sensor"); row(2, "BMP388 not found", false, C_DIM); return; }
  if (isnan(baroHpa)) { row(1, "Reading..."); return; }
  String v = String(presIn(baroHpa), presDec());
  oled.setTextSize(2); oled.setTextColor(C_ON); oled.setCursor(1, 13); oled.print(v); oled.setTextSize(1);
  oled.setTextColor(C_DIM); oled.setCursor(1 + 12 * v.length() + 4, 20); oled.print(fPres); oled.setTextColor(C_ON);
  float d3 = baro3h();
  if (isnan(d3)) rowLR(3, "3 h change", "--", C_DIM);
  else {
    float dv = presIn(baroHpa) - presIn(baroHpa - d3); String w = baroWord(d3), c = String(dv >= 0 ? "+" : "") + String(dv, fPres == "inHg" ? 2 : 1) + "/3h";
    if (w.length() + c.length() < 21) rowLR(3, w, c, C_ON);
    else { rowLR(2, "", c, C_ON); row(3, w); }             // "Falling very rapidly": the change goes up a row
  }
  if (!timeValid) { row(5, "Clock not set", false, C_DIM); return; }
  const int gx = 26, gy = 48, gw = 101, gh = 62;
  uint32_t last = (uint32_t)time(nullptr) / BARO_SAVE_S, n = BARO_SPAN_H[span] * 6, first = last - n;
  float cmin[gw], cmax[gw], cend[gw]; float lo = 1e9, hi = -1e9;
  for (int c = 0; c < gw; c++) { cmin[c] = NAN; cmax[c] = NAN; cend[c] = NAN; }
  for (uint32_t k = 0; k < n; k++) {
    float h = brGet(first + 1 + k); if (isnan(h)) continue;
    float y = presIn(h); int c = (int)((uint64_t)k * gw / n);
    if (isnan(cmin[c]) || y < cmin[c]) cmin[c] = y; if (isnan(cmax[c]) || y > cmax[c]) cmax[c] = y; cend[c] = y;
    lo = min(lo, y); hi = max(hi, y);
  }
  for (int k = 0; k <= (span == 2 ? 7 : 4); k++) { int x = gx + (gw - 1) * k / (span == 2 ? 7 : 4); for (int y = gy; y < gy + gh; y += 2) oled.drawPixel(x, y, 3); }
  for (int x = gx; x < gx + gw; x += 2) { oled.drawPixel(x, gy, 3); oled.drawPixel(x, gy + gh - 1, 3); }
  oled.setTextColor(C_DIM);
  const char* L0 = span == 2 ? "-7d" : span ? "-48h" : "-24h"; const char* L1 = span == 2 ? "" : span ? "-24h" : "-12h";
  oled.setCursor(gx - 3 * strlen(L0), gy + gh + 3); oled.print(L0); oled.setCursor(gx + gw / 2 - 3 * strlen(L1), gy + gh + 3); oled.print(L1); oled.setCursor(127 - 18, gy + gh + 3); oled.print("now");
  if (hi < lo) { oled.setCursor(gx + 14, gy + 26); oled.print("No values yet"); oled.setTextColor(C_ON); return; }
  float minSpan = fPres == "hPa" ? 10 : fPres == "inHg" ? 0.3f : 8, step = fPres == "inHg" ? 0.1f : 1;   // a calm day stays flat
  float L = floorf(lo / step) * step, H = ceilf(hi / step) * step;
  if (H - L < minSpan) { float mid = (lo + hi) / 2; L = floorf((mid - minSpan / 2) / step) * step; H = L + minSpan; }
  oled.setCursor(0, gy); oled.print(String(H, fPres == "inHg" ? 1 : 0)); oled.setCursor(0, gy + gh - 8); oled.print(String(L, fPres == "inHg" ? 1 : 0));
  oled.setTextColor(C_ON);
  auto Y = [&](float v) { return gy + gh - 1 - (int)lroundf((v - L) / (H - L) * (gh - 1)); };
  for (int c = 0; c < gw; c++) {
    if (isnan(cmin[c])) continue;
    int x = gx + c;
    if (c && !isnan(cend[c - 1])) oled.drawLine(x - 1, Y(cend[c - 1]), x, Y(cmin[c] < cend[c - 1] ? cmin[c] : cmax[c]), C_ON);
    oled.drawFastVLine(x, Y(cmax[c]), Y(cmin[c]) - Y(cmax[c]) + 1, C_ON);
  }
}

void drawScreen() {
  uint32_t now = millis();
  { int k = 0; while (k < scrN && scrOrd[k] != scr) k++; if (k == scrN && scr != S_CLOCK) setScreen(scrOrd[0]); }
  mqOn = false; yOff = 0;
  oled.clearDisplay();
  oled.setTextSize(1);
  if (otaSt) { otaDraw(true); return; }
  if (csOn) csDraw();
  else if (qmOn) qmDraw();
  else if (scr == S_STATUS) {
    head("LIFE LOGGER");
    todayLoad();
    static const char* LB0[] = { "date", "batt" };
    int nL = lbSet ? min((int)lbKeys.size(), 8) : 2, r = nL;
    for (int i = 0; i < nL; i++) { uint16_t c; String l = lbLine(lbSet ? lbKeys[i] : String(LB0[i]), c); row(i, l, false, c); }
    struct L { String l, r; };                           // timers and BNWAS
    std::vector<L> al;
    for (auto& k : timers) al.push_back({ k.name, k.ringing ? String("DONE") : clockLeft(k.left(now)) });
    if (cfg.bnwasOn) al.push_back({ "BNWAS", bnStage ? String("PRESS") : clockLeft(bnLeftS(now)) });
    auto maxRow = [&]() { return (109 - yOff) / 9; };    // the last row that fits, highlight included
    auto divide = [&]() { oled.drawFastHLine(0, 12 + 9 * r + yOff, 128, C_DIM); yOff += 5; };   // a little space each side of the line
    int room = max(0, min(3, maxRow() + 1 - r - 3 - 1)); // alarms get at most 3 rows; the menu at least 3
    if (al.size() && room) {
      if (r) divide();
      for (int i = 0; i < (int)al.size() && i < room; i++) {
        if (i == room - 1 && (int)al.size() > room) { row(r + i, "+" + String(al.size() - i) + " more", false, C_DIM); break; }
        rowLR(r + i, al[i].l, al[i].r);
      }
      r += min((int)al.size(), room);
    }
    if (r) divide();
    int M = menuN(), n = max(1, maxRow() + 1 - r); int& top = listTop[S_STATUS];
    if (sel < top) top = sel; if (sel > top + n - 1) top = sel - n + 1;
    top = constrain(top, 0, max(0, M - n));
    for (int i = 0; i < n && top + i < M; i++) {
      int k = scrOrd[1 + top + i]; bool on = top + i == sel; String c; menuCount(k, c);
      rowLR(r + i, String(on ? ">" : " ") + SCR_MENU[k], c, C_ON, on);
    }
    if (M > n) { int H = n * 9, bh = max(4, H * n / M), by = 10 + 9 * r + yOff + (H - bh) * top / (M - n); oled.drawFastVLine(127, by, bh, C_DIM); }
    yOff = 0;
  } else if (scr == S_CL) {
    clLoad();
    String ct = String(clDoneN) + "/" + String(clTotal), h = ("CHECKLIST " + ct).length() <= 12 ? "CHECKLIST " + ct : "LIST " + ct;
    head(h.c_str());
    if (!clRows.size()) { if (clTotal) { row(1, "All ticked"); row(2, "(ticked items hidden)", false, C_DIM); } else { row(1, "No items"); row(2, "Add them on the phone"); } }
    else {
      std::vector<String> L;
      for (auto& r : clRows) { String ind; for (int k = 0; k < r.depth; k++) ind += " "; L.push_back(ind + (r.done ? "[x] " : "[ ] ") + r.text); }
      drawList(L, sel, true, 2);
    }
  } else if (scr == S_TODAY) {
    todayLoad();
    String h = "TODAY"; if (todayFor.length()) h += " " + shortDate(todayFor);
    head(h.c_str());
    if (todayOpen >= (int)todayEvs.size()) todayOpen = -1;
    if (todayOpen >= 0) { std::vector<String> L; todayDetail(todayEvs[todayOpen], L); drawList(L, sel, true, 0); }
    else if (!todayRows.size()) row(1, todayNote);
    else drawList(todayRows, sel, true, 1);
  } else if (scr == S_NOTES) {
    todayLoad();
    if (noteOpen >= (int)boxNotes.size()) noteOpen = -1;   // the file went away or changed
    if (noteOpen < 0) {
      head("NOTES");
      if (!boxNotes.size()) { row(1, "No notes here"); row(2, "Tick 'Show on the box'", false, C_DIM); row(3, "on a note", false, C_DIM); }
      else { std::vector<String> L; for (auto& b : boxNotes) L.push_back(b.title); drawList(L, sel, true); }
    } else {
      BoxNote& b = boxNotes[noteOpen];
      head(b.title.c_str());
      int n = b.lines.size(), top = constrain(sel, 0, max(0, n - ROWS));
      for (int i = 0; i < ROWS && top + i < n; i++) row(i, b.lines[top + i]);
      if (!n) row(1, "(no text)", false, C_DIM);
      if (n > ROWS) { int H = ROWS * 9, bh = max(6, H * ROWS / n), by = 10 + (H - bh) * top / (n - ROWS); oled.drawFastVLine(127, by, bh, C_DIM); }   // where you are in it
    }
  } else if (scr == S_WO) {
    todayLoad();
    if (woOpen >= (int)boxWos.size()) woOpen = -1;
    if (woOpen < 0) {
      head("WORK ORDERS");
      if (!boxWos.size()) { row(1, "None here"); row(2, "Tick 'Show on the box'", false, C_DIM); row(3, "on a work order", false, C_DIM); }
      else { std::vector<String> L; for (auto& w : boxWos) L.push_back(w.no + " " + w.title); drawList(L, sel, true, 1); }
    } else {
      BoxWo& w = boxWos[woOpen];
      head((w.no + " " + w.title).c_str());
      if (!w.lines.size()) row(1, "(no description)", false, C_DIM);
      else drawList(w.lines, sel, true, 1);
    }
  } else if (scr == S_CLOCK) {
    drawClock();
  } else if (scr == S_SUN) {
    drawSun();
  } else if (scr == S_BARO) {
    drawBaro();
  } else if (scr == S_EXP) {
    todayLoad(); head("EXPIRING");
    if (!expRows.size()) { row(1, "Nothing coming up"); row(2, "Expiring tab on the", false, C_DIM); row(3, "dashboard", false, C_DIM); }
    else { long dn = timeValid ? dayNum(localDate(time(nullptr))) : 0; std::vector<String> L; for (auto& x : expRows) if (x.date.length() == 10) L.push_back(expLine(x, dn)); drawList(L, sel, true, 0); }
  } else if (scr == S_CARD || scr == S_PCARD) {           // a card to hold up for someone: the business one, or your own
    todayLoad();
    BoxCard& c = cards[scr == S_CARD ? 0 : 1];
    if (!c.lines.size()) { head(scr == S_CARD ? "BUSINESS CARD" : "PERSONAL CARD"); row(1, "Nothing to show yet"); row(2, "Settings > Business", false, C_DIM); row(3, "and Personal Info", false, C_DIM); }
    else {
      int y = 2;
      if (c.w) {                                          // the logo, centred
        int x0 = (128 - c.w) / 2;
        for (int j = 0; j < c.h; j++) for (int i = 0; i < c.w; i++) {
          uint8_t b = c.logo[(j * c.w + i) / 2], v = (i & 1) ? (b & 15) : (b >> 4);
          if (v) oled.drawPixel(x0 + i, y + j, v);
        }
        y += c.h + 4;
      }
      int room = (128 - y) / 9;
      for (int i = 0; i < (int)c.lines.size() && i < room; i++) {
        const String& l = c.lines[i];
        oled.setCursor((128 - (int)l.length() * 6) / 2, y + i * 9); oled.setTextColor(C_ON); oled.print(l);
      }
    }
  } else if (scr == S_JOB) {
    head("JOB TIMER");
    todayLoad();
    JobSt j = jobState(); int xi = j.last;
    if (xi < 0) row(0, "Nothing timed yet", false, C_DIM);
    else {
      const TmE& x = tms[xi];
      row(0, jobName(x.prj)); row(1, x.desc, false, C_DIM);
      int64_t nw = nowMs();
      String tt = j.run >= 0 ? hms((uint32_t)((nw - x.start) / 1000)) : j.pz >= 0 ? hms((uint32_t)((x.end - x.start) / 1000)) : String("0:00:00");
      oled.setTextSize(2); oled.setTextColor(j.pz >= 0 && (millis() / 600) % 2 ? C_DIM : C_ON); oled.setCursor((128 - tt.length() * 12) / 2, 32); oled.print(tt); oled.setTextSize(1);
      const char* st = j.run >= 0 ? "RUNNING" : j.pz >= 0 ? "PAUSED" : "STOPPED";
      oled.setTextColor(j.run >= 0 ? C_ON : C_DIM); oled.setCursor((128 - strlen(st) * 6) / 2, 53); oled.print(st); oled.setTextColor(C_ON);
      rowLR(6, "Today", hms(j.todayS), C_DIM);
      oled.drawFastHLine(0, 12 + 9 * 7 + 3, 128, C_DIM);
    }
    std::vector<JobOpt> O = jobOpts();                    // the choices: five rows, scrolling with the knob
    int top = max(0, min(sel - 2, (int)O.size() - 5));
    if (!O.size()) { row(8, "Nothing to start yet:", false, C_DIM); row(9, "pin a project in", false, C_DIM); row(10, "Time > Projects, or", false, C_DIM); row(11, "open a job", false, C_DIM); }
    for (int i = 0; i < 5 && top + i < (int)O.size(); i++) row(8 + i, String(top + i == sel ? "> " : "  ") + O[top + i].t, top + i == sel);
    oled.setTextColor(C_DIM);
    if (top > 0) { oled.setCursor(121, 11 + 9 * 8); oled.print('^'); }
    if (top + 5 < (int)O.size()) { oled.setCursor(121, 11 + 9 * 12); oled.print('v'); }
    oled.setTextColor(C_ON);
  } else if (scr == S_TRIP) {
    head("MAP TODAY");
    tripLoad();
    static const char* MP0[] = { "spd", "dist", "fix", "spots", "lat", "lon", "sats" };
    std::vector<String> K; if (mapSet) K = mapKeys; else for (const char* k : MP0) K.push_back(k);
    bool fresh = gps.fix && millis() - gps.lastFixMs < 5000, mv = fresh && !isnan(gps.kn) && gps.kn >= 1 && !isnan(gps.cog);
    if (mv) {                                             // moving: the first lines, the course, and the compass under them
      std::vector<String> show; for (auto& k : K) if (k != "-" && k != "cog" && show.size() < 4) show.push_back(k);
      show.push_back("cog");
      for (int i = 0; i < (int)show.size(); i++) { uint16_t c; String l = mapLine(show[i], c); row(i, l, false, c); }
      int top = 11 + 9 * show.size() + 1, h = 127 - 20 - top, r = min(40, h / 2 - 1);
      if (r > 8) rose(64, top + h / 2, r, gps.cog);
    } else for (int i = 0; i < (int)K.size() && i < 10; i++) { uint16_t c; String l = K[i] == "-" ? String("") : mapLine(K[i], c); if (K[i] == "-") c = C_ON; row(i, l, false, c); }
    row(11, String(sel == 0 ? "> " : "  ") + (WT.on ? "End trip " + distTxt(WT.m) : String("[ ] Work trip")), sel == 0);   // turn to pick, press to start / end or save
    row(12, String(sel == 1 ? "> " : "  ") + "Save spot here", sel == 1);
  } else {
    head("JOIN");
    row(0, "WiFi " + ascii(cfg.apSsid));
    row(1, "Pass " + ascii(cfg.apPass));
    if (!apUp) { row(0, "Hotspot off at home", false, C_DIM); row(1, "QUICK MENU: Hotspot", false, C_DIM); }
    row(3, "Open " + fit(cfg.mdns, 15) + ".local");
    row(4, "  or " + (apUp ? WiFi.softAPIP() : WiFi.localIP()).toString());
    row(6, staUp ? "Home: " + WiFi.localIP().toString() : String("Home WiFi: not here"));
    std::vector<Dev> dv; for (auto& d : devs) if (millis() - d.ms < 600000UL) dv.push_back(d);   // dashboards open in the last 10 minutes, newest first
    for (size_t i = 1; i < dv.size(); i++) for (size_t k = i; k > 0 && dv[k].ms > dv[k - 1].ms; k--) std::swap(dv[k], dv[k - 1]);
    oled.drawFastHLine(0, 12 + 9 * 7 + 3, 128, C_DIM);
    row(8, "CONNECTED " + String(dv.size()), false, C_DIM);
    if (!dv.size()) row(9, "No dashboard open", false, C_DIM);
    for (int i = 0; i < (int)dv.size() && i < (dv.size() > 4 ? 3 : 4); i++) { uint32_t ag = (millis() - dv[i].ms) / 1000; rowLR(9 + i, dv[i].n, ag < 60 ? String("now") : String(ag / 60) + "m"); }
    if (dv.size() > 4) row(12, "+" + String(dv.size() - 3) + " more", false, C_DIM);
  }
  // alarms on top of whatever screen is up
  bool blink = (now / 500) % 2;
  if (bnStage == 1) alarmBox("BNWAS", "Press a button", "alarm in " + String(bnLeftS(now)) + " s", blink);
  else if (bnStage == 2) alarmBox("BNWAS", "Press a button", "", blink);
  else if (timerRinging()) { String n; for (auto& k : timers) if (k.ringing) { n = k.name; break; } alarmBox("TIMER", n, "Press to stop", blink); }
  else if (findOn) alarmBox("HERE", "Find my box", "Press to stop", blink);
  else if (tripAsk) {                                     // still on the work trip? turn to pick, press
    oled.fillRect(0, 26, 128, 78, C_OFF); oled.drawRect(0, 26, 128, 78, C_ON); oled.drawRect(2, 28, 124, 74, C_ON);
    oled.setTextSize(2); oled.setTextColor(C_ON); oled.setCursor(64 - 6 * 9, 33); oled.print("WORK TRIP"); oled.setTextSize(1);
    String q = "Still on it? " + distTxt(WT.m); if (q.length() > 20) { int k = q.lastIndexOf(' '); q.remove(k, 1); } q = fit(q, 20); oled.setCursor(64 - 3 * q.length(), 53); oled.print(q);
    const char* O[] = { "Keep going", "End trip" };
    for (int i = 0; i < 2; i++) {
      int y = 68 + i * 13; bool on = i == tripAskSel;
      if (on) oled.fillRect(8, y - 2, 112, 11, C_ON);
      oled.setTextColor(on ? C_OFF : C_ON); oled.setCursor(64 - 3 * strlen(O[i]), y); oled.print(O[i]);
    }
    oled.setTextColor(C_ON);
  }
  else if (millis() < msgUntil) { oled.fillRect(14, 50, 100, 26, C_OFF); oled.drawRect(14, 50, 100, 26, C_ON); oled.setCursor(64 - 3 * msg.length(), 59); oled.print(msg); }
  pushFrame();
}

int listLen() {
  if (scr == S_STATUS) return menuN();
  if (scr == S_CL) { clLoad(); return clRows.size(); }
  if (scr == S_TODAY) { todayLoad(); if (todayOpen >= (int)todayEvs.size()) todayOpen = -1; if (todayOpen >= 0) { std::vector<String> L; todayDetail(todayEvs[todayOpen], L); return L.size(); } return todayRows.size(); }
  if (scr == S_NOTES) { todayLoad(); if (noteOpen >= (int)boxNotes.size()) noteOpen = -1; return noteOpen >= 0 ? max(1, (int)boxNotes[noteOpen].lines.size() - ROWS + 1) : (int)boxNotes.size(); }   // reading: one step per line
  if (scr == S_WO) { todayLoad(); if (woOpen >= (int)boxWos.size()) woOpen = -1; return woOpen >= 0 ? (int)boxWos[woOpen].lines.size() : (int)boxWos.size(); }
  if (scr == S_EXP) { todayLoad(); return expRows.size(); }
  if (scr == S_JOB) { todayLoad(); return jobOpts().size(); }
  if (scr == S_TRIP) return 2;
  if (scr == S_BARO) return 3;                             // the graph's span: 24 h, 48 h, 7 days
  return 0;
}
void setScreen(int s) {   // each screen comes back where you left it
  if (scr == S_NOTES && noteOpen >= 0) { noteOpen = -1; sel = noteListSel; }   // leaving a note goes back to the list
  if (scr == S_WO && woOpen >= 0) { woOpen = -1; sel = woListSel; }
  if (scr == S_TODAY && todayOpen >= 0) { todayOpen = -1; sel = todayListSel; }
  selMem[scr] = sel;
  scr = constrain(s, 0, S_COUNT - 1); mqT0 = millis();
  int n = listLen();
  if (scr == S_CL && clSelId.length()) for (size_t i = 0; i < clRows.size(); i++) if (clRows[i].id == clSelId) { selMem[S_CL] = i; break; }
  sel = constrain(selMem[scr], 0, max(0, n - 1));
  redraw = true;
}
void nextScreen() {       // without the knob: the button goes round the screens
  int k = 0; while (k < scrN && scrOrd[k] != scr) k++;
  setScreen(scrOrd[k < scrN ? (k + 1) % scrN : 0]);
}
void moveSel(int d) {   // one row per click of the knob, stopping at the ends
  if (tripAsk) { tripAskSel = constrain(tripAskSel + d, 0, 1); redraw = true; return; }
  int n = listLen(); if (n <= 0) { sel = 0; return; }
  int s2 = constrain(sel + d, 0, n - 1);
  if (s2 != sel) { sel = s2; redraw = true; mqT0 = millis(); }
  selMem[scr] = sel;
  if (scr == S_CL && sel < (int)clRows.size()) clSelId = clRows[sel].id;
}
void input() { redraw = true; }
void screenPower(bool on) {
  on = on && oledOK;
  if (on == screenOn) return;
  screenOn = on;
  if (!on) { qmOn = false; csOn = false; }
  if (oledOK) oled.oled_command(on ? SSD1327_DISPLAYON : SSD1327_DISPLAYOFF);
  if (on) { oled.setContrast(cfg.contrast); frameStale = true; redraw = true; }
}
// any turn or press: restarts the sleep timer and the BNWAS period; true if it should do nothing else
// (it stopped an alarm, or only woke the screen)
bool touched(uint32_t now) {
  lastInputMs = now; input(); imuWoke = false;
  if (otaSt == 3) { otaEndMs = 0; otaTick(); return true; }
  if (cfg.bnwasOn) bnwasRestart(now);
  if (alarmActive()) { silence(true, now); msg = "Stopped"; msgUntil = now + 1000; return true; }
  if (tripBeeping()) { tripAskSince = millis() - TRIP_BEEP_MS; if (!screenOn) screenPower(true); redraw = true; return true; }   // the first touch only stops the beeping
  if (!screenOn && oledOK) { screenPower(true); return true; }
  return false;
}
void todayPress() {   // TODAY: press opens the highlighted event (when, where, notes), press again goes back
  todayLoad();
  if (todayOpen >= (int)todayEvs.size()) todayOpen = -1;
  if (todayOpen < 0) { if (!todayEvs.size()) return; todayListSel = sel; todayOpen = constrain(sel, 0, (int)todayEvs.size() - 1); sel = 0; listTop[S_TODAY] = 0; }
  else { todayOpen = -1; sel = todayListSel; }
  selMem[S_TODAY] = sel; mqT0 = millis(); redraw = true;
}
// the button: back one step (an open note, work order or TODAY item to its list), then to LIFE LOGGER
void back() {
  if (scr == S_NOTES && noteOpen >= 0) { notesPress(); return; }
  if (scr == S_WO && woOpen >= 0) { woPress(); return; }
  if (scr == S_TODAY && todayOpen >= 0) { todayPress(); return; }
  if (scr == S_STATUS) return;
  int from = scr; setScreen(S_STATUS);
  for (int k = 1; k < scrN; k++) if (scrOrd[k] == from) { sel = k - 1; selMem[S_STATUS] = sel; break; }
}
void mapPress() {
  if (sel == 0) { if (WT.on) tripEnd(false); else tripStart(); }
  else spotSave();
}
// the knob's press: on LIFE LOGGER it opens the screen picked in the menu; it never moves between screens otherwise
void press() {
  if (tripAsk) { tripAnswer(); return; }
  if (scr == S_STATUS) { if (sel >= 0 && sel < menuN()) setScreen(scrOrd[1 + sel]); }
  else if (scr == S_CL) clToggle(sel); else if (scr == S_NOTES) notesPress(); else if (scr == S_WO) woPress();
  else if (scr == S_TODAY) todayPress(); else if (scr == S_TRIP) mapPress(); else if (scr == S_JOB) jobPress(sel);
  redraw = true;
}
void onShort() { if (!screenOn) return; if (encOK) back(); else nextScreen(); }
// the shortcuts (ACTION press, BACK + SELECT 3 s): home, clock, job timer, quick mark, work trip, screen off, find my phone, sound check, restart
void jobQuick() {                                        // pause what's running, resume what's paused, else start the last one
  std::vector<JobOpt> O = jobOpts(); int k = -1;
  for (const char a : { 'P', 'R', 'A', 'G' }) { for (int i = 0; i < (int)O.size() && k < 0; i++) if (O[i].a == a) k = i; if (k >= 0) break; }
  if (k < 0) { msg = "No job to start"; msgUntil = millis() + 1500; redraw = true; return; }
  jobPress(k);
}
struct QmRow { String t, a; };
String jobQuickLabel() {
  std::vector<JobOpt> O = jobOpts(); JobSt j = jobState();
  auto nm = [&](int i) { return tms[i].desc.length() ? tms[i].desc : jobName(tms[i].prj); };
  for (const JobOpt& o : O) if (o.a == 'P') return "Pause " + nm(j.run);
  for (const JobOpt& o : O) if (o.a == 'R') return "Resume " + nm(j.pz);
  for (const JobOpt& o : O) if (o.a == 'A' || o.a == 'G') return o.t;
  return "";
}
std::vector<QmRow> qmRows() {
  std::vector<QmRow> r; r.push_back({ "Close", "" });
  bool hasCs = false; for (const String& k : qmKeys) if (k == "setclk") hasCs = true;
  if (!timeValid && !hasCs) r.push_back({ "Set clock", "setclk" });   // no clock: always offered
  if (!apUp) r.push_back({ "Hotspot on", "apon" });                    // off at home: a way back without the dashboard
  for (const String& k : qmKeys) {
    if (k == "screen") r.push_back({ "Screen off", "off" });
    else if (k == "gps") {
      if (gpsAsleep) r.push_back({ "GPS on", "gps1" }); else r.push_back({ "GPS off", "gps2" });
      if (gpsMode) r.push_back({ "GPS auto (home off)", "gps0" });
    }
    else if (k == "job") { String t = jobQuickLabel(); if (t.length()) r.push_back({ t, "job" }); }
    else if (k == "trip") r.push_back({ WT.on ? "End work trip" : "Start work trip", "trip" });
    else if (k == "mark") r.push_back({ "Quick mark", "mark" });
    else if (k == "bnwas") r.push_back({ cfg.bnwasOn ? "BNWAS off" : "BNWAS on", "bnwas" });
    else if (k == "phone") r.push_back({ "Find my phone", "phone" });
    else if (k == "clock") r.push_back({ "Clock", "clock" });
    else if (k == "setclk") r.push_back({ "Set clock", "setclk" });
    else if (k == "home") r.push_back({ "Home", "home" });
    else if (k == "beep") r.push_back({ "Test alert", "beep" });
    else if (k == "alert" && hapOK) r.push_back({ String("Alerts: ") + ALERT_NAME[cfg.alert] + " > " + ALERT_NAME[(cfg.alert + 1) % 3], "alert" });
    else if (k == "restart") r.push_back({ "Restart", "restart" });
  }
  return r;
}
int daysIn(int y, int m) { static const uint8_t D[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }; return m == 2 && ((y % 4 == 0 && y % 100) || y % 400 == 0) ? 29 : D[m - 1]; }
void csOpen() {                                          // starts from the box's time, or 1 January if it has none
  time_t t = time(nullptr); struct tm tm; localtime_r(&t, &tm);
  if (timeValid) { csV[0] = tm.tm_year + 1900; csV[1] = tm.tm_mon + 1; csV[2] = tm.tm_mday; csV[3] = tm.tm_hour; csV[4] = tm.tm_min; }
  else { csV[0] = 2026; csV[1] = 1; csV[2] = 1; csV[3] = 12; csV[4] = 0; }
  if (csV[0] < 2025) csV[0] = 2026;
  csOn = true; csF = 0; qmOn = false; redraw = true;
}
void csTurn(int d) {
  static const int LO[] = { 2025, 1, 1, 0, 0 }, HI[] = { 2099, 12, 31, 23, 59 };
  int hi = csF == 2 ? daysIn(csV[0], csV[1]) : HI[csF], span = hi - LO[csF] + 1;
  csV[csF] = LO[csF] + (((csV[csF] - LO[csF] + d) % span) + span) % span;   // wraps round
  csV[2] = min(csV[2], daysIn(csV[0], csV[1]));
  redraw = true;
}
void csNext() {
  if (csF < 4) { csF++; redraw = true; return; }
  csOn = false; redraw = true;
  time_t e = localToEpoch(csV[0], csV[1], csV[2], csV[3], csV[4], 0);
  if (!setClockTo(e, "manual")) { msg = "Not set"; msgUntil = millis() + 1500; return; }
  bool corr = !cfg.clkMan && ((gpsI2C && !gpsAsleep) || staUp);
  msg = corr ? "Set (GPS/net may fix)" : "Clock set"; msgUntil = millis() + 2500;
}
void csBack() { if (csF > 0) csF--; else { csOn = false; msg = "Clock not changed"; msgUntil = millis() + 1200; } redraw = true; }
void csDraw() {
  head("SET CLOCK");
  static const char* N[] = { "Year", "Month", "Day", "Hour", "Minute" };
  for (int i = 0; i < 5; i++) {
    String v = String(csV[i]);
    if (i == 3 && boxH12) v = String(csV[3] % 12 ? csV[3] % 12 : 12) + (csV[3] < 12 ? " am" : " pm");
    else if (i) { if (v.length() < 2) v = "0" + v; }
    rowLR(i, (i == csF ? "> " : "  ") + String(N[i]), v, C_ON, i == csF);
  }
  String z = cfg.tz; int k = 0; while (k < (int)z.length() && isalpha((unsigned char)z[k])) k++;
  row(6, "Zone " + (k >= 3 ? z.substring(0, k) : z), false, C_DIM);
  row(7, cfg.clkMan ? "Source: manual" : "Source: auto", false, C_DIM);
  if (!cfg.clkMan) row(8, "GPS/net may correct", false, C_DIM);
  row(10, "Turn: change", false, C_DIM);
  row(11, csF < 4 ? "Press: next field" : "Press: set the clock", false, C_DIM);
  row(12, csF ? "BACK: previous" : "BACK: cancel", false, C_DIM);
}
void qmDraw() {
  std::vector<QmRow> R = qmRows();
  qmSel = constrain(qmSel, 0, (int)R.size() - 1);
  head("QUICK MENU");
  const int vis = 12;
  if (qmSel < qmTop) qmTop = qmSel; if (qmSel >= qmTop + vis) qmTop = qmSel - vis + 1;
  for (int i = 0; i < vis && qmTop + i < (int)R.size(); i++) { int k = qmTop + i; row(i, (k == qmSel ? "> " : "  ") + R[k].t, k == qmSel, k ? C_ON : C_DIM); }
}
void qmRun() {
  std::vector<QmRow> R = qmRows();
  qmOn = false; redraw = true;
  if (qmSel <= 0 || qmSel >= (int)R.size()) return;        // Close
  runAction(R[qmSel].a);
}
void runAction(const String& a) {                          // a quick-menu row, or an extra button's action
  if (a == "gps") { gpsMode = (gpsMode + 1) % 3; lastGpsPower = 0; msg = gpsMode == 1 ? "GPS on" : gpsMode == 2 ? "GPS off" : "GPS auto"; msgUntil = millis() + 1500; redraw = true; return; }
  if (a.startsWith("gps")) {
    gpsMode = a[3] - '0'; lastGpsPower = 0;
    msg = gpsMode == 1 ? "GPS on" : gpsMode == 2 ? "GPS off" : (staUp ? "GPS auto: asleep" : "GPS auto: on"); msgUntil = millis() + 1500;
  } else if (a == "alert") {
    cfg.alert = (cfg.alert + 1) % 3; if (flashOK) cfgSave();
    msg = String("Alerts: ") + ALERT_NAME[cfg.alert]; msgUntil = millis() + 1500;
  } else if (a == "setclk") { csOpen();
  } else if (a == "apon") {
    apForce = true; apApply();
    msg = apUp ? "Hotspot on" : "Hotspot failed"; msgUntil = millis() + 1500;
  } else if (a == "bnwas") {
    cfg.bnwasOn = !cfg.bnwasOn; bnwasRestart(millis()); if (flashOK) cfgSave();
    msg = cfg.bnwasOn ? "BNWAS on" : "BNWAS off"; msgUntil = millis() + 1500;
  } else shortcut(a);
}
void shortcut(const String& k) {
  if (!screenOn && k != "phone" && k != "beep" && k != "restart") screenPower(true);
  if (k == "home") { setScreen(S_STATUS); }
  else if (k == "clock") { setScreen(S_CLOCK); }
  else if (k == "job") jobQuick();
  else if (k == "mark") spotSave();
  else if (k == "trip") { if (WT.on) tripEnd(false); else tripStart(); }
  else if (k == "off") screenPower(false);
  else if (k == "phone") { findPhoneEp = time(nullptr); msg = "Calling phones"; msgUntil = millis() + 2000; ledBlink(1200); }
  else if (k == "beep") { beepOnce(); msg = alertSound() && alertVibe() ? "Sound + vibrate" : alertVibe() ? "Vibrate" : "Sound"; msgUntil = millis() + 1200; }
  else if (k == "restart") { msg = "Restarting"; msgUntil = millis() + 2000; redraw = true; drawScreen(); delay(600); ESP.restart(); }
  redraw = true;
}
void woPress() {   // WORK ORDERS: press opens one (description, then steps), press again goes back
  todayLoad();
  if (woOpen >= (int)boxWos.size()) woOpen = -1;
  if (woOpen < 0) { if (!boxWos.size()) return; woListSel = sel; woOpen = constrain(sel, 0, (int)boxWos.size() - 1); sel = 0; listTop[S_WO] = 0; }
  else { woOpen = -1; sel = woListSel; }
  selMem[S_WO] = sel; mqT0 = millis(); redraw = true;
}
// MAP: a press saves where the box is now, as a spot on the dashboard's Map tab
void spotSave() {
  bool fresh = gps.fix && millis() - gps.lastFixMs < 5000;
  if (!fresh) { msg = gpsAsleep ? (gpsMode == 2 ? "GPS is off" : "GPS asleep at home") : "No GPS fix"; msgUntil = millis() + 1500; redraw = true; return; }
  if (!timeValid || !store) { msg = "Not saved"; msgUntil = millis() + 1500; redraw = true; return; }
  uint32_t t = time(nullptr);
  String ev = "{\"op\":\"spot.add\",\"id\":\"sp-" + String(t, HEX) + String(millis() % 1000) + "\",\"lat\":" + String(gps.lat, 6) + ",\"lon\":" + String(gps.lon, 6) +
              ",\"t\":" + jStr(isoLocal(t)) + ",\"src\":\"box\"}";
  String ts; uint32_t first;
  if (appendEvents(ev, ts, first) == 1) { spotsToday++; msg = "Spot saved"; ledBlink(400); }
  else msg = "Not saved";
  msgUntil = millis() + 1500; redraw = true;
}
// NOTES: press opens the highlighted note, press again goes back to the list
void notesPress() {
  todayLoad();
  if (noteOpen >= (int)boxNotes.size()) noteOpen = -1;
  if (noteOpen < 0) { if (!boxNotes.size()) return; noteListSel = sel; noteOpen = constrain(sel, 0, (int)boxNotes.size() - 1); sel = 0; }
  else { noteOpen = -1; sel = noteListSel; }
  selMem[S_NOTES] = sel; redraw = true;
}
void onHold() {   // without the encoder, hold the button: what a knob press does
  if (!screenOn || encOK) return;
  if (scr != S_STATUS) press();
}

void encStart() {
  for (uint8_t a : ENC_ADDRS) if (enc.begin(a)) { encOK = true; encAddr = a; break; }
  if (!encOK && battType != 1 && enc.begin(0x36)) { encOK = true; encAddr = 0x36; }   // address jumpers left open, no MAX17048 there
  if (!encOK) return;
  enc.pinMode(ENC_SW, INPUT_PULLUP);
  enc.getEncoderDelta();                                   // start counting from here
  if (ENC_INT_PIN >= 0) { pinMode(ENC_INT_PIN, INPUT_PULLUP); enc.setGPIOInterrupts((uint32_t)1 << ENC_SW, true); enc.enableEncoderInterrupt(); }
  Serial.printf("Encoder at 0x%02X (seesaw product %lu)\n", encAddr, (unsigned long)(enc.getVersion() >> 16));
}

void screenStart() {
  pinInput(hp.back); pinInput(hp.sel);
  for (int i = 0; i < 4; i++) if (hp.xbP[i].length()) pinInput(hp.xbP[i]);
  buzStart();                                              // claims LEDC channels 0 and 1 first
  pinLedStart(hp.backLed); pinLedStart(hp.selLed); pinLedStart(hp.ind[0]); pinLedStart(hp.ind[1]);
  bnwasRestart(millis());
  oledOK = oled.begin(OLED_ADDR, true);
  encStart();
  lastInputMs = millis();
  if (!oledOK) return;
  oled.setRotation(OLED_ROT);
  oled.cp437(true);
  oled.clearDisplay(); oled.display();
  oled.oled_command(SSD1327_DISPLAYOFF);
  screenPower(true);                                       // on at power-up, then sleeps when left alone
}

uint32_t lastTripCheck = 0;
void encTick(uint32_t now) {
  if (!encOK || now - lastEncMs < 20) return;
  lastEncMs = now;
  if (ENC_INT_PIN >= 0 && digitalRead(ENC_INT_PIN) == HIGH && !encSw.down) return;   // nothing new (INT pulls low on a turn or press)
  // push switch
  bool d = !enc.digitalRead(ENC_SW);
  // ACTION (the knob's push): a press runs your shortcut, or the highlighted row while the quick menu is
  // open; a hold opens the quick menu, which stays open: turn to a row, press to run it, BACK to close.
  if (d != encSw.down) {
    encSw.down = d;
    if (d) {
      encSw.t0 = now; encSw.longDone = false; encSw.turned = false;
      bool wasOff = !screenOn && oledOK && !alarmActive() && !tripBeeping();
      encSw.swallow = touched(now); encSw.woke = encSw.swallow && wasOff;   // only woke the screen: a hold still opens the menu
    } else {
      if (!encSw.swallow && !encSw.longDone && !encSw.turned) { if (csOn) csNext(); else if (qmOn) qmRun(); else shortcut(btnP); }
      encSw.swallow = false;
    }
  }
  if (encSw.down && (!encSw.swallow || encSw.woke) && !encSw.longDone && !encSw.turned && !qmOn && !csOn && !btn.down && now - encSw.t0 >= HOLD_MS) {
    encSw.longDone = true; qmOn = true; qmSel = 0; qmTop = 0; lastInputMs = now; redraw = true;
  }
  // turning
  int32_t delta = enc.getEncoderDelta();
  if (!delta) return;
  if (ENC_REVERSE) delta = -delta;
  if (encSw.down) encSw.turned = true;
  if (csOn) { csTurn((int)delta); lastInputMs = now; return; }
  if (qmOn) { qmSel = constrain(qmSel + (int)delta, 0, (int)qmRows().size() - 1); lastInputMs = now; redraw = true; return; }
  if (touched(now)) return;                                // that click woke the screen (or stopped an alarm)
  if (btn.down && !btn.swallow) {                          // holding BACK: the knob sets the screen's brightness
    bright = true; btn.held = true;
    cfg.contrast = constrain(cfg.contrast + (int)delta * 8, 1, 255); oled.setContrast(cfg.contrast);
    msg = "Bright " + String((int)lroundf(cfg.contrast * 100.0f / 255)) + "%"; msgUntil = now + 1500;
  } else moveSel((int)delta);
}

// the box's own row in the climate files: its temperature and humidity, and its battery in the node's battery columns
void envBoxRow(File& f, const String& u, const String& l) {
  if (!boxThFresh()) return;
  f.print(u); f.print(','); f.print(l); f.print(",Box,");
  f.print(csvNum(boxC, 2)); f.print(','); f.print(csvNum(boxRH, 1)); f.print(',');
  f.print(csvNum(battType ? battV : NAN, 3)); f.print(','); f.print(csvNum(battType ? battPct : NAN, 1)); f.println(',');
}
// IMU (Add-ons): picked up wakes the screen, and if that's all that woke it, it sleeps again after 20 s set down.
// The picture turns with the box (whichever side is down is its bottom), unless the screen is lying (nearly) flat.
float imSlow[3] = { NAN, NAN, NAN }; uint32_t imStillMs = 0, imRotMs = 0; uint8_t imRotCur = 0, imRotWant = 0;
void imuTick(uint32_t now) {
  if (!imK) return;
  thTick(); lxTick(); imRead();
  if (isnan(imG[0]) || now - imSeen > 1000) return;
  float d = 0;
  for (int i = 0; i < 3; i++) { if (isnan(imSlow[i])) imSlow[i] = imG[i]; float e = imG[i] - imSlow[i]; d += e * e; imSlow[i] += 0.1f * (imG[i] - imSlow[i]); }
  d = sqrtf(d);
  if (d > 0.03f) imStillMs = now;
  if (imuWake && d > 0.12f && !screenOn && oledOK && !alarmActive()) { screenPower(true); lastInputMs = now; imuWoke = true; redraw = true; }
  if (imuWoke && screenOn && now - imStillMs > 20000 && now - lastInputMs > 20000 && !alarmActive()) { imuWoke = false; screenPower(false); }
  if (!imuRot || !oledOK || otaSt) return;
  float x = imG[0], y = imuFlip ? -imG[1] : imG[1];
  if (sqrtf(x * x + y * y) < 0.7f) { imRotMs = now; return; }   // lying flat: keep it as it is
  float g = atan2f(y, x) * 57.29578f - 90.0f * imuMount;   // gravity, counter-clockwise from the screen's top
  float off = fmodf(g - 180.0f + 45.0f + 3600.0f, 360.0f), inq = fmodf(off, 90.0f);
  uint8_t want = (uint8_t)(off / 90.0f) & 3;
  if (inq < 15 || inq > 75) { imRotMs = now; return; }    // near a corner: wait until it's clearly one side
  if (want != imRotWant) { imRotWant = want; imRotMs = now; return; }
  if (want != imRotCur && now - imRotMs > 800) { imRotCur = want; oled.setRotation((OLED_ROT + want) & 3); frameStale = true; redraw = true; }
}
void screenTick() {
  uint32_t now = millis();
  otaTick();
  if (timeValid && now - lastTripCheck > 5000) { lastTripCheck = now; tripLoad(); }   // today's distance from the file, once
  tripWatch();
  alarmsTick(now);
  // BACK
  expRead();                                               // the expanders' buttons, if any are on them
  if (imK) imuTick(now); else { thTick(); lxTick(); }
  bool raw = pinDown(hp.back);
  if (raw != btn.raw) { btn.raw = raw; btn.chg = now; }
  if (now - btn.chg > 25 && raw != btn.down) {
    btn.down = raw;
    if (raw) { btn.t0 = now; btn.held = false; btn.paired = btn2.down; if (btn2.down) btn2.paired = true; btn.swallow = touched(now); }
    else {
      if (btn.swallow) btn.swallow = false;
      else if (bright) { bright = false; cfgSave(); msg = "Saved"; msgUntil = now + 800; redraw = true; }
      else if (!btn.held && !btn.paired && !bothDone) { if (csOn) csBack(); else { inputSrc = 1; onShort(); inputSrc = 0; } }
      if (!btn2.down) bothDone = false;
    }
  }
  if (btn.down && !btn.held && !btn.swallow && !btn2.down && now - btn.t0 >= HOLD_MS) { btn.held = true; if (csOn) { csOn = false; redraw = true; } else { inputSrc = 1; onHold(); inputSrc = 0; } }
  // SELECT: a press opens, ticks or starts what is highlighted (or runs the quick menu's row); a hold turns the screen off
  bool raw2 = pinDown(hp.sel);
  if (raw2 != btn2.raw) { btn2.raw = raw2; btn2.chg = now; }
  if (now - btn2.chg > 25 && raw2 != btn2.down) {
    btn2.down = raw2;
    if (raw2) { btn2.t0 = now; btn2.held = false; btn2.paired = btn.down; if (btn.down) btn.paired = true; btn2.swallow = touched(now); }
    else {
      if (btn2.swallow) {}
      else if (!btn2.held && !btn2.paired && !bothDone && screenOn) { inputSrc = 2; if (csOn) csNext(); else if (qmOn) qmRun(); else press(); inputSrc = 0; }
      btn2.swallow = false;
      if (!btn.down) bothDone = false;
    }
  }
  if (btn2.down && !btn2.held && !btn2.swallow && !btn.down && now - btn2.t0 >= SEL_OFF_MS) { btn2.held = true; qmOn = false; screenPower(false); }
  if (qmOn && btn.down && !btn.paired) { qmOn = false; btn.paired = true; redraw = true; }   // BACK while the menu is open: close it, run nothing else
  for (int i = 0; i < 4; i++) {                            // extra buttons: a press runs its action
    if (!hp.xbP[i].length() || !hp.xbA[i].length()) continue;
    Btn& b = xbtn[i]; bool r = pinDown(hp.xbP[i]);
    if (r != b.raw) { b.raw = r; b.chg = now; }
    if (now - b.chg > 25 && r != b.down) { b.down = r; if (r) b.swallow = touched(now); else { if (!b.swallow) runAction(hp.xbA[i]); b.swallow = false; } }
  }
  // both held: the third shortcut, once
  if (btn.down && btn2.down && !bothDone && now - max(btn.t0, btn2.t0) >= BOTH_MS) { bothDone = true; btn.held = btn2.held = true; btn.swallow = btn2.swallow = false; inputSrc = 3; shortcut(btnB); inputSrc = 0; }
  encTick(now);
  // alarms wake the screen; otherwise it sleeps after cfg.scrOffMin minutes untouched
  if (alarmActive() || tripBeeping()) { if (!screenOn) screenPower(true); lastInputMs = now; }
  else if (screenOn && cfg.scrOffMin > 0 && now - lastInputMs > (uint32_t)cfg.scrOffMin * 60000UL) screenPower(false);
  // button lights: the screen's brightness while it is on, dim while it sleeps, off if Settings says so;
  // only the button that did something blinks; both flash during an alarm
  uint8_t full = cfg.btnLed ? (uint8_t)constrain(cfg.contrast, 1, 255) : 0;
  uint8_t base = screenOn ? full : (oledOK ? min(full, LED_DIM) : 0), l1 = base, l2 = base;
  if (alarmActive() || tripBeeping()) l1 = l2 = (now / (bnStage == 1 ? 500 : 150)) % 2 ? full : 0;
  else if (now < ledBlinkUntil) { uint8_t b = (now / 100) % 2 ? full : 0; if (ledBlinkMask & 1) l1 = b; if (ledBlinkMask & 2) l2 = b; }
  if (lightTestMs && now - lightTestMs < 4000) { uint32_t k = (now - lightTestMs) / 1000; l1 = k == 0 ? 255 : 0; l2 = k == 1 ? 255 : 0; } else lightTestMs = 0;
  pinLevel(hp.backLed, l1); pinLevel(hp.selLed, l2);
  if (!screenOn) return;
  if (redraw || now - lastDrawMs > (alarmActive() ? 250u : mqOn ? 30u : 1000u)) { drawScreen(); lastDrawMs = now; redraw = false; }
}

// ---- indicator lights: two LEDs on any pins (Settings > Pin mapping), each showing up to three states ----
// Settings > Indicator lights: per light, a list of [state, pattern]; the first state that is true sets the pattern.
// Patterns: s solid, b slow blink (1 a second), f fast blink (4 a second), p a blip every 2 s.
// dim: follow the screen's brightness (and stay faint while it sleeps); slp: off while the screen sleeps.
const char* IND_K[] = { "job", "jpause", "trip", "gpson", "fix", "nofix", "batt", "chg", "sd", "clk", "ap", "wifi", "nowifi" };
const uint8_t IND_N = sizeof IND_K / sizeof IND_K[0];
struct IndRule { uint8_t k; char p; };
struct IndCfg { IndRule r[3]; uint8_t n = 0; bool dim = true, slp = false; } indC[2];
String indParse(const String& js, bool apply) {             // "" if fine, else what's wrong
  IndCfg n[2];
  if (js.length()) {
    JsonDocument d; if (deserializeJson(d, js) || !d.is<JsonArray>()) return "Indicator lights: bad settings";
    int i = 0;
    for (JsonVariant L : d.as<JsonArray>()) {
      if (i >= 2) return "Two indicator lights at most";
      n[i].dim = L["dim"] | true; n[i].slp = L["slp"] | false;
      for (JsonVariant R : L["r"].as<JsonArray>()) {
        if (n[i].n >= 3) return "Three states per light at most";
        String k = R[0] | ""; String p = R[1] | "s"; int ki = -1;
        for (uint8_t j = 0; j < IND_N; j++) if (k == IND_K[j]) ki = j;
        if (ki < 0) return "Unknown state " + k;
        if (p.length() != 1 || !strchr("sbfp", p[0])) return "Unknown pattern " + p;
        for (uint8_t j = 0; j < n[i].n; j++) if (n[i].r[j].k == ki) return "A state is chosen twice for one light";
        n[i].r[n[i].n++] = { (uint8_t)ki, p[0] };
      }
      i++;
    }
  }
  if (apply) { indC[0] = n[0]; indC[1] = n[1]; }
  return "";
}
bool indState(uint8_t k) {
  switch (k) {
    case 0: return jobState().run >= 0;
    case 1: return jobState().pz >= 0;
    case 2: return WT.on;
    case 3: return !gpsAsleep;
    case 4: return !gpsAsleep && gps.fix;
    case 5: return !gpsAsleep && !gps.fix;
    case 6: return battType && !battChg && !isnan(battPct) && battPct < 20;
    case 7: return battType && battChg;
    case 8: return !sdOK;
    case 9: return !timeValid;
    case 10: return apUp;
    case 11: return staUp;
    case 12: return !staUp;
  }
  return false;
}
void indTick() {
  if (!hp.ind[0].length() && !hp.ind[1].length()) return;
  uint32_t now = millis();
  if (!indAt || now - indAt >= 1000) {                      // the states, once a second (the job one reads the time log)
    indAt = now ? now : 1; indMask = 0; bool jobDone = false;
    for (int L = 0; L < 2; L++) for (uint8_t j = 0; j < indC[L].n; j++) {
      uint8_t k = indC[L].r[j].k; if ((k == 0 || k == 1) && jobDone) continue;
      if (k == 0 || k == 1) { JobSt js = jobState(); jobDone = true; if (js.run >= 0) indMask |= 1; if (js.pz >= 0) indMask |= 2; }
      else if (indState(k)) indMask |= 1UL << k;
    }
  }
  for (int L = 0; L < 2; L++) {
    if (!hp.ind[L].length()) continue;
    const IndCfg& c = indC[L]; int8_t cur = -1;
    for (uint8_t j = 0; j < c.n && cur < 0; j++) if (indMask >> c.r[j].k & 1) cur = j;
    indCur[L] = cur;
    bool lit = false;
    if (cur >= 0) switch (c.r[cur].p) {
      case 's': lit = true; break;
      case 'b': lit = (now / 500) % 2 == 0; break;
      case 'f': lit = (now / 125) % 2 == 0; break;
      case 'p': lit = now % 2000 < 120; break;
    }
    uint8_t full = c.dim ? (uint8_t)constrain(cfg.contrast, 1, 255) : 255;
    uint8_t v = !lit ? 0 : screenOn ? full : c.slp ? 0 : c.dim ? min(full, LED_DIM) : full;
    if (lightTestMs && now - lightTestMs < 4000) v = (now - lightTestMs) / 1000 == (uint32_t)(2 + L) ? 255 : 0;
    pinLevel(hp.ind[L], v);
  }
}

// ---- web: timers, find-my-box, BNWAS ----
String alarmsJson() {
  uint32_t now = millis();
  String s = "{\"buzzer\":" + jBool(buzOK) + ",\"timers\":[";
  for (size_t i = 0; i < timers.size(); i++) {
    const KTimer& t = timers[i];
    if (i) s += ",";
    s += "{\"id\":" + String(t.id) + ",\"name\":" + jStr(t.name) + ",\"secs\":" + String(t.secs) + ",\"left\":" + String(t.left(now)) + ",\"ring\":" + jBool(t.ringing) + "}";
  }
  s += "],\"find\":" + jBool(findOn) + ",\"findLeft\":" + String(findOn ? (FIND_MAX_MS - min(FIND_MAX_MS, now - findSince)) / 1000 : 0);
  s += ",\"alert\":\"" + String(cfg.alert == 0 ? "sound" : cfg.alert == 1 ? "vibrate" : "both") + "\",\"motor\":" + jBool(hapOK);
  s += ",\"bnwas\":{\"on\":" + jBool(cfg.bnwasOn) + ",\"min\":" + String(cfg.bnwasMin) + ",\"stage\":" + String(bnStage) + ",\"left\":" + String(bnLeftS(now)) + "}}";
  return s;
}
void handleTimersGet() { server.send(200, "application/json", alarmsJson()); }
void handleTimersPost() {
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) { sendErr(400, "Bad request"); return; }
  String op = d["op"] | "";
  uint32_t now = millis();
  if (op == "add") {
    uint32_t secs = d["secs"] | 0;
    if (secs < 1 || secs > 86400) { sendErr(400, "Timer must be 1 second to 24 hours"); return; }
    if (timers.size() >= MAX_TIMERS) { sendErr(400, "Up to 8 timers at once"); return; }
    String name = ascii(d["name"] | ""); name.trim(); if (!name.length()) name = "Timer"; name = name.substring(0, 24);
    timers.push_back({ timerNextId++, secs, now + secs * 1000UL, 0, name, false });
  } else if (op == "del") {
    uint32_t id = d["id"] | 0;
    for (size_t i = 0; i < timers.size(); i++) if (timers[i].id == id) { timers.erase(timers.begin() + i); break; }
  } else if (op == "stop") {
    silence(false, now);                                     // timers and find-my-box; BNWAS needs the box's button
  } else if (op == "find") {
    findOn = d["on"] | false; findSince = now;
  } else if (op == "bnwas") {
    bool on = d["on"] | cfg.bnwasOn; int mn = d["min"] | cfg.bnwasMin;
    if (mn < 1 || mn > 240) { sendErr(400, "Interval must be 1 to 240 minutes"); return; }
    bool chg = on != cfg.bnwasOn || mn != cfg.bnwasMin;
    cfg.bnwasOn = on; cfg.bnwasMin = mn;
    if (chg) { bnwasRestart(now); if (flashOK && !cfgSave()) { sendErr(503, "Could not save"); return; } }
  } else if (op == "test") {                              // the dashboard's Test: one beep, buzz or both
    beepOnce();
  } else if (op == "alert") {                             // {"op":"alert","mode":"sound"|"vibrate"|"both"}
    String m = d["mode"] | ""; int a = m == "sound" ? 0 : m == "vibrate" ? 1 : m == "both" ? 2 : -1;
    if (a < 0) { sendErr(400, "mode: sound, vibrate or both"); return; }
    if (a != cfg.alert) { cfg.alert = a; if (flashOK && !cfgSave()) { sendErr(503, "Could not save"); return; } }
  } else if (op == "screen") {                            // {"op":"screen","offMin":5} and/or {"on":true}
    if (d["offMin"].is<int>()) {
      int m = d["offMin"];
      if (m < 0 || m > 240) { sendErr(400, "Sleep time must be 0 (never) to 240 minutes"); return; }
      if (m != cfg.scrOffMin) { cfg.scrOffMin = m; if (flashOK && !cfgSave()) { sendErr(503, "Could not save"); return; } }
    }
    if (d["on"].is<bool>()) { lastInputMs = now; screenPower(d["on"].as<bool>()); }
    if (d["btnLed"].is<bool>() && d["btnLed"].as<bool>() != cfg.btnLed) {
      cfg.btnLed = d["btnLed"].as<bool>(); if (flashOK && !cfgSave()) { sendErr(503, "Could not save"); return; }
    }
    if (d["clHide"].is<bool>() && d["clHide"].as<bool>() != cfg.clHideDone) {
      cfg.clHideDone = d["clHide"].as<bool>(); clVer = 0xFFFFFFFF;            // re-read the list
      if (flashOK && !cfgSave()) { sendErr(503, "Could not save"); return; }
    }
  } else if (op == "dev") {                               // {"op":"dev","clkMan":bool,"gpsDet":0-2,"tripAskMin":10-480,"envMin":1|2|5|10|15}
    Cfg o = cfg;
    if (d["clkMan"].is<bool>()) cfg.clkMan = d["clkMan"];
    if (d["gpsDet"].is<int>()) cfg.gpsDet = constrain(d["gpsDet"].as<int>(), 0, 2);
    if (d["tripAskMin"].is<int>()) cfg.tripAskMin = constrain(d["tripAskMin"].as<int>(), 10, 480);
    if (d["envMin"].is<int>()) { if (!envMinOk(d["envMin"].as<int>())) { cfg = o; sendErr(400, "Climate log: 1, 2, 5, 10 or 15 minutes"); return; } cfg.envMin = d["envMin"]; }
    if (d["ind"].is<JsonArray>()) {                       // [{"r":[[state,pattern],...],"dim":bool,"slp":bool},{...}]
      String js; serializeJson(d["ind"], js); String e = indParse(js, false);
      if (e.length()) { cfg = o; sendErr(400, e.c_str()); return; }
      cfg.ind = js;
    }
    if (flashOK && !cfgSave()) { cfg = o; sendErr(503, "Could not save"); return; }
    if (cfg.envMin != o.envMin) lastEnvSlot = 0;            // the next log waits for the new interval's boundary
    if (cfg.clkMan != o.clkMan) ntpApply();                 // Manual stops internet time; Automatic starts it on the house Wi-Fi
    if (cfg.ind != o.ind) { indParse(cfg.ind, true); indAt = 0; }
  } else { sendErr(400, "Unknown op"); return; }
  redraw = true;
  alarmsTick(now);
  server.send(200, "application/json", alarmsJson());
}

// ======================================================================
//  Web
// ======================================================================
String jStr(const String& s) {
  String o = "\"";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o + "\"";
}
String jNum(float v, int dec) { return isnan(v) ? String("null") : String(v, dec); }
String jBool(bool b) { return b ? "true" : "false"; }
String macStr(const uint8_t* m) {
  char b[18]; snprintf(b, sizeof b, "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(b);
}

void sendErr(int code, const char* msg) {
  server.send(code, "application/json", String("{\"ok\":false,\"err\":") + jStr(msg) + "}");
}

// update page, gzipped to save room in flash. Its source, to change it (then gzip -9 it into the bytes below):
/*
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Life Logger update</title>
<style>body{font:15px system-ui;margin:40px auto;max-width:480px;padding:0 16px;background:#14171c;color:#e6e8eb}
input,button{font:inherit;margin:6px 0;padding:8px;width:100%;box-sizing:border-box}
button{background:#3b82f6;color:#fff;border:0;border-radius:6px;cursor:pointer}#m{margin-top:12px}.mut{color:#8b95a3;font-size:13px}</style></head>
<body><h2>Life Logger update</h2><p>Pick the <b>.lbk</b> update file (dashboard + firmware in one). If this page came up instead of the
dashboard, the box has no dashboard yet: the .lbk puts it back.</p>
<input id="pw" type="password" placeholder="Box password (none on a new box)"><input id="f" type="file" accept=".lbk,.bin">
<button onclick="go()">Upload</button><div id="m"></div><p class="mut">The box restarts when it's done (about 20 s).</p>
<script>function go(){const f=document.getElementById('f').files[0];if(!f)return;
const fd=new FormData();fd.append('firmware',f);const x=new XMLHttpRequest();
x.open('POST','/update?pw='+encodeURIComponent(document.getElementById('pw').value));
x.upload.onprogress=e=>{document.getElementById('m').textContent=Math.round(100*e.loaded/e.total)+'%'};
x.onload=()=>{document.getElementById('m').textContent=x.responseText;if(x.status==200)setTimeout(()=>location.href='/',20000)};
x.send(fd)}</script></body></html>
*/
const uint8_t UPDATE_HTML_GZ[] PROGMEM = {
  0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x95, 0x54, 0x4d, 0x6f, 0xdc, 0x36, 0x10, 0xbd, 0xef, 0xaf, 0x60, 0x64, 0x04, 0x92,
  0xea, 0x5d, 0x49, 0x6b, 0x3b, 0xee, 0x56, 0x5f, 0x05, 0x92, 0xb6, 0xa8, 0x01, 0x07, 0x0d, 0x52, 0x07, 0x28, 0x50, 0xf4, 0x40, 0x91, 0xa3, 0x15,
  0x61, 0x89, 0x64, 0x48, 0xca, 0xbb, 0x5b, 0x63, 0xff, 0x7b, 0x87, 0x92, 0x9c, 0xf4, 0xd0, 0x1c, 0x7a, 0xd9, 0x95, 0xe6, 0xe3, 0xcd, 0x9b, 0x37,
  0x33, 0x2a, 0x5f, 0x71, 0xc5, 0xdc, 0x49, 0x03, 0xe9, 0xdc, 0xd0, 0xd7, 0xe5, 0xf2, 0x0b, 0x94, 0xd7, 0xe5, 0x00, 0x8e, 0x12, 0xd6, 0x51, 0x63,
  0xc1, 0x55, 0xc1, 0xe8, 0xda, 0xcd, 0x2e, 0xa8, 0x57, 0xb3, 0x59, 0xd2, 0x01, 0xaa, 0xe0, 0x49, 0xc0, 0x41, 0x2b, 0xe3, 0x02, 0xc2, 0x94, 0x74,
  0x20, 0x31, 0xec, 0x20, 0xb8, 0xeb, 0x2a, 0x0e, 0x4f, 0x82, 0xc1, 0x66, 0x7a, 0x59, 0x0b, 0x29, 0x9c, 0xa0, 0xfd, 0xc6, 0x32, 0xda, 0x43, 0xb5,
  0x0d, 0xea, 0xd2, 0x09, 0xd7, 0x43, 0x7d, 0x2f, 0x5a, 0x20, 0xf7, 0x6a, 0xbf, 0x07, 0x43, 0x46, 0xcd, 0xa9, 0x83, 0x32, 0x9d, 0x3d, 0xab, 0xd2,
  0xba, 0x13, 0xfe, 0x37, 0x8a, 0x9f, 0x9e, 0x5b, 0x84, 0xce, 0xb7, 0x6f, 0xf4, 0x91, 0xd8, 0x93, 0x75, 0x30, 0x6c, 0x46, 0x51, 0x0c, 0xd4, 0xec,
  0x85, 0xcc, 0x6f, 0x32, 0xb4, 0xd2, 0xd1, 0x29, 0x34, 0x1c, 0xe7, 0x6a, 0xf9, 0xcd, 0x0e, 0x8d, 0x85, 0xa6, 0x9c, 0x0b, 0xb9, 0xcf, 0x33, 0xb2,
  0xbd, 0xc5, 0xd7, 0x86, 0xb2, 0xc7, 0xbd, 0x51, 0xa3, 0xe4, 0xf9, 0xc5, 0xf6, 0x66, 0xfb, 0xfd, 0x96, 0x15, 0x4c, 0xf5, 0xca, 0xe4, 0x17, 0x70,
  0x0b, 0x3b, 0x68, 0xce, 0x2b, 0x21, 0xf5, 0xe8, 0xd6, 0xcd, 0xe8, 0x9c, 0x92, 0x73, 0x49, 0x21, 0x3b, 0x30, 0xc2, 0xbd, 0xd4, 0x42, 0x18, 0x92,
  0x7d, 0xc1, 0xdd, 0x21, 0xe8, 0x5c, 0x6f, 0x9b, 0x65, 0xaf, 0x8b, 0x46, 0x1d, 0x37, 0x56, 0xfc, 0xed, 0x3d, 0x8d, 0x32, 0x1c, 0xcc, 0x06, 0x2d,
  0xe7, 0xd5, 0x02, 0xf7, 0xef, 0xea, 0xd7, 0xcd, 0xee, 0xaa, 0xbd, 0x7d, 0xa9, 0xde, 0xb6, 0x6d, 0x31, 0x27, 0xe4, 0xd9, 0xf2, 0xb0, 0x31, 0x94,
  0x8b, 0xd1, 0xfa, 0x7a, 0x05, 0x1b, 0x8d, 0xc5, 0x30, 0xad, 0x04, 0xaa, 0x6b, 0xce, 0x17, 0xc3, 0xf3, 0x4c, 0x66, 0xe3, 0x94, 0xce, 0xb7, 0x57,
  0xfa, 0x78, 0x4e, 0x86, 0xd1, 0x3d, 0x2f, 0x60, 0xbb, 0xe6, 0x87, 0x37, 0xf4, 0xba, 0xf0, 0xe4, 0x3d, 0x19, 0xc8, 0xb7, 0xd7, 0x18, 0x51, 0xa6,
  0xb3, 0x96, 0x65, 0x3a, 0x4d, 0x75, 0x55, 0x7a, 0x51, 0x71, 0xc4, 0x57, 0xff, 0xa9, 0x3f, 0x9a, 0x4b, 0x5d, 0x7f, 0x10, 0xec, 0x91, 0xb8, 0x0e,
  0x48, 0xd9, 0xd4, 0x49, 0xdf, 0x3c, 0x96, 0x69, 0x53, 0x2f, 0x21, 0xa4, 0x15, 0x3d, 0x90, 0x88, 0x53, 0xdb, 0x35, 0x8a, 0x1a, 0x4e, 0x2e, 0xd1,
  0x62, 0x86, 0x03, 0x35, 0x40, 0x84, 0x24, 0x4a, 0x42, 0x9c, 0x90, 0xbb, 0x16, 0xb3, 0x85, 0x25, 0x9a, 0xee, 0x81, 0x30, 0x5c, 0x15, 0x4c, 0x46,
  0x2f, 0x4e, 0x8f, 0x72, 0xa2, 0xbc, 0x13, 0x56, 0x5f, 0x10, 0xd6, 0x53, 0x25, 0xd4, 0x8b, 0x74, 0xd4, 0x12, 0xa9, 0xc8, 0x57, 0xec, 0x13, 0xb8,
  0x7c, 0xf2, 0x7a, 0x12, 0x04, 0x07, 0x64, 0x89, 0x70, 0xc4, 0xcb, 0x99, 0x94, 0xa9, 0xc6, 0x5e, 0xa6, 0xa9, 0x11, 0xc1, 0xab, 0x40, 0x1f, 0x02,
  0xe2, 0xf7, 0x18, 0x9f, 0xa8, 0xb5, 0x07, 0x94, 0x32, 0x20, 0xba, 0xa7, 0x0c, 0x3a, 0xd5, 0xa3, 0xaa, 0x55, 0xf0, 0x16, 0x0b, 0xbc, 0xb8, 0x48,
  0x24, 0x91, 0x28, 0x92, 0x25, 0xb8, 0xc9, 0x70, 0xf0, 0xc5, 0x63, 0x5c, 0xcb, 0xaf, 0x68, 0xed, 0x0b, 0x98, 0xef, 0x36, 0x20, 0x94, 0x31, 0xd0,
  0xb8, 0xdc, 0x9e, 0xc6, 0x3a, 0x69, 0x84, 0xf4, 0x77, 0x30, 0x0f, 0x17, 0x41, 0x58, 0x8f, 0x72, 0x55, 0xc1, 0x5e, 0x45, 0x08, 0xf2, 0x49, 0xf7,
  0x8a, 0x72, 0x14, 0x6c, 0xf2, 0xd6, 0x25, 0x17, 0x4f, 0x13, 0xe2, 0x80, 0xf8, 0x29, 0xbe, 0xa0, 0xbc, 0x84, 0xf5, 0xc8, 0x03, 0x4d, 0xa3, 0x0b,
  0xea, 0x87, 0xa5, 0x77, 0x03, 0xd6, 0x51, 0x83, 0x0d, 0x1e, 0x3a, 0x90, 0xd8, 0x65, 0x68, 0x09, 0xf7, 0x1c, 0x23, 0xda, 0x28, 0x24, 0x75, 0x95,
  0x11, 0x1b, 0x2f, 0x4d, 0x5b, 0x66, 0x84, 0x76, 0x75, 0x3b, 0x4a, 0xe6, 0x04, 0x32, 0xf0, 0x85, 0x71, 0x07, 0x50, 0x5e, 0xd2, 0x56, 0x78, 0xce,
  0xe3, 0x80, 0x97, 0x98, 0xec, 0xc1, 0xfd, 0xdc, 0x83, 0x7f, 0x7c, 0x7b, 0xba, 0xe3, 0x51, 0xd8, 0x86, 0x71, 0xe2, 0xbb, 0xb1, 0x7f, 0x66, 0x7f,
  0x15, 0xa2, 0x8d, 0x5e, 0xb5, 0xb1, 0x01, 0x37, 0x1a, 0x59, 0xac, 0x96, 0x5c, 0x5e, 0x79, 0x2d, 0x7e, 0x51, 0x66, 0xf8, 0x89, 0x3a, 0x1a, 0xc5,
  0x45, 0xcb, 0x13, 0xaa, 0x35, 0x48, 0x9f, 0xbd, 0xcc, 0x38, 0x5c, 0xb7, 0x71, 0x31, 0xc7, 0x1f, 0xa7, 0xf0, 0x3f, 0xde, 0xdf, 0xff, 0xea, 0x9c,
  0xfe, 0x08, 0x9f, 0x47, 0xec, 0x00, 0x93, 0x56, 0xc7, 0x44, 0x61, 0x4e, 0x14, 0x7e, 0xf8, 0xed, 0xf7, 0x87, 0x70, 0x1d, 0xa6, 0xf3, 0xde, 0xfc,
  0xa8, 0x0f, 0x55, 0x78, 0x09, 0x92, 0x29, 0x0e, 0x9f, 0x3e, 0xde, 0xbd, 0x53, 0x83, 0xc6, 0xf6, 0xa4, 0x8b, 0xbe, 0x49, 0x58, 0x1f, 0x90, 0xf1,
  0x13, 0xed, 0x47, 0x88, 0x27, 0xd4, 0x71, 0x52, 0x36, 0x51, 0x52, 0x1b, 0xb5, 0x47, 0xb9, 0x6c, 0x05, 0x55, 0xfd, 0xfc, 0xcd, 0xf4, 0x01, 0xb3,
  0x1d, 0x1c, 0xdd, 0xbb, 0xe5, 0xcb, 0xf4, 0x9e, 0xba, 0x2e, 0x99, 0x8e, 0x30, 0xc2, 0x93, 0xfd, 0x0e, 0x12, 0x8f, 0x06, 0x3c, 0x85, 0xc4, 0x29,
  0x47, 0xfb, 0xf8, 0x32, 0x7c, 0x1d, 0x9e, 0x27, 0xf6, 0xd2, 0x7b, 0xaa, 0x28, 0xfe, 0x3f, 0xe8, 0xc7, 0x04, 0x19, 0x61, 0x47, 0x16, 0x1e, 0xd0,
  0xea, 0xf5, 0x3d, 0x26, 0x38, 0x50, 0x37, 0xda, 0xaa, 0xba, 0xca, 0xb2, 0x18, 0x3f, 0xa1, 0x0f, 0x62, 0x00, 0x1c, 0x65, 0xe4, 0x81, 0x7b, 0xc5,
  0xa8, 0x9f, 0x5d, 0xd2, 0x19, 0x68, 0xab, 0x30, 0x0d, 0xd7, 0x18, 0x84, 0x61, 0x13, 0x01, 0xeb, 0x15, 0x6f, 0x79, 0xec, 0x4f, 0x77, 0x1e, 0x35,
  0xae, 0xd3, 0x74, 0xb4, 0xe9, 0xf4, 0x79, 0xfe, 0x07, 0xab, 0xc1, 0xed, 0xaa, 0xb4, 0x05, 0x00, 0x00,
};

// login page, gzipped to save room in flash. Its source, to change it (then gzip -9 it into the bytes below):
/*
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Life Logger</title>
<style>body{font:15px system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;background:#14171c;color:#e6e8eb}
.b{width:100%;max-width:360px;margin:16px;padding:26px;background:#1d2128;border:1px solid #2c323c;border-radius:14px}
h1{font-size:22px;margin:0 0 4px}p{color:#8b95a3;font-size:13.5px;margin:0 0 16px}
input[type=password]{font:inherit;width:100%;box-sizing:border-box;padding:10px;border-radius:8px;border:1px solid #39404b;background:#14171c;color:inherit}
label{display:flex;gap:8px;align-items:center;margin:12px 0;font-size:14px}button{font:inherit;width:100%;padding:10px;border:0;border-radius:8px;background:#3b82f6;color:#fff;cursor:pointer}
#m{min-height:20px;margin-top:10px;font-size:13.5px;color:#f87171}.f{text-align:center;color:#6b7480;font-size:12px;margin-top:14px}
@media (prefers-color-scheme:light){body{background:#f2f4f7;color:#1c2128}.b{background:#fff;border-color:#dde2e8}input[type=password]{background:#fff;border-color:#c9d0d9}}</style></head>
<body><form class="b" onsubmit="go();return false"><h1>Life Logger</h1><p>You're reaching the box over your home Wi-Fi, so it asks for the box password. On the box's own hotspot it doesn't.</p>
<input id="pw" type="password" placeholder="Box password" autocomplete="current-password" autofocus>
<label><input id="k" type="checkbox" checked> Remember this device</label><button>Open Life Logger</button><div id="m"></div><div class="f">Life Logger · Aspect Wave LLC</div></form>
<script>async function go(){const m=document.getElementById('m');m.textContent='';
try{const r=await fetch('/api/login',{method:'POST',headers:{'Content-Type':'text/plain'},body:JSON.stringify({pw:document.getElementById('pw').value,keep:document.getElementById('k').checked})});
if(r.ok){location.reload();return}const j=await r.json().catch(()=>({}));m.textContent=j.err||('Error '+r.status)}catch(e){m.textContent='The box did not answer'}}</script></body></html>
*/
const uint8_t LOGIN_HTML_GZ[] PROGMEM = {
  0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x7d, 0x56, 0x5d, 0x6f, 0xdb, 0x36, 0x14, 0x7d, 0xf7, 0xaf, 0xe0, 0x14, 0x0c, 0xb2,
  0x31, 0xcb, 0xb6, 0xec, 0x34, 0x71, 0x24, 0xd9, 0xd8, 0x5a, 0xb4, 0x40, 0x87, 0x60, 0x19, 0xda, 0x0e, 0xc5, 0x30, 0xec, 0x81, 0xa2, 0xae, 0x2c,
  0xc6, 0x92, 0x48, 0x90, 0x94, 0x3f, 0xa6, 0xea, 0x77, 0xed, 0x7d, 0xbf, 0x6c, 0x97, 0x92, 0x9c, 0xda, 0x5d, 0xb2, 0x97, 0x44, 0xa2, 0xee, 0xd7,
  0x39, 0xf7, 0xdc, 0x4b, 0x47, 0xdf, 0x25, 0x82, 0x99, 0xa3, 0x04, 0x92, 0x99, 0x22, 0x5f, 0x47, 0xfd, 0x5f, 0xa0, 0xc9, 0x3a, 0x2a, 0xc0, 0x50,
  0xc2, 0x32, 0xaa, 0x34, 0x98, 0x95, 0x53, 0x99, 0xd4, 0x5b, 0x3a, 0xeb, 0x41, 0x77, 0x5c, 0xd2, 0x02, 0x56, 0xce, 0x8e, 0xc3, 0x5e, 0x0a, 0x65,
  0x1c, 0xc2, 0x44, 0x69, 0xa0, 0x44, 0xb3, 0x3d, 0x4f, 0x4c, 0xb6, 0x4a, 0x60, 0xc7, 0x19, 0x78, 0xed, 0xcb, 0x98, 0x97, 0xdc, 0x70, 0x9a, 0x7b,
  0x9a, 0xd1, 0x1c, 0x56, 0xbe, 0xb3, 0x8e, 0x0c, 0x37, 0x39, 0xac, 0xef, 0x79, 0x0a, 0xe4, 0x5e, 0x6c, 0x36, 0xa0, 0xa2, 0x69, 0x77, 0x34, 0x88,
  0xb4, 0x39, 0xe2, 0xff, 0x58, 0x24, 0xc7, 0x3a, 0xc5, 0x98, 0x81, 0xff, 0x4a, 0x1e, 0x88, 0x3e, 0x6a, 0x03, 0x85, 0x57, 0xf1, 0xb1, 0x47, 0xa5,
  0xcc, 0xc1, 0xeb, 0x0e, 0xc6, 0xce, 0x47, 0xd8, 0x08, 0x20, 0xbf, 0xbd, 0x77, 0xc6, 0x1f, 0x44, 0x2c, 0x8c, 0x18, 0x6b, 0x5a, 0x6a, 0x4f, 0x83,
  0xe2, 0x69, 0x58, 0x50, 0xb5, 0xe1, 0x65, 0x30, 0x0b, 0x0b, 0x5e, 0x7a, 0x19, 0xf0, 0x4d, 0x86, 0xd1, 0x66, 0xb3, 0x5d, 0x16, 0x26, 0x5c, 0xcb,
  0x9c, 0x1e, 0x83, 0x34, 0x87, 0x43, 0x48, 0x73, 0xbe, 0x29, 0x3d, 0x8e, 0xe1, 0x74, 0xc0, 0x10, 0x02, 0xa8, 0xf0, 0xb1, 0xd2, 0x86, 0xa7, 0x47,
  0xaf, 0x07, 0x75, 0x3a, 0x8e, 0x29, 0xdb, 0x6e, 0x94, 0xa8, 0xca, 0x24, 0xb8, 0xf2, 0xaf, 0xfd, 0x5b, 0x9f, 0x85, 0x4c, 0xe4, 0x42, 0x05, 0x57,
  0x70, 0x03, 0x4b, 0x88, 0x9b, 0xc1, 0x24, 0xae, 0x5b, 0xc8, 0x36, 0xcf, 0xf7, 0x58, 0xc0, 0xa1, 0x63, 0x20, 0x58, 0xdc, 0xcc, 0xe4, 0xe1, 0x54,
  0x90, 0x7f, 0x83, 0xcf, 0x92, 0x26, 0x09, 0x2f, 0x37, 0xc1, 0xdc, 0xbe, 0x5c, 0x04, 0x4e, 0xe6, 0xfe, 0x7c, 0x19, 0xc6, 0x42, 0x25, 0xa0, 0x02,
  0xdf, 0x82, 0x17, 0x39, 0x4f, 0xc8, 0xd5, 0x9c, 0x2d, 0xe6, 0x0b, 0xd6, 0x7f, 0xf0, 0x14, 0x4d, 0x78, 0xa5, 0x03, 0xff, 0x5a, 0x1e, 0x9a, 0x41,
  0xe6, 0xb7, 0x5c, 0x79, 0x9a, 0xff, 0x05, 0xc1, 0x7c, 0xfe, 0x35, 0xd5, 0x8c, 0xcc, 0x88, 0xb5, 0x90, 0x75, 0x5f, 0xe8, 0x32, 0xbe, 0x7b, 0x45,
  0x17, 0xe1, 0x57, 0x6b, 0x7f, 0x31, 0x79, 0x75, 0x69, 0x6f, 0xcb, 0x6b, 0x06, 0xbc, 0x94, 0x95, 0xf9, 0xc3, 0x0a, 0x63, 0x25, 0xa9, 0xd6, 0x7b,
  0xcc, 0xfa, 0x67, 0xd7, 0x10, 0x5e, 0x66, 0x48, 0xaf, 0x09, 0xcf, 0x80, 0xc6, 0xe2, 0x60, 0xa3, 0x59, 0x3c, 0x7d, 0x79, 0x78, 0xf2, 0x04, 0xd1,
  0xb7, 0xd8, 0x2f, 0xcb, 0x5e, 0x3e, 0x9d, 0x9c, 0x23, 0x5c, 0xdc, 0x5d, 0xcf, 0xae, 0xe3, 0x97, 0x69, 0xee, 0x33, 0x37, 0x83, 0x9c, 0xc6, 0x90,
  0xd7, 0x17, 0x5d, 0xdc, 0x50, 0xd9, 0x46, 0x7d, 0xa6, 0x9b, 0x27, 0xda, 0x91, 0x17, 0x32, 0x3b, 0x87, 0x6e, 0x99, 0x89, 0x2b, 0x63, 0x44, 0xf9,
  0x22, 0xb2, 0x67, 0x30, 0xa0, 0xa0, 0x9e, 0x01, 0x73, 0x56, 0xf3, 0x22, 0x5e, 0xce, 0xd3, 0x9b, 0x93, 0x34, 0xd2, 0x34, 0x0d, 0x59, 0xa5, 0x34,
  0x3e, 0x4b, 0xc1, 0x6d, 0x41, 0xcd, 0xe0, 0xaa, 0xa8, 0xcf, 0x34, 0x39, 0xff, 0xaa, 0x0d, 0xcf, 0x08, 0xd9, 0xe5, 0xfa, 0x4f, 0x87, 0x4e, 0xe1,
  0x96, 0xb7, 0xc8, 0x48, 0x33, 0x49, 0x6b, 0x03, 0x07, 0xe3, 0xb5, 0x70, 0x4f, 0x40, 0x7b, 0x93, 0x9b, 0xf8, 0xf6, 0x7a, 0x79, 0x01, 0x74, 0xfe,
  0x4d, 0x82, 0x56, 0x34, 0x3f, 0x16, 0x90, 0x70, 0x4a, 0x86, 0x52, 0x41, 0x0a, 0x4a, 0x7b, 0xad, 0x37, 0xce, 0x68, 0x06, 0x05, 0x04, 0xb9, 0xad,
  0x6c, 0x54, 0xb7, 0x63, 0x78, 0x8e, 0x2d, 0x9d, 0xa7, 0xd7, 0xe9, 0xed, 0x29, 0x93, 0xcf, 0xac, 0x56, 0x1b, 0x54, 0xfd, 0x85, 0x0d, 0x22, 0xee,
  0x19, 0xea, 0xed, 0x92, 0x04, 0xe6, 0xb0, 0x6c, 0x9e, 0xd5, 0xd4, 0xff, 0x7b, 0xb2, 0xbb, 0x64, 0x96, 0xdc, 0x35, 0x4d, 0x34, 0xed, 0xf6, 0x42,
  0x34, 0x6d, 0x57, 0xd3, 0x20, 0xb2, 0x95, 0xad, 0xa3, 0x54, 0xa8, 0x82, 0xb0, 0x1c, 0x83, 0xad, 0x9c, 0xd8, 0x21, 0xa2, 0xd4, 0x55, 0x5c, 0x70,
  0xdc, 0x42, 0x1b, 0x31, 0x1c, 0x85, 0x0a, 0x4c, 0xa5, 0x4a, 0x92, 0xd2, 0x5c, 0x03, 0x2e, 0x9d, 0xcc, 0xbf, 0xdc, 0x38, 0xf8, 0x1e, 0xc9, 0xf5,
  0xef, 0xa2, 0x72, 0x15, 0x10, 0x05, 0x94, 0x65, 0xd8, 0x69, 0x62, 0x32, 0x20, 0xa8, 0x5e, 0x22, 0x76, 0xa0, 0xc8, 0x51, 0x54, 0x8a, 0x64, 0xa2,
  0x00, 0xf2, 0x99, 0x7b, 0xef, 0xf8, 0x18, 0x95, 0x4a, 0xb8, 0x21, 0x54, 0x6f, 0x35, 0xc1, 0xdc, 0x4f, 0xc6, 0x27, 0x38, 0x13, 0xf2, 0x50, 0x9e,
  0x0e, 0x5d, 0x4d, 0xc4, 0xbe, 0x44, 0x6f, 0xa3, 0xa5, 0x30, 0xd6, 0x2d, 0x11, 0xa0, 0x4b, 0xd7, 0x4c, 0xa2, 0xa9, 0x44, 0x04, 0x2d, 0x1b, 0x84,
  0x27, 0x2b, 0x47, 0xee, 0x1d, 0xd2, 0xb2, 0xe2, 0x9c, 0xe2, 0x38, 0x04, 0x95, 0xcd, 0x20, 0x13, 0x39, 0x72, 0xb1, 0x72, 0x5e, 0x9f, 0xa5, 0x70,
  0x08, 0xad, 0x8c, 0x60, 0xa2, 0xc0, 0x35, 0x68, 0xd0, 0x05, 0xb5, 0xa5, 0xb0, 0xfd, 0xde, 0xe5, 0xf7, 0x54, 0xb0, 0x4a, 0x63, 0x92, 0x76, 0x52,
  0xd6, 0x67, 0xb9, 0xb6, 0xa7, 0x54, 0xd8, 0x67, 0xb6, 0xc5, 0x32, 0x71, 0x75, 0xdb, 0x27, 0x48, 0xd6, 0xe4, 0x03, 0x76, 0xbe, 0x88, 0xc1, 0xc2,
  0xe2, 0x9a, 0x74, 0x2b, 0x3c, 0x9a, 0xf6, 0x21, 0xba, 0x41, 0x59, 0x3f, 0x48, 0x28, 0xc9, 0x05, 0x8d, 0xfd, 0x87, 0x28, 0xe1, 0xbb, 0x36, 0x43,
  0x81, 0x54, 0x4f, 0xf1, 0xa5, 0x3b, 0xe9, 0x9b, 0x93, 0x3a, 0xe7, 0xdc, 0x93, 0x7f, 0xfe, 0x26, 0x3f, 0x69, 0x09, 0xcc, 0x90, 0xcf, 0x74, 0x87,
  0xa7, 0xf7, 0x6f, 0x7a, 0x97, 0xa9, 0xed, 0xa8, 0xbd, 0x05, 0x98, 0xe2, 0xd2, 0xac, 0xa9, 0x3e, 0x96, 0x8c, 0xa4, 0x55, 0xc9, 0x0c, 0x17, 0x25,
  0xb1, 0x5d, 0xc5, 0x4d, 0x56, 0x6a, 0x43, 0x8a, 0x15, 0xde, 0x5b, 0x55, 0x81, 0xc8, 0x27, 0x1b, 0x30, 0x6f, 0x73, 0xb0, 0x8f, 0xaf, 0x8f, 0xef,
  0x93, 0xa1, 0x5b, 0xb8, 0xa3, 0xb0, 0x98, 0xd8, 0xf1, 0x78, 0xd3, 0x5f, 0x4a, 0xae, 0x1b, 0x0e, 0x8c, 0x3a, 0xf6, 0xae, 0x6a, 0x45, 0xf7, 0x14,
  0xdb, 0x91, 0x82, 0x61, 0xd9, 0xd0, 0x9d, 0x52, 0xc9, 0xa7, 0xb9, 0xc0, 0xe1, 0x70, 0xc7, 0x35, 0xde, 0x6d, 0x99, 0x48, 0x02, 0xf7, 0xd7, 0x87,
  0x8f, 0x9f, 0xdc, 0xb1, 0xd5, 0x1a, 0x8e, 0x46, 0x50, 0xbb, 0x7d, 0x24, 0xef, 0x13, 0x72, 0xe7, 0x06, 0xae, 0x0d, 0x3e, 0xc5, 0x16, 0xa1, 0x4f,
  0x33, 0xb6, 0x52, 0x0c, 0x7e, 0xfe, 0xf8, 0xf0, 0xcb, 0x44, 0x1b, 0x85, 0x12, 0xc2, 0x9b, 0x63, 0x58, 0xcb, 0x7d, 0xf0, 0x62, 0x81, 0x72, 0xef,
  0x8e, 0x26, 0x3b, 0x9a, 0x57, 0x30, 0xde, 0x02, 0xc8, 0x97, 0x0d, 0xb7, 0x68, 0xd7, 0x77, 0xa7, 0x19, 0x35, 0xa3, 0x70, 0xc0, 0xd3, 0xa1, 0x9a,
  0x88, 0xed, 0xa8, 0xce, 0x05, 0xa3, 0x96, 0x92, 0x89, 0x82, 0x5c, 0xd0, 0xe4, 0x49, 0xec, 0x4d, 0x07, 0xf1, 0xb1, 0x87, 0xa8, 0x26, 0x8f, 0x5a,
  0x94, 0x43, 0x8c, 0x42, 0x2d, 0xd6, 0xe1, 0x68, 0xb5, 0x1e, 0xd6, 0xcd, 0xe8, 0x5b, 0x7e, 0x1e, 0x27, 0xa0, 0xd4, 0x97, 0x2f, 0x43, 0xf7, 0xad,
  0x52, 0xa8, 0x6a, 0xf7, 0x07, 0x85, 0x50, 0xa8, 0xa9, 0xf4, 0xa8, 0xe9, 0x1c, 0x61, 0x54, 0x7f, 0xc3, 0xe8, 0xa7, 0x5e, 0xf9, 0x09, 0x2e, 0xee,
  0x12, 0xe5, 0x8d, 0xd7, 0xee, 0x1e, 0x94, 0xdb, 0x4e, 0x6a, 0xd7, 0x3b, 0x54, 0x46, 0x3b, 0xa3, 0xd3, 0xf6, 0x27, 0xc5, 0xbf, 0x31, 0x82, 0x5d,
  0xbc, 0x68, 0x08, 0x00, 0x00,
};

void sendGz(const uint8_t* d, size_t n) { server.sendHeader("Content-Encoding", "gzip"); server.send_P(200, "text/html", (const char*)d, n); }
const char* uiSource() {
  if (sdOK && SD.exists("/www/index.html")) return "sd";              // test override on the card
  if (flashOK && LittleFS.exists("/www/index.html.gz")) return "flash";
  if (flashOK && LittleFS.exists("/www/index.html")) return "flash";
  return "none";
}

void handleRoot() {
  if (!authed()) { server.sendHeader("Cache-Control", "no-store"); sendGz(LOGIN_HTML_GZ, sizeof LOGIN_HTML_GZ); return; }
  const char* src = uiSource();
  if (!strcmp(src, "none")) { sendGz(UPDATE_HTML_GZ, sizeof UPDATE_HTML_GZ); return; }   // first install: ask for the .lbk
  fs::FS& fs = !strcmp(src, "sd") ? (fs::FS&)SD : (fs::FS&)LittleFS;
  bool gz = strcmp(src, "flash") == 0 && LittleFS.exists("/www/index.html.gz");
  File f = fs.open(gz ? "/www/index.html.gz" : "/www/index.html", FILE_READ);
  server.sendHeader("Cache-Control", "no-cache");
  server.streamFile(f, "text/html");                                  // .gz -> Content-Encoding: gzip
  f.close();
}

// one "key":value pair of a JSON object (v is already JSON); smaller code than chains of String +
void kv(String& s, const char* k, const String& v) { s += ",\""; s += k; s += "\":"; s += v; }
void kvB(String& s, const char* k, bool v) { kv(s, k, v ? "true" : "false"); }
void kvU(String& s, const char* k, uint32_t v) { kv(s, k, String(v)); }
void kvS(String& s, const char* k, const String& v) { kv(s, k, jStr(v)); }
void handleStatus() {
  if (server.hasArg("dev")) devSeen(server.arg("dev"));   // the dashboard says which phone or computer it's open on (for JOIN)
  time_t t = time(nullptr);
  String s; s.reserve(2800);
  s += "{\"fw\":\"" FW_VERSION "\"";
  kvU(s, "epoch", (uint32_t)t); kvS(s, "local", isoLocal(t));
  kvS(s, "tz", cfg.tz); kvB(s, "setupDone", cfg.setupDone);
  if (findPhoneEp) kvU(s, "findPhone", findPhoneEp);      // a shortcut asked the open dashboards to sound
  kvB(s, "passSet", passSet()); kvB(s, "login", needLogin()); kv(s, "remembered", String(sessKept()));
  s += ",\"hw\":{\"pr\":" + jStr(AK_NAME[prK]) + ",\"th\":" + jStr(AK_NAME[thK]) + ",\"boxC\":" + jNum(boxThFresh() ? boxC : NAN, 2) + ",\"boxRH\":" + jNum(boxThFresh() ? boxRH : NAN, 1) +
       ",\"lx\":" + jStr(AK_NAME[lxK]) + ",\"lux\":" + jNum(lxK ? boxLux : NAN, 1) + ",\"imu\":" + jStr(AK_NAME[imK]) + ",\"rot\":" + String(imRotCur) + ",\"ind\":[" + String(hp.ind[0].length() ? indCur[0] : -2) + "," + String(hp.ind[1].length() ? indCur[1] : -2) + "],\"indCfg\":" + (cfg.ind.length() ? cfg.ind : String("[]")) + "}";
  kvB(s, "timeValid", timeValid); kvS(s, "timeSrc", timeSrc); kvB(s, "rtcOK", rtcOK);
  s += ",\"dev\":{\"clkMan\":" + jBool(cfg.clkMan); kvB(s, "apHomeOff", cfg.apHomeOff); kvB(s, "apForce", apForce); kvS(s, "mdns", cfg.mdns);
  kvU(s, "gpsDet", cfg.gpsDet); kvU(s, "tripAskMin", cfg.tripAskMin); kvU(s, "envMin", cfg.envMin); s += "}";
  kvS(s, "store", storeName); kvB(s, "storeOK", store != nullptr); kvB(s, "flashOK", flashOK);
  kvU(s, "flashKB", flashOK ? LittleFS.usedBytes() / 1024 : 0); kvU(s, "flashTotalKB", flashOK ? LittleFS.totalBytes() / 1024 : 0);
  kvB(s, "sdOK", sdOK); kvU(s, "cardMB", sdOK ? (uint32_t)(SD.cardSize() / (1024ULL * 1024ULL)) : 0);
  kvB(s, "eeOK", eeOK); kvS(s, "lastAlive", lastAlive > 1735689600UL ? isoLocal(lastAlive) : String(""));
  kvS(s, "resetReason", resetName(resetReason));
  kvU(s, "logSeq", logNextSeq - 1); kvU(s, "logBytes", logBytes); kvU(s, "logBad", logBadLines); kvU(s, "envLogS", envLogS()); kvU(s, "envFlashDays", ENV_FLASH_DAYS);
  kvS(s, "uiSrc", uiSource()); kvU(s, "docVer", docVer);
  bool up = WiFi.status() == WL_CONNECTED;
  s += ",\"sta\":{\"ssid\":" + jStr(cfg.homeSsid); kvB(s, "up", up); kvS(s, "ip", WiFi.localIP().toString()); kv(s, "rssi", String(up ? WiFi.RSSI() : 0)); s += "}";
  s += ",\"ap\":{\"up\":" + jBool(apUp); kvS(s, "ssid", cfg.apSsid); kvS(s, "ip", WiFi.softAPIP().toString()); kvU(s, "clients", WiFi.softAPgetStationNum()); s += "}";
  kvU(s, "channel", WiFi.channel()); kvS(s, "mdns", cfg.mdns + ".local");
  uint8_t m[6]; WiFi.macAddress(m); kvS(s, "hubMac", macStr(m));
  kvU(s, "uptime", millis() / 1000); kvU(s, "heap", ESP.getFreeHeap());
  s += ",\"batt\":{\"ok\":" + jBool(battType != 0); kv(s, "pct", jNum(battPct, 0)); kv(s, "v", jNum(battV, 2)); kv(s, "rate", jNum(battRate, 2)); kv(s, "left", jNum(battLeftH, 1)); kvB(s, "chg", battChg); kvB(s, "usbSense", USB_PIN >= 0); kvB(s, "measuring", !battChg && !battSpanOk); kv(s, "offV", jNum(BATT_OFF_V, 2)); s += "}";
  s += ",\"gps\":{\"mode\":\"" + String(gpsMode == 1 ? "on" : gpsMode == 2 ? "off" : "auto") + "\",\"asleep\":" + jBool(gpsAsleep) + ",\"seen\":" + jBool(gps.seen && millis() - gps.lastNmeaMs < 5000); kvB(s, "fix", gps.fix); kvU(s, "sats", gps.sats); kv(s, "kn", jNum(gps.kn, 1));
  if (gps.lastFixMs) { kv(s, "lat", String(gps.lat, 6)); kv(s, "lon", String(gps.lon, 6)); kvU(s, "fixAge", (millis() - gps.lastFixMs) / 1000); }
  kvU(s, "ptsToday", gps.ptsDay == trackDate(t) ? gps.ptsToday : 0); kvS(s, "store", sdOK ? "sd" : "flash"); s += "}";
  kv(s, "alarm", alarmsJson());
  { float d3 = baro3h(); uint32_t l = brLast();
    s += ",\"baro\":{\"ok\":" + jBool(bmpOK); kv(s, "hpa", jNum(baroHpa, 2)); kv(s, "d3", jNum(d3, 2)); kvS(s, "word", isnan(d3) ? String("") : baroWord(d3));
    kv(s, "boxC", jNum(baroC, 1)); kvU(s, "saveS", BARO_SAVE_S); if (l) kvU(s, "lastEp", l * BARO_SAVE_S); s += "}"; }
  if (lastFix.ep) { s += ",\"lastFix\":{\"lat\":" + String(lastFix.lat, 5); kv(s, "lon", String(lastFix.lon, 5)); kvU(s, "ep", lastFix.ep); s += "}"; }
  s += ",\"screen\":{\"ok\":" + jBool(oledOK); kvB(s, "on", screenOn); kvB(s, "enc", encOK); kvU(s, "encAddr", encAddr); kvU(s, "offMin", cfg.scrOffMin); kvB(s, "clHide", cfg.clHideDone); kvB(s, "btnLed", cfg.btnLed); kvU(s, "bright", cfg.contrast); s += "}";
  kvB(s, "gpsBus", gpsI2C);
  s += ",\"nodes\":[";
  bool first = true;
  for (int i = 0; i < MAX_NODES; i++) {
    const Node& n = nodes[i];
    if (!n.used) continue;
    if (!first) s += ",";
    first = false;
    s += "{\"name\":" + jStr(n.name); kvS(s, "mac", macStr(n.mac));
    kv(s, "tempC", jNum(n.tempC, 2)); kv(s, "rh", jNum(n.rh, 1)); kv(s, "vbat", jNum(n.vbat, 3)); kv(s, "soc", jNum(n.soc, 1));
    uint32_t ag = nodeAge(n);
    kv(s, "rssi", String(n.rssi)); kv(s, "age", ag == 0xFFFFFFFFUL ? String("null") : String(ag)); kvB(s, "live", n.live);
    kvU(s, "backfilled", n.bfRows); kvU(s, "interval", n.interval); kvU(s, "count", n.count); kvU(s, "seq", n.seq); kvB(s, "sensorOK", n.flags & 1);
    s += "}";
  }
  s += "]}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

void handleLogGet() {
  server.sendHeader("Cache-Control", "no-store");
  if (!store) { sendErr(503, flashOK && LittleFS.exists(LOG_ONCARD) ? "The log is on the SD card: put the card back in the box" : "No storage available"); return; }
  File f = store->open(LOG_PATH, FILE_READ);
  if (!f) { server.send(200, "application/x-ndjson", ""); return; }
  server.streamFile(f, "application/x-ndjson");
  f.close();
}

// Body: one JSON event per line. Each is stored as {"seq":N,"ts":"<local ISO>","e":<event>}
void handleLogPost() {
  if (!store)     { sendErr(503, flashOK && LittleFS.exists(LOG_ONCARD) ? "The log is on the SD card: put the card back in the box" : "No storage available"); return; }
  if (!timeValid) { sendErr(409, "Hub clock not set"); return; }
  String body = server.arg("plain");
  if (body.length() == 0 || body.length() > 32768) { sendErr(400, "Empty or oversized body"); return; }
  String ts; uint32_t first;
  int n = appendEvents(body, ts, first);
  if (n == -2) { sendErr(507, "The box's storage is full - nothing was saved. Insert or check the SD card, or make room in Settings."); return; }
  if (n < 0)  { sendErr(503, "Write failed"); return; }
  if (n == 0) { sendErr(400, "No valid events"); return; }
  server.send(200, "application/json",
              "{\"ok\":true,\"first\":" + String(first) + ",\"last\":" + String(logNextSeq - 1) + ",\"ts\":" + jStr(ts) + "}");
}

void listEnvDays(fs::FS& fs, String& out, bool& first, const String& skipIn, const char* dir = "/env") {
  File d = fs.open(dir);
  if (!d) return;
  File e = d.openNextFile();
  while (e) {
    String nm = e.name();
    int sl = nm.lastIndexOf('/');
    if (sl >= 0) nm = nm.substring(sl + 1);
    if (!e.isDirectory() && nm.endsWith(".csv") && nm.length() == 14) {
      String day = nm.substring(0, 10);
      if (skipIn.indexOf(day) < 0) {
        if (!first) out += ",";
        first = false;
        out += "\"" + day + "\"";
      }
    }
    e.close();
    e = d.openNextFile();
  }
  d.close();
}

void handleEnvDays() {
  String s = "[";
  bool first = true;
  if (sdOK) listEnvDays(SD, s, first, "");
  if (flashOK) listEnvDays(LittleFS, s, first, s);   // flash days not already on the card
  s += "]";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

bool validDate(const String& d) {
  if (d.length() != 10 || d[4] != '-' || d[7] != '-') return false;
  for (int i = 0; i < 10; i++) if (i != 4 && i != 7 && !isdigit((unsigned char)d[i])) return false;
  return true;
}

void handleBaroDays() {                                    // ["2026-11-20", ...]: days with a pressure track
  String s = "[";
  bool first = true;
  if (sdOK) listEnvDays(SD, s, first, "", "/baro");
  if (flashOK) listEnvDays(LittleFS, s, first, s, "/baro");
  s += "]";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}
void handleBaro() {                                        // one day's pressure track: utc,hpa,box_c,n
  String d = server.arg("date");
  if (!validDate(d)) { sendErr(400, "date=YYYY-MM-DD"); return; }
  String path = "/baro/" + d + ".csv";
  fs::FS* fs = (sdOK && SD.exists(path)) ? (fs::FS*)&SD : (flashOK && LittleFS.exists(path)) ? (fs::FS*)&LittleFS : nullptr;
  if (!fs) { sendErr(404, "No file for that date"); return; }
  File f = fs->open(path, FILE_READ);
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(f, "text/csv");
  f.close();
}
void handleEnv() {
  String d = server.arg("date");
  if (!validDate(d)) { sendErr(400, "date=YYYY-MM-DD"); return; }
  String path = "/env/" + d + ".csv";
  fs::FS* fs = nullptr;
  if (sdOK && SD.exists(path)) fs = &SD;
  else if (flashOK && LittleFS.exists(path)) fs = &LittleFS;
  if (!fs) { sendErr(404, "No file for that date"); return; }
  File f = fs->open(path, FILE_READ);
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(f, "text/csv");
  f.close();
}

void handleDownload() {
  String p = server.arg("f");
  if (!(p.startsWith("/hwo/") || p.startsWith("/env/") || p.startsWith("/gps/")) || p.indexOf("..") >= 0 || p.startsWith("/hwo/config") || p.startsWith("/hwo/sessions")) { sendErr(400, "Not allowed"); return; }
  fs::FS* fs = nullptr;
  if (p.startsWith("/hwo/") && store && store->exists(p)) fs = store;
  else if (sdOK && SD.exists(p)) fs = &SD;
  else if (flashOK && LittleFS.exists(p)) fs = &LittleFS;
  if (!fs) { sendErr(404, "Not found"); return; }
  File f = fs->open(p, FILE_READ);
  String fn = p.substring(p.lastIndexOf('/') + 1);
  if (p.startsWith("/gps/")) fn = "track_" + fn;
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + fn + "\"");
  server.streamFile(f, "application/octet-stream");
  f.close();
}

// ?epoch=<UTC seconds> sets it from the dashboard's device; ?local=YYYY-MM-DDTHH:MM[:SS] is a time typed in, in the
// box's own time zone. In Automatic, GPS and internet time win (force=1 sets it anyway; they correct it again later).
bool setClockTo(time_t e, const char* src) {
  if ((uint32_t)e < 1735689600UL) return false;          // before 2025: not a real time
  setSystemUtc(e); writeRtc(e);
  timeValid = true; timeSrc = src; lastEnvSlot = 0;
  return true;
}
time_t localToEpoch(int y, int mo, int d, int h, int mi, int se) {   // a wall-clock time in the box's time zone
  struct tm tm = {}; tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = h; tm.tm_min = mi; tm.tm_sec = se; tm.tm_isdst = -1;
  return mktime(&tm);
}
void handleTime() {
  String L = server.arg("local");
  bool typed = L.length() > 0, force = server.arg("force") == "1" || typed;
  time_t e;
  if (typed) {
    if (L.length() < 16 || L[4] != '-' || L[7] != '-' || L[10] != 'T' || L[13] != ':') { sendErr(400, "local=YYYY-MM-DDTHH:MM"); return; }
    int y = L.substring(0, 4).toInt(), mo = L.substring(5, 7).toInt(), d = L.substring(8, 10).toInt(), h = L.substring(11, 13).toInt(), mi = L.substring(14, 16).toInt();
    int se = L.length() >= 19 ? L.substring(17, 19).toInt() : 0;
    if (y < 2025 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || se > 59) { sendErr(400, "That date or time doesn't exist"); return; }
    e = localToEpoch(y, mo, d, h, mi, se);
  } else e = (time_t)(uint32_t)server.arg("epoch").toInt();
  if ((uint32_t)e < 1735689600UL) { sendErr(400, "epoch looks wrong"); return; }   // before 2025-01-01
  if (!cfg.clkMan && !force && timeValid && (!strcmp(timeSrc, "gps") || !strcmp(timeSrc, "ntp"))) {
    server.send(200, "application/json", "{\"ok\":false,\"kept\":" + jStr(timeSrc) + "}"); return;
  }
  setClockTo(e, typed ? "manual" : "laptop");
  server.send(200, "application/json", "{\"ok\":true,\"local\":" + jStr(isoLocal(e)) + ",\"auto\":" + jBool(!cfg.clkMan) + "}");
}


// ---------- attachments ----------
String safeName(const String& in, size_t maxLen) {
  String o;
  for (size_t i = 0; i < in.length() && o.length() < maxLen; i++) {
    char c = in[i];
    o += (isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_') ? c : '_';
  }
  return o.length() ? o : String("file");
}

const char* mimeFor(const String& p) {
  String l = p; l.toLowerCase();
  if (l.endsWith(".jpg") || l.endsWith(".jpeg")) return "image/jpeg";
  if (l.endsWith(".png"))  return "image/png";
  if (l.endsWith(".gif"))  return "image/gif";
  if (l.endsWith(".webp")) return "image/webp";
  if (l.endsWith(".pdf"))  return "application/pdf";
  if (l.endsWith(".txt") || l.endsWith(".csv")) return "text/plain";
  return "application/octet-stream";
}

void handleAttUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    attOK = false; attBytes = 0; attErr = "";
    if (!authed()) { attErr = "Log in"; return; }
    String id = safeName(server.arg("id"), 12);
    String fn = safeName(up.filename, 40);
    attPath = "/att/" + id + "_" + fn;
    if (!sdOK) { attErr = "Insert the microSD card to add attachments"; return; }
    if (!SD.exists("/att")) SD.mkdir("/att");
    attFile = SD.open(attPath, FILE_WRITE);
    attWhere = "sd";
    attOK = (bool)attFile;
    if (!attOK) attErr = "Could not create the file on the SD card";
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!attOK) return;
    if (attFile.write(up.buf, up.currentSize) != up.currentSize) { attFile.close(); SD.remove(attPath); attOK = false; attErr = "SD write failed (card full?)"; return; }
    attBytes += up.currentSize;
  } else if (up.status == UPLOAD_FILE_END) {
    if (attFile) attFile.close();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (attFile) attFile.close();
    if (attOK) SD.remove(attPath);
    attOK = false; attErr = "Upload aborted";
  }
}

// ---------- documents (daily checklist + archived days) ----------
bool validIcsName(const String& n) {
  if (!n.startsWith("ics/") || n.length() < 5 || n.length() > 28) return false;
  for (size_t i = 4; i < n.length(); i++) { char c = n[i]; if (!isalnum((unsigned char)c) && c != '-' && c != '_') return false; }
  return true;
}
bool validCxName(const String& n) {   // archive search index: cx/YYYY-MM or cx/YYYY-MM-<part>
  if (!n.startsWith("cx/") || n.length() < 10 || n.length() > 13) return false;
  for (size_t i = 3; i < n.length(); i++) if (!isdigit((unsigned char)n[i]) && n[i] != '-') return false;
  return true;
}
bool validNoteName(const String& n) {   // notes nt/0 …, archived notes na/1 …, note history nh/1 … (up to 4 digits)
  if (!(n.startsWith("nt/") || n.startsWith("na/") || n.startsWith("nh/")) || n.length() < 4 || n.length() > 7) return false;
  for (size_t i = 3; i < n.length(); i++) if (!isdigit((unsigned char)n[i])) return false;
  return true;
}
bool validPlanName(const String& n) {     // archived Planner plans: pl/<1-24 of A-Z a-z 0-9 ->
  if (!n.startsWith("pl/") || n.length() < 4 || n.length() > 27) return false;
  for (size_t i = 3; i < n.length(); i++) if (!isalnum((unsigned char)n[i]) && n[i] != '-') return false;
  return true;
}
bool validDocName(const String& n) {
  if (n == "checklist" || n == "oled" || n == "plan") return true;
  if (validPlanName(n)) return true;
  if (validIcsName(n) || validCxName(n) || validNoteName(n)) return true;
  if (!n.startsWith("cl/") || n.length() < 13 || n.length() > 17) return false;
  for (size_t i = 3; i < n.length(); i++) if (!isdigit((unsigned char)n[i]) && n[i] != '-') return false;
  return true;
}
String docPath(const String& n) { return n == "checklist" || n == "oled" || n == "plan" ? "/hwo/doc/" + n + ".json" : "/hwo/" + n + ".json"; }  // cl/<day>, cx/<month>, ics/<id>, nt/<k>, na/<k>, pl/<id>
bool flashFirst(const String& n) { return n == "checklist" || n == "oled" || n == "plan" || n.startsWith("nt/"); }   // kept in flash, copied to the card

void handleDocGet() {
  String n = server.arg("n");
  if (!validDocName(n)) { sendErr(400, "Bad document name"); return; }
  String p = docPath(n);
  fs::FS* fs = nullptr;
  if (flashFirst(n)) { if (flashOK && LittleFS.exists(p)) fs = &LittleFS; else if (sdOK && SD.exists(p)) fs = &SD; }
  else { if (sdOK && SD.exists(p)) fs = &SD; else if (flashOK && LittleFS.exists(p)) fs = &LittleFS; }
  if (!fs) { sendErr(404, "No such document"); return; }
  File f = fs->open(p, FILE_READ);
  server.sendHeader("Cache-Control", "no-store");
  server.streamFile(f, "application/json");
  f.close();
}

const size_t DOC_MAX = 98304;   // 96 kB a document: room for a checklist of about 500-800 items (big bodies go to PSRAM)
bool writeDoc(fs::FS& fs, const String& p, const String& body) {
  String tmp = p + ".tmp";
  File f = fs.open(tmp, FILE_WRITE);
  if (!f) return false;
  size_t w = f.print(body);
  f.close();
  if (w != body.length()) { fs.remove(tmp); return false; }
  fs.remove(p);
  return fs.rename(tmp, p);
}

void handleDocPut() {
  String n = server.arg("n");
  if (!validDocName(n)) { sendErr(400, "Bad document name"); return; }
  String body = server.arg("plain");
  if (body.length() < 2 || body.length() > DOC_MAX || body[0] != '{') { sendErr(400, "Body must be a JSON object under 96 kB"); return; }
  if (validIcsName(n)) { sendErr(400, "Calendars go to /api/doc-up"); return; }
  String p = docPath(n);
  bool ok = false;
  if (n == "checklist" || n == "oled" || n == "plan") {      // plan: the Planner's current plan
    if (flashOK) { ensureDirs(LittleFS); ok = writeDoc(LittleFS, p, body); if (ok && sdOK) { ensureDirs(SD); writeDoc(SD, p, body); } }
    else if (sdOK) { ensureDirs(SD); ok = writeDoc(SD, p, body); }
  } else if (n.startsWith("nt/")) {      // notes: flash plus a copy on the card; card only once flash is nearly full
    size_t freeB = flashOK ? LittleFS.totalBytes() - LittleFS.usedBytes() + fileSize(LittleFS, p.c_str()) : 0;
    if (flashOK && freeB >= 128 * 1024) { ensureDirs(LittleFS); ok = writeDoc(LittleFS, p, body); if (ok && sdOK) { ensureDirs(SD); writeDoc(SD, p, body); } }
    else if (sdOK) { ensureDirs(SD); ok = writeDoc(SD, p, body); if (ok && flashOK) LittleFS.remove(p); }
    else if (flashOK) { sendErr(507, "Flash is nearly full - insert the SD card to keep saving notes"); return; }
  } else if (sdOK) {
    ensureDirs(SD); ok = writeDoc(SD, p, body);
  } else if (flashOK) {
    if (LittleFS.totalBytes() - LittleFS.usedBytes() < 150 * 1024) { sendErr(507, "Flash is nearly full - insert the SD card to keep archiving"); return; }
    ensureDirs(LittleFS); ok = writeDoc(LittleFS, p, body);
  }
  if (!ok) { sendErr(503, "Could not save the document"); return; }
  docVer++; redraw = true;                                 // the screen shows the change straight away
  server.send(200, "application/json", "{\"ok\":true,\"ver\":" + String(docVer) + "}");
}

void listDocs(fs::FS& fs, String& out, bool& first, const char* dir) {
  File d = fs.open(String("/hwo/") + dir);
  if (!d) return;
  File e = d.openNextFile();
  while (e) {
    String nm = e.name(); int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1);
    e.close();
    if (nm.endsWith(".json")) {
      String id = String(dir) + "/" + nm.substring(0, nm.length() - 5);
      if (out.indexOf("\"" + id + "\"") < 0) { if (!first) out += ","; first = false; out += "\"" + id + "\""; }
    }
    e = d.openNextFile();
  }
  d.close();
}

void handleDocList() {
  String s = "[";
  bool first = true;
  String d = server.arg("d");
  const char* dir = d == "ics" ? "ics" : d == "cx" ? "cx" : d == "nt" ? "nt" : d == "na" ? "na" : d == "nh" ? "nh" : d == "pl" ? "pl" : "cl";
  if (sdOK) listDocs(SD, s, first, dir);
  if (flashOK) listDocs(LittleFS, s, first, dir);
  s += "]";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

// Imported calendars can be hundreds of kB: streamed to the card (or flash if there is room)
File upFile; String upPath, upErr; fs::FS* upFs = nullptr; bool upOK = false; size_t upBytes = 0;
void handleDocUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    upOK = false; upBytes = 0; upErr = ""; upFs = nullptr;
    if (!authed()) { upErr = "Log in"; return; }
    String n = server.arg("n");
    if (!validIcsName(n)) { upErr = "Bad calendar name"; return; }
    upFs = sdOK ? (fs::FS*)&SD : (flashOK ? (fs::FS*)&LittleFS : nullptr);
    if (!upFs) { upErr = "No storage"; return; }
    ensureDirs(*upFs);
    upPath = docPath(n);
    upFile = upFs->open(upPath + ".tmp", FILE_WRITE);
    upOK = (bool)upFile;
    if (!upOK) upErr = "Could not create the file";
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!upOK) return;
    if (upFs == &LittleFS && LittleFS.totalBytes() - LittleFS.usedBytes() < 250 * 1024) { upFile.close(); LittleFS.remove(upPath + ".tmp"); upOK = false; upErr = "Not enough room in flash - insert the SD card"; return; }
    if (upFile.write(up.buf, up.currentSize) != up.currentSize) { upFile.close(); upFs->remove(upPath + ".tmp"); upOK = false; upErr = "Write failed (card full?)"; return; }
    upBytes += up.currentSize;
  } else if (up.status == UPLOAD_FILE_END) {
    if (upFile) upFile.close();
    if (upOK) { upFs->remove(upPath); upOK = upFs->rename(upPath + ".tmp", upPath); if (!upOK) upErr = "Rename failed"; }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (upFile) upFile.close();
    if (upFs) upFs->remove(upPath + ".tmp");
    upOK = false; upErr = "Upload aborted";
  }
}

void handleDocDel() {
  String n = server.arg("n");
  if (!validIcsName(n) && !validCxName(n) && !validNoteName(n) && !validPlanName(n) && n != "plan" && !(n.startsWith("cl/") && validDocName(n))) { sendErr(400, "Only imported calendars, archived checklist days, their index, note files and Planner plans can be removed"); return; }
  String p = docPath(n);
  bool a = sdOK && SD.exists(p) && SD.remove(p);
  bool b = flashOK && LittleFS.exists(p) && LittleFS.remove(p);
  docVer++;
  server.send(200, "application/json", String("{\"ok\":true,\"removed\":") + jBool(a || b) + "}");
}

// ---------- GPS track files ----------
// The box's own track is /gps/YYYY-MM-DD.csv; Google Timeline points brought in from the dashboard go beside
// it in /gps/x/YYYY-MM-DD.csv, so they can be taken off again without touching the box's own.
void listGpsDir(fs::FS& fs, const char* dir, std::vector<GDay>& L, bool isX) {
  File d = fs.open(dir);
  if (!d || !d.isDirectory()) { if (d) d.close(); return; }
  for (File e = d.openNextFile(); e; e = d.openNextFile()) {
    String nm = e.name(); int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1);
    uint32_t sz = e.size(); bool dir2 = e.isDirectory(); e.close();
    if (dir2 || !nm.endsWith(".csv") || nm.length() != 14) continue;
    String day = nm.substring(0, 10); GDay* g = nullptr;
    for (auto& x : L) if (x.d == day) { g = &x; break; }
    if (!g) { L.push_back(GDay()); g = &L.back(); g->d = day; }
    uint32_t& v = isX ? g->x : g->own; if (!v) v = sz;      // the card's copy first
  }
  d.close();
}
void handleGpsDays() {                                    // [["2026-11-20", box's own bytes, Google Timeline bytes], ...]
  std::vector<GDay> L;
  if (sdOK) { listGpsDir(SD, "/gps", L, false); listGpsDir(SD, "/gps/x", L, true); }
  if (flashOK) { listGpsDir(LittleFS, "/gps", L, false); listGpsDir(LittleFS, "/gps/x", L, true); }
  for (size_t i = 1; i < L.size(); i++) for (size_t k = i; k > 0 && L[k].d < L[k - 1].d; k--) std::swap(L[k], L[k - 1]);
  String s = "[";
  for (size_t i = 0; i < L.size(); i++) s += String(i ? "," : "") + "[\"" + L[i].d + "\"," + String(L[i].own) + "," + String(L[i].x) + "]";
  s += "]";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}
fs::FS* gpsFileFS(const String& path) { return (sdOK && SD.exists(path)) ? (fs::FS*)&SD : (flashOK && LittleFS.exists(path)) ? (fs::FS*)&LittleFS : nullptr; }
void handleGps() {                                        // the day's CSV: the box's own points, then any brought in from Google
  String d = server.arg("date");
  if (!validDate(d)) { sendErr(400, "date=YYYY-MM-DD"); return; }
  String p1 = "/gps/" + d + ".csv", p2 = "/gps/x/" + d + ".csv";
  fs::FS* f1 = gpsFileFS(p1); fs::FS* f2 = gpsFileFS(p2);
  if (!f1 && !f2) { sendErr(404, "No track for that date"); return; }
  static const char HDR[] = "utc,lat,lon,alt_m,kn,sats,hdop,seg\n";
  File a = f1 ? f1->open(p1, FILE_READ) : File(), b = f2 ? f2->open(p2, FILE_READ) : File();
  size_t total = (a ? a.size() : strlen(HDR)) + (b ? b.size() : 0);
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(total);
  server.send(200, "text/csv", "");
  if (!a) server.sendContent(HDR);
  uint8_t buf[1024];
  for (File* f : { &a, &b }) { if (!*f) continue; int n; while ((n = f->read(buf, sizeof buf)) > 0) server.sendContent((const char*)buf, n); f->close(); }
}
void handleGpsAdd() {                                     // Google Timeline rows for one day, in pieces: utc,lat,lon,alt_m,kn,sats,hdop,seg
  String d = server.arg("date");
  if (!validDate(d)) { sendErr(400, "date=YYYY-MM-DD"); return; }
  String body = server.arg("plain");
  if (!body.length() || body.length() > 49152) { sendErr(400, "Empty or oversized body (send fewer rows at a time)"); return; }
  fs::FS* fs = gpsFS(); if (!fs) { sendErr(503, "No storage available"); return; }
  if (fs == &LittleFS && LittleFS.totalBytes() - LittleFS.usedBytes() < body.length() + 300 * 1024) { sendErr(507, "Flash is nearly full - insert the SD card to bring in a timeline"); return; }
  if (!fs->exists("/gps")) fs->mkdir("/gps");
  if (!fs->exists("/gps/x")) fs->mkdir("/gps/x");
  File f = fs->open("/gps/x/" + d + ".csv", FILE_APPEND);
  if (!f) { sendErr(503, "Could not write"); return; }
  int rows = 0, a = 0;
  while (a < (int)body.length()) {
    int b = body.indexOf('\n', a); if (b < 0) b = body.length();
    String l = body.substring(a, b); a = b + 1; l.trim();
    int c1 = l.indexOf(','), c2 = c1 < 0 ? -1 : l.indexOf(',', c1 + 1);
    if (!l.startsWith(d.substring(0, 2)) || l.length() > 120 || c1 < 19 || c2 < 0) continue;   // 2026-11-20T08:00:00Z,lat,lon,...
    if (f.println(l) != l.length() + 2) { f.close(); sendErr(507, "Storage full"); return; }
    rows++;
  }
  f.close();
  if (!rows) { sendErr(400, "No valid rows"); return; }
  server.send(200, "application/json", "{\"ok\":true,\"rows\":" + String(rows) + "}");
}
void handleGpsDel() {                                     // takes the Google Timeline points off a day; the box's own track stays
  String d = server.arg("date");
  if (!validDate(d)) { sendErr(400, "date=YYYY-MM-DD"); return; }
  String p = "/gps/x/" + d + ".csv"; bool any = false;
  if (sdOK && SD.exists(p)) any |= SD.remove(p);
  if (flashOK && LittleFS.exists(p)) any |= LittleFS.remove(p);
  server.send(200, "application/json", String("{\"ok\":true,\"removed\":") + jBool(any) + "}");
}

// ---------- a shared Google Maps list link: fetching it needs HTTPS, and the TLS code doesn't fit in the
// firmware's 1.25 MB (the app half of the "Default 4MB with spiffs" partitions, kept so .lbk updates work).
// Google Takeout's Saved lists (.csv) and Saved Places.json import without it. ----------
void handleGmapsList() { sendErr(501, "This box can't fetch Google links (no room for HTTPS in its firmware). Use Google Takeout > Saved instead"); }

// ---------- restore a backup: replaces the work-order log (the old one is kept beside it) ----------
File rsFile; bool rsAuth = false, rsOK = false; size_t rsBytes = 0; String rsErr;
void handleRestoreUpload() {
  HTTPUpload& up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    rsAuth = pwCheck(server.arg("pw")); rsOK = false; rsBytes = 0; rsErr = "";
    if (!rsAuth) { rsErr = "Wrong password"; return; }
    if (!store) { rsErr = "No storage"; return; }
    rsFile = store->open("/hwo/log.new", FILE_WRITE);
    rsOK = (bool)rsFile;
    if (!rsOK) rsErr = "Could not write";
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!rsOK) return;
    if (rsFile.write(up.buf, up.currentSize) != up.currentSize) { rsFile.close(); store->remove("/hwo/log.new"); rsOK = false; rsErr = "Write failed (full?)"; return; }
    rsBytes += up.currentSize;
  } else if (up.status == UPLOAD_FILE_END) {
    if (rsFile) rsFile.close();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (rsFile) rsFile.close();
    if (store) store->remove("/hwo/log.new");
    rsOK = false; rsErr = "Upload aborted";
  }
}
void handleRestoreDone() {
  if (!rsOK) { sendErr(rsAuth ? 400 : 403, rsErr.length() ? rsErr.c_str() : "Restore failed"); return; }
  File f = store->open("/hwo/log.new", FILE_READ);
  int c = f ? f.peek() : -1;
  if (f) f.close();
  if (rsBytes < 10 || c != '{') { store->remove("/hwo/log.new"); sendErr(400, "That is not a log file"); return; }
  uint32_t kept = 0, bad = 0;
  bool nz = normalizeLog(*store, "/hwo/log.new", "/hwo/log.chk", true, kept, bad);
  store->remove("/hwo/log.new");
  if (!nz || !kept) { store->remove("/hwo/log.chk"); sendErr(400, nz ? "No readable lines in that log" : "Write failed (full?)"); return; }
  store->remove("/hwo/log.before-restore.jsonl");
  if (store->exists(LOG_PATH)) store->rename(LOG_PATH, "/hwo/log.before-restore.jsonl");
  if (!store->rename("/hwo/log.chk", LOG_PATH)) { sendErr(503, "Rename failed - old log kept as log.before-restore.jsonl"); return; }
  if (store == &SD && flashOK) {                          // flash takes a copy of the restored log if it fits
    LittleFS.remove(LOG_PATH);
    size_t n = fileSize(SD, LOG_PATH);
    if (flashFree() >= n + LOG_FLASH_KEEP && copyFile(SD, LittleFS, LOG_PATH)) { LittleFS.remove(LOG_ONCARD); flashCopy = true; } else dropFlashCopy();
  } else if (store == &LittleFS) flashCopy = false;
  scanLog();
  server.send(200, "application/json", "{\"ok\":true,\"events\":" + String(kept) + ",\"damaged\":" + String(bad) + "}");
}

// ---------- settings ----------
void handleConfigGet() {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", "{\"apSsid\":" + jStr(cfg.apSsid) + ",\"homeSsid\":" + jStr(cfg.homeSsid) + ",\"homeSet\":" + jBool(cfg.homePass.length() > 0) +
              ",\"tz\":" + jStr(cfg.tz) + ",\"setupDone\":" + jBool(cfg.setupDone) + ",\"passSet\":" + jBool(passSet()) + ",\"apDefault\":" + jBool(cfg.apPass == HUB_AP_PASS) +
              ",\"apHomeOff\":" + jBool(cfg.apHomeOff) + ",\"mdns\":" + jStr(cfg.mdns) + "}");
}
// Body: JSON with any of apSsid, apPass, homeSsid, homePass, tz, setupDone, newPass. Passwords left out stay as
// they are. Until a box password exists none is needed; after that ?pw= must be the box password.
void handleConfigPost() {
  if (!flashOK) { sendErr(503, "Flash not mounted"); return; }
  if (!pwCheck(server.arg("pw"))) { sendErr(pwLocked() ? 429 : 403, pwLocked() ? "Too many tries - wait 30 s" : "Wrong box password"); return; }
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) { sendErr(400, "Bad request"); return; }
  Cfg n = cfg;
  if (d["apSsid"].is<const char*>()) n.apSsid = (const char*)d["apSsid"];
  if (d["apPass"].is<const char*>() && strlen(d["apPass"])) n.apPass = (const char*)d["apPass"];
  if (d["homeSsid"].is<const char*>()) n.homeSsid = (const char*)d["homeSsid"];
  if (d["homePass"].is<const char*>()) n.homePass = (const char*)d["homePass"];
  if (d["tz"].is<const char*>() && strlen(d["tz"])) n.tz = (const char*)d["tz"];
  if (d["setupDone"].is<bool>()) n.setupDone = d["setupDone"];
  if (d["apHomeOff"].is<bool>()) n.apHomeOff = d["apHomeOff"];
  if (d["mdns"].is<const char*>()) { n.mdns = (const char*)d["mdns"]; n.mdns.trim(); n.mdns.toLowerCase(); if (n.mdns.endsWith(".local")) n.mdns.remove(n.mdns.length() - 6); }
  String np = d["newPass"] | "";
  if (np.length() && (np.length() < 8 || np.length() > 64)) { sendErr(400, "Box password: 8 to 64 characters"); return; }
  if (n.setupDone && !passSet() && !np.length()) { sendErr(400, "Choose a box password"); return; }
  if (n.setupDone && !cfg.setupDone && n.apPass == HUB_AP_PASS) { sendErr(400, "Choose your own hotspot password (the starting one is public)"); return; }
  n.apSsid.trim(); n.homeSsid.trim();
  if (!n.apSsid.length() || n.apSsid.length() > 32) { sendErr(400, "Hotspot name: 1 to 32 characters"); return; }
  if (n.apPass.length() < 8 || n.apPass.length() > 63) { sendErr(400, "Hotspot password: 8 to 63 characters"); return; }
  if (n.homeSsid.length() > 32 || n.homePass.length() > 63) { sendErr(400, "Home Wi-Fi name or password too long"); return; }
  if (!mdnsOk(n.mdns)) { sendErr(400, "Network name: 1 to 32 of a-z, 0-9 and -, not starting or ending with -"); return; }
  bool mdnsChg = n.mdns != cfg.mdns;
  bool ap = n.apSsid != cfg.apSsid || n.apPass != cfg.apPass || n.apHomeOff != cfg.apHomeOff, sta = n.homeSsid != cfg.homeSsid || n.homePass != cfg.homePass;
  Cfg old = cfg; cfg = n;
  if (np.length()) passSetTo(np);
  if (!cfgSave()) { cfg = old; sendErr(503, "Could not save"); return; }
  if (np.length()) { sess.clear(); sessSave(); if (!viaHotspot()) setCookie(sessNew(false), false); }   // a new password signs every device out
  cfgApplyAp = ap; cfgApplySta = sta; cfgApplyMdns = mdnsChg; cfgApplyAt = millis() + 1500;   // after this reply has gone out
  server.send(200, "application/json", String("{\"ok\":true,\"apChanged\":") + jBool(ap) + ",\"homeChanged\":" + jBool(sta) + "}");
}
void handleScan() {
  if (scanning) { WiFi.scanDelete(); scanning = false; }
  int n = WiFi.scanNetworks(false, false, false, 120);
  String s = "["; int k = 0;
  for (int i = 0; i < n && k < 30; i++) {
    String id = WiFi.SSID(i); if (!id.length()) continue;
    bool dup = false; for (int j = 0; j < i; j++) if (WiFi.SSID(j) == id) { dup = true; break; }
    if (dup) continue;
    if (k++) s += ",";
    s += "{\"ssid\":" + jStr(id) + ",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
  }
  WiFi.scanDelete(); lastScan = millis();
  server.send(200, "application/json", s + "]");
}


// ---------- login / logout / box password ----------
void handleLogin() {
  JsonDocument d; deserializeJson(d, server.arg("plain"));
  if (!needLogin()) { server.send(200, "application/json", "{\"ok\":true}"); return; }
  if (pwLocked()) { sendErr(429, "Too many tries - wait 30 s"); return; }
  if (!pwCheck(d["pw"] | "")) { sendErr(pwLocked() ? 429 : 403, pwLocked() ? "Too many tries - wait 30 s" : "Wrong password"); return; }
  bool keep = d["keep"] | false;
  setCookie(sessNew(keep), keep);
  server.send(200, "application/json", "{\"ok\":true}");
}
void handleLogout() {
  int i = sessFind();
  if (i >= 0) { bool k = sess[i].keep; sess.erase(sess.begin() + i); if (k) sessSave(); }
  server.sendHeader("Set-Cookie", "lb_s=; Path=/; Max-Age=0; SameSite=Strict");
  server.send(200, "application/json", "{\"ok\":true}");
}
void handlePassword() {   // {old, new}: change it; signs every device out (this one gets a fresh login)
  JsonDocument d; if (deserializeJson(d, server.arg("plain"))) { sendErr(400, "Bad request"); return; }
  String np = d["new"] | "";
  if (np.length() < 8 || np.length() > 64) { sendErr(400, "Box password: 8 to 64 characters"); return; }
  if (!pwCheck(d["old"] | "")) { sendErr(pwLocked() ? 429 : 403, pwLocked() ? "Too many tries - wait 30 s" : "The current password is wrong"); return; }
  String os = cfg.passSalt, oh = cfg.passHash;
  passSetTo(np);
  if (!cfgSave()) { cfg.passSalt = os; cfg.passHash = oh; sendErr(503, "Could not save"); return; }
  sess.clear(); sessSave();
  if (!viaHotspot()) setCookie(sessNew(false), false);
  server.send(200, "application/json", "{\"ok\":true}");
}
void handleForget() {     // forget every remembered device (this one stays logged in until the box restarts)
  bool here = !viaHotspot() && sessFind() >= 0;
  sess.clear(); sessSave();
  if (here) setCookie(sessNew(false), false);
  server.send(200, "application/json", "{\"ok\":true}");
}

bool serveAttachment(const String& uri) {
  if (!uri.startsWith("/att/") || uri.indexOf("..") >= 0) return false;
  if (!sdOK) { sendErr(503, "SD card not in — attachments live on the card"); return true; }
  if (!SD.exists(uri)) { sendErr(404, "Attachment not found on this SD card"); return true; }
  File f = SD.open(uri, FILE_READ);
  server.sendHeader("Cache-Control", "max-age=31536000, immutable");
  server.streamFile(f, mimeFor(uri));
  f.close();
  return true;
}

// Settings > Pin mapping ({"pins":{back,sel,backLed,selLed,buzA,buzB,xb:[[pin,action]x4]}}) and Add-ons
// ({"add":{off,bh,pcf,imuM,imuF,imuW,imuR}}): checked, saved, and the box restarts to use them
void handleHwPost() {
  JsonDocument d; if (deserializeJson(d, server.arg("plain"))) { sendErr(400, "Bad JSON"); return; }
  if (d["pins"].is<JsonObject>()) {
    JsonObject P = d["pins"];
    HwPins n = hp;
    const char* K[] = { "back", "sel", "backLed", "selLed", "buzA", "buzB" }; String* R[] = { &n.back, &n.sel, &n.backLed, &n.selLed, &n.buzA, &n.buzB };
    for (int i = 0; i < 6; i++) if (P[K[i]].is<const char*>()) *R[i] = (const char*)P[K[i]];
    if (P["xb"].is<JsonArray>()) { int i = 0; for (JsonVariant v : P["xb"].as<JsonArray>()) { if (i >= 4) break; n.xbP[i] = v[0] | ""; n.xbA[i] = v[1] | ""; i++; } }
    if (P["ind"].is<JsonArray>()) { int i = 0; for (JsonVariant v : P["ind"].as<JsonArray>()) { if (i >= 2) break; n.ind[i++] = v | ""; } }
    std::vector<String> all = { n.back, n.sel, n.backLed, n.selLed, n.buzA, n.buzB };
    for (int i = 0; i < 4; i++) all.push_back(n.xbP[i]);
    all.push_back(n.ind[0]); all.push_back(n.ind[1]);
    for (size_t i = 0; i < all.size(); i++) {
      if (!pinValid(all[i], i == 4 || i == 5)) { sendErr(400, (String(i == 4 || i == 5 ? "The buzzer needs Feather pins: " : "Not a pin this box has free: ") + all[i]).c_str()); return; }
      for (size_t j = 0; j < i; j++) if (all[i].length() && all[i] == all[j]) { sendErr(400, ("Pin " + all[i] + " is used twice").c_str()); return; }
    }
    if (!n.back.length() || !n.sel.length()) { sendErr(400, "BACK and SELECT need a pin each"); return; }
    for (int i = 0; i < 4; i++) {
      bool ok = !n.xbA[i].length(); for (const char* a : XB_ACT) if (n.xbA[i] == a) ok = true;
      if (!ok) { sendErr(400, ("Unknown action " + n.xbA[i]).c_str()); return; }
    }
    HwPins keep = hp; hp = n; String js = hwPinsJson(); hp = keep;
    cfg.hwPins = js;
  }
  if (d["add"].is<JsonObject>()) {
    JsonObject A = d["add"];
    String off = A["off"] | addOff; int bh = A["bh"] | (int)bhAddr, pcf = A["pcf"] | (int)pcfAddr;
    if (bh && bh != 0x23 && bh != 0x5C) { sendErr(400, "The BH1750 is at 0x23 or 0x5C"); return; }
    if (pcf && (pcf < 0x20 || pcf > 0x27)) { sendErr(400, "A PCF8574 is at 0x20 to 0x27"); return; }
    if (bh && bh == pcf) { sendErr(400, "The light sensor and the PCF8574 can't share an address"); return; }
    String js = "{\"off\":" + jStr(off) + ",\"bh\":" + String(bh) + ",\"pcf\":" + String(pcf) + ",\"imuM\":" + String(constrain((int)(A["imuM"] | (int)imuMount), 0, 3)) +
                ",\"imuF\":" + jBool(A["imuF"] | imuFlip) + ",\"imuW\":" + jBool(A["imuW"] | imuWake) + ",\"imuR\":" + jBool(A["imuR"] | imuRot) + "}";
    cfg.hwAdd = js;
  }
  if (flashOK && !cfgSave()) { sendErr(503, "Could not save"); return; }
  server.send(200, "application/json", "{\"ok\":true,\"restart\":true}");
  delay(400); ESP.restart();
}

void setupWeb() {
  // no CORS: the dashboard comes from the box itself, so other web pages open on a phone can't read the box
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, G(handleStatus));
  server.on("/api/log", HTTP_GET, G(handleLogGet));
  server.on("/api/log", HTTP_POST, G(handleLogPost));
  server.on("/api/envdays", HTTP_GET, G(handleEnvDays));
  server.on("/api/env", HTTP_GET, G(handleEnv));
  server.on("/api/barodays", HTTP_GET, G(handleBaroDays));
  server.on("/api/baro", HTTP_GET, G(handleBaro));
  server.on("/api/time", HTTP_POST, G(handleTime));
  server.on("/dl", HTTP_GET, G(handleDownload));
  server.on("/update", HTTP_GET, []() { sendGz(UPDATE_HTML_GZ, sizeof UPDATE_HTML_GZ); });
  server.on("/update", HTTP_POST,
    []() {
      bool ok = otaAuthorized && (upKind == 1 || upKind == 4) && !Update.hasError() && Update.isFinished();   // a cut-off file never counts
      if (otaAuthorized && !ok && !otaErr.length() && upKind != 9) otaErr = "Upload incomplete - nothing changed";
      if (ok && upKind == 4) {                       // bundle: the new dashboard goes live with the new firmware
        LittleFS.remove("/www/index.html.gz"); LittleFS.remove("/www/index.html");
        ok = LittleFS.rename("/www/index.tmp", "/www/index.html.gz");
        if (!ok) otaErr = "Firmware written but the dashboard could not be saved";
      } else LittleFS.remove("/www/index.tmp");
      server.sendHeader("Connection", "close");
      String msg = ok ? "Update OK - restarting" : (!otaAuthorized ? "Wrong password" : otaErr.length() ? otaErr : String("Update failed: ") + Update.errorString());
      if (otaAuthorized) { if (ok) { otaSt = 2; otaDraw(true); } else { if (!otaErr.length()) otaErr = msg; otaFailed(); } }   // nothing on the box for a wrong password
      server.send(ok ? 200 : (otaAuthorized ? 500 : 403), "text/plain", msg);
      if (ok) { delay(500); ESP.restart(); }
    },
    []() {
      HTTPUpload& up = server.upload();
      if (up.status == UPLOAD_FILE_START) {
        otaAuthorized = pwCheck(server.arg("pw"));
        upKind = 0; upHead = ""; otaErr = ""; upUiLeft = 0; upBinSize = 0;
        if (otaAuthorized) { otaSt = 1; otaDone = 0; otaTot = max(0, server.clientContentLength()); otaNew = ""; qmOn = csOn = false; otaDraw(true); }
      } else if (up.status == UPLOAD_FILE_WRITE) {
        if (!otaAuthorized || upKind == 9) return;
        otaDone += up.currentSize; otaDraw(false);
        const uint8_t* d = up.buf; size_t n = up.currentSize;
        while (n) {
          if (upKind == 0) {
            if (d[0] == 0xE9) { upKind = 1; Update.begin(UPDATE_SIZE_UNKNOWN); }
            else if (d[0] == 'L') upKind = 2;
            else { upKind = 9; otaErr = "Not a Life Logger update file (.lbk) or firmware (.bin)"; return; }
          }
          if (upKind == 2) {
            while (n && upHead.length() < 300) { char c = *d++; n--; if (c == '\n') { upKind = 3; break; } upHead += c; }
            if (upKind == 2) { if (upHead.length() >= 300) { upKind = 9; otaErr = "Bad update file header"; } return; }
            if (!upHead.startsWith("LOGBOOK1")) { upKind = 9; otaErr = "Bad update file header"; return; }
            int u = upHead.indexOf("\"ui\":"), bi = upHead.indexOf("\"bin\":");
            if (u < 0 || bi < 0 || !flashOK) { upKind = 9; otaErr = flashOK ? "Bad update file header" : "Flash not mounted"; return; }
            upUiLeft = upHead.substring(u + 5).toInt(); upBinSize = upHead.substring(bi + 6).toInt();
            { int v = upHead.indexOf("\"fw\":\""); if (v >= 0) { int e = upHead.indexOf('"', v + 6); if (e > v + 6 && e - v < 20) otaNew = upHead.substring(v + 6, e); } }   // the new version, for old --> new
            if (!LittleFS.exists("/www")) LittleFS.mkdir("/www");
            LittleFS.remove("/www/index.tmp");
            if (flashFree() < upUiLeft + 64 * 1024) { LittleFS.remove("/www/index.html.gz"); LittleFS.remove("/www/index.html"); }   // short of room: the old dashboard goes first (the box shows its update page until the new one is in)
            upUi = LittleFS.open("/www/index.tmp", FILE_WRITE);
            if (!upUi || upUiLeft < 1000 || upBinSize < 100000) { upKind = 9; otaErr = "Bad update file"; return; }
            continue;
          }
          if (upKind == 3) {
            size_t k = n < upUiLeft ? n : upUiLeft;
            if (upUi.write(d, k) != k) { upUi.close(); upKind = 9; otaErr = "Flash full - dashboard not saved"; return; }
            d += k; n -= k; upUiLeft -= k;
            if (!upUiLeft) { upUi.close(); upKind = 4; if (!Update.begin(upBinSize)) { upKind = 9; otaErr = String("Firmware: ") + Update.errorString(); return; } }
            continue;
          }
          if (upKind == 1 || upKind == 4) { Update.write((uint8_t*)d, n); n = 0; }
        }
      } else if (up.status == UPLOAD_FILE_END) {
        if (otaAuthorized && (upKind == 1 || upKind == 4)) Update.end(true);
        if (upUi) upUi.close();
      } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.abort(); if (upUi) upUi.close(); LittleFS.remove("/www/index.tmp");
        if (otaAuthorized) { otaErr = "The upload was cut off"; otaFailed(); }
      }
    });
  // dashboard .html upload -> flash
  server.on("/update-ui", HTTP_POST,
    []() {
      if (!flashOK)      { server.send(503, "text/plain", "Flash storage not mounted"); return; }
      if (!uiAuthorized) { server.send(uiBytes ? 507 : 403, "text/plain", uiBytes ? "Flash full - dashboard not changed" : "Wrong password"); return; }
      if (uiBytes < 2000) { LittleFS.remove("/www/index.tmp"); server.send(400, "text/plain", "File too small to be the dashboard"); return; }
      LittleFS.remove("/www/index.html"); LittleFS.remove("/www/index.html.gz");
      LittleFS.rename("/www/index.tmp", "/www/index.html");
      server.send(200, "text/plain", "Dashboard updated (" + String(uiBytes / 1024) + " kB)");
    },
    []() {
      HTTPUpload& up = server.upload();
      if (up.status == UPLOAD_FILE_START) {
        uiAuthorized = flashOK && pwCheck(server.arg("pw"));
        uiBytes = 0;
        if (uiAuthorized) {
          if (!LittleFS.exists("/www")) LittleFS.mkdir("/www");
          uiFile = LittleFS.open("/www/index.tmp", FILE_WRITE);
        }
      } else if (up.status == UPLOAD_FILE_WRITE) {
        if (uiAuthorized && uiFile) { if (uiFile.write(up.buf, up.currentSize) != up.currentSize) { uiFile.close(); LittleFS.remove("/www/index.tmp"); uiAuthorized = false; } else uiBytes += up.currentSize; }
      } else if (up.status == UPLOAD_FILE_END) {
        if (uiFile) uiFile.close();
      } else if (up.status == UPLOAD_FILE_ABORTED) {
        if (uiFile) uiFile.close();
        LittleFS.remove("/www/index.tmp");
        uiBytes = 0;
      }
    });
  server.on("/api/att", HTTP_POST,
    []() {
      if (!gate()) return;
      if (!attOK) { sendErr(507, attErr.length() ? attErr.c_str() : "Upload failed"); return; }
      server.send(200, "application/json", "{\"ok\":true,\"path\":" + jStr(attPath) + ",\"size\":" + String(attBytes) +
                  ",\"where\":" + jStr(attWhere) + "}");
    }, handleAttUpload);
  server.on("/api/doc", HTTP_GET, G(handleDocGet));
  server.on("/api/doc", HTTP_POST, G(handleDocPut));
  server.on("/api/docs", HTTP_GET, G(handleDocList));
  server.on("/api/doc-up", HTTP_POST, []() {
    if (!gate()) return;
    if (!upOK) { sendErr(507, upErr.length() ? upErr.c_str() : "Upload failed"); return; }
    docVer++;
    server.send(200, "application/json", "{\"ok\":true,\"size\":" + String(upBytes) + ",\"ver\":" + String(docVer) + "}");
  }, handleDocUpload);
  server.on("/api/doc-del", HTTP_POST, G(handleDocDel));
  server.on("/api/timers", HTTP_GET, G(handleTimersGet));
  server.on("/api/timers", HTTP_POST, G(handleTimersPost));
  server.on("/api/config", HTTP_GET, G(handleConfigGet));
  server.on("/api/config", HTTP_POST, G(handleConfigPost));
  server.on("/api/scan", HTTP_GET, G(handleScan));
  server.on("/api/gpsdays", HTTP_GET, G(handleGpsDays));
  server.on("/api/gps", HTTP_GET, G(handleGps));
  server.on("/api/gps-add", HTTP_POST, G(handleGpsAdd));
  server.on("/api/hw", HTTP_GET, G([]() { server.sendHeader("Cache-Control", "no-store"); server.send(200, "application/json", hwJson()); }));
  server.on("/api/hw", HTTP_POST, G(handleHwPost));
  server.on("/api/hw-scan", HTTP_POST, G([]() { addScan(true); server.send(200, "application/json", hwJson()); }));
  server.on("/api/hw-test", HTTP_POST, G([]() { lightTestMs = millis(); server.send(200, "application/json", "{\"ok\":true}"); }));
  server.on("/api/gps-del", HTTP_POST, G(handleGpsDel));
  server.on("/api/gmaps-list", HTTP_GET, G(handleGmapsList));
  server.on("/api/restore", HTTP_POST, handleRestoreDone, handleRestoreUpload);
  server.on("/api/att-del", HTTP_POST, []() {
    if (!gate()) return;
    String p = server.arg("f");
    if (!p.startsWith("/att/") || p.indexOf("..") >= 0) { sendErr(400, "Not allowed"); return; }
    bool a = sdOK && SD.exists(p) && SD.remove(p);
    server.send(200, "application/json", String("{\"ok\":true,\"removed\":") + jBool(a) + "}");
  });
  server.on("/api/login", HTTP_POST, handleLogin);
  server.on("/api/logout", HTTP_POST, handleLogout);
  server.on("/api/password", HTTP_POST, G(handlePassword));
  server.on("/api/forget-devices", HTTP_POST, G(handleForget));
  server.onNotFound([]() { if (!gate()) return; if (!serveAttachment(server.uri())) sendErr(404, "Not found"); });
  const char* hk[] = { "Cookie" }; server.collectHeaders(hk, 1);   // the login cookie
  server.begin();
}

// ======================================================================
//  Setup / loop
// ======================================================================
void i2cPowerOn() {
#if defined(PIN_I2C_POWER)
  // ESP32-S2 Feather revisions differ in polarity: drive the opposite of the pin's rest state
  pinMode(PIN_I2C_POWER, INPUT);
  delay(1);
  bool rest = digitalRead(PIN_I2C_POWER);
  i2cRest = rest;
  pinMode(PIN_I2C_POWER, OUTPUT);
  digitalWrite(PIN_I2C_POWER, !rest);
  delay(10);
#endif
}

void lowShow(const char* l1, const char* l2, const char* l3, const char* l4, uint32_t ms) {
  if (!oledOK) return;
  oled.oled_command(SSD1327_DISPLAYON);
  oled.clearDisplay(); oled.setTextColor(C_ON); oled.setTextSize(1);
  oled.setCursor(1, 20); oled.print(l1); oled.setCursor(1, 44); oled.print(l2); oled.setCursor(1, 56); oled.print(l3); oled.setCursor(1, 68); oled.print(l4);
  String v = !isnan(battV) ? String(battV, 2) + " V" : String("");
  oled.setCursor(1, 92); oled.print(v);
  oled.display(); delay(ms);
  oled.clearDisplay(); oled.display(); oled.oled_command(SSD1327_DISPLAYOFF);
}
void lowBattOff() {
  Serial.printf("Battery %.2f V: switching off\n", battV);
  heartbeatNow(true);                                      // the off time is measured from now, and History will say why
  lowShow("BATTERY LOW", "Switched off.", "Charge it, then", "slide power off & on", 6000);
  pinLevel(hp.backLed, 0); pinLevel(hp.selLed, 0); buzSet(0); hapSet(false);
  WiFi.mode(WIFI_OFF);
  if (sdOK) SD.end();                                      // every write is already closed; this just lets go of the card
  if (flashOK) LittleFS.end();
#if defined(PIN_I2C_POWER)
  digitalWrite(PIN_I2C_POWER, i2cRest);                    // the GPS, screen, knob and EEPROM off
#endif
  Serial.flush();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_deep_sleep_start();                                  // nothing wakes it: only the power switch (EN) restarts it
}
void setup() {
  Serial.begin(115200);
  delay(300);
  resetReason = esp_reset_reason();
  Serial.printf("Life Logger v" FW_VERSION "   reset: %s\n", resetName(resetReason));

  setenv("TZ", TZ_POSIX, 1);       // replaced by the saved time zone once flash is mounted
  tzset();

  i2cPowerOn();
  Wire.begin();
  rtcOK = rtc.begin();
  if (rtcOK) {
    if (rtc.initialized() && !rtc.lostPower()) {
      DateTime d = rtc.now();
      if (d.year() >= 2025) { setSystemUtc((time_t)d.unixtime()); timeValid = true; timeSrc = "rtc"; }
    }
    rtc.start();
  }
  flashOK = startFlash();
  if (flashOK) cfgLoad(); else cfgDefaults();
  hwPinsLoad(cfg.hwPins); hwAddLoad(cfg.hwAdd); indParse(cfg.ind, true);
  hwStart();                                               // the scan; expanders and sensors from Settings > Add-ons
  baroStart();
  hapStart();
  if (USB_PIN >= 0) { pinMode(USB_PIN, INPUT); analogSetPinAttenuation(USB_PIN, ADC_11db); }
  eeStart();
  Wire.setClock(400000);
  battStart();
  gpsStart();
  screenStart();

  if (flashOK) { sessLoad(); fixLoad(); }
  tripResume();
  setenv("TZ", cfg.tz.c_str(), 1); tzset();
  sdOK = startSD();
  syncSdMirror();
  chooseStore();
  seedNodes();

  Serial.printf("Screen %s  GPS %s\n", oledOK ? "ok" : "none", gpsI2C ? "ok" : "none");
  Serial.printf("BMP388 %s  vibration motor %s  USB sense %s\n", bmpOK ? "ok" : "none", hapOK ? "ok" : "none", USB_PIN >= 0 ? "A2" : "off");
  Serial.printf("Flash %s  SD %s  RTC %s  EEPROM %s  battery gauge %s   log: %s, %lu events\n",
                flashOK ? "ok" : "FAILED (partition scheme?)", sdOK ? "ok" : "none", rtcOK ? "ok" : "none",
                eeOK ? "ok" : "none", battType == 1 ? "MAX17048" : battType == 2 ? "LC709203F" : "none",
                storeName, (unsigned long)(logNextSeq - 1));
  if (eeOK && lastAlive) Serial.printf("Last heartbeat: %lu (epoch)\n", (unsigned long)lastAlive);

  startWiFi();
  startEspNow();
  setupWeb();
  Serial.printf("House Wi-Fi %s  %s   hotspot %s  %s   ch %d   http://%s.local\n",
                staUp ? "up" : "down", WiFi.localIP().toString().c_str(), apUp ? "up" : "off",
                WiFi.softAPIP().toString().c_str(), (int)WiFi.channel(), cfg.mdns.c_str());
  Serial.printf("MAC for the node sketches (HUB_MAC): %s\n", WiFi.macAddress().c_str());
}

void loop() {
  server.handleClient();
  processRx();

  if (ntpSynced) {
    ntpSynced = false;
    if (!cfg.clkMan) {                    // (Manual stops internet time; a sync already under way is ignored)
      writeRtc(time(nullptr));
      timeValid = true;
      timeSrc = "ntp";
      lastNtpSync = millis();
    }
  }
  gpsTick();
  gpsPowerTick();
  battTick();
  screenTick();
  indTick();

  outageCheck();
  heartbeatTick();
  baroTick();
  envLogTick();
  wifiTick();

  if (!sdOK && millis() - lastSdRetry > 30000) {   // card can be added later, no reboot needed
    lastSdRetry = millis();
    SD.end();
    sdOK = startSD();
    if (sdOK) {
      syncSdMirror();
      chooseStore();
      Serial.println("SD card mounted");
    }
  }
  delay(2);
}
