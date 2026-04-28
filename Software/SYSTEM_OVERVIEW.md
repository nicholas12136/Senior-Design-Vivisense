# ViviSense System Overview

This document explains what ViviSense is, how the software pieces fit together, and where to make changes.

It is written for someone who is new to the project:

- first to understand the system at a high level
- then to understand the runtime data flow
- then to know where to edit code safely

## 1. What ViviSense does

ViviSense is an obstacle-awareness system for a wheelchair.

Its job is to:

1. read obstacle data from multiple Time-of-Flight sensors
2. convert that data into points around the wheelchair
3. decide where obstacles are and how close they are
4. communicate that information through:
   - the LED ring
   - audio cues
   - caregiver controls
   - optional presentation/debug tools

The system is designed so that the important obstacle decisions happen in one place: `MainController`.

That is the main idea to remember:

- sensors measure
- `MainController` decides
- other devices display or control

## 2. Main Software Pieces

The main software lives under `Software/`.

The most important parts are:

- `Software/ESP firmware/MainController`
  - the central runtime brain
  - receives sensor packets
  - builds world points
  - runs the polar occupancy grid
  - creates LED output
  - handles serial tuning/debug

- `Software/ESP firmware/LEDRingController`
  - receives LED frames
  - applies them to the physical LED ring
  - does not decide what obstacles mean

- `Software/CaregiverApp`
  - creates the Wi-Fi access point and serves the caregiver web UI
  - sends caregiver settings to `MainController`
  - sends navigation commands
  - can send LED preview frames for UI/testing

- `Software/CaregiverApp/ui`
  - the browser UI
  - obstacle mode selection
  - threshold controls
  - audio controls
  - sector enabling/disabling

- `Software/ESP firmware/LeftPodSender_ArduinoIDE`
- `Software/ESP firmware/RightPodSender_ArduinoIDE`
  - sensor pod firmware
  - reads the ToF hardware
  - sends raw sensor packets over ESP-NOW

- `Software/ESP firmware/PresentationGridReceiver`
  - optional presentation receiver
  - receives the live polar occupancy owner grid over ESP-NOW
  - forwards it over USB serial to a laptop

- `Software/Visualizer`
  - Python tools for visualization and debugging
  - includes the live presentation polar-grid viewer

## 3. System Architecture In One Picture

Sensor Pods
  -> ESP-NOW raw sensor packets
  -> MainController
       -> point conversion
       -> polar occupancy filtering
       -> obstacle decisions
       -> LED render frame
       -> audio decisions
       -> status / debug / presentation data

MainController
  -> LEDRingController for physical LED output
  -> CaregiverApp for control/status interaction
  -> optional PresentationGridReceiver for live demo visualization

Caregiver Web UI
  -> browser
  -> CaregiverApp
  -> MainController
```

## 4. What Each Device Is Responsible For

This split matters because it tells you where to make changes.

### Sensor Pods

The sensor pods are responsible for:

- taking raw distance readings
- packaging those readings into `SensorPacket` messages
- transmitting them wirelessly

The pods are not responsible for:

- obstacle filtering
- occupancy logic
- LED decisions

If your problem is about bad raw data or sensor bring-up, start in the pod firmware.

If your problem is about how the chair reacts to data, start in `MainController`.

### MainController

`MainController` is the core of the system.

It is responsible for:

- receiving sensor frames
- converting range cells into world-frame points
- deciding which points count as obstacles
- maintaining the polar occupancy grid
- converting occupancy into sector distances
- creating radar or sector LED frames
- handling serial tuning commands
- sending presentation-grid data when enabled

If you are changing obstacle behavior, this is usually the file to edit:

- `Software/ESP firmware/MainController/src/main.cpp`

### LEDRingController

The LED controller is intentionally simpler.

It is responsible for:

- receiving a prepared LED frame
- driving the LEDs

It is not responsible for:

- occupancy logic
- distance classification
- sector/radar interpretation

That is good for maintenance, because the obstacle logic stays centralized.

### CaregiverApp and UI

The caregiver side is responsible for:

- exposing controls in the browser
- sending those settings to `MainController`
- sending navigation commands
- giving a friendly operator-facing interface

It is not the source of truth for obstacle detection.

The app changes settings, but `MainController` still performs the real detection logic.

## 5. Runtime Data Flow

This is the most important section if you want to understand how the system behaves live.

### Step 1: Sensors send raw packets

Each sensor pod sends a `SensorPacket` to `MainController`.

That packet contains per-cell data such as:

- measured distance
- target status
- number of targets
- sensor ID
- timestamp

At this stage, the data is still just raw sensor information.

### Step 2: MainController stores the latest frame

In `MainController`, the ESP-NOW receive callback stores the latest packet for each sensor and marks that sensor as needing processing.

This is important:

- the callback stays light and fast
- heavy work happens later in the main loop

That makes the system more stable and easier to debug.

### Step 3: Raw cells become world points

Later in the main loop, `MainController` processes pending sensors and converts each valid range cell into a world-frame point.

This logic lives in:

- `Software/ESP firmware/MainController/include/SensorAndPoint.h`

Each point gets:

- `worldX`
- `worldY`
- `worldZ`

using the sensor pose and rotation math.

The world convention is:

- `+X` forward
- `+Y` left
- `+Z` up

This step is where raw sensor geometry becomes wheelchair-relative geometry.

### Step 4: Invalid points are rejected

Not every sensor reading becomes a usable point.

Before a point participates in obstacle detection, the system checks things like:

- valid cell index
- valid target status
- minimum distance

If a point fails here, it never reaches the occupancy grid.

This is a very common debugging lesson:

- if the system is missing obstacles, the problem may happen before occupancy filtering

### Step 5: Points are filtered for occupancy use

After a valid world point exists, `MainController` decides whether it should count toward the obstacle grid.

Right now that includes:

- planar range limit
- floor cutoff
- ceiling cutoff

This is where floor and overhead clutter can be removed without deleting the raw point pipeline entirely.

### Step 6: Points are placed into the polar occupancy grid

The internal obstacle model is a polar grid.

That means each point is assigned to:

- a distance ring
- an angular zone

The current internal grid uses:

- `12` rings
- `24` angular bins

This internal grid is more detailed than the caregiver-facing sector count.

That separation is intentional:

- the filter can stay fine-grained
- the user-facing output can still be 4, 6, or 8 sectors

### Step 7: Confidence and hysteresis smooth the result

Each polar cell has confidence.

When a cell keeps getting hit:

- confidence rises

When a cell stops getting hit:

- confidence decays

A cell only becomes occupied once it crosses the enter threshold.

A cell only becomes clear once it falls below the exit threshold.

This hysteresis is what prevents rapid blinking and unstable behavior.

This is also why the occupancy grid feels more stable than raw point data.

### Step 8: Occupancy becomes user-facing proximity

Once the polar occupancy grid is updated, `MainController` converts it into a simpler output:

- closest occupied distance by sector

The caregiver system uses sector counts of:

- `4`
- `6`
- `8`

So the system is effectively doing:

- fine internal reasoning
- simpler external communication

### Step 9: MainController builds the LED frame

After proximity is known, `MainController` renders the LED output.

There are two visual modes:

- `sector`
  - each sector shows the nearest obstacle severity
- `radar`
  - occupied polar bins are drawn more directly by angle and distance

The important thing is:

- both modes come from the same occupancy grid

That keeps the system consistent.

### Step 10: Other outputs are updated

Depending on what is enabled, `MainController` may also:

- play obstacle audio logic
- emit status/debug serial lines
- send the presentation polar grid

So one obstacle pipeline feeds multiple outputs.

## 6. Why The Polar Grid Matters

The project used to carry more than one detection idea, but the current system is polar-only.

That means:

- there is one real obstacle model
- one filtering path
- one set of occupancy tuning parameters

This simplifies the system a lot.

It also means that when you tune occupancy behavior, you are affecting:

- sector mode
- radar mode
- audio behavior tied to proximity
- the presentation grid output

That is useful, because the demo tools and the real wheelchair behavior stay aligned.

## 7. The Most Important Parameters

If someone is new and wants to tune behavior, these are the first parameters to learn.

### Distance severity thresholds

- `red_mm`
- `orange_mm`
- `yellow_mm`

These decide how close an obstacle must be before it changes color/severity.

They do not decide whether a point exists.

They decide how serious the obstacle looks once it has already been detected.

### Occupancy stability parameters

- `polar_rise`
- `polar_decay`
- `polar_enter`
- `polar_exit`

These decide how quickly cells turn on and off.

They shape system feel:

- twitchy vs stable
- immediate vs persistent

### Timing parameters

- `proximity_ms`
- `stale_ms`

These decide:

- how often occupancy updates
- how long disconnected sensors are trusted

### Height window parameters

- `floor_z_mm`
- `ceiling_z_mm`

These decide which vertical slice of the environment is allowed to count for obstacle occupancy.

They are especially useful for:

- removing floor hits
- removing overhead clutter

## 8. Where To Change Things

This section is the practical map for editing the project.

### If you want to change obstacle behavior

Start here:

- `Software/ESP firmware/MainController/src/main.cpp`

Examples:

- occupancy filtering
- radar rendering
- sector mapping
- serial tuning keys
- presentation-grid sending

### If you want to change point geometry or sensor pose math

Start here:

- `Software/ESP firmware/MainController/include/SensorAndPoint.h`

Examples:

- coordinate conventions
- cell ray generation
- world transform math

And here:

- hardcoded sensor pose table in `MainController/src/main.cpp`

Examples:

- sensor position
- sensor angle
- sensor orientation corrections

### If you want to change caregiver controls

Start here:

- `Software/CaregiverApp/ui/src/pages/feedbackConfig.ts`

And then check:

- `Software/CaregiverApp/src/main.cpp`

The UI defines the controls.

The app firmware packages and forwards the selected settings.

### If you want to change LED hardware behavior only

Start here:

- `Software/ESP firmware/LEDRingController`

This is for things like:

- LED driver behavior
- packet application
- physical LED output details

Not obstacle logic.

### If you want to change presentation/demo visualization

Look at:

- `Software/ESP firmware/PresentationGridReceiver`
- `Software/Visualizer/serial_presentation_polar_grid_viewer.py`

This path is for demo display and debugging, not core detection.

## 9. Typical Debugging Order

If something looks wrong, this order usually saves time.

1. Verify sensor packets are arriving.
2. Verify points are being created.
3. Verify the points are in the right world positions.
4. Verify occupancy filtering is accepting or rejecting the right points.
5. Verify the polar grid is becoming occupied where expected.
6. Verify the output mode is rendering that occupancy correctly.

This order matters because a later stage can look broken when the real problem is earlier.

For example:

- a bad sensor pose can look like a bad occupancy filter
- a too-high floor cutoff can look like missing radar bins
- wrong thresholds can look like wrong LED logic

## 10. Common Mental Model Mistakes

These are the misunderstandings that usually slow people down.

### Mistake 1: Thinking the UI performs detection

It does not.

The UI only sends settings and commands.

`MainController` performs detection.

### Mistake 2: Thinking the LED controller decides obstacle meaning

It does not.

The LED controller only applies prepared frames.

### Mistake 3: Thinking raw points and occupancy are the same thing

They are not.

A raw point is just a measured location.

Occupancy is a filtered, time-smoothed interpretation of many points over time.

### Mistake 4: Thinking thresholds control stability

They do not.

Thresholds control severity coloring.

The polar confidence parameters control stability.

### Mistake 5: Thinking a missing obstacle is always an occupancy bug

Sometimes the point never became valid in the first place.

Sometimes it was filtered out by floor/ceiling logic.

Sometimes it is present in occupancy but outside the current visual severity range.

## 11. Recommended Reading Order For A New Developer

If someone is onboarding, this order works well:

1. Read this file.
2. Read `Software/RUNNING_THE_SYSTEM.md`.
3. Read `Software/OCCUPANCY_GRID_TUNING_GUIDE.md`.
4. Read `Software/ESP firmware/MainController/PROGRAM_FLOW.md`.
5. Open `Software/ESP firmware/MainController/src/main.cpp`.
6. Open `Software/ESP firmware/MainController/include/SensorAndPoint.h`.

That order goes from:

- concept
- to operation
- to tuning
- to implementation

## 12. Short Summary

If you only remember the essentials, remember this:

1. Sensor pods send raw ranging data.
2. `MainController` converts it to world points.
3. `MainController` filters those points into a polar occupancy grid.
4. `MainController` turns occupancy into LED, audio, and presentation outputs.
5. The caregiver UI changes settings, but `MainController` is the real obstacle logic.

That is the core architecture of the current ViviSense system.
