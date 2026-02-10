#include <esp_now.h>
#include <WiFi.h>

// ====== CHANGE THESE LINES ONLY ======
const int NUM_SENSORS = 2;
const uint8_t SENSOR_IDS[NUM_SENSORS] = {5, 6};
// =====================================

// Must match the sender EXACTLY
struct SensorPacket
{
    uint8_t sensor_id;
    uint32_t timestamp_ms;
    uint16_t distance_mm[16];
    uint8_t target_status[16];
    uint8_t nb_targets[16];
} __attribute__((packed));

SensorPacket SensorData[NUM_SENSORS];
bool dataReady[NUM_SENSORS] = {false};

// Find array index for a given sensor_id. Returns -1 if not in the list.
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

    SensorData[index] = recPkt;
    dataReady[index] = true;

    Serial.print("Sensor ");
    Serial.print(recPkt.sensor_id);
    Serial.println(" Data Received");
}

void printCsvRow()
{
    Serial.printf("%lu,", (unsigned long)SensorData[0].timestamp_ms);

    // Distances
    for (int s = 0; s < NUM_SENSORS; s++)
        for (int i = 0; i < 16; i++)
            Serial.printf("%u,", (unsigned)SensorData[s].distance_mm[i]);

    // Status
    for (int s = 0; s < NUM_SENSORS; s++)
        for (int i = 0; i < 16; i++)
            Serial.printf("%u,", (unsigned)SensorData[s].target_status[i]);

    Serial.println();
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
        printCsvRow();

        for (int i = 0; i < NUM_SENSORS; i++)
            dataReady[i] = false;
    }
}
