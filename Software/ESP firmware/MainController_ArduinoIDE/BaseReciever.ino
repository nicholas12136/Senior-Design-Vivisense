#include <esp_now.h>
#include <WiFi.h>
#include "SensorAndPoint.h"

// ====== CHANGE THESE LINES ONLY ======
const int NUM_SENSORS = 4;
const uint8_t SENSOR_IDS[NUM_SENSORS] = {5, 6, 7, 8};

struct SensorConfig
{
  uint8_t sensorId;

  // mm
  float x_off_mm;
  float y_off_mm;
  float z_off_mm;

  // DEGREES (we will convert to radians in setup)
  float alpha_deg;
  float beta_deg;
  float gamma_deg;
};

SensorConfig SENSOR_CONFIGS[NUM_SENSORS] =
{
  //ID   x      y      z      alpha  beta   gamma
  {5,    0.0f,  -63.0f, 1500.0f,  10.0f, 20.0f,  -90.0f},
  {6,  -50.0f,  -12.5f, 1500.0f,   0.0f, 20.0f, -120.0f},
  {7,  -50.0f, 25.0f, 1500.0f,   180.0f, 20.0f,  120.0f},
  {8,    0.0f, 63.0f, 1500.0f, 180.0f,20.0f,   90.0f},
};
// =====================================

// Must match the sender EXACTLY
struct SensorPacket
{
  uint8_t  sensor_id;
  uint32_t timestamp_ms;
  uint16_t distance_mm[16];
  uint8_t  target_status[16];
  uint8_t  nb_targets[16];
} __attribute__((packed));

Sensor sensors[NUM_SENSORS];
bool dataReady[NUM_SENSORS] = {false};
uint32_t lastTimestampMs[NUM_SENSORS] = {0};

int findSensorIndex(uint8_t id)
{
  for (int i = 0; i < NUM_SENSORS; i++)
  {
    if (SENSOR_IDS[i] == id)
      return i;
  }
  return -1;
}

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len)
{
  if (len != sizeof(SensorPacket))
    return;

  SensorPacket recPkt;
  memcpy(&recPkt, incomingData, sizeof(SensorPacket));

  int index = findSensorIndex(recPkt.sensor_id);
  if (index == -1)
  {
    Serial.print("Unknown sensor ID: ");
    Serial.println(recPkt.sensor_id);
    return;
  }

  sensors[index].setAllCellData(recPkt.distance_mm, recPkt.target_status, recPkt.nb_targets);

  lastTimestampMs[index] = recPkt.timestamp_ms;
  dataReady[index] = true;
}

void printPointCsv(uint32_t timestampMs, const Point &p)
{
  if (!p.isValid) return;

  Serial.print("P,");
  Serial.print(timestampMs); Serial.print(",");
  Serial.print(p.sensorId);  Serial.print(",");
  Serial.print(p.cellId);    Serial.print(",");
  Serial.print(p.worldX, 1); Serial.print(",");
  Serial.print(p.worldY, 1); Serial.print(",");
  Serial.print(p.worldZ, 1); Serial.print(",");
  Serial.print(p.targetStatus); Serial.print(",");
  Serial.println(p.numTargets);
}

void sendFrameToPc(uint32_t timestampMs)
{
  for (int s = 0; s < NUM_SENSORS; s++)
  {
    for (int cell = 0; cell < 16; cell++)
    {
      // Min distance filter in mm
      Point p = sensors[s].createWorldPointFromCell(cell, 50);
      printPointCsv(timestampMs, p);
    }
  }

  Serial.print("E,");
  Serial.println(timestampMs);
}

void setup()
{
  Serial.begin(115200);
  Serial.println("Receiver Serial Started");

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  // Configure sensors with per-sensor offsets
  for (int i = 0; i < NUM_SENSORS; i++)
  {
    float alphaRad = degreesToRadians(SENSOR_CONFIGS[i].alpha_deg);
    float betaRad  = degreesToRadians(SENSOR_CONFIGS[i].beta_deg);
    float gammaRad = degreesToRadians(SENSOR_CONFIGS[i].gamma_deg);

    sensors[i].configureSensor(
      SENSOR_CONFIGS[i].sensorId,
      SENSOR_CONFIGS[i].x_off_mm,
      SENSOR_CONFIGS[i].y_off_mm,
      SENSOR_CONFIGS[i].z_off_mm,
      alphaRad,
      betaRad,
      gammaRad
    );
  }

  Serial.println("Sensors configured.");
}

void loop()
{
  bool allReady = true;
  for (int i = 0; i < NUM_SENSORS; i++)
  {
    if (!dataReady[i])
    {
      allReady = false;
      break;
    }
  }

  if (allReady)
  {
    // Pick a timestamp to label the frame.
    // This is simplest: use the newest timestamp we have.
    uint32_t frameTs = lastTimestampMs[0];
    for (int i = 1; i < NUM_SENSORS; i++)
      if (lastTimestampMs[i] > frameTs) frameTs = lastTimestampMs[i];

    sendFrameToPc(frameTs);

    for (int i = 0; i < NUM_SENSORS; i++)
      dataReady[i] = false;
  }
}
