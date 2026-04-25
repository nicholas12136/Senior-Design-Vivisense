import {
  buildRingSVG,
  sectorModeColorFn,
  radarModeColorFn,
  SECTOR_LABELS_4,
  SECTOR_LABELS_6,
  SECTOR_LABELS_8,
  RED,
  ORANGE,
  YELLOW,
  GREEN,
  BLUE,
  LED_OFF,
  type Sector,
  type LEDPosition,
} from '../utils/ledRing.js';
import { sendMessage, onMessage, offMessage } from '../utils/websocket.js';

type FeedbackMode = 'sector' | 'radar';
type AudioFeedbackMode = 'tonal' | 'verbal';
type SectorCount = 4 | 6 | 8;

interface ToneSettings {
  redPitchHz: number;
  redTempoMs: number;
  orangePitchHz: number;
  orangeTempoMs: number;
  yellowPitchHz: number;
  yellowTempoMs: number;
}

interface AdvancedSettings {
  thresholds: { redMax: number; orangeMax: number; yellowMax: number }; // cm
  sectorCount: SectorCount;
  activeSectors: Partial<Record<Sector, boolean>>;
  brightness: number;
  tones: ToneSettings;
}

interface ConfigState {
  audioEnabled: boolean;
  visualEnabled: boolean;
  selectedMode: FeedbackMode;
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
    audioEnabled: true,
    visualEnabled: true,
    selectedMode: 'sector',
    selectedAudioMode: null,
    advanced: {
      thresholds: { redMax: 60, orangeMax: 105, yellowMax: 150 },
      sectorCount: 6,
      activeSectors: buildDefaultSectors(6),
      brightness: 20,
      tones: {
        redPitchHz: 1200, redTempoMs: 150,
        orangePitchHz: 800, orangeTempoMs: 500,
        yellowPitchHz: 400, yellowTempoMs: 1000,
      },
    },
  };
}

let state: ConfigState = defaultState();
let previewActive = false;
let configStatusHandler: ((data: unknown) => void) | null = null;

// Approximate ring distances in cm for preview ring coloring (outer -> inner).
const RING_DISTANCES_CM = [280, 220, 160, 110, 60, 0];

function getColorForDistance(distanceCm: number): string {
  if (distanceCm <= state.advanced.thresholds.redMax) return RED;
  if (distanceCm <= state.advanced.thresholds.orangeMax) return ORANGE;
  if (distanceCm <= state.advanced.thresholds.yellowMax) return YELLOW;
  return GREEN;
}

function advancedColorFn(led: LEDPosition): string {
  if (led.ring === 5) return BLUE;
  if (!state.advanced.activeSectors[led.sector]) return LED_OFF;
  return getColorForDistance(RING_DISTANCES_CM[led.ring] ?? 9999);
}

function refreshPreview(): void {
  const el = document.getElementById('advanced-preview');
  if (!el) return;
  const labels = getSectorLabels(state.advanced.sectorCount);
  el.innerHTML = buildRingSVG(220, advancedColorFn, state.advanced.brightness, true, labels);
}

function playTestChirp(pitchHz: number): void {
  const ctx = new AudioContext();
  const osc = ctx.createOscillator();
  const gain = ctx.createGain();
  osc.connect(gain);
  gain.connect(ctx.destination);
  osc.type = 'sine';
  osc.frequency.value = pitchHz;
  gain.gain.setValueAtTime(0.5, ctx.currentTime);
  osc.start();
  osc.stop(ctx.currentTime + 0.08);
  osc.onended = () => ctx.close();
}

function updateTonalSettingsVisibility(): void {
  const el = document.getElementById('tonal-settings');
  if (!el) return;
  const show = state.audioEnabled && state.selectedAudioMode === 'tonal';
  el.classList.toggle('section-hidden', !show);
}

function sendConfig(): void {
  const { thresholds, sectorCount, brightness, tones } = state.advanced;
  const labels = getSectorLabels(sectorCount);
  const activeSectorsArray = labels.map(s => state.advanced.activeSectors[s] ?? true);
  sendMessage({
    type: 'config',
    zoneMode: sectorCount,
    brightness,
    redThreshold: thresholds.redMax,
    orangeThreshold: thresholds.orangeMax,
    yellowThreshold: thresholds.yellowMax,
    activeSectors: activeSectorsArray,
    renderMode: state.selectedMode === 'radar' ? 1 : 0,
    audioEnabled: state.audioEnabled && state.selectedAudioMode === 'tonal',
    visualEnabled: state.visualEnabled,
    toneRedPitchHz: tones.redPitchHz,
    toneRedTempoMs: tones.redTempoMs,
    toneOrangePitchHz: tones.orangePitchHz,
    toneOrangeTempoMs: tones.orangeTempoMs,
    toneYellowPitchHz: tones.yellowPitchHz,
    toneYellowTempoMs: tones.yellowTempoMs,
  });
}

function sendPreview(active: boolean): void {
  if (!active) {
    sendMessage({ type: 'preview', active: false });
    return;
  }
  const { thresholds, sectorCount, brightness } = state.advanced;
  const labels = getSectorLabels(sectorCount);
  const activeSectorsArray = labels.map(s => state.advanced.activeSectors[s] ?? true);
  sendMessage({
    type: 'preview',
    active: true,
    zoneMode: sectorCount,
    brightness,
    redThreshold: thresholds.redMax,
    orangeThreshold: thresholds.orangeMax,
    yellowThreshold: thresholds.yellowMax,
    activeSectors: activeSectorsArray,
  });
}

function renderSectorToggles(): string {
  return getSectorLabels(state.advanced.sectorCount).map(sector => {
    const isActive = state.advanced.activeSectors[sector] ?? false;
    return `<button class="sector-toggle${isActive ? ' active' : ''}" data-sector="${sector}">${sector}</button>`;
  }).join('');
}

function syncSelectedModeCards(): void {
  document.querySelectorAll<HTMLElement>('.mode-card[data-mode]').forEach(card => {
    card.classList.toggle('selected', card.dataset['mode'] === state.selectedMode);
  });
}

function applyStatus(msg: Record<string, unknown>): void {
  const zoneMode = msg['zoneMode'] as number | undefined;
  const brightness = msg['brightness'] as number | undefined;
  const redThreshold = msg['redThreshold'] as number | undefined;
  const orangeThreshold = msg['orangeThreshold'] as number | undefined;
  const yellowThreshold = msg['yellowThreshold'] as number | undefined;
  const renderMode = msg['renderMode'] as number | undefined;
  const activeSectors = msg['activeSectors'] as boolean[] | undefined;
  const audioEn = msg['audioEnabled'] as boolean | undefined;
  const visualEn = msg['visualEnabled'] as boolean | undefined;

  if (zoneMode !== undefined && ([4, 6, 8] as number[]).includes(zoneMode)) {
    state.advanced.sectorCount = zoneMode as SectorCount;
  }
  if (brightness !== undefined) state.advanced.brightness = brightness;
  if (redThreshold !== undefined) state.advanced.thresholds.redMax = redThreshold;
  if (orangeThreshold !== undefined) state.advanced.thresholds.orangeMax = orangeThreshold;
  if (yellowThreshold !== undefined) state.advanced.thresholds.yellowMax = yellowThreshold;
  if (renderMode !== undefined && (renderMode === 0 || renderMode === 1)) {
    state.selectedMode = renderMode === 1 ? 'radar' : 'sector';
  }
  if (activeSectors !== undefined) {
    getSectorLabels(state.advanced.sectorCount).forEach((s, i) => {
      state.advanced.activeSectors[s] = activeSectors[i] ?? true;
    });
  }
  if (audioEn !== undefined) state.audioEnabled = audioEn;
  if (visualEn !== undefined) state.visualEnabled = visualEn;

  const activeEl = document.activeElement;

  const toneFields: Array<[keyof ToneSettings, string, string]> = [
    ['redPitchHz',    'tone-red-pitch',    'tone-red-pitch-val'],
    ['redTempoMs',    'tone-red-tempo',    'tone-red-tempo-val'],
    ['orangePitchHz', 'tone-orange-pitch', 'tone-orange-pitch-val'],
    ['orangeTempoMs', 'tone-orange-tempo', 'tone-orange-tempo-val'],
    ['yellowPitchHz', 'tone-yellow-pitch', 'tone-yellow-pitch-val'],
    ['yellowTempoMs', 'tone-yellow-tempo', 'tone-yellow-tempo-val'],
  ];
  for (const [key, sliderId, valId] of toneFields) {
    const raw = msg[`tone${key.charAt(0).toUpperCase()}${key.slice(1)}`] as number | undefined;
    if (raw === undefined) continue;
    state.advanced.tones[key] = raw;
    const slider = document.getElementById(sliderId) as HTMLInputElement | null;
    const valEl  = document.getElementById(valId);
    if (slider && activeEl !== slider) slider.value = String(key.endsWith('Ms') ? 2050 - raw : raw);
    if (valEl) valEl.textContent = key.endsWith('Hz') ? `${raw} Hz` : `${raw} ms`;
  }

  const brightSlider = document.getElementById('brightness-slider') as HTMLInputElement | null;
  const brightVal = document.getElementById('brightness-value');
  if (brightSlider && brightness !== undefined && activeEl !== brightSlider) {
    brightSlider.value = String(brightness);
  }
  if (brightVal && brightness !== undefined) brightVal.textContent = `${brightness}%`;

  const redSlider = document.getElementById('threshold-red') as HTMLInputElement | null;
  const redValEl = document.getElementById('red-threshold-val');
  const orangeSlider = document.getElementById('threshold-orange') as HTMLInputElement | null;
  const orangeValEl = document.getElementById('orange-threshold-val');
  const yellowSlider = document.getElementById('threshold-yellow') as HTMLInputElement | null;
  const yellowValEl = document.getElementById('yellow-threshold-val');
  const greenStartsEl = document.getElementById('green-starts-val');
  if (redSlider && redThreshold !== undefined && activeEl !== redSlider) {
    redSlider.value = String(redThreshold);
  }
  if (redValEl && redThreshold !== undefined) redValEl.textContent = String(redThreshold);
  if (orangeSlider && orangeThreshold !== undefined && activeEl !== orangeSlider) {
    orangeSlider.value = String(orangeThreshold);
  }
  if (orangeValEl && orangeThreshold !== undefined) orangeValEl.textContent = String(orangeThreshold);
  if (yellowSlider && yellowThreshold !== undefined && activeEl !== yellowSlider) {
    yellowSlider.value = String(yellowThreshold);
  }
  if (yellowValEl && yellowThreshold !== undefined) yellowValEl.textContent = String(yellowThreshold);
  if (greenStartsEl && yellowThreshold !== undefined) greenStartsEl.textContent = String(yellowThreshold);

  syncSelectedModeCards();
  updateTonalSettingsVisibility();

  if (zoneMode !== undefined || activeSectors !== undefined) {
    document.querySelectorAll<HTMLButtonElement>('.sector-count-btn[data-count]').forEach(btn => {
      btn.classList.toggle('active', Number(btn.dataset['count']) === state.advanced.sectorCount);
    });
    const container = document.getElementById('sector-toggles-container');
    if (container) {
      container.innerHTML = renderSectorToggles();
      initSectorToggles();
    }
  }

  const audioEnableBtn = document.getElementById('audio-enable-btn') as HTMLButtonElement | null;
  const audioModeCards = document.getElementById('audio-mode-cards');
  if (audioEnableBtn) {
    audioEnableBtn.classList.toggle('active', state.audioEnabled);
    audioEnableBtn.textContent = state.audioEnabled ? 'Enabled' : 'Disabled';
  }
  audioModeCards?.classList.toggle('section-disabled', !state.audioEnabled);

  const visualEnableBtn = document.getElementById('visual-enable-btn') as HTMLButtonElement | null;
  const visualModeCards = document.getElementById('visual-mode-cards');
  if (visualEnableBtn) {
    visualEnableBtn.classList.toggle('active', state.visualEnabled);
    visualEnableBtn.textContent = state.visualEnabled ? 'Enabled' : 'Disabled';
  }
  visualModeCards?.classList.toggle('section-disabled', !state.visualEnabled);

  refreshPreview();
}

export function renderFeedbackConfig(): string {
  const sectorSVG = buildRingSVG(120, sectorModeColorFn, 100, false);
  const radarSVG = buildRingSVG(120, radarModeColorFn, 100, false);

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

  const { thresholds, sectorCount, brightness, tones } = state.advanced;
  const previewSVG = buildRingSVG(220, advancedColorFn, brightness, true, getSectorLabels(sectorCount));
  const sectorTogglesHTML = renderSectorToggles();

  return `
    <main class="config-content">

      <section class="config-section">
        <div class="section-header">
          <div>
            <h2 class="section-title">Obstacle Audio Feedback Mode</h2>
            <p class="section-subtitle">Controls tonal/verbal obstacle cues only. Controller command prompts always play.</p>
          </div>
          <button class="enable-btn${state.audioEnabled ? ' active' : ''}" id="audio-enable-btn">
            ${state.audioEnabled ? 'Enabled' : 'Disabled'}
          </button>
        </div>

        <div class="mode-cards${!state.audioEnabled ? ' section-disabled' : ''}" id="audio-mode-cards">
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

      <div class="section-divider"></div>

      <section class="config-section">
        <div class="section-header">
          <div>
            <h2 class="section-title">Visual Feedback Mode</h2>
            <p class="section-subtitle">Select how the LED ring communicates obstacle proximity.</p>
          </div>
          <button class="enable-btn${state.visualEnabled ? ' active' : ''}" id="visual-enable-btn">
            ${state.visualEnabled ? 'Enabled' : 'Disabled'}
          </button>
        </div>

        <div class="mode-cards${!state.visualEnabled ? ' section-disabled' : ''}" id="visual-mode-cards">
          <div class="mode-card${state.selectedMode === 'sector' ? ' selected' : ''}" data-mode="sector">
            <div class="mode-diagram">${sectorSVG}</div>
            <h3 class="mode-name">Sector Mode</h3>
            <p class="mode-desc">Each zone shows red/orange/yellow by nearest obstacle distance, or green when clear.</p>
          </div>

          <div class="mode-card${state.selectedMode === 'radar' ? ' selected' : ''}" data-mode="radar">
            <div class="mode-diagram">${radarSVG}</div>
            <h3 class="mode-name">Radar Mode</h3>
            <p class="mode-desc">Occupied positions are rendered by angle and radius directly from the polar occupancy grid.</p>
          </div>
        </div>
      </section>

      <div class="section-divider"></div>

      <section class="config-section">
        <div class="advanced-header" id="advanced-toggle">
          <div>
            <h2 class="section-title">Advanced Settings</h2>
            <p class="section-subtitle">Adjust thresholds, active sectors, and brightness.</p>
          </div>
          <span class="advanced-chevron" id="advanced-chevron">&#9660;</span>
        </div>

        <div class="advanced-body" id="advanced-body">

          <div class="subsection">
            <h3 class="subsection-title">Active Sectors</h3>
            <div class="sector-count-selector">
              <button class="sector-count-btn${sectorCount === 4 ? ' active' : ''}" data-count="4">4 Sectors</button>
              <button class="sector-count-btn${sectorCount === 6 ? ' active' : ''}" data-count="6">6 Sectors</button>
              <button class="sector-count-btn${sectorCount === 8 ? ' active' : ''}" data-count="8">8 Sectors</button>
            </div>
            <div class="sector-toggles" id="sector-toggles-container">${sectorTogglesHTML}</div>
          </div>

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
                  <span>Orange up to <strong><span id="orange-threshold-val">${thresholds.orangeMax}</span> cm</strong></span>
                </div>
                <input type="range" id="threshold-orange" min="20" max="600" value="${thresholds.orangeMax}" />
              </div>
              <div class="threshold-row">
                <div class="threshold-label">
                  <span class="color-dot dot-yellow"></span>
                  <span>Yellow up to <strong><span id="yellow-threshold-val">${thresholds.yellowMax}</span> cm</strong></span>
                </div>
                <input type="range" id="threshold-yellow" min="30" max="800" value="${thresholds.yellowMax}" />
              </div>
              <p class="threshold-note">
                <span class="color-dot dot-green"></span>
                Green beyond <span id="green-starts-val">${thresholds.yellowMax}</span> cm
              </p>
            </div>
          </div>

          <div class="subsection${state.audioEnabled && state.selectedAudioMode === 'tonal' ? '' : ' section-hidden'}" id="tonal-settings">
            <h3 class="subsection-title">Tonal Settings</h3>

            <div class="tone-zone">
              <div class="tone-zone-header">
                <span class="color-dot dot-red"></span><span>Red Zone</span>
              </div>
              <div class="tone-control-row">
                <span class="tone-axis-label">Pitch</span>
                <span class="tone-end-label">Low</span>
                <input type="range" id="tone-red-pitch" min="200" max="4000" value="${tones.redPitchHz}" />
                <span class="tone-end-label">High</span>
                <span class="tone-val" id="tone-red-pitch-val">${tones.redPitchHz} Hz</span>
              </div>
              <div class="tone-control-row">
                <span class="tone-axis-label">Tempo</span>
                <span class="tone-end-label">Slow</span>
                <input type="range" id="tone-red-tempo" min="50" max="2000" value="${2050 - tones.redTempoMs}" />
                <span class="tone-end-label">Rapid</span>
                <span class="tone-val" id="tone-red-tempo-val">${tones.redTempoMs} ms</span>
                <button class="tone-test-btn" data-zone="red">&#9654; Test</button>
              </div>
            </div>

            <div class="tone-zone">
              <div class="tone-zone-header">
                <span class="color-dot dot-orange"></span><span>Orange Zone</span>
              </div>
              <div class="tone-control-row">
                <span class="tone-axis-label">Pitch</span>
                <span class="tone-end-label">Low</span>
                <input type="range" id="tone-orange-pitch" min="200" max="4000" value="${tones.orangePitchHz}" />
                <span class="tone-end-label">High</span>
                <span class="tone-val" id="tone-orange-pitch-val">${tones.orangePitchHz} Hz</span>
              </div>
              <div class="tone-control-row">
                <span class="tone-axis-label">Tempo</span>
                <span class="tone-end-label">Slow</span>
                <input type="range" id="tone-orange-tempo" min="50" max="2000" value="${2050 - tones.orangeTempoMs}" />
                <span class="tone-end-label">Rapid</span>
                <span class="tone-val" id="tone-orange-tempo-val">${tones.orangeTempoMs} ms</span>
                <button class="tone-test-btn" data-zone="orange">&#9654; Test</button>
              </div>
            </div>

            <div class="tone-zone">
              <div class="tone-zone-header">
                <span class="color-dot dot-yellow"></span><span>Yellow Zone</span>
              </div>
              <div class="tone-control-row">
                <span class="tone-axis-label">Pitch</span>
                <span class="tone-end-label">Low</span>
                <input type="range" id="tone-yellow-pitch" min="200" max="4000" value="${tones.yellowPitchHz}" />
                <span class="tone-end-label">High</span>
                <span class="tone-val" id="tone-yellow-pitch-val">${tones.yellowPitchHz} Hz</span>
              </div>
              <div class="tone-control-row">
                <span class="tone-axis-label">Tempo</span>
                <span class="tone-end-label">Slow</span>
                <input type="range" id="tone-yellow-tempo" min="50" max="2000" value="${2050 - tones.yellowTempoMs}" />
                <span class="tone-end-label">Rapid</span>
                <span class="tone-val" id="tone-yellow-tempo-val">${tones.yellowTempoMs} ms</span>
                <button class="tone-test-btn" data-zone="yellow">&#9654; Test</button>
              </div>
            </div>
          </div>

          <div class="subsection">
            <h3 class="subsection-title">Brightness - <span id="brightness-value">${brightness}%</span></h3>
            <input type="range" id="brightness-slider" min="0" max="100" value="${brightness}" />
          </div>

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
  syncSelectedModeCards();

  const audioEnableBtn = document.getElementById('audio-enable-btn') as HTMLButtonElement | null;
  const audioModeCards = document.getElementById('audio-mode-cards');
  audioEnableBtn?.addEventListener('click', () => {
    state.audioEnabled = !state.audioEnabled;
    audioEnableBtn.classList.toggle('active', state.audioEnabled);
    audioEnableBtn.textContent = state.audioEnabled ? 'Enabled' : 'Disabled';
    audioModeCards?.classList.toggle('section-disabled', !state.audioEnabled);
    updateTonalSettingsVisibility();
    sendConfig();
  });

  const visualEnableBtn = document.getElementById('visual-enable-btn') as HTMLButtonElement | null;
  const visualModeCards = document.getElementById('visual-mode-cards');
  visualEnableBtn?.addEventListener('click', () => {
    state.visualEnabled = !state.visualEnabled;
    visualEnableBtn.classList.toggle('active', state.visualEnabled);
    visualEnableBtn.textContent = state.visualEnabled ? 'Enabled' : 'Disabled';
    visualModeCards?.classList.toggle('section-disabled', !state.visualEnabled);
    sendConfig();
  });

  document.querySelectorAll<HTMLElement>('.mode-card[data-mode]').forEach(card => {
    card.addEventListener('click', () => {
      const mode = card.dataset['mode'] as FeedbackMode;
      if (mode === state.selectedMode) return;
      state.selectedMode = mode;
      document.querySelectorAll('.mode-card[data-mode]').forEach(c => c.classList.remove('selected'));
      card.classList.add('selected');
      sendConfig();
    });
  });

  document.querySelectorAll<HTMLElement>('.mode-card[data-audio-mode]').forEach(card => {
    card.addEventListener('click', () => {
      const mode = card.dataset['audioMode'] as AudioFeedbackMode;
      state.selectedAudioMode = mode;
      document.querySelectorAll('.mode-card[data-audio-mode]').forEach(c => c.classList.remove('selected'));
      card.classList.add('selected');
      updateTonalSettingsVisibility();
    });
  });

  // Tone pitch/tempo sliders
  type ToneSliderDef = { sliderId: string; valId: string; key: keyof ToneSettings; unit: string };
  const toneSliders: ToneSliderDef[] = [
    { sliderId: 'tone-red-pitch',    valId: 'tone-red-pitch-val',    key: 'redPitchHz',    unit: 'Hz' },
    { sliderId: 'tone-red-tempo',    valId: 'tone-red-tempo-val',    key: 'redTempoMs',    unit: 'ms' },
    { sliderId: 'tone-orange-pitch', valId: 'tone-orange-pitch-val', key: 'orangePitchHz', unit: 'Hz' },
    { sliderId: 'tone-orange-tempo', valId: 'tone-orange-tempo-val', key: 'orangeTempoMs', unit: 'ms' },
    { sliderId: 'tone-yellow-pitch', valId: 'tone-yellow-pitch-val', key: 'yellowPitchHz', unit: 'Hz' },
    { sliderId: 'tone-yellow-tempo', valId: 'tone-yellow-tempo-val', key: 'yellowTempoMs', unit: 'ms' },
  ];
  for (const { sliderId, valId, key, unit } of toneSliders) {
    const slider = document.getElementById(sliderId) as HTMLInputElement | null;
    const valEl  = document.getElementById(valId);
    slider?.addEventListener('input', () => {
      const v = unit === 'ms' ? 2050 - Number(slider.value) : Number(slider.value);
      state.advanced.tones[key] = v;
      if (valEl) valEl.textContent = `${v} ${unit}`;
      sendConfig();
    });
  }

  // Tone test buttons — play chirp in browser via Web Audio API
  document.querySelectorAll<HTMLButtonElement>('.tone-test-btn').forEach(btn => {
    btn.addEventListener('click', () => {
      const zone = btn.dataset['zone'] as 'red' | 'orange' | 'yellow';
      const pitchMap = { red: 'redPitchHz', orange: 'orangePitchHz', yellow: 'yellowPitchHz' } as const;
      playTestChirp(state.advanced.tones[pitchMap[zone]]);
    });
  });

  previewActive = false;

  if (configStatusHandler) offMessage(configStatusHandler);
  configStatusHandler = (raw: unknown) => {
    const msg = raw as Record<string, unknown>;
    if (msg['type'] !== 'status') return;
    applyStatus(msg);
  };
  onMessage(configStatusHandler);
  sendMessage({ type: 'getConfig' });

  const displayBtn = document.getElementById('display-toggle') as HTMLButtonElement | null;
  displayBtn?.addEventListener('click', () => {
    previewActive = !previewActive;
    displayBtn.classList.toggle('active', previewActive);
    if (previewActive) {
      sendPreview(true);
    } else {
      sendPreview(false);
      sendConfig();
    }
  });

  const advancedToggle = document.getElementById('advanced-toggle');
  const advancedBody = document.getElementById('advanced-body');
  const advancedChevron = document.getElementById('advanced-chevron');
  advancedToggle?.addEventListener('click', () => {
    const isOpen = advancedBody?.classList.toggle('open');
    advancedChevron?.classList.toggle('open', isOpen);
  });

  const redSlider = document.getElementById('threshold-red') as HTMLInputElement | null;
  const orangeSlider = document.getElementById('threshold-orange') as HTMLInputElement | null;
  const yellowSlider = document.getElementById('threshold-yellow') as HTMLInputElement | null;
  const redVal = document.getElementById('red-threshold-val');
  const orangeVal = document.getElementById('orange-threshold-val');
  const yellowVal = document.getElementById('yellow-threshold-val');
  const greenStartsVal = document.getElementById('green-starts-val');

  redSlider?.addEventListener('input', () => {
    let v = Number(redSlider.value);
    if (v >= state.advanced.thresholds.orangeMax) {
      v = state.advanced.thresholds.orangeMax - 10;
      redSlider.value = String(v);
    }
    state.advanced.thresholds.redMax = v;
    if (redVal) redVal.textContent = String(v);
    refreshPreview();
    sendConfig();
    if (previewActive) sendPreview(true);
  });

  orangeSlider?.addEventListener('input', () => {
    let v = Number(orangeSlider.value);
    if (v <= state.advanced.thresholds.redMax) {
      v = state.advanced.thresholds.redMax + 10;
    }
    if (v >= state.advanced.thresholds.yellowMax) {
      v = state.advanced.thresholds.yellowMax - 10;
    }
    orangeSlider.value = String(v);
    state.advanced.thresholds.orangeMax = v;
    if (orangeVal) orangeVal.textContent = String(v);
    refreshPreview();
    sendConfig();
    if (previewActive) sendPreview(true);
  });

  yellowSlider?.addEventListener('input', () => {
    let v = Number(yellowSlider.value);
    if (v <= state.advanced.thresholds.orangeMax) {
      v = state.advanced.thresholds.orangeMax + 10;
      yellowSlider.value = String(v);
    }
    state.advanced.thresholds.yellowMax = v;
    if (yellowVal) yellowVal.textContent = String(v);
    if (greenStartsVal) greenStartsVal.textContent = String(v);
    refreshPreview();
    sendConfig();
    if (previewActive) sendPreview(true);
  });

  document.querySelectorAll<HTMLButtonElement>('.sector-count-btn[data-count]').forEach(btn => {
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

      document.querySelectorAll('.sector-count-btn[data-count]').forEach(b => b.classList.remove('active'));
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

  const brightnessSlider = document.getElementById('brightness-slider') as HTMLInputElement | null;
  const brightnessValue = document.getElementById('brightness-value');
  brightnessSlider?.addEventListener('input', () => {
    state.advanced.brightness = Number(brightnessSlider.value);
    if (brightnessValue) brightnessValue.textContent = `${state.advanced.brightness}%`;
    refreshPreview();
    sendConfig();
    if (previewActive) sendPreview(true);
  });
}
