#include <Adafruit_NeoPixel.h>

#define LED_PIN        13
#define NUMPIXELS      93
#define BRIGHTNESS     40

// Thresholds in meters
const float RED_DIST    = 0.5;
const float YELLOW_DIST = 1.0;
const float GREEN_DIST  = 1.5;
const float OFF_DIST    = 2.0;

// Sensor Pin Definitions [Trig, Echo]
int sensors[4][2] = {
  {32, 33}, // Front
  {25, 26}, // Right
  {27, 14}, // Back
  {12, 35}  // Left (GPIO 35 is Input-Only)
};

const char* labels[] = {"Front", "Right", "Back", "Left"};

Adafruit_NeoPixel ring(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Mapping for OUTSIDE-IN (Ring 0 is the 32-LED outer ring)
int ringStarts[] = {0, 32, 56, 72, 84, 92}; 
int ringSizes[]  = {32, 24, 16, 12, 8, 1};

void setup() {
  Serial.begin(115200);
  ring.begin();
  ring.setBrightness(BRIGHTNESS);
  ring.show();

  for(int i=0; i<4; i++) {
    pinMode(sensors[i][0], OUTPUT);
    pinMode(sensors[i][1], INPUT);
  }
  Serial.println("System Booted: Proximity Radar Active");
}

void loop() {
  ring.clear();

  for (int i = 0; i < 4; i++) {
    float dist = getDistance(sensors[i][0], sensors[i][1]);
    
    // Print Distance to Serial Monitor
    Serial.print(labels[i]);
    Serial.print(": ");
    Serial.print(dist);
    Serial.print("m    ");

    // Mapping: Each sensor covers 2 slices (90-degree quadrant)
    int sliceA = (i * 2 + 7) % 8;
    int sliceB = (i * 2) % 8;
    
    applyLogicToSector(sliceA, dist);
    applyLogicToSector(sliceB, dist);
  }
  
  Serial.println(); // New line for the next set of readings
  ring.show();
  
  delay(100); // Small delay to prevent ultrasonic "ghost" echoes
}

float getDistance(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  // pulseIn returns time in microseconds, 30ms timeout (~5m)
  long duration = pulseIn(echo, HIGH, 60000); 
  if (duration == 0) return 5.0; 

  return (duration * 0.000343) / 2.0;
}

void applyLogicToSector(int s, float d) {
  if (d > OFF_DIST) return; 

  if (d < RED_DIST) {
    setSliceColor(s, 4, 5, ring.Color(255, 0, 0));   // Inner Red
  } 
  else if (d < YELLOW_DIST) {
    setSliceColor(s, 2, 3, ring.Color(255, 120, 0)); // Middle Yellow
  } 
  else if (d <= GREEN_DIST) {
    setSliceColor(s, 0, 1, ring.Color(0, 255, 0));   // Outer Green
  }
}

void setSliceColor(int slice, int startRing, int endRing, uint32_t color) {
  for (int r = startRing; r <= endRing; r++) {
    int ledsInRing = ringSizes[r];
    if (ledsInRing == 1) { 
       ring.setPixelColor(92, color);
       continue;
    }
    int ledsPerSlice = ledsInRing / 8;
    int startIdx = ringStarts[r] + (slice * ledsPerSlice);
    for (int i = 0; i < ledsPerSlice; i++) {
      ring.setPixelColor(startIdx + i, color);
    }
  }
}