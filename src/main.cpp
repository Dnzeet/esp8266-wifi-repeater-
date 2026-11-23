// ESP8266 Wi-Fi repeater for Wemos D1 mini and D1 mini Pro
// SSD1306 128x64 UI, six-button input, battery monitor (A0), NeoPixel status, web configuration.

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <U8g2lib.h>
#include <Adafruit_NeoPixel.h>
#include <lwip/napt.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <LwipDhcpServer.h>
extern "C" {
#include <user_interface.h>
}
#include "webui.h"

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

#define NAPT_ENTRIES 512
#define NAPT_PORTS   10
#define CFG_MAGIC    0x52505435u

// ===================================================================
//  Config
// ===================================================================
enum Btn : uint8_t { B_UP, B_DOWN, B_LEFT, B_RIGHT, B_OK, B_BACK, B_COUNT };
enum PinRole : uint8_t { P_UP, P_DOWN, P_LEFT, P_RIGHT, P_OK, P_BACK, P_SDA, P_SCL, P_LED, P_COUNT };

#define MAX_SAVED 8
struct SavedNet { char ssid[33]; char pass[65]; };

struct Config {
  uint32_t magic;
  SavedNet saved[MAX_SAVED];  // remembered uplinks, most recently used first
  char apSsid[33];
  char apPass[65];
  uint8_t apHidden;
  uint8_t apSetupOpen;  // keep the AP open (no password) until the upstream link is up
  uint8_t dnsMode;      // DNS handed to clients: 0 upstream, 1-4 presets, 5 custom
  uint8_t dnsCustom[4];
  uint8_t maxClients;
  uint8_t repeater;
  uint8_t brightness;   // OLED, percent 5..100
  uint16_t timeoutSec;  // 0 = never
  uint8_t flip;
  uint8_t ledMode;      // 0 off, 1 connection, 2 battery, 3 alternate
  uint8_t ledBright;    // percent 5..100
  uint8_t ledCount;     // 1..8
  uint16_t adcFullMv;   // battery millivolts that read as ADC 1023
  int8_t pin[P_COUNT];
  uint8_t pingLed;      // blink the onboard LED to show ping (appended last to keep older saved data valid)
};

static Config cfg;
static const int8_t DEFAULT_PINS[P_COUNT] = {14, 12, 13, 0, 2, 3, 4, 5, 15};
static const uint16_t TIMEOUTS[] = {0, 15, 30, 60, 120, 300};
static const char *const TIMEOUT_LABELS[] = {"Never", "15 s", "30 s", "1 min", "2 min", "5 min"};
#define N_TIMEOUTS 6
static const char *const LED_MODE[5] = {"Off", "Connection", "Battery", "Alternate", "Ping"};
static const char *const LED_MODE_SHORT[5] = {"Off", "Link", "Batt", "Alt", "Ping"};

static void cfgSave() {
  EEPROM.put(0, cfg);
  EEPROM.commit();
}

static void cfgDefaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = CFG_MAGIC;
  strlcpy(cfg.apSsid, "ESP-Repeater", sizeof(cfg.apSsid));
  strlcpy(cfg.apPass, "repeater123", sizeof(cfg.apPass));
  cfg.maxClients = 4;
  cfg.apSetupOpen = 1;
  cfg.repeater = 1;
  cfg.brightness = 70;
  cfg.timeoutSec = 30;
  cfg.ledMode = 1;
  cfg.ledBright = 40;
  cfg.ledCount = 1;
  cfg.adcFullMv = 4200;
  memcpy(cfg.pin, DEFAULT_PINS, sizeof(cfg.pin));
}

static bool pinAllowed(int p, bool no16) {
  switch (p) {
    case 0: case 1: case 2: case 3: case 4: case 5:
    case 12: case 13: case 14: case 15: return true;
    case 16: return !no16;
    default: return false;
  }
}

static bool pinsValid(const int8_t *p, const char **err) {
  for (int i = 0; i < P_COUNT; i++) {
    bool required = (i == P_SDA || i == P_SCL);
    bool no16 = required || i == P_LED;
    if (p[i] == -1) {
      if (required) { if (err) *err = "Display pins are required"; return false; }
      continue;
    }
    if (!pinAllowed(p[i], no16)) { if (err) *err = "Unsupported GPIO number"; return false; }
    for (int j = 0; j < i; j++)
      if (p[j] == p[i]) { if (err) *err = "Each pin can only be used once"; return false; }
  }
  return true;
}

static int timeoutIdx() {
  for (int i = 0; i < N_TIMEOUTS; i++) if (TIMEOUTS[i] == cfg.timeoutSec) return i;
  return 2;
}

static void cfgLoad() {
  EEPROM.begin(2048);
  EEPROM.get(0, cfg);
  if (cfg.magic != CFG_MAGIC) { cfgDefaults(); cfgSave(); }
  for (int i = 0; i < MAX_SAVED; i++) { cfg.saved[i].ssid[32] = 0; cfg.saved[i].pass[64] = 0; }
  cfg.apSsid[32] = 0; cfg.apPass[64] = 0;
  if (!cfg.apSsid[0]) strlcpy(cfg.apSsid, "ESP-Repeater", sizeof(cfg.apSsid));
  if (cfg.brightness < 5 || cfg.brightness > 100) cfg.brightness = 70;
  if (cfg.maxClients < 1 || cfg.maxClients > 8) cfg.maxClients = 4;
  if (cfg.apSetupOpen > 1) cfg.apSetupOpen = 1;
  if (cfg.dnsMode > 5) cfg.dnsMode = 0;
  if (cfg.ledMode > 4) cfg.ledMode = 1;
  if (cfg.pingLed > 1) cfg.pingLed = 0;
  if (cfg.ledBright < 5 || cfg.ledBright > 100) cfg.ledBright = 40;
  if (cfg.ledCount < 1 || cfg.ledCount > 8) cfg.ledCount = 1;
  if (cfg.adcFullMv < 1000 || cfg.adcFullMv > 20000) cfg.adcFullMv = 4200;
  bool tOk = false;
  for (int i = 0; i < N_TIMEOUTS; i++) if (TIMEOUTS[i] == cfg.timeoutSec) tOk = true;
  if (!tOk) cfg.timeoutSec = 30;
  if (!pinsValid(cfg.pin, nullptr)) memcpy(cfg.pin, DEFAULT_PINS, sizeof(cfg.pin));
}

// ===================================================================
//  Saved networks and DNS choice
// ===================================================================
static bool haveSaved() { return cfg.saved[0].ssid[0] != 0; }

static int savedN() {
  int n = 0;
  for (int i = 0; i < MAX_SAVED; i++) if (cfg.saved[i].ssid[0]) n++;
  return n;
}

static int savedFind(const char *ssid) {
  for (int i = 0; i < MAX_SAVED; i++)
    if (cfg.saved[i].ssid[0] && !strcmp(cfg.saved[i].ssid, ssid)) return i;
  return -1;
}

static void savedPromote(int i) {
  if (i <= 0) return;
  SavedNet t = cfg.saved[i];
  for (int j = i; j > 0; j--) cfg.saved[j] = cfg.saved[j - 1];
  cfg.saved[0] = t;
}

static void savedAdd(const char *ssid, const char *pass) {
  int i = savedFind(ssid);
  if (i < 0) {
    for (i = 0; i < MAX_SAVED; i++) if (!cfg.saved[i].ssid[0]) break;
    if (i >= MAX_SAVED) i = MAX_SAVED - 1;
    memset(&cfg.saved[i], 0, sizeof(SavedNet));
    strlcpy(cfg.saved[i].ssid, ssid, sizeof(cfg.saved[i].ssid));
  }
  strlcpy(cfg.saved[i].pass, pass, sizeof(cfg.saved[i].pass));
  savedPromote(i);
}

static void savedRemove(int i) {
  for (int j = i; j < MAX_SAVED - 1; j++) cfg.saved[j] = cfg.saved[j + 1];
  memset(&cfg.saved[MAX_SAVED - 1], 0, sizeof(SavedNet));
}

static const uint8_t DNS_P[5][4] = {{0, 0, 0, 0}, {1, 1, 1, 1}, {8, 8, 8, 8}, {9, 9, 9, 9}, {94, 140, 14, 14}};
static const char *const DNS_NAME[6] = {"Upstream (auto)", "Cloudflare", "Google", "Quad9", "AdGuard", "Custom"};
static const char *const DNS_SHORT[6] = {"Auto", "CF", "Google", "Quad9", "AdGuard", "Custom"};

static bool customSet() { return cfg.dnsCustom[0] || cfg.dnsCustom[1] || cfg.dnsCustom[2] || cfg.dnsCustom[3]; }

static IPAddress dnsEffective() {
  if (cfg.dnsMode >= 1 && cfg.dnsMode <= 4) return IPAddress(DNS_P[cfg.dnsMode][0], DNS_P[cfg.dnsMode][1], DNS_P[cfg.dnsMode][2], DNS_P[cfg.dnsMode][3]);
  if (cfg.dnsMode == 5 && customSet()) return IPAddress(cfg.dnsCustom[0], cfg.dnsCustom[1], cfg.dnsCustom[2], cfg.dnsCustom[3]);
  IPAddress d = WiFi.dnsIP(0);
  if (!(uint32_t)d) d = WiFi.dnsIP(1);
  if (!(uint32_t)d) d = IPAddress(1, 1, 1, 1);
  return d;
}

// ===================================================================
//  Shared state
// ===================================================================
enum Screen : uint8_t {
  SC_SPLASH, SC_HOME, SC_MENU, SC_NETS, SC_KEYS, SC_CONNECT,
  SC_DISPLAY, SC_LED, SC_INFO, SC_CONFIRM, SC_MESSAGE, SC_SAVED, SC_SPEED
};
enum ConnState : uint8_t { CS_IDLE, CS_CONNECTING, CS_CONNECTED, CS_FAILED };
enum ConfirmKind : uint8_t { CF_SAVED, CF_RESTART };

struct ListState { int sel; int top; };
struct Net { char ssid[33]; int8_t rssi; uint8_t enc; };

#define MAX_NETS 24
static Net nets[MAX_NETS];
static uint8_t netCount = 0;
static bool scanning = false;
static uint32_t scanStart = 0;

static Screen screen = SC_SPLASH;
static uint32_t splashUntil = 0;
static bool dirty = true;
static bool cfgDirty = false;
static uint32_t lastDraw = 0;
static ListState menuL, netL, dispL, ledL, infoL, savedL;
static ConfirmKind confirmKind;
static bool confirmYes = false;
static int confirmIdx = 0;
static char toastMsg[28];
static uint32_t toastUntil = 0;
static char msg1[24], msg2[40];

static ConnState cs = CS_IDLE;
static uint32_t csSince = 0, doneAt = 0, nextRetry = 0;
static char pendSsid[33], pendPass[65], connectedSsid[33], failReason[24];
static bool pendSave = false;

static uint32_t rebootAt = 0, apRestartAt = 0;
static bool apIsOpen = false, everConnected = false;
static uint32_t lastConnAt = 0;
static bool autoScan = false, autoTried = false;
static bool inetKnown = false, inetOk = false, dnsOk = false;
static uint16_t pingMs = 0, dnsMs = 0;
static uint32_t inetNext = 0;
static uint8_t speedState = 0;  // 0 none, 1 running, 2 done, 3 failed
static bool speedPending = false;
static uint32_t speedKbps = 0, speedLiveKbps = 0;
static uint8_t speedPct = 0;
static char speedMsg[24];
static bool builtinActive = false;
static uint32_t builtinChangedAt = 0;

static char kbBuf[64], kbSsid[33];
static bool kbShow = false;
static uint8_t kbPage = 0, kbR = 0, kbC = 0;

static bool screenOn = true;
static uint32_t lastActivity = 0;

// ===================================================================
//  Battery (A0 through a voltage divider)
// ===================================================================
static float batRaw = 0;
static uint16_t batMv = 0;
static uint8_t batPct = 0;
static bool batPresent = false, batInit = false;
static uint32_t batAt = 0;

static const uint16_t BAT_MV[] = {4200, 4150, 4110, 4080, 4020, 3980, 3950, 3910, 3870, 3850, 3840,
                                  3820, 3800, 3790, 3770, 3750, 3730, 3710, 3690, 3610, 3270};
static const uint8_t BAT_PC[] = {100, 95, 90, 85, 80, 75, 70, 65, 60, 55, 50,
                                 45, 40, 35, 30, 25, 20, 15, 10, 5, 0};

static uint8_t batPercent(uint16_t mv) {
  if (mv >= BAT_MV[0]) return 100;
  for (int i = 0; i < 20; i++) {
    if (mv >= BAT_MV[i + 1])
      return BAT_PC[i + 1] + (uint32_t)(mv - BAT_MV[i + 1]) * (BAT_PC[i] - BAT_PC[i + 1]) / (BAT_MV[i] - BAT_MV[i + 1]);
  }
  return 0;
}

static void batPoll() {
  uint32_t now = millis();
  if (now - batAt < 500) return;
  batAt = now;
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogRead(A0);
  float r = sum / 8.0f;
  batRaw = batInit ? batRaw * 0.7f + r * 0.3f : r;
  batInit = true;
  batMv = (uint16_t)(batRaw * cfg.adcFullMv / 1023.0f);
  batPresent = batMv >= 2500;
  batPct = batPresent ? batPercent(batMv) : 0;
}

static bool batCalibrate(uint32_t measuredMv) {
  if (batRaw < 20 || measuredMv < 2000 || measuredMv > 30000) return false;
  uint32_t full = (uint32_t)(measuredMv * 1023.0f / batRaw);
  if (full < 1000 || full > 20000) return false;
  cfg.adcFullMv = (uint16_t)full;
  cfgSave();
  batInit = false;
  batAt = 0;
  return true;
}

// ===================================================================
//  Display helpers
// ===================================================================
static U8G2 *oled = nullptr;
static const uint8_t *const F_UI  = u8g2_font_6x10_tr;
static const uint8_t *const F_SM  = u8g2_font_5x7_tr;
static const uint8_t *const F_BIG = u8g2_font_7x13B_tr;

static const uint8_t IC_WIFI[] PROGMEM = {0x7e, 0x81, 0x3c, 0x42, 0x18, 0x18, 0x00, 0x00};
static const uint8_t IC_POWER[] PROGMEM = {0x18, 0x5a, 0x99, 0x81, 0x81, 0x42, 0x3c, 0x00};
static const uint8_t IC_SUN[] PROGMEM = {0x08, 0x2a, 0x1c, 0x7f, 0x1c, 0x2a, 0x08, 0x00};
static const uint8_t IC_LED[] PROGMEM = {0x3c, 0x42, 0x42, 0x42, 0x24, 0x3c, 0x3c, 0x18};
static const uint8_t IC_INFO[] PROGMEM = {0x3c, 0x42, 0x99, 0x81, 0x99, 0x99, 0x42, 0x3c};
static const uint8_t IC_X[] PROGMEM = {0x81, 0xc3, 0x66, 0x3c, 0x3c, 0x66, 0xc3, 0x81};
static const uint8_t IC_REFRESH[] PROGMEM = {0x3c, 0x42, 0xe1, 0x41, 0x01, 0x42, 0x3c, 0x00};
static const uint8_t IC_STAR[] PROGMEM = {0x18, 0x18, 0xff, 0x7e, 0x3c, 0x66, 0x42, 0x00};
static const uint8_t IC_GLOBE[] PROGMEM = {0x3c, 0x5a, 0x99, 0xff, 0x99, 0x5a, 0x3c, 0x00};
static const uint8_t IC_BOLT[] PROGMEM = {0x30, 0x18, 0x0c, 0x7e, 0x18, 0x0c, 0x06, 0x00};

static void applyBrightness() {
  if (oled) oled->setContrast((uint8_t)(2 + (uint32_t)cfg.brightness * 253 / 100));
}

static void displayInit() {
  oled = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE, cfg.pin[P_SCL], cfg.pin[P_SDA]);
  oled->begin();
  oled->setBusClock(400000);
  oled->setBitmapMode(1);
  oled->setFlipMode(cfg.flip ? 1 : 0);
  applyBrightness();
}

static void screenWake() {
  screenOn = true;
  if (oled) oled->setPowerSave(0);
  lastActivity = millis();
  dirty = true;
}

static void screenSleep() {
  screenOn = false;
  if (oled) oled->setPowerSave(1);
}

// Copies `in` to `out`, replaces non-ASCII bytes, and shortens with ".." to fit maxPx.
static void fit(const char *in, char *out, size_t n, int maxPx) {
  size_t i = 0;
  for (; in[i] && i < n - 1; i++) {
    uint8_t c = (uint8_t)in[i];
    out[i] = (c < 0x20 || c > 0x7e) ? '?' : (char)c;
  }
  out[i] = 0;
  if (oled->getStrWidth(out) <= maxPx) return;
  char tmp[48];
  while (i > 0) {
    i--;
    out[i] = 0;
    snprintf(tmp, sizeof(tmp), "%s..", out);
    if (oled->getStrWidth(tmp) <= maxPx) { strlcpy(out, tmp, n); return; }
  }
}

static int rssiLevel(int r) { return r >= -55 ? 4 : r >= -65 ? 3 : r >= -75 ? 2 : r >= -85 ? 1 : 0; }

static void bars(int x, int yb, int lvl) {
  for (int i = 0; i < 4; i++) {
    int h = 2 + i * 2;
    if (i < lvl) oled->drawBox(x + i * 4, yb - h + 1, 3, h);
    else oled->drawHLine(x + i * 4, yb, 3);
  }
}

static void drawLock(int x, int y) {
  oled->drawHLine(x + 1, y, 3);
  oled->drawVLine(x + 1, y + 1, 2);
  oled->drawVLine(x + 3, y + 1, 2);
  oled->drawBox(x, y + 3, 5, 4);
}

static void drawTick(int x, int y) {
  oled->drawLine(x, y + 3, x + 2, y + 5);
  oled->drawLine(x + 2, y + 5, x + 6, y);
}

// Battery glyph and percentage, right-aligned to xr, sitting in the top strip.
static void drawBattery(int xr) {
  oled->setFont(F_SM);
  if (!batPresent) {
    oled->drawStr(xr - oled->getStrWidth("USB"), 8, "USB");
    return;
  }
  int x = xr - 16;
  oled->drawFrame(x, 1, 14, 8);
  oled->drawBox(x + 14, 3, 2, 4);
  bool blink = batPct < 15 && ((millis() / 400) % 2);
  int w = (batPct * 10 + 50) / 100;
  if (w > 10) w = 10;
  if (!blink && w > 0) oled->drawBox(x + 2, 3, w, 4);
  char b[8];
  snprintf(b, sizeof(b), "%u%%", (unsigned)batPct);
  oled->drawStr(x - 3 - oled->getStrWidth(b), 8, b);
}

static void header(const char *title) {
  oled->setDrawColor(1);
  oled->setFont(F_UI);
  oled->drawStr(3, 9, title);
  drawBattery(125);
  oled->drawHLine(0, 11, 128);
}

static void scrollbar(int total, int top, int vis) {
  if (total <= vis) return;
  oled->drawVLine(127, 13, 50);
  int h = 50 * vis / total;
  if (h < 6) h = 6;
  int y = 13 + (50 - h) * top / (total - vis);
  oled->drawBox(126, y, 2, h);
}

static void listNav(ListState &L, int n, int d, int vis) {
  if (n <= 0) return;
  L.sel = (L.sel + d + n) % n;
  if (L.sel < L.top) L.top = L.sel;
  if (L.sel >= L.top + vis) L.top = L.sel - vis + 1;
  if (L.sel == 0) L.top = 0;
  if (L.sel == n - 1) L.top = (n - vis > 0) ? n - vis : 0;
}

static void rowBegin(int y, bool sel) {
  if (sel) {
    oled->setDrawColor(1);
    oled->drawBox(0, y, 125, 12);
    oled->setDrawColor(0);
  } else {
    oled->setDrawColor(1);
  }
}

static void segBar(int x, int y, int value) {  // 10 segments, value 0..100
  int on = (value + 9) / 10;
  for (int i = 0; i < 10; i++) {
    if (i < on) oled->drawBox(x + i * 4, y, 3, 6);
    else oled->drawHLine(x + i * 4, y + 5, 3);
  }
}

// ===================================================================
//  Buttons
// ===================================================================
struct Button {
  int8_t pin;
  bool stable, raw, longSent;
  uint32_t rawSince, downSince, nextRepeat;
};
static Button btn[B_COUNT];
static bool evPress[B_COUNT], evLong[B_COUNT];

static void buttonsInit() {
  for (int i = 0; i < B_COUNT; i++) {
    memset(&btn[i], 0, sizeof(Button));
    btn[i].pin = cfg.pin[i];
    if (btn[i].pin >= 0) pinMode(btn[i].pin, btn[i].pin == 16 ? INPUT : INPUT_PULLUP);
  }
}

// Direction buttons repeat while held. BACK fires on release (short) or after 700 ms (long).
static void buttonsPoll() {
  uint32_t now = millis();
  memset(evPress, 0, sizeof(evPress));
  memset(evLong, 0, sizeof(evLong));
  for (int i = 0; i < B_COUNT; i++) {
    Button &b = btn[i];
    if (b.pin < 0) continue;
    // the onboard LED shares GPIO2 with some button layouts: ignore that pin while it is driving the LED
    if (b.pin == LED_BUILTIN && (builtinActive || now - builtinChangedAt < 40)) continue;
    bool raw = digitalRead(b.pin) == LOW;
    if (raw != b.raw) { b.raw = raw; b.rawSince = now; }
    if (now - b.rawSince >= 25 && raw != b.stable) {
      b.stable = raw;
      if (raw) {
        b.downSince = now;
        b.nextRepeat = now + 400;
        b.longSent = false;
        if (i != B_BACK) evPress[i] = true;
      } else if (i == B_BACK && !b.longSent) {
        evPress[i] = true;
      }
    }
    if (b.stable) {
      if (i < B_OK && (int32_t)(now - b.nextRepeat) >= 0) { evPress[i] = true; b.nextRepeat = now + 90; }
      if (i == B_BACK && !b.longSent && now - b.downSince >= 700) { evLong[i] = true; b.longSent = true; }
    }
  }
}

// ===================================================================
//  Traffic meter (bytes to and from devices on the access point)
// ===================================================================
static volatile uint32_t bytesToClients = 0, bytesFromClients = 0;
static netif_linkoutput_fn origOut = nullptr;
static netif_input_fn origIn = nullptr;
static uint32_t dlKbps = 0, ulKbps = 0;

static err_t IRAM_ATTR hookOut(struct netif *n, struct pbuf *p) {
  bytesToClients += p->tot_len;
  return origOut ? origOut(n, p) : ERR_IF;
}

static err_t IRAM_ATTR hookIn(struct pbuf *p, struct netif *n) {
  bytesFromClients += p->tot_len;
  return origIn ? origIn(p, n) : ERR_IF;
}

// Wraps the access point interface so every frame to or from a client is counted.
static void trafficHook() {
  uint32_t ap = (uint32_t)WiFi.softAPIP();
  if (!ap) return;
  for (struct netif *n = netif_list; n; n = n->next) {
    if (netif_ip4_addr(n)->addr != ap) continue;
    if (n->linkoutput != hookOut) { origOut = n->linkoutput; n->linkoutput = hookOut; }
    if (n->input != hookIn) { origIn = n->input; n->input = hookIn; }
    return;
  }
}

static void trafficTick() {
  static uint32_t last = 0, pd = 0, pu = 0, hookAt = 0;
  uint32_t now = millis();
  if (now - last < 1000) return;
  uint32_t el = now - last;
  last = now;
  uint32_t d = bytesToClients, u = bytesFromClients;
  uint32_t kd = (uint32_t)((uint64_t)(d - pd) * 8 / el);  // bytes * 8 / ms = kbit/s
  uint32_t ku = (uint32_t)((uint64_t)(u - pu) * 8 / el);
  pd = d;
  pu = u;
  dlKbps = (dlKbps + kd * 2) / 3;
  ulKbps = (ulKbps + ku * 2) / 3;
  if (now - hookAt > 3000) { hookAt = now; trafficHook(); }
}

static void fmtRate(char *o, size_t n, uint32_t k) {
  if (k < 1000) snprintf(o, n, "%uK", (unsigned)k);
  else snprintf(o, n, "%u.%uM", (unsigned)(k / 1000), (unsigned)((k % 1000) / 100));
}

// ===================================================================
//  Wi-Fi core
// ===================================================================
static IPAddress apIpWanted() {
  if (cs == CS_CONNECTED) {
    IPAddress l = WiFi.localIP();
    if (l[0] == 192 && l[1] == 168 && l[2] == 4) return IPAddress(192, 168, 45, 1);
  }
  return IPAddress(192, 168, 4, 1);
}

static void applyDns() {
  // This core's DHCP server hands out a single DNS address.
  ip4_addr_t a;
  a.addr = (uint32_t)dnsEffective();
  WiFi.softAPDhcpServer().setDns(a);
}

// Setup mode: the AP has no password until the upstream link is up (or after it has been lost for a while).
static bool apWantOpen() {
  if (!cfg.apPass[0]) return true;
  if (!cfg.apSetupOpen) return false;
  if (cs == CS_CONNECTED) return false;
  if (!everConnected) return true;
  return millis() - lastConnAt > 15000;
}

static void startAp() {
  IPAddress ip = apIpWanted();
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  int ch = (cs == CS_CONNECTED) ? WiFi.channel() : 1;
  apIsOpen = apWantOpen();
  WiFi.softAP(cfg.apSsid, apIsOpen ? nullptr : cfg.apPass, ch, cfg.apHidden, cfg.maxClients);
  applyDns();
}

static void setRepeater(bool on) {
  cfg.repeater = on ? 1 : 0;
  cfgSave();
  ip_napt_enable_no(SOFTAP_IF, cfg.repeater);
  dirty = true;
}

static void staConnect(const char *ssid, const char *pass, bool save) {
  strlcpy(pendSsid, ssid, sizeof(pendSsid));
  strlcpy(pendPass, pass, sizeof(pendPass));
  pendSave = save;
  failReason[0] = 0;
  WiFi.disconnect();
  WiFi.begin(pendSsid, pendPass);
  cs = CS_CONNECTING;
  csSince = millis();
  dirty = true;
}

static void failConnect(const char *why) {
  WiFi.disconnect();
  cs = CS_FAILED;
  strlcpy(failReason, why, sizeof(failReason));
  nextRetry = millis() + 30000;
  pendSave = false;
  dirty = true;
}

static void cancelConnect() {
  WiFi.disconnect();
  cs = CS_IDLE;
  pendSave = false;
  nextRetry = millis() + 30000;
  dirty = true;
}

static void onConnected() {
  cs = CS_CONNECTED;
  everConnected = true;
  lastConnAt = millis();
  failReason[0] = 0;
  strlcpy(connectedSsid, WiFi.SSID().c_str(), sizeof(connectedSsid));
  if (pendSave) {
    savedAdd(pendSsid, pendPass);
    cfgSave();
    pendSave = false;
  } else {
    int si = savedFind(connectedSsid);
    if (si > 0) { savedPromote(si); cfgSave(); }
  }
  inetKnown = false;
  inetNext = millis() + 3000;
  kbBuf[0] = 0;
  if (WiFi.softAPIP() != apIpWanted()) startAp();
  else applyDns();
  ip_napt_enable_no(SOFTAP_IF, cfg.repeater);
  doneAt = millis();
  dirty = true;
}

static void startScan();

static void pollWifi() {
  uint32_t now = millis();
  wl_status_t st = WiFi.status();
  switch (cs) {
    case CS_CONNECTING:
      if (st == WL_CONNECTED) onConnected();
      else if (st == WL_CONNECT_FAILED && now - csSince > 1500) failConnect("Wrong password");
      else if (st == WL_NO_SSID_AVAIL && now - csSince > 8000) failConnect("Network not found");
      else if (now - csSince > 25000) failConnect("Timed out");
      break;
    case CS_CONNECTED:
      if (st == WL_CONNECTED) lastConnAt = now;
      if (st != WL_CONNECTED) {
        cs = CS_CONNECTING;
        csSince = now;
        pendSave = false;
        inetKnown = false;
        strlcpy(pendSsid, connectedSsid, sizeof(pendSsid));
        dirty = true;
      }
      break;
    default:
      if (haveSaved() && !autoScan && (int32_t)(now - nextRetry) >= 0 && screen != SC_CONNECT && screen != SC_KEYS) {
        // do not disturb someone using the setup page: only the first attempt scans with clients attached
        if (autoTried && WiFi.softAPgetStationNum() > 0) { nextRetry = now + 30000; break; }
        autoTried = true;
        autoScan = true;
        startScan();
      }
      break;
  }
}

// Picks the strongest visible saved network after a scan.
static void autoConnect() {
  int best = -1, bestR = -999;
  for (int i = 0; i < netCount; i++) {
    int si = savedFind(nets[i].ssid);
    if (si >= 0 && nets[i].rssi > bestR) { best = si; bestR = nets[i].rssi; }
  }
  if (best >= 0) staConnect(cfg.saved[best].ssid, cfg.saved[best].pass, false);
  else { failConnect("None in range"); nextRetry = millis() + 45000; }
}

static void startScan() {
  if (scanning) return;
  WiFi.scanDelete();
  WiFi.scanNetworks(true, true);
  scanning = true;
  scanStart = millis();
  dirty = true;
}

static void pollScan() {
  if (!scanning) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    if (millis() - scanStart > 15000) { scanning = false; WiFi.scanDelete(); dirty = true; if (autoScan) { autoScan = false; nextRetry = millis() + 30000; } }
    return;
  }
  scanning = false;
  netCount = 0;
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    int rssi = WiFi.RSSI(i);
    bool enc = WiFi.encryptionType(i) != ENC_TYPE_NONE;
    int dup = -1;
    for (int j = 0; j < netCount; j++) if (!strcmp(nets[j].ssid, s.c_str())) { dup = j; break; }
    if (dup >= 0) {
      if (rssi > nets[dup].rssi) { nets[dup].rssi = rssi; nets[dup].enc = enc; }
      continue;
    }
    if (netCount >= MAX_NETS) continue;
    strlcpy(nets[netCount].ssid, s.c_str(), sizeof(nets[netCount].ssid));
    nets[netCount].rssi = rssi;
    nets[netCount].enc = enc;
    netCount++;
  }
  WiFi.scanDelete();
  for (int a = 0; a < netCount; a++)
    for (int b = a + 1; b < netCount; b++)
      if (nets[b].rssi > nets[a].rssi) { Net t = nets[a]; nets[a] = nets[b]; nets[b] = t; }
  netL.sel = 0;
  netL.top = 0;
  dirty = true;
  if (autoScan) { autoScan = false; autoConnect(); }
}

// ===================================================================
//  NeoPixel status LED
// ===================================================================
struct RGB { uint8_t r, g, b; };
static Adafruit_NeoPixel *pix = nullptr;
static uint32_t ledLast = 0, ledShown = 0xFFFFFFFFu;
static char ledLabelBuf[28];

static void ledInit() {
  if (pix) { pix->clear(); pix->show(); delete pix; pix = nullptr; }
  if (cfg.pin[P_LED] < 0) return;
  pix = new Adafruit_NeoPixel(cfg.ledCount, cfg.pin[P_LED], NEO_GRB + NEO_KHZ800);
  pix->begin();
  pix->clear();
  pix->show();
  ledShown = 0xFFFFFFFFu;
}

static uint8_t tri(uint32_t period) {  // 0..255..0 triangle wave
  uint32_t t = millis() % period, h = period / 2;
  return (uint8_t)((t < h ? t : period - t) * 255 / h);
}

static RGB scale(RGB c, uint8_t v) { return RGB{(uint8_t)(c.r * v / 255), (uint8_t)(c.g * v / 255), (uint8_t)(c.b * v / 255)}; }

static RGB ledConnection(const char **label) {
  switch (cs) {
    case CS_CONNECTED:
      if (inetKnown && !inetOk) { *label = "Connected, no internet"; return RGB{255, 90, 0}; }
      if (cfg.repeater) { *label = "Connected, repeating"; return RGB{0, 200, 40}; }
      *label = "Connected, repeater off";
      return RGB{0, 70, 255};
    case CS_CONNECTING:
      *label = "Connecting";
      return scale(RGB{255, 140, 0}, 40 + tri(1200) * 215 / 255);
    case CS_FAILED:
      *label = "Connection failed";
      return scale(RGB{255, 0, 0}, 60 + tri(1600) * 195 / 255);
    default:
      if (haveSaved()) { *label = "Not connected"; return scale(RGB{255, 0, 0}, 40 + tri(2400) * 100 / 255); }
      *label = "Not set up";
      return scale(RGB{130, 0, 255}, 30 + tri(3000) * 150 / 255);
  }
}

static RGB ledBattery(const char **label) {
  if (!batPresent) { *label = "No battery"; return RGB{0, 0, 0}; }
  snprintf(ledLabelBuf, sizeof(ledLabelBuf), "Battery %u%%", (unsigned)batPct);
  *label = ledLabelBuf;
  if (batPct >= 60) return RGB{0, 200, 40};
  if (batPct >= 30) return RGB{255, 150, 0};
  if (batPct >= 15) return RGB{255, 60, 0};
  return ((millis() / 300) % 2) ? RGB{255, 0, 0} : RGB{0, 0, 0};
}

static RGB ledPing(const char **label) {
  if (cs != CS_CONNECTED) return ledConnection(label);
  if (!inetKnown) { *label = "Checking internet"; return RGB{0, 40, 120}; }
  if (!inetOk) { *label = "No internet"; return scale(RGB{255, 0, 0}, 60 + tri(800) * 195 / 255); }
  if (!dnsOk) { *label = "DNS problem"; return RGB{130, 0, 255}; }
  snprintf(ledLabelBuf, sizeof(ledLabelBuf), "Ping %u ms", (unsigned)pingMs);
  *label = ledLabelBuf;
  if (pingMs < 50) return RGB{0, 200, 40};
  if (pingMs < 100) return RGB{120, 200, 0};
  if (pingMs < 200) return RGB{255, 150, 0};
  return RGB{255, 60, 0};
}

static RGB ledCurrent(const char **label) {
  const char *dummy;
  if (!label) label = &dummy;
  switch (cfg.ledMode) {
    case 1: return ledConnection(label);
    case 2: return ledBattery(label);
    case 4: return ledPing(label);
    case 3: return ((millis() / 1000) % 4 < 3) ? ledConnection(label) : ledBattery(label);
    default: *label = "LED off"; return RGB{0, 0, 0};
  }
}

static void ledUpdate() {
  if (!pix) return;
  uint32_t now = millis();
  if (now - ledLast < 40) return;
  ledLast = now;
  RGB c = ledCurrent(nullptr);
  uint8_t br = cfg.ledBright;
  uint32_t col = pix->Color(c.r * br / 100, c.g * br / 100, c.b * br / 100);
  if (col == ledShown) return;
  ledShown = col;
  for (int i = 0; i < cfg.ledCount; i++) pix->setPixelColor(i, col);
  pix->show();
}

// Onboard LED as a ping indicator: 1 blink under 50 ms, 2 under 120 ms, 3 slower,
// one long blink for a DNS problem, rapid flicker when there is no internet.
static void builtinLedUpdate() {
  static bool on = false;
  bool want = false;
  if (cfg.pingLed && cs == CS_CONNECTED && inetKnown) {
    uint32_t t = millis() % 3000;
    if (!inetOk) want = (millis() / 125) % 2 == 0;
    else if (!dnsOk) want = t < 600;
    else {
      int n = pingMs < 50 ? 1 : (pingMs < 120 ? 2 : 3);
      want = (t < (uint32_t)n * 300) && ((t % 300) < 120);
    }
  }
  if (want == on) return;
  on = want;
  builtinActive = on;
  builtinChangedAt = millis();
  bool shared = false;
  for (int i = 0; i < B_COUNT; i++) if (cfg.pin[i] == LED_BUILTIN) shared = true;
  if (on) { pinMode(LED_BUILTIN, OUTPUT); digitalWrite(LED_BUILTIN, LOW); }
  else if (shared) pinMode(LED_BUILTIN, INPUT_PULLUP);
  else { pinMode(LED_BUILTIN, OUTPUT); digitalWrite(LED_BUILTIN, HIGH); }
}

// ===================================================================
//  Screens: drawing
// ===================================================================
static void toast(const char *m) {
  strlcpy(toastMsg, m, sizeof(toastMsg));
  toastUntil = millis() + 1500;
  dirty = true;
}

static void showMessage(const char *a, const char *b) {
  strlcpy(msg1, a, sizeof(msg1));
  strlcpy(msg2, b, sizeof(msg2));
  screen = SC_MESSAGE;
  dirty = true;
}

static void drawSplash() {
  oled->drawDisc(64, 30, 2);
  const int r[3] = {8, 14, 20};
  for (int i = 0; i < 3; i++) oled->drawCircle(64, 30, r[i], U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
  oled->setFont(F_BIG);
  oled->drawStr((128 - oled->getStrWidth("Repeater")) / 2, 51, "Repeater");
  oled->setFont(F_SM);
  char v[24];
  snprintf(v, sizeof(v), "D1 mini  v%s", FW_VERSION);
  oled->drawStr((128 - oled->getStrWidth(v)) / 2, 62, v);
}

static void drawArrow(int x0, int x1, int y, bool live) {
  if (live) {
    int off = (millis() / 110) % 6;
    for (int x = x0 - 6 + off; x < x1 - 4; x += 6) {
      int a = x < x0 ? x0 : x;
      int e = x + 3 > x1 - 4 ? x1 - 4 : x + 3;
      if (e > a) oled->drawHLine(a, y, e - a);
    }
    oled->drawLine(x1 - 4, y - 2, x1, y);
    oled->drawLine(x1 - 4, y + 2, x1, y);
  } else {
    for (int x = x0; x <= x1; x += 4) oled->drawPixel(x, y);
  }
}

static void centerText(int cx, int y, const char *s) {
  oled->drawStr(cx - oled->getStrWidth(s) / 2, y, s);
}

static void drawHome() {
  bool conn = (cs == CS_CONNECTED);
  bool live = conn && cfg.repeater;

  // top strip: link quality on the left, battery on the right
  oled->setFont(F_SM);
  bars(2, 8, conn ? rssiLevel(WiFi.RSSI()) : 0);
  char b[40];
  if (conn) snprintf(b, sizeof(b), "ch%d %ddBm", (int)WiFi.channel(), (int)WiFi.RSSI());
  else strcpy(b, (cs == CS_CONNECTING || autoScan) ? "searching" : "no uplink");
  oled->drawStr(20, 8, b);
  drawBattery(125);
  oled->drawHLine(0, 11, 128);

  // upstream network and address
  const char *shown = conn ? connectedSsid : ((cs != CS_IDLE && pendSsid[0]) ? pendSsid : cfg.saved[0].ssid);
  bool showNet = conn && inetKnown;
  oled->setFont(F_BIG);
  fit(shown[0] ? shown : "No network", b, sizeof(b), showNet ? 88 : 124);
  oled->drawStr(2, 25, b);

  // top-right corner: internet latency (alternating with the last speed test), live traffic under it
  if (conn) {
    char t[12], r[10];
    if (showNet) {
      const uint8_t *ic = IC_GLOBE;
      if (inetOk) {
        bool showSpeed = speedState == 2 && speedKbps > 0 && ((millis() / 3000) % 2);
        if (showSpeed) snprintf(t, sizeof(t), "%u.%uM", (unsigned)(speedKbps / 1000), (unsigned)((speedKbps % 1000) / 100));
        else if (!dnsOk) strcpy(t, "DNS!");
        else snprintf(t, sizeof(t), "%ums", (unsigned)pingMs);
      } else {
        ic = IC_X;
        strcpy(t, "none");
      }
      oled->setFont(F_SM);
      oled->drawXBMP(91, 16, 8, 8, ic);
      oled->drawStr(127 - oled->getStrWidth(t), 23, t);
    }
    oled->setFont(F_SM);
    for (int i = 0; i < 2; i++) {
      fmtRate(r, sizeof(r), i ? ulKbps : dlKbps);
      int by = 33 + i * 8;
      if (i == 0) { oled->drawHLine(93, by - 5, 5); oled->drawHLine(94, by - 4, 3); oled->drawPixel(95, by - 3); }
      else { oled->drawPixel(95, by - 5); oled->drawHLine(94, by - 4, 3); oled->drawHLine(93, by - 3, 5); }
      oled->drawStr(127 - oled->getStrWidth(r), by, r);
    }
  }

  oled->setFont(F_UI);
  char l2[32];
  if (conn) strlcpy(l2, WiFi.localIP().toString().c_str(), sizeof(l2));
  else if (cs == CS_CONNECTING) snprintf(l2, sizeof(l2), "Connecting%.*s", (int)((millis() / 400) % 4), "...");
  else if (cs == CS_FAILED) strlcpy(l2, failReason, sizeof(l2));
  else if (autoScan) strlcpy(l2, "Searching saved", sizeof(l2));
  else strlcpy(l2, haveSaved() ? "Disconnected" : "OK > Networks", sizeof(l2));
  oled->drawStr(2, 37, l2);

  // traffic path: uplink -> repeater -> clients
  oled->drawFrame(4, 48, 24, 6);
  oled->drawLine(8, 48, 5, 42);
  oled->drawLine(24, 48, 27, 42);
  oled->drawPixel(8, 51);
  if (conn) oled->drawPixel(11, 51);
  oled->setFont(F_SM);
  centerText(16, 62, conn ? ((inetKnown && !inetOk) ? "NO NET" : "UPLINK") : "NO LINK");

  oled->drawFrame(56, 42, 16, 11);
  for (int i = 0; i < 3; i++) { oled->drawHLine(53, 44 + i * 3, 3); oled->drawHLine(72, 44 + i * 3, 3); }
  if (cfg.repeater) oled->drawBox(60, 46, 8, 3);
  else { oled->drawLine(60, 44, 67, 50); oled->drawLine(67, 44, 60, 50); }
  centerText(64, 62, cfg.repeater ? "REPEATER" : "PAUSED");

  int n = WiFi.softAPgetStationNum();
  char c[8];
  snprintf(c, sizeof(c), "%d", n);
  oled->setFont(F_BIG);
  centerText(108, 55, c);
  oled->setFont(F_SM);
  centerText(108, 62, n == 1 ? "CLIENT" : "CLIENTS");

  drawArrow(33, 51, 47, live);
  drawArrow(77, 93, 47, live && n > 0);
}

#define MENU_N 9
static const uint8_t *const MENU_ICON[MENU_N] = {IC_WIFI, IC_STAR, IC_POWER, IC_GLOBE, IC_SUN, IC_LED, IC_BOLT, IC_INFO, IC_REFRESH};

static void menuItem(int i, char *l, char *v) {
  v[0] = 0;
  switch (i) {
    case 0: strcpy(l, "Networks"); break;
    case 1: strcpy(l, "Saved"); snprintf(v, 8, "%d", savedN()); break;
    case 2: strcpy(l, "Repeater"); strcpy(v, cfg.repeater ? "ON" : "OFF"); break;
    case 3: strcpy(l, "DNS"); strlcpy(v, DNS_SHORT[cfg.dnsMode], 8); break;
    case 4: strcpy(l, "Display"); break;
    case 5: strcpy(l, "LED"); strcpy(v, LED_MODE_SHORT[cfg.ledMode]); break;
    case 6: strcpy(l, "Speed test"); break;
    case 7: strcpy(l, "Info"); break;
    default: strcpy(l, "Restart"); break;
  }
}

static void drawMenu() {
  header("Menu");
  for (int r = 0; r < 4; r++) {
    int i = menuL.top + r;
    if (i >= MENU_N) break;
    int y = 14 + r * 12;
    char l[24], v[8];
    menuItem(i, l, v);
    rowBegin(y, i == menuL.sel);
    oled->drawXBMP(3, y + 2, 8, 8, MENU_ICON[i]);
    oled->setFont(F_UI);
    oled->drawStr(16, y + 10, l);
    if (v[0]) oled->drawStr(121 - oled->getStrWidth(v), y + 10, v);
    oled->setDrawColor(1);
  }
  scrollbar(MENU_N, menuL.top, 4);
}

static void drawNets() {
  header("Networks");
  if (scanning && netCount == 0) {
    oled->setFont(F_UI);
    char t[16];
    snprintf(t, sizeof(t), "Scanning%.*s", (int)((millis() / 350) % 4), "...");
    oled->drawStr(36, 40, t);
    return;
  }
  int n = netCount + 1;
  for (int r = 0; r < 4; r++) {
    int i = netL.top + r;
    if (i >= n) break;
    int y = 14 + r * 12;
    rowBegin(y, i == netL.sel);
    oled->setFont(F_UI);
    if (i < netCount) {
      bool cur = (cs == CS_CONNECTED && !strcmp(nets[i].ssid, connectedSsid));
      if (cur) drawTick(2, y + 3);
      else if (savedFind(nets[i].ssid) >= 0) oled->drawXBMP(2, y + 2, 8, 8, IC_STAR);
      else if (nets[i].enc) drawLock(3, y + 3);
      char buf[40];
      fit(nets[i].ssid, buf, sizeof(buf), 68);
      oled->drawStr(12, y + 10, buf);
      char d[8];
      snprintf(d, sizeof(d), "%d", (int)nets[i].rssi);
      oled->setFont(F_SM);
      oled->drawStr(104 - oled->getStrWidth(d), y + 9, d);
      bars(107, y + 10, rssiLevel(nets[i].rssi));
    } else {
      char t[28];
      if (scanning) snprintf(t, sizeof(t), "Scanning%.*s", (int)((millis() / 350) % 4), "...");
      else snprintf(t, sizeof(t), "Rescan  (%d found)", netCount);
      oled->drawStr(5, y + 10, t);
    }
    oled->setDrawColor(1);
  }
  scrollbar(n, netL.top, 4);
}

static const char *const KB[4][3] = {
  {"qwertyuiop", "asdfghjkl@", "zxcvbnm._-"},
  {"QWERTYUIOP", "ASDFGHJKL@", "ZXCVBNM._-"},
  {"1234567890", "!@#$%^&*()", "-_=+.,/?:;"},
  {"~`'\"\\|<>{}", "[]()!?:;,.", "@#$%^&*-+="}
};
static const char *const KB_PAGE[4] = {"abc", "ABC", "123", "#+="};

static void drawKeys() {
  oled->drawFrame(0, 0, 128, 13);
  int len = strlen(kbBuf);
  if (len == 0) {
    char t[48], f[40];
    oled->setFont(F_SM);
    fit(kbSsid, f, sizeof(f), 70);
    snprintf(t, sizeof(t), "Password for %s", f);
    oled->drawStr(4, 9, t);
  } else {
    oled->setFont(F_UI);
    char shown[24];
    int start = len > 16 ? len - 16 : 0, n = 0;
    for (int i = start; i < len; i++) shown[n++] = kbShow ? kbBuf[i] : '*';
    shown[n] = 0;
    oled->drawStr(4, 10, shown);
    if ((millis() / 500) % 2 == 0) oled->drawVLine(5 + oled->getStrWidth(shown), 3, 8);
    char cnt[8];
    snprintf(cnt, sizeof(cnt), "%d", len);
    oled->setFont(F_SM);
    oled->drawStr(125 - oled->getStrWidth(cnt), 9, cnt);
  }
  for (int r = 0; r < 3; r++) {
    for (int c = 0; c < 10; c++) {
      int x = 4 + c * 12, y = 16 + r * 11;
      bool sel = (kbR == r && kbC == c);
      char s[2] = {KB[kbPage][r][c], 0};
      if (sel) { oled->drawBox(x, y, 12, 11); oled->setDrawColor(0); }
      oled->setFont(F_UI);
      oled->drawStr(x + 3, y + 8, s);
      oled->setDrawColor(1);
    }
  }
  const char *lab[5] = {KB_PAGE[kbPage], "SPC", "DEL", kbShow ? "HIDE" : "SHOW", "OK"};
  for (int f = 0; f < 5; f++) {
    int x = 4 + f * 24;
    bool sel = (kbR == 3 && kbC == f);
    if (sel) { oled->drawBox(x, 49, 23, 11); oled->setDrawColor(0); }
    else oled->drawFrame(x, 49, 23, 11);
    oled->setFont(F_SM);
    oled->drawStr(x + (23 - oled->getStrWidth(lab[f])) / 2, 57, lab[f]);
    oled->setDrawColor(1);
  }
}

static void drawConnect() {
  char buf[40];
  if (cs == CS_CONNECTING) {
    header("Connecting");
    oled->setFont(F_BIG);
    fit(pendSsid, buf, sizeof(buf), 120);
    oled->drawStr(4, 32, buf);
    oled->drawFrame(4, 40, 120, 8);
    const int w = 36;
    int pos = (int)((millis() / 12) % (112 + w)) - w;
    int x0 = pos < 0 ? 0 : pos, x1 = pos + w > 112 ? 112 : pos + w;
    if (x1 > x0) oled->drawBox(8 + x0, 42, x1 - x0, 4);
    oled->setFont(F_SM);
    oled->drawStr(4, 61, "BACK to cancel");
  } else if (cs == CS_FAILED) {
    header("Failed");
    oled->setFont(F_BIG);
    fit(pendSsid, buf, sizeof(buf), 120);
    oled->drawStr(4, 30, buf);
    oled->setFont(F_UI);
    oled->drawStr(4, 44, failReason);
    oled->setFont(F_SM);
    oled->drawStr(4, 61, "OK retry        BACK exit");
  } else {
    header("Connected");
    drawTick(4, 19);
    oled->setFont(F_BIG);
    fit(connectedSsid, buf, sizeof(buf), 108);
    oled->drawStr(14, 30, buf);
    oled->setFont(F_UI);
    oled->drawStr(4, 45, WiFi.localIP().toString().c_str());
    oled->setFont(F_SM);
    oled->drawStr(4, 61, "Repeater is running");
  }
}

static void drawDisplay() {
  header("Display");
  for (int i = 0; i < 3; i++) {
    int y = 14 + i * 12;
    bool sel = (dispL.sel == i);
    rowBegin(y, sel);
    oled->setFont(F_UI);
    char v[16];
    if (i == 0) {
      oled->drawStr(5, y + 10, "Brightness");
      segBar(64, y + 3, cfg.brightness);
      snprintf(v, sizeof(v), "%d", cfg.brightness);
    } else if (i == 1) {
      oled->drawStr(5, y + 10, "Timeout");
      snprintf(v, sizeof(v), sel ? "< %s >" : "%s", TIMEOUT_LABELS[timeoutIdx()]);
    } else {
      oled->drawStr(5, y + 10, "Flip screen");
      snprintf(v, sizeof(v), sel ? "< %s >" : "%s", cfg.flip ? "On" : "Off");
    }
    oled->drawStr(121 - oled->getStrWidth(v), y + 10, v);
    oled->setDrawColor(1);
  }
  oled->setFont(F_SM);
  oled->drawStr(4, 61, "LEFT / RIGHT to adjust");
}

static void drawLed() {
  header("LED");
  for (int i = 0; i < 3; i++) {
    int y = 14 + i * 12;
    bool sel = (ledL.sel == i);
    rowBegin(y, sel);
    oled->setFont(F_UI);
    char v[20];
    if (i == 0) {
      oled->drawStr(5, y + 10, "Mode");
      snprintf(v, sizeof(v), sel ? "< %s >" : "%s", LED_MODE[cfg.ledMode]);
    } else if (i == 1) {
      oled->drawStr(5, y + 10, "Level");
      segBar(64, y + 3, cfg.ledBright);
      snprintf(v, sizeof(v), "%d", cfg.ledBright);
    } else {
      oled->drawStr(5, y + 10, "Onboard ping");
      snprintf(v, sizeof(v), sel ? "< %s >" : "%s", cfg.pingLed ? "On" : "Off");
    }
    oled->drawStr(121 - oled->getStrWidth(v), y + 10, v);
    oled->setDrawColor(1);
  }
  oled->drawHLine(0, 51, 128);
  if (cfg.pin[P_LED] < 0) {
    oled->setFont(F_SM);
    oled->drawStr(3, 62, "LED pin not set");
  } else {
    const char *lab;
    ledCurrent(&lab);
    char t[40];
    oled->setFont(F_UI);
    fit(lab, t, sizeof(t), 122);
    oled->drawStr(3, 62, t);
  }
}

#define INFO_N 13
static void infoItem(int i, char *l, char *v) {
  bool c = (cs == CS_CONNECTED);
  uint32_t s = millis() / 1000;
  switch (i) {
    case 0: strcpy(l, "Web UI"); snprintf(v, 24, "http://%s", WiFi.softAPIP().toString().c_str()); break;
    case 1: strcpy(l, "Upstream address"); strlcpy(v, c ? WiFi.localIP().toString().c_str() : "-", 24); break;
    case 2: strcpy(l, "Gateway"); strlcpy(v, c ? WiFi.gatewayIP().toString().c_str() : "-", 24); break;
    case 3:
      strcpy(l, "Channel and signal");
      if (c) snprintf(v, 24, "ch %d  %d dBm", (int)WiFi.channel(), (int)WiFi.RSSI()); else strcpy(v, "-");
      break;
    case 4:
      strcpy(l, "Internet");
      if (!c || !inetKnown) strcpy(v, "-");
      else if (!inetOk) strcpy(v, "No internet");
      else if (!dnsOk) strcpy(v, "DNS problem");
      else snprintf(v, 24, "Online  %u ms", (unsigned)pingMs);
      break;
    case 5: snprintf(l, 24, "DNS (%s)", DNS_NAME[cfg.dnsMode]); strlcpy(v, dnsEffective().toString().c_str(), 24); break;
    case 6:
      strcpy(l, "Last speed test");
      if (speedState == 2) snprintf(v, 24, "%u.%u Mbps", (unsigned)(speedKbps / 1000), (unsigned)((speedKbps % 1000) / 100));
      else strcpy(v, "Not run");
      break;
    case 7:
      strcpy(l, "Battery");
      if (batPresent) snprintf(v, 24, "%u.%02u V  %u%%", (unsigned)(batMv / 1000), (unsigned)((batMv % 1000) / 10), (unsigned)batPct);
      else strcpy(v, "Not detected");
      break;
    case 8: strcpy(l, "AP MAC"); strlcpy(v, WiFi.softAPmacAddress().c_str(), 24); break;
    case 9: strcpy(l, "Free memory"); snprintf(v, 24, "%u.%u KB", (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)((ESP.getFreeHeap() % 1024) * 10 / 1024)); break;
    case 10:
      strcpy(l, "Uptime");
      if (s >= 86400) snprintf(v, 24, "%ud %02uh %02um", (unsigned)(s / 86400), (unsigned)((s / 3600) % 24), (unsigned)((s / 60) % 60));
      else snprintf(v, 24, "%02u:%02u:%02u", (unsigned)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60));
      break;
    case 11: strcpy(l, "AP security"); strcpy(v, apIsOpen ? "Open (setup)" : "Password"); break;
    default: strcpy(l, "Firmware"); strlcpy(v, FW_VERSION, 24); break;
  }
}

static void drawInfo() {
  header("Info");
  for (int r = 0; r < 3; r++) {
    int i = infoL.top + r;
    if (i >= INFO_N) break;
    int y = 14 + r * 16;
    char l[24], v[24];
    infoItem(i, l, v);
    oled->setFont(F_SM);
    oled->drawStr(3, y + 6, l);
    oled->setFont(F_UI);
    oled->drawStr(3, y + 15, v);
  }
  scrollbar(INFO_N, infoL.top, 3);
}

static void drawConfirm() {
  oled->drawFrame(6, 8, 116, 50);
  oled->setFont(F_UI);
  const char *a = "No", *b = "Yes";
  if (confirmKind == CF_RESTART) {
    const char *q = "Restart device?";
    oled->drawStr((128 - oled->getStrWidth(q)) / 2, 24, q);
  } else {
    char buf[40];
    a = "Join";
    b = "Forget";
    fit(cfg.saved[confirmIdx].ssid, buf, sizeof(buf), 104);
    oled->drawStr((128 - oled->getStrWidth(buf)) / 2, 24, buf);
    oled->setFont(F_SM);
    const char *sub = "Saved network";
    oled->drawStr((128 - oled->getStrWidth(sub)) / 2, 33, sub);
  }
  for (int k = 0; k < 2; k++) {
    int x = k == 0 ? 18 : 70;
    bool sel = (k == 1) == confirmYes;
    if (sel) { oled->drawBox(x, 38, 40, 14); oled->setDrawColor(0); }
    else oled->drawFrame(x, 38, 40, 14);
    oled->setFont(F_UI);
    const char *t = k == 0 ? a : b;
    oled->drawStr(x + (40 - oled->getStrWidth(t)) / 2, 49, t);
    oled->setDrawColor(1);
  }
}

static void drawSaved() {
  header("Saved");
  int n = savedN();
  if (n == 0) {
    oled->setFont(F_UI);
    oled->drawStr(14, 36, "No saved networks");
    oled->setFont(F_SM);
    oled->drawStr(12, 48, "Join one from Networks");
    return;
  }
  for (int r = 0; r < 4; r++) {
    int i = savedL.top + r;
    if (i >= n) break;
    int y = 14 + r * 12;
    rowBegin(y, i == savedL.sel);
    bool cur = (cs == CS_CONNECTED && !strcmp(cfg.saved[i].ssid, connectedSsid));
    if (cur) drawTick(2, y + 3);
    else oled->drawXBMP(2, y + 2, 8, 8, IC_STAR);
    char buf[40];
    oled->setFont(F_UI);
    fit(cfg.saved[i].ssid, buf, sizeof(buf), 100);
    oled->drawStr(13, y + 10, buf);
    oled->setDrawColor(1);
  }
  scrollbar(n, savedL.top, 4);
}

static void drawSpeed() {
  header("Speed test");
  char b[24];
  if (speedState == 1 || speedState == 2) {
    uint32_t k = speedState == 1 ? speedLiveKbps : speedKbps;
    snprintf(b, sizeof(b), "%u.%u", (unsigned)(k / 1000), (unsigned)((k % 1000) / 100));
    oled->setFont(F_BIG);
    int w = oled->getStrWidth(b);
    oled->drawStr(4, 31, b);
    oled->setFont(F_SM);
    oled->drawStr(8 + w, 31, "Mbps down");
    if (speedState == 1) {
      oled->drawFrame(4, 38, 120, 7);
      oled->drawBox(6, 40, (116 * speedPct) / 100, 3);
      oled->drawStr(4, 61, "Measuring download...");
    } else {
      snprintf(b, sizeof(b), "Ping %u ms   DNS %u ms", (unsigned)pingMs, (unsigned)dnsMs);
      oled->drawStr(4, 46, b);
      oled->drawStr(4, 61, "OK to run again");
    }
  } else if (speedState == 3) {
    oled->setFont(F_UI);
    oled->drawStr(4, 30, "Test failed");
    oled->setFont(F_SM);
    oled->drawStr(4, 42, speedMsg);
    oled->drawStr(4, 61, "OK to retry");
  } else {
    oled->setFont(F_UI);
    oled->drawStr(4, 30, "Download speed");
    oled->setFont(F_SM);
    oled->drawStr(4, 42, "Uses up to about 6 MB");
    oled->drawStr(4, 61, "OK to start");
  }
}

static void drawMessage() {
  oled->setFont(F_BIG);
  oled->drawStr((128 - oled->getStrWidth(msg1)) / 2, 34, msg1);
  oled->setFont(F_SM);
  oled->drawStr((128 - oled->getStrWidth(msg2)) / 2, 46, msg2);
}

static void drawToast() {
  oled->setFont(F_UI);
  int w = oled->getStrWidth(toastMsg) + 14;
  int x = (128 - w) / 2;
  oled->setDrawColor(0);
  oled->drawBox(x - 2, 45, w + 4, 18);
  oled->setDrawColor(1);
  oled->drawBox(x, 47, w, 14);
  oled->setDrawColor(0);
  oled->drawStr(x + 7, 57, toastMsg);
  oled->setDrawColor(1);
}

static void uiDraw() {
  if (!oled || !screenOn) return;
  uint32_t now = millis();
  bool anim = (screen == SC_CONNECT && cs == CS_CONNECTING) || screen == SC_KEYS || (screen == SC_NETS && scanning) ||
              (screen == SC_HOME && (cs == CS_CONNECTING || (cs == CS_CONNECTED && cfg.repeater))) ||
              (screen == SC_LED && cfg.ledMode != 0) || (screen == SC_SPEED && speedState == 1);
  if (!dirty && now - lastDraw < (anim ? 90u : 400u)) return;
  dirty = false;
  lastDraw = now;
  oled->clearBuffer();
  oled->setDrawColor(1);
  switch (screen) {
    case SC_SPLASH:  drawSplash(); break;
    case SC_HOME:    drawHome(); break;
    case SC_MENU:    drawMenu(); break;
    case SC_NETS:    drawNets(); break;
    case SC_KEYS:    drawKeys(); break;
    case SC_CONNECT: drawConnect(); break;
    case SC_DISPLAY: drawDisplay(); break;
    case SC_LED:     drawLed(); break;
    case SC_INFO:    drawInfo(); break;
    case SC_SAVED:   drawSaved(); break;
    case SC_SPEED:   drawSpeed(); break;
    case SC_CONFIRM: drawConfirm(); break;
    case SC_MESSAGE: drawMessage(); break;
  }
  if (now < toastUntil) drawToast();
  oled->sendBuffer();
}

// ===================================================================
//  Screens: input
// ===================================================================
static void goHome() { screen = SC_HOME; dirty = true; }

static void openNets() {
  screen = SC_NETS;
  netL.sel = 0;
  netL.top = 0;
  if (!scanning) startScan();
}

static void kbOpen(const char *ssid) {
  strlcpy(kbSsid, ssid, sizeof(kbSsid));
  kbBuf[0] = 0;
  kbShow = false;
  kbPage = 0;
  kbR = 0;
  kbC = 0;
  screen = SC_KEYS;
}

static void kbMove(int dr, int dc) {
  if (dc) {
    int cols = (kbR == 3) ? 5 : 10;
    kbC = (kbC + dc + cols) % cols;
  }
  if (dr) {
    int nr = (kbR + dr + 4) % 4;
    if (kbR == 3 && nr < 3) kbC = kbC * 2;
    else if (kbR < 3 && nr == 3) kbC = kbC / 2;
    kbR = nr;
  }
}

static void kbAppend(char ch) {
  size_t l = strlen(kbBuf);
  if (l < 63) { kbBuf[l] = ch; kbBuf[l + 1] = 0; }
}

static void kbBackspace() {
  size_t l = strlen(kbBuf);
  if (l) kbBuf[l - 1] = 0;
}

static void kbPress() {
  if (kbR < 3) { kbAppend(KB[kbPage][kbR][kbC]); return; }
  switch (kbC) {
    case 0: kbPage = (kbPage + 1) % 4; break;
    case 1: kbAppend(' '); break;
    case 2: kbBackspace(); break;
    case 3: kbShow = !kbShow; break;
    default:
      if (strlen(kbBuf) < 8) toast("Min. 8 characters");
      else { staConnect(kbSsid, kbBuf, true); screen = SC_CONNECT; }
      break;
  }
}

static void doConfirm() {
  if (confirmKind == CF_SAVED) {
    if (confirmIdx < 0 || confirmIdx >= MAX_SAVED || !cfg.saved[confirmIdx].ssid[0]) { screen = SC_SAVED; return; }
    if (confirmYes) {
      savedRemove(confirmIdx);
      cfgSave();
      savedL.sel = 0;
      savedL.top = 0;
      screen = SC_SAVED;
      toast("Network forgotten");
    } else {
      staConnect(cfg.saved[confirmIdx].ssid, cfg.saved[confirmIdx].pass, false);
      screen = SC_CONNECT;
    }
  } else {
    showMessage("Restarting", "Please wait");
    rebootAt = millis() + 700;
  }
}

static void dnsCycle(int d) {
  int n = customSet() ? 6 : 5;
  cfg.dnsMode = (uint8_t)((cfg.dnsMode + d + n) % n);
  cfgSave();
  applyDns();
  dirty = true;
}

static void leaveSettings(Screen to) {
  if (cfgDirty) { cfgSave(); cfgDirty = false; }
  screen = to;
}

#define P(b) evPress[b]

static void uiHandle() {
  bool any = false;
  for (int i = 0; i < B_COUNT; i++) if (evPress[i] || evLong[i]) any = true;
  if (!any) return;
  lastActivity = millis();
  if (!screenOn) { screenWake(); return; }
  dirty = true;
  if (screen == SC_SPLASH) { goHome(); return; }
  if (screen == SC_MESSAGE) return;

  // Long BACK jumps home from anywhere except the keyboard (where it cancels).
  if (evLong[B_BACK] && screen != SC_KEYS) {
    if (screen == SC_CONNECT && cs == CS_CONNECTING) cancelConnect();
    leaveSettings(SC_HOME);
    return;
  }

  switch (screen) {
    case SC_HOME:
      if (P(B_OK)) screen = SC_MENU;
      break;

    case SC_MENU:
      if (P(B_UP)) listNav(menuL, MENU_N, -1, 4);
      if (P(B_DOWN)) listNav(menuL, MENU_N, 1, 4);
      if (P(B_LEFT) || P(B_RIGHT)) {
        if (menuL.sel == 2) setRepeater(!cfg.repeater);
        else if (menuL.sel == 3) dnsCycle(P(B_RIGHT) ? 1 : -1);
      }
      if (P(B_BACK)) goHome();
      if (P(B_OK)) {
        switch (menuL.sel) {
          case 0: openNets(); break;
          case 1: screen = SC_SAVED; savedL.sel = 0; savedL.top = 0; break;
          case 2: setRepeater(!cfg.repeater); break;
          case 3: dnsCycle(1); break;
          case 4: screen = SC_DISPLAY; dispL.sel = 0; dispL.top = 0; break;
          case 5: screen = SC_LED; ledL.sel = 0; ledL.top = 0; break;
          case 6: screen = SC_SPEED; break;
          case 7: screen = SC_INFO; infoL.sel = 0; infoL.top = 0; break;
          default: confirmKind = CF_RESTART; confirmYes = false; screen = SC_CONFIRM; break;
        }
      }
      break;

    case SC_NETS: {
      int n = netCount + 1;
      if (P(B_UP)) listNav(netL, n, -1, 4);
      if (P(B_DOWN)) listNav(netL, n, 1, 4);
      if (P(B_BACK)) screen = SC_MENU;
      if (P(B_OK)) {
        if (netL.sel >= netCount) startScan();
        else {
          int si = savedFind(nets[netL.sel].ssid);
          if (si >= 0) { staConnect(cfg.saved[si].ssid, cfg.saved[si].pass, false); screen = SC_CONNECT; }
          else if (nets[netL.sel].enc) kbOpen(nets[netL.sel].ssid);
          else { staConnect(nets[netL.sel].ssid, "", true); screen = SC_CONNECT; }
        }
      }
      break;
    }

    case SC_KEYS:
      if (evLong[B_BACK]) { kbBuf[0] = 0; screen = SC_NETS; }
      else if (P(B_BACK)) { if (kbBuf[0]) kbBackspace(); else screen = SC_NETS; }
      else {
        if (P(B_UP)) kbMove(-1, 0);
        if (P(B_DOWN)) kbMove(1, 0);
        if (P(B_LEFT)) kbMove(0, -1);
        if (P(B_RIGHT)) kbMove(0, 1);
        if (P(B_OK)) kbPress();
      }
      break;

    case SC_CONNECT:
      if (cs == CS_CONNECTING) {
        if (P(B_BACK)) { cancelConnect(); screen = SC_NETS; }
      } else if (cs == CS_FAILED) {
        if (P(B_OK)) {
          if (pendPass[0]) kbOpen(pendSsid);
          else staConnect(pendSsid, "", true);
        }
        if (P(B_BACK)) screen = SC_NETS;
      } else if (P(B_OK) || P(B_BACK)) {
        goHome();
      }
      break;

    case SC_DISPLAY:
      if (P(B_UP)) listNav(dispL, 3, -1, 3);
      if (P(B_DOWN)) listNav(dispL, 3, 1, 3);
      if (P(B_LEFT) || P(B_RIGHT)) {
        int d = P(B_RIGHT) ? 1 : -1;
        if (dispL.sel == 0) {
          cfg.brightness = (uint8_t)constrain((int)cfg.brightness + d * 5, 5, 100);
          applyBrightness();
        } else if (dispL.sel == 1) {
          cfg.timeoutSec = TIMEOUTS[constrain(timeoutIdx() + d, 0, N_TIMEOUTS - 1)];
        } else {
          cfg.flip = !cfg.flip;
          if (oled) oled->setFlipMode(cfg.flip ? 1 : 0);
        }
        cfgDirty = true;
      }
      if (P(B_BACK)) leaveSettings(SC_MENU);
      break;

    case SC_LED:
      if (P(B_UP)) listNav(ledL, 3, -1, 3);
      if (P(B_DOWN)) listNav(ledL, 3, 1, 3);
      if (P(B_LEFT) || P(B_RIGHT)) {
        int d = P(B_RIGHT) ? 1 : -1;
        if (ledL.sel == 0) cfg.ledMode = (uint8_t)constrain((int)cfg.ledMode + d, 0, 4);
        else if (ledL.sel == 1) cfg.ledBright = (uint8_t)constrain((int)cfg.ledBright + d * 5, 5, 100);
        else cfg.pingLed = !cfg.pingLed;
        ledShown = 0xFFFFFFFFu;
        cfgDirty = true;
      }
      if (P(B_BACK)) leaveSettings(SC_MENU);
      break;

    case SC_SAVED: {
      int n = savedN();
      if (P(B_UP)) listNav(savedL, n, -1, 4);
      if (P(B_DOWN)) listNav(savedL, n, 1, 4);
      if (P(B_BACK)) screen = SC_MENU;
      if (P(B_OK) && n > 0) { confirmKind = CF_SAVED; confirmIdx = savedL.sel; confirmYes = false; screen = SC_CONFIRM; }
      break;
    }

    case SC_SPEED:
      if (P(B_BACK)) screen = SC_MENU;
      if (P(B_OK)) { if (cs == CS_CONNECTED) speedPending = true; else toast("Not connected"); }
      break;

    case SC_INFO:
      if (P(B_UP) && infoL.top > 0) infoL.top--;
      if (P(B_DOWN) && infoL.top < INFO_N - 3) infoL.top++;
      if (P(B_BACK)) screen = SC_MENU;
      break;

    case SC_CONFIRM:
      if (P(B_LEFT) || P(B_RIGHT) || P(B_UP) || P(B_DOWN)) confirmYes = !confirmYes;
      if (P(B_BACK)) screen = (confirmKind == CF_SAVED) ? SC_SAVED : SC_MENU;
      if (P(B_OK)) { if (confirmKind == CF_SAVED || confirmYes) doConfirm(); else screen = SC_MENU; }
      break;

    default: break;
  }
}

static void uiTick() {
  uint32_t now = millis();
  if (screen == SC_SPLASH && (int32_t)(now - splashUntil) >= 0) goHome();
  if (screen == SC_CONNECT && cs == CS_CONNECTED && doneAt && now - doneAt > 2500) { doneAt = 0; goHome(); }
  if (screenOn && cfg.timeoutSec && screen != SC_CONNECT && screen != SC_MESSAGE &&
      now - lastActivity > (uint32_t)cfg.timeoutSec * 1000UL) {
    if (cfgDirty) { cfgSave(); cfgDirty = false; }
    kbBuf[0] = 0;
    screen = SC_HOME;
    screenSleep();
  }
}

// ===================================================================
//  Web server
// ===================================================================
static ESP8266WebServer server(80);

// Only clients on the repeater's own network may use the web UI.
static bool guard() {
  IPAddress r = server.client().remoteIP();
  IPAddress a = WiFi.softAPIP();
  if (r[0] == a[0] && r[1] == a[1] && r[2] == a[2]) return true;
  server.send(403, "text/plain", "Open this page from the repeater's own network.");
  return false;
}

static void sendJson(const String &s) {
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

static void sendErr(const char *e) {
  server.send(400, "application/json", String("{\"ok\":false,\"error\":\"") + e + "\"}");
}

static void jStr(String &o, const char *k, const char *v) {
  o += '"';
  o += k;
  o += "\":\"";
  for (const char *p = v; *p; p++) {
    char c = *p;
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  o += '"';
}

static const char *csName() {
  switch (cs) {
    case CS_CONNECTED: return "connected";
    case CS_CONNECTING: return "connecting";
    case CS_FAILED: return "failed";
    default: return "idle";
  }
}

static void hStatus() {
  if (!guard()) return;
  bool c = (cs == CS_CONNECTED);
  const char *shown = c ? connectedSsid : ((cs != CS_IDLE && pendSsid[0]) ? pendSsid : cfg.saved[0].ssid);
  const char *ledLab = "LED off";
  ledCurrent(&ledLab);
  String o;
  o.reserve(1100);
  o += '{';
  jStr(o, "fw", FW_VERSION); o += ',';
  jStr(o, "state", csName()); o += ',';
  jStr(o, "reason", failReason); o += ',';
  jStr(o, "ssid", shown); o += ',';
  jStr(o, "ip", c ? WiFi.localIP().toString().c_str() : ""); o += ',';
  jStr(o, "gw", c ? WiFi.gatewayIP().toString().c_str() : ""); o += ',';
  jStr(o, "apSsid", cfg.apSsid); o += ',';
  jStr(o, "apIp", WiFi.softAPIP().toString().c_str()); o += ',';
  jStr(o, "led", ledLab); o += ','; jStr(o, "dnsName", DNS_NAME[cfg.dnsMode]); o += ','; jStr(o, "dnsIp", dnsEffective().toString().c_str()); o += ','; jStr(o, "speedMsg", speedMsg);
  o += ",\"rssi\":"; o += c ? WiFi.RSSI() : 0;
  o += ",\"channel\":"; o += c ? WiFi.channel() : 0;
  o += ",\"clients\":"; o += (int)WiFi.softAPgetStationNum();
  o += ",\"repeater\":"; o += cfg.repeater ? "true" : "false";
  o += ",\"apOpen\":"; o += apIsOpen ? "true" : "false";
  o += ",\"savedCount\":"; o += savedN();
  o += ",\"inet\":\""; o += (!c || !inetKnown) ? "unknown" : (!inetOk ? "offline" : (dnsOk ? "online" : "nodns")); o += '"';
  o += ",\"ping\":"; o += (int)pingMs;
  o += ",\"dnsMs\":"; o += (int)dnsMs;
  o += ",\"speedState\":"; o += (int)speedState;
  o += ",\"speedKbps\":"; o += (uint32_t)speedKbps;
  o += ",\"dlKbps\":"; o += (uint32_t)dlKbps;
  o += ",\"ulKbps\":"; o += (uint32_t)ulKbps;
  o += ",\"batPresent\":"; o += batPresent ? "true" : "false";
  o += ",\"batMv\":"; o += (int)batMv;
  o += ",\"batPct\":"; o += (int)batPct;
  o += ",\"adcRaw\":"; o += (int)batRaw;
  o += ",\"heap\":"; o += ESP.getFreeHeap();
  o += ",\"uptime\":"; o += (uint32_t)(millis() / 1000);
  o += '}';
  sendJson(o);
}

static void hClients() {
  if (!guard()) return;
  String o;
  o.reserve(300);
  o += "{\"list\":[";
  struct station_info *si = wifi_softap_get_station_info();
  bool first = true;
  while (si) {
    char mac[18];
    snprintf(mac, sizeof(mac), "%02x:%02x:%02x:%02x:%02x:%02x", si->bssid[0], si->bssid[1], si->bssid[2], si->bssid[3], si->bssid[4], si->bssid[5]);
    IPAddress ip(si->ip.addr);
    if (!first) o += ',';
    first = false;
    o += "{\"mac\":\""; o += mac; o += "\",\"ip\":\""; o += ip.toString(); o += "\"}";
    si = STAILQ_NEXT(si, next);
  }
  wifi_softap_free_station_info();
  o += "]}";
  sendJson(o);
}

static void hScreen() {
  if (!guard()) return;
  String o;
  o.reserve(2100);
  o += "{\"on\":";
  o += screenOn ? "true" : "false";
  o += ",\"buf\":\"";
  if (oled) {
    static const char H[] = "0123456789abcdef";
    const uint8_t *b = oled->getBufferPtr();
    for (int i = 0; i < 1024; i++) { o += H[b[i] >> 4]; o += H[b[i] & 15]; }
  }
  o += "\"}";
  sendJson(o);
}

static void hNetworks() {
  if (!guard()) return;
  String o;
  o.reserve(1600);
  o += "{\"scanning\":";
  o += scanning ? "true" : "false";
  o += ",\"list\":[";
  for (int i = 0; i < netCount; i++) {
    if (i) o += ',';
    o += '{';
    jStr(o, "ssid", nets[i].ssid);
    o += ",\"rssi\":"; o += (int)nets[i].rssi;
    o += ",\"enc\":"; o += nets[i].enc ? "true" : "false";
    o += '}';
  }
  o += "]}";
  sendJson(o);
}

static void hScan() {
  if (!guard()) return;
  startScan();
  sendJson("{\"ok\":true}");
}

static void hConnect() {
  if (!guard()) return;
  String s = server.arg("ssid"), p = server.arg("pass");
  if (s.length() < 1 || s.length() > 32) { sendErr("Network name must be 1-32 characters"); return; }
  int si = savedFind(s.c_str());
  if (!p.length() && si >= 0) p = cfg.saved[si].pass;  // use the stored password
  if (p.length() && (p.length() < 8 || p.length() > 63)) { sendErr("Password must be 8-63 characters"); return; }
  staConnect(s.c_str(), p.c_str(), true);
  sendJson("{\"ok\":true}");
}

static void hForget() {
  if (!guard()) return;
  if (server.arg("all") == "1") {
    memset(cfg.saved, 0, sizeof(cfg.saved));
  } else {
    int i = savedFind(server.arg("ssid").c_str());
    if (i < 0) { sendErr("Not a saved network"); return; }
    savedRemove(i);
  }
  cfgSave();
  dirty = true;
  sendJson("{\"ok\":true}");
}

static void hSaved() {
  if (!guard()) return;
  String o;
  o.reserve(400);
  o += "{\"list\":[";
  bool first = true;
  for (int i = 0; i < MAX_SAVED; i++) {
    if (!cfg.saved[i].ssid[0]) continue;
    if (!first) o += ',';
    first = false;
    o += '{';
    jStr(o, "ssid", cfg.saved[i].ssid);
    o += ",\"current\":"; o += (cs == CS_CONNECTED && !strcmp(cfg.saved[i].ssid, connectedSsid)) ? "true" : "false";
    o += '}';
  }
  o += "]}";
  sendJson(o);
}

static void hSpeed() {
  if (!guard()) return;
  if (cs != CS_CONNECTED) { sendErr("Not connected to an upstream network"); return; }
  if (speedState != 1) speedPending = true;
  sendJson("{\"ok\":true}");
}

static void hRepeater() {
  if (!guard()) return;
  setRepeater(server.arg("on") == "1");
  sendJson("{\"ok\":true}");
}

static void hBatCal() {
  if (!guard()) return;
  if (!batCalibrate((uint32_t)server.arg("mv").toInt())) { sendErr("Could not calibrate. Check the voltage and the A0 wiring"); return; }
  sendJson("{\"ok\":true}");
}

static const char *const PIN_NAMES[P_COUNT] = {"up", "down", "left", "right", "ok", "back", "sda", "scl", "led"};
static const char *const PIN_ARGS[P_COUNT] = {"pUp", "pDown", "pLeft", "pRight", "pOk", "pBack", "pSda", "pScl", "pLed"};

static void hCfgGet() {
  if (!guard()) return;
  String o;
  o.reserve(700);
  o += '{';
  jStr(o, "apSsid", cfg.apSsid); o += ',';
  jStr(o, "apPass", cfg.apPass);
  o += ",\"apHidden\":"; o += (int)cfg.apHidden;
  o += ",\"apSetupOpen\":"; o += (int)cfg.apSetupOpen;
  o += ",\"maxClients\":"; o += (int)cfg.maxClients;
  o += ",\"brightness\":"; o += (int)cfg.brightness;
  o += ",\"timeout\":"; o += (int)cfg.timeoutSec;
  o += ",\"flip\":"; o += (int)cfg.flip;
  o += ",\"ledMode\":"; o += (int)cfg.ledMode;
  o += ",\"ledBright\":"; o += (int)cfg.ledBright;
  o += ",\"ledCount\":"; o += (int)cfg.ledCount;
  o += ",\"pingLed\":"; o += (int)cfg.pingLed;
  o += ",\"adcFullMv\":"; o += (int)cfg.adcFullMv;
  o += ",\"dnsMode\":"; o += (int)cfg.dnsMode;
  o += ','; jStr(o, "dnsCustom", customSet() ? IPAddress(cfg.dnsCustom[0], cfg.dnsCustom[1], cfg.dnsCustom[2], cfg.dnsCustom[3]).toString().c_str() : "");
  o += ",\"pins\":{";
  for (int i = 0; i < P_COUNT; i++) {
    if (i) o += ',';
    o += '"'; o += PIN_NAMES[i]; o += "\":"; o += (int)cfg.pin[i];
  }
  o += "}}";
  sendJson(o);
}

static void hCfgPost() {
  if (!guard()) return;
  Config n = cfg;
  const char *err = nullptr;
  bool apCh = false, pinsCh = false, ledCh = false, dnsCh = false;

  if (server.hasArg("apSsid")) {
    String s = server.arg("apSsid");
    if (s.length() < 1 || s.length() > 32) err = "Network name must be 1-32 characters";
    else if (strcmp(s.c_str(), n.apSsid)) { strlcpy(n.apSsid, s.c_str(), sizeof(n.apSsid)); apCh = true; }
  }
  if (!err && server.hasArg("apPass")) {
    String p = server.arg("apPass");
    if (p.length() && (p.length() < 8 || p.length() > 63)) err = "Password must be 8-63 characters, or empty for an open network";
    else if (strcmp(p.c_str(), n.apPass)) { strlcpy(n.apPass, p.c_str(), sizeof(n.apPass)); apCh = true; }
  }
  if (!err && server.hasArg("apHidden")) {
    uint8_t h = server.arg("apHidden") == "1";
    if (h != n.apHidden) { n.apHidden = h; apCh = true; }
  }
  if (!err && server.hasArg("apSetupOpen")) n.apSetupOpen = server.arg("apSetupOpen") == "1";
  if (!err && server.hasArg("maxClients")) {
    int m = server.arg("maxClients").toInt();
    if (m < 1 || m > 8) err = "Maximum clients must be 1-8";
    else if (m != n.maxClients) { n.maxClients = m; apCh = true; }
  }
  if (!err && server.hasArg("brightness")) {
    int b = server.arg("brightness").toInt();
    if (b < 5 || b > 100) err = "Brightness must be 5-100";
    else n.brightness = b;
  }
  if (!err && server.hasArg("timeout")) {
    int t = server.arg("timeout").toInt();
    bool ok = false;
    for (int i = 0; i < N_TIMEOUTS; i++) if (TIMEOUTS[i] == t) ok = true;
    if (!ok) err = "Unsupported screen timeout";
    else n.timeoutSec = t;
  }
  if (!err && server.hasArg("flip")) n.flip = server.arg("flip") == "1";
  if (!err && server.hasArg("ledMode")) {
    int m = server.arg("ledMode").toInt();
    if (m < 0 || m > 4) err = "Unknown LED mode"; else n.ledMode = m;
  }
  if (!err && server.hasArg("ledBright")) {
    int b = server.arg("ledBright").toInt();
    if (b < 5 || b > 100) err = "LED brightness must be 5-100"; else n.ledBright = b;
  }
  if (!err && server.hasArg("ledCount")) {
    int c = server.arg("ledCount").toInt();
    if (c < 1 || c > 8) err = "LED count must be 1-8";
    else if (c != n.ledCount) { n.ledCount = c; ledCh = true; }
  }
  if (!err && server.hasArg("pingLed")) n.pingLed = server.arg("pingLed") == "1";
  if (!err && server.hasArg("dnsMode")) {
    int m = server.arg("dnsMode").toInt();
    if (m < 0 || m > 5) err = "Unknown DNS option";
    else if (m != n.dnsMode) { n.dnsMode = m; dnsCh = true; }
  }
  if (!err && server.hasArg("dnsCustom")) {
    String d = server.arg("dnsCustom");
    IPAddress ip;
    if (d.length() == 0) { if (n.dnsCustom[0] || n.dnsCustom[1] || n.dnsCustom[2] || n.dnsCustom[3]) dnsCh = true; memset(n.dnsCustom, 0, 4); }
    else if (!ip.fromString(d)) err = "Custom DNS must be an IPv4 address";
    else { for (int k = 0; k < 4; k++) n.dnsCustom[k] = ip[k]; dnsCh = true; }
  }
  if (!err && n.dnsMode == 5 && !(n.dnsCustom[0] || n.dnsCustom[1] || n.dnsCustom[2] || n.dnsCustom[3])) err = "Enter a custom DNS address";
  if (!err && server.hasArg("adcFullMv")) {
    int v = server.arg("adcFullMv").toInt();
    if (v < 1000 || v > 20000) err = "Full-scale voltage must be 1000-20000 mV";
    else n.adcFullMv = v;
  }
  if (!err) {
    for (int i = 0; i < P_COUNT; i++) {
      if (!server.hasArg(PIN_ARGS[i])) continue;
      int v = server.arg(PIN_ARGS[i]).toInt();
      if (v < -1 || v > 16) { err = "Unsupported GPIO number"; break; }
      if (v != n.pin[i]) { n.pin[i] = (int8_t)v; pinsCh = true; }
    }
  }
  if (!err && pinsCh) pinsValid(n.pin, &err);
  if (err) { sendErr(err); return; }

  cfg = n;
  cfgSave();
  if (dnsCh) { applyDns(); inetNext = millis() + 1000; }
  applyBrightness();
  if (oled) oled->setFlipMode(cfg.flip ? 1 : 0);
  if (ledCh && !pinsCh) ledInit();
  ledShown = 0xFFFFFFFFu;
  if (pinsCh) rebootAt = millis() + 1200;
  if (apCh) apRestartAt = millis() + 800;
  dirty = true;
  sendJson(String("{\"ok\":true,\"restart\":") + (pinsCh ? "true" : "false") + ",\"apRestart\":" + (apCh ? "true" : "false") + "}");
}

static void hReboot() {
  if (!guard()) return;
  showMessage("Restarting", "Please wait");
  rebootAt = millis() + 800;
  sendJson("{\"ok\":true}");
}

static void hFactory() {
  if (!guard()) return;
  cfgDefaults();
  cfgSave();
  showMessage("Reset", "Restarting");
  rebootAt = millis() + 800;
  sendJson("{\"ok\":true}");
}

static void hRoot() {
  if (!guard()) return;
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

static void webInit() {
  server.on("/", HTTP_GET, hRoot);
  server.on("/api/status", HTTP_GET, hStatus);
  server.on("/api/clients", HTTP_GET, hClients);
  server.on("/api/screen", HTTP_GET, hScreen);
  server.on("/api/networks", HTTP_GET, hNetworks);
  server.on("/api/scan", HTTP_POST, hScan);
  server.on("/api/connect", HTTP_POST, hConnect);
  server.on("/api/forget", HTTP_POST, hForget);
  server.on("/api/saved", HTTP_GET, hSaved);
  server.on("/api/speedtest", HTTP_POST, hSpeed);
  server.on("/api/repeater", HTTP_POST, hRepeater);
  server.on("/api/battery/cal", HTTP_POST, hBatCal);
  server.on("/api/config", HTTP_GET, hCfgGet);
  server.on("/api/config", HTTP_POST, hCfgPost);
  server.on("/api/reboot", HTTP_POST, hReboot);
  server.on("/api/factory", HTTP_POST, hFactory);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
}

// ===================================================================
//  Internet check and speed test
// ===================================================================
static bool dnsQuery(IPAddress srv, uint16_t &ms) {
  WiFiUDP u;
  if (!u.begin(40000 + (micros() % 2000))) return false;
  static const uint8_t q[] = {0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0, 0, 0, 0, 0, 0,
                              7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0, 0, 1, 0, 1};
  uint32_t t = millis();
  u.beginPacket(srv, 53);
  u.write(q, sizeof(q));
  u.endPacket();
  while (millis() - t < 1500) {
    int n = u.parsePacket();
    if (n >= 12) {
      uint8_t r[12];
      u.read(r, 12);
      u.stop();
      ms = (uint16_t)(millis() - t);
      return r[0] == 0x12 && r[1] == 0x34 && (r[2] & 0x80) && ((r[3] & 0x0F) == 0) && (r[6] | r[7]);
    }
    delay(1);
  }
  u.stop();
  return false;
}

static void inetProbe() {
  WiFiClient c;
  uint32_t t = millis();
  c.setTimeout(1200);
  bool ok = c.connect(IPAddress(1, 1, 1, 1), 443);
  uint32_t ping = millis() - t;
  c.stop();
  if (!ok) {
    t = millis();
    ok = c.connect(IPAddress(8, 8, 8, 8), 443);
    ping = millis() - t;
    c.stop();
  }
  uint16_t dm = 0;
  dnsOk = dnsQuery(dnsEffective(), dm);
  dnsMs = dnsOk ? dm : 0;
  inetOk = ok || dnsOk;
  pingMs = ok ? (uint16_t)(ping > 999 ? 999 : ping) : dnsMs;
  inetKnown = true;
  dirty = true;
}

struct SpeedSrv { const char *host; const char *path; };
static const SpeedSrv SPEED_SRV[4] = {
  {"cachefly.cachefly.net", "/10mb.test"},
  {"speedtest.tele2.net", "/10MB.zip"},
  {"proof.ovh.net", "/files/10Mb.dat"},
  {"ipv4.download.thinkbroadband.com", "/10MB.zip"},
};

// Plain-HTTP download from one server (follows up to two redirects, http only).
static bool speedTry(const char *host0, const char *path0, uint32_t &kbps) {
  char host[48], path[96];
  strlcpy(host, host0, sizeof(host));
  strlcpy(path, path0, sizeof(path));
  for (int hop = 0; hop < 3; hop++) {
    IPAddress ip;
    if (!WiFi.hostByName(host, ip, 2500)) { strlcpy(speedMsg, "DNS failed", sizeof(speedMsg)); return false; }
    WiFiClient c;
    c.setTimeout(2500);
    if (!c.connect(ip, 80)) { strlcpy(speedMsg, "No connection", sizeof(speedMsg)); return false; }
    c.print(String("GET ") + path + " HTTP/1.0\r\nHost: " + host + "\r\nConnection: close\r\nUser-Agent: Mozilla/5.0\r\nAccept: */*\r\n\r\n");

    int code = 0;
    String loc;
    bool first = true, hdr = false;
    uint32_t deadline = millis() + 5000;
    while (!hdr && millis() < deadline) {
      if (!c.available()) { if (!c.connected()) break; delay(1); continue; }
      String line = c.readStringUntil('\n');
      line.trim();
      if (first) { first = false; int sp = line.indexOf(' '); code = sp >= 0 ? line.substring(sp + 1).toInt() : 0; }
      else if (line.length() == 0) hdr = true;
      else if (line.startsWith("Location:") || line.startsWith("location:")) { loc = line.substring(9); loc.trim(); }
    }
    if (!hdr) { c.stop(); strlcpy(speedMsg, "No response", sizeof(speedMsg)); return false; }
    if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
      c.stop();
      if (!loc.startsWith("http://")) { strlcpy(speedMsg, "Redirect to HTTPS", sizeof(speedMsg)); return false; }
      String rest = loc.substring(7);
      int sl = rest.indexOf('/');
      String h = sl >= 0 ? rest.substring(0, sl) : rest;
      String pth = sl >= 0 ? rest.substring(sl) : String("/");
      strlcpy(host, h.c_str(), sizeof(host));
      strlcpy(path, pth.c_str(), sizeof(path));
      continue;
    }
    if (code != 200) { c.stop(); snprintf(speedMsg, sizeof(speedMsg), "HTTP %d", code); return false; }

    static uint8_t buf[1024];
    uint32_t bytes = 0, t0 = millis(), lastUi = t0, lastData = t0;
    while ((c.connected() || c.available()) && millis() - t0 < 6000 && bytes < 10000000UL) {
      int n = c.available();
      uint32_t now = millis();
      if (n <= 0) {
        if (now - lastData > 3000) break;
        delay(1);
        continue;
      }
      if (n > (int)sizeof(buf)) n = sizeof(buf);
      n = c.read(buf, n);
      if (n > 0) { bytes += n; lastData = now; }
      if (now - lastUi > 300) {
        lastUi = now;
        uint32_t el = now - t0;
        if (el > 0) speedLiveKbps = (uint32_t)((uint64_t)bytes * 8 / el);
        uint32_t pc = el * 100 / 6000;
        speedPct = (uint8_t)(pc > 100 ? 100 : pc);
        dirty = true;
        server.handleClient();
        uiDraw();
      }
      yield();
    }
    c.stop();
    uint32_t ms = millis() - t0;
    if (ms < 300 || bytes < 20000) { strlcpy(speedMsg, "Too little data", sizeof(speedMsg)); return false; }
    kbps = (uint32_t)((uint64_t)bytes * 8 / ms);
    return true;
  }
  strlcpy(speedMsg, "Too many redirects", sizeof(speedMsg));
  return false;
}

static void speedRun() {
  speedPending = false;
  speedState = 1;
  speedLiveKbps = 0;
  speedPct = 0;
  speedMsg[0] = 0;
  dirty = true;
  uiDraw();
  uint32_t k = 0, began = millis();
  bool ok = false;
  for (int i = 0; i < 4 && !ok && millis() - began < 20000; i++) ok = speedTry(SPEED_SRV[i].host, SPEED_SRV[i].path, k);
  if (ok) { speedKbps = k; speedState = 2; }
  else speedState = 3;
  inetNext = millis() + 5000;
  dirty = true;
}

// ===================================================================
//  Arduino entry points
// ===================================================================
void setup() {
  cfgLoad();
  displayInit();
  buttonsInit();
  ledInit();
  batPoll();
  splashUntil = millis() + 1500;
  lastActivity = millis();

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setAutoReconnect(true);
  startAp();

  ip_napt_init(NAPT_ENTRIES, NAPT_PORTS);
  ip_napt_enable_no(SOFTAP_IF, cfg.repeater);
  trafficHook();

  webInit();
}

void loop() {
  server.handleClient();
  buttonsPoll();
  batPoll();
  pollScan();
  pollWifi();
  uiHandle();
  uiTick();
  ledUpdate();
  builtinLedUpdate();
  trafficTick();
  uiDraw();

  uint32_t now = millis();
  if (speedPending) {
    if (cs == CS_CONNECTED) speedRun();
    else { speedPending = false; speedState = 3; strlcpy(speedMsg, "Not connected", sizeof(speedMsg)); }
  } else if (cs == CS_CONNECTED && (int32_t)(now - inetNext) >= 0 && now - lastActivity > 2500 && screen != SC_KEYS) {
    inetProbe();
    inetNext = millis() + ((inetOk && dnsOk) ? 30000 : 10000);
  }
  static uint32_t apChk = 0;
  if (now - apChk > 1000) {
    apChk = now;
    bool want = apWantOpen();
    if (!apRestartAt && want != apIsOpen) apRestartAt = now + (want ? 200 : 2000);
  }
  if (apRestartAt && (int32_t)(now - apRestartAt) >= 0) { apRestartAt = 0; startAp(); }
  if (rebootAt && (int32_t)(now - rebootAt) >= 0) ESP.restart();
  delay(2);
}
