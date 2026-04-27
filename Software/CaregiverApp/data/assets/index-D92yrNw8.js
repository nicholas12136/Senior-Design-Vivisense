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
    </div>`}function n(){let e=document.getElementById(`menu-btn`),t=document.getElementById(`nav-dropdown`);if(!e||!t)return;function n(){t.classList.add(`open`),e.setAttribute(`aria-expanded`,`true`),t.setAttribute(`aria-hidden`,`false`)}function r(){t.classList.remove(`open`),e.setAttribute(`aria-expanded`,`false`),t.setAttribute(`aria-hidden`,`true`)}e.addEventListener(`click`,e=>{e.stopPropagation(),t.classList.contains(`open`)?r():n()}),document.addEventListener(`click`,n=>{!t.contains(n.target)&&n.target!==e&&r()}),t.querySelectorAll(`.nav-item`).forEach(e=>{e.addEventListener(`click`,r)});let i=document.getElementById(`connections-toggle`),a=document.getElementById(`connections-panel`),o=document.getElementById(`connections-chevron`);i?.addEventListener(`click`,()=>{let e=a?.classList.toggle(`open`);i.setAttribute(`aria-expanded`,e?`true`:`false`),a?.setAttribute(`aria-hidden`,e?`false`:`true`),o&&(o.style.transform=e?`rotate(180deg)`:``)})}var r=`/ws`,i=null,a=null,o=[],s=null;function c(e){let t=document.getElementById(`ws-dot`);t&&t.classList.toggle(`connected`,e)}function l(e,t,n){let r=document.getElementById(e);if(!r)return;r.classList.remove(`state-online`,`state-degraded`,`state-offline`,`state-unknown`),r.classList.add(`state-${t}`);let i=r.querySelector(`.component-value`);i&&(i.textContent=n)}function u(e){return e===`online`||e===`degraded`||e===`offline`||e===`unknown`?e:`unknown`}function d(e){if(!e){l(`comp-main`,`unknown`,`--`),l(`comp-led`,`unknown`,`--`),l(`comp-left-pod`,`unknown`,`--`),l(`comp-right-pod`,`unknown`,`--`),l(`comp-tower-pod`,`unknown`,`--`);return}let t=e.mainControllerConnected===!0,n=e.ledControllerConnected===!0,r=Number.isFinite(e.leftPodOnlineSensors)?Number(e.leftPodOnlineSensors):null,i=Number.isFinite(e.rightPodOnlineSensors)?Number(e.rightPodOnlineSensors):null,a=Number.isFinite(e.towerPodOnlineSensors)?Number(e.towerPodOnlineSensors):null,o=u(e.leftPodState),s=u(e.rightPodState),c=u(e.towerPodState);l(`comp-main`,t?`online`:`offline`,t?`Online`:`Offline`),l(`comp-led`,n?`online`:`offline`,n?`Online`:`Offline`),l(`comp-left-pod`,o,r===null?`--`:`${r}/2`),l(`comp-right-pod`,s,i===null?`--`:`${i}/2`),l(`comp-tower-pod`,c,a===null?`--`:`${a}/4`)}function f(e){if(!e||typeof e!=`object`)return null;let t=e;return t.type===`status`?t:null}function p(){let e=`ws://${window.location.host}${r}`;i=new WebSocket(e),i.onopen=()=>{c(!0),a&&=(clearTimeout(a),null),d(s),console.log(`[WS] Connected`)},i.onclose=()=>{c(!1),d(null),console.log(`[WS] Disconnected - retrying in 3 s`),a=setTimeout(p,3e3)},i.onerror=()=>{i?.close()},i.onmessage=e=>{try{let t=JSON.parse(e.data),n=f(t);n&&(s=n,d(s)),o.forEach(e=>e(t))}catch{}}}function m(){!i||i.readyState===WebSocket.CLOSED?p():(c(i.readyState===WebSocket.OPEN),d(s))}function h(e){i&&i.readyState===WebSocket.OPEN?(i.send(JSON.stringify(e)),console.debug(`[WS] sent:`,JSON.stringify(e).substring(0,120))):console.warn(`[WS] message dropped - not connected:`,JSON.stringify(e).substring(0,80))}function g(e){o.push(e)}function _(e){let t=o.indexOf(e);t!==-1&&o.splice(t,1)}var v=`<svg viewBox="0 0 24 24">
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
        <div class="custom-btn filled" data-action="stayonline">Stay on<br>the line</div>
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
</svg>`}function F(e){return e.ring===5?D:e.sector===`N`?C:e.sector===`SE`?w:e.sector===`SW`?T:E}function I(e){return e.ring===5?D:e.ring===4&&Math.abs(e.angleDeg)<20?C:e.ring===2&&e.angleDeg>85&&e.angleDeg<130?w:e.ring===0&&e.angleDeg<-90&&e.angleDeg>-130?T:S}function L(e){return e===4?O:e===8?A:k}function R(e){return Object.fromEntries(L(e).map(e=>[e,!0]))}function z(){return{audioEnabled:!1,visualEnabled:!1,selectedMode:`sector`,selectedAudioMode:null,obstacleVolume:200,advanced:{thresholds:{redMax:60,orangeMax:105,yellowMax:150},sectorCount:6,activeSectors:R(6),brightness:20,tones:{redPitchHz:1200,redTempoMs:150,orangePitchHz:800,orangeTempoMs:500,yellowPitchHz:400,yellowTempoMs:1e3}}}}var B=z(),V=null;function H(){let e=document.getElementById(`tonal-settings`);if(!e)return;let t=B.audioEnabled&&B.selectedAudioMode===`tonal`;e.classList.toggle(`section-hidden`,!t)}function U(){let{thresholds:e,sectorCount:t,brightness:n,tones:r}=B.advanced,i=L(t).map(e=>B.advanced.activeSectors[e]??!0);h({type:`config`,zoneMode:t,brightness:n,redThreshold:e.redMax,orangeThreshold:e.orangeMax,yellowThreshold:e.yellowMax,activeSectors:i,renderMode:+(B.selectedMode===`radar`),audioEnabled:B.audioEnabled&&B.selectedAudioMode!==null,audioMode:+(B.selectedAudioMode===`verbal`),obstacleVolume:B.obstacleVolume,visualEnabled:B.visualEnabled,toneRedPitchHz:r.redPitchHz,toneRedTempoMs:r.redTempoMs,toneOrangePitchHz:r.orangePitchHz,toneOrangeTempoMs:r.orangeTempoMs,toneYellowPitchHz:r.yellowPitchHz,toneYellowTempoMs:r.yellowTempoMs})}function W(){return L(B.advanced.sectorCount).map(e=>`<button class="sector-toggle${B.advanced.activeSectors[e]??!1?` active`:``}" data-sector="${e}">${e}</button>`).join(``)}function G(){document.querySelectorAll(`.mode-card[data-mode]`).forEach(e=>{e.classList.toggle(`selected`,e.dataset.mode===B.selectedMode)})}function K(e){let t=e.zoneMode,n=e.brightness,r=e.redThreshold,i=e.orangeThreshold,a=e.yellowThreshold,o=e.renderMode,s=e.activeSectors;e.audioEnabled;let c=e.visualEnabled,l=e.obstacleVolume;t!==void 0&&[4,6,8].includes(t)&&(B.advanced.sectorCount=t),n!==void 0&&(B.advanced.brightness=n),r!==void 0&&(B.advanced.thresholds.redMax=r),i!==void 0&&(B.advanced.thresholds.orangeMax=i),a!==void 0&&(B.advanced.thresholds.yellowMax=a),o!==void 0&&(o===0||o===1)&&(B.selectedMode=o===1?`radar`:`sector`),s!==void 0&&L(B.advanced.sectorCount).forEach((e,t)=>{B.advanced.activeSectors[e]=s[t]??!0}),c!==void 0&&(B.visualEnabled=c);let u=document.activeElement;for(let[t,n,r]of[[`redPitchHz`,`tone-red-pitch`,`tone-red-pitch-val`],[`redTempoMs`,`tone-red-tempo`,`tone-red-tempo-val`],[`orangePitchHz`,`tone-orange-pitch`,`tone-orange-pitch-val`],[`orangeTempoMs`,`tone-orange-tempo`,`tone-orange-tempo-val`],[`yellowPitchHz`,`tone-yellow-pitch`,`tone-yellow-pitch-val`],[`yellowTempoMs`,`tone-yellow-tempo`,`tone-yellow-tempo-val`]]){let i=e[`tone${t.charAt(0).toUpperCase()}${t.slice(1)}`];if(i===void 0)continue;B.advanced.tones[t]=i;let a=document.getElementById(n),o=document.getElementById(r);a&&u!==a&&(a.value=String(t.endsWith(`Ms`)?2050-i:i)),o&&(o.textContent=t.endsWith(`Hz`)?`${i} Hz`:`${i} ms`)}let d=document.getElementById(`brightness-slider`),f=document.getElementById(`brightness-value`);d&&n!==void 0&&u!==d&&(d.value=String(n)),f&&n!==void 0&&(f.textContent=`${n}%`),l!==void 0&&(B.obstacleVolume=l);let p=document.getElementById(`obstacle-volume-slider`),m=document.getElementById(`obstacle-volume-value`);p&&l!==void 0&&u!==p&&(p.value=String(Math.round(l/2.55))),m&&l!==void 0&&(m.textContent=`${Math.round(l/2.55)}%`);let h=document.getElementById(`threshold-red`),g=document.getElementById(`red-threshold-val`),_=document.getElementById(`threshold-orange`),v=document.getElementById(`orange-threshold-val`),y=document.getElementById(`threshold-yellow`),b=document.getElementById(`yellow-threshold-val`),x=document.getElementById(`green-starts-val`);if(h&&r!==void 0&&u!==h&&(h.value=String(r)),g&&r!==void 0&&(g.textContent=String(r)),_&&i!==void 0&&u!==_&&(_.value=String(i)),v&&i!==void 0&&(v.textContent=String(i)),y&&a!==void 0&&u!==y&&(y.value=String(a)),b&&a!==void 0&&(b.textContent=String(a)),x&&a!==void 0&&(x.textContent=String(a)),G(),H(),t!==void 0||s!==void 0){document.querySelectorAll(`.sector-count-btn[data-count]`).forEach(e=>{e.classList.toggle(`active`,Number(e.dataset.count)===B.advanced.sectorCount)});let e=document.getElementById(`sector-toggles-container`);e&&(e.innerHTML=W(),J())}let S=document.getElementById(`audio-enable-btn`),C=document.getElementById(`audio-mode-cards`);S&&(S.classList.toggle(`active`,B.audioEnabled),S.textContent=B.audioEnabled?`Enabled`:`Disabled`),C?.classList.toggle(`section-disabled`,!B.audioEnabled);let w=document.getElementById(`visual-enable-btn`),T=document.getElementById(`visual-mode-cards`);w&&(w.classList.toggle(`active`,B.visualEnabled),w.textContent=B.visualEnabled?`Enabled`:`Disabled`),T?.classList.toggle(`section-disabled`,!B.visualEnabled)}function q(){let e=P(120,F,100,!1),t=P(120,I,100,!1),{thresholds:n,sectorCount:r,brightness:i,tones:a}=B.advanced,o=W();return`
    <main class="config-content">

      <section class="config-section">
        <div class="section-header">
          <div>
            <h2 class="section-title">Obstacle Audio Feedback Mode</h2>
            <p class="section-subtitle">Controls tonal/verbal obstacle cues only. Controller command prompts always play.</p>
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
            <h3 class="subsection-title">Active Sectors</h3>
            <div class="sector-count-selector">
              <button class="sector-count-btn${r===4?` active`:``}" data-count="4">4 Sectors</button>
              <button class="sector-count-btn${r===6?` active`:``}" data-count="6">6 Sectors</button>
              <button class="sector-count-btn${r===8?` active`:``}" data-count="8">8 Sectors</button>
            </div>
            <div class="sector-toggles" id="sector-toggles-container">${o}</div>
          </div>

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

          <div class="subsection${B.audioEnabled&&B.selectedAudioMode===`tonal`?``:` section-hidden`}" id="tonal-settings">
            <h3 class="subsection-title">Tonal Settings</h3>
            <div class="subsection">
              <h3 class="subsection-title">Obstacle Audio Volume - <span id="obstacle-volume-value">${Math.round(B.obstacleVolume/2.55)}%</span></h3>
              <input type="range" id="obstacle-volume-slider" min="0" max="100" value="${Math.round(B.obstacleVolume/2.55)}" />
            </div>

            <div class="tone-zone">
              <div class="tone-zone-header">
                <span class="color-dot dot-red"></span><span>Red Zone</span>
              </div>
              <div class="tone-control-row">
                <span class="tone-axis-label">Pitch</span>
                <span class="tone-end-label">Low</span>
                <input type="range" id="tone-red-pitch" min="200" max="4000" value="${a.redPitchHz}" />
                <span class="tone-end-label">High</span>
                <span class="tone-val" id="tone-red-pitch-val">${a.redPitchHz} Hz</span>
              </div>
              <div class="tone-control-row">
                <span class="tone-axis-label">Tempo</span>
                <span class="tone-end-label">Slow</span>
                <input type="range" id="tone-red-tempo" min="50" max="2000" value="${2050-a.redTempoMs}" />
                <span class="tone-end-label">Rapid</span>
                <span class="tone-val" id="tone-red-tempo-val">${a.redTempoMs} ms</span>
              </div>
            </div>

            <div class="tone-zone">
              <div class="tone-zone-header">
                <span class="color-dot dot-orange"></span><span>Orange Zone</span>
              </div>
              <div class="tone-control-row">
                <span class="tone-axis-label">Pitch</span>
                <span class="tone-end-label">Low</span>
                <input type="range" id="tone-orange-pitch" min="200" max="4000" value="${a.orangePitchHz}" />
                <span class="tone-end-label">High</span>
                <span class="tone-val" id="tone-orange-pitch-val">${a.orangePitchHz} Hz</span>
              </div>
              <div class="tone-control-row">
                <span class="tone-axis-label">Tempo</span>
                <span class="tone-end-label">Slow</span>
                <input type="range" id="tone-orange-tempo" min="50" max="2000" value="${2050-a.orangeTempoMs}" />
                <span class="tone-end-label">Rapid</span>
                <span class="tone-val" id="tone-orange-tempo-val">${a.orangeTempoMs} ms</span>
              </div>
            </div>

            <div class="tone-zone">
              <div class="tone-zone-header">
                <span class="color-dot dot-yellow"></span><span>Yellow Zone</span>
              </div>
              <div class="tone-control-row">
                <span class="tone-axis-label">Pitch</span>
                <span class="tone-end-label">Low</span>
                <input type="range" id="tone-yellow-pitch" min="200" max="4000" value="${a.yellowPitchHz}" />
                <span class="tone-end-label">High</span>
                <span class="tone-val" id="tone-yellow-pitch-val">${a.yellowPitchHz} Hz</span>
              </div>
              <div class="tone-control-row">
                <span class="tone-axis-label">Tempo</span>
                <span class="tone-end-label">Slow</span>
                <input type="range" id="tone-yellow-tempo" min="50" max="2000" value="${2050-a.yellowTempoMs}" />
                <span class="tone-end-label">Rapid</span>
                <span class="tone-val" id="tone-yellow-tempo-val">${a.yellowTempoMs} ms</span>
              </div>
            </div>
          </div>

          <div class="subsection">
            <h3 class="subsection-title">Brightness - <span id="brightness-value">${i}%</span></h3>
            <input type="range" id="brightness-slider" min="0" max="100" value="${i}" />
          </div>

        </div>
      </section>
    </main>`}function J(){document.querySelectorAll(`.sector-toggle`).forEach(e=>{e.addEventListener(`click`,()=>{let t=e.dataset.sector,n=B.advanced.activeSectors[t]??!1;B.advanced.activeSectors[t]=!n,e.classList.toggle(`active`,!n),U()})})}function Y(){B=z(),G();let e=document.getElementById(`audio-enable-btn`),t=document.getElementById(`audio-mode-cards`);e?.addEventListener(`click`,()=>{B.audioEnabled=!B.audioEnabled,e.classList.toggle(`active`,B.audioEnabled),e.textContent=B.audioEnabled?`Enabled`:`Disabled`,t?.classList.toggle(`section-disabled`,!B.audioEnabled),H(),U()});let n=document.getElementById(`visual-enable-btn`),r=document.getElementById(`visual-mode-cards`);n?.addEventListener(`click`,()=>{B.visualEnabled=!B.visualEnabled,n.classList.toggle(`active`,B.visualEnabled),n.textContent=B.visualEnabled?`Enabled`:`Disabled`,r?.classList.toggle(`section-disabled`,!B.visualEnabled),U()}),document.querySelectorAll(`.mode-card[data-mode]`).forEach(e=>{e.addEventListener(`click`,()=>{let t=e.dataset.mode;t!==B.selectedMode&&(B.selectedMode=t,document.querySelectorAll(`.mode-card[data-mode]`).forEach(e=>e.classList.remove(`selected`)),e.classList.add(`selected`),U())})}),document.querySelectorAll(`.mode-card[data-audio-mode]`).forEach(e=>{e.addEventListener(`click`,()=>{let t=e.dataset.audioMode;B.selectedAudioMode=t,document.querySelectorAll(`.mode-card[data-audio-mode]`).forEach(e=>e.classList.remove(`selected`)),e.classList.add(`selected`),H(),U()})});for(let{sliderId:e,valId:t,key:n,unit:r}of[{sliderId:`tone-red-pitch`,valId:`tone-red-pitch-val`,key:`redPitchHz`,unit:`Hz`},{sliderId:`tone-red-tempo`,valId:`tone-red-tempo-val`,key:`redTempoMs`,unit:`ms`},{sliderId:`tone-orange-pitch`,valId:`tone-orange-pitch-val`,key:`orangePitchHz`,unit:`Hz`},{sliderId:`tone-orange-tempo`,valId:`tone-orange-tempo-val`,key:`orangeTempoMs`,unit:`ms`},{sliderId:`tone-yellow-pitch`,valId:`tone-yellow-pitch-val`,key:`yellowPitchHz`,unit:`Hz`},{sliderId:`tone-yellow-tempo`,valId:`tone-yellow-tempo-val`,key:`yellowTempoMs`,unit:`ms`}]){let i=document.getElementById(e),a=document.getElementById(t);i?.addEventListener(`input`,()=>{let e=r===`ms`?2050-Number(i.value):Number(i.value);B.advanced.tones[n]=e,a&&(a.textContent=`${e} ${r}`),U()})}V&&_(V),V=e=>{let t=e;t.type===`status`&&K(t)},g(V),h({type:`getConfig`});let i=document.getElementById(`advanced-toggle`),a=document.getElementById(`advanced-body`),o=document.getElementById(`advanced-chevron`);i?.addEventListener(`click`,()=>{let e=a?.classList.toggle(`open`);o?.classList.toggle(`open`,e)});let s=document.getElementById(`threshold-red`),c=document.getElementById(`threshold-orange`),l=document.getElementById(`threshold-yellow`),u=document.getElementById(`red-threshold-val`),d=document.getElementById(`orange-threshold-val`),f=document.getElementById(`yellow-threshold-val`),p=document.getElementById(`green-starts-val`);s?.addEventListener(`input`,()=>{let e=Number(s.value);e>=B.advanced.thresholds.orangeMax&&(e=B.advanced.thresholds.orangeMax-10,s.value=String(e)),B.advanced.thresholds.redMax=e,u&&(u.textContent=String(e)),refreshPreview(),U(),previewActive&&sendPreview(!0)}),c?.addEventListener(`input`,()=>{let e=Number(c.value);e<=B.advanced.thresholds.redMax&&(e=B.advanced.thresholds.redMax+10),e>=B.advanced.thresholds.yellowMax&&(e=B.advanced.thresholds.yellowMax-10),c.value=String(e),B.advanced.thresholds.orangeMax=e,d&&(d.textContent=String(e)),refreshPreview(),U(),previewActive&&sendPreview(!0)}),l?.addEventListener(`input`,()=>{let e=Number(l.value);e<=B.advanced.thresholds.orangeMax&&(e=B.advanced.thresholds.orangeMax+10,l.value=String(e)),B.advanced.thresholds.yellowMax=e,f&&(f.textContent=String(e)),p&&(p.textContent=String(e)),refreshPreview(),U(),previewActive&&sendPreview(!0)}),document.querySelectorAll(`.sector-count-btn[data-count]`).forEach(e=>{e.addEventListener(`click`,()=>{let t=Number(e.dataset.count);if(t===B.advanced.sectorCount)return;let n=B.advanced.activeSectors,r={};for(let e of L(t))r[e]=n[e]??!0;B.advanced.sectorCount=t,B.advanced.activeSectors=r,document.querySelectorAll(`.sector-count-btn[data-count]`).forEach(e=>e.classList.remove(`active`)),e.classList.add(`active`);let i=document.getElementById(`sector-toggles-container`);i&&(i.innerHTML=W()),J(),U()})}),J();let m=document.getElementById(`brightness-slider`),v=document.getElementById(`brightness-value`);m?.addEventListener(`input`,()=>{B.advanced.brightness=Number(m.value),v&&(v.textContent=`${B.advanced.brightness}%`),U()});let y=document.getElementById(`obstacle-volume-slider`),b=document.getElementById(`obstacle-volume-value`);y?.addEventListener(`input`,()=>{B.obstacleVolume=Math.round(Number(y.value)*2.55),b&&(b.textContent=`${y.value}%`),U()})}function X(){return window.location.hash||`#controller`}function Z(){let e=X(),r=document.getElementById(`app`);if(!r)return;let i,a;e===`#feedback-config`?(i=q(),a=Y):(i=y(),a=b),r.innerHTML=t(e)+`<div id="page-content">${i}</div>`,n(),m(),a()}function Q(){window.addEventListener(`hashchange`,Z),Z()}m(),Q();