import { sendMessage } from '../utils/websocket.js';

const MIC_SVG = `<svg viewBox="0 0 24 24">
  <path d="M12 14c1.66 0 3-1.34 3-3V5c0-1.66-1.34-3-3-3S9 3.34 9 5v6c0 1.66 1.34 3 3 3z"/>
  <path d="M17 11c0 2.76-2.24 5-5 5s-5-2.24-5-5H5c0 3.53 2.61 6.43 6 6.92V21h2v-3.08c3.39-.49 6-3.39 6-6.92h-2z"/>
</svg>`;

export function renderController(): string {
  return `
    <main class="main-content">

      <div class="control-panel">

        <!-- Speed buttons -->
        <div class="speed-controls">
          <div class="speed-btn" data-action="speedup">
            <div class="arrow-up"></div>
            <div class="speed-label">Speed<br>Up</div>
          </div>
          <div class="speed-btn" data-action="slowdown">
            <div class="arrow-down"></div>
            <div class="speed-label">Slow<br>Down</div>
          </div>
        </div>

        <!-- D-pad -->
        <div class="d-pad-container">
          <div class="d-pad">
            <div class="d-pad-ring">
              <div class="d-btn d-forward" data-action="forward">Go Forward</div>
              <div class="d-btn d-right"   data-action="right">Turn<br>Right</div>
              <div class="d-btn d-back"    data-action="backward">Back Up</div>
              <div class="d-btn d-left"    data-action="left">Turn<br>Left</div>
              <div class="d-center"        data-action="stop">Stop!</div>
            </div>
          </div>
        </div>

        <!-- Mic button -->
        <div class="mic-container">
          <div class="mic-btn" data-action="speak">${MIC_SVG}</div>
          <div class="mic-label">Tap to<br>Speak</div>
        </div>

      </div>

      <!-- Volume -->
      <div class="volume-container">
        <label class="section-label" for="volume-slider">Volume Control</label>
        <input type="range" id="volume-slider" min="0" max="255" value="180" />
      </div>

      <!-- Custom buttons -->
      <div class="custom-grid">
        <div class="custom-btn filled">Stay on<br>the line</div>
        <div class="custom-btn empty"></div>
        <div class="custom-btn empty"></div>
        <div class="custom-btn empty"></div>
        <div class="custom-btn empty"></div>
        <div class="custom-btn empty"></div>
        <div class="custom-btn empty"></div>
        <div class="custom-btn empty"></div>
      </div>

    </main>`;
}

export function initController(): void {
  // Navigate / speed / mic buttons — send action via WebSocket
  document.querySelectorAll<HTMLElement>('[data-action]').forEach(el => {
    el.addEventListener('click', () => {
      const action = el.dataset['action'];
      if (action) sendMessage({ type: 'navigate', action });
    });
  });

  // Volume slider — send volume level via WebSocket
  const volumeSlider = document.getElementById('volume-slider') as HTMLInputElement | null;
  volumeSlider?.addEventListener('input', () => {
    sendMessage({ type: 'volume', level: Number(volumeSlider.value) });
  });
}
