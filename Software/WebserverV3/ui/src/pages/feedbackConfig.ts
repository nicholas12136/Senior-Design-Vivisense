import {
  buildRingSVG,
  sectorModeColorFn,
  radarModeColorFn,
  SECTOR_LABELS_4,
  SECTOR_LABELS_6,
  SECTOR_LABELS_8,
  RED, YELLOW, GREEN,
  LED_OFF,
  type Sector,
  type LEDPosition,
} from '../utils/ledRing.js';
import { sendMessage } from '../utils/websocket.js';

type FeedbackMode = 'sector' | 'radar';
type AudioFeedbackMode = 'tonal' | 'verbal';
type SectorCount = 4 | 6 | 8;

interface AdvancedSettings {
  thresholds: { redMax: number; yellowMax: number }; // distances in cm
  sectorCount: SectorCount;
  activeSectors: Partial<Record<Sector, boolean>>;
  brightness: number;
}

interface ConfigState {
  selectedMode: FeedbackMode | null;
  selectedAudioMode: AudioFeedbackMode | null;
  advanced: AdvancedSettings;
}

function getSectorLabels(count: SectorCount): Sector[] {
  if (count === 4) return SECTOR_LABELS_4;
  if (count === 8) return SECTOR_LABELS_8;
  return SECTOR_LABELS_6;
}

function buildDefaultSectors(count: SectorCount): Partial<Record<Sector, boolean>> {
  return Object.fromEntries(getSectorLabels(count).map(s => [s, true]));
}

function defaultState(): ConfigState {
  return {
    selectedMode: null,
    selectedAudioMode: null,
    advanced: {
      thresholds: { redMax: 60, yellowMax: 150 },
      sectorCount: 6,
      activeSectors: buildDefaultSectors(6),
      brightness: 100,
    },
  };
}

let state: ConfigState = defaultState();
let previewActive = false;

// Ring → approximate max distance in cm (ring 0 = innermost/closest)
const RING_DISTANCES = [20, 50, 100, 150, 200, 300];

function getColorForRing(ring: number): string {
  const dist = RING_DISTANCES[ring] ?? 0;
  if (dist <= state.advanced.thresholds.redMax) return RED;
  if (dist <= state.advanced.thresholds.yellowMax) return YELLOW;
  return GREEN;
}

function advancedColorFn(led: LEDPosition): string {
  if (!state.advanced.activeSectors[led.sector]) return LED_OFF;
  return getColorForRing(led.ring);
}

function refreshPreview(): void {
  const el = document.getElementById('advanced-preview');
  if (!el) return;
  const labels = getSectorLabels(state.advanced.sectorCount);
  el.innerHTML = buildRingSVG(220, advancedColorFn, state.advanced.brightness, true, labels);
}

// Sends the current advanced settings to the ESP32 for obstacle detection mode.
// Called whenever settings change so the device stays in sync even when not previewing.
function sendConfig(): void {
  const { thresholds, sectorCount, brightness } = state.advanced;
  const labels = getSectorLabels(sectorCount);
  const activeSectorsArray = labels.map(s => state.advanced.activeSectors[s] ?? true);
  sendMessage({
    type:            'config',
    zoneMode:        sectorCount,
    brightness:      brightness,
    redThreshold:    thresholds.redMax,
    yellowThreshold: thresholds.yellowMax,
    activeSectors:   activeSectorsArray,
  });
}

// Sends a preview message to the ESP32.
// active=true  → light the ring to match the current live preview
// active=false → clear the ring and return to obstacle detection mode
function sendPreview(active: boolean): void {
  if (!active) {
    sendMessage({ type: 'preview', active: false });
    return;
  }
  const { thresholds, sectorCount, brightness } = state.advanced;
  const labels = getSectorLabels(sectorCount);
  const activeSectorsArray = labels.map(s => state.advanced.activeSectors[s] ?? true);
  sendMessage({
    type:            'preview',
    active:          true,
    zoneMode:        sectorCount,
    brightness:      brightness,
    redThreshold:    thresholds.redMax,
    yellowThreshold: thresholds.yellowMax,
    activeSectors:   activeSectorsArray,
  });
}

function renderSectorToggles(): string {
  return getSectorLabels(state.advanced.sectorCount).map(sector => {
    const isActive = state.advanced.activeSectors[sector] ?? false;
    return `<button class="sector-toggle${isActive ? ' active' : ''}" data-sector="${sector}">${sector}</button>`;
  }).join('');
}

// ─── Render ───────────────────────────────────────────────────────────────────

export function renderFeedbackConfig(): string {
  const sectorSVG = buildRingSVG(120, sectorModeColorFn, 100, false);
  const radarSVG  = buildRingSVG(120, radarModeColorFn, 100, false);

  const tonalSVG = `<svg width="120" height="120" viewBox="0 0 120 120" xmlns="http://www.w3.org/2000/svg">
    <circle cx="60" cy="60" r="58" fill="#0a0a14"/>
    <circle cx="60" cy="60" r="58" fill="none" stroke="#22223a" stroke-width="1.5"/>
    <rect x="16" y="46" width="13" height="28" rx="6.5" fill="#764ba2" opacity="0.45"/>
    <rect x="35" y="32" width="13" height="56" rx="6.5" fill="#764ba2" opacity="0.7"/>
    <rect x="54" y="20" width="13" height="80" rx="6.5" fill="#9b59b6"/>
    <rect x="73" y="35" width="13" height="50" rx="6.5" fill="#764ba2" opacity="0.7"/>
    <rect x="92" y="48" width="13" height="24" rx="6.5" fill="#764ba2" opacity="0.45"/>
  </svg>`;

  const verbalSVG = `<svg width="120" height="120" viewBox="0 0 120 120" xmlns="http://www.w3.org/2000/svg">
    <circle cx="60" cy="60" r="58" fill="#0a0a14"/>
    <circle cx="60" cy="60" r="58" fill="none" stroke="#22223a" stroke-width="1.5"/>
    <rect x="46" y="16" width="28" height="46" rx="14" fill="#764ba2"/>
    <path d="M26 54 Q26 90 60 90 Q94 90 94 54" fill="none" stroke="#764ba2" stroke-width="4.5" stroke-linecap="round"/>
    <line x1="60" y1="90" x2="60" y2="104" stroke="#764ba2" stroke-width="4.5" stroke-linecap="round"/>
    <line x1="42" y1="104" x2="78" y2="104" stroke="#764ba2" stroke-width="4.5" stroke-linecap="round"/>
  </svg>`;

  const { thresholds, sectorCount, brightness } = state.advanced;
  const previewSVG = buildRingSVG(220, advancedColorFn, brightness, true, getSectorLabels(sectorCount));

  const sectorTogglesHTML = renderSectorToggles();

  return `
    <main class="config-content">

      <!-- Audio feedback mode -->
      <section class="config-section">
        <h2 class="section-title">Audio Feedback Mode</h2>
        <p class="section-subtitle">Select how audio communicates obstacle proximity.</p>

        <div class="mode-cards">

          <div class="mode-card${state.selectedAudioMode === 'tonal' ? ' selected' : ''}" data-audio-mode="tonal">
            <div class="mode-diagram">${tonalSVG}</div>
            <h3 class="mode-name">Tonal</h3>
            <p class="mode-desc">Proximity communicated through tones that change in pitch as obstacles get closer.</p>
          </div>

          <div class="mode-card${state.selectedAudioMode === 'verbal' ? ' selected' : ''}" data-audio-mode="verbal">
            <div class="mode-diagram">${verbalSVG}</div>
            <h3 class="mode-name">Verbal</h3>
            <p class="mode-desc">Spoken audio cues describe the direction and distance of detected obstacles.</p>
          </div>

        </div>
      </section>

      <!-- Divider -->
      <div class="section-divider"></div>

      <!-- Visual feedback mode -->
      <section class="config-section">
        <h2 class="section-title">Visual Feedback Mode</h2>
        <p class="section-subtitle">Select how the LED ring communicates obstacle proximity.</p>

        <div class="mode-cards">

          <div class="mode-card${state.selectedMode === 'sector' ? ' selected' : ''}" data-mode="sector">
            <div class="mode-diagram">${sectorSVG}</div>
            <h3 class="mode-name">Sector Mode</h3>
            <p class="mode-desc">Each directional zone lights yellow, orange, or red based on obstacle distance.</p>
          </div>

          <div class="mode-card${state.selectedMode === 'radar' ? ' selected' : ''}" data-mode="radar">
            <div class="mode-diagram">${radarSVG}</div>
            <h3 class="mode-name">Radar Mode</h3>
            <p class="mode-desc">Ring color shows obstacle distance with no directional zones — full 360° display. Yellow = far, orange = medium, red = close.</p>
          </div>

        </div>
      </section>

      <!-- Divider -->
      <div class="section-divider"></div>

      <!-- Advanced settings (collapsible) -->
      <section class="config-section">
        <div class="advanced-header" id="advanced-toggle">
          <div>
            <h2 class="section-title">Advanced Settings</h2>
            <p class="section-subtitle">Adjust distance thresholds, active sectors, and brightness.</p>
          </div>
          <span class="advanced-chevron" id="advanced-chevron">&#9660;</span>
        </div>

        <div class="advanced-body" id="advanced-body">

          <!-- Distance thresholds -->
          <div class="subsection">
            <h3 class="subsection-title">Distance Thresholds</h3>
            <div class="threshold-control">
              <div class="threshold-row">
                <div class="threshold-label">
                  <span class="color-dot dot-red"></span>
                  <span>Red up to <strong><span id="red-threshold-val">${thresholds.redMax}</span> cm</strong></span>
                </div>
                <input type="range" id="threshold-red" min="10" max="400" value="${thresholds.redMax}" />
              </div>
              <div class="threshold-row">
                <div class="threshold-label">
                  <span class="color-dot dot-orange"></span>
                  <span>Orange up to <strong><span id="yellow-threshold-val">${thresholds.yellowMax}</span> cm</strong></span>
                </div>
                <input type="range" id="threshold-yellow" min="20" max="800" value="${thresholds.yellowMax}" />
              </div>
              <p class="threshold-note">
                <span class="color-dot dot-yellow"></span>
                Yellow beyond <span id="green-starts-val">${thresholds.yellowMax}</span> cm
              </p>
            </div>
          </div>

          <!-- Sector count + toggles -->
          <div class="subsection">
            <h3 class="subsection-title">Active Sectors</h3>
            <div class="sector-count-selector">
              <button class="sector-count-btn${sectorCount === 4 ? ' active' : ''}" data-count="4">4 Sectors</button>
              <button class="sector-count-btn${sectorCount === 6 ? ' active' : ''}" data-count="6">6 Sectors</button>
              <button class="sector-count-btn${sectorCount === 8 ? ' active' : ''}" data-count="8">8 Sectors</button>
            </div>
            <div class="sector-toggles" id="sector-toggles-container">
              ${sectorTogglesHTML}
            </div>
          </div>

          <!-- Brightness -->
          <div class="subsection">
            <h3 class="subsection-title">Brightness — <span id="brightness-value">${brightness}%</span></h3>
            <input type="range" id="brightness-slider" min="0" max="100" value="${brightness}" />
          </div>

          <!-- Live preview -->
          <div class="subsection">
            <div class="preview-header">
              <h3 class="subsection-title">Live Preview</h3>
              <button class="display-btn" id="display-toggle">Display</button>
            </div>
            <div class="preview-container" id="preview-container">
              <div id="advanced-preview">${previewSVG}</div>
            </div>
          </div>

        </div>
      </section>
    </main>`;
}

// ─── Init ─────────────────────────────────────────────────────────────────────

function initSectorToggles(): void {
  document.querySelectorAll<HTMLButtonElement>('.sector-toggle').forEach(btn => {
    btn.addEventListener('click', () => {
      const sector = btn.dataset['sector'] as Sector;
      const current = state.advanced.activeSectors[sector] ?? false;
      state.advanced.activeSectors[sector] = !current;
      btn.classList.toggle('active', !current);
      refreshPreview();
      sendConfig();
      if (previewActive) sendPreview(true);
    });
  });
}

export function initFeedbackConfig(): void {
  state = defaultState();

  // Visual mode card selection
  document.querySelectorAll<HTMLElement>('.mode-card[data-mode]').forEach(card => {
    card.addEventListener('click', () => {
      const mode = card.dataset['mode'] as FeedbackMode;
      state.selectedMode = mode;
      document.querySelectorAll('.mode-card[data-mode]').forEach(c => c.classList.remove('selected'));
      card.classList.add('selected');
    });
  });

  // Audio mode card selection
  document.querySelectorAll<HTMLElement>('.mode-card[data-audio-mode]').forEach(card => {
    card.addEventListener('click', () => {
      const mode = card.dataset['audioMode'] as AudioFeedbackMode;
      state.selectedAudioMode = mode;
      document.querySelectorAll('.mode-card[data-audio-mode]').forEach(c => c.classList.remove('selected'));
      card.classList.add('selected');
    });
  });

  // Display button — toggle preview on the physical LED ring
  previewActive = false;
  // Sync settings to device on page load so obstacle detection uses current config
  sendConfig();
  const displayBtn = document.getElementById('display-toggle') as HTMLButtonElement | null;
  displayBtn?.addEventListener('click', () => {
    previewActive = !previewActive;
    displayBtn.classList.toggle('active', previewActive);
    if (previewActive) {
      sendPreview(true);
    } else {
      sendPreview(false);
      sendConfig(); // Re-apply settings for obstacle detection now that preview ended
    }
  });

  // Advanced settings toggle
  const advancedToggle  = document.getElementById('advanced-toggle');
  const advancedBody    = document.getElementById('advanced-body');
  const advancedChevron = document.getElementById('advanced-chevron');
  advancedToggle?.addEventListener('click', () => {
    const isOpen = advancedBody?.classList.toggle('open');
    advancedChevron?.classList.toggle('open', isOpen);
  });

  // Distance threshold sliders
  const redSlider      = document.getElementById('threshold-red')    as HTMLInputElement | null;
  const yellowSlider   = document.getElementById('threshold-yellow') as HTMLInputElement | null;
  const redVal         = document.getElementById('red-threshold-val');
  const yellowVal      = document.getElementById('yellow-threshold-val');
  const greenStartsVal = document.getElementById('green-starts-val');

  redSlider?.addEventListener('input', () => {
    let v = Number(redSlider.value);
    if (v >= state.advanced.thresholds.yellowMax) {
      v = state.advanced.thresholds.yellowMax - 10;
      redSlider.value = String(v);
    }
    state.advanced.thresholds.redMax = v;
    if (redVal) redVal.textContent = String(v);
    refreshPreview();
    sendConfig();
    if (previewActive) sendPreview(true);
  });

  yellowSlider?.addEventListener('input', () => {
    let v = Number(yellowSlider.value);
    if (v <= state.advanced.thresholds.redMax) {
      v = state.advanced.thresholds.redMax + 10;
      yellowSlider.value = String(v);
    }
    state.advanced.thresholds.yellowMax = v;
    if (yellowVal) yellowVal.textContent = String(v);
    if (greenStartsVal) greenStartsVal.textContent = String(v);
    refreshPreview();
    sendConfig();
    if (previewActive) sendPreview(true);
  });

  // Sector count selector
  document.querySelectorAll<HTMLButtonElement>('.sector-count-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      const count = Number(btn.dataset['count']) as SectorCount;
      if (count === state.advanced.sectorCount) return;

      const oldSectors = state.advanced.activeSectors;
      const newSectors: Partial<Record<Sector, boolean>> = {};
      for (const s of getSectorLabels(count)) {
        newSectors[s] = oldSectors[s] ?? true;
      }
      state.advanced.sectorCount = count;
      state.advanced.activeSectors = newSectors;

      document.querySelectorAll('.sector-count-btn').forEach(b => b.classList.remove('active'));
      btn.classList.add('active');

      const container = document.getElementById('sector-toggles-container');
      if (container) container.innerHTML = renderSectorToggles();
      initSectorToggles();

      refreshPreview();
      sendConfig();
      if (previewActive) sendPreview(true);
    });
  });

  initSectorToggles();

  // Brightness slider
  const brightnessSlider = document.getElementById('brightness-slider') as HTMLInputElement | null;
  const brightnessValue  = document.getElementById('brightness-value');
  brightnessSlider?.addEventListener('input', () => {
    state.advanced.brightness = Number(brightnessSlider.value);
    if (brightnessValue) brightnessValue.textContent = `${state.advanced.brightness}%`;
    refreshPreview();
    sendConfig();
    if (previewActive) sendPreview(true);
  });
}
