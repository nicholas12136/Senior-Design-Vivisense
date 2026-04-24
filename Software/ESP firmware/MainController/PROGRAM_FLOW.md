# MainController Program Flow (Deep Dive)

This document explains how `Software/ESP firmware/MainController/src/main.cpp` and
`Software/ESP firmware/MainController/include/SensorAndPoint.h` work together at runtime.

It is written for debugging and tuning: where data comes from, how it is transformed, how
filtering works, and how output packets are generated.

## 1. What MainController does

MainController is the real-time bridge between sensor pods and feedback devices.

It:

1. Receives ESP-NOW sensor packets from multiple pods.
2. Converts raw range cells to world-frame XYZ points.
3. Computes obstacle proximity by zone (direct / cartesian grid / polar grid).
4. Broadcasts zone distances to LEDRingController.
5. Broadcasts health/status to CaregiverApp.
6. Streams serial telemetry for desktop viewers/debug tools.

## 2. Runtime architecture at a glance

```
Sensor Pods (ESP-NOW SensorPacket)
    -> OnDataRecv() callback
       -> store latest sensor frame and mark sensor pending
main loop
    -> processPendingSensors()
       -> computeLatestWorldPoints()
       -> emit P/E serial lines
    -> emitReceiverStatusIfDue()   (S lines + component status)
    -> broadcastZoneProximity()    (~15 Hz, configurable)
       -> direct OR cartesian OR polar path
       -> ZoneProximityPacket broadcast
```

## 3. Core files and responsibilities

- `src/main.cpp`
  - packet handling
  - state machines
  - occupancy/polar filtering
  - zone distance computation
  - ESP-NOW output + serial command interface
- `include/SensorAndPoint.h`
  - math primitives (Vec3/Mat3)
  - sensor pose transforms
  - cell ray generation
  - conversion from raw cell distance to world point

## 4. Coordinate conventions

World frame:

- `+X`: forward
- `+Y`: left
- `+Z`: up

Angle for zone assignment:

- `angleDeg = atan2(-y, x) * 180/pi`
- `0 deg` forward
- `+90 deg` right
- `-90 deg` left
- `+/-180 deg` behind

This sign convention is used consistently in zone-index mapping.

## 5. Data structures that matter

Main state arrays (per sensor):

- `sensorSeen[]`: alive/offline state
- `packetCountBySensor[]`: packets received
- `lastSensorTimestampMs[]`: timestamp from sender packet
- `sensorPendingProcess[]`: callback-to-loop handoff flag
- `lastRxMsBySensor[]`: local last receive time (for stale timeout)
- `rxHzBySensor[]`: estimated packet frequency

Point conversion cache:

- `Sensor::latestWorldPoints[16]`: latest converted points for each pod.

Filter state:

- Cartesian:
  - `occConfidence[]`
  - `occOccupied[]`
- Polar:
  - `polarConfidence[]`
  - `polarOccupied[]`

## 6. Startup flow (`setup()`)

Startup sequence:

1. `Serial.begin(...)`
2. `configureSensors()`
   - loads hardcoded sensor pose table (`SENSOR_CONFIGS`)
3. Wi-Fi station mode setup
4. ESP-NOW init + channel selection
5. print current detection mode
6. emit current tuning config (`CFG,...`)
7. register receive callback (`esp_now_register_recv_cb(OnDataRecv)`)
8. register broadcast peer (FF:FF:FF:FF:FF:FF)

If ESP-NOW init fails, setup prints failure and returns.

## 7. Main loop flow (`loop()`)

Order is important:

1. `serviceSerialCommands()`
2. `invalidateStaleSensors()`
3. `processPendingSensors()`
4. `emitReceiverStatusIfDue()`
5. if visual enabled and peer added:
   - periodic `broadcastZoneProximity()`
6. `delay(1)`

This keeps callback work minimal and does heavy processing in loop context.

## 8. Receive callback flow (`OnDataRecv`)

`OnDataRecv(...)` handles three packet categories:

1. `DetectionModePacket` (`MSG_DETECTION_MODE`)
   - updates mode
   - clears occupancy + polar filter state
2. `ConfigPacket` (`MSG_CONFIG`)
   - updates zones/brightness/visual/active sectors and thresholds
   - clears filters if visual disabled
3. `SensorPacket`
   - writes raw distances/status/targets into sensor object
   - updates sensor health counters/timestamps
   - sets `sensorPendingProcess[idx] = 1`

Callback does not do expensive point conversion for every packet. It just stores data and flags work.

## 9. Raw-to-point conversion path

`processPendingSensors()`:

1. scans sensors for `sensorPendingProcess[i]`
2. runs `convertPacketToPoints(i)`
3. emits serial point frame (`emitPointsForSensor(i)`)

`convertPacketToPoints(i)` calls:

- `Sensor::computeLatestWorldPoints()`
  - loops all 16 cells
  - each cell uses `createWorldPointFromCell(cell)`

Inside `createWorldPointFromCell(...)`:

1. reject invalid cell index
2. copy raw `distance/status/numTargets`
3. **validity gate**: only `targetStatus == 5` is accepted
4. reject distance <= minimum
5. convert to sensor-frame vector by unit ray * distance
6. transform to world frame:
   - `p_world = R * p_sensor + t`
7. set `point.isValid = true`

Rotation convention used to build `R`:

- `R = Rz(gamma) * Ry(beta) * Rx(alpha)`

## 10. Serial telemetry lines

Main serial line types:

1. `P,sid,cell,valid,x_m,y_m,z_m,targetStatus,ts_ms`
2. `E,sid,ts_ms` (end of one sensor frame)
3. `S,sid,pkts,rx_hz`
4. `DL,...` (zone debug snapshot)
5. `DP,...` (polar confidence bins, polar mode)
6. `DO,...` (polar occupied bins, polar mode)
7. `CFG,...` / `ACK,...` / `ERR,...` (serial config interface)

Note: conversion-latency fields were intentionally removed from `S` to keep runtime output focused on functional state.

## 11. Stale sensor handling

`invalidateStaleSensors()`:

1. compute age from `millis() - lastRxMsBySensor[i]`
2. if age exceeds `sensorStaleTimeoutMs`
   - mark sensor offline
   - clear pending flag
   - zero `rxHz`
   - print stale log

This prevents old points from contributing after link loss.

## 12. Proximity computation modes

Main mode switch happens in `broadcastZoneProximity()`.

### Mode 0: Direct

- iterate valid latest points directly
- assign point to zone
- keep minimum planar distance per zone

### Mode 1: Cartesian grid

1. `accumulateGridHits(...)`:
   - map valid points to occupancy cell index
2. `updateOccupancyGrid(...)`:
   - confidence rise/decay
   - hysteresis enter/exit thresholds
3. `computeZoneProximityFromGrid(...)`:
   - scan occupied cells, map each to zone
   - keep nearest distance per zone

### Mode 2: Polar grid

1. `accumulatePolarHits(...)`:
   - map valid points to `(ring, zone)` bin
2. `updatePolarGrid(...)`:
   - confidence rise/decay + hysteresis
3. `computeZoneProximityFromPolar(...)`:
   - for each zone, first occupied ring => representative distance

## 13. Zone and sector semantics

- `proximityNumZones`: 4, 6, or 8
- `proximityActiveSectors`: bitmask of enabled zones
- output array is always length 8, but only first `num_zones` active

Disabled/inactive zone output is set to `1e9` (effectively no obstacle).

## 14. Distance rings and thresholds

`applyProximityThresholds(red, yellow)` derives five ring boundaries:

1. `0.5 * red`
2. `red`
3. midpoint of red..yellow
4. `yellow`
5. `yellow + 0.5 * (yellow - red)`

Used for:

- ring index selection
- representative ring distance in polar mode
- LED-facing proximity semantics downstream

## 15. Outbound packets

### ZoneProximityPacket (`MSG_ZONE_PROXIMITY`)

- broadcast about every `proximityPeriodMs` (default 67 ms)
- fields:
  - `num_zones`
  - `closest_mm[8]`

### LedRenderFramePacket (`MSG_LED_RENDER_FRAME`)

- broadcast alongside `ZoneProximityPacket`
- carries pre-rendered ring/zone color intent for LEDRingController
- supports:
  - `render_mode=0` sector fill
  - `render_mode=1` radar-style sparse ring hits

### ComponentStatusPacket (`MSG_COMPONENT_STATUS`)

- emitted with status cadence (`statusEmitPeriodMs`)
- includes:
  - sensor seen bitmask
  - visual-enabled bit
  - detection-mode bits

## 16. Serial command interface (live tuning)

Commands:

- `GET`
- `HELP`
- `SET,<key>,<value>`

Main keys:

- mode and zones:
  - `mode`
  - `zones`
  - `sectors_mask`
- visualization:
  - `visual`
  - `bright`
- thresholds/timing:
  - `red_mm`
  - `yellow_mm`
  - `stale_ms`
  - `status_ms`
  - `proximity_ms`
  - `led_mode` (`0=sector-fill`, `1=radar`)
- cart filter:
  - `occ_rise`
  - `occ_decay`
  - `occ_enter`
  - `occ_exit`
- polar filter:
  - `polar_rise`
  - `polar_decay`
  - `polar_enter`
  - `polar_exit`

Responses:

- success: `ACK,key,value` then new `CFG,...`
- failure: `ERR,...`

## 17. Threading/execution model

There is no RTOS task split in this file. Behavior is callback + loop:

- callback context:
  - minimal parsing and state write
- loop context:
  - all conversion, filtering, and output

This design avoids heavy compute inside ESP-NOW callback and keeps timing predictable.

## 18. Typical debug workflow

1. Open serial monitor.
2. Verify `S` lines show expected sensor packet rates.
3. Verify `P/E` lines are present and points are valid where expected.
4. Change one tuning value with `SET,...`.
5. Confirm `ACK` and updated `CFG`.
6. Observe `DL/DP/DO` changes (especially in polar mode).
7. Confirm LED behavior matches computed zone distances.

## 19. Common extension points

If you need to evolve behavior:

1. Add high-resolution radar bins:
   - create separate radar bin arrays and update path in `broadcastZoneProximity()`.
2. Keep LED logic stable:
   - derive zone distances from stable occupied bins, not raw nearest point.
3. Add new serial tunables:
   - add key handling in `handleSerialCommand()`
   - include value in `emitTuningConfigLine()`.

---

If this file changes significantly, update this document at the same time so tuning/debug remains reliable.
