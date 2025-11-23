
 
// Include Libraries
#include <esp_now.h>
#include <WiFi.h>
 
// Define a data structure
struct SensorPacket
{
  uint8_t sensor_id;
  uint32_t timestamp_ms;
  uint16_t distance_mm[16];
  uint8_t target_status[16];
  uint8_t nb_targets[16];
} __attribute__((packed));

// Create a structured object
SensorPacket recvData;
SensorPacket Sensor1Data;
SensorPacket Sensor2Data;

void printSensorData(SensorPacket data){
  Serial.printf("Sensor ID: %d| Time: %d\n",data.sensor_id,data.timestamp_ms);
  for (int i = 0; i < 16; i++){
    Serial.printf("Zone %d| Dist: %d, Targets %d ,Status: %d\n",i+1, data.distance_mm[i], data.nb_targets[i], data.target_status[i]);
  }
}

// Callback function executed when data is received
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len){

  // --- Step 1: Temporarily cast the incoming data pointer for immediate inspection ---
  // We use this pointer *only* to read the ID quickly before copying.
  SensorPacket *tempPtr = (SensorPacket *)incomingData;
  uint8_t senderId = tempPtr->sensor_id;

  // --- Step 2: Decide where to save the data based on the ID ---
  switch (senderId)
  {
  case 1:
    // If the ID is 1, copy the data into the Sensor1Data structure
    { // Optional: Safety check the length
      memcpy(&Sensor1Data, incomingData, sizeof(SensorPacket));
      printSensorData(Sensor1Data);
    }
    break;
  case 2:
    // If the ID is 2, copy the data into the Sensor2Data structure
    {
      memcpy(&Sensor2Data, incomingData, sizeof(SensorPacket));
      printSensorData(Sensor2Data);
    }
    break;
  default:
    // Handle unknown IDs if necessary
    Serial.println("Received data from unknown sensor ID");
    break;
  }
}

  void setup()
  {
    // Set up Serial Monitor
    Serial.begin(115200);
    Serial.println("Reciever Serial Started");

    // Set ESP32 as a Wi-Fi Station
    WiFi.mode(WIFI_STA);

    // Initilize ESP-NOW
    if (esp_now_init() != ESP_OK)
    {
      Serial.println("Error initializing ESP-NOW");
      return;
    }

    // Register callback function
    esp_now_register_recv_cb(OnDataRecv);
  }

  void loop()
  {
  }