// SPDX-FileCopyrightText: 2026 Aspect Wave LLC
// SPDX-License-Identifier: GPL-3.0-or-later
/*
  Life Logger house node  v2.0
  Sophie Scopazzi / Aspect Wave LLC

  Board : Adafruit ESP32-S2 Feather with w.FL (6282) + SHT30 on STEMMA QT (address 0x44)
          Antenna: optional. Indoors the node reaches the box without one; a w.FL antenna (5445) adds range.
  Role  : One temperature/humidity node ("Inside" or "Outside") for the Life Logger box.

  Box at home : wakes every SEND_EVERY_S, sends one ESP-NOW reading, gets the time back, deep-sleeps.
  Box away    : no answer, so the node keeps a reading every AWAY_EVERY_S in its own flash and checks
                for the box now and then. When the box is back (with its SD card in), the node sends the
                kept readings in, a few hundred per wake, until it has caught up.

  Timestamps of kept readings: the node's clock runs on its internal RC oscillator, which drifts.
  The box sets the node's clock on every contact; when it comes back, the kept readings are spread
  evenly between the last contact and now (both exact), which removes the drift. If the node lost
  power while away (battery flat), readings from before that can only be placed from the last
  contact forward, uncorrected, and readings taken before its clock was ever set are dropped.

  Channel: the box sits on whatever channel the house router uses. The node remembers the last
           channel that worked (RTC memory) and hunts 1-13 only now and then when that fails.

  Works with the Life Logger box (it collects the kept readings when it is back in range).
  Arduino IDE: Tools > Partition Scheme > "Default 4MB with spiffs" (the kept readings live there).
  Libraries: Adafruit SHT31 (works for SHT30), Adafruit MAX1704X, Adafruit LC709203F, Adafruit BusIO.
  Arduino core: esp32 by Espressif 3.x
*/

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_idf_version.h>
#include <esp_random.h>
#include <Wire.h>
#include <FS.h>
#include <LittleFS.h>
#include <sys/time.h>
#include <Adafruit_SHT31.h>
#include <Adafruit_MAX1704X.h>
#include <Adafruit_LC709203F.h>

#define FW_VERSION "2.0"

// ===================== USER SETTINGS =====================
const char NODE_NAME[]      = "Outside";      // "Inside" / "Outside" (11 characters max)
uint8_t    HUB_MAC[6]       = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };  // dashboard > Settings > Sensor nodes
const uint32_t SEND_EVERY_S = 60;             // box at home
const uint32_t AWAY_EVERY_S = 300;            // box away: one kept reading per wake (matches the box's 5-min log)
const uint8_t  HUNT_EVERY   = 6;              // box away: full channel hunt every 6th wake (30 min)
const uint16_t BATCHES_PER_WAKE = 40;         // catching up: up to 40 x 24 readings per wake
const uint32_t MAX_KEPT_BYTES = 1200000;      // ~1.2 MB of flash = over a year at 5 min
const bool DEEP_SLEEP       = true;           // false = stay awake (USB-powered), prints to Serial
// Only used if the board has the older LC709203F gauge instead of the MAX17048:
const lc709203_adjustment_t LIPO_SIZE = LC709203F_APA_2000MAH;
// =========================================================

// ---------- ESP-NOW packets: keep identical in the Life Logger box firmware ----------
#define HWO_MAGIC 0x31574F48UL   // "HOW1" node -> box: live reading
#define HWO_TIME  0x54574F48UL   // "HOWT" box -> node: clock, and whether it can take buffered readings
#define HWO_BFILL 0x42574F48UL   // "HOWB" node -> box: readings kept while the box was away
#define HWO_BACK  0x41574F48UL   // "HOWA" box -> node: that batch is written
typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint8_t  ver;
  char     name[12];
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
  uint32_t clockS;
  uint32_t pending;
} EnvPacketV2;
typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint32_t epoch;
  uint8_t  flags;        // bit0 epoch valid, bit1 box can store buffered readings
} TimePacket;
typedef struct __attribute__((packed)) {
  uint32_t epoch;
  int16_t  t100;
  uint16_t rh100;
  uint8_t  soc;
} BfRec;
#define BF_MAX 24
typedef struct __attribute__((packed)) {
  uint32_t magic;
  char     name[12];
  uint16_t bseq;
  uint8_t  n;
  BfRec    r[BF_MAX];
} BfPacket;
#define BF_HEAD 19
typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint16_t bseq;
  uint8_t  ok;
} BfAck;
// ---------------------------------------------------------------------------------

const char* KEPT_PATH = "/pending.bin";   // kept readings, node-clock times (not yet corrected)
const char* READY_PATH   = "/ready.bin";     // corrected times, waiting to be sent in
const char* READY_POS = "/ready.pos";    // how far into READY_PATH the box has confirmed

RTC_DATA_ATTR uint8_t  lastCh = 0;      // survives deep sleep, not power loss
RTC_DATA_ATTR uint32_t seqNo  = 0;
RTC_DATA_ATTR uint32_t failCount = 0;
RTC_DATA_ATTR bool     synced = false;  // node clock has been set by the box since power-on
RTC_DATA_ATTR uint32_t syncEpoch = 0;   // when
RTC_DATA_ATTR uint16_t bseq = 0;
RTC_DATA_ATTR bool     away = false;

Adafruit_SHT31      sht;
Adafruit_MAX17048   gaugeMax;
Adafruit_LC709203F  gaugeLc;
bool i2cRestState = LOW;
bool fsOK = false;

volatile int8_t txResult = -1;
volatile bool gotTime = false, gotAck = false;
volatile uint32_t rxEpoch = 0;
volatile uint8_t  rxFlags = 0, rxAckOk = 0;
volatile uint16_t rxAckSeq = 0;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
static void onSent(const esp_now_send_info_t* info, esp_now_send_status_t st) {
#else
static void onSent(const uint8_t* mac, esp_now_send_status_t st) {
#endif
  txResult = (st == ESP_NOW_SEND_SUCCESS) ? 1 : 0;
}

#if ESP_IDF_VERSION_MAJOR >= 5
static void onRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
#else
static void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
#endif
  if (len < 4) return;
  uint32_t m; memcpy(&m, data, 4);
  if (m == HWO_TIME && len == sizeof(TimePacket)) {
    TimePacket t; memcpy(&t, data, sizeof t);
    rxEpoch = t.epoch; rxFlags = t.flags; gotTime = true;
  } else if (m == HWO_BACK && len == sizeof(BfAck)) {
    BfAck a; memcpy(&a, data, sizeof a);
    rxAckSeq = a.bseq; rxAckOk = a.ok; gotAck = true;
  }
}

uint32_t nowS() { struct timeval tv; gettimeofday(&tv, nullptr); return (uint32_t)tv.tv_sec; }
void setClock(uint32_t e) { struct timeval tv = { (time_t)e, 0 }; settimeofday(&tv, nullptr); }
size_t fsize(const char* p) { if (!fsOK || !LittleFS.exists(p)) return 0; File f = LittleFS.open(p, FILE_READ); size_t n = f ? f.size() : 0; if (f) f.close(); return n; }
uint32_t readPos() { File f = LittleFS.open(READY_POS, FILE_READ); uint32_t p = 0; if (f) { f.read((uint8_t*)&p, 4); f.close(); } return p; }
void writePos(uint32_t p) { File f = LittleFS.open(READY_POS, FILE_WRITE); if (f) { f.write((uint8_t*)&p, 4); f.close(); } }
uint32_t keptCount() { uint32_t r = fsize(READY_PATH); uint32_t pos = r ? readPos() : 0; return (fsize(KEPT_PATH) + (r > pos ? r - pos : 0)) / sizeof(BfRec); }

void i2cPower(bool on) {
#if defined(PIN_I2C_POWER)
  // Feather revisions differ in polarity: "on" is the opposite of the pin's rest state
  static bool probed = false;
  if (!probed) {
    pinMode(PIN_I2C_POWER, INPUT);
    delay(1);
    i2cRestState = digitalRead(PIN_I2C_POWER);
    probed = true;
  }
  pinMode(PIN_I2C_POWER, OUTPUT);
  digitalWrite(PIN_I2C_POWER, on ? !i2cRestState : i2cRestState);
#endif
}

void readSensors(EnvPacket& p) {
  i2cPower(true);
  delay(10);
  Wire.begin();
  p.tempC = NAN; p.rh = NAN; p.vbat = NAN; p.soc = NAN; p.flags = 0;
  if (sht.begin(0x44)) {
    float t, h;
    if (sht.readBoth(&t, &h)) { p.tempC = t; p.rh = h; p.flags |= 1; }
  }
  if (gaugeMax.begin()) {
    p.vbat = gaugeMax.cellVoltage();
    p.soc  = gaugeMax.cellPercent();
    p.flags |= 2;
  } else if (gaugeLc.begin()) {
    gaugeLc.setPackSize(LIPO_SIZE);
    p.vbat = gaugeLc.cellVoltage();
    p.soc  = gaugeLc.cellPercent();
    p.flags |= 2;
  }
  if (p.soc > 100.0f) p.soc = 100.0f;      // MAX17048 can read a little over 100 on charge
}

bool sendRaw(const void* d, size_t n) {
  for (int tries = 0; tries < 2; tries++) {
    txResult = -1;
    if (esp_now_send(HUB_MAC, (const uint8_t*)d, n) != ESP_OK) return false;
    uint32_t t0 = millis();
    while (txResult < 0 && millis() - t0 < 60) delay(1);
    if (txResult == 1) return true;        // box's radio acknowledged
  }
  return false;
}

bool sendOn(uint8_t ch, const void* d, size_t n) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  return sendRaw(d, n);
}

bool radioUp() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onRecv);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, HUB_MAC, 6);
  peer.channel = 0;                        // 0 = whatever channel the radio is on
  peer.ifidx   = WIFI_IF_STA;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

// Live reading: last channel first; the full 1-13 hunt only every HUNT_EVERY-th try while the box is away
bool sendLive(const void* d, size_t n) {
  if (lastCh && sendOn(lastCh, d, n)) return true;
  if (lastCh && away && (failCount % HUNT_EVERY) != 0) return false;
  for (uint8_t ch = 1; ch <= 13; ch++) {
    if (ch == lastCh) continue;
    if (sendOn(ch, d, n)) { lastCh = ch; return true; }
  }
  return false;
}

// Keep this reading (box away). Time = node clock, corrected later.
void keep(const EnvPacket& p) {
  if (!fsOK) return;
  if (fsize(KEPT_PATH) + fsize(READY_PATH) > MAX_KEPT_BYTES) return;    // full: newest are not kept
  BfRec r;
  r.epoch = nowS();
  r.t100  = isnan(p.tempC) ? INT16_MIN : (int16_t)lroundf(p.tempC * 100.0f);
  r.rh100 = isnan(p.rh) ? 0xFFFF : (uint16_t)lroundf(p.rh * 100.0f);
  r.soc   = isnan(p.soc) ? 255 : (uint8_t)lroundf(p.soc);
  File f = LittleFS.open(KEPT_PATH, FILE_APPEND);
  if (f) { f.write((uint8_t*)&r, sizeof r); f.close(); }
}

// Box answered with the true time T: correct the kept readings and move them to READY_PATH
void syncTo(uint32_t T) {
  uint32_t N = nowS();
  if (fsOK && fsize(KEPT_PATH)) {
    double ratio = 1.0;
    bool scale = synced && syncEpoch > 1735689600UL && N > syncEpoch + 60 && T > syncEpoch;
    if (scale) { ratio = (double)(T - syncEpoch) / (double)(N - syncEpoch); if (ratio < 0.9 || ratio > 1.1) scale = false; }
    File in = LittleFS.open(KEPT_PATH, FILE_READ), out = LittleFS.open(READY_PATH, FILE_APPEND);
    BfRec r;
    while (in && out && in.read((uint8_t*)&r, sizeof r) == sizeof r) {
      if (synced) {
        if (scale && r.epoch >= syncEpoch) r.epoch = syncEpoch + (uint32_t)llround((double)(r.epoch - syncEpoch) * ratio);
        else r.epoch = r.epoch + (T - N);                          // shift only
      } else {
        if (N < 1735689600UL) r.epoch = T - (N - r.epoch);          // clock since power-on: anchor at now
        // else: kept before a power loss with a clock that was set then: start-anchored, kept as is
      }
      out.write((uint8_t*)&r, sizeof r);
    }
    if (in) in.close(); if (out) out.close();
    LittleFS.remove(KEPT_PATH);
  }
  setClock(T);
  synced = true;
  syncEpoch = T;
}

// Send READY_PATH in batches; the box confirms each one after writing it to its SD card
void catchUp() {
  if (!fsOK) return;
  size_t total = fsize(READY_PATH);
  if (!total) return;
  uint32_t pos = readPos();
  File f = LittleFS.open(READY_PATH, FILE_READ);
  if (!f) return;
  for (uint16_t b = 0; b < BATCHES_PER_WAKE && pos < total; b++) {
    BfPacket bp = {};
    bp.magic = HWO_BFILL;
    strncpy(bp.name, NODE_NAME, 11);
    bp.bseq = bseq;
    f.seek(pos);
    int got = f.read((uint8_t*)bp.r, sizeof(BfRec) * BF_MAX);
    bp.n = got / sizeof(BfRec);
    if (!bp.n) break;
    bool done = false;
    for (int tries = 0; tries < 3 && !done; tries++) {
      gotAck = false;
      if (!sendRaw(&bp, BF_HEAD + bp.n * sizeof(BfRec))) continue;
      uint32_t t0 = millis();
      while (!gotAck && millis() - t0 < 300) delay(1);
      if (gotAck && rxAckSeq == bp.bseq) {
        if (!rxAckOk) { f.close(); return; }       // box can't store them right now (no SD): try another day
        done = true;
      }
    }
    if (!done) break;
    pos += bp.n * sizeof(BfRec);
    bseq++;
    writePos(pos);
  }
  f.close();
  if (pos >= total) { LittleFS.remove(READY_PATH); LittleFS.remove(READY_POS); }
}

// Power came back after the battery ran flat: kept readings from before still count
void coldBootCleanup() {
  if (!fsOK || !fsize(KEPT_PATH)) return;
  File in = LittleFS.open(KEPT_PATH, FILE_READ), out = LittleFS.open(READY_PATH, FILE_APPEND);
  BfRec r;
  while (in && out && in.read((uint8_t*)&r, sizeof r) == sizeof r)
    if (r.epoch > 1735689600UL) out.write((uint8_t*)&r, sizeof r);      // clock was set: keep; never set: no way to place it
  if (in) in.close(); if (out) out.close();
  LittleFS.remove(KEPT_PATH);
}

uint32_t doOne() {
  EnvPacketV2 p = {};
  p.b.magic = HWO_MAGIC;
  p.b.ver = 2;
  strncpy(p.b.name, NODE_NAME, 11);
  p.b.seq = ++seqNo;
  p.b.intervalS = SEND_EVERY_S;
  readSensors(p.b);
  p.clockS = nowS();
  p.pending = keptCount();

  bool up = radioUp();
  gotTime = false;
  bool ok = up && sendLive(&p, sizeof p);
  if (ok) {
    uint32_t t0 = millis();
    while (!gotTime && millis() - t0 < 200) delay(1);
    failCount = 0; away = false;
    if (gotTime && (rxFlags & 1)) {
      syncTo(rxEpoch);
      if (rxFlags & 2) catchUp();
    }
  } else {
    failCount++;
    if (failCount >= 2) away = true;       // one lost packet isn't "away"
    if (away) keep(p.b);
  }
  esp_now_deinit();
  if (!DEEP_SLEEP) {
    Serial.printf("[%s #%lu] %.2f C  %.1f %%RH  %.0f %%  -> %s (ch %u)  kept %lu  clock %s\n", NODE_NAME,
                  (unsigned long)p.b.seq, p.b.tempC, p.b.rh, p.b.soc, ok ? (gotTime ? "box ack+time" : "box ack") : "no box",
                  lastCh, (unsigned long)keptCount(), synced ? "set" : "not set");
  }
  return away ? AWAY_EVERY_S : SEND_EVERY_S;
}

void setup() {
  if (!DEEP_SLEEP) { Serial.begin(115200); delay(500); Serial.println("Life Logger node v" FW_VERSION); }
  uint32_t t0 = millis();
  fsOK = LittleFS.begin(true);
  if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {     // power-on: RTC memory is blank
    bseq = (uint16_t)esp_random();
    coldBootCleanup();
  }
  uint32_t every = doOne();
  if (DEEP_SLEEP) {
    i2cPower(false);
    uint64_t awakeUs = (uint64_t)(millis() - t0) * 1000ULL;
    uint64_t sleepUs = (uint64_t)every * 1000000ULL;
    sleepUs = (sleepUs > awakeUs + 1000000ULL) ? sleepUs - awakeUs : 1000000ULL;
    esp_sleep_enable_timer_wakeup(sleepUs);
    esp_deep_sleep_start();
  }
}

void loop() {
  static uint32_t last = millis(), every = SEND_EVERY_S;
  if (millis() - last >= every * 1000UL) {
    last = millis();
    every = doOne();
  }
  delay(10);
}
