/*
 * ViviSense - ESP32 Soft AP Web Server (Voice + Enhanced UI)
 * * HARDWARE: ESP32 + MAX98357A I2S Amp
 * * REQ: You must have 'sounds.h' in the sketch folder.
 */

#include <WiFi.h>
#include "driver/i2s.h"
#include "sounds.h" // Holds your converted audio arrays

// --- I2S Pin Definitions (MAX98357A) ---
#define I2S_BCK_IO      27
#define I2S_WS_IO       26
#define I2S_DO_IO       25
#define I2S_NUM         I2S_NUM_0

// --- Network Definitions ---
const char* ssid = "ESP32-Network";
const char* password = "Esp32-Password";
WiFiServer server(80);
String header;

// --- Global State ---
int currentVolume = 200; // 0-255
unsigned long currentTime = millis();
unsigned long previousTime = 0;
const long timeoutTime = 2000;

// --- AUDIO ENGINE ---
void setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 16000, // Default, changes dynamically based on file
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT, // Mono
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false
  };
  i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCK_IO,
    .ws_io_num = I2S_WS_IO,
    .data_out_num = I2S_DO_IO,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_set_pin(I2S_NUM, &pin_config);
}

void playAudio(const unsigned char* audioData, unsigned int dataLen, unsigned int sampleRate) {
  if (dataLen == 0) return; 
  
  i2s_set_sample_rates(I2S_NUM, sampleRate);
  
  size_t bytes_written;
  int16_t sampleBuffer[64]; 
  int bufferIdx = 0;
  float volFactor = (float)currentVolume / 255.0;

  for (unsigned int i = 0; i < dataLen; i += 2) {
    uint8_t low = pgm_read_byte(&audioData[i]);
    uint8_t high = pgm_read_byte(&audioData[i + 1]);
    
    int16_t sample = (int16_t)((high << 8) | low);
    sample = (int16_t)(sample * volFactor);
    
    sampleBuffer[bufferIdx++] = sample;
    
    if (bufferIdx >= 64) {
      i2s_write(I2S_NUM, sampleBuffer, sizeof(sampleBuffer), &bytes_written, portMAX_DELAY);
      bufferIdx = 0;
    }
  }
  
  if (bufferIdx > 0) {
    i2s_write(I2S_NUM, sampleBuffer, bufferIdx * 2, &bytes_written, portMAX_DELAY);
  }
  i2s_zero_dma_buffer(I2S_NUM);
}

// --- BUTTON HANDLERS (Updated to use playAudio) ---

void handleStop() {
  Serial.println("Action: STOP");
  // Assuming 'stop_data' exists in sounds.h
  playAudio(stop_data, stop_len, stop_rate); 
}

// void handleSpeedUp() {
//   Serial.println("Action: Speed Up");
//   // playAudio(speedup_data, speedup_len, speedup_rate); 
// }

// void handleSlowDown() {
//   Serial.println("Action: Slow Down");
//   playAudio(slow_data, slow_len, slow_rate);
// }

// void handleGoForward() {
//   Serial.println("Action: Forward");
//   playAudio(forward_data, forward_len, forward_rate);
// }

// void handleTurnLeft() {
//   Serial.println("Action: Left");
//   playAudio(left_data, left_len, left_rate);
// }

// void handleTurnRight() {
//   Serial.println("Action: Right");
//   playAudio(right_data, right_len, right_rate);
// }

// void handleBackUp() {
//   Serial.println("Action: Back Up");
//   playAudio(backup_data, backup_len, backup_rate);
// }

// Placeholders for other buttons
void handleHoldToSpeak() {} 
void handleCustomButton1() {}
void handleCustomButton2() {}
void handleCustomButton3() {}
void handleCustomButton4() {}
void handleCustomButton5() {}
void handleCustomButton6() {}
void handleCustomButton7() {}
void handleCustomButton8() {}


void setup() {
  Serial.begin(115200);
  setupI2S(); // Start the Audio Driver

  WiFi.softAP(ssid, password);
  Serial.println("Access Point Started");
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP());
  server.begin();
}

void loop() {
  WiFiClient client = server.available();

  if (client) {
    currentTime = millis();
    previousTime = currentTime;
    String currentLine = "";

    while (client.connected() && currentTime - previousTime <= timeoutTime) {
      currentTime = millis();
      if (client.available()) {
        char c = client.read();
        header += c;
        if (c == '\n') {
          if (currentLine.length() == 0) {
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            // --- COMMAND HANDLING ---
            if (header.indexOf("GET /volume?level=") >= 0) {
              int levelPos = header.indexOf('=');
              currentVolume = header.substring(levelPos + 1).toInt();
            }
            else if (header.indexOf("GET /stop") >= 0) handleStop();
            //else if (header.indexOf("GET /slowdown") >= 0) handleSlowDown();
            //else if (header.indexOf("GET /forward") >= 0) handleGoForward();
            //else if (header.indexOf("GET /left") >= 0) handleTurnLeft();
            //else if (header.indexOf("GET /right") >= 0) handleTurnRight();
            //else if (header.indexOf("GET /backup") >= 0) handleBackUp();
            // Add other custom buttons here...

            // --- RESTORED HTML UI ---
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">");
            
            // CSS
            client.println("<style>");
            client.println("* { box-sizing: border-box; margin: 0; padding: 0; }");
            client.println("html, body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background-color: #f5f5f5; height: 100%; }");
            client.println("a { text-decoration: none; color: inherit; }");
            client.println("body { max-width: 480px; margin: 0 auto; background: #fff; box-shadow: 0 0 20px rgba(0,0,0,0.1); display: flex; flex-direction: column; min-height: 100vh; }");
            client.println(".header { padding: 12px 16px; display: flex; align-items: center; justify-content: space-between; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; box-shadow: 0 2px 8px rgba(0,0,0,0.1); }");
            client.println(".app-name { font-size: 20px; font-weight: 700; letter-spacing: 0.5px; }");
            client.println(".main-content { flex: 1; padding: 24px 20px; display: flex; flex-direction: column; gap: 24px; }");
            client.println(".control-panel { display: flex; align-items: center; justify-content: space-between; gap: 16px; }");
            
            // Speed Controls
            client.println(".speed-controls { display: flex; flex-direction: column; gap: 12px; }");
            client.println(".speed-btn { display: flex; flex-direction: column; align-items: center; justify-content: center; width: 70px; height: 90px; background: linear-gradient(135deg, #f8f9fa 0%, #e9ecef 100%); border: 2px solid #dee2e6; border-radius: 12px; box-shadow: 0 2px 8px rgba(0,0,0,0.08); }");
            client.println(".arrow-up { width: 0; height: 0; border-left: 16px solid transparent; border-right: 16px solid transparent; border-bottom: 24px solid #495057; }");
            client.println(".arrow-down { width: 0; height: 0; border-left: 16px solid transparent; border-right: 16px solid transparent; border-top: 24px solid #495057; }");

            // D-Pad
            client.println(".d-pad { width: 220px; height: 220px; position: relative; flex-shrink: 0; }");
            client.println(".d-pad-segment { position: absolute; width: 100%; height: 100%; clip-path: polygon(50% 50%, 50% 0%, 100% 0%, 100% 50%); }");
            client.println(".d-pad-segment-inner { width: 100%; height: 100%; background: linear-gradient(135deg, #f8f9fa 0%, #e9ecef 100%); border: 3px solid #dee2e6; border-radius: 50%; }");
            client.println(".segment-top { transform: rotate(-45deg); }");
            client.println(".segment-right { transform: rotate(45deg); }");
            client.println(".segment-bottom { transform: rotate(135deg); }");
            client.println(".segment-left { transform: rotate(225deg); }");
            client.println(".segment-label { position: absolute; font-weight: 600; font-size: 11px; color: #495057; pointer-events: none; text-align: center; }");
            client.println(".label-top { top: 10px; left: 50%; transform: translateX(-50%); }");
            client.println(".label-right { right: 10px; top: 50%; transform: translateY(-50%); }");
            client.println(".label-bottom { bottom: 10px; left: 50%; transform: translateX(-50%); }");
            client.println(".label-left { left: 10px; top: 50%; transform: translateY(-50%); }");
            client.println(".center-btn { width: 120px; height: 120px; position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); border-radius: 50%; background: linear-gradient(135deg, #ef4444 0%, #dc2626 100%); color: white; z-index: 10; font-size: 22px; font-weight: 700; border: 4px solid white; display: flex; align-items: center; justify-content: center; }");

            // Mic
            client.println(".mic-container { display: flex; flex-direction: column; align-items: center; gap: 8px; }");
            client.println(".mic-btn { width: 80px; height: 80px; background: linear-gradient(135deg, #f8f9fa 0%, #e9ecef 100%); border: 2px solid #dee2e6; border-radius: 50%; display: flex; align-items: center; justify-content: center; }");

            // Volume
            client.println(".volume-container { padding: 0 4px; }");
            client.println(".volume-label { display: block; text-align: center; font-size: 13px; font-weight: 600; margin-bottom: 12px; color: #495057; }");
            client.println("input[type='range'] { width: 100%; }");

            // Custom Grid
            client.println(".custom-grid { padding: 0 4px 20px 4px; display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; }");
            client.println(".custom-btn { height: 70px; background: linear-gradient(135deg, #ffffff 0%, #f8f9fa 100%); border: 2px solid #dee2e6; border-radius: 12px; display: flex; align-items: center; justify-content: center; text-align: center; font-size: 11px; font-weight: 600; padding: 8px; color: #495057; }");
            
            client.println("</style>");
            
            // Script for Slider
            client.println("<script>function updateVolume(val){fetch('/volume?level='+val);}</script>");
            client.println("</head><body>");
            
            // Header
            client.println("<div class='header'><span class='app-name'>ViviSense</span></div>");
            
            client.println("<div class='main-content'>");
            client.println("<div class='control-panel'>");
            
            // Speed Controls
            client.println("<div class='speed-controls'>");
            client.println("<a href='/speedup'><div class='speed-btn'><div class='arrow-up'></div></div></a>");
            client.println("<a href='/slowdown'><div class='speed-btn'><div class='arrow-down'></div></div></a>");
            client.println("</div>");
            
            // D-Pad
            client.println("<div class='d-pad'>");
            client.println("<a href='/forward'><div class='d-pad-segment segment-top'><div class='d-pad-segment-inner'></div></div><div class='segment-label label-top'>Go Forward</div></a>");
            client.println("<a href='/right'><div class='d-pad-segment segment-right'><div class='d-pad-segment-inner'></div></div><div class='segment-label label-right'>Right</div></a>");
            client.println("<a href='/backup'><div class='d-pad-segment segment-bottom'><div class='d-pad-segment-inner'></div></div><div class='segment-label label-bottom'>Back Up</div></a>");
            client.println("<a href='/left'><div class='d-pad-segment segment-left'><div class='d-pad-segment-inner'></div></div><div class='segment-label label-left'>Left</div></a>");
            client.println("<a href='/stop'><div class='center-btn'>Stop!</div></a>");
            client.println("</div>");
            
            // Mic
            client.println("<a href='/speak'><div class='mic-container'><div class='mic-btn'>Mic</div></div></a>");
            client.println("</div>"); // End control panel
            
            // Volume
            client.println("<div class='volume-container'>");
            client.println("<label class='volume-label'>Volume Control</label>");
            client.println("<input type='range' min='0' max='255' value='" + String(currentVolume) + "' onchange='updateVolume(this.value)'>");
            client.println("</div>");

            // Custom Buttons
            client.println("<div class='custom-grid'>");
            client.println("<a href='/custom1'><div class='custom-btn'>Custom 1</div></a>");
            client.println("<a href='/custom2'><div class='custom-btn'>Custom 2</div></a>");
            client.println("<a href='/custom3'><div class='custom-btn'>Custom 3</div></a>");
            client.println("<a href='/custom4'><div class='custom-btn'>Custom 4</div></a>");
            client.println("</div>");
            
            client.println("</div></body></html>");
            client.println();
            break;
          } else {
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }
    header = "";
    client.stop();
  }
}