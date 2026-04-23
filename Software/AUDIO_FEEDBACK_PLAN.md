# ViviSense Audio Feedback Implementation Plan

**Status:** Architecture migration complete (nav commands + I2S moved to MainController). Tonal/verbal feedback implementation pending.
**Scope:** Define how tonal and verbal audio feedback should be added to the ViviSense firmware now that the MainController ESP32 owns the I2S amp and speaker. Covers arbitration, trigger logic, clip pipeline, and phasing.
**Companions:**

- `Software/SYSTEM_OVERVIEW.md` — whole-system reference
- `Software/RUNNING_THE_SYSTEM.md` — flashing and bring-up
- `Software/ESP firmware/MainController/PROGRAM_FLOW.md` — zone computation details

---

## 1. Goals

Two independent auditory channels, both heard by the child on the chair:

1. **Tonal feedback** — short tones that fire on threshold events (an obstacle in a zone crossing from a farther distance band into a closer one). Tone frequency encodes urgency, mirroring the LED color rings (yellow → orange → red).
2. **Verbal feedback** — spoken phrases, primarily automatic directional obstacle warnings ("obstacle ahead," "obstacle on the left"). A few alternative verbal modes are proposed in §6.3 for the team to evaluate.

Arbitration: single physical I2S channel, **priority-interrupt** scheduler. Verbal preempts tonal. Same-priority events while the channel is busy are dropped (with a short cooldown) — never queued indefinitely, because stale audio confuses the child.

Everything is gated by the `audioEnabled` flag (sent from CaregiverApp via ConfigPacket), with new sub-flags (`tonalEnabled`, `verbalEnabled`, verbal-mode selector) handled locally on MainController.

---

## 2. Hardware Placement

The MAX98357A + speaker is mounted on the **MainController ESP32** (chair-mounted). The speaker driver physically wires to GPIO pins on MainController: BCK=27, WS=26, DO=25.

This is the correct placement because MainController already computes per-zone obstacle proximity in real time (`closestMmByZone[8]`), making it the natural place to decide and play audio feedback without any additional inter-ESP communication.

Hardware checklist before firmware work:

- Speaker driver is mounted in the chair enclosure (not the caregiver's device).
- MAX98357A `GAIN` pad set appropriately — recommend 12 dB (pin floating) or 9 dB (100 kΩ to GND).
- Enclosure has a port or mesh aimed toward the child's head position.
- Power rail can sustain peak current (~500 mA for short verbal phrases at moderate volume).

---

## 3. Where Audio Logic Lives

All audio output is owned by **MainController**. That device:

- Has the I2S amp wired (BCK=27, WS=26, DO=25) — `setupI2S()` already in firmware.
- Computes `closestMmByZone[8]` every proximity cycle — tonal and verbal triggers read this directly with no additional ESP-NOW traffic.
- Receives `NavigationCommandPacket` (MSG_NAV_COMMAND = 0xB6) from CaregiverApp over ESP-NOW whenever the caregiver taps a nav button; plays the corresponding clip.
- Receives `audioEnabled` and `volume` from CaregiverApp in `ConfigPacket` (sent on every settings change or volume adjust).

**CaregiverApp's audio role** is now limited to:

- Sending audio config (audioEnabled, volume) inside the existing `ConfigPacket` whenever the caregiver changes settings.
- Sending `NavigationCommandPacket` when the caregiver taps forward / left / right / stop etc. in the UI.
- No I2S hardware, no `sounds.h`, no `playAudio()` — all removed.

No changes are required to `LEDRingController` or any sensor pod firmware.

---

## 4. System Diagram (Audio Path)

```
CaregiverApp (WiFi/UI/config)
  caregiver taps nav button
  -> NavigationCommandPacket (MSG_NAV_COMMAND=0xB6) over ESP-NOW
  -> ConfigPacket with audioEnabled + volume on any settings change

                   MainController (chair-mounted, owns speaker)
                     ├── Receives NavigationCommandPacket
                     │     -> dispatch to handleGoForward() / handleStop() / ...
                     │     -> playAudio(clip, len, rate)   [immediate]
                     │
                     ├── computeZoneProximityFromPolar()  (~15 Hz)
                     │     -> closestMmByZone[8] updated
                     │
                     └── AudioTrigger state machine  [future — Phase 2+]
                           - per-zone band tracking
                           - hysteresis + cooldown
                           - decides: no-op / tone(freq) / verbal(clipId)
                           │
                           ▼
                         AudioScheduler (priority interrupt)  [Phase 1]
                           - verbal preempts tonal
                           - busy-while-verbal → drop new tones
                           - publishes AudioEvent to FreeRTOS queue
                           │
                           ▼
                         AudioTask (FreeRTOS task on core 1)  [Phase 1]
                           - dequeues events
                           - tones: synthesize sine in RAM → i2s_write
                           - verbal: stream PROGMEM PCM → i2s_write
                           - honors volume, stop-flag for preempt
                           │
                           ▼
                         I2S_NUM_0 → MAX98357A → speaker
```

The current `playAudio()` on MainController is synchronous and blocks the caller with `portMAX_DELAY`. A dedicated FreeRTOS task is required (Phase 1) so ESP-NOW callbacks and audio playback do not stall each other.

---

## 5. Tonal Feedback — Detailed Design

### 5.1 Trigger Model

Threshold events, **not** continuous. A tone fires when a zone's closest distance **crosses into a closer band** than it was previously in. Crossing outward is silent (the LED handles the visual cue for "you're now safe").

Re-using the existing distance bands from `computeZoneProximityFromPolar()`:

| Band | Distance range (mm) | LED color | Tone |
|---|---|---|---|
| RED    | < proximityRedThresholdMm (default 600 mm)   | Red    | High urgency |
| ORANGE | redThreshold to yellowThreshold (600–1500)   | Orange | Medium urgency |
| YELLOW | yellowThreshold to 3000                      | Yellow | Low urgency |
| CLEAR  | ≥ 3000 mm                                    | Off    | (silent) |

### 5.2 Frequency Mapping (Proposed)

| Band   | Frequency | Duration | Rationale |
|---|---|---|---|
| YELLOW | 440 Hz (A4)  | 120 ms               | Calm, clearly audible, unobtrusive |
| ORANGE | 700 Hz       | 150 ms               | Brighter, more attention-getting |
| RED    | 1000 Hz (C6) | 180 ms, repeat once after 100 ms gap | Double-beep conveys urgency |

Waveform: pure sine, with a 10 ms linear fade-in and 20 ms fade-out to avoid click artifacts.

### 5.3 State Machine (Per Zone)

For each of the 4/6/8 zones (reads directly from `closestMmByZone[]`):

```
state = { last_band: CLEAR, last_event_ms: 0, pending_band: CLEAR, pending_since_ms: 0 }

on proximity update:
  new_band = band_of(closestMmByZone[z])

  if new_band == last_band:
    pending_band = CLEAR; continue

  # Debounce: require new band to persist for HYSTERESIS_MS
  if new_band != pending_band:
    pending_band = new_band
    pending_since_ms = now
    continue

  if now - pending_since_ms < HYSTERESIS_MS: continue

  # Band change confirmed
  if is_closer(new_band, last_band):
    if now - last_event_ms >= COOLDOWN_MS:
      fire_tone(new_band)
      last_event_ms = now

  last_band = new_band
  pending_band = CLEAR
```

Tuneables (starting values):

- `HYSTERESIS_MS = 150` — zone must stay in the new band for ~2 proximity cycles before a transition counts.
- `COOLDOWN_MS = 800` per zone — prevents chatter when something hovers near a boundary.
- Global **all-zones cooldown** of ~300 ms prevents a burst of tones when many zones change at once.

### 5.4 Synthesis vs. Pre-Baked

Recommend **runtime sine synthesis** over pre-baking tones into `sounds.h`:

- No PROGMEM cost.
- Frequency can be changed without reflashing.
- A 120 ms, 16 kHz, 16-bit mono tone is 3840 bytes of RAM — trivial.

```
fill_sine_buffer(freq_hz, duration_ms, buf):
  n = duration_ms * SAMPLE_RATE / 1000
  for i in 0..n-1:
    env = envelope(i, n, fade_in_ms=10, fade_out_ms=20)
    buf[i] = int16(AMPLITUDE * env * sin(2π * freq_hz * i / SAMPLE_RATE))
  return n samples
```

### 5.5 Active-Sector Interaction

If the caregiver has a zone disabled via `proximityActiveSectors` bitmask (already tracked on MainController), that zone must be muted for audio too. Read the same bitmask — do not duplicate state.

---

## 6. Verbal Feedback — Detailed Design

### 6.1 Primary Mode: Directional Obstacle Warnings

The verbal channel narrates what just happened when a zone crosses into RED (and optionally ORANGE — see §6.4):

**4-zone mode** — 4 clips: N→ "obstacle ahead", E→ "obstacle on the right", S→ "obstacle behind", W→ "obstacle on the left"

**6-zone mode (default)** — 6 clips: AHEAD, TOP_RIGHT→ "obstacle ahead right", BOTTOM_RIGHT→ "obstacle on the right", BEHIND, BOTTOM_LEFT→ "obstacle on the left", TOP_LEFT→ "obstacle ahead left"

**8-zone mode** — 8 clips (compass directions): "obstacle ahead," "obstacle ahead right," "obstacle right," etc.

Keep phrases short — under 800 ms each.

### 6.2 Trigger Rules

Verbal fires only on RED-band entries by default. When a verbal cue fires for a given zone, suppress further verbal cues for that zone for a `VERBAL_COOLDOWN_MS` window (suggest 2000 ms). Tonal still fires during that window.

### 6.3 Alternative / Complementary Verbal Modes

1. **Clear-path mode.** Narrate what's clear ("ahead clear," "right clear"). Positive framing, but can sound chatty in busy environments.
2. **Coaching mode.** RED on one side + clear on opposite → "try left." Downside: requires evaluating all zones; failure modes are dangerous.
3. **Severity escalation.** First ORANGE: tonal only. Second ORANGE within N seconds, or any RED: verbal fires. Reduces verbal fatigue.
4. **Periodic status mode.** Every 15–30 s, speak "all clear" or "obstacles left and right." Less startling but less responsive.
5. **Caregiver-customizable phrases.** Record clips in app or TTS at setup time. Highest implementation cost.
6. **Hybrid: directional warnings for RED, clear-path for sustained ORANGE.** Combines (1) and primary mode.

Recommendation: ship **directional warnings (§6.1)** as v1, add **severity escalation (#3)** as a togglable refinement after field testing.

### 6.4 RED-only vs. ORANGE+RED Verbal

Default: RED only. Leave a `verbalOnOrange` flag in the UI for testing.

### 6.5 Clip Pipeline

Continue using the existing pipeline — `webserver/wav_to_header.py` converts WAVs to PROGMEM arrays in `sounds.h`. New clips follow the same path:

- Record or TTS-generate WAVs at **16-bit PCM, mono, 16 kHz**.
- Drop them into the directory the `wav_to_header.py` points at.
- Regenerate `ESP firmware/MainController/include/sounds.h`.
- Reflash MainController firmware.

PROGMEM budget: 16-bit × 16 kHz × mono × 1 s = 32 KB/s. An 8-zone set with 800 ms clips ≈ 200 KB — fits easily.

---

## 7. Audio Priority / Scheduler

### 7.1 Rules

Single I2S output. No mixing. Priority values:

| Event | Priority |
|---|---|
| Verbal (RED-driven obstacle warning) | 2 (high) |
| Tonal (any band) | 1 (low) |
| Manual navigate (caregiver via NavigationCommandPacket) | 2 (high) |
| Explicit "speak" override from caregiver | 3 (highest) |

Scheduling:

- New event with **higher** priority than currently playing: stop current, start new.
- New event with **equal** priority while channel is busy: drop.
- New event with **lower** priority while channel is busy: drop.
- Channel idle: always play.

Dropping (rather than queueing) is correct because audio is about the *current* situation — a 2-second-old beep tells the child nothing useful.

### 7.2 Implementation Sketch

- A FreeRTOS task (`audioTask`) owns the I2S peripheral on MainController.
- A small queue (length 1 or 2) holds the next `AudioEvent`.
- The task exposes a `stop_flag` that the current playback loop checks each I2S buffer write. A preempt sets `stop_flag = true` and pushes the new event; the task drains I2S DMA with `i2s_zero_dma_buffer()` and starts the new event on the next loop iteration.
- Total preempt latency target: < 50 ms.

### 7.3 Interaction With NavigationCommandPacket

NavigationCommandPacket arrives in `OnDataRecv()` (ESP-NOW ISR context on MainController). Once the FreeRTOS audio task exists, nav commands must enqueue an `AudioEvent` rather than calling `playAudio()` directly — otherwise the ISR context will block. Until Phase 1 is complete, the current synchronous `playAudio()` in the nav handlers is acceptable for testing.

---

## 8. Configuration and UI

### 8.1 Firmware State on MainController

Add three new runtime-state variables on **MainController**, all defaulting to `true`:

- `tonalEnabled` — master switch for threshold tones.
- `verbalEnabled` — master switch for verbal cues.
- `verbalOnOrange` — escalation flag (default `false`; verbal only on RED).

Existing `proximityAudioEnabled` (set from `ConfigPacket.audio_enabled`) remains the top-level master.

Persist these to NVS so they survive reboot. These do **not** need to be in `ConfigPacket` — they are received as part of the standard WebSocket config flow and forwarded to MainController if needed, or held locally if MainController handles all audio decisions autonomously.

### 8.2 WebSocket API Additions (CaregiverApp side)

Extend the existing `{"type":"config", ...}` message with new keys:

```json
{
  "type": "config",
  "audioEnabled": true,
  "tonalEnabled": true,
  "verbalEnabled": true,
  "verbalOnOrange": false,
  "verbalMode": "directional"
}
```

`verbalMode` is a string enum (`"directional"`, `"clearPath"`, `"coaching"`, `"severityEscalation"`, `"periodic"`, `"custom"`).

CaregiverApp forwards relevant audio sub-config to MainController — either by extending `ConfigPacket` further or by adding a new `AudioSubConfigPacket` (MSG_AUDIO_SUBCONFIG = 0xB7, TBD in Phase 4).

Echo the new fields back in `{"type":"status", ...}` responses.

### 8.3 UI Additions

On the Feedback Config page (existing):

- Split the current "Audio" toggle into a three-row group: master Audio, Tonal, Verbal.
- Dropdown for verbal mode (initially only `directional` is wired).
- Checkbox for "verbal on orange" for testing.

---

## 9. Implementation Phasing

Implement in sequence — each phase leaves the firmware buildable, flashable, and useable.

### Phase 0 — Architecture migration ✅ DONE

- Moved I2S amp (MAX98357A) from CaregiverApp to MainController (BCK=27, WS=26, DO=25).
- Added `setupI2S()`, `playAudio()`, and navigation handlers to `MainController/src/main.cpp`.
- Added `sounds.h` stub to `MainController/include/`.
- Added `NavigationCommandPacket` (MSG_NAV_COMMAND = 0xB6) and `NAV_*` action constants to both firmware files.
- CaregiverApp nav handlers now send `NavigationCommandPacket` over ESP-NOW instead of calling `playAudio()` directly.
- Added `volume` field to `ConfigPacket` (now 14 bytes); CaregiverApp sends it on every config change or volume adjust.
- MainController `OnDataRecv()` now dispatches `NavigationCommandPacket` to nav handlers and reads `audio_enabled` + `volume` from `ConfigPacket`.
- **Verify:** flashing both ESPs and tapping a nav button in the caregiver UI causes the MainController to log `Nav: Forward` (etc.) over Serial. No audio yet (sounds.h stubs are empty).

### Phase 1 — Audio task + scheduler refactor

- Move `playAudio()` and I2S writes into a dedicated `audioTask` running on core 1 of MainController.
- Define `AudioEvent { priority, kind (TONE|CLIP), freq_hz?, duration_ms?, clip_data?, clip_len?, clip_rate? }`.
- Add the priority-interrupt scheduler.
- Re-wire nav handlers (`handleGoForward()`, etc.) to enqueue `AudioEvent` instead of calling `playAudio()` directly.
- **Verify:** caregiver-triggered nav audio works, and a second button press can interrupt a playing clip.

### Phase 2 — Tonal threshold engine

- In MainController's proximity update loop (after `computeZoneProximityFromPolar()`), pass `closestMmByZone[]` to a new `AudioTrigger` state machine.
- Implement per-zone band tracking, hysteresis, cooldown.
- Implement sine-wave synthesis helper.
- Wire to the scheduler.
- **Verify:** holding an obstacle at ~2 m produces a YELLOW beep once as it enters range, a single ORANGE beep at ~1.5 m, a RED double-beep at ~0.6 m. Moving away produces no sound. Fluttering at 600 mm produces at most 1 beep per 800 ms.

### Phase 3 — Verbal directional warnings

- Record or TTS-generate the directional phrase set for 4/6/8-zone modes.
- Run `webserver/wav_to_header.py` and update `MainController/include/sounds.h`.
- Map zone index → clip based on current `proximityNumZones`.
- Add verbal cooldown logic (per zone).
- Wire to scheduler at priority 2.
- **Verify:** walking an obstacle around the chair in 6-zone mode produces the correct directional phrase for each sector as it crosses into RED. Phrase does not repeat within the cooldown window.

### Phase 4 — UI + sub-config

- Add `tonalEnabled`, `verbalEnabled`, `verbalOnOrange`, `verbalMode` to WebSocket config on CaregiverApp.
- Forward to MainController (extend ConfigPacket or add AudioSubConfigPacket MSG_AUDIO_SUBCONFIG = 0xB7).
- Persist to NVS on MainController.
- Add the UI controls described in §8.3.
- **Verify:** each toggle mutes/un-mutes its channel; settings persist across reboots.

### Phase 5 — Field tune

- Adjust tone frequencies, durations, and cooldowns based on hearing tests with the actual speaker/enclosure.
- Confirm audio levels are safe (measure at head position; target comfortable speech level).

### Phase 6 (optional) — Alternative verbal modes

- Implement `severityEscalation` first (§6.3 item 3). Adds value without new recordings.
- Then `clearPath` if bandwidth permits.
- Evaluate with users before adding more.

---

## 10. Testing Plan

### Bench tests

- **Nav command test (Phase 0 verify):** tap each nav button in UI; confirm MainController Serial prints the correct `Nav: *` log line. Confirm no audio until real clips are loaded.
- **Tonal chatter test:** sweep an object slowly across each band boundary. Confirm no more than ~1 beep per 800 ms per zone.
- **Multi-zone burst test:** cover all zones simultaneously. Confirm global cooldown prevents more than N beeps in 1 s.
- **Preempt test:** trigger a long verbal clip, then a tone — tone should be suppressed. Trigger a verbal, then another verbal — second preempts.
- **Audio-off test:** toggle `audioEnabled` off via UI; verify total silence during RED events.

### Integration tests

- LED behavior must not regress — tonal engine reads `closestMmByZone[]` read-only; LED ring unaffected.
- WebSocket reconnect: confirm sub-config state re-sent to browser on reconnect.
- Power cycle: verify persisted sub-config restores correctly.
- **ConfigPacket size:** both CaregiverApp and MainController must agree on 14-byte `ConfigPacket`. If either is reflashed without the other, config packets will be silently ignored (size mismatch). Flash both together after any struct change.

### User tests (light)

- Child feedback on whether RED double-beep feels "urgent but not scary."
- Caregiver feedback on whether verbal directionality is understood in 6-zone mode.

---

## 11. Risks and Open Questions

- **I2S blocking during preempt.** `i2s_zero_dma_buffer` can briefly block. Budget worst-case preempt latency during Phase 1; if > 50 ms, switch to I2S driver v2 or reduce `dma_buf_len`.
- **ESP-NOW ISR + playAudio().** Until Phase 1 (FreeRTOS task), `playAudio()` is called from `OnDataRecv()` (ISR context). This works for testing but will cause watchdog issues under heavy traffic. Phase 1 must fix this.
- **ConfigPacket size sync.** Both ESPs must be flashed together whenever ConfigPacket changes. Document this in `RUNNING_THE_SYSTEM.md`.
- **Speaker enclosure coloration.** The 440/700/1000 Hz palette is a starting point — expect to re-tune.
- **Verbal phrase selection for 6-zone.** "Obstacle ahead right" is fine; verify it doesn't confuse children with "top right" ambiguity.
- **Power draw during verbal + motor events.** Verify supply rail doesn't droop enough to brown out the ESP32.
- **Accessibility.** Audio feedback must remain optional — never mandatory. LED ring remains the primary feedback channel.
- **Localization.** Current clip set is English. Keep clip naming language-agnostic (e.g. `warn_ahead_en.wav`) for future localization.

---

## 12. Prompt for a Group Member Using Claude

Paste the following into a fresh Claude session (after opening the Software folder) to get implementation help aligned with this plan. Replace `<PHASE>` with the phase number from §9 you're working on.

---

> I'm working on the **ViviSense** wheelchair obstacle-awareness system with my team. The Software folder is attached (or mounted). Before doing anything else, please read:
>
> - `Software/SYSTEM_OVERVIEW.md`
> - `Software/RUNNING_THE_SYSTEM.md`
> - `Software/AUDIO_FEEDBACK_PLAN.md` — the design doc our team finalized (read the Status line at the top to see what's already done)
> - `Software/ESP firmware/MainController/src/main.cpp` — owns the I2S amp and all audio output
> - `Software/ESP firmware/MainController/include/sounds.h` — audio clip stubs (to be filled with real WAVs)
> - `Software/CaregiverApp/src/main.cpp` — sends NavigationCommandPacket + ConfigPacket to MainController
>
> I'm going to implement **Phase <PHASE>** from `AUDIO_FEEDBACK_PLAN.md`. That phase's "Verify" bullet is my acceptance test.
>
> Ground rules:
>
> 1. **MainController owns all audio.** Do not add audio playback or I2S code to CaregiverApp, LEDRingController, or any sensor pod. CaregiverApp only sends config and navigation commands over ESP-NOW.
> 2. **Do not** change the existing `ConfigPacket`, `NavigationCommandPacket`, `SensorPacket`, or `LedFrame_t` structs without updating **both** sides (CaregiverApp and MainController) in the same diff. Size mismatch causes silent packet drops.
> 3. Audio playback must not block ESP-NOW callbacks — use a FreeRTOS task with a small event queue as described in §7.2 of the plan (Phase 1 delivers this; earlier phases may call playAudio() directly for testing only).
> 4. Prefer small, reviewable diffs. Edit existing files rather than creating new ones unless structure genuinely requires it.
>
> Before you write any code:
>
> 1. Confirm in your own words what Phase <PHASE> requires and what its "Verify" test is.
> 2. Ask any clarifying questions — don't guess.
> 3. Tell me which files you intend to edit and what functions/structs you plan to add or change.
> 4. Flag anything in the plan that looks wrong after reading the actual code.
>
> When I approve, proceed with the smallest change that gets the acceptance test working.

---

## 13. Changelog of This Document

| Date | Change |
|---|---|
| 2026-04-22 | Initial draft. Tonal = threshold-event with per-band frequency. Verbal = directional warnings primary, alternatives documented. Scheduler = priority interrupt, verbal preempts tonal. No ConfigPacket changes. Audio logic on CaregiverApp. |
| 2026-04-23 | Architecture migration: audio engine moved to MainController. CaregiverApp sends NavigationCommandPacket (0xB6) + ConfigPacket with volume field. I2S, setupI2S(), playAudio(), nav handlers, and sounds.h all moved to MainController. Phase 0 added as completed. Claude prompt updated to reflect new ownership. |
