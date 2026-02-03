// Wiring Setup 
// Esp GND    -> LED White Wire
// Esp Pin 13 -> LED Green Wire
// Esp 5V     -> LED Red Wire

#include <Adafruit_NeoPixel.h>

#define PIN            13
#define NUMPIXELS      93
#define BRIGHTNESS     40


Adafruit_NeoPixel ring(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// Playback data extracted from your CSV (Filtered to remove static chassis points)
// Format: {Front, FL, Left, BL, Back, BR, Right, FR}
float simDistances[14][8] = {
  { 1.68f, 1.86f, 1.76f, 1.11f, 1.00f, 1.11f, 1.76f, 1.86f }, // 0s
  { 1.68f, 1.86f, 1.76f, 1.23f, 1.28f, 1.23f, 1.76f, 1.86f }, // 1s
  { 1.68f, 1.86f, 1.76f, 1.23f, 1.28f, 1.23f, 1.76f, 1.86f }, // 2s
  { 1.68f, 1.86f, 1.76f, 1.23f, 1.28f, 1.23f, 1.76f, 1.86f }, // 3s
  { 1.68f, 1.86f, 1.76f, 1.23f, 1.28f, 1.23f, 1.76f, 1.86f }, // 4s
  { 1.68f, 1.86f, 1.76f, 1.23f, 1.28f, 1.23f, 1.76f, 1.86f }, // 5s
  { 1.68f, 1.86f, 1.76f, 1.23f, 1.28f, 1.23f, 1.76f, 1.86f }, // 6s
  { 1.68f, 1.86f, 1.76f, 1.23f, 1.28f, 1.23f, 1.76f, 1.86f }, // 7s
  { 1.68f, 1.86f, 1.76f, 1.23f, 1.28f, 1.23f, 1.76f, 1.86f }, // 8s
  { 1.27f, 1.49f, 1.76f, 1.23f, 1.28f, 1.23f, 1.76f, 1.49f }, // 9s
  { 1.05f, 1.86f, 1.76f, 1.23f, 1.28f, 1.23f, 1.15f, 1.00f }, // 10s: WALL MOVES TO RIGHT
  { 1.68f, 1.86f, 1.76f, 1.23f, 1.28f, 1.16f, 1.00f, 1.18f }, // 11s
  { 1.68f, 1.50f, 1.76f, 1.23f, 1.28f, 1.16f, 1.00f, 1.18f }, // 12s
  { 1.68f, 1.22f, 1.09f, 1.23f, 1.28f, 1.16f, 1.00f, 1.18f }  // 13s
};

int ringStarts[] = {0, 32, 56, 72, 84, 92}; 
int ringSizes[]  = {32, 24, 16, 12, 8, 1};

void setup() {
  ring.begin();
  ring.setBrightness(BRIGHTNESS);
  ring.show();
  Serial.begin(115200);
}

void loop() {
  static int currentSecond = 0;
  Serial.print("Simulating Second: "); Serial.println(currentSecond);

  updateRadar(simDistances[currentSecond]);
  ring.show();

  currentSecond++;
  if (currentSecond >= 14) currentSecond = 0; // Loop the 14-second simulation
  
  delay(1000); // 1-second update interval
}

void updateRadar(float distances[8]) {
  ring.clear();
  for (int s = 0; s < 8; s++) {
    float d = distances[s];
    
    if (d > 3.0) continue; // Off

    // RED: < 1.0m
    if (d < 1.0) {
      setSliceColor(s, 4, 5, ring.Color(255, 0, 0));
    }
    // YELLOW: 1.0m to 2.0m
    else if (d < 2.0) {
      setSliceColor(s, 2, 3, ring.Color(255, 120, 0));
    }
    // GREEN: 2.0m to 3.0m
    else if (d <= 3.0) {
      setSliceColor(s, 0, 1, ring.Color(0, 255, 0));
    }
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