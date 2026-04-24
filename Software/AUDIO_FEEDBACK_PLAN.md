# ViviSense Audio Feedback Implementation Plan

**Status:** design plan, no code changes yet
**Scope:** define how tonal and verbal audio feedback should be added to the existing ViviSense firmware (CaregiverApp ESP32 + MAX98357A I2S amp), so both auditory channels coexist cleanly with the LED ring and the caregiver WebSocket API.
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

Everything is gated by the existing `audioEnabled` flag, with new sub-flags (`tonalEnabled`, `verbalEnabled`, verbal-mode selector) surfaced through the existing WebSocket config channel.

---

## 2. Hardware Placement (Confirm Before Coding)

The MAX98357A + speaker must be physically mounted on the wheelchair, close enough to the child to be audible at low volume in a busy indoor environment. This matches the current architecture — `CaregiverApp` already owns the I2S pins (BCK=27, WS=26, DO=25) and hosts the WiFi AP the caregiver's phone connects to, so the "CaregiverApp" ESP32 is really the chair-side network / audio brain. Name is misleading but the wiring is already correct.

Actions to confirm on hardware before firmware work begins:

- Speaker driver is mounted in the chair enclosure (not the caregiver's device).
- MAX98357A `GAIN` pad is set appropriately — recommend 12 dB (pin floating) or 9 dB (100 kΩ to GND). Too high will clip on verbal phrases.
- Enclosure has a port or mesh aimed toward the child's head position, not the floor.
- Power rail can sustain peak current (~500 mA for short verbal phrases at moderate volume).

---

## 3. Where Audio Logic Lives

The audio engine belongs on **CaregiverApp** (the chair-side ESP32 with the I2S amp). That device already:

- Receives `ZoneProximityPacket` over ESP-NOW (it has a handler branch in `onEspNowReceived`, currently just updating a timestamp).
- Owns the `audioEnabled` flag in `ConfigPacket`.
- Hosts the WebSocket server, which is where the caregiver UI will send audio-sub-config.
- Has the I2S driver and `playAudio()` primitive already written.

No new ESP32 is needed. No changes are required to MainController or LEDRingController firmware. MainController keeps broadcasting zone proximity; CaregiverApp simply starts *using* it for audio decisions in addition to ignoring it (as it does today).

---

## 4. System Diagram (Audio Path)

```
MainController                        CaregiverApp (chair-mounted)
  computes per-zone nearest obstacle
  -> ZoneProximityPacket (broadcast, ~15 Hz)
                                      onEspNowReceived()
                                        │
                                        ▼
                                      AudioTrigger state machine
                                        - per-zone band tracking
                                        - hysteresis + cooldown
                                        - decides: no-op / tone(freq) / verbal(clipId)
                                        │
                                        ▼
                                      AudioScheduler (priority interrupt)
                                        - verbal preempts tonal
                                        - busy-while-verbal → drop new tones
                                        - publishes AudioEvent to FreeRTOS queue
                                        │
                                        ▼
                                      AudioTask (FreeRTOS task on core 1)
                                        - dequeues events
                                        - for tones: synthesize sine in RAM → i2s_write
                                        - for verbal: stream PROGMEM PCM → i2s_write
                                        - honors volume, stop-flag for preempt
                                        │
                                        ▼
                                      I2S_NUM_0 → MAX98357A → speaker
```

The current `playAudio()` is synchronous and blocks the caller with `portMAX_DELAY`. A dedicated FreeRTOS task is required so WebSocket handling, ESP-NOW callbacks, and audio playback do not stall each other.

---

## 5. Tonal Feedback — Detailed Design

### 5.1 Trigger Model

Threshold events, **not** continuous. A tone fires when a zone's closest distance **crosses into a closer band** than it was previously in. Crossing outward is silent (the LED handles the visual cue for "you're now safe").

Re-using the existing distance bands from `processZoneProximity()`:

| Band | Distance range (mm) | LED color | Tone |
|---|---|---|---|
| RED    | < DIST_RING2 (default 600 mm)       | Red      | High urgency |
| ORANGE | DIST_RING2 to DIST_RING4 (600–1500) | Orange   | Medium urgency |
| YELLOW | DIST_RING4 to 3000                  | Yellow   | Low urgency |
| CLEAR  | ≥ 3000 mm                           | Off      | (silent) |

Rings 1 / 3 / 5 (the lighter rings within each color) are not tonally distinct — a single tone per color band keeps the sound palette small enough for a child to learn. The LED ring continues to convey the finer-grained distance.

### 5.2 Frequency Mapping (Proposed)

Pick tones that are distinguishable but all within the speaker's comfortable range. Starting suggestion, tuneable during testing:

| Band   | Frequency | Duration | Rationale |
|---|---|---|---|
| YELLOW | 440 Hz (A4)   | 120 ms   | Calm, clearly audible, unobtrusive |
| ORANGE | 700 Hz        | 150 ms   | Brighter, more attention-getting |
| RED    | 1000 Hz (C6)  | 180 ms, repeat once after 100 ms gap | Double-beep conveys urgency without panic |

Waveform: pure sine, with a 10 ms linear fade-in and 20 ms fade-out to avoid the "click" artifact from starting mid-cycle (important on a small speaker).

### 5.3 State Machine (Per Zone)

For each of the 4/6/8 zones:

```
state = { last_band: CLEAR, last_event_ms: 0, pending_band: CLEAR, pending_since_ms: 0 }

on ZoneProximityPacket:
  new_band = band_of(closest_mm[z])

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

- `HYSTERESIS_MS = 150` — zone must stay in the new band for ~2 packet intervals before a transition counts.
- `COOLDOWN_MS = 800` per zone — prevents chatter when something hovers near a boundary.
- A global **all-zones cooldown** of ~300 ms prevents a burst of tones when many zones change at once (e.g. pushing through a doorway).

### 5.4 Synthesis vs. Pre-Baked

Recommend **runtime sine synthesis** over pre-baking tones into `sounds.h`:

- No PROGMEM cost.
- Frequency can be changed by config without reflashing.
- A 120 ms, 16 kHz, 16-bit mono tone is 3840 bytes of RAM — trivial.

Implementation sketch (pseudocode for the AudioTask, no real code yet):

```
fill_sine_buffer(freq_hz, duration_ms, buf):
  n = duration_ms * SAMPLE_RATE / 1000
  for i in 0..n-1:
    env = envelope(i, n, fade_in_ms=10, fade_out_ms=20)   # 0..1
    buf[i] = int16(AMPLITUDE * env * sin(2π * freq_hz * i / SAMPLE_RATE))
  return n samples
```

Then stream `buf` to I2S exactly the way the current `playAudio()` does.

### 5.5 Active-Sector Interaction

If the caregiver has a zone disabled via `currentActiveSectors[]`, that zone must be muted for audio too (both tonal and verbal). Reuse the existing array — do not duplicate state.

---

## 6. Verbal Feedback — Detailed Design

### 6.1 Primary Mode: Directional Obstacle Warnings

The verbal channel narrates what just happened when a zone crosses into RED (and optionally ORANGE — see §6.4). The phrase is selected by zone index, using the existing zone labels:

**4-zone mode** — 4 clips:
- N → "obstacle ahead"
- E → "obstacle on the right"
- S → "obstacle behind"
- W → "obstacle on the left"

**6-zone mode (default)** — 6 clips:
- AHEAD, TOP_RIGHT → "obstacle ahead right", BOTTOM_RIGHT → "obstacle on the right", BEHIND, BOTTOM_LEFT → "obstacle on the left", TOP_LEFT → "obstacle ahead left"

**8-zone mode** — 8 clips (N / NE / E / SE / S / SW / W / NW), spoken as compass-style cues ("obstacle ahead," "obstacle ahead right," "obstacle right," …).

Keep phrases short — under 800 ms each. Children tune out long phrases in motion.

### 6.2 Trigger Rules

Verbal fires only on RED-band entries by default (the "this matters" threshold). When a verbal cue fires for a given zone, suppress further verbal cues for that zone for a `VERBAL_COOLDOWN_MS` window (suggest 2000 ms) even if the zone re-crosses RED. Tonal still fires during that window — you still get a beep for each real event.

### 6.3 Alternative / Complementary Verbal Modes (Team to Evaluate)

You asked for alternatives. Each of these can either replace the directional-warning mode or coexist as a user-selectable setting:

1. **Clear-path mode.** Instead of narrating obstacles, narrate what's clear: "ahead clear," "right clear." Reinforces successful navigation rather than only flagging failures, and children often respond better to positive framing. Downside: during busy environments it can sound chatty.

2. **Coaching mode.** When a RED event is detected on one side, speak guidance toward the clear side: a RED on TOP_RIGHT while BOTTOM_LEFT is clear yields "try left." Combines warning + suggested action. Downside: requires evaluating *all* zones to pick a direction, and the "clear" direction must be truly clear — failure modes are bad (sending a child toward another obstacle).

3. **Severity escalation.** First ORANGE entry in a zone: tonal only. Second ORANGE within N seconds, or any RED: verbal fires. Reduces verbal fatigue and treats verbal as a "you aren't reacting" escalation. Works well with the tonal system already carrying the first-order alerting load.

4. **Periodic status mode.** No event-driven verbal at all — instead, every 15–30 s, speak a short status: "all clear" or "obstacles left and right." Less startling, but less responsive.

5. **Caregiver-customizable phrases.** Let the caregiver record short clips in the app (or type phrases into a TTS at setup time, rendered on a server and uploaded to SPIFFS) and map each clip to a zone/band. Personalization can help kids tune in to a familiar voice. Highest implementation cost.

6. **Hybrid: directional warnings for RED, clear-path for long-running ORANGE.** Combines (1) and the primary mode — verbal warns when something gets close, but if an obstacle has been sustained in the ORANGE band for more than ~4 s, speak a suggested clear direction instead of just re-warning.

Recommendation: ship **directional warnings (§6.1)** as the v1, and add **severity escalation (#3)** as a togglable refinement once the team has field-tested v1. Defer customizable phrases (#5) unless a user study explicitly calls for it.

### 6.4 RED-only vs. ORANGE+RED Verbal

Default: RED only, because verbal on ORANGE will feel spammy during normal navigation. Leave an `verbalOnOrange` flag in the UI for testing — easy to add, easy to disable.

### 6.5 Clip Pipeline

Continue using the existing pipeline — there is already a `webserver/wav_to_header.py` that turns WAVs into PROGMEM byte arrays in `sounds.h`. New clips follow the same path:

- Record or TTS-generate WAVs at **16-bit PCM, mono, 16 kHz** (matches `playAudio()`'s `i2s_set_sample_rate(16000)`).
- Name each WAV with a clear short identifier, e.g. `obstacle_ahead.wav`, `obstacle_ahead_right.wav`.
- Drop them into whichever directory the existing `wav_to_header.py` points at.
- Regenerate `include/sounds.h`. The new header will expose arrays like `obstacle_ahead_data[]`, `obstacle_ahead_len`, `obstacle_ahead_rate`.
- Reflash filesystem + firmware.

PROGMEM budget check: 16-bit × 16 kHz × mono × 1 s = 32 KB per second. An 8-zone directional set with 800 ms clips is ~200 KB total, which fits easily on ESP32 flash alongside the SPIFFS UI — but run a `pio run` size report once the clips are in to confirm.

---

## 7. Audio Priority / Scheduler

### 7.1 Rules

Single I2S output. No mixing. Priority values:

| Event | Priority |
|---|---|
| Verbal (RED-driven obstacle warning) | 2 (high) |
| Tonal (any band) | 1 (low) |
| Manual navigate (caregiver-triggered, e.g. forward/left/right/stop from UI) | 2 (high) |
| Explicit "speak" override from caregiver | 3 (highest) |

Scheduling:

- New event with **higher** priority than the currently playing audio: stop current, start new.
- New event with **equal** priority while channel is busy: drop (don't queue).
- New event with **lower** priority while channel is busy: drop.
- Channel idle: always play.

Dropping (rather than queueing) is correct here because:
- Audio is about the *current* situation; a 2-second-old beep tells the child nothing useful.
- Queueing verbal phrases stacks quickly in busy environments and produces a confusing "voice-over" effect.

### 7.2 Implementation Sketch

- A FreeRTOS task (`audioTask`) owns the I2S peripheral.
- A small queue (length 1 or 2) holds the next `AudioEvent`.
- The task exposes a `stop_flag` that the current playback loop checks each I2S buffer write. A preempt sets `stop_flag = true` and pushes the new event; the task drains I2S DMA with `i2s_zero_dma_buffer()` and starts the new event on the next loop iteration.
- Total preempt latency target: < 50 ms.

### 7.3 Interaction With Manual Navigate

The caregiver's existing `{"type":"navigate", "action":"..."}` messages already call `playAudio()` directly on the WebSocket handler task. These calls must route through the new scheduler so they participate in the priority system. Once that's done, the caregiver's "stop" command can actually interrupt an in-progress tonal beep, which is the behavior we want.

---

## 8. Configuration and UI

### 8.1 Firmware State

Add three new runtime-state variables on CaregiverApp, all defaulting to `true`:

- `tonalEnabled` — master switch for threshold tones.
- `verbalEnabled` — master switch for verbal cues.
- `verbalOnOrange` — escalation flag (default `false`; verbal only on RED).

Existing `audioEnabled` remains the top-level master (both tonal and verbal are muted if this is off).

Persist these to NVS so they survive reboot. None of these need to be in `ConfigPacket` (which goes over ESP-NOW to MainController/LEDRingController) — the audio decisions happen entirely on CaregiverApp, so keep the sub-config local-only.

### 8.2 WebSocket API Additions

Extend the existing `{"type":"config", ...}` message with the new keys:

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

`verbalMode` is a string enum (`"directional"`, `"clearPath"`, `"coaching"`, `"severityEscalation"`, `"periodic"`, `"custom"`) — string rather than int because it's human-readable in logs and trivially extensible. Reject unknown values on the firmware side.

Echo the new fields back in `{"type":"status", ...}` responses.

### 8.3 UI Additions

On the Feedback Config page (existing):

- Split the current "Audio" toggle into a three-row group: master Audio, Tonal, Verbal.
- Dropdown for verbal mode (initially only `directional` is wired; other modes become options as they're implemented).
- Checkbox for "verbal on orange" for testing.

No changes to the LED preview or zone-selection UI.

---

## 9. Implementation Phasing

Implement in sequence — each phase should leave the firmware buildable, flashable, and useable. Do not skip ahead.

### Phase 1 — Audio task + scheduler refactor

- Move `playAudio()` and I2S writes into a dedicated `audioTask` running on core 1.
- Define `AudioEvent { priority, kind (TONE|CLIP), freq_hz?, duration_ms?, clip_data?, clip_len?, clip_rate? }`.
- Add the priority-interrupt scheduler.
- Re-wire the existing `handleGoForward()` / `handleTurnLeft()` / etc. to enqueue `AudioEvent` instead of calling `playAudio()` directly.
- **Verify:** caregiver-triggered audio still works exactly as before, and now a second button press can interrupt a playing clip.

### Phase 2 — Tonal threshold engine

- Process `ZoneProximityPacket` in `onEspNowReceived()` beyond just updating timestamps — pass it to the new `AudioTrigger` state machine.
- Implement per-zone band tracking, hysteresis, cooldown.
- Implement sine-wave synthesis helper.
- Wire to the scheduler.
- **Verify:** holding an obstacle at ~2 m produces a YELLOW beep once as it enters range, a single ORANGE beep at ~1.5 m, a RED double-beep at ~0.6 m. Moving away produces no sound. Fluttering at 600 mm produces at most 1 beep per 800 ms.

### Phase 3 — Verbal directional warnings

- Record or TTS-generate the directional phrase set for 4/6/8-zone modes.
- Regenerate `sounds.h`.
- Map zone index → clip based on current `zoneMode`.
- Add verbal cooldown logic (per zone).
- Wire to scheduler at priority 2.
- **Verify:** walking an obstacle around the chair in 6-zone mode produces the correct directional phrase for each sector as it crosses into RED. Phrase does not repeat within the cooldown window even if the zone re-crosses.

### Phase 4 — UI + sub-config

- Add `tonalEnabled`, `verbalEnabled`, `verbalOnOrange`, `verbalMode` to config JSON schema.
- Persist to NVS.
- Add the UI controls described in §8.3.
- **Verify:** each toggle actually mutes / un-mutes its respective channel; settings persist across reboots.

### Phase 5 — Field tune

- Adjust tone frequencies, durations, and cooldowns based on hearing tests with the actual speaker/enclosure.
- Confirm audio levels are safe (recommend measuring at the head position — target a comfortable speech level, not above).

### Phase 6 (optional) — Alternative verbal modes

- Implement `severityEscalation` first (§6.3 item 3). Adds value without needing new recordings.
- Then `clearPath` if bandwidth permits (requires a new clip set).
- Evaluate with users before adding more.

---

## 10. Testing Plan

### Bench tests

- **Tonal chatter test:** sweep an object slowly across each band boundary. Confirm no more than ~1 beep per 800 ms per zone.
- **Multi-zone burst test:** cover all zones simultaneously (walk into a corner). Confirm the global cooldown prevents more than N beeps in 1 s (pick N during tuning).
- **Preempt test:** trigger a long verbal clip, then trigger a tone — tone should be suppressed. Trigger a verbal clip, then trigger another verbal clip — second one preempts.
- **Audio-off test:** toggle `audioEnabled` off via UI; verify total silence even during strong RED events. Toggle `tonalEnabled` off; verify verbals still fire and vice versa.

### Integration tests

- LED behavior must not regress — the tonal engine is read-only with respect to `ZoneProximityPacket`, so the LED ring should be unaffected, but verify.
- WebSocket reconnect: confirm sub-config state is re-sent to the browser on reconnect.
- Power cycle: verify persisted sub-config restores correctly.

### User tests (light)

- Get child feedback on whether the RED double-beep feels "urgent but not scary." Tune duration and gap if it feels alarming.
- Get caregiver feedback on whether verbal directionality is understood — confusion between "ahead right" and "right" in 6-zone mode is a known risk.

---

## 11. Risks and Open Questions

- **I2S blocking during preempt.** `i2s_zero_dma_buffer` can briefly block. Budget the worst-case preempt latency during Phase 1 testing; if > 50 ms, switch to the newer `I2S_driver_v2` or reduce `dma_buf_len`.
- **Speaker enclosure coloration.** A small enclosure will emphasize certain frequencies. The proposed 440/700/1000 Hz palette is a starting point — expect to re-tune.
- **Verbal phrase selection for 6-zone.** The 6-zone labels mix "top" and "bottom" terminology that doesn't translate cleanly to spoken directions. "Obstacle ahead right" is fine, but verify it doesn't confuse children with "top right" ambiguity.
- **Power draw during verbal + motor events.** If the chair has any other high-current peripherals, verify the supply rail doesn't droop enough to brown out the ESP32 during a loud verbal clip.
- **Accessibility.** Consider that some users may be hearing-impaired. Audio feedback should remain optional — never mandatory for safe use of the system. LED ring remains the primary feedback channel.
- **Localization.** Current clip set is English. If other languages are needed later, the wav_to_header pipeline and clip naming should stay language-agnostic (e.g. `warn_ahead_en.wav`).

---

## 12. Prompt for a Group Member Using Claude

Paste the following into a fresh Claude session (after uploading or mounting the Software folder) to get implementation help aligned with this plan. Replace `<PHASE>` with the phase number from §9 you're working on.

---

> I'm working on the **ViviSense** wheelchair obstacle-awareness system with my team. The Software folder is attached (or mounted). Before doing anything else, please read:
>
> - `Software/SYSTEM_OVERVIEW.md`
> - `Software/RUNNING_THE_SYSTEM.md`
> - `Software/AUDIO_FEEDBACK_PLAN.md` — the design doc our team just finalized
> - `Software/CaregiverApp/src/main.cpp` — current audio/I2S + WebSocket code
> - `Software/CaregiverApp/include/sounds.h` — stubbed audio clip data
>
> I'm going to implement **Phase <PHASE>** from `AUDIO_FEEDBACK_PLAN.md`. That phase's "Verify" bullet is my acceptance test — when I can demonstrate that bullet on real hardware, the phase is done.
>
> Ground rules:
>
> 1. **Do not** modify firmware for `MainController`, `LEDRingController`, `LeftPodSender`, `RightPodSender`, or `BaseSender`. The LED and sensor pipeline is stable and I don't want regressions. If you think a change outside CaregiverApp is required, stop and ask first.
> 2. **Do not** change the existing `ZoneProximityPacket`, `ConfigPacket`, `SensorPacket`, or `LedFrame_t` structs — other devices deserialize them by exact size.
> 3. The audio sub-config (`tonalEnabled`, `verbalEnabled`, `verbalOnOrange`, `verbalMode`) lives **locally on CaregiverApp only**. It does not go in `ConfigPacket`.
> 4. Audio playback must not block the WebSocket or ESP-NOW callbacks — use a FreeRTOS task with a small event queue as described in §7.2 of the plan.
> 5. Prefer small, reviewable diffs. Prefer editing existing files (`CaregiverApp/src/main.cpp`, `CaregiverApp/include/sounds.h`, `CaregiverApp/ui/src/...`) over creating new ones, unless a new file genuinely improves structure.
>
> Before you write any code:
>
> 1. Confirm in your own words what Phase <PHASE> requires and what its "Verify" test is.
> 2. Ask me any clarifying questions about ambiguous parts of the plan — don't guess.
> 3. Tell me which files you intend to edit and what functions/structs you plan to add or change.
> 4. Flag anything in the plan that looks wrong to you after reading the actual code. The plan is a guide, not gospel.
>
> When I approve, proceed with the smallest change that gets the acceptance test working. Add short comments near any new state explaining what invariant it maintains (e.g., "per-zone band from last confirmed ZoneProximityPacket; used for threshold-event detection"). Do not touch unrelated code.

---

## 13. Changelog of This Document

| Date | Change |
|---|---|
| 2026-04-22 | Initial draft. Tonal = threshold-event with per-band frequency. Verbal = directional warnings primary, alternatives documented. Scheduler = priority interrupt, verbal preempts tonal. No ConfigPacket changes. |
