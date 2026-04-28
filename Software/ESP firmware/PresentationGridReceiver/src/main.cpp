#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#if __has_include(<esp_idf_version.h>)
#include <esp_idf_version.h>
#endif

#define ESPNOW_CHANNEL 1
#define SERIAL_BAUD 230400

const uint8_t MSG_PRESENTATION_POLAR_GRID = 0xB7;
static constexpr uint8_t PRESENTATION_RING_COUNT = 12;
static constexpr uint8_t PRESENTATION_ZONE_COUNT = 24;
static constexpr int PRESENTATION_CELL_COUNT = PRESENTATION_RING_COUNT * PRESENTATION_ZONE_COUNT;
static constexpr int PRESENTATION_OWNER_PACKED_BYTES = (PRESENTATION_CELL_COUNT + 1) / 2;

struct __attribute__((packed)) PresentationPolarGridPacket
{
  uint8_t  msg_type;
  uint8_t  ring_count;
  uint8_t  zone_count;
  uint8_t  reserved;
  uint16_t frame_seq;
  uint32_t source_ms;
  uint8_t  packed_owner[PRESENTATION_OWNER_PACKED_BYTES];
};

static void printHexByte(uint8_t value)
{
  static const char kHex[] = "0123456789ABCDEF";
  Serial.write(kHex[(value >> 4) & 0x0F]);
  Serial.write(kHex[value & 0x0F]);
}

static void emitPresentationFrame(const PresentationPolarGridPacket& pkt)
{
  Serial.print("PG,");
  Serial.print((unsigned int)pkt.frame_seq);
  Serial.print(",");
  Serial.print((unsigned long)pkt.source_ms);
  Serial.print(",");
  Serial.print((unsigned int)pkt.ring_count);
  Serial.print(",");
  Serial.print((unsigned int)pkt.zone_count);
  Serial.print(",");
  for (int i = 0; i < PRESENTATION_OWNER_PACKED_BYTES; i++) printHexByte(pkt.packed_owner[i]);
  Serial.println();
}

static void handleEspNowPayload(const uint8_t* mac, const uint8_t* data, int len)
{
  (void)mac;
  if (data == nullptr || len != (int)sizeof(PresentationPolarGridPacket)) return;

  PresentationPolarGridPacket pkt;
  memcpy(&pkt, data, sizeof(pkt));
  if (pkt.msg_type != MSG_PRESENTATION_POLAR_GRID) return;
  if (pkt.ring_count != PRESENTATION_RING_COUNT || pkt.zone_count != PRESENTATION_ZONE_COUNT) return;
  emitPresentationFrame(pkt);
}

#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
void onEspNowReceived(const esp_now_recv_info_t* info, const uint8_t* data, int len)
{
  const uint8_t* mac = (info != nullptr) ? info->src_addr : nullptr;
  handleEspNowPayload(mac, data, len);
}
#else
void onEspNowReceived(const uint8_t* mac, const uint8_t* data, int len)
{
  handleEspNowPayload(mac, data, len);
}
#endif

void setup()
{
  Serial.begin(SERIAL_BAUD);
  delay(200);
  Serial.println();
  Serial.println("[PresentationGridReceiver] Starting");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  Serial.printf("[ESP-NOW] Receiver MAC: %s\n", WiFi.macAddress().c_str());
  Serial.printf("[ESP-NOW] Listening on channel %d\n", ESPNOW_CHANNEL);

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("[ESP-NOW] Init FAILED");
    while (true) delay(1000);
  }

  esp_now_register_recv_cb(onEspNowReceived);
  Serial.println("[ESP-NOW] Ready for presentation polar grid packets");
}

void loop()
{
  delay(1);
}
