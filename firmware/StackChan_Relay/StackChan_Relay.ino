/*
 * StackChan Relay Client
 *
 * Connects to WiFi, then long-polls a relay server for commands
 * (speak / emote / move_head / wiggle) and executes them on the
 * M5StackChan (CoreS3) hardware: Avatar face + RGB LEDs + head servos.
 *
 * Board: M5CoreS3
 * Libraries required: M5StackChan, M5Unified, M5Stack_Avatar, ArduinoJson (v7+)
 */

#include <M5StackChan.h>
#include <M5Unified.h>
#include <Avatar.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_wifi.h"

using namespace m5avatar;

// ====================== USER CONFIG ======================
#define WIFI_SSID     "yyw一点也不可爱"
#define WIFI_PASSWORD "12345678@"

#define RELAY_HOST  "43-129-25-76.nip.io"
#define RELAY_TOKEN "FjYDQYYC5rUFi3f6vhN26MI1kzu_h2Qy"
// ===========================================================

#define POLL_TIMEOUT_MS   35000  // server holds /poll for ~25s, give it margin
#define HTTP_TIMEOUT_MS   10000
#define SERVO_SPEED       400    // 0-1000, moderate speed for move_head
#define WIGGLE_SPEED      600
#define WIGGLE_ANGLE_DEG  20
#define PITCH_MIN_DEG     5
#define PITCH_MAX_DEG     85
#define YAW_MIN_DEG       -128
#define YAW_MAX_DEG       128

Avatar avatar;

// Reused across requests: creating a fresh WiFiClientSecure (and doing a
// full TLS handshake) for every /poll cycle is slow and was causing
// intermittent "HTTP error -1" failures. Keep one TLS connection alive
// and let HTTPClient's keep-alive reuse it across /poll and /result.
WiFiClientSecure netClient;
HTTPClient http;

// ---------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------

void connectWiFi() {
  avatar.setExpression(Expression::Sleepy);
  M5StackChan.showRgbColor(0, 0, 60);  // blue while connecting

  // Any previously-established TLS connection is dead once WiFi drops.
  netClient.stop();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Connecting to WiFi %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println(" connected");
  Serial.println(WiFi.localIP());

  // Disable WiFi power-save so the long-poll connection doesn't get
  // throttled/dropped by modem sleep.
  esp_wifi_set_ps(WIFI_PS_NONE);

  M5StackChan.showRgbColor(0, 60, 0);  // green flash on success
  delay(300);
  M5StackChan.showRgbColor(0, 0, 0);
  avatar.setExpression(Expression::Neutral);
}

void ensureWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi dropped, reconnecting...");
    avatar.setExpression(Expression::Sleepy);
    M5StackChan.showRgbColor(60, 0, 0);  // red while reconnecting
    WiFi.disconnect();
    connectWiFi();
  }
}

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------

Expression expressionFromString(const String &name) {
  if (name == "happy")     return Expression::Happy;
  if (name == "sad")       return Expression::Sad;
  if (name == "angry")     return Expression::Angry;
  if (name == "shy")       return Expression::Doubt;
  if (name == "thinking")  return Expression::Doubt;
  if (name == "surprised") return Expression::Neutral;
  if (name == "sleepy")    return Expression::Sleepy;
  return Expression::Neutral;  // "normal" and anything unrecognized
}

void rgbForExpression(Expression exp, uint8_t &r, uint8_t &g, uint8_t &b) {
  switch (exp) {
    case Expression::Happy:  r = 60; g = 60; b = 0;  break;  // yellow
    case Expression::Sad:    r = 0;  g = 0;  b = 60; break;  // blue
    case Expression::Angry:  r = 60; g = 0;  b = 0;  break;  // red
    case Expression::Doubt:  r = 60; g = 0;  b = 60; break;  // purple
    case Expression::Sleepy: r = 0;  g = 0;  b = 20; break;  // dim blue
    default:                 r = 0;  g = 30; b = 30; break;  // neutral cyan
  }
}

long clampLong(long v, long lo, long hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void waitForMotion(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (M5StackChan.Motion.isMoving() && (millis() - start) < timeoutMs) {
    M5StackChan.update();
    delay(10);
  }
}

void waitForYaw(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (M5StackChan.Motion.isYawMoving() && (millis() - start) < timeoutMs) {
    M5StackChan.update();
    delay(10);
  }
}

// ---------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------

void handleSpeak(JsonDocument &doc) {
  const char *text = doc["text"] | "";
  Serial.printf("[speak] %s\n", text);
  avatar.setSpeechText(text);
}

void handleEmote(JsonDocument &doc) {
  String expression = doc["expression"] | "normal";
  Expression exp = expressionFromString(expression);
  Serial.printf("[emote] %s\n", expression.c_str());

  avatar.setExpression(exp);

  uint8_t r, g, b;
  rgbForExpression(exp, r, g, b);
  M5StackChan.showRgbColor(r, g, b);
}

void handleMoveHead(JsonDocument &doc) {
  long pitch = doc["pitch"] | 0;
  long yaw = doc["yaw"] | 0;

  pitch = clampLong(pitch, PITCH_MIN_DEG, PITCH_MAX_DEG);
  yaw = clampLong(yaw, YAW_MIN_DEG, YAW_MAX_DEG);

  Serial.printf("[move_head] pitch=%ld yaw=%ld\n", pitch, yaw);

  // Motion angle unit is 0.1 degree (10 == 1 degree).
  M5StackChan.Motion.move(yaw * 10, pitch * 10, SERVO_SPEED);
  waitForMotion(4000);
}

void handleWiggle(JsonDocument &doc) {
  (void)doc;
  Serial.println("[wiggle]");
  int amount = WIGGLE_ANGLE_DEG * 10;  // 0.1 degree units

  M5StackChan.Motion.moveYaw(-amount, WIGGLE_SPEED);
  waitForYaw(2000);
  M5StackChan.Motion.moveYaw(amount, WIGGLE_SPEED);
  waitForYaw(2000);
  M5StackChan.Motion.moveYaw(0, WIGGLE_SPEED);
  waitForYaw(2000);
}

// ---------------------------------------------------------------------
// Relay communication
// ---------------------------------------------------------------------

// On a connection-level failure (negative HTTPClient error code), the
// underlying socket/TLS session may be wedged. Drop it so the next
// request starts a clean TCP+TLS handshake instead of reusing it.
void closeConnectionOnError(int httpCode) {
  if (httpCode <= 0) {
    netClient.stop();
  }
}

bool pollCommand(JsonDocument &doc) {
  // Connect timeout covers TCP connect + TLS handshake, which can take a
  // few seconds against a VPS over Let's Encrypt - give it plenty of room.
  http.setConnectTimeout(15000);
  http.setTimeout(POLL_TIMEOUT_MS);

  String url = String("https://") + RELAY_HOST + "/poll?token=" + RELAY_TOKEN;
  if (!http.begin(netClient, url)) {
    Serial.println("[poll] begin() failed");
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[poll] HTTP error %d\n", code);
    http.end();
    closeConnectionOnError(code);
    return false;
  }

  String body = http.getString();
  http.end();

  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.printf("[poll] JSON error: %s\n", err.c_str());
    return false;
  }
  return true;
}

void reportResult(const String &id, const String &status) {
  if (id.isEmpty()) return;

  http.setConnectTimeout(15000);
  http.setTimeout(HTTP_TIMEOUT_MS);

  String url = String("https://") + RELAY_HOST + "/result?token=" + RELAY_TOKEN;
  if (!http.begin(netClient, url)) {
    Serial.println("[result] begin() failed");
    return;
  }
  http.addHeader("Content-Type", "application/json");

  JsonDocument doc;
  doc["id"] = id;
  doc["status"] = status;
  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);
  Serial.printf("[result] POST -> %d\n", code);
  http.end();
  closeConnectionOnError(code);
}

// ---------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------

void setup() {
  Serial.begin(115200);

  M5StackChan.begin();
  avatar.init();

  netClient.setInsecure();   // skip cert validation (Let's Encrypt via Caddy)
  http.setReuse(true);        // keep the TLS connection alive between requests

  connectWiFi();
}

void loop() {
  M5StackChan.update();
  ensureWiFi();

  static uint8_t pollFailures = 0;

  JsonDocument doc;
  if (!pollCommand(doc)) {
    pollFailures++;
    // Exponential backoff: 1s, 2s, 4s, 8s, capped at 8s.
    uint32_t backoffMs = 1000UL << min((int)pollFailures - 1, 3);
    delay(backoffMs);
    return;
  }
  pollFailures = 0;

  if (doc["action"].isNull()) {
    return;  // nothing to do, poll again immediately
  }

  String id = doc["id"] | "";
  String action = doc["action"] | "";

  if (action == "speak") {
    handleSpeak(doc);
  } else if (action == "emote") {
    handleEmote(doc);
  } else if (action == "move_head") {
    handleMoveHead(doc);
  } else if (action == "wiggle") {
    handleWiggle(doc);
  } else {
    Serial.printf("[main] unknown action: %s\n", action.c_str());
  }

  reportResult(id, "ok");
}
