/* -------------------------------------------------
Copyright (c)
Arduino project by Tech Talkies YouTube Channel.
https://www.youtube.com/@techtalkies1
-------------------------------------------------*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Adafruit_Protomatter.h>
#include <EEPROM.h>
#include <Fonts/Picopixel.h>  // small units font
#include "pico/time.h"        // repeating timer for the encoder poll
#include "clockFont.h"        // TTFont + TechTalkies logo font
#include "secrets.h"

// ─── Config ───────────────────────────────────────────────────────────────────
#define POLL_MS 22000
#define RETRY_MS 10000
#define FRAME_MS 50
#define SWEEP_MS 8000  // one sweep revolution
#define LIST_ROWS 4
#define MAX_EXTRAP_S 60
#define EXTRAPOLATE 1
#define SPLASH_MIN_MS 2500
#define AUTH_RETRIES 3
#define AUTH_RETRY_MS 3000
#define LONG_PRESS_MS 800
#define SET_ROWS 3

// Radar range choices, cycled from the settings page. Same index in both lists.
#define RANGE_COUNT 4
const uint8_t rangeOptionsNm[RANGE_COUNT] = { 10, 20, 40, 80 };
const uint16_t rangeOptionsKm[RANGE_COUNT] = { 20, 40, 80, 150 };

// Rotary encoder
#define ENC_CLK 26
#define ENC_DT 27
#define ENC_SW 28
#define ENC_REVERSE 0                          // set to 1 if turning clockwise goes the wrong way
#define ENC_HALF_STEP 0                        // 0: detents rest at both-pins-high (most KY-040). 1: module rests at 11 and 00 alternately
#define ENC_MIN_STEPS (ENC_HALF_STEP ? 1 : 2)  // quadrature steps needed to count a click (4 / 2 are the ideal values)

const char* AUTH_HOST = "auth.opensky-network.org";
const char* API_HOST = "opensky-network.org";

String bearerToken = "";
uint32_t tokenExpiresMillis = 0;

// ─── Network status ───────────────────────────────────────────────────────────
#define NS_BOOT 0
#define NS_WIFI 1
#define NS_AUTH 2
#define NS_FETCH 3
#define NS_PARSE 4
#define NS_READY 5
#define NS_WIFI_FAIL 6
#define NS_AUTH_FAIL 7
#define NS_API_FAIL 8
#define NS_DATA_FAIL 9
volatile uint8_t netStatus = NS_BOOT;
volatile uint8_t authAttempt = 0;

// ─── Matrix pins ──────────────────────────────────────────────────────────────
#define PANEL_WIDTH 64
#define BIT_DEPTH 4
uint8_t rgbPins[] = { 2, 3, 4, 5, 6, 7 };
uint8_t addrPins[] = { 8, 9, 10, 11 };
uint8_t clockPin = 12;
uint8_t latchPin = 13;
uint8_t oePin = 14;

Adafruit_Protomatter matrix(
  PANEL_WIDTH, BIT_DEPTH, 1, rgbPins, 4, addrPins,
  clockPin, latchPin, oePin, true);

// ─── Brightness ───────────────────────────────────────────────────────────────
// Protomatter uses OE for its bit-angle modulation, so the panel can't be dimmed in hardware.
// Every colour goes through col(), which scales it instead.
volatile uint8_t brightPct = 100;

// Scaling a channel below ~8/255 rounds it to zero in the 5/6-bit colour, so nonzero channels keep a floor of 8
uint16_t col(uint8_t r, uint8_t g, uint8_t b) {
  auto scale = [](uint8_t v) -> uint8_t {
    if (v == 0) return 0;
    uint8_t s = v * brightPct / 100;
    return s < 8 ? 8 : s;
  };
  return matrix.color565(scale(r), scale(g), scale(b));
}

// ─── Aircraft store ───────────────────────────────────────────────────────────
struct Plane {
  float lat, lon;
  float vel;
  float trk;
  float age;
  int alt;
  bool ground;
  char call[9];
  char icao[7];
  char country[16];
  float vrate;  // m/s, positive = climbing
};
#define MAX_PLANES 80

Plane planes[MAX_PLANES];
int planeCount = 0;
uint32_t fetchMs = 0;
bool haveData = false;
bool lastOk = true;
uint32_t lastFrame = 0;
float nmX[MAX_PLANES], nmY[MAX_PLANES];
bool inR[MAX_PLANES];

char selIcao[7] = "";
int lastPos = 0;
int listTop = 0;
bool jumpNearest = false;

Plane stage[MAX_PLANES];
volatile int stageCount = 0;
volatile uint32_t stageFetchMs = 0;
volatile bool stageReady = false;
volatile bool refetchNow = false;  // set when the range changes so the new area is fetched now
uint32_t lastPoll = 0;
uint32_t lastWifiTry = 0;
bool gotFirst = false;

// ─── Settings ─────────────────────────────────────────────────────────────────
volatile uint8_t rangeIdx = 2;  // index into rangeOptions (40 NM)
bool metricUnits = false;
bool settingsOpen = false;
bool settingsDirty = false;
bool detailsOpen = false;
uint8_t settingsRow = 0;

// Radar half-width in NM. The OpenSky bbox is built from this, so the unit setting only changes how the range is shown.
float activeRangeNm() {
  return metricUnits ? rangeOptionsKm[rangeIdx] / 1.852f : rangeOptionsNm[rangeIdx];
}

// ─── Persistent settings (RP2040 flash via EEPROM emulation) ──────────────────
#define SETTINGS_MAGIC 0x5A
struct SavedSettings {
  uint8_t magic;
  uint8_t brightPct;
  uint8_t rangeIdx;
  uint8_t metricUnits;
};

void loadSettings() {
  EEPROM.begin(sizeof(SavedSettings));
  SavedSettings s;
  EEPROM.get(0, s);
  if (s.magic != SETTINGS_MAGIC) return;  // first boot: keep defaults
  if (s.brightPct >= 10 && s.brightPct <= 100) brightPct = s.brightPct;
  if (s.rangeIdx < RANGE_COUNT) rangeIdx = s.rangeIdx;
  metricUnits = s.metricUnits;
}

// Called when the settings page closes, not on every turn, because each commit writes flash
void saveSettings() {
  SavedSettings s = { SETTINGS_MAGIC, brightPct, rangeIdx, metricUnits };
  EEPROM.put(0, s);
  EEPROM.commit();
  settingsDirty = false;
}

// ─── Rotary encoder (1 kHz timer poll, debounced, resyncs at every detent) ────
// Why polling: an edge-interrupt decoder reads the pins whenever the ISR happens to run. If the ISR is
// late (matrix refresh, core 1 busy with flash during TLS) or a contact bounces, it loses an edge and its
// running count stays offset, so the knob feels dead until another glitch realigns it.
// Here a new pin state is accepted only after ~3 ms of stability, steps are summed while the knob is away
// from its rest position, and one click is counted when it returns to rest. The sum is cleared at rest,
// so a missed edge can never carry over to the next click.
volatile int32_t encCount = 0;  // written only by the timer callback
int32_t encTaken = 0;           // read side (loop/draw functions)
struct repeating_timer encTimer;

// Quadrature table: index = (previous CLK/DT state << 2) | current state.
// Invalid or skipped transitions are 0. Not const, so it lives in RAM and the callback never touches flash.
int8_t encTable[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };

// Debug: log of accepted pin states. The callback only writes into this buffer; loop() prints it.
#define ENC_DEBUG 0
#define ENC_LOG_SIZE 64
volatile uint8_t encLog[ENC_LOG_SIZE];
volatile uint8_t encLogHead = 0;

bool __not_in_flash_func(encTimerCb)(struct repeating_timer* t) {
  static uint8_t state = 3;  // last accepted state (3 = both pins high = rest)
  static uint8_t pend = 3;   // candidate state being debounced
  static uint8_t pcount = 0;
  static int8_t sum = 0;

  const uint8_t cur = ((uint8_t)gpio_get(ENC_CLK) << 1) | (uint8_t)gpio_get(ENC_DT);
  if (cur != pend) {  // changed: restart the stability count
    pend = cur;
    pcount = 0;
    return true;
  }
  if (pcount < 2) {  // wait for 3 identical samples in a row
    pcount++;
    return true;
  }
  if (pend == state) return true;  // stable and unchanged

  // Accepted state change
  sum += encTable[(state << 2) | pend];
  state = pend;
  if (ENC_DEBUG) {
    encLog[encLogHead] = state;
    encLogHead = (encLogHead + 1) % ENC_LOG_SIZE;
  }

  const bool atRest = (state == 3) || (ENC_HALF_STEP && state == 0);
  if (atRest) {
    if (sum >= ENC_MIN_STEPS) encCount += ENC_REVERSE ? -1 : 1;
    else if (sum <= -ENC_MIN_STEPS) encCount += ENC_REVERSE ? 1 : -1;
    sum = 0;  // resync
  }
  return true;
}

// Clicks since the last call. Lock-free: only the callback writes encCount.
int takeEncoder() {
  const int32_t c = encCount;
  const int d = (int)(c - encTaken);
  encTaken = c;
  return d;
}

// ─── Button ───────────────────────────────────────────────────────────────────
// Short press: next settings row, open details on the selected plane, or select nearest if none.
// Long press: open or close the settings page.
// Any press while details are open just closes them.
void onButtonRelease(uint32_t heldMs) {
  if (detailsOpen) {
    detailsOpen = false;
    return;
  }
  if (heldMs >= LONG_PRESS_MS) {
    settingsOpen = !settingsOpen;
    settingsRow = 0;
    if (!settingsOpen && settingsDirty) saveSettings();
  } else if (settingsOpen) {
    settingsRow = (settingsRow + 1) % SET_ROWS;
  } else if (selIcao[0]) {
    detailsOpen = true;
  } else {
    jumpNearest = true;
  }
}

void pollButton() {
  static bool down = false;
  static uint32_t changeT = 0, pressT = 0;
  bool isDown = digitalRead(ENC_SW) == LOW;
  if (isDown == down || millis() - changeT < 30) return;
  changeT = millis();
  down = isDown;
  if (down) pressT = millis();
  else onButtonRelease(millis() - pressT);
}

// ─── Token Manager (core 1) ───────────────────────────────────────────────────
bool refreshOpenSkyToken() {
  netStatus = NS_AUTH;

  WiFiClientSecure tokenClient;
  tokenClient.setInsecure();
  tokenClient.setTimeout(8000);

  if (!tokenClient.connect(AUTH_HOST, 443)) {
    Serial.println("Auth connect failed");
    netStatus = NS_AUTH_FAIL;
    return false;
  }

  String payload = "grant_type=client_credentials&client_id=" + String(OS_CLIENT_ID) + "&client_secret=" + String(OS_CLIENT_SECRET);
  String path = "/auth/realms/opensky-network/protocol/openid-connect/token";

  tokenClient.printf("POST %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "Content-Type: application/x-www-form-urlencoded\r\n"
                     "Content-Length: %d\r\n"
                     "Connection: close\r\n\r\n"
                     "%s",
                     path.c_str(), AUTH_HOST, payload.length(), payload.c_str());

  while (tokenClient.connected()) {
    String line = tokenClient.readStringUntil('\n');
    if (line == "\r") break;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, tokenClient);
  tokenClient.stop();

  if (err || doc["access_token"].isNull()) {
    Serial.print("Token JSON err: ");
    Serial.println(err ? err.c_str() : "no access_token");
    netStatus = NS_AUTH_FAIL;
    return false;
  }

  bearerToken = doc["access_token"].as<String>();
  int expiresIn = doc["expires_in"].as<int>();
  tokenExpiresMillis = millis() + ((expiresIn - 60) * 1000);

  Serial.println("OAuth Token Refreshed!");
  return true;
}

// ─── Data Fetcher (core 1) ────────────────────────────────────────────────────
int fetchAircraft() {
  if (bearerToken == "" || millis() > tokenExpiresMillis) {
    bool ok = false;
    for (uint8_t attempt = 1; attempt <= AUTH_RETRIES && !ok; attempt++) {
      authAttempt = attempt;
      ok = refreshOpenSkyToken();
      if (!ok && attempt < AUTH_RETRIES) {
        netStatus = NS_AUTH_FAIL;  // splash shows the retry countdown during the wait
        delay(AUTH_RETRY_MS);
      }
    }
    if (!ok) return -1;
  }

  netStatus = NS_FETCH;

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(8000);

  if (!client.connect(API_HOST, 443)) {
    Serial.println("API connect failed");
    netStatus = NS_API_FAIL;
    return -1;
  }

  const float rangeNm = activeRangeNm();
  float latDiff = rangeNm / 60.0f;
  float lonDiff = rangeNm / (60.0f * cosf(HOME_LAT * DEG_TO_RAD));

  float lamin = HOME_LAT - latDiff;
  float lamax = HOME_LAT + latDiff;
  float lomin = HOME_LON - lonDiff;
  float lomax = HOME_LON + lonDiff;

  char path[128];
  snprintf(path, sizeof(path), "/api/states/all?lamin=%.4f&lomin=%.4f&lamax=%.4f&lomax=%.4f",
           lamin, lomin, lamax, lomax);

  client.printf("GET %s HTTP/1.0\r\n"
                "Host: %s\r\n"
                "User-Agent: pico-flight-radar/1.0\r\n"
                "Accept: application/json\r\n"
                "Accept-Encoding: identity\r\n"
                "Authorization: Bearer %s\r\n"
                "Connection: close\r\n\r\n",
                path, API_HOST, bearerToken.c_str());

  String status = client.readStringUntil('\n');
  uint32_t respMs = millis();

  if (status.indexOf(" 200") < 0) {
    if (status.indexOf(" 401") >= 0) bearerToken = "";
    client.stop();
    netStatus = NS_API_FAIL;
    return -1;
  }

  if (!client.find("\r\n\r\n")) {
    client.stop();
    netStatus = NS_API_FAIL;
    return -1;
  }

  netStatus = NS_PARSE;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, client);
  client.stop();

  if (err) {
    netStatus = NS_DATA_FAIL;
    return -1;
  }

  if (doc["states"].isNull()) {
    stageFetchMs = respMs;
    return 0;
  }
  if (!doc["states"].is<JsonArray>()) {
    netStatus = NS_DATA_FAIL;
    return -1;
  }

  uint32_t respTime = doc["time"] | 0;
  JsonArray states = doc["states"].as<JsonArray>();

  int n = 0;
  for (JsonArray a : states) {
    if (n >= MAX_PLANES) break;
    if (a[5].isNull() || a[6].isNull()) continue;

    Plane& p = stage[n];
    p.lon = a[5].as<float>();
    p.lat = a[6].as<float>();
    p.ground = a[8].as<bool>();
    p.vel = a[9].isNull() ? 0 : a[9].as<float>();
    p.trk = a[10].isNull() ? NAN : a[10].as<float>();
    p.vrate = a[11].isNull() ? NAN : a[11].as<float>();

    uint32_t tp = a[3].isNull() ? 0 : a[3].as<uint32_t>();
    p.age = (respTime && tp && respTime >= tp) ? (float)(respTime - tp) : 0;

    float alt_m = a[13].is<float>() ? a[13].as<float>() : (a[7].is<float>() ? a[7].as<float>() : 0);
    p.alt = p.ground ? 0 : (int)(alt_m * 3.28084f);

    const char* ic = a[0].is<const char*>() ? a[0].as<const char*>() : "";
    strncpy(p.icao, ic, 6);
    p.icao[6] = 0;

    const char* co = a[2].is<const char*>() ? a[2].as<const char*>() : "";
    strncpy(p.country, co, 15);
    p.country[15] = 0;

    const char* f = a[1].is<const char*>() ? a[1].as<const char*>() : "";
    strncpy(p.call, f, 8);
    p.call[8] = 0;

    for (int i = strlen(p.call) - 1; i >= 0 && p.call[i] == ' '; i--) p.call[i] = 0;
    if (!p.call[0]) strcpy(p.call, "-----");

    n++;
  }
  stageFetchMs = respMs;
  return n;
}

// ─── Boot splash ──────────────────────────────────────────────────────────────
void drawSplash() {
  const uint32_t now = millis();
  const uint16_t cyan = col(0, 200, 255);
  const uint16_t red = col(255, 60, 60);
  const uint16_t green = col(0, 255, 0);
  const uint16_t track = col(0, 30, 50);

  matrix.fillScreen(0);

  uint8_t b = 170 + (uint8_t)(85.0f * (0.5f + 0.5f * sinf(now * (TWO_PI / 2000.0f))));
  matrix.setFont(&TechTalkies);
  matrix.setTextSize(2);
  matrix.setTextColor(col(0, (b * 4) / 5, b));
  matrix.setCursor(4, 17);
  matrix.print("|");
  matrix.setTextSize(1);

  matrix.setFont(&TTFont);

  matrix.setTextColor(cyan);
  matrix.setCursor(20, 10);
  matrix.print("FLIGHT");
  matrix.setTextColor(col(255, 255, 255));
  matrix.setCursor(20, 18);
  matrix.print("RADAR");

  uint8_t st = netStatus;
  const char* txt;
  uint8_t step;
  switch (st) {
    case NS_WIFI:
      txt = "WIFI";
      step = 1;
      break;
    case NS_AUTH:
      txt = "LOGIN";
      step = 2;
      break;
    case NS_FETCH:
      txt = "FETCHING";
      step = 3;
      break;
    case NS_PARSE:
      txt = "PARSING";
      step = 4;
      break;
    case NS_READY:
      txt = "READY";
      step = 5;
      break;
    case NS_WIFI_FAIL:
      txt = "NO WIFI";
      step = 1;
      break;
    case NS_AUTH_FAIL:
      txt = "LOGIN FAIL";
      step = 2;
      break;
    case NS_API_FAIL:
      txt = "API ERROR";
      step = 3;
      break;
    case NS_DATA_FAIL:
      txt = "BAD DATA";
      step = 4;
      break;
    default:
      txt = "STARTING";
      step = 0;
      break;
  }
  char retryTxt[16];
  if (st == NS_AUTH_FAIL && authAttempt < AUTH_RETRIES) {
    snprintf(retryTxt, sizeof(retryTxt), "RETRY %d/%d", authAttempt + 1, AUTH_RETRIES);
    txt = retryTxt;
  }

  bool fail = st >= NS_WIFI_FAIL;
  bool busy = !fail && st != NS_READY;

  matrix.setTextColor(fail ? red : (st == NS_READY ? green : cyan));
  matrix.setCursor(2, 28);
  matrix.print(txt);
  if (busy) {
    int dots = (now / 350) % 4;
    for (int i = 0; i < dots; i++) matrix.print('.');
  }

  matrix.fillRect(2, 30, 60, 2, track);
  matrix.fillRect(2, 30, 60 * step / 5, 2, fail ? red : cyan);
  if (busy) {
    int sx = 2 + (int)((now / 25) % 58);
    matrix.fillRect(sx, 30, 3, 2, col(150, 150, 150));
  }

  matrix.show();
}

// ─── Settings page ────────────────────────────────────────────────────────────
void adjustSetting(int steps) {
  if (!steps) return;
  settingsDirty = true;
  switch (settingsRow) {
    case 0:  // brightness, 10% steps
      brightPct = constrain((int)brightPct + steps * 10, 10, 100);
      break;
    case 1:
      {  // radar range
        int r = constrain((int)rangeIdx + steps, 0, RANGE_COUNT - 1);
        if (r != rangeIdx) {
          rangeIdx = r;
          selIcao[0] = 0;  // the old selection may be out of the new range
          lastPos = 0;
          refetchNow = true;
        }
        break;
      }
    case 2:  // units
      metricUnits = !metricUnits;
      refetchNow = true;  // the range is different in km, so fetch the new box
      break;
  }
}

void drawSettings() {
  matrix.fillScreen(0);
  matrix.setFont(&TTFont);

  adjustSetting(takeEncoder());

  matrix.setTextColor(col(0, 200, 255));
  matrix.setCursor(2, 5);
  matrix.print("SETTINGS");

  const char* labels[SET_ROWS] = { "BRT", "RNG", "UNT" };
  for (uint8_t i = 0; i < SET_ROWS; i++) {
    const int y = 13 + i * 8;
    const bool sel = i == settingsRow;
    matrix.setTextColor(sel ? col(255, 120, 0) : col(150, 150, 150));
    matrix.setCursor(2, y);
    matrix.print(sel ? '>' : ' ');
    matrix.setCursor(8, y);
    matrix.print(labels[i]);

    matrix.setCursor(30, y);
    switch (i) {
      case 0:
        matrix.print((int)brightPct);
        printUnit("%", 150, 150, 150);
        break;
      case 1:
        matrix.print(metricUnits ? rangeOptionsKm[rangeIdx] : rangeOptionsNm[rangeIdx]);
        printUnit(metricUnits ? "KM" : "NM", 150, 150, 150);
        break;
      case 2:
        matrix.print(metricUnits ? "M/KMH" : "FT/KTS");
        break;
    }
  }

  matrix.show();
}

// ─── Radar UI ─────────────────────────────────────────────────────────────────
void updatePositions() {
  const float nmPerLon = 60.0f * cosf(HOME_LAT * DEG_TO_RAD);
  float since = (millis() - fetchMs) / 1000.0f;
  if (since > MAX_EXTRAP_S) since = MAX_EXTRAP_S;

  for (int i = 0; i < planeCount; i++) {
    const Plane& p = planes[i];
    float x = (p.lon - HOME_LON) * nmPerLon;
    float y = (p.lat - HOME_LAT) * 60.0f;
    if (EXTRAPOLATE && !p.ground && !isnan(p.trk) && p.vel > 0) {
      float distNm = p.vel * (p.age + since) / 1852.0f;
      float t = p.trk * DEG_TO_RAD;
      x += sinf(t) * distNm;
      y += cosf(t) * distNm;
    }
    nmX[i] = x;
    nmY[i] = y;
  }
}

bool toPx(int i, int& px, int& py) {
  const float scale = 15.0f / activeRangeNm();
  px = 16 + (int)roundf(nmX[i] * scale);
  py = 16 - (int)roundf(nmY[i] * scale);
  return px >= 0 && px <= 31 && py >= 0 && py <= 31;
}

void drawBrackets() {
  const uint16_t c = col(0, 200, 0);
  const int L = 3;
  matrix.drawFastHLine(1, 1, L, c);
  matrix.drawFastVLine(1, 1, L, c);
  matrix.drawFastHLine(28, 1, L, c);
  matrix.drawFastVLine(30, 1, L, c);
  matrix.drawFastHLine(1, 30, L, c);
  matrix.drawFastVLine(1, 28, L, c);
  matrix.drawFastHLine(28, 30, L, c);
  matrix.drawFastVLine(30, 28, L, c);
}

void fitText(char* s, uint16_t maxW) {
  int16_t x1, y1;
  uint16_t w, h;
  while (s[0]) {
    matrix.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
    if (w <= maxW) break;
    s[strlen(s) - 1] = 0;
  }
}

// Units are printed in the small font and dimmer than the value beside them
void printUnit(const char* u, uint8_t r, uint8_t g, uint8_t b) {
  const int16_t x = matrix.getCursorX();
  const int16_t y = matrix.getCursorY();
  matrix.setFont(&Picopixel);
  matrix.setCursor(x + 2, y);  // 2px gap after the value
  matrix.setTextColor(col(r * 2 / 5, g * 2 / 5, b * 2 / 5));
  matrix.print(u);
  matrix.setFont(&TTFont);
}

// Heading arrow for the selected plane: shaft from just outside the halo, barbs at the tip
void drawHeading(int px, int py, float trkDeg, uint16_t c) {
  float t = trkDeg * DEG_TO_RAD;
  float fx = sinf(t), fy = -cosf(t);  // forward (screen y points down)
  float rx = cosf(t), ry = sinf(t);   // right side

  // 3 pixels: tip plus two barbs behind it, forming a chevron just outside the halo
  const int tx = px + (int)roundf(fx * 4), ty = py + (int)roundf(fy * 4);
  matrix.drawPixel(tx, ty, c);
  matrix.drawPixel(tx - (int)roundf(fx + rx), ty - (int)roundf(fy + ry), c);
  matrix.drawPixel(tx - (int)roundf(fx - rx), ty - (int)roundf(fy - ry), c);
}

// Full-panel details for the selected plane
// Takes an index, not a Plane&: the IDE's auto-generated prototypes sit above the struct and can't name it
void drawDetails(int idx) {
  const Plane& p = planes[idx];
  matrix.fillScreen(0);
  matrix.setFont(&TTFont);

  // Callsign bar, same style as the dashboard
  char call[9];
  strncpy(call, p.call, 8);
  call[8] = 0;
  fitText(call, 60);
  int16_t bx, by;
  uint16_t bw, bh;
  matrix.getTextBounds(call, 2, 5, &bx, &by, &bw, &bh);
  matrix.drawFastHLine(bx, by + bh + 1, 64 - bx, col(255, 255, 255));  // underline, 1px below text, runs to the panel edge
  matrix.setTextColor(col(255, 0, 255));
  matrix.setCursor(2, 5);
  matrix.print(call);

  matrix.setTextColor(col(200, 200, 200));
  matrix.setCursor(2, 13);
  matrix.print("HDG ");
  if (isnan(p.trk)) matrix.print("--");
  else matrix.print((int)roundf(p.trk));
  printUnit("DEG", 200, 200, 200);

  matrix.setTextColor(col(0, 255, 0));
  matrix.setCursor(2, 21);
  matrix.print("V/S ");
  if (isnan(p.vrate)) {
    matrix.print("--");
  } else {
    const int fpm = (int)roundf(p.vrate * 196.85f);
    if (fpm > 0) matrix.print('+');
    matrix.print(fpm);
  }
  printUnit("FPM", 0, 255, 0);

  char country[16];
  strncpy(country, p.country, 15);
  country[15] = 0;
  fitText(country, 60);
  matrix.setTextColor(col(255, 220, 0));
  matrix.setCursor(2, 29);
  matrix.print(country);

  matrix.show();
}

void drawFrame() {
  updatePositions();
  const uint32_t now = millis();
  const float rangeNm = activeRangeNm();
  const float r2 = rangeNm * rangeNm;

  int order[MAX_PLANES];
  float d2[MAX_PLANES];
  int n = 0;
  for (int i = 0; i < planeCount; i++) {
    d2[i] = nmX[i] * nmX[i] + nmY[i] * nmY[i];
    inR[i] = d2[i] <= r2;
    if (!inR[i] || planes[i].ground) continue;
    int k = n++;
    while (k > 0 && d2[order[k - 1]] > d2[i]) {
      order[k] = order[k - 1];
      k--;
    }
    order[k] = i;
  }

  // Keep pending steps while the list is empty so turns aren't lost during a range refetch
  int steps = n > 0 ? takeEncoder() : 0;
  int pos = -1;
  if (n > 0) {
    for (int k = 0; k < n; k++) {
      if (strcmp(planes[order[k]].icao, selIcao) == 0) {
        pos = k;
        break;
      }
    }
    if (pos < 0) pos = lastPos < n ? lastPos : n - 1;
    if (jumpNearest) pos = 0;
    else if (steps) pos = ((pos + steps) % n + n) % n;
    jumpNearest = false;

    strncpy(selIcao, planes[order[pos]].icao, 6);
    selIcao[6] = 0;
    lastPos = pos;
  } else {
    selIcao[0] = 0;
    lastPos = 0;
    jumpNearest = false;
  }

  int fi = pos >= 0 ? order[pos] : -1;

  // Details replace the radar view. They close if the plane has left range or the list is empty.
  if (detailsOpen && fi >= 0) {
    drawDetails(fi);
    return;
  }
  detailsOpen = false;

  matrix.fillScreen(0);

  // ── Radar UI ──
  const int cx = 16, cy = 16;
  matrix.drawCircle(cx, cy, 15, col(0, 80, 0));
  matrix.drawCircle(cx, cy, 7, col(0, 40, 0));

  float ang = (now % SWEEP_MS) * (TWO_PI / SWEEP_MS);
  matrix.drawLine(cx, cy, cx + (int)roundf(sinf(ang) * 15), cy - (int)roundf(cosf(ang) * 15), col(0, 120, 0));

  matrix.drawPixel(cx, cy, col(255, 255, 255));

  for (int i = 0; i < planeCount; i++) {
    if (!inR[i]) continue;
    int px, py;
    if (!toPx(i, px, py)) continue;
    const Plane& p = planes[i];

    uint16_t c = (i == fi) ? col(0, 220, 255) : col(150, 150, 0);
    matrix.drawPixel(px, py, c);
  }

  // Blinking halo around the selected plane
  const bool blinkOn = (now / 500) % 2 == 0;

  if (fi >= 0 && blinkOn) {
    int px, py;
    if (toPx(fi, px, py)) {
      const uint16_t h = col(255, 255, 255);
      // Ring of radius ~2: keeps the 4 axis points and the 8 near-diagonal points, drops the corners
      for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++) {
          int d2 = dx * dx + dy * dy;
          if (d2 >= 4 && d2 <= 5) matrix.drawPixel(px + dx, py + dy, h);
        }
    }
  }

  // Heading arrow on the selected plane (skipped if the track is unknown)
  if (fi >= 0 && blinkOn && !isnan(planes[fi].trk)) {
    int px, py;
    if (toPx(fi, px, py)) drawHeading(px, py, planes[fi].trk, col(255, 255, 255));
  }

  drawBrackets();

  // ── Telemetry Dashboard ──
  if (n > 0 && pos >= 0) {
    const Plane& p = planes[order[pos]];
    float dist = sqrtf(d2[order[pos]]);

    // Callsign inverted: orange bar with black text
    char call[9];
    strncpy(call, p.call, 8);
    call[8] = 0;
    fitText(call, 30);
    int16_t bx, by;
    uint16_t bw, bh;
    matrix.getTextBounds(call, 34, 5, &bx, &by, &bw, &bh);
    matrix.drawFastHLine(bx, by + bh + 1, 64 - bx, col(255, 255, 255));  // underline, 1px below text, runs to the panel edge
    matrix.setTextColor(col(255, 0, 255));
    matrix.setCursor(34, 5);
    matrix.print(call);

    // Altitude + Units
    matrix.setTextColor(col(200, 200, 200));
    matrix.setCursor(34, 13);
    if (p.ground) {
      matrix.print("GND");
    } else {
      matrix.print(metricUnits ? (int)(p.alt * 0.3048f) : p.alt);
      printUnit(metricUnits ? "M" : "FT", 200, 200, 200);
    }

    // Speed + Units
    matrix.setTextColor(col(0, 255, 0));
    matrix.setCursor(34, 21);
    matrix.print(metricUnits ? (int)(p.vel * 3.6f) : (int)(p.vel * 1.94384f));
    printUnit(metricUnits ? "KMH" : "KTS", 0, 255, 0);

    // Distance + Units
    matrix.setTextColor(col(255, 220, 0));
    matrix.setCursor(34, 29);
    matrix.print(metricUnits ? dist * 1.852f : dist, 1);
    printUnit(metricUnits ? "KM" : "NM", 255, 220, 0);
  }

  bool stale = (now - fetchMs) > (2UL * POLL_MS + 5000);
  if (!lastOk || stale) matrix.drawPixel(63, 0, col(255, 0, 0));

  matrix.show();
}

// ─── Setup & Loops ────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);
  loadSettings();

  ProtomatterStatus status = matrix.begin();
  if (status != PROTOMATTER_OK) {
    pinMode(LED_BUILTIN, OUTPUT);
    while (1) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      delay(100);
    }
  }

  matrix.setFont(&TTFont);
  matrix.setTextWrap(false);

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  // Poll the encoder every 1 ms from a hardware timer (no GPIO interrupts)
  add_repeating_timer_us(-1000, encTimerCb, NULL, &encTimer);

  drawSplash();
}

void loop() {
  if (ENC_DEBUG) {
    static uint8_t logTail = 0;
    while (logTail != encLogHead) {
      uint8_t s = encLog[logTail];
      logTail = (logTail + 1) % ENC_LOG_SIZE;
      Serial.print("state CLK=");
      Serial.print((s >> 1) & 1);
      Serial.print(" DT=");
      Serial.print(s & 1);
      Serial.print(" count=");
      Serial.println(encCount);
    }
  }

  if (stageReady) {
    if (stageCount >= 0) {
      memcpy(planes, stage, sizeof(Plane) * stageCount);
      planeCount = stageCount;
      fetchMs = stageFetchMs;
      lastOk = true;
      haveData = true;
    } else {
      lastOk = false;
    }
    __sync_synchronize();
    stageReady = false;
  }

  pollButton();

  if (millis() - lastFrame >= FRAME_MS) {
    lastFrame = millis();
    if (settingsOpen) drawSettings();
    else if (haveData && millis() >= SPLASH_MIN_MS) drawFrame();
    else drawSplash();
  }
}

bool connectWifi(uint32_t timeoutMs) {
  netStatus = NS_WIFI;
  static bool started = false, wasUp = false;
  if (!started || wasUp) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    started = true;
    wasUp = false;
  }
  uint32_t t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < timeoutMs) delay(250);
  lastWifiTry = millis();
  if (WiFi.status() == WL_CONNECTED) {
    wasUp = true;
    return true;
  }
  netStatus = NS_WIFI_FAIL;
  return false;
}

void setup1() {
  connectWifi(20000);
}

void loop1() {
  uint32_t now = millis();
  if (WiFi.status() != WL_CONNECTED) {
    if (now - lastWifiTry > 10000) connectWifi(15000);
    delay(100);
    return;
  }
  uint32_t interval = gotFirst ? POLL_MS : RETRY_MS;
  if (!stageReady && (refetchNow || lastPoll == 0 || now - lastPoll > interval)) {
    refetchNow = false;
    lastPoll = now;
    if (ENC_DEBUG) {
      Serial.print("fetch start range=");
      Serial.print(activeRangeNm());
      Serial.print("NM heap=");
      Serial.println(rp2040.getFreeHeap());
    }
    int n = fetchAircraft();
    if (ENC_DEBUG) {
      Serial.print("fetch end n=");
      Serial.print(n);
      Serial.print(" took=");
      Serial.print(millis() - now);
      Serial.print("ms heap=");
      Serial.println(rp2040.getFreeHeap());
    }
    if (n >= 0) {
      gotFirst = true;
      netStatus = NS_READY;
    }
    stageCount = n;
    __sync_synchronize();
    stageReady = true;
  }
  delay(100);
}