(function(){let e=document.createElement(`link`).relList;if(e&&e.supports&&e.supports(`modulepreload`))return;for(let e of document.querySelectorAll(`link[rel="modulepreload"]`))n(e);new MutationObserver(e=>{for(let t of e)if(t.type===`childList`)for(let e of t.addedNodes)e.tagName===`LINK`&&e.rel===`modulepreload`&&n(e)}).observe(document,{childList:!0,subtree:!0});function t(e){let t={};return e.integrity&&(t.integrity=e.integrity),e.referrerPolicy&&(t.referrerPolicy=e.referrerPolicy),e.crossOrigin===`use-credentials`?t.credentials=`include`:e.crossOrigin===`anonymous`?t.credentials=`omit`:t.credentials=`same-origin`,t}function n(e){if(e.ep)return;e.ep=!0;let n=t(e);fetch(e.href,n)}})();var e=`<svg viewBox="0 0 100 100">
  <circle cx="35" cy="35" r="12" fill="white"/>
  <path d="M 35 50 Q 25 50 20 58 Q 15 66 15 75 L 15 85 Q 15 90 20 90 L 50 90 Q 55 90 55 85 L 55 75 Q 55 66 50 58 Q 45 50 35 50 Z" fill="white"/>
  <path d="M 60 25 Q 62 27 64 32" stroke="white" stroke-width="4" fill="none" stroke-linecap="round"/>
  <path d="M 67 20 Q 71 24 75 35" stroke="white" stroke-width="4" fill="none" stroke-linecap="round"/>
  <path d="M 75 15 Q 81 21 87 38" stroke="white" stroke-width="4" fill="none" stroke-linecap="round"/>
</svg>`;function t(t){let n=t===`#feedback-config`;return`
    <div class="header-wrap">
      <header class="header">
        <div class="header-left">
          <button class="menu-btn" id="menu-btn" aria-label="Open navigation" aria-expanded="false">
            <span></span><span></span><span></span>
          </button>
          <nav class="nav-dropdown" id="nav-dropdown" aria-hidden="true">
            <a href="#controller" class="nav-item${n?``:` active`}">Controller</a>
            <a href="#feedback-config" class="nav-item${n?` active`:``}">Feedback Config</a>
          </nav>
          <div class="logo-container">
            <div class="logo-circle">${e}</div>
            <span class="app-name">ViviSense</span>
          </div>
        </div>
        <div class="status-indicators">
          <div class="battery-indicator">
            <div class="battery-icon"><div class="battery-fill"></div></div>
            <span>80%</span>
          </div>
          <div class="ws-status">
            <div class="ws-dot" id="ws-dot"></div>
            <span>Live</span>
          </div>
        </div>
      </header>

      <div class="component-status-bar">
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
      </div>
    </div>`}function n(){let e=document.getElementById(`menu-btn`),t=document.getElementById(`nav-dropdown`);if(!e||!t)return;function n(){t.classList.add(`open`),e.setAttribute(`aria-expanded`,`true`),t.setAttribute(`aria-hidden`,`false`)}function r(){t.classList.remove(`open`),e.setAttribute(`aria-expanded`,`false`),t.setAttribute(`aria-hidden`,`true`)}e.addEventListener(`click`,e=>{e.stopPropagation(),t.classList.contains(`open`)?r():n()}),document.addEventListener(`click`,n=>{!t.contains(n.target)&&n.target!==e&&r()}),t.querySelectorAll(`.nav-item`).forEach(e=>{e.addEventListener(`click`,r)})}var r=`/ws`,i=null,a=null,o=[],s=null;function c(e){let t=document.getElementById(`ws-dot`);t&&t.classList.toggle(`connected`,e)}function l(e,t,n){let r=document.getElementById(e);if(!r)return;r.classList.remove(`state-online`,`state-degraded`,`state-offline`,`state-unknown`),r.classList.add(`state-${t}`);let i=r.querySelector(`.component-value`);i&&(i.textContent=n)}function u(e){return e===`online`||e===`degraded`||e===`offline`||e===`unknown`?e:`unknown`}function d(e){if(!e){l(`comp-main`,`unknown`,`--`),l(`comp-led`,`unknown`,`--`),l(`comp-left-pod`,`unknown`,`--`),l(`comp-right-pod`,`unknown`,`--`);return}let t=e.mainControllerConnected===!0,n=e.ledControllerConnected===!0,r=Number.isFinite(e.leftPodOnlineSensors)?Number(e.leftPodOnlineSensors):null,i=Number.isFinite(e.rightPodOnlineSensors)?Number(e.rightPodOnlineSensors):null,a=u(e.leftPodState),o=u(e.rightPodState);l(`comp-main`,t?`online`:`offline`,t?`Online`:`Offline`),l(`comp-led`,n?`online`:`offline`,n?`Online`:`Offline`),r===null?l(`comp-left-pod`,a,`--`):l(`comp-left-pod`,a,`${r}/4`),i===null?l(`comp-right-pod`,o,`--`):l(`comp-right-pod`,o,`${i}/4`)}function f(e){if(!e||typeof e!=`object`)return null;let t=e;return t.type===`status`?t:null}function p(){let e=`ws://${window.location.host}${r}`;i=new WebSocket(e),i.onopen=()=>{c(!0),a&&=(clearTimeout(a),null),d(s),console.log(`[WS] Connected`)},i.onclose=()=>{c(!1),d(null),console.log(`[WS] Disconnected - retrying in 3 s`),a=setTimeout(p,3e3)},i.onerror=()=>{i?.close()},i.onmessage=e=>{try{let t=JSON.parse(e.data),n=f(t);n&&(s=n,d(s)),o.forEach(e=>e(t))}catch{}}}function m(){!i||i.readyState===WebSocket.CLOSED?p():(c(i.readyState===WebSocket.OPEN),d(s))}function h(e){i&&i.readyState===WebSocket.OPEN?(i.send(JSON.stringify(e)),console.debug(`[WS] sent:`,JSON.stringify(e).substring(0,120))):console.warn(`[WS] message dropped - not connected:`,JSON.stringify(e).substring(0,80))}function g(e){o.push(e)}function _(e){let t=o.indexOf(e);t!==-1&&o.splice(t,1)}var v=`<svg viewBox="0 0 24 24">
  <path d="M12 14c1.66 0 3-1.34 3-3V5c0-1.66-1.34-3-3-3S9 3.34 9 5v6c0 1.66 1.34 3 3 3z"/>
  <path d="M17 11c0 2.76-2.24 5-5 5s-5-2.24-5-5H5c0 3.53 2.61 6.43 6 6.92V21h2v-3.08c3.39-.49 6-3.39 6-6.92h-2z"/>
</svg>`;function y(){return`
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
          <div class="mic-btn" data-action="speak">${v}</div>
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

    </main>`}function b(){document.querySelectorAll(`[data-action]`).forEach(e=>{e.addEventListener(`click`,()=>{let t=e.dataset.action;t&&h({type:`navigate`,action:t})})});let e=document.getElementById(`volume-slider`);e?.addEventListener(`input`,()=>{h({type:`volume`,level:Number(e.value)})})}var x=[32,24,16,12,8,1],S=`#1a1a2e`,C=`#ff2222`,w=`#ff8800`,T=`#ffdd00`,E=`#00cc55`,D=`#0066ff`,O=[`N`,`E`,`S`,`W`],k=[`N`,`NE`,`SE`,`S`,`SW`,`NW`],A=[`N`,`NE`,`E`,`SE`,`S`,`SW`,`W`,`NW`];function j(e){let t=e;for(;t>=180;)t-=360;for(;t<-180;)t+=360;return t}function M(e,t){let n=t.length,r=360/n,i=Math.floor((e+r*.5)/r);return i%=n,i<0&&(i+=n),t[i]}function N(e,t,n,r=k){let i=[];return x.forEach((a,o)=>{let s=n[o]??0;if(a===1){i.push({x:e,y:t,ring:o,indexInRing:0,angleDeg:0,sector:r[0]});return}let c=360/a;for(let n=0;n<a;n++){let a=((180+n*c)%360+360)%360,l=j(a),u=(a-90)*(Math.PI/180),d=e+s*Math.cos(u),f=t+s*Math.sin(u);i.push({x:d,y:f,ring:o,indexInRing:n,angleDeg:l,sector:M(l,r)})}}),i}function P(e,t,n=100,r=!0,i=k){let a=e/2,o=e/2,s=e/2*.87,c=x.length,l=x.map((e,t)=>t===c-1?0:s*(1-t/(c-2)*.86)),u=Math.max(2.2,e*.018),d=N(a,o,l,i).map(e=>{let n=t(e),i=n!==`#1a1a2e`&&r?`filter="url(#ledglow)"`:``;return`<circle cx="${e.x.toFixed(2)}" cy="${e.y.toFixed(2)}" r="${u}" fill="${n}" ${i}/>`}).join(``),f=(1-n/100)*.85,p=n<100?`<circle cx="${a}" cy="${o}" r="${s+6}" fill="rgba(0,0,0,${f.toFixed(3)})" pointer-events="none"/>`:``;return`<svg width="${e}" height="${e}" viewBox="0 0 ${e} ${e}" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <filter id="ledglow" x="-60%" y="-60%" width="220%" height="220%">
      <feGaussianBlur stdDeviation="1.8" result="blur"/>
      <feMerge><feMergeNode in="blur"/><feMergeNode in="SourceGraphic"/></feMerge>
    </filter>
  </defs>
  <circle cx="${a}" cy="${o}" r="${s+8}" fill="#0a0a14"/>
  <circle cx="${a}" cy="${o}" r="${s+8}" fill="none" stroke="#22223a" stroke-width="1.5"/>
  ${d}
  ${p}
</svg>`}function F(e){return e.ring===5?D:e.sector===`N`?C:e.sector===`SE`?w:e.sector===`SW`?T:E}function I(e){return e.ring===5?D:e.ring===4&&Math.abs(e.angleDeg)<20?C:e.ring===2&&e.angleDeg>85&&e.angleDeg<130?w:e.ring===0&&e.angleDeg<-90&&e.angleDeg>-130?T:S}function L(e){return e===4?O:e===8?A:k}function R(e){return Object.fromEntries(L(e).map(e=>[e,!0]))}function z(){return{audioEnabled:!1,visualEnabled:!1,selectedMode:`sector`,selectedAudioMode:null,advanced:{thresholds:{redMax:60,orangeMax:105,yellowMax:150},sectorCount:6,activeSectors:R(6),brightness:100}}}var B=z(),V=!1,H=null,U=[280,220,160,110,60,0];function W(e){return e<=B.advanced.thresholds.redMax?C:e<=B.advanced.thresholds.orangeMax?w:e<=B.advanced.thresholds.yellowMax?T:E}function G(e){return e.ring===5?D:B.advanced.activeSectors[e.sector]?W(U[e.ring]??9999):S}function K(){let e=document.getElementById(`advanced-preview`);if(!e)return;let t=L(B.advanced.sectorCount);e.innerHTML=P(220,G,B.advanced.brightness,!0,t)}function q(){let{thresholds:e,sectorCount:t,brightness:n}=B.advanced,r=L(t).map(e=>B.advanced.activeSectors[e]??!0);h({type:`config`,zoneMode:t,brightness:n,redThreshold:e.redMax,orangeThreshold:e.orangeMax,yellowThreshold:e.yellowMax,activeSectors:r,renderMode:+(B.selectedMode===`radar`),audioEnabled:B.audioEnabled,visualEnabled:B.visualEnabled})}function J(e){if(!e){h({type:`preview`,active:!1});return}let{thresholds:t,sectorCount:n,brightness:r}=B.advanced,i=L(n).map(e=>B.advanced.activeSectors[e]??!0);h({type:`preview`,active:!0,zoneMode:n,brightness:r,redThreshold:t.redMax,orangeThreshold:t.orangeMax,yellowThreshold:t.yellowMax,activeSectors:i})}function Y(){return L(B.advanced.sectorCount).map(e=>`<button class="sector-toggle${B.advanced.activeSectors[e]??!1?` active`:``}" data-sector="${e}">${e}</button>`).join(``)}function X(e){let t=e.zoneMode,n=e.brightness,r=e.redThreshold,i=e.orangeThreshold,a=e.yellowThreshold,o=e.renderMode,s=e.activeSectors,c=e.audioEnabled,l=e.visualEnabled;t!==void 0&&[4,6,8].includes(t)&&(B.advanced.sectorCount=t),n!==void 0&&(B.advanced.brightness=n),r!==void 0&&(B.advanced.thresholds.redMax=r),i!==void 0&&(B.advanced.thresholds.orangeMax=i),a!==void 0&&(B.advanced.thresholds.yellowMax=a),o!==void 0&&(o===0||o===1)&&(B.selectedMode=o===1?`radar`:`sector`),s!==void 0&&L(B.advanced.sectorCount).forEach((e,t)=>{B.advanced.activeSectors[e]=s[t]??!0}),c!==void 0&&(B.audioEnabled=c),l!==void 0&&(B.visualEnabled=l);let u=document.getElementById(`brightness-slider`),d=document.getElementById(`brightness-value`);u&&n!==void 0&&(u.value=String(n)),d&&n!==void 0&&(d.textContent=`${n}%`);let f=document.getElementById(`threshold-red`),p=document.getElementById(`red-threshold-val`),m=document.getElementById(`threshold-orange`),h=document.getElementById(`orange-threshold-val`),g=document.getElementById(`threshold-yellow`),_=document.getElementById(`yellow-threshold-val`),v=document.getElementById(`green-starts-val`);if(f&&r!==void 0&&(f.value=String(r)),p&&r!==void 0&&(p.textContent=String(r)),m&&i!==void 0&&(m.value=String(i)),h&&i!==void 0&&(h.textContent=String(i)),g&&a!==void 0&&(g.value=String(a)),_&&a!==void 0&&(_.textContent=String(a)),v&&a!==void 0&&(v.textContent=String(a)),document.querySelectorAll(`.mode-card[data-mode]`).forEach(e=>{e.classList.toggle(`selected`,e.dataset.mode===B.selectedMode)}),t!==void 0||s!==void 0){document.querySelectorAll(`.sector-count-btn[data-count]`).forEach(e=>{e.classList.toggle(`active`,Number(e.dataset.count)===B.advanced.sectorCount)});let e=document.getElementById(`sector-toggles-container`);e&&(e.innerHTML=Y(),Q())}let y=document.getElementById(`audio-enable-btn`),b=document.getElementById(`audio-mode-cards`);y&&(y.classList.toggle(`active`,B.audioEnabled),y.textContent=B.audioEnabled?`Enabled`:`Disabled`),b?.classList.toggle(`section-disabled`,!B.audioEnabled);let x=document.getElementById(`visual-enable-btn`),S=document.getElementById(`visual-mode-cards`);x&&(x.classList.toggle(`active`,B.visualEnabled),x.textContent=B.visualEnabled?`Enabled`:`Disabled`),S?.classList.toggle(`section-disabled`,!B.visualEnabled),K()}function Z(){let e=P(120,F,100,!1),t=P(120,I,100,!1),{thresholds:n,sectorCount:r,brightness:i}=B.advanced,a=P(220,G,i,!0,L(r)),o=Y();return`
    <main class="config-content">

      <section class="config-section">
        <div class="section-header">
          <div>
            <h2 class="section-title">Audio Feedback Mode</h2>
            <p class="section-subtitle">Select how audio communicates obstacle proximity.</p>
          </div>
          <button class="enable-btn${B.audioEnabled?` active`:``}" id="audio-enable-btn">
            ${B.audioEnabled?`Enabled`:`Disabled`}
          </button>
        </div>

        <div class="mode-cards${B.audioEnabled?``:` section-disabled`}" id="audio-mode-cards">
          <div class="mode-card${B.selectedAudioMode===`tonal`?` selected`:``}" data-audio-mode="tonal">
            <div class="mode-diagram"><svg width="120" height="120" viewBox="0 0 120 120" xmlns="http://www.w3.org/2000/svg">
    <circle cx="60" cy="60" r="58" fill="#0a0a14"/>
    <circle cx="60" cy="60" r="58" fill="none" stroke="#22223a" stroke-width="1.5"/>
    <rect x="16" y="46" width="13" height="28" rx="6.5" fill="#764ba2" opacity="0.45"/>
    <rect x="35" y="32" width="13" height="56" rx="6.5" fill="#764ba2" opacity="0.7"/>
    <rect x="54" y="20" width="13" height="80" rx="6.5" fill="#9b59b6"/>
    <rect x="73" y="35" width="13" height="50" rx="6.5" fill="#764ba2" opacity="0.7"/>
    <rect x="92" y="48" width="13" height="24" rx="6.5" fill="#764ba2" opacity="0.45"/>
  </svg></div>
            <h3 class="mode-name">Tonal</h3>
            <p class="mode-desc">Proximity communicated through tones that change in pitch as obstacles get closer.</p>
          </div>

          <div class="mode-card${B.selectedAudioMode===`verbal`?` selected`:``}" data-audio-mode="verbal">
            <div class="mode-diagram"><svg width="120" height="120" viewBox="0 0 120 120" xmlns="http://www.w3.org/2000/svg">
    <circle cx="60" cy="60" r="58" fill="#0a0a14"/>
    <circle cx="60" cy="60" r="58" fill="none" stroke="#22223a" stroke-width="1.5"/>
    <rect x="46" y="16" width="28" height="46" rx="14" fill="#764ba2"/>
    <path d="M26 54 Q26 90 60 90 Q94 90 94 54" fill="none" stroke="#764ba2" stroke-width="4.5" stroke-linecap="round"/>
    <line x1="60" y1="90" x2="60" y2="104" stroke="#764ba2" stroke-width="4.5" stroke-linecap="round"/>
    <line x1="42" y1="104" x2="78" y2="104" stroke="#764ba2" stroke-width="4.5" stroke-linecap="round"/>
  </svg></div>
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
          <button class="enable-btn${B.visualEnabled?` active`:``}" id="visual-enable-btn">
            ${B.visualEnabled?`Enabled`:`Disabled`}
          </button>
        </div>

        <div class="mode-cards${B.visualEnabled?``:` section-disabled`}" id="visual-mode-cards">
          <div class="mode-card${B.selectedMode===`sector`?` selected`:``}" data-mode="sector">
            <div class="mode-diagram">${e}</div>
            <h3 class="mode-name">Sector Mode</h3>
            <p class="mode-desc">Each zone shows red/orange/yellow by nearest obstacle distance, or green when clear.</p>
          </div>

          <div class="mode-card${B.selectedMode===`radar`?` selected`:``}" data-mode="radar">
            <div class="mode-diagram">${t}</div>
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
            <h3 class="subsection-title">Distance Thresholds</h3>
            <div class="threshold-control">
              <div class="threshold-row">
                <div class="threshold-label">
                  <span class="color-dot dot-red"></span>
                  <span>Red up to <strong><span id="red-threshold-val">${n.redMax}</span> cm</strong></span>
                </div>
                <input type="range" id="threshold-red" min="10" max="400" value="${n.redMax}" />
              </div>
              <div class="threshold-row">
                <div class="threshold-label">
                  <span class="color-dot dot-orange"></span>
                  <span>Orange up to <strong><span id="orange-threshold-val">${n.orangeMax}</span> cm</strong></span>
                </div>
                <input type="range" id="threshold-orange" min="20" max="600" value="${n.orangeMax}" />
              </div>
              <div class="threshold-row">
                <div class="threshold-label">
                  <span class="color-dot dot-yellow"></span>
                  <span>Yellow up to <strong><span id="yellow-threshold-val">${n.yellowMax}</span> cm</strong></span>
                </div>
                <input type="range" id="threshold-yellow" min="30" max="800" value="${n.yellowMax}" />
              </div>
              <p class="threshold-note">
                <span class="color-dot dot-green"></span>
                Green beyond <span id="green-starts-val">${n.yellowMax}</span> cm
              </p>
            </div>
          </div>

          <div class="subsection">
            <h3 class="subsection-title">Active Sectors</h3>
            <div class="sector-count-selector">
              <button class="sector-count-btn${r===4?` active`:``}" data-count="4">4 Sectors</button>
              <button class="sector-count-btn${r===6?` active`:``}" data-count="6">6 Sectors</button>
              <button class="sector-count-btn${r===8?` active`:``}" data-count="8">8 Sectors</button>
            </div>
            <div class="sector-toggles" id="sector-toggles-container">${o}</div>
          </div>

          <div class="subsection">
            <h3 class="subsection-title">Brightness - <span id="brightness-value">${i}%</span></h3>
            <input type="range" id="brightness-slider" min="0" max="100" value="${i}" />
          </div>

          <div class="subsection">
            <div class="preview-header">
              <h3 class="subsection-title">Live Preview</h3>
              <button class="display-btn" id="display-toggle">Display</button>
            </div>
            <div class="preview-container" id="preview-container">
              <div id="advanced-preview">${a}</div>
            </div>
          </div>

        </div>
      </section>
    </main>`}function Q(){document.querySelectorAll(`.sector-toggle`).forEach(e=>{e.addEventListener(`click`,()=>{let t=e.dataset.sector,n=B.advanced.activeSectors[t]??!1;B.advanced.activeSectors[t]=!n,e.classList.toggle(`active`,!n),K(),q(),V&&J(!0)})})}function ee(){B=z();let e=document.getElementById(`audio-enable-btn`),t=document.getElementById(`audio-mode-cards`);e?.addEventListener(`click`,()=>{B.audioEnabled=!B.audioEnabled,e.classList.toggle(`active`,B.audioEnabled),e.textContent=B.audioEnabled?`Enabled`:`Disabled`,t?.classList.toggle(`section-disabled`,!B.audioEnabled),q()});let n=document.getElementById(`visual-enable-btn`),r=document.getElementById(`visual-mode-cards`);n?.addEventListener(`click`,()=>{B.visualEnabled=!B.visualEnabled,n.classList.toggle(`active`,B.visualEnabled),n.textContent=B.visualEnabled?`Enabled`:`Disabled`,r?.classList.toggle(`section-disabled`,!B.visualEnabled),q()}),document.querySelectorAll(`.mode-card[data-mode]`).forEach(e=>{e.addEventListener(`click`,()=>{let t=e.dataset.mode;t!==B.selectedMode&&(B.selectedMode=t,document.querySelectorAll(`.mode-card[data-mode]`).forEach(e=>e.classList.remove(`selected`)),e.classList.add(`selected`),q())})}),document.querySelectorAll(`.mode-card[data-audio-mode]`).forEach(e=>{e.addEventListener(`click`,()=>{let t=e.dataset.audioMode;B.selectedAudioMode=t,document.querySelectorAll(`.mode-card[data-audio-mode]`).forEach(e=>e.classList.remove(`selected`)),e.classList.add(`selected`)})}),V=!1,H&&_(H),H=e=>{let t=e;t.type===`status`&&X(t)},g(H),h({type:`getConfig`});let i=document.getElementById(`display-toggle`);i?.addEventListener(`click`,()=>{V=!V,i.classList.toggle(`active`,V),V?J(!0):(J(!1),q())});let a=document.getElementById(`advanced-toggle`),o=document.getElementById(`advanced-body`),s=document.getElementById(`advanced-chevron`);a?.addEventListener(`click`,()=>{let e=o?.classList.toggle(`open`);s?.classList.toggle(`open`,e)});let c=document.getElementById(`threshold-red`),l=document.getElementById(`threshold-orange`),u=document.getElementById(`threshold-yellow`),d=document.getElementById(`red-threshold-val`),f=document.getElementById(`orange-threshold-val`),p=document.getElementById(`yellow-threshold-val`),m=document.getElementById(`green-starts-val`);c?.addEventListener(`input`,()=>{let e=Number(c.value);e>=B.advanced.thresholds.orangeMax&&(e=B.advanced.thresholds.orangeMax-10,c.value=String(e)),B.advanced.thresholds.redMax=e,d&&(d.textContent=String(e)),K(),q(),V&&J(!0)}),l?.addEventListener(`input`,()=>{let e=Number(l.value);e<=B.advanced.thresholds.redMax&&(e=B.advanced.thresholds.redMax+10),e>=B.advanced.thresholds.yellowMax&&(e=B.advanced.thresholds.yellowMax-10),l.value=String(e),B.advanced.thresholds.orangeMax=e,f&&(f.textContent=String(e)),K(),q(),V&&J(!0)}),u?.addEventListener(`input`,()=>{let e=Number(u.value);e<=B.advanced.thresholds.orangeMax&&(e=B.advanced.thresholds.orangeMax+10,u.value=String(e)),B.advanced.thresholds.yellowMax=e,p&&(p.textContent=String(e)),m&&(m.textContent=String(e)),K(),q(),V&&J(!0)}),document.querySelectorAll(`.sector-count-btn[data-count]`).forEach(e=>{e.addEventListener(`click`,()=>{let t=Number(e.dataset.count);if(t===B.advanced.sectorCount)return;let n=B.advanced.activeSectors,r={};for(let e of L(t))r[e]=n[e]??!0;B.advanced.sectorCount=t,B.advanced.activeSectors=r,document.querySelectorAll(`.sector-count-btn[data-count]`).forEach(e=>e.classList.remove(`active`)),e.classList.add(`active`);let i=document.getElementById(`sector-toggles-container`);i&&(i.innerHTML=Y()),Q(),K(),q(),V&&J(!0)})}),Q();let v=document.getElementById(`brightness-slider`),y=document.getElementById(`brightness-value`);v?.addEventListener(`input`,()=>{B.advanced.brightness=Number(v.value),y&&(y.textContent=`${B.advanced.brightness}%`),K(),q(),V&&J(!0)})}function te(){return window.location.hash||`#controller`}function $(){let e=te(),r=document.getElementById(`app`);if(!r)return;let i,a;e===`#feedback-config`?(i=Z(),a=ee):(i=y(),a=b),r.innerHTML=t(e)+`<div id="page-content">${i}</div>`,n(),m(),a()}function ne(){window.addEventListener(`hashchange`,$),$()}m(),ne();