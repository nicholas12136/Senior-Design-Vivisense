/*
 * ViviSense - ESP32 Soft AP Web Server (Restored UI + Voice Support)
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
int currentVolume = 255; // 0-255
unsigned long currentTime = millis();
unsigned long previousTime = 0;
const long timeoutTime = 2000;

// ============================================
// AUDIO ENGINE (I2S)
// ============================================

void setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 16000, 
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    // CHANGE: Use RIGHT_LEFT so the buffer sends to both channels
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, 
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 128, // Slightly larger buffer for stability
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
  // We double the buffer size because we are sending the same sample twice (Left + Right)
  int16_t sampleBuffer[128]; 
  int bufferIdx = 0;
  float volFactor = (float)currentVolume / 255.0;

  for (unsigned int i = 0; i < dataLen; i += 2) {
    uint8_t low = pgm_read_byte(&audioData[i]);
    uint8_t high = pgm_read_byte(&audioData[i + 1]);
    
    int16_t sample = (int16_t)((high << 8) | low);
    sample = (int16_t)(sample * volFactor);
    
    // PUSH THE SAME SAMPLE TWICE (Once for Left, once for Right)
    sampleBuffer[bufferIdx++] = sample; // Left
    sampleBuffer[bufferIdx++] = sample; // Right
    
    if (bufferIdx >= 128) {
      i2s_write(I2S_NUM, sampleBuffer, sizeof(sampleBuffer), &bytes_written, portMAX_DELAY);
      bufferIdx = 0;
    }
  }
  
  if (bufferIdx > 0) {
    i2s_write(I2S_NUM, sampleBuffer, bufferIdx * 2, &bytes_written, portMAX_DELAY);
  }
  i2s_zero_dma_buffer(I2S_NUM);
}

// ============================================
// BUTTON ACTIONS (Trigger Audio)
// ============================================

void handleStop() {
  Serial.println("Action: STOP");
  playAudio(stop_data, stop_len, stop_rate); 
}

void handleSpeedUp() {
  Serial.println("Action: Speed Up");
  playAudio(speed_up_data, speed_up_len, speed_up_rate); 
}

void handleSlowDown() {
  Serial.println("Action: Slow Down");
  playAudio(slow_down_data, slow_down_len, slow_down_rate);
}

void handleGoForward() {
  Serial.println("Action: Forward");
  playAudio(go_forward_data, go_forward_len, go_forward_rate);
}

void handleTurnLeft() {
  Serial.println("Action: Left");
  playAudio(turn_left_data, turn_left_len, turn_left_rate);
}

void handleTurnRight() {
  Serial.println("Action: Right");
  playAudio(turn_right_data, turn_right_len, turn_right_rate);
}

void handleBackUp() {
  Serial.println("Action: Back Up");
  playAudio(back_up_data, back_up_len, back_up_rate);
}

// Placeholders (Add audio calls here when you have the files)
void handleHoldToSpeak() { Serial.println("Action: Speak"); }
void handleCustomButton1() { Serial.println("Action: Custom 1"); }
void handleCustomButton2() { Serial.println("Action: Custom 2"); }
void handleCustomButton3() { Serial.println("Action: Custom 3"); }
void handleCustomButton4() { Serial.println("Action: Custom 4"); }
void handleCustomButton5() { Serial.println("Action: Custom 5"); }
void handleCustomButton6() { Serial.println("Action: Custom 6"); }
void handleCustomButton7() { Serial.println("Action: Custom 7"); }
void handleCustomButton8() { Serial.println("Action: Custom 8"); }


// ============================================
// SETUP & LOOP
// ============================================

void setup() {
  Serial.begin(115200);
  
  // Initialize Audio Logic
  setupI2S();

  // Initialize WiFi
  WiFi.softAP(ssid, password);
  Serial.println("");
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
            // Send HTTP Header
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            // Check Buttons
            if (header.indexOf("GET /volume?level=") >= 0) {
              int levelPos = header.indexOf('=');
              currentVolume = header.substring(levelPos + 1).toInt();
            }
            else if (header.indexOf("GET /speedup") >= 0) handleSpeedUp();
            else if (header.indexOf("GET /slowdown") >= 0) handleSlowDown();
            else if (header.indexOf("GET /forward") >= 0) handleGoForward();
            else if (header.indexOf("GET /left") >= 0) handleTurnLeft();
            else if (header.indexOf("GET /right") >= 0) handleTurnRight();
            else if (header.indexOf("GET /backup") >= 0) handleBackUp();
            else if (header.indexOf("GET /stop") >= 0) handleStop();
            //else if (header.indexOf("GET /speak") >= 0) handleHoldToSpeak();
            //else if (header.indexOf("GET /custom1") >= 0) handleCustomButton1();
            // ... add other custom checks if needed

            // --- START OF RESTORED UI ---
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            
            // --- CSS STYLES ---
            client.println("<style>");
            client.println("* { box-sizing: border-box; margin: 0; padding: 0; }");
            client.println("html, body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background-color: #f5f5f5; height: 100%; }");
            client.println("a { text-decoration: none; color: inherit; }");
            client.println("body { max-width: 480px; margin: 0 auto; background: #fff; box-shadow: 0 0 20px rgba(0,0,0,0.1); display: flex; flex-direction: column; min-height: 100vh; }");
            
            // Header
            client.println(".header { padding: 12px 16px; display: flex; align-items: center; justify-content: space-between; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; box-shadow: 0 2px 8px rgba(0,0,0,0.1); }");
            client.println(".header-left { display: flex; align-items: center; gap: 12px; }");
            client.println(".menu-icon { width: 28px; height: 28px; display: flex; flex-direction: column; justify-content: space-around; cursor: pointer; padding: 4px; }");
            client.println(".menu-icon span { display: block; height: 3px; background: white; border-radius: 2px; }");
            client.println(".logo-container { display: flex; align-items: center; gap: 8px; }");
            client.println(".logo-circle { width: 36px; height: 36px; background: #1e3a5f; border-radius: 50%; display: flex; align-items: center; justify-content: center; }");
            client.println(".logo-circle svg { width: 32px; height: 32px; }");
            client.println(".app-name { font-size: 20px; font-weight: 700; letter-spacing: 0.5px; }");
            client.println(".status-indicators { display: flex; align-items: center; gap: 12px; }");
            client.println(".battery-indicator { display: flex; flex-direction: column; align-items: center; font-size: 10px; }");
            client.println(".battery-icon { width: 28px; height: 14px; border: 2px solid white; border-radius: 3px; position: relative; padding: 2px; }");
            client.println(".battery-icon::after { content: ''; position: absolute; right: -4px; top: 4px; width: 3px; height: 6px; background: white; border-radius: 0 2px 2px 0; }");
            client.println(".battery-fill { height: 100%; background: #4ade80; border-radius: 1px; width: 80%; }");
            client.println(".bt-indicator { display: flex; flex-direction: column; align-items: center; font-size: 10px; }");
            client.println(".bt-icon svg { width: 20px; height: 20px; fill: #60a5fa; }");
            
            // Main Content
            client.println(".main-content { flex: 1; padding: 24px 20px; display: flex; flex-direction: column; gap: 24px; }");
            
            // Control Panel
            client.println(".control-panel { display: flex; align-items: center; justify-content: space-between; gap: 16px; }");
            
            // Speed Controls
            client.println(".speed-controls { display: flex; flex-direction: column; gap: 12px; }");
            client.println(".speed-btn { display: flex; flex-direction: column; align-items: center; justify-content: center; width: 70px; height: 90px; background: linear-gradient(135deg, #f8f9fa 0%, #e9ecef 100%); border: 2px solid #dee2e6; border-radius: 12px; box-shadow: 0 2px 8px rgba(0,0,0,0.08); transition: all 0.2s; cursor: pointer; gap: 6px; }");
            client.println(".speed-btn:active { background: linear-gradient(135deg, #e9ecef 0%, #dee2e6 100%); transform: translateY(2px); box-shadow: 0 1px 4px rgba(0,0,0,0.1); }");
            client.println(".arrow-up { width: 0; height: 0; border-left: 16px solid transparent; border-right: 16px solid transparent; border-bottom: 24px solid #495057; }");
            client.println(".arrow-down { width: 0; height: 0; border-left: 16px solid transparent; border-right: 16px solid transparent; border-top: 24px solid #495057; }");
            client.println(".speed-label { font-size: 10px; font-weight: 600; color: #495057; text-align: center; line-height: 1.2; }");

            // D-Pad
            client.println(".d-pad { width: 220px; height: 220px; position: relative; flex-shrink: 0; }");
            client.println(".d-pad-segment { position: absolute; width: 100%; height: 100%; clip-path: polygon(50% 50%, 50% 0%, 100% 0%, 100% 50%); cursor: pointer; }");
            client.println(".d-pad-segment-inner { width: 100%; height: 100%; background: linear-gradient(135deg, #f8f9fa 0%, #e9ecef 100%); border: 3px solid #dee2e6; border-radius: 50%; display: flex; align-items: center; justify-content: center; font-weight: 600; font-size: 12px; color: #495057; transition: all 0.2s; box-shadow: inset 0 2px 4px rgba(0,0,0,0.06); }");
            client.println(".d-pad-segment:active .d-pad-segment-inner { background: linear-gradient(135deg, #dee2e6 0%, #ced4da 100%); }");
            client.println(".segment-top { transform: rotate(-45deg); }");
            client.println(".segment-right { transform: rotate(45deg); }");
            client.println(".segment-bottom { transform: rotate(135deg); }");
            client.println(".segment-left { transform: rotate(225deg); }");
            client.println(".segment-label { position: absolute; font-weight: 600; font-size: 11px; color: #495057; pointer-events: none; text-align: center; }");
            client.println(".label-top { top: 10px; left: 50%; transform: translateX(-50%); }");
            client.println(".label-right { right: 10px; top: 50%; transform: translateY(-50%); }");
            client.println(".label-bottom { bottom: 10px; left: 50%; transform: translateX(-50%); }");
            client.println(".label-left { left: 10px; top: 50%; transform: translateY(-50%); }");
            client.println(".center-btn { width: 120px; height: 120px; position: absolute; top: 50%; left: 50%; transform: translate(-50%, -50%); border-radius: 50%; background: linear-gradient(135deg, #ef4444 0%, #dc2626 100%); color: white; z-index: 10; font-size: 22px; font-weight: 700; border: 4px solid white; box-shadow: 0 4px 12px rgba(239, 68, 68, 0.4); display: flex; align-items: center; justify-content: center; cursor: pointer; transition: all 0.2s; }");
            client.println(".center-btn:active { background: linear-gradient(135deg, #dc2626 0%, #b91c1c 100%); transform: translate(-50%, -50%) scale(0.95); }");
            
            // Mic Button
            client.println(".mic-container { display: flex; flex-direction: column; align-items: center; gap: 8px; }");
            client.println(".mic-btn { width: 80px; height: 80px; background: linear-gradient(135deg, #f8f9fa 0%, #e9ecef 100%); border: 2px solid #dee2e6; border-radius: 50%; display: flex; align-items: center; justify-content: center; box-shadow: 0 4px 12px rgba(0,0,0,0.1); transition: all 0.2s; cursor: pointer; }");
            client.println(".mic-btn svg { width: 36px; height: 36px; fill: #495057; }");
            client.println(".mic-btn:active { background: linear-gradient(135deg, #dee2e6 0%, #ced4da 100%); transform: translateY(2px); box-shadow: 0 2px 6px rgba(0,0,0,0.1); }");
            client.println(".mic-label { font-size: 11px; font-weight: 600; color: #6c757d; text-align: center; }");
            
            // Volume Slider
            client.println(".volume-container { padding: 0 4px; }");
            client.println(".volume-label { display: block; text-align: center; font-size: 13px; font-weight: 600; margin-bottom: 12px; color: #495057; }");
            client.println("input[type='range'] { -webkit-appearance: none; width: 100%; height: 8px; background: #dee2e6; border-radius: 4px; outline: none; }");
            client.println("input[type='range']::-webkit-slider-thumb { -webkit-appearance: none; appearance: none; width: 24px; height: 24px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); border-radius: 50%; cursor: pointer; box-shadow: 0 2px 6px rgba(102, 126, 234, 0.4); }");

            // Custom Buttons Grid
            client.println(".custom-grid { padding: 0 4px 20px 4px; display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; }");
            client.println(".custom-btn { height: 70px; background: linear-gradient(135deg, #ffffff 0%, #f8f9fa 100%); border: 2px solid #dee2e6; border-radius: 12px; display: flex; align-items: center; justify-content: center; text-align: center; font-size: 11px; font-weight: 600; padding: 8px; box-shadow: 0 2px 6px rgba(0,0,0,0.06); transition: all 0.2s; cursor: pointer; color: #495057; }");
            client.println(".custom-btn:active { background: linear-gradient(135deg, #e9ecef 0%, #dee2e6 100%); transform: translateY(2px); box-shadow: 0 1px 3px rgba(0,0,0,0.08); }");
            client.println(".custom-btn.empty { background: #f8f9fa; border: 2px dashed #dee2e6; cursor: default; }");
            client.println("</style>");
            
            // Slider JS
            client.println("<script>function updateVolume(value) { fetch('/volume?level=' + value); }</script>");
            client.println("</head>");

            // --- HTML BODY ---
            client.println("<body>");
            
            // Header
            client.println("<div class='header'>");
            client.println("  <div class='header-left'>");
            client.println("    <div class='menu-icon'><span></span><span></span><span></span></div>");
            client.println("    <div class='logo-container'>");
            client.println("      <div class='logo-circle'><svg viewBox='0 0 100 100'><circle cx='35' cy='35' r='12' fill='white'/><path d='M 35 50 Q 25 50 20 58 Q 15 66 15 75 L 15 85 Q 15 90 20 90 L 50 90 Q 55 90 55 85 L 55 75 Q 55 66 50 58 Q 45 50 35 50 Z' fill='white'/><path d='M 60 25 Q 60 25 62 27 Q 64 29 64 32' stroke='white' stroke-width='4' fill='none'/><path d='M 67 20 Q 67 20 71 24 Q 75 28 75 35' stroke='white' stroke-width='4' fill='none'/><path d='M 75 15 Q 75 15 81 21 Q 87 27 87 38' stroke='white' stroke-width='4' fill='none'/></svg></div>");
            client.println("      <span class='app-name'>ViviSense</span>");
            client.println("    </div>");
            client.println("  </div>");
            client.println("  <div class='status-indicators'>");
            client.println("    <div class='battery-indicator'><div class='battery-icon'><div class='battery-fill'></div></div><span>80%</span></div>");
            client.println("    <div class='bt-indicator'><div class='bt-icon'><svg viewBox='0 0 24 24'><path d='M17.71 7.71L12 2h-1v7.59L6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 11 14.41V22h1l5.71-5.71-4.3-4.29 4.3-4.29zM13 5.83l1.88 1.88L13 9.59V5.83zm1.88 10.46L13 18.17v-3.76l1.88 1.88z'/></svg></div><span>Connected</span></div>");
            client.println("  </div>");
            client.println("</div>");
            
            // Content
            client.println("<div class='main-content'>");
            client.println("<div class='control-panel'>");
            
            // Speed
            client.println("  <div class='speed-controls'>");
            client.println("    <a href='/speedup'><div class='speed-btn'><div class='arrow-up'></div><div class='speed-label'>Speed<br>Up</div></div></a>");
            client.println("    <a href='/slowdown'><div class='speed-btn'><div class='arrow-down'></div><div class='speed-label'>Slow<br>Down</div></div></a>");
            client.println("  </div>");
            
            // D-Pad
            client.println("  <div class='d-pad'>");
            client.println("    <a href='/forward'><div class='d-pad-segment segment-top'><div class='d-pad-segment-inner'></div></div><div class='segment-label label-top'>Go Forward</div></a>");
            client.println("    <a href='/right'><div class='d-pad-segment segment-right'><div class='d-pad-segment-inner'></div></div><div class='segment-label label-right'>Turn<br>Right</div></a>");
            client.println("    <a href='/backup'><div class='d-pad-segment segment-bottom'><div class='d-pad-segment-inner'></div></div><div class='segment-label label-bottom'>Back Up</div></a>");
            client.println("    <a href='/left'><div class='d-pad-segment segment-left'><div class='d-pad-segment-inner'></div></div><div class='segment-label label-left'>Turn<br>Left</div></a>");
            client.println("    <a href='/stop'><div class='center-btn'>Stop!</div></a>");
            client.println("  </div>");
            
            // Mic
            client.println("  <a href='/speak'><div class='mic-container'>");
            client.println("    <div class='mic-btn'><svg viewBox='0 0 24 24'><path d='M12 14c1.66 0 3-1.34 3-3V5c0-1.66-1.34-3-3-3S9 3.34 9 5v6c0 1.66 1.34 3 3 3z'/><path d='M17 11c0 2.76-2.24 5-5 5s-5-2.24-5-5H5c0 3.53 2.61 6.43 6 6.92V21h2v-3.08c3.39-.49 6-3.39 6-6.92h-2z'/></svg></div>");
            client.println("    <div class='mic-label'>Hold to<br>Speak</div>");
            client.println("  </div></a>");
            client.println("</div>"); // End .control-panel
            
            // Volume
            client.println("<div class='volume-container'>");
            client.println("  <label class='volume-label' for='volume'>Volume Control</label>");
            client.println("  <input type='range' id='volume' min='0' max='255' value='" + String(currentVolume) + "' onchange='updateVolume(this.value)'>");
            client.println("</div>");

            // Custom Grid
            client.println("<div class='custom-grid'>");
            client.println("  <a href='/custom1'><div class='custom-btn'>Stay on<br>the line</div></a>");
            client.println("  <a href='/custom2'><div class='custom-btn empty'></div></a>");
            client.println("  <a href='/custom3'><div class='custom-btn empty'></div></a>");
            client.println("  <a href='/custom4'><div class='custom-btn empty'></div></a>");
            client.println("  <a href='/custom5'><div class='custom-btn empty'></div></a>");
            client.println("  <a href='/custom6'><div class='custom-btn empty'></div></a>");
            client.println("  <a href='/custom7'><div class='custom-btn empty'></div></a>");
            client.println("  <a href='/custom8'><div class='custom-btn empty'></div></a>");
            client.println("</div>");
            
            client.println("</div>"); // End .main-content
            client.println("</body></html>");
            
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