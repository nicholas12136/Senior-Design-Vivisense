// WebSocket client - connects to the ESP32's server at ws://{host}/ws
// The host resolves automatically whether served from the ESP32 or a dev server.

type MessageHandler = (data: unknown) => void;
type ComponentState = 'online' | 'degraded' | 'offline' | 'unknown';

interface StatusMessage {
  type: 'status';
  mainControllerConnected?: boolean;
  ledControllerConnected?: boolean;
  leftPodState?: string;
  rightPodState?: string;
  towerPodState?: string;
  leftPodOnlineSensors?: number;
  rightPodOnlineSensors?: number;
  towerPodOnlineSensors?: number;
}

const WS_PATH = '/ws';

let ws: WebSocket | null = null;
let reconnectTimer: ReturnType<typeof setTimeout> | null = null;
const messageHandlers: MessageHandler[] = [];
let lastStatusMessage: StatusMessage | null = null;

function updateStatusDot(connected: boolean): void {
  const dot = document.getElementById('ws-dot');
  if (!dot) return;
  dot.classList.toggle('connected', connected);
}

function setComponentChip(id: string, state: ComponentState, value: string): void {
  const chip = document.getElementById(id);
  if (!chip) return;

  chip.classList.remove('state-online', 'state-degraded', 'state-offline', 'state-unknown');
  chip.classList.add(`state-${state}`);

  const valueEl = chip.querySelector('.component-value');
  if (valueEl) valueEl.textContent = value;
}

function parseComponentState(value: unknown): ComponentState {
  if (value === 'online' || value === 'degraded' || value === 'offline' || value === 'unknown') {
    return value;
  }
  return 'unknown';
}

function updateComponentStatusUI(status: StatusMessage | null): void {
  if (!status) {
    setComponentChip('comp-main', 'unknown', '--');
    setComponentChip('comp-led', 'unknown', '--');
    setComponentChip('comp-left-pod', 'unknown', '--');
    setComponentChip('comp-right-pod', 'unknown', '--');
    setComponentChip('comp-tower-pod', 'unknown', '--');
    return;
  }

  const mainConnected = status.mainControllerConnected === true;
  const ledConnected = status.ledControllerConnected === true;

  const leftCount  = Number.isFinite(status.leftPodOnlineSensors)  ? Number(status.leftPodOnlineSensors)  : null;
  const rightCount = Number.isFinite(status.rightPodOnlineSensors) ? Number(status.rightPodOnlineSensors) : null;
  const towerCount = Number.isFinite(status.towerPodOnlineSensors) ? Number(status.towerPodOnlineSensors) : null;

  const leftState  = parseComponentState(status.leftPodState);
  const rightState = parseComponentState(status.rightPodState);
  const towerState = parseComponentState(status.towerPodState);

  setComponentChip('comp-main', mainConnected ? 'online' : 'offline', mainConnected ? 'Online' : 'Offline');
  setComponentChip('comp-led',  ledConnected  ? 'online' : 'offline', ledConnected  ? 'Online' : 'Offline');
  setComponentChip('comp-left-pod',  leftState,  leftCount  !== null ? `${leftCount}/2`  : '--');
  setComponentChip('comp-right-pod', rightState, rightCount !== null ? `${rightCount}/2` : '--');
  setComponentChip('comp-tower-pod', towerState, towerCount !== null ? `${towerCount}/4` : '--');
}

function asStatusMessage(value: unknown): StatusMessage | null {
  if (!value || typeof value !== 'object') return null;
  const record = value as Record<string, unknown>;
  if (record['type'] !== 'status') return null;
  return record as unknown as StatusMessage;
}

function connect(): void {
  const url = `ws://${window.location.host}${WS_PATH}`;
  ws = new WebSocket(url);

  ws.onopen = () => {
    updateStatusDot(true);
    if (reconnectTimer) {
      clearTimeout(reconnectTimer);
      reconnectTimer = null;
    }
    updateComponentStatusUI(lastStatusMessage);
    console.log('[WS] Connected');
  };

  ws.onclose = () => {
    updateStatusDot(false);
    updateComponentStatusUI(null);
    console.log('[WS] Disconnected - retrying in 3 s');
    reconnectTimer = setTimeout(connect, 3000);
  };

  ws.onerror = () => {
    ws?.close();
  };

  ws.onmessage = (event) => {
    try {
      const data: unknown = JSON.parse(event.data as string);
      const status = asStatusMessage(data);
      if (status) {
        lastStatusMessage = status;
        updateComponentStatusUI(lastStatusMessage);
      }
      messageHandlers.forEach(h => h(data));
    } catch {
      // ignore malformed frames
    }
  };
}

export function connectWebSocket(): void {
  if (!ws || ws.readyState === WebSocket.CLOSED) connect();
  // Sync indicators to current state (DOM may have been replaced by route changes)
  else {
    updateStatusDot(ws.readyState === WebSocket.OPEN);
    updateComponentStatusUI(lastStatusMessage);
  }
}

export function sendMessage(msg: object): void {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(msg));
    console.debug('[WS] sent:', JSON.stringify(msg).substring(0, 120));
  } else {
    console.warn('[WS] message dropped - not connected:', JSON.stringify(msg).substring(0, 80));
  }
}

export function onMessage(handler: MessageHandler): void {
  messageHandlers.push(handler);
}

export function offMessage(handler: MessageHandler): void {
  const idx = messageHandlers.indexOf(handler);
  if (idx !== -1) messageHandlers.splice(idx, 1);
}
