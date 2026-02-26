/*
 * ViviSense - ESP32 WebSocket Server
 * =====================================
 * HARDWARE: ESP32 + MAX98357A I2S Amp
 *
 * ARCHITECTURE
 * ─────────────────────────────────────────────────────────────
 *  Port 80  │ HTTP  │ Serves the HTML page (one-time page load)
 *  Port 81  │ WS    │ All ongoing communication:
 *           │       │   Text  → button commands, volume
 *           │       │   Binary → raw PCM audio (push-to-speak)
 *
 * WEBSOCKET MESSAGE PROTOCOL (Browser → ESP32)
 * ─────────────────────────────────────────────────────────────
 *  "stop"        Trigger stop audio clip
 *  "forward"     Trigger go forward clip
 *  "left"        Trigger turn left clip
 *  "right"       Trigger turn right clip
 *  "backup"      Trigger back up clip
 *  "speedup"     Trigger speed up clip
 *  "slowdown"    Trigger slow down clip
 *  "custom1"     Trigger custom clip 1
 *  "vol:180"     Set volume (0-255)
 *  [binary]      Raw 16-bit PCM audio at 16kHz mono (push-to-speak)
 *
 * WEBSOCKET MESSAGE PROTOCOL (ESP32 → Browser)
 * ─────────────────────────────────────────────────────────────
 *  "ack:stop"    Confirm command received and audio triggered
 *  "ack:voice"   Confirm voice clip received and playing
 *  (Future: "imu:{...}" for pod status dashboard)
 *
 * Wire Setup
 * ──────────
 * ESP32 Pins         MAX98357A Pins
 * GND         ->     GND
 * GPIO 27     ->     BCLK
 * GPIO 26     ->     LRC
 * GPIO 25     ->     DIN
 * VIN         ->     Vin, SD, Gain
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include "driver/i2s.h"
#include <sounds.h>

// --- I2S Pins (MAX98357A) ---
#define I2S_BCK_IO  27
#define I2S_WS_IO   26
#define I2S_DO_IO   25
#define I2S_NUM     I2S_NUM_0

// --- Network ---
const char* ssid     = "ESP32-Network";
const char* password = "Esp32-Password";

WiFiServer       httpServer(80);   // Serves HTML page
WebSocketsServer wsServer(81);     // Handles all real-time comms

// --- Global State ---
int currentVolume = 255;

// --- Pending Action ---
// The WS callback must return quickly - never block inside it.
// Audio calls block for the full clip duration, so we set a flag here
// and let loop() do the actual work on the next iteration.
enum PendingAction {
  ACTION_NONE,
  ACTION_STOP,
  ACTION_SPEED_UP,
  ACTION_SLOW_DOWN,
  ACTION_FORWARD,
  ACTION_LEFT,
  ACTION_RIGHT,
  ACTION_BACKUP,
  ACTION_CUSTOM1,
  ACTION_VOICE
};
volatile PendingAction pendingAction = ACTION_NONE;

// --- Voice Buffer (Push-to-Speak, chunked transfer) ---
// Size determined at runtime by adaptive malloc in setup() — see BUF_SIZES[].
// Filled incrementally by binary WS frames, played on "voice:end".
#define VOICE_CHUNK_MAX  4096   // must match JS CHUNK_BYTES
uint8_t* voiceBuffer      = nullptr;
size_t   voiceBufCapacity = 0;     // bytes actually allocated (set in setup)
size_t   voiceBufferLen   = 0;
size_t   voiceBufferOffset = 0;   // write cursor during chunked receive
bool     voiceReceiving   = false; // true between voice:start and voice:end
unsigned int voiceSampleRate = 16000; // actual rate reported by browser

// --- Active WS client (for sending acks back) ---
uint8_t activeClient = 0;


// ============================================================
// AUDIO ENGINE
// ============================================================

// Track the rate currently loaded into I2S hardware so we only call
// i2s_set_sample_rates() when it genuinely needs to change.
// Redundant calls to that function momentarily reset the clock dividers
// and can introduce a brief stutter at the start of playback.
static unsigned int currentI2SRate = 0;

void setI2SRate(unsigned int rate) {
  if (rate == currentI2SRate) return;
  i2s_set_sample_rates(I2S_NUM, rate);
  currentI2SRate = rate;
}

void setupI2S() {
  i2s_config_t cfg = {
    .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate          = 16000,
    .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format       = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count        = 8,
    .dma_buf_len          = 128,
    .use_apll             = false   // APLL conflicts with i2s_set_sample_rates() on this framework version
  };
  i2s_driver_install(I2S_NUM, &cfg, 0, NULL);

  i2s_pin_config_t pins = {
    .bck_io_num   = I2S_BCK_IO,
    .ws_io_num    = I2S_WS_IO,
    .data_out_num = I2S_DO_IO,
    .data_in_num  = I2S_PIN_NO_CHANGE
  };
  i2s_set_pin(I2S_NUM, &pins);
  currentI2SRate = 16000; // matches .sample_rate in cfg above
}

// Play a pre-recorded clip from PROGMEM (sounds.h)
void playAudio(const unsigned char* data, unsigned int len, unsigned int rate) {
  if (len == 0) return;
  setI2SRate(rate);

  size_t written;
  int16_t buf[128];
  int idx = 0;
  float vol = (float)currentVolume / 255.0f;

  for (unsigned int i = 0; i < len; i += 2) {
    int16_t sample = (int16_t)((pgm_read_byte(&data[i + 1]) << 8) | pgm_read_byte(&data[i]));
    sample = (int16_t)(sample * vol);
    buf[idx++] = sample; // Left
    buf[idx++] = sample; // Right
    if (idx >= 128) {
      i2s_write(I2S_NUM, buf, sizeof(buf), &written, portMAX_DELAY);
      idx = 0;
    }
  }
  if (idx > 0) i2s_write(I2S_NUM, buf, idx * 2, &written, portMAX_DELAY);
  i2s_zero_dma_buffer(I2S_NUM);
}

// Play live voice data from heap RAM (push-to-speak)
void playVoiceData(uint8_t* data, size_t len) {
  if (!data || len == 0) return;
  Serial.printf("[Voice] Playing %u bytes (~%.1fs)\n", len, len / 32000.0f);
  setI2SRate(voiceSampleRate);
  Serial.printf("[Voice] Playback at %u Hz\n", voiceSampleRate);

  size_t written;
  int16_t buf[128];
  int idx = 0;
  float vol = (float)currentVolume / 255.0f;

  for (size_t i = 0; i + 1 < len; i += 2) {
    int16_t sample = (int16_t)((data[i + 1] << 8) | data[i]);
    sample = (int16_t)(sample * vol);
    buf[idx++] = sample; // Left
    buf[idx++] = sample; // Right
    if (idx >= 128) {
      i2s_write(I2S_NUM, buf, sizeof(buf), &written, portMAX_DELAY);
      idx = 0;
    }
  }
  if (idx > 0) i2s_write(I2S_NUM, buf, idx * 2, &written, portMAX_DELAY);
  i2s_zero_dma_buffer(I2S_NUM);
}


// ============================================================
// BUTTON ACTION HANDLERS
// ============================================================

void handleStop()      { Serial.println("[CMD] Stop");      playAudio(stop_data,       stop_len,       stop_rate);       wsServer.sendTXT(activeClient, "ack:stop");     }
void handleSpeedUp()   { Serial.println("[CMD] Speed Up");  playAudio(speed_up_data,   speed_up_len,   speed_up_rate);   wsServer.sendTXT(activeClient, "ack:speedup");  }
void handleSlowDown()  { Serial.println("[CMD] Slow Down"); playAudio(slow_down_data,  slow_down_len,  slow_down_rate);  wsServer.sendTXT(activeClient, "ack:slowdown"); }
void handleGoForward() { Serial.println("[CMD] Forward");   playAudio(go_forward_data, go_forward_len, go_forward_rate); wsServer.sendTXT(activeClient, "ack:forward");  }
void handleTurnLeft()  { Serial.println("[CMD] Left");      playAudio(turn_left_data,  turn_left_len,  turn_left_rate);  wsServer.sendTXT(activeClient, "ack:left");     }
void handleTurnRight() { Serial.println("[CMD] Right");     playAudio(turn_right_data, turn_right_len, turn_right_rate); wsServer.sendTXT(activeClient, "ack:right");    }
void handleBackUp()    { Serial.println("[CMD] Back Up");   playAudio(back_up_data,    back_up_len,    back_up_rate);    wsServer.sendTXT(activeClient, "ack:backup");   }
void handleCustom1()   { Serial.println("[CMD] Custom 1");  /* add audio here */        wsServer.sendTXT(activeClient, "ack:custom1");  }


// ============================================================
// WEBSOCKET EVENT HANDLER
// ============================================================
// Keep this fast. Never call playAudio() here directly.
// Set pendingAction and return — loop() handles the rest.

void webSocketEvent(uint8_t clientId, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {

    case WStype_CONNECTED: {
      Serial.printf("[WS] Client #%u connected from %s\n", clientId,
                    wsServer.remoteIP(clientId).toString().c_str());
      // Include voice buffer capacity so the browser can trim PCM to fit exactly
      char helloMsg[64];
      snprintf(helloMsg, sizeof(helloMsg), "hello:vivicense:maxSamples=%u",
               (unsigned)(voiceBufCapacity / 2));
      wsServer.sendTXT(clientId, helloMsg);
      break;
    }

    case WStype_DISCONNECTED:
      Serial.printf("[WS] Client #%u disconnected\n", clientId);
      break;

    case WStype_TEXT: {
      String msg = String((char*)payload);
      Serial.printf("[WS] Text from #%u: %s\n", clientId, msg.c_str());
      activeClient = clientId;

      if      (msg == "stop")     pendingAction = ACTION_STOP;
      else if (msg == "forward")  pendingAction = ACTION_FORWARD;
      else if (msg == "left")     pendingAction = ACTION_LEFT;
      else if (msg == "right")    pendingAction = ACTION_RIGHT;
      else if (msg == "backup")   pendingAction = ACTION_BACKUP;
      else if (msg == "speedup")  pendingAction = ACTION_SPEED_UP;
      else if (msg == "slowdown") pendingAction = ACTION_SLOW_DOWN;
      else if (msg == "custom1")  pendingAction = ACTION_CUSTOM1;
      else if (msg.startsWith("vol:")) {
        currentVolume = msg.substring(4).toInt();
        Serial.printf("[WS] Volume set to %d\n", currentVolume);
      }
      // ── Chunked voice transfer ────────────────────────────────
      // "voice:start" → allocate buffer, reset write cursor
      // [binary]      → append each 4KB chunk to buffer
      // "voice:end"   → total received, flag loop() to play
      else if (msg.startsWith("voice:start")) {
        // Format: "voice:start:SAMPLERATE" e.g. "voice:start:44100"
        // Fall back to 16000 if rate not provided
        int colonPos = msg.lastIndexOf(':');
        voiceSampleRate = (colonPos > 10) ? msg.substring(colonPos + 1).toInt() : 16000;
        if (voiceSampleRate < 8000 || voiceSampleRate > 48000) voiceSampleRate = 16000;
        Serial.printf("[Voice] Expecting audio at %u Hz\n", voiceSampleRate);
        voiceBufferOffset = 0;
        voiceBufferLen    = 0;
        // Buffer is pre-allocated in setup(). If it failed there, it will
        // fail here too (WiFi has fragmented the heap further), so don't retry.
        if (voiceBuffer && voiceBufCapacity > 0) {
          voiceReceiving = true;
          Serial.printf("[Voice] Buffer ready (%u bytes), receiving chunks...\n", voiceBufCapacity);
          wsServer.sendTXT(clientId, "ack:voice:start");
        } else {
          voiceReceiving = false;
          Serial.printf("[Voice] ERROR: No buffer (capacity=%u)\n", voiceBufCapacity);
          wsServer.sendTXT(clientId, "err:nomem");
        }
      }
      else if (msg == "voice:end") {
        voiceReceiving = false;
        voiceBufferLen = voiceBufferOffset;
        Serial.printf("[Voice] Transfer complete: %u bytes\n", voiceBufferLen);
        if (voiceBufferLen > 0) pendingAction = ACTION_VOICE;
      }
      break;
    }

    case WStype_BIN: {
      // Each binary frame is one <=4KB PCM chunk.
      // Append to voiceBuffer when a voice:start is active.
      if (!voiceReceiving || !voiceBuffer) break;
      size_t space  = (voiceBufCapacity > voiceBufferOffset)
                        ? voiceBufCapacity - voiceBufferOffset : 0;
      size_t toCopy = min(length, space);
      if (toCopy > 0) {
        memcpy(voiceBuffer + voiceBufferOffset, payload, toCopy);
        voiceBufferOffset += toCopy;
      }
      Serial.printf("[Voice] +%u bytes | running total: %u\n", toCopy, voiceBufferOffset);
      break;
    }

    default:
      break;
  }
}


// ============================================================
// HTML PAGE  (served once over HTTP on port 80)
// ============================================================

void serveUI(WiFiClient& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println("Cache-Control: no-store");
  client.println("Connection: close");
  client.println();

  client.println("<!DOCTYPE html><html>");
  client.println("<head><meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'>");
  client.println("<link rel='icon' href='data:,'>");

  // --- CSS ---
  client.println("<style>");
  client.println("*{box-sizing:border-box;margin:0;padding:0;}");
  client.println("html,body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;background:#f5f5f5;height:100%;}");
  client.println("a{text-decoration:none;color:inherit;}");
  client.println("body{max-width:480px;margin:0 auto;background:#fff;box-shadow:0 0 20px rgba(0,0,0,.1);display:flex;flex-direction:column;min-height:100vh;}");

  // Header
  client.println(".header{padding:12px 16px;display:flex;align-items:center;justify-content:space-between;background:linear-gradient(135deg,#667eea,#764ba2);color:white;box-shadow:0 2px 8px rgba(0,0,0,.1);}");
  client.println(".header-left{display:flex;align-items:center;gap:12px;}");
  client.println(".menu-icon{width:28px;height:28px;display:flex;flex-direction:column;justify-content:space-around;cursor:pointer;padding:4px;}");
  client.println(".menu-icon span{display:block;height:3px;background:white;border-radius:2px;}");
  client.println(".logo-container{display:flex;align-items:center;gap:8px;}");
  client.println(".logo-circle{width:36px;height:36px;background:#1e3a5f;border-radius:50%;display:flex;align-items:center;justify-content:center;}");
  client.println(".logo-circle svg{width:32px;height:32px;}");
  client.println(".app-name{font-size:20px;font-weight:700;letter-spacing:.5px;}");
  client.println(".status-indicators{display:flex;align-items:center;gap:12px;}");
  client.println(".battery-indicator{display:flex;flex-direction:column;align-items:center;font-size:10px;}");
  client.println(".battery-icon{width:28px;height:14px;border:2px solid white;border-radius:3px;position:relative;padding:2px;}");
  client.println(".battery-icon::after{content:'';position:absolute;right:-4px;top:4px;width:3px;height:6px;background:white;border-radius:0 2px 2px 0;}");
  client.println(".battery-fill{height:100%;background:#4ade80;border-radius:1px;width:80%;}");

  // Live WebSocket status dot
  client.println(".ws-status{display:flex;flex-direction:column;align-items:center;gap:3px;font-size:10px;}");
  client.println(".ws-dot{width:10px;height:10px;border-radius:50%;background:#ef4444;transition:background .4s;}");
  client.println(".ws-dot.connected{background:#4ade80;}");

  // Main content
  client.println(".main-content{flex:1;padding:24px 20px;display:flex;flex-direction:column;gap:24px;}");
  client.println(".control-panel{display:flex;align-items:center;justify-content:space-between;gap:16px;}");

  // Speed buttons
  client.println(".speed-controls{display:flex;flex-direction:column;gap:12px;}");
  client.println(".speed-btn{display:flex;flex-direction:column;align-items:center;justify-content:center;width:70px;height:90px;background:linear-gradient(135deg,#f8f9fa,#e9ecef);border:2px solid #dee2e6;border-radius:12px;box-shadow:0 2px 8px rgba(0,0,0,.08);transition:all .2s;cursor:pointer;gap:6px;}");
  client.println(".speed-btn:active{background:linear-gradient(135deg,#e9ecef,#dee2e6);transform:translateY(2px);}");
  client.println(".arrow-up{width:0;height:0;border-left:16px solid transparent;border-right:16px solid transparent;border-bottom:24px solid #495057;}");
  client.println(".arrow-down{width:0;height:0;border-left:16px solid transparent;border-right:16px solid transparent;border-top:24px solid #495057;}");
  client.println(".speed-label{font-size:10px;font-weight:600;color:#495057;text-align:center;line-height:1.2;}");

  // D-Pad
  client.println(".d-pad{width:220px;height:220px;position:relative;flex-shrink:0;}");
  client.println(".d-pad-segment{position:absolute;width:100%;height:100%;clip-path:polygon(50% 50%,50% 0%,100% 0%,100% 50%);cursor:pointer;}");
  client.println(".d-pad-segment-inner{width:100%;height:100%;background:linear-gradient(135deg,#f8f9fa,#e9ecef);border:3px solid #dee2e6;border-radius:50%;transition:all .2s;box-shadow:inset 0 2px 4px rgba(0,0,0,.06);}");
  client.println(".d-pad-segment:active .d-pad-segment-inner{background:linear-gradient(135deg,#dee2e6,#ced4da);}");
  client.println(".segment-top{transform:rotate(-45deg);}.segment-right{transform:rotate(45deg);}.segment-bottom{transform:rotate(135deg);}.segment-left{transform:rotate(225deg);}");
  client.println(".segment-label{position:absolute;font-weight:600;font-size:11px;color:#495057;pointer-events:none;text-align:center;}");
  client.println(".label-top{top:10px;left:50%;transform:translateX(-50%);}");
  client.println(".label-right{right:10px;top:50%;transform:translateY(-50%);}");
  client.println(".label-bottom{bottom:10px;left:50%;transform:translateX(-50%);}");
  client.println(".label-left{left:10px;top:50%;transform:translateY(-50%);}");
  client.println(".center-btn{width:120px;height:120px;position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);border-radius:50%;background:linear-gradient(135deg,#ef4444,#dc2626);color:white;z-index:10;font-size:22px;font-weight:700;border:4px solid white;box-shadow:0 4px 12px rgba(239,68,68,.4);display:flex;align-items:center;justify-content:center;cursor:pointer;transition:all .2s;}");
  client.println(".center-btn:active{background:linear-gradient(135deg,#dc2626,#b91c1c);transform:translate(-50%,-50%) scale(.95);}");

  // Mic button
  client.println(".mic-container{display:flex;flex-direction:column;align-items:center;gap:8px;}");
  client.println(".mic-btn{width:80px;height:80px;background:linear-gradient(135deg,#f8f9fa,#e9ecef);border:2px solid #dee2e6;border-radius:50%;display:flex;align-items:center;justify-content:center;box-shadow:0 4px 12px rgba(0,0,0,.1);transition:all .2s;cursor:pointer;user-select:none;-webkit-user-select:none;touch-action:none;}");
  client.println(".mic-btn svg{width:36px;height:36px;fill:#495057;transition:fill .2s;}");
  client.println(".mic-btn.recording{background:linear-gradient(135deg,#ef4444,#dc2626);border-color:#dc2626;box-shadow:0 4px 20px rgba(239,68,68,.5);animation:pulse 1s infinite;}");
  client.println(".mic-btn.recording svg,.mic-btn.uploading svg{fill:white;}");
  client.println(".mic-btn.uploading{background:linear-gradient(135deg,#f59e0b,#d97706);border-color:#d97706;}");
  client.println("@keyframes pulse{0%,100%{transform:scale(1);}50%{transform:scale(1.08);}}");
  client.println(".mic-label{font-size:11px;font-weight:600;color:#6c757d;text-align:center;}");

  // Volume + custom grid
  client.println(".volume-container{padding:0 4px;}");
  client.println(".volume-label{display:block;text-align:center;font-size:13px;font-weight:600;margin-bottom:12px;color:#495057;}");
  client.println("input[type='range']{-webkit-appearance:none;width:100%;height:8px;background:#dee2e6;border-radius:4px;outline:none;}");
  client.println("input[type='range']::-webkit-slider-thumb{-webkit-appearance:none;width:24px;height:24px;background:linear-gradient(135deg,#667eea,#764ba2);border-radius:50%;cursor:pointer;box-shadow:0 2px 6px rgba(102,126,234,.4);}");
  client.println(".custom-grid{padding:0 4px 20px;display:grid;grid-template-columns:repeat(4,1fr);gap:10px;}");
  client.println(".custom-btn{height:70px;background:linear-gradient(135deg,#fff,#f8f9fa);border:2px solid #dee2e6;border-radius:12px;display:flex;align-items:center;justify-content:center;text-align:center;font-size:11px;font-weight:600;padding:8px;box-shadow:0 2px 6px rgba(0,0,0,.06);transition:all .2s;cursor:pointer;color:#495057;}");
  client.println(".custom-btn:active{background:linear-gradient(135deg,#e9ecef,#dee2e6);transform:translateY(2px);}");
  client.println(".custom-btn.empty{background:#f8f9fa;border:2px dashed #dee2e6;cursor:default;}");
  client.println("</style>");

  // --- JavaScript ---
  client.println("<script>");

  // WebSocket: connect on load, auto-reconnect if dropped
  client.println("var ws,wsDot,reconnTimer;");
  client.println("var voiceMaxSamples=32000;"); // updated from hello:vivicense:maxSamples=N
  client.println("function wsConnect(){");
  client.println("  ws=new WebSocket('ws://192.168.4.1:81/');");
  client.println("  ws.onopen=function(){clearTimeout(reconnTimer);if(wsDot)wsDot.classList.add('connected');console.log('[WS] Connected');};");
  client.println("  ws.onclose=function(){if(wsDot)wsDot.classList.remove('connected');reconnTimer=setTimeout(wsConnect,2000);};");
  client.println("  ws.onerror=function(e){console.warn('[WS] Error',e);};");
  client.println("  ws.onmessage=function(e){");
  client.println("    console.log('[WS] ESP32:',e.data);");
  client.println("    var m=e.data.match(/maxSamples=(\\d+)/);");
  client.println("    if(m){voiceMaxSamples=parseInt(m[1]);console.log('[Voice] Server maxSamples:',voiceMaxSamples,'('+Math.round(voiceMaxSamples/16000*10)/10+'s)');}");
  client.println("    if(e.data.startsWith('err:')){");
  client.println("      var l=document.getElementById('micLabel');");
  client.println("      if(l){l.textContent='Error: '+e.data;setTimeout(function(){l.textContent='Tap to Speak';},3000);}");
  client.println("    }");
  client.println("  };");
  client.println("}");

  // send() — text command helper, silently drops if socket not open
  client.println("function send(cmd){if(ws&&ws.readyState===WebSocket.OPEN)ws.send(cmd);else console.warn('[WS] Not connected, dropped:',cmd);}");

  // Volume
  client.println("function updateVolume(v){send('vol:'+v);}");

  // ── Push-to-speak: click once to start, click again to stop & send ──
  //
  // State machine:  idle → recording (red pulse) → sending (amber) → idle
  //
  // FIX: The WebSockets library caps each binary frame at ~8192 bytes.
  // Sending the whole recording as one blob silently fails for anything
  // longer than ~0.5s.  Instead we:
  //   1. Send text "voice:start" to prime the ESP32 buffer
  //   2. Slice the Int16 PCM into CHUNK_BYTES chunks and send each one
  //   3. Send text "voice:end" to trigger playback
  //
  // Each chunk is 4096 bytes — well within the library limit.
  client.println("var CHUNK_BYTES=4096;");
  client.println("(function(){");
  client.println("var btn,lbl,stream,ctx,source,proc;");
  client.println("var chunks=[],recording=false,busy=false,lastToggle=0;");
  client.println("");
  client.println("function init(){");
  client.println("  wsDot=document.getElementById('wsDot');");
  client.println("  btn=document.getElementById('micBtn');");
  client.println("  lbl=document.getElementById('micLabel');");
  client.println("  btn.addEventListener('click',onToggle);");
  client.println("  btn.addEventListener('touchend',function(e){e.preventDefault();onToggle();},{passive:false});");
  client.println("  wsConnect();");
  client.println("}");
  client.println("");
  client.println("function onToggle(){");
  client.println("  var now=Date.now();if(now-lastToggle<500||busy)return;lastToggle=now;");
  client.println("  if(!recording)startRecording();");
  client.println("  else stopRecording();");
  client.println("}");
  client.println("");
  client.println("async function startRecording(){");
  client.println("  if(!ws||ws.readyState!==WebSocket.OPEN){");
  client.println("    lbl.textContent='Not connected';");
  client.println("    setTimeout(function(){lbl.textContent='Tap to Speak';},2000);");
  client.println("    return;");
  client.println("  }");
  client.println("  try{");
  client.println("    stream=await navigator.mediaDevices.getUserMedia({audio:{channelCount:1,echoCancellation:true,noiseSuppression:true}});");
  client.println("    ctx=new(window.AudioContext||window.webkitAudioContext)();");
  client.println("    await ctx.resume();");
  client.println("    var actualRate=ctx.sampleRate;");
  client.println("    console.log('[Voice] AudioContext rate: '+actualRate+'Hz');");
  client.println("    source=ctx.createMediaStreamSource(stream);");
  // 4096-sample buffer = 256ms per callback at 16kHz — good balance of
  // responsiveness vs CPU. Warmup skips the first ~200ms while the
  // AudioContext and mic gain stabilise, preventing a clipped first syllable.
  client.println("    proc=ctx.createScriptProcessor(4096,1,1);");
  client.println("    chunks=[];");
  client.println("    var warmup=true;");
  client.println("    setTimeout(function(){warmup=false;},200);");
  client.println("    proc.onaudioprocess=function(e){");
  client.println("      if(!recording||warmup)return;");
  client.println("      chunks.push(new Float32Array(e.inputBuffer.getChannelData(0)));");
  client.println("    };");
  client.println("    var silentGain=ctx.createGain();silentGain.gain.value=0;");
  client.println("    source.connect(proc);proc.connect(silentGain);silentGain.connect(ctx.destination);");
  client.println("    recording=true;");
  client.println("    setTimeout(function(){if(recording)stopRecording();},Math.round(voiceMaxSamples/16)+500);");
  client.println("    btn.classList.add('recording');");
  client.println("    lbl.textContent='Tap to Stop';");
  client.println("  }catch(err){");
  client.println("    if(stream){stream.getTracks().forEach(function(t){t.stop();});stream=null;}");
  client.println("    if(ctx){try{ctx.close();}catch(ignore){} ctx=null;}");
  client.println("    source=null;proc=null;recording=false;busy=false;");
  client.println("    lbl.textContent='Mic error';setTimeout(function(){lbl.textContent='Tap to Speak';},3000);");
  client.println("  }");
  client.println("}");
  client.println("");
  client.println("var capturedRate=44100;");
  client.println("function stopRecording(){");
  client.println("  recording=false;busy=true;");
  client.println("  if(proc){proc.disconnect();proc=null;}");
  client.println("  if(source){source.disconnect();source=null;}");
  client.println("  if(stream){stream.getTracks().forEach(function(t){t.stop();});stream=null;}");
  client.println("  capturedRate=ctx?ctx.sampleRate:44100;");
  client.println("  if(ctx){ctx.close();ctx=null;}");
  client.println("  btn.classList.remove('recording');btn.classList.add('uploading');");
  client.println("  lbl.textContent='Sending...';");
  client.println("  sendAudio().catch(function(e){console.error('[Voice]',e);resetBtn();});");
  client.println("}");
  client.println("");
  client.println("async function sendAudio(){");
  client.println("  if(!chunks.length||!ws||ws.readyState!==WebSocket.OPEN){");
  client.println("    if(!chunks.length){lbl.textContent='Nothing recorded';setTimeout(function(){lbl.textContent='Tap to Speak';},2000);}");
  client.println("    busy=false;return;");
  client.println("  }");
  client.println("  // 1. Flatten Float32 chunks → one array at the native capture rate");
  client.println("  var total=0;");
  client.println("  for(var i=0;i<chunks.length;i++)total+=chunks[i].length;");
  client.println("  var flat=new Float32Array(total),off=0;");
  client.println("  for(var i=0;i<chunks.length;i++){var c=chunks[i];for(var j=0;j<c.length;j++)flat[off++]=c[j];}");
  client.println("  chunks=[];");
  client.println("  // 2. Resample to 16000 Hz via OfflineAudioContext so the ESP32");
  client.println("  //    always receives PCM at its I2S-initialised rate.");
  client.println("  var TARGET_RATE=16000;");
  client.println("  var pcmFloat=flat;");
  client.println("  if(capturedRate!==TARGET_RATE){");
  client.println("    try{");
  client.println("      var numOut=Math.round(total*TARGET_RATE/capturedRate);");
  client.println("      var offCtx=new OfflineAudioContext(1,numOut,TARGET_RATE);");
  client.println("      var buf=offCtx.createBuffer(1,total,capturedRate);");
  client.println("      buf.copyToChannel(flat,0);");
  client.println("      var src=offCtx.createBufferSource();");
  client.println("      src.buffer=buf;src.connect(offCtx.destination);src.start();");
  client.println("      var rendered=await offCtx.startRendering();");
  client.println("      pcmFloat=rendered.getChannelData(0);");
  client.println("      console.log('[Voice] Resampled '+capturedRate+'→'+TARGET_RATE+' Hz ('+total+'→'+pcmFloat.length+' samples)');");
  client.println("    }catch(e){console.warn('[Voice] Resample failed, sending at native rate',e);}");
  client.println("  }");
  client.println("  // 3. Trim to server buffer capacity (received in hello:vivicense:maxSamples=N)");
  client.println("  if(pcmFloat.length>voiceMaxSamples){");
  client.println("    console.warn('[Voice] Trimmed '+pcmFloat.length+' → '+voiceMaxSamples+' samples');");
  client.println("    pcmFloat=pcmFloat.subarray(0,voiceMaxSamples);");
  client.println("  }");
  client.println("  // 4. Convert Float32 → Int16 PCM");
  client.println("  var pcm=new Int16Array(pcmFloat.length);");
  client.println("  for(var i=0;i<pcmFloat.length;i++)");
  client.println("    pcm[i]=Math.max(-32768,Math.min(32767,Math.round(pcmFloat[i]*32767)));");
  client.println("  // 5. Send start signal — always 16 kHz after resampling");
  client.println("  ws.send('voice:start:'+TARGET_RATE);");
  client.println("  // 6. Slice into CHUNK_BYTES frames and send each");
  client.println("  var samplesPerChunk=CHUNK_BYTES/2;"); // Int16 = 2 bytes/sample
  client.println("  for(var s=0;s<pcm.length;s+=samplesPerChunk){");
  client.println("    var slice=pcm.slice(s,Math.min(s+samplesPerChunk,pcm.length));");
  client.println("    ws.send(slice.buffer);");
  client.println("  }");
  client.println("  // 7. Send end signal — ESP32 plays on receipt");
  client.println("  ws.send('voice:end');");
  client.println("  lbl.textContent='Playing!';");
  client.println("  var playMs=Math.round(pcmFloat.length/TARGET_RATE*1000)+500;");
  client.println("  setTimeout(resetBtn,playMs);");
  client.println("}");
  client.println("");
  client.println("function resetBtn(){");
  client.println("  busy=false;");
  client.println("  btn.classList.remove('uploading');");
  client.println("  lbl.textContent='Tap to Speak';");
  client.println("}");
  client.println("");
  client.println("document.addEventListener('DOMContentLoaded',init);");
  client.println("})();");
  client.println("</script></head>");

  // --- BODY ---
  client.println("<body>");

  // Header — live WS dot replaces old BT indicator
  client.println("<div class='header'>");
  client.println("  <div class='header-left'>");
  client.println("    <div class='menu-icon'><span></span><span></span><span></span></div>");
  client.println("    <div class='logo-container'>");
  client.println("      <div class='logo-circle'><svg viewBox='0 0 100 100'><circle cx='35' cy='35' r='12' fill='white'/><path d='M 35 50 Q 25 50 20 58 Q 15 66 15 75 L 15 85 Q 15 90 20 90 L 50 90 Q 55 90 55 85 L 55 75 Q 55 66 50 58 Q 45 50 35 50 Z' fill='white'/><path d='M 60 25 Q 62 27 64 32' stroke='white' stroke-width='4' fill='none'/><path d='M 67 20 Q 71 24 75 35' stroke='white' stroke-width='4' fill='none'/><path d='M 75 15 Q 81 21 87 38' stroke='white' stroke-width='4' fill='none'/></svg></div>");
  client.println("      <span class='app-name'>ViviSense</span>");
  client.println("    </div>");
  client.println("  </div>");
  client.println("  <div class='status-indicators'>");
  client.println("    <div class='battery-indicator'><div class='battery-icon'><div class='battery-fill'></div></div><span>80%</span></div>");
  client.println("    <div class='ws-status'><div class='ws-dot' id='wsDot'></div><span>Live</span></div>");
  client.println("  </div>");
  client.println("</div>");

  // Main content
  client.println("<div class='main-content'><div class='control-panel'>");

  // Speed — onclick sends WS text, no page reload
  client.println("  <div class='speed-controls'>");
  client.println("    <div class='speed-btn' onclick=\"send('speedup')\"><div class='arrow-up'></div><div class='speed-label'>Speed<br>Up</div></div>");
  client.println("    <div class='speed-btn' onclick=\"send('slowdown')\"><div class='arrow-down'></div><div class='speed-label'>Slow<br>Down</div></div>");
  client.println("  </div>");

  // D-Pad — onclick sends WS text, no page reload
  client.println("  <div class='d-pad'>");
  client.println("    <div class='d-pad-segment segment-top'    onclick=\"send('forward')\"><div class='d-pad-segment-inner'></div></div><div class='segment-label label-top'>Go Forward</div>");
  client.println("    <div class='d-pad-segment segment-right'  onclick=\"send('right')\"  ><div class='d-pad-segment-inner'></div></div><div class='segment-label label-right'>Turn<br>Right</div>");
  client.println("    <div class='d-pad-segment segment-bottom' onclick=\"send('backup')\" ><div class='d-pad-segment-inner'></div></div><div class='segment-label label-bottom'>Back Up</div>");
  client.println("    <div class='d-pad-segment segment-left'   onclick=\"send('left')\"   ><div class='d-pad-segment-inner'></div></div><div class='segment-label label-left'>Turn<br>Left</div>");
  client.println("    <div class='center-btn' onclick=\"send('stop')\">Stop!</div>");
  client.println("  </div>");

  // Mic
  client.println("  <div class='mic-container'>");
  client.println("    <div class='mic-btn' id='micBtn'><svg viewBox='0 0 24 24'><path d='M12 14c1.66 0 3-1.34 3-3V5c0-1.66-1.34-3-3-3S9 3.34 9 5v6c0 1.66 1.34 3 3 3z'/><path d='M17 11c0 2.76-2.24 5-5 5s-5-2.24-5-5H5c0 3.53 2.61 6.43 6 6.92V21h2v-3.08c3.39-.49 6-3.39 6-6.92h-2z'/></svg></div>");
  client.println("    <div class='mic-label' id='micLabel'>Tap to<br>Speak</div>");
  client.println("  </div>");

  client.println("</div>"); // .control-panel

  // Volume slider
  client.println("<div class='volume-container'>");
  client.println("  <label class='volume-label' for='vol'>Volume Control</label>");
  client.println("  <input type='range' id='vol' min='0' max='255' value='" + String(currentVolume) + "' onchange='updateVolume(this.value)'>");
  client.println("</div>");

  // Custom buttons
  client.println("<div class='custom-grid'>");
  client.println("  <div class='custom-btn' onclick=\"send('custom1')\">Stay on<br>the line</div>");
  client.println("  <div class='custom-btn empty'></div><div class='custom-btn empty'></div><div class='custom-btn empty'></div>");
  client.println("  <div class='custom-btn empty'></div><div class='custom-btn empty'></div><div class='custom-btn empty'></div><div class='custom-btn empty'></div>");
  client.println("</div>");

  client.println("</div></body></html>"); // .main-content
  client.println();
}


// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  // Adaptively allocate voice buffer BEFORE WiFi to get a contiguous heap block.
  // Try progressively smaller sizes until one succeeds; the actual capacity is
  // sent to the browser in the WebSocket hello message so it can trim PCM accordingly.
  Serial.printf("[Heap] Free before alloc: %u bytes\n", ESP.getFreeHeap());
  static const uint32_t BUF_SIZES[] = {128000, 96000, 80000, 64000, 0};
  for (int i = 0; BUF_SIZES[i] > 0; i++) {
    voiceBuffer = (uint8_t*)malloc(BUF_SIZES[i]);
    if (voiceBuffer) { voiceBufCapacity = BUF_SIZES[i]; break; }
    Serial.printf("[Voice] malloc(%u) failed, trying smaller\n", BUF_SIZES[i]);
  }
  if (voiceBuffer) Serial.printf("[Voice] Buffer: %u bytes (%.1f sec)\n",
                                  voiceBufCapacity, voiceBufCapacity / 32000.0f);
  else             Serial.println("[Voice] WARNING: all alloc attempts failed");
  setupI2S();

  WiFi.softAP(ssid, password);
  Serial.print("[WiFi] AP started — IP: ");
  Serial.println(WiFi.softAPIP());

  httpServer.begin();
  Serial.println("[HTTP] Listening on port 80");

  wsServer.begin();
  wsServer.onEvent(webSocketEvent);
  Serial.println("[WS]   Listening on port 81");
}


// ============================================================
// LOOP
// ============================================================

void loop() {
  // 1. Let the WebSocket library process incoming frames
  wsServer.loop();

  // 2. Serve the HTML page to any new browser connection on port 80
  WiFiClient httpClient = httpServer.available();
  if (httpClient) {
    unsigned long start = millis();
    String req = "";
    while (httpClient.connected() && millis() - start < 2000) {
      if (httpClient.available()) {
        req += (char)httpClient.read();
        if (req.endsWith("\r\n\r\n")) break; // end of HTTP headers
      }
    }
    serveUI(httpClient);
    httpClient.stop();
  }

  // 3. Execute any pending action flagged by the WS callback.
  //    Snapshot and clear the flag before the blocking audio call
  //    so new WS messages aren't lost while audio is playing.
  if (pendingAction != ACTION_NONE) {
    PendingAction action = pendingAction;
    pendingAction = ACTION_NONE;

    switch (action) {
      case ACTION_STOP:      handleStop();      break;
      case ACTION_SPEED_UP:  handleSpeedUp();   break;
      case ACTION_SLOW_DOWN: handleSlowDown();  break;
      case ACTION_FORWARD:   handleGoForward(); break;
      case ACTION_LEFT:      handleTurnLeft();  break;
      case ACTION_RIGHT:     handleTurnRight(); break;
      case ACTION_BACKUP:    handleBackUp();    break;
      case ACTION_CUSTOM1:   handleCustom1();   break;
      case ACTION_VOICE:
        if (voiceBuffer && voiceBufferLen > 0) {
          playVoiceData(voiceBuffer, voiceBufferLen);
          wsServer.sendTXT(activeClient, "ack:voice");
        }
        break;
      default: break;
    }
  }
}