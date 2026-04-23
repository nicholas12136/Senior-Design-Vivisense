/*
 * ViviSense - LED Ring Controller ESP32
 *
 * Live path:
 *   MainController (LedFrame_t) -> this controller -> NeoPixel ring
 *
 * Preview path:
 *   CaregiverApp sends LedFrame_t directly for UI preview mode.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#if __has_include(<esp_idf_version.h>)
#include <esp_idf_version.h>
#endif
#include <Adafruit_NeoPixel.h>
#include "led_frame.h"

#define LED_PIN         0
#define NUM_LEDS        93
#define ESPNOW_CHANNEL  1

const uint8_t MSG_COMPONENT_STATUS = 0xB4;
const uint8_t COMPONENT_LED_CONTROLLER = 2;
const uint8_t LED_BRIGHTNESS_DEFAULT = 26;   // UI default 20% (of capped 50% max)
const uint8_t LED_BRIGHTNESS_MAX = 128;      // 50%
static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct __attribute__((packed)) ComponentStatusPacket {
  uint8_t msg_type;
  uint8_t component_id;
  uint8_t sensor_seen_mask;
  uint8_t flags;
};

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

bool visualEnabled = true;
uint32_t lastHeartbeatMs = 0;
const uint32_t HEARTBEAT_PERIOD_MS = 1000;

static uint32_t resolveColor(uint8_t code) {
  switch (code) {
    case LED_COLOR_RED:    return strip.Color(255, 0, 0); //R,G,B
    case LED_COLOR_ORANGE: return strip.Color(255, 50, 0);
    case LED_COLOR_YELLOW: return strip.Color(255, 221, 0);
    case LED_COLOR_GREEN:  return strip.Color(15, 138, 15);
    case LED_COLOR_BLUE:   return strip.Color(0, 85, 255);
    default:               return 0;
  }
}

static void applyFrame(const LedFrame_t& frame) {
  uint8_t brightness = frame.brightness;
  if (brightness > LED_BRIGHTNESS_MAX) brightness = LED_BRIGHTNESS_MAX;
  strip.setBrightness(brightness);
  strip.clear();
  for (int i = 0; i < NUM_LEDS; i++) {
    if (frame.leds[i] == LED_COLOR_OFF) continue;
    strip.setPixelColor(i, resolveColor(frame.leds[i]));
  }
  strip.show();
}

static void handleEspNowPayload(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;
  if (!visualEnabled) return;

  if (len == (int)sizeof(LedFrame_t)) {
    const LedFrame_t* frame = reinterpret_cast<const LedFrame_t*>(data);
    applyFrame(*frame);
    return;
  }

  Serial.printf("[ESP-NOW] Ignored packet size=%d\n", len);
}

#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
void onEspNowReceived(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  const uint8_t* mac = (info != nullptr) ? info->src_addr : nullptr;
  handleEspNowPayload(mac, data, len);
}
#else
void onEspNowReceived(const uint8_t* mac, const uint8_t* data, int len) {
  handleEspNowPayload(mac, data, len);
}
#endif

static void sendHeartbeat() {
  ComponentStatusPacket pkt = {};
  pkt.msg_type = MSG_COMPONENT_STATUS;
  pkt.component_id = COMPONENT_LED_CONTROLLER;
  pkt.sensor_seen_mask = 0;
  pkt.flags = visualEnabled ? 0x01 : 0x00;
  esp_now_send(BROADCAST_MAC, reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
}

void setup() {
  Serial.begin(115200);

  strip.begin();
  strip.setBrightness(LED_BRIGHTNESS_DEFAULT);
  strip.show();

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  Serial.printf("\n[ESP-NOW] LED ESP32 MAC address: %s\n", WiFi.macAddress().c_str());
  Serial.printf("[ESP-NOW] Listening on channel %d\n", ESPNOW_CHANNEL);

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Init FAILED - halting");
    while (true) delay(1000);
  }

  esp_now_register_recv_cb(onEspNowReceived);

  {
    esp_now_peer_info_t broadcastPeer = {};
    memcpy(broadcastPeer.peer_addr, BROADCAST_MAC, 6);
    broadcastPeer.channel = ESPNOW_CHANNEL;
    broadcastPeer.ifidx = WIFI_IF_STA;
    broadcastPeer.encrypt = false;
    if (esp_now_add_peer(&broadcastPeer) != ESP_OK) {
      Serial.println("[ESP-NOW] Broadcast peer FAILED");
    }
  }

  Serial.println("[ESP-NOW] Ready - waiting for LedFrame_t packets");
}

void loop() {
  uint32_t nowMs = millis();
  if ((nowMs - lastHeartbeatMs) >= HEARTBEAT_PERIOD_MS) {
    lastHeartbeatMs = nowMs;
    sendHeartbeat();
  }
}
