/*
 * ViviSense - ESP32 Soft AP Web Server (Enhanced UI)
 * * This code creates a WiFi Access Point "ESP32-Network".
 * * Connect, go to 192.168.4.1. This will serve an enhanced app UI.
 * * All buttons link to the /alert endpoint to play a tone.
 * * A slider in the UI controls the volume.
 *
 * * HARDWARE:
 * - ESP32
 * - Audio Amplifier Module (8002B)
 * - 8-ohm Speaker
 *
 * * WIRING (Amplifier Module):
 * - ESP32 GND      -> Amp Module 'GND'
 * - ESP32 3.3V     -> Amp Module 'VCC'
 * - ESP32 GPIO 25  -> Amp Module 'L' (or 'IN')
 * - Speaker Pin 1  -> Amp Module 'V1' (or 'OUT1')
 * - Speaker Pin 2  -> Amp Module 'V2l' (or 'OUT2')
 */

// Load Wi-Fi library
#include <WiFi.h>

// --- Network Definitions ---
const char* ssid = "ESP32-Network";
const char* password = "Esp32-Password";
WiFiServer server(80);
String header;

// --- Speaker Pin Definitions ---
const int speakerPin = 25;
const int alertFrequency = 1000; // 1kHz

// --- LEDC (PWM) Tone Generation Setup ---
const int ledcChannel = 0;
const int ledcResolution = 8; // 8-bit resolution (0-255)

// --- Global State Variables ---
int currentVolume = 128; // Start at 50% volume (0-255)
bool isMuted = false;

// Current time
unsigned long currentTime = millis();
unsigned long previousTime = 0;
const long timeoutTime = 2000;

// ============================================
// PLACEHOLDER FUNCTIONS FOR BUTTON ACTIONS
// ============================================
// These functions are called when buttons are pressed.
// Add your implementation code inside each function.

void handleSpeedUp() {
  Serial.println("BUTTON PRESSED: Speed Up");
  // TODO: Add your speed up implementation here
  // Example: increase motor speed, send command to robot, etc.
  
  // Placeholder tone feedback
  ledcWrite(ledcChannel, currentVolume);
  delay(100);
  ledcWrite(ledcChannel, 0);
}

void handleSlowDown() {
  Serial.println("BUTTON PRESSED: Slow Down");
  // TODO: Add your slow down implementation here
  
  // Placeholder tone feedback
  ledcWrite(ledcChannel, currentVolume);
  delay(100);
  ledcWrite(ledcChannel, 0);
}

void handleGoForward() {
  Serial.println("BUTTON PRESSED: Go Forward");
  // TODO: Add your forward movement implementation here
  
  // Placeholder tone feedback
  ledcWrite(ledcChannel, currentVolume);
  delay(100);
  ledcWrite(ledcChannel, 0);
}

void handleTurnLeft() {
  Serial.println("BUTTON PRESSED: Turn Left");
  // TODO: Add your left turn implementation here
  
  // Placeholder tone feedback
  ledcWrite(ledcChannel, currentVolume);
  delay(100);
  ledcWrite(ledcChannel, 0);
}

void handleTurnRight() {
  Serial.println("BUTTON PRESSED: Turn Right");
  // TODO: Add your right turn implementation here
  
  // Placeholder tone feedback
  ledcWrite(ledcChannel, currentVolume);
  delay(100);
  ledcWrite(ledcChannel, 0);
}

void handleBackUp() {
  Serial.println("BUTTON PRESSED: Back Up");
  // TODO: Add your backward movement implementation here
  
  // Placeholder tone feedback
  ledcWrite(ledcChannel, currentVolume);
  delay(100);
  ledcWrite(ledcChannel, 0);
}

void handleStop() {
  Serial.println("BUTTON PRESSED: Stop!");
  // TODO: Add your stop/emergency stop implementation here
  
  // Placeholder tone feedback (longer for stop)
  ledcWrite(ledcChannel, currentVolume);
  delay(250);
  ledcWrite(ledcChannel, 0);
}

void handleHoldToSpeak() {
  Serial.println("BUTTON PRESSED: Hold to Speak");
  // TODO: Add your voice/microphone implementation here
  // This might involve starting audio recording, voice commands, etc.
  
  // Placeholder tone feedback
  ledcWrite(ledcChannel, currentVolume);
  delay(150);
  ledcWrite(ledcChannel, 0);
}

void handleCustomButton1() {
  Serial.println("BUTTON PRESSED: Custom Button 1 - Stay on the line");
  // TODO: Add your custom button 1 implementation here
  
  // Placeholder tone feedback
  ledcWrite(ledcChannel, currentVolume);
  delay(100);
  ledcWrite(ledcChannel, 0);
}

void handleCustomButton2() {
  Serial.println("BUTTON PRESSED: Custom Button 2");
  // TODO: Add your custom button 2 implementation here
  
  // Placeholder tone feedback
  ledcWrite(ledcChannel, currentVolume);
  delay(100);
  ledcWrite(ledcChannel, 0);
}

void handleCustomButton3() {
  Serial.println("BUTTON PRESSED: Custom Button 3");
  // TODO: Add your custom button 3 implementation here
  
  // Placeholder tone feedback
  ledcWrite(ledcChannel, currentVolume);
  delay(100);
  ledcWrite(ledcChannel, 0);
}

void handleCustomButton4() {
  Serial.println("BUTTON PRESSED: Custom Button 4");
  // TODO: Add your custom button 4 implementation here
  
  // Placeholder tone feedback
  ledcWrite(ledcChannel, currentVolume);
  delay(100);
  ledcWrite(ledcChannel, 0);
}

void handleCustomButton5() {
  Serial.println("BUTTON PRESSED: Custom Button 5");
  // TODO: Add your custom button 5 implementation here
  
  // Placeholder tone feedback
  ledcWrite(ledcChannel, currentVolume);
  delay(100);
  ledcWrite(ledcChannel, 0);
}

void handleCustomButton6() {
  Serial.println("BUTTON PRESSED: Custom Button 6");
  // TODO: Add your custom button 6 implementation here
  
  // Placeholder tone feedback
  ledcWrite(ledcChannel, currentVolume);
  delay(100);
  ledcWrite(ledcChannel, 0);
}

void handleCustomButton7() {
  Serial.println("BUTTON PRESSED: Custom Button 7");
  // TODO: Add your custom button 7 implementation here
  
  // Placeholder tone feedback
  ledcWrite(ledcChannel, currentVolume);
  delay(100);
  ledcWrite(ledcChannel, 0);
}

void handleCustomButton8() {
  Serial.println("BUTTON PRESSED: Custom Button 8");
  // TODO: Add your custom button 8 implementation here
  
  // Placeholder tone feedback
  ledcWrite(ledcChannel, currentVolume);
  delay(100);
  ledcWrite(ledcChannel, 0);
}

// ============================================
// END OF PLACEHOLDER FUNCTIONS
// ============================================


void setup() {
  Serial.begin(115200);

  // --- Setup the Speaker Pin (LEDC) ---
  ledcSetup(ledcChannel, alertFrequency, ledcResolution);
  ledcAttachPin(speakerPin, ledcChannel);
  ledcWrite(ledcChannel, 0); // Start silent

  WiFi.softAP(ssid, password);
  Serial.println("");
  Serial.println("Access Point Started");
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP()); // 192.168.4.1
  server.begin();
}

void loop() {
  WiFiClient client = server.available();

  if (client) {
    currentTime = millis();
    previousTime = currentTime;
    Serial.println("New Client.");
    String currentLine = "";

    while (client.connected() && currentTime - previousTime <= timeoutTime) {
      currentTime = millis();
      if (client.available()) {
        char c = client.read();
        Serial.write(c);
        header += c;
        if (c == '\n') {
          if (currentLine.length() == 0) {
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();

            // --- Check for Volume Change Request ---
            if (header.indexOf("GET /volume?level=") >= 0) {
              int levelPos = header.indexOf('=');
              String volString = header.substring(levelPos + 1);
              int newVolume = volString.toInt();
              if (newVolume >= 0 && newVolume <= 255) {
                currentVolume = newVolume;
                isMuted = false;
                Serial.print("Volume set to: ");
                Serial.println(currentVolume);
              }
            }
            // --- Check for Button Requests ---
            else if (header.indexOf("GET /speedup") >= 0) {
              handleSpeedUp();
            }
            else if (header.indexOf("GET /slowdown") >= 0) {
              handleSlowDown();
            }
            else if (header.indexOf("GET /forward") >= 0) {
              handleGoForward();
            }
            else if (header.indexOf("GET /left") >= 0) {
              handleTurnLeft();
            }
            else if (header.indexOf("GET /right") >= 0) {
              handleTurnRight();
            }
            else if (header.indexOf("GET /backup") >= 0) {
              handleBackUp();
            }
            else if (header.indexOf("GET /stop") >= 0) {
              handleStop();
            }
            else if (header.indexOf("GET /speak") >= 0) {
              handleHoldToSpeak();
            }
            else if (header.indexOf("GET /custom1") >= 0) {
              handleCustomButton1();
            }
            else if (header.indexOf("GET /custom2") >= 0) {
              handleCustomButton2();
            }
            else if (header.indexOf("GET /custom3") >= 0) {
              handleCustomButton3();
            }
            else if (header.indexOf("GET /custom4") >= 0) {
              handleCustomButton4();
            }
            else if (header.indexOf("GET /custom5") >= 0) {
              handleCustomButton5();
            }
            else if (header.indexOf("GET /custom6") >= 0) {
              handleCustomButton6();
            }
            else if (header.indexOf("GET /custom7") >= 0) {
              handleCustomButton7();
            }
            else if (header.indexOf("GET /custom8") >= 0) {
              handleCustomButton8();
            }

            // --- START OF HTML/CSS FOR THE APP UI ---
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            
            // --- CSS STYLES ---
            client.println("<style>");
            client.println("* { box-sizing: border-box; margin: 0; padding: 0; }");
            client.println("html, body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; background-color: #f5f5f5; height: 100%; }");
            client.println("a { text-decoration: none; color: inherit; }");
            client.println("body { max-width: 480px; margin: 0 auto; background: #fff; box-shadow: 0 0 20px rgba(0,0,0,0.1); display: flex; flex-direction: column; min-height: 100vh; }");
            
            // Header with status indicators
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
            
            // Main Content Area
            client.println(".main-content { flex: 1; padding: 24px 20px; display: flex; flex-direction: column; gap: 24px; }");
            
            // Control Panel Container
            client.println(".control-panel { display: flex; align-items: center; justify-content: space-between; gap: 16px; }");
            
            // Left Speed Controls
            client.println(".speed-controls { display: flex; flex-direction: column; gap: 12px; }");
            client.println(".speed-btn { display: flex; flex-direction: column; align-items: center; justify-content: center; width: 70px; height: 90px; background: linear-gradient(135deg, #f8f9fa 0%, #e9ecef 100%); border: 2px solid #dee2e6; border-radius: 12px; box-shadow: 0 2px 8px rgba(0,0,0,0.08); transition: all 0.2s; cursor: pointer; gap: 6px; }");
            client.println(".speed-btn:active { background: linear-gradient(135deg, #e9ecef 0%, #dee2e6 100%); transform: translateY(2px); box-shadow: 0 1px 4px rgba(0,0,0,0.1); }");
            client.println(".arrow-up { width: 0; height: 0; border-left: 16px solid transparent; border-right: 16px solid transparent; border-bottom: 24px solid #495057; }");
            client.println(".arrow-down { width: 0; height: 0; border-left: 16px solid transparent; border-right: 16px solid transparent; border-top: 24px solid #495057; }");
            client.println(".speed-label { font-size: 10px; font-weight: 600; color: #495057; text-align: center; line-height: 1.2; }");

            // Center D-Pad
            client.println(".d-pad { width: 220px; height: 220px; position: relative; flex-shrink: 0; }");
            client.println(".d-pad-ring { position: absolute; width: 100%; height: 100%; border-radius: 50%; }");
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
            
            // Right Mic Button
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
            client.println("input[type='range']::-moz-range-thumb { width: 24px; height: 24px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); border-radius: 50%; cursor: pointer; border: none; box-shadow: 0 2px 6px rgba(102, 126, 234, 0.4); }");

            // Custom Buttons Grid
            client.println(".custom-grid { padding: 0 4px 20px 4px; display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; }");
            client.println(".custom-btn { height: 70px; background: linear-gradient(135deg, #ffffff 0%, #f8f9fa 100%); border: 2px solid #dee2e6; border-radius: 12px; display: flex; align-items: center; justify-content: center; text-align: center; font-size: 11px; font-weight: 600; padding: 8px; box-shadow: 0 2px 6px rgba(0,0,0,0.06); transition: all 0.2s; cursor: pointer; color: #495057; }");
            client.println(".custom-btn:active { background: linear-gradient(135deg, #e9ecef 0%, #dee2e6 100%); transform: translateY(2px); box-shadow: 0 1px 3px rgba(0,0,0,0.08); }");
            client.println(".custom-btn.empty { background: #f8f9fa; border: 2px dashed #dee2e6; cursor: default; }");
            client.println(".custom-btn.empty:active { transform: none; }");
            
            client.println("</style>");
            // --- END OF CSS ---
            
            // --- JAVASCRIPT FOR SLIDER ---
            client.println("<script>");
            client.println("function updateVolume(value) {");
            client.println("  fetch('/volume?level=' + value);");
            client.println("}");
            client.println("</script>");

            client.println("</head>");

            // --- START OF HTML BODY ---
            client.println("<body>");
            
            // Header with Status Indicators
            client.println("<div class='header'>");
            client.println("  <div class='header-left'>");
            client.println("    <div class='menu-icon'><span></span><span></span><span></span></div>");
            client.println("    <div class='logo-container'>");
            client.println("      <div class='logo-circle'>");
            client.println("        <svg viewBox='0 0 100 100' xmlns='http://www.w3.org/2000/svg'>");
            client.println("          <circle cx='35' cy='35' r='12' fill='white'/>");
            client.println("          <path d='M 35 50 Q 25 50 20 58 Q 15 66 15 75 L 15 85 Q 15 90 20 90 L 50 90 Q 55 90 55 85 L 55 75 Q 55 66 50 58 Q 45 50 35 50 Z' fill='white'/>");
            client.println("          <path d='M 60 25 Q 60 25 62 27 Q 64 29 64 32' stroke='white' stroke-width='4' fill='none' stroke-linecap='round'/>");
            client.println("          <path d='M 67 20 Q 67 20 71 24 Q 75 28 75 35' stroke='white' stroke-width='4' fill='none' stroke-linecap='round'/>");
            client.println("          <path d='M 75 15 Q 75 15 81 21 Q 87 27 87 38' stroke='white' stroke-width='4' fill='none' stroke-linecap='round'/>");
            client.println("        </svg>");
            client.println("      </div>");
            client.println("      <span class='app-name'>ViviSense</span>");
            client.println("    </div>");
            client.println("  </div>");
            client.println("  <div class='status-indicators'>");
            client.println("    <div class='battery-indicator'>");
            client.println("      <div class='battery-icon'><div class='battery-fill'></div></div>");
            client.println("      <span>80%</span>");
            client.println("    </div>");
            client.println("    <div class='bt-indicator'>");
            client.println("      <div class='bt-icon'>");
            client.println("        <svg viewBox='0 0 24 24'><path d='M17.71 7.71L12 2h-1v7.59L6.41 5 5 6.41 10.59 12 5 17.59 6.41 19 11 14.41V22h1l5.71-5.71-4.3-4.29 4.3-4.29zM13 5.83l1.88 1.88L13 9.59V5.83zm1.88 10.46L13 18.17v-3.76l1.88 1.88z'/></svg>");
            client.println("      </div>");
            client.println("      <span>Connected</span>");
            client.println("    </div>");
            client.println("  </div>");
            client.println("</div>");
            
            // Main Content
            client.println("<div class='main-content'>");
            
            // Control Panel
            client.println("<div class='control-panel'>");
            
            // Left Speed Controls
            client.println("  <div class='speed-controls'>");
            client.println("    <a href='/speedup'><div class='speed-btn'><div class='arrow-up'></div><div class='speed-label'>Speed<br>Up</div></div></a>");
            client.println("    <a href='/slowdown'><div class='speed-btn'><div class='arrow-down'></div><div class='speed-label'>Slow<br>Down</div></div></a>");
            client.println("  </div>");
            
            // Center D-Pad
            client.println("  <div class='d-pad'>");
            client.println("    <a href='/forward'>");
            client.println("      <div class='d-pad-segment segment-top'>");
            client.println("        <div class='d-pad-segment-inner'></div>");
            client.println("      </div>");
            client.println("      <div class='segment-label label-top'>Go Forward</div>");
            client.println("    </a>");
            client.println("    <a href='/right'>");
            client.println("      <div class='d-pad-segment segment-right'>");
            client.println("        <div class='d-pad-segment-inner'></div>");
            client.println("      </div>");
            client.println("      <div class='segment-label label-right'>Turn<br>Right</div>");
            client.println("    </a>");
            client.println("    <a href='/backup'>");
            client.println("      <div class='d-pad-segment segment-bottom'>");
            client.println("        <div class='d-pad-segment-inner'></div>");
            client.println("      </div>");
            client.println("      <div class='segment-label label-bottom'>Back Up</div>");
            client.println("    </a>");
            client.println("    <a href='/left'>");
            client.println("      <div class='d-pad-segment segment-left'>");
            client.println("        <div class='d-pad-segment-inner'></div>");
            client.println("      </div>");
            client.println("      <div class='segment-label label-left'>Turn<br>Left</div>");
            client.println("    </a>");
            client.println("    <a href='/stop'><div class='center-btn'>Stop!</div></a>");
            client.println("  </div>");
            
            // Right Mic Button
            client.println("  <a href='/speak'><div class='mic-container'>");
            client.println("    <div class='mic-btn'>");
            client.println("      <svg viewBox='0 0 24 24'><path d='M12 14c1.66 0 3-1.34 3-3V5c0-1.66-1.34-3-3-3S9 3.34 9 5v6c0 1.66 1.34 3 3 3z'/><path d='M17 11c0 2.76-2.24 5-5 5s-5-2.24-5-5H5c0 3.53 2.61 6.43 6 6.92V21h2v-3.08c3.39-.49 6-3.39 6-6.92h-2z'/></svg>");
            client.println("    </div>");
            client.println("    <div class='mic-label'>Hold to<br>Speak</div>");
            client.println("  </div></a>");
            
            client.println("</div>"); // End .control-panel
            
            // Volume Slider
            client.println("<div class='volume-container'>");
            client.println("  <label class='volume-label' for='volume'>Volume Control</label>");
            client.println("  <input type='range' id='volume' min='0' max='255' value='" + String(currentVolume) + "' onchange='updateVolume(this.value)'>");
            client.println("</div>");

            // Custom Buttons Grid
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
            // --- END OF HTML ---
            
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
    Serial.println("Client disconnected.");
    Serial.println("");
  }
}