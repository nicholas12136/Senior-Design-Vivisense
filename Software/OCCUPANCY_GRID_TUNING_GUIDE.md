# ViviSense Occupancy Grid Tuning Guide

This guide explains the **occupancy grid parameters** used by `MainController` in simple language first, then in more depth.

It is focused on the **polar occupancy grid** only.

Relevant code:

- `Software/ESP firmware/MainController/src/main.cpp`
- `Software/ESP firmware/MainController/include/runtime_defaults.h`
- `Software/ESP firmware/MainController/include/SensorAndPoint.h`

## 1. Quick Summary

The current obstacle pipeline works like this:

1. Sensor pods send raw distance readings.
2. `MainController` converts valid readings into world-frame points.
3. Each point is placed into a **polar grid cell**:
   - a **ring** for distance
   - a **zone** for angle
4. Each grid cell builds up **confidence** over time.
5. When a cell’s confidence gets high enough, it becomes **occupied**.
6. Occupied cells are converted into:
   - obstacle distances by sector
   - LED output
   - optional presentation-grid output

In practice, the most important tuning knobs are:

- `red_mm`
- `orange_mm`
- `yellow_mm`
- `floor_z_mm`
- `ceiling_z_mm`
- `proximity_ms`
- `polar_rise`
- `polar_decay`
- `polar_enter`
- `polar_exit`
- `zones`
- `sectors_mask`

If you only remember one thing:

- **thresholds** change what counts as red/orange/yellow
- **polar filter params** change how stable or twitchy detection feels
- **proximity_ms** changes how quickly the system updates

## 2. Where The Parameters Live

Boot defaults are in:

- `Software/ESP firmware/MainController/include/runtime_defaults.h`

Live tuning is handled in:

- `Software/ESP firmware/MainController/src/main.cpp`

Serial commands supported by `MainController` include:

- `GET`
- `SET,zones,<4|6|8>`
- `SET,sectors_mask,<0..255>`
- `SET,red_mm,<value>`
- `SET,orange_mm,<value>`
- `SET,yellow_mm,<value>`
- `SET,floor_z_mm,<value>`
- `SET,ceiling_z_mm,<value>`
- `SET,stale_ms,<value>`
- `SET,status_ms,<value>`
- `SET,proximity_ms,<value>`
- `SET,led_mode,<0|1>`
- `SET,polar_rise,<1..255>`
- `SET,polar_decay,<0..255>`
- `SET,polar_enter,<1..255>`
- `SET,polar_exit,<0..254>`

## 3. Simple Explanations

### `red_mm`

How close an obstacle must be before the system treats it as **red / very close**.

Bigger value:

- red starts farther away
- system feels more cautious

Smaller value:

- red starts closer in
- system feels less aggressive

### `orange_mm`

How far the **orange** zone extends.

Bigger value:

- more things show as orange instead of yellow/green

Smaller value:

- orange band becomes narrower

### `yellow_mm`

How far the **yellow** zone extends.

Bigger value:

- farther obstacles still count as warnings

Smaller value:

- system ignores more far-away clutter

### `floor_z_mm`

The minimum world `Z` height a point must have to count for the occupancy grid.

Bigger value:

- ignores more low points
- helps reject floor hits
- can hide very low obstacles

Smaller value:

- includes more low points
- better for seeing low obstacles
- more risk of floor detections

### `ceiling_z_mm`

The maximum world `Z` height a point may have and still count for the occupancy grid.

Bigger value:

- includes taller points
- more useful for walls, people, and upper furniture
- more risk of overhead clutter

Smaller value:

- ignores higher points
- can reduce overhead clutter
- can miss tall but relevant obstacles

### `proximity_ms`

How often the occupancy grid is updated.

Smaller value:

- updates happen faster
- response feels quicker
- more CPU / more outgoing updates

Bigger value:

- updates happen slower
- response feels calmer but less immediate

### `polar_rise`

How quickly confidence increases when a cell is hit.

Bigger value:

- cells turn occupied faster
- more responsive
- can be more sensitive to noise

Smaller value:

- cells need repeated hits before becoming occupied
- more stable, less reactive

### `polar_decay`

How quickly confidence falls when a cell is not hit.

Bigger value:

- occupied cells disappear faster
- less lingering
- can flicker more

Smaller value:

- occupied cells persist longer
- smoother
- can leave “ghost” occupancy behind

### `polar_enter`

Confidence threshold required for an empty cell to become occupied.

Bigger value:

- harder to declare a cell occupied
- more conservative

Smaller value:

- easier to declare occupancy
- more sensitive

### `polar_exit`

Confidence threshold below which an occupied cell becomes empty again.

Bigger value:

- cells clear earlier
- more responsive clearing

Smaller value:

- cells hold longer before clearing
- more persistence

Important:

- `polar_exit` must stay below `polar_enter`
- that gap is what gives you hysteresis and prevents rapid flicker

### `zones`

How many user-facing sectors the occupied grid collapses into:

- `4`
- `6`
- `8`

This does **not** change the internal polar grid resolution.

It changes how the internal occupied bins are grouped for output.

### `sectors_mask`

Which output sectors are active.

If a bit is off:

- that sector is ignored for output
- internal polar bins still exist, but they do not affect final zone output there

### `stale_ms`

How long before a sensor is treated as stale/offline if it stops sending.

Smaller value:

- dead sensors are removed faster

Bigger value:

- stale data can live longer

### `status_ms`

How often status/health packets are emitted.

This is mostly a telemetry/debug parameter, not a core occupancy behavior parameter.

### `led_mode`

This is not an occupancy-grid parameter directly.

It only changes **how occupancy results are displayed**:

- `0` = sector fill
- `1` = radar style

## 4. Deeper Explanation

## 4.1 Point Validity Before The Grid

Before a point ever reaches the occupancy grid, it must pass the point-conversion rules in `SensorAndPoint.h`.

The important one is:

- only `targetStatus == 5` is accepted

That means bad or low-confidence readings do not even enter the grid.

This matters because sometimes people try to fix “missing detections” by changing occupancy parameters, when the real issue is that the raw points are being rejected earlier.

So:

- if a point never gets into the grid, `polar_rise`, `polar_enter`, and friends cannot help

## 4.2 Internal Polar Grid Shape

The internal grid has:

- `12` rings
- `24` angular zones

These defaults come from `runtime_defaults.h`.

Each point is mapped by:

- planar distance -> ring
- angle -> zone
- world `Z` must also fall between `floor_z_mm` and `ceiling_z_mm`

Then that cell receives a hit for the current update.

## 4.3 Confidence And Hysteresis

Each cell has:

- `polarConfidence[idx]`
- `polarOccupied[idx]`

Every update:

- if the cell is hit, confidence goes up by `polar_rise`
- if the cell is not hit, confidence drops by `polar_decay`

Then occupancy is decided by hysteresis:

- if currently empty and confidence >= `polar_enter`, mark occupied
- if currently occupied and confidence <= `polar_exit`, mark empty

This hysteresis is very important.

Without it:

- cells would constantly blink on/off when readings sit near the threshold

## 4.4 Thresholds Versus Filter Parameters

A common tuning mistake is mixing these up.

Thresholds:

- `red_mm`
- `orange_mm`
- `yellow_mm`

These change **how serious** a detected obstacle looks.

Filter parameters:

- `polar_rise`
- `polar_decay`
- `polar_enter`
- `polar_exit`

These change **how stable and responsive** the detection is.

So:

- if the wrong color is showing, adjust thresholds
- if the occupancy flickers or lingers, adjust filter parameters

## 4.5 `proximity_ms` And System Feel

`proximity_ms` changes how often the occupancy update runs.

If you reduce it:

- grid state updates more often
- confidence changes accumulate faster in real time
- the system feels more immediate

If you increase it:

- grid updates are more spread out
- confidence changes happen more slowly in real time

This means timing and filter parameters interact.

Example:

- `polar_rise = 140` at `33 ms`
- feels different from `polar_rise = 140` at `100 ms`

Even though the number is the same, the update cadence is different.

## 5. Default Values

Current defaults from `runtime_defaults.h`:

- `kDefaultRedThresholdMm = 600`
- `kDefaultOrangeThresholdMm = 1050`
- `kDefaultYellowThresholdMm = 1500`
- `kDefaultObstacleFloorZMm = 75`
- `kDefaultObstacleCeilingZMm = 1600`
- `kProximityPeriodMs = 33`
- `kPolarConfRise = 140`
- `kPolarConfDecay = 36`
- `kPolarEnterThreshold = 120`
- `kPolarExitThreshold = 70`
- `kMaxObstacleRangeMm = 3000`
- `kDefaultZoneCount = 6`

These are a reasonable responsive baseline.

## 6. Symptom -> What To Change

This is the most useful section during real tuning.

### Case 1: Detection flickers on and off too much

What it usually means:

- the grid is too sensitive
- cells enter or exit occupancy too easily

Try this:

1. Increase `polar_enter`
2. Decrease `polar_decay`
3. Decrease `polar_exit`
4. If needed, reduce `polar_rise`

Example:

- before:
  - `polar_rise=140`
  - `polar_decay=36`
  - `polar_enter=120`
  - `polar_exit=70`
- try:
  - `polar_rise=120`
  - `polar_decay=24`
  - `polar_enter=140`
  - `polar_exit=60`

Why:

- higher enter threshold means one noisy hit is less likely to turn a cell on
- lower decay means confidence does not swing as violently between frames

### Case 2: Obstacles linger too long after they are gone

What it usually means:

- the grid is too sticky

Try this:

1. Increase `polar_decay`
2. Increase `polar_exit`
3. If still sticky, reduce `polar_rise`

Example:

- before:
  - `polar_decay=36`
  - `polar_exit=70`
- try:
  - `polar_decay=60`
  - `polar_exit=90`

Why:

- confidence drops faster
- occupied cells are allowed to clear sooner

### Case 3: Obstacles appear too slowly

What it usually means:

- confidence is building too slowly
- or the update period is too slow

Try this:

1. Increase `polar_rise`
2. Lower `polar_enter`
3. Lower `proximity_ms`

Example:

- before:
  - `polar_rise=140`
  - `polar_enter=120`
  - `proximity_ms=33`
- try:
  - `polar_rise=180`
  - `polar_enter=100`
  - `proximity_ms=25`

Why:

- cells hit occupancy faster
- updates happen more often

### Case 4: Far-away clutter is too distracting

What it usually means:

- warning distance is too large

Try this:

1. Lower `yellow_mm`
2. If needed, lower `orange_mm`

Example:

- before:
  - `yellow_mm=1500`
  - `orange_mm=1050`
- try:
  - `yellow_mm=1100`
  - `orange_mm=850`

Why:

- far objects stop showing up as warnings

### Case 4b: The floor is being detected as an obstacle

What it usually means:

- low points are still inside the occupancy height window

Try this:

1. Raise `floor_z_mm`
2. Leave the polar confidence settings alone at first
3. Re-test with the chair on flat ground before changing anything else

Example:

- before:
  - `floor_z_mm=75`
- try:
  - `floor_z_mm=125`
  - then `floor_z_mm=175` if floor hits still appear

Why:

- points below the new floor cutoff never reach the occupancy grid

Watch out:

- if you raise it too far, curbs, shoe-level obstacles, and low boxes can disappear too

### Case 4c: Overhead or tall clutter is being detected

What it usually means:

- high points are still inside the occupancy height window

Try this:

1. Lower `ceiling_z_mm`
2. Re-test around door frames, tabletops, and nearby people

Example:

- before:
  - `ceiling_z_mm=1600`
- try:
  - `ceiling_z_mm=1400`

Why:

- points above the new ceiling cutoff stop contributing to occupancy

### Case 5: The chair feels too aggressive and alarms too early

What it usually means:

- danger bands are too wide

Try this:

1. Lower `red_mm`
2. Lower `orange_mm`
3. Possibly lower `yellow_mm`

Example:

- before:
  - `red_mm=600`
  - `orange_mm=1050`
  - `yellow_mm=1500`
- try:
  - `red_mm=450`
  - `orange_mm=800`
  - `yellow_mm=1200`

### Case 6: The chair feels too permissive and should warn earlier

What it usually means:

- thresholds are too small

Try this:

1. Raise `yellow_mm`
2. Raise `orange_mm`
3. Raise `red_mm` if needed

Example:

- before:
  - `red_mm=600`
  - `orange_mm=1050`
  - `yellow_mm=1500`
- try:
  - `red_mm=700`
  - `orange_mm=1200`
  - `yellow_mm=1800`

### Case 7: A sensor disconnect leaves obstacles “stuck”

What it usually means:

- stale timeout is too long

Try this:

1. Lower `stale_ms`

Example:

- before:
  - `stale_ms=400`
- try:
  - `stale_ms=250`

Why:

- the system stops trusting missing sensors sooner

### Case 8: The system is too noisy in a busy environment

What it usually means:

- the filter is too eager
- too many bins are becoming occupied from transient hits

Try this:

1. Raise `polar_enter`
2. Lower `polar_rise`
3. Lower `yellow_mm`
4. Increase `zones` only if you want more directional detail, not less noise

### Case 9: The system is stable, but directional output is too coarse

What it usually means:

- your output sectors are too large

Try this:

1. Increase `zones` from `4` to `6`, or `6` to `8`

Why:

- internal polar bins stay the same
- but final output is split into finer directions

### Case 10: A direction should be ignored entirely

What it usually means:

- you want to disable one or more output sectors

Try this:

1. Change `sectors_mask`

Example:

- if you only want front-facing sectors active, disable the rear-facing bits

## 7. Good Safe Starting Strategies

If you do not know where to start, use one of these approaches.

### Stable Demo Setup

Use when:

- you want calm, presentation-friendly behavior

Try:

- `proximity_ms=50`
- `polar_rise=120`
- `polar_decay=24`
- `polar_enter=140`
- `polar_exit=60`

Effect:

- smoother
- less flicker
- slightly slower response

### Fast Reactive Setup

Use when:

- you want the system to react quickly

Try:

- `proximity_ms=25`
- `polar_rise=180`
- `polar_decay=40`
- `polar_enter=100`
- `polar_exit=75`

Effect:

- faster occupancy changes
- more immediate feel
- more risk of twitchiness

### Clutter Reduction Setup

Use when:

- the environment has many harmless far objects

Try:

- `yellow_mm=1000`
- `orange_mm=750`
- `red_mm=450`
- `polar_enter=140`

Effect:

- system focuses more on nearby threats

## 8. Example Serial Commands

Check current settings:

```text
GET
```

Make the system more stable:

```text
SET,polar_enter,140
SET,polar_exit,60
SET,polar_decay,24
SET,polar_rise,120
```

Make the system react faster:

```text
SET,polar_rise,180
SET,polar_enter,100
SET,proximity_ms,25
```

Reduce far-away warnings:

```text
SET,yellow_mm,1100
SET,orange_mm,850
SET,red_mm,500
```

Reject more floor points:

```text
SET,floor_z_mm,125
```

Reject more overhead points:

```text
SET,ceiling_z_mm,1400
```

Tighten stale-sensor behavior:

```text
SET,stale_ms,250
```

Switch to 8 output sectors:

```text
SET,zones,8
```

Disable some sectors:

```text
SET,sectors_mask,63
```

## 9. Tuning Order That Usually Works Best

Do tuning in this order:

1. Confirm valid points are reaching the grid.
2. Set `zones` and `sectors_mask`.
3. Set `red_mm`, `orange_mm`, `yellow_mm`.
4. Set `floor_z_mm` and `ceiling_z_mm`.
5. Set `proximity_ms`.
6. Tune `polar_rise`, `polar_decay`, `polar_enter`, `polar_exit`.
7. Fine-tune `stale_ms` if sensors drop out.

Why this order works:

- first define what directions matter
- then define what “near” means
- then define how fast and stable the grid should be

## 10. Common Mistakes

### Mistake: Using thresholds to fix flicker

Thresholds do not fix flicker well.

Use:

- `polar_enter`
- `polar_exit`
- `polar_rise`
- `polar_decay`

### Mistake: Using filter values to fix color severity

Filter values affect stability.

Use:

- `red_mm`
- `orange_mm`
- `yellow_mm`

### Mistake: Lowering `proximity_ms` too much without retuning filter values

If updates happen more often, the same rise/decay numbers behave differently in real time.

### Mistake: Forgetting that stale sensors can hold bad perception

If a sensor drops or pauses, `stale_ms` matters.

## 11. Short Troubleshooting Table

Problem: flicker

- raise `polar_enter`
- lower `polar_exit`
- lower `polar_decay`

Problem: ghost obstacles

- raise `polar_decay`
- raise `polar_exit`

Problem: too slow

- raise `polar_rise`
- lower `polar_enter`
- lower `proximity_ms`

Problem: too many far warnings

- lower `yellow_mm`

Problem: floor detections

- raise `floor_z_mm`

Problem: overhead detections

- lower `ceiling_z_mm`

Problem: alerts too aggressive

- lower `red_mm`, `orange_mm`, `yellow_mm`

Problem: alerts too permissive

- raise `red_mm`, `orange_mm`, `yellow_mm`

Problem: stale sensors hang around

- lower `stale_ms`

## 12. Final Recommendation

If you are doing occupancy-grid tuning live, change only **one family of parameters at a time**:

- severity family:
  - `red_mm`, `orange_mm`, `yellow_mm`
- height window family:
  - `floor_z_mm`, `ceiling_z_mm`
- filter family:
  - `polar_rise`, `polar_decay`, `polar_enter`, `polar_exit`
- timing family:
  - `proximity_ms`, `stale_ms`

That makes it much easier to understand cause and effect.

If you change all of them at once, it becomes very hard to know which adjustment actually fixed the problem.
