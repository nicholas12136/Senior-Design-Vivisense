var canvas = document.getElementById("gridCanvas");
var ctx = canvas.getContext("2d");

var wsUrlInput = document.getElementById("wsUrl");
var connectBtn = document.getElementById("connectBtn");
var connState = document.getElementById("connState");
var showInflatedCheckbox = document.getElementById("showInflated");
var durationInput = document.getElementById("durationSec");
var startLogBtn = document.getElementById("startLogBtn");
var stopLogBtn = document.getElementById("stopLogBtn");
var logState = document.getElementById("logState");
var fpsState = document.getElementById("fpsState");

var zoneFront = document.getElementById("zoneFront");
var zoneLeft = document.getElementById("zoneLeft");
var zoneRight = document.getElementById("zoneRight");
var zoneRear = document.getElementById("zoneRear");

var ws = null;
var gridMsgCount = 0;
var lastFpsTime = Date.now();

var latest = {
  Nx: 40,
  Ny: 40,
  age: [],
  raw: [],
  inflated: [],
  zones: null,
  logging: false
};

var browserRecording = false;
var csvRows = [];
var prevLoggingState = false;

function defaultWsUrl() {
  return "ws://" + window.location.host + "/ws";
}

wsUrlInput.value = defaultWsUrl();

function setConnState(text) {
  connState.textContent = text;
}

function severityClass(sev) {
  if (sev === "urgent") return "sev-urgent";
  if (sev === "warning") return "sev-warning";
  return "sev-safe";
}

function formatZoneLine(name, z) {
  if (!z) return name + ": -";
  var dText = z.d < 0 ? "none" : z.d.toFixed(2) + " m";
  return name + ": d=" + dText + ", count=" + z.count + ", sev=" + z.sev;
}

function renderZones(zones) {
  if (!zones) return;

  zoneFront.textContent = formatZoneLine("Front", zones.front);
  zoneLeft.textContent = formatZoneLine("Left", zones.left);
  zoneRight.textContent = formatZoneLine("Right", zones.right);
  zoneRear.textContent = formatZoneLine("Rear", zones.rear);

  zoneFront.className = "zone " + severityClass(zones.front ? zones.front.sev : "safe");
  zoneLeft.className = "zone " + severityClass(zones.left ? zones.left.sev : "safe");
  zoneRight.className = "zone " + severityClass(zones.right ? zones.right.sev : "safe");
  zoneRear.className = "zone " + severityClass(zones.rear ? zones.rear.sev : "safe");
}

function drawGrid() {
  var Nx = latest.Nx;
  var Ny = latest.Ny;
  var values;

  if (showInflatedCheckbox.checked) {
    values = latest.inflated;
  } else {
    values = latest.raw;
  }

  if (!values || values.length !== Nx * Ny) {
    return;
  }

  var w = canvas.width;
  var h = canvas.height;
  var cellW = w / Ny;
  var cellH = h / Nx;

  ctx.fillStyle = "#101820";
  ctx.fillRect(0, 0, w, h);

  var ix, iy;
  for (iy = 0; iy < Ny; iy++) {
    for (ix = 0; ix < Nx; ix++) {
      var idx = iy * Nx + ix; // row-major from ESP32
      var v = values[idx];
      var age = latest.age[idx];

      if (!v && (!age || age <= 0)) {
        continue;
      }

      var intensity = Math.min(255, (age || 1) * 36);
      if (showInflatedCheckbox.checked) {
        ctx.fillStyle = "rgb(" + intensity + "," + intensity + ",255)";
      } else {
        ctx.fillStyle = "rgb(" + intensity + ",255," + intensity + ")";
      }

      // Coordinate mapping:
      // +X forward should be UP on screen:
      //   ix grows with +X, but canvas Y grows downward, so invert ix.
      // +Y left should be LEFT on screen:
      //   iy grows with +Y, but canvas X grows rightward, so invert iy.
      var screenRow = (Nx - 1) - ix;
      var screenCol = (Ny - 1) - iy;

      var x = screenCol * cellW;
      var y = screenRow * cellH;
      ctx.fillRect(x, y, cellW, cellH);
    }
  }

  ctx.strokeStyle = "rgba(255,255,255,0.12)";
  for (var c = 0; c <= Ny; c++) {
    var gx = c * cellW;
    ctx.beginPath();
    ctx.moveTo(gx, 0);
    ctx.lineTo(gx, h);
    ctx.stroke();
  }
  for (var r = 0; r <= Nx; r++) {
    var gy = r * cellH;
    ctx.beginPath();
    ctx.moveTo(0, gy);
    ctx.lineTo(w, gy);
    ctx.stroke();
  }
}

function updateFps() {
  var now = Date.now();
  var dt = now - lastFpsTime;
  if (dt >= 1000) {
    var fps = (gridMsgCount * 1000.0) / dt;
    fpsState.textContent = "FPS: " + fps.toFixed(1);
    gridMsgCount = 0;
    lastFpsTime = now;
  }
}

function toCsvTimestamp() {
  var d = new Date();
  var y = d.getFullYear();
  var mo = String(d.getMonth() + 1).padStart(2, "0");
  var da = String(d.getDate()).padStart(2, "0");
  var hh = String(d.getHours()).padStart(2, "0");
  var mm = String(d.getMinutes()).padStart(2, "0");
  var ss = String(d.getSeconds()).padStart(2, "0");
  return "" + y + mo + da + "_" + hh + mm + ss;
}

function downloadCsvIfAny() {
  if (csvRows.length === 0) {
    csvRows = [];
    return;
  }

  var header = "timestamp_ms,sid,x_m,y_m,z_m,ix,iy\n";
  var body = csvRows.join("\n") + "\n";
  var blob = new Blob([header + body], { type: "text/csv" });
  var url = URL.createObjectURL(blob);

  var a = document.createElement("a");
  a.href = url;
  a.download = "vivisense_points_" + toCsvTimestamp() + ".csv";
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);

  csvRows = [];
}

function stopBrowserRecordingAndDownload() {
  if (!browserRecording) {
    return;
  }
  browserRecording = false;
  downloadCsvIfAny();
}

function handleGridMessage(msg) {
  if (msg.params) {
    latest.Nx = msg.params.Nx;
    latest.Ny = msg.params.Ny;
  }
  latest.age = msg.age || latest.age;
  latest.raw = msg.raw || latest.raw;
  latest.inflated = msg.inflated || latest.inflated;
  latest.zones = msg.zones || latest.zones;
  latest.logging = !!msg.logging;

  logState.textContent = "Logging: " + latest.logging;
  renderZones(latest.zones);
  drawGrid();

  gridMsgCount += 1;
  updateFps();

  if (prevLoggingState && !latest.logging) {
    stopBrowserRecordingAndDownload();
  }
  prevLoggingState = latest.logging;
}

function handlePointsMessage(msg) {
  if (!browserRecording) {
    return;
  }
  if (!msg.points || !Array.isArray(msg.points)) {
    return;
  }
  var t = (typeof msg.t === "number") ? msg.t : 0;
  var i;
  for (i = 0; i < msg.points.length; i++) {
    var p = msg.points[i];
    csvRows.push(
      t + "," +
      (p.sid || 0) + "," +
      (Number(p.x || 0).toFixed(3)) + "," +
      (Number(p.y || 0).toFixed(3)) + "," +
      (Number(p.z || 0).toFixed(3)) + "," +
      (p.ix == null ? -1 : p.ix) + "," +
      (p.iy == null ? -1 : p.iy)
    );
  }
}

function handleMessageText(text) {
  var msg;
  try {
    msg = JSON.parse(text);
  } catch (e) {
    return;
  }

  if (msg.age && msg.zones) {
    handleGridMessage(msg);
    return;
  }

  if (msg.points) {
    handlePointsMessage(msg);
    return;
  }
}

function connectWs() {
  if (ws) {
    ws.close();
    ws = null;
  }

  ws = new WebSocket(wsUrlInput.value.trim());

  ws.onopen = function () {
    setConnState("Connected");
  };

  ws.onclose = function () {
    setConnState("Disconnected");
  };

  ws.onerror = function () {
    setConnState("Error");
  };

  ws.onmessage = function (evt) {
    handleMessageText(evt.data);
  };
}

function sendCmd(obj) {
  if (!ws || ws.readyState !== WebSocket.OPEN) {
    return;
  }
  ws.send(JSON.stringify(obj));
}

connectBtn.addEventListener("click", function () {
  connectWs();
});

startLogBtn.addEventListener("click", function () {
  var duration = parseInt(durationInput.value, 10);
  if (!duration || duration < 1) duration = 10;
  if (duration > 120) duration = 120;

  csvRows = [];
  browserRecording = true;
  sendCmd({ cmd: "start_log", duration_s: duration });
});

stopLogBtn.addEventListener("click", function () {
  sendCmd({ cmd: "stop_log" });
  stopBrowserRecordingAndDownload();
});

showInflatedCheckbox.addEventListener("change", function () {
  drawGrid();
});

connectWs();
