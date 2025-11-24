
 
// Include Libraries
#include <esp_now.h>
#include <WiFi.h>
 
//Globals
uint16_t cmdSeq = 0;
bool s1DataReady = false;
bool s2DataReady = false;
bool s3DataReady = false;
bool s4DataReady = false;
bool s5DataReady = false;
bool s6DataReady = false;

const uint8_t podMac[6] = {0x88, 0x88, 0x88, 0x88, 0x88}; //Just a placeholder need to replace

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

enum ControlCmd : uint8_t
{
  CMD_NOP = 0,
  CMD_START = 1,
  CMD_STOP = 2,
  CMD_POLL_FOR_MS = 3
};

struct CommandPacket
{
  uint8_t version = 1;
  uint8_t cmd; // ControlCmd
  uint16_t seq;
  uint32_t value; // e.g., duration_ms for CMD_POLL_FOR_MS
} __attribute__((packed));

// Create a structured object
SensorPacket recvData;
SensorPacket Sensor1Data;
SensorPacket Sensor2Data;
SensorPacket Sensor3Data;
SensorPacket Sensor4Data;
SensorPacket Sensor5Data;
SensorPacket Sensor6Data;

bool addEspNowPeer(const uint8_t *mac)
{
  esp_now_peer_info_t peer{};
  memcpy(peer.peer_addr, mac, 6);
  peer.ifidx = WIFI_IF_STA;
  peer.channel = 0; // match current channel
  peer.encrypt = false;
  bool paired = esp_now_add_peer(&peer);
  if (paired)
  {
    Serial.println("Peer Added");
  }
  return paired;
}

void printSensorData(SensorPacket data){
  Serial.printf("Sensor ID: %d| Time: %d\n",data.sensor_id,data.timestamp_ms);
  for (int i = 0; i < 16; i++){
    Serial.printf("Zone %d| Dist: %d, Targets %d ,Status: %d\n",i+1, data.distance_mm[i], data.nb_targets[i], data.target_status[i]);
  }
}

// Call once in setup()
void printCsvHeader() {
  Serial.print("time_ms");
  for (int i = 0; i < 16; i++) Serial.printf(",sensor1_zone%d_distance", i);
  for (int i = 0; i < 16; i++) Serial.printf(",sensor2_zone%d_distance", i);
  for (int i = 0; i < 16; i++) Serial.printf(",sensor1_zone%d_status", i);
  for (int i = 0; i < 16; i++) Serial.printf(",sensor2_zone%d_status", i);
  Serial.println();
}

// Print a row once you have current data for both sensors
void printCsvRow(const SensorPacket &s1, const SensorPacket &s2) {
  Serial.printf("%u", s1.timestamp_ms);
  for (int i = 0; i < 16; ++i) Serial.printf(",%u", (unsigned)s1.distance_mm[i]);
  for (int i = 0; i < 16; ++i) Serial.printf(",%u", (unsigned)s2.distance_mm[i]);
  for (int i = 0; i < 16; ++i) Serial.printf(",%u", (unsigned)s1.target_status[i]);
  for (int i = 0; i < 16; ++i) Serial.printf(",%u", (unsigned)s2.target_status[i]);
  Serial.println();
}


void sendPollForMS(uint32_t durationMs) {
  CommandPacket pkt{};
  pkt.cmd = CMD_POLL_FOR_MS;
  pkt.seq = ++cmdSeq;
  pkt.value = durationMs;
  esp_err_t r = esp_now_send(podMac, (uint8_t*)&pkt, sizeof(pkt));
  Serial.printf("Control send POLL_FOR %u ms seq= %u status= %d\n", durationMs, pkt.seq, r);
  printCsvHeader();
}

void sendStartPolling(){
  CommandPacket pkt{};
  pkt.cmd = CMD_START;
  pkt.seq = ++cmdSeq;
  pkt.value = 0;
  esp_err_t r = esp_now_send(podMac, (uint8_t*)&pkt, sizeof(pkt));
  Serial.printf("Command send to START POLLING seq= %u status= %d\n",pkt.seq,r);
  printCsvHeader();
}

void sendStopPolling(){
  CommandPacket pkt{};
  pkt.cmd = CMD_STOP;
  pkt.seq = ++cmdSeq;
  pkt.value = 0;
  esp_err_t r = esp_now_send(podMac, (uint8_t*)&pkt, sizeof(pkt));
  Serial.printf("Command send to STOP POLLING seq= %u status= %d\n",pkt.seq,r);
}


// Callback function executed when data is received
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len){

  if(len != sizeof(SensorPacket)) return;

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


// Call from loop()
void handleSerialCommands()
{
  if (!Serial.available())
    return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  line.toLowerCase();

  if (line == "start" || line == "s")
  {
    sendStartPolling();
  }
  else if (line == "stop" || line == "x")
  {
    sendStopPolling();
  }
  else if (line.startsWith("poll "))
  {
    uint32_t dur = line.substring(5).toInt(); // ms
    if (dur > 0)
      sendPollForMS(dur);
  }
  else
  {
    Serial.println("Commands: start|s, stop|x, poll <ms>");
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
    addEspNowPeer(podMac);


  }

  void loop()
  {
    handleSerialCommands();

    if (s1DataReady && s2DataReady)
    {
      printCsvRow(Sensor1Data, Sensor2Data);
      s1DataReady = false;
      s2DataReady = false;
    }
    
  }