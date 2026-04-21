// WebSocket client — connects to the ESP32's server at ws://{host}/ws
// The host resolves automatically whether served from the ESP32 or a dev server.

type MessageHandler = (data: unknown) => void;

const WS_PATH = '/ws';

let ws: WebSocket | null = null;
let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
const messageHandlers: MessageHandler[] = [];

function updateStatusDot(connected: boolean): void {
  const dot = document.getElementById('ws-dot');
  if (!dot) return;
  dot.classList.toggle('connected', connected);
}

function connect(): void {
  const url = `ws://${window.location.host}${WS_PATH}`;
  ws = new WebSocket(url);

  ws.onopen = () => {
    updateStatusDot(true);
    if (reconnectTimer) { clearTimeout(reconnectTimer); reconnectTimer = null; }
    console.log('[WS] Connected');
  };

  ws.onclose = () => {
    updateStatusDot(false);
    console.log('[WS] Disconnected — retrying in 3 s');
    reconnectTimer = setTimeout(connect, 3000);
  };

  ws.onerror = () => {
    ws?.close();
  };

  ws.onmessage = (event) => {
    try {
      const data: unknown = JSON.parse(event.data as string);
      messageHandlers.forEach(h => h(data));
    } catch {
      // ignore malformed frames
    }
  };
}

export function connectWebSocket(): void {
  if (!ws || ws.readyState === WebSocket.CLOSED) connect();
  // Sync the dot to current WS state (the dot element may have been replaced by a navigation)
  else updateStatusDot(ws.readyState === WebSocket.OPEN);
}

export function sendMessage(msg: object): void {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(msg));
    console.debug('[WS] sent:', JSON.stringify(msg).substring(0, 120));
  } else {
    console.warn('[WS] message dropped — not connected:', JSON.stringify(msg).substring(0, 80));
  }
}

export function onMessage(handler: MessageHandler): void {
  messageHandlers.push(handler);
}

export function offMessage(handler: MessageHandler): void {
  const idx = messageHandlers.indexOf(handler);
  if (idx !== -1) messageHandlers.splice(idx, 1);
}
