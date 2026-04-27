const LOGO_SVG = `<svg viewBox="0 0 100 100">
  <circle cx="35" cy="35" r="12" fill="white"/>
  <path d="M 35 50 Q 25 50 20 58 Q 15 66 15 75 L 15 85 Q 15 90 20 90 L 50 90 Q 55 90 55 85 L 55 75 Q 55 66 50 58 Q 45 50 35 50 Z" fill="white"/>
  <path d="M 60 25 Q 62 27 64 32" stroke="white" stroke-width="4" fill="none" stroke-linecap="round"/>
  <path d="M 67 20 Q 71 24 75 35" stroke="white" stroke-width="4" fill="none" stroke-linecap="round"/>
  <path d="M 75 15 Q 81 21 87 38" stroke="white" stroke-width="4" fill="none" stroke-linecap="round"/>
</svg>`;

export function renderHeader(currentRoute: string): string {
  const isConfig = currentRoute === '#feedback-config';
  return `
    <div class="header-wrap">
      <header class="header">
        <div class="header-left">
          <button class="menu-btn" id="menu-btn" aria-label="Open navigation" aria-expanded="false">
            <span></span><span></span><span></span>
          </button>
          <nav class="nav-dropdown" id="nav-dropdown" aria-hidden="true">
            <a href="#controller" class="nav-item${!isConfig ? ' active' : ''}">Controller</a>
            <a href="#feedback-config" class="nav-item${isConfig ? ' active' : ''}">Feedback Config</a>
          </nav>
          <div class="logo-container">
            <div class="logo-circle">${LOGO_SVG}</div>
            <span class="app-name">ViviSense</span>
          </div>
        </div>
        <div class="status-indicators">
          <div class="ws-status">
            <div class="ws-dot" id="ws-dot"></div>
            <span>Live</span>
          </div>
        </div>
      </header>

      <div class="connections-bar">
        <button class="connections-toggle" id="connections-toggle" aria-expanded="false">
          <span class="connections-label">Connections</span>
          <span class="connections-chevron" id="connections-chevron">&#9660;</span>
        </button>
        <div class="connections-panel" id="connections-panel" aria-hidden="true">
          <div class="conn-section-label">Controllers</div>
          <div class="connections-row">
            <div class="component-status-chip state-unknown" id="comp-main">
              <span class="component-dot"></span>
              <span class="component-name">Main</span>
              <span class="component-value">--</span>
            </div>
            <div class="component-status-chip state-unknown" id="comp-led">
              <span class="component-dot"></span>
              <span class="component-name">LED</span>
              <span class="component-value">--</span>
            </div>
          </div>
          <div class="conn-section-label">Sensor Pods</div>
          <div class="connections-row">
            <div class="component-status-chip state-unknown" id="comp-left-pod">
              <span class="component-dot"></span>
              <span class="component-name">Left Pod</span>
              <span class="component-value">--</span>
            </div>
            <div class="component-status-chip state-unknown" id="comp-right-pod">
              <span class="component-dot"></span>
              <span class="component-name">Right Pod</span>
              <span class="component-value">--</span>
            </div>
            <div class="component-status-chip state-unknown" id="comp-tower-pod">
              <span class="component-dot"></span>
              <span class="component-name">Tower Pod</span>
              <span class="component-value">--</span>
            </div>
          </div>
        </div>
      </div>
    </div>`;
}

export function initHeader(): void {
  const btn = document.getElementById('menu-btn') as HTMLButtonElement | null;
  const nav = document.getElementById('nav-dropdown') as HTMLElement | null;
  if (!btn || !nav) return;

  function openNav(): void {
    nav!.classList.add('open');
    btn!.setAttribute('aria-expanded', 'true');
    nav!.setAttribute('aria-hidden', 'false');
  }

  function closeNav(): void {
    nav!.classList.remove('open');
    btn!.setAttribute('aria-expanded', 'false');
    nav!.setAttribute('aria-hidden', 'true');
  }

  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    nav.classList.contains('open') ? closeNav() : openNav();
  });

  document.addEventListener('click', (e) => {
    if (!nav.contains(e.target as Node) && e.target !== btn) {
      closeNav();
    }
  });

  nav.querySelectorAll('.nav-item').forEach(link => {
    link.addEventListener('click', closeNav);
  });

  const connToggle  = document.getElementById('connections-toggle') as HTMLButtonElement | null;
  const connPanel   = document.getElementById('connections-panel') as HTMLElement | null;
  const connChevron = document.getElementById('connections-chevron') as HTMLElement | null;
  connToggle?.addEventListener('click', () => {
    const isOpen = connPanel?.classList.toggle('open');
    connToggle.setAttribute('aria-expanded', isOpen ? 'true' : 'false');
    connPanel?.setAttribute('aria-hidden', isOpen ? 'false' : 'true');
    if (connChevron) connChevron.style.transform = isOpen ? 'rotate(180deg)' : '';
  });
}
