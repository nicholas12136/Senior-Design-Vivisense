// Include Libraries
#include <esp_now.h>
#include <WiFi.h>

// Globals
uint16_t cmdSeq = 0;
bool s1DataReady = false;
bool s2DataReady = false;
bool s3DataReady = false;
bool s4DataReady = false;
bool s5DataReady = false;
bool s6DataReady = false;

// Define a data structure
struct SensorPacket
{
    uint8_t sensor_id;
    uint8_t res;
    uint32_t timestamp_ms;
    uint16_t distance_mm[16];
    uint8_t target_status[16];
    uint8_t nb_targets[16];
} __attribute__((packed));

// Create a structured object
SensorPacket recvData;
SensorPacket Sensor1Data;
SensorPacket Sensor2Data;
SensorPacket Sensor3Data;
SensorPacket Sensor4Data;
SensorPacket Sensor5Data;
SensorPacket Sensor6Data;

// Callback function executed when data is received
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{

    if (len != sizeof(SensorPacket))
        return;

    SensorPacket recPkt;
    memcpy(&recPkt, incomingData, sizeof(CommandPacket));
    uint8_t senderId = recPkt.sensor_id;

    // --- Step 2: Decide where to save the data based on the ID ---
    switch (senderId)
    {
    case 1:
        // If the ID is 1, copy the data into the Sensor1Data structure
        Sensor1Data = recPkt;
        s1DataReady = true;
        break;
    case 2:
        Sensor2Data = recPkt;
        s2DataReady = true;
        break;
    case 3:
        Sensor3Data = recPkt;
        s3DataReady = true;
        break;
    case 4:
        Sensor4Data = recPkt;
        s4DataReady = true;
        break;
    case 5:
        Sensor5Data = recPkt;
        s5DataReady = true;
        break;
    case 6:
        Sensor6Data = recPkt;
        s6DataReady = true;
        break;
    default:
        // Handle unknown IDs if necessary
        Serial.println("Received data from unknown sensor ID");
        break;
    }
}

// Print a row once you have current data for all 6 sensors
void printCsvRow(const SensorPacket &s1, const SensorPacket &s2, const SensorPacket &s3,
                 const SensorPacket &s4, const SensorPacket &s5, const SensorPacket &s6)
{
    Serial.printf("%u,", s1.timestamp_ms); // assumes samples are time-aligned
    const SensorPacket sensors[6] = {s1, s2, s3, s4, s5, s6};
    // Distances for all sensors
    for (int s = 0; s < 6; ++s)
    {
        for (int i = 0; i < 16; ++i)
        {
            Serial.printf("%u,", (unsigned)sensors[s].distance_mm[i]);
        }
    }
    // Target statuses for all sensors
    for (int s = 0; s < 6; ++s)
    {
        for (int i = 0; i < 16; ++i)
        {
            Serial.printf("%u,", (unsigned)sensors[s].target_status[i]);
        }
    }
    Serial.println();
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

    if (s1DataReady && s2DataReady && s3DataReady && s4DataReady && s5DataReady && s6DataReady)
    {
        printCsvRow(Sensor1Data, Sensor2Data, Sensor3Data, Sensor4Data, Sensor5Data, Sensor6Data);
        s1DataReady = false;
        s2DataReady = false;
        s3DataReady = false;
        s4DataReady = false;
        s5DataReady = false;
        s6DataReady = false;
    }
}