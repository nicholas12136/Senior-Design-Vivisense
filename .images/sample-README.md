<table border="0">
  <tr>
    <td valign="center">
      <img src=".assets/images/rat-spinning.gif" width="100">
    </td>
    <td>
      <h1>NRC 2026: Micromouse Competition</h1>
      <blockquote>
        The official SIUE Autonomous Robotics Club GitHub repository for the 2026 NRC Micromouse Competition
      </blockquote>
    </td>
    <td valign="center">
      <img src=".assets/images/siue-arc-logo.jpg" width="100">
    </td>
  </tr>
</table>



[![Competition](https://img.shields.io/badge/Competition-NRC_2026-red)](https://irp.cdn-website.com/9297868f/files/uploaded/NRCContestRules2026-4d8e93e7.pdf)
[![Platform](https://img.shields.io/badge/Platform-ESP32-blue)](https://randomnerdtutorials.com/getting-started-with-esp32/)
[![Language](https://img.shields.io/badge/Language-C++-green)](https://www.w3schools.com/cpp/default.asp)
[![License](https://img.shields.io/badge/Danger-!!!-purple)](https://www.youtube.com/watch?v=dQw4w9WgXcQ)



---

## Project Overview
Our team is developing an autonomous "Mouse" to compete in the **National Robotics Challenge**. The robot must navigate a $10 \times 10$ unit square maze, mapping the environment in real-time to find the fastest path to the center.

### Competition Constraints (NRC Rules)
**Some Basics:**
* **Mouse Size:** Max $7" \times 7" \times 7"$.
* **Maze Size:** $10 \times 10$ of $10" \times 10"$ tiles.
* **Time:** 10-minute total run time.
* **Autonomy:** No external communication once the maze layout is disclosed.
* **Scoring:** Based on a factor of things but overall shortest time wins.

<details>
<summary><b>Click for more extensive rules</b></summary>


**Maze Criteria:**
* No inaccessible locations
* Mouse will always start in one of the four corners 
* Exactly three starting walls
* Only one entrance to the center, multiple paths to the destination square are allowed and are to be expected
* Has a hollow center, i.e. the center peg has no walls attached to it
* Has walls attached to every peg except the center peg
* Is unsolvable by a wall-following robot 

**Time Criteria:**
* The run timer will start when the front edge of the robot crosses the start line and stops when
the front edge of the robot crosses the finish line
* If a robot re-enters the start square before entering the destination square on a run that run is aborted and a new run will begin with a new time that starts when the starting square is exited.
* The robot may, after reaching the destination square, continue to navigate the maze for as long
as their total maze time allows. The time taken will not count toward any run.
* If an operator touches the robot during a run, it is
deemed aborted, and the robot must be removed from the maze.
* If a robot has already crossed the finish line, it may be removed at any time without affecting the
run time of that run
* The minimum run time shall be the robot's official time (Dash Attempt).

**Scoring Criteria:**
* Robots that do not enter the center square will be ranked by the maximum number of cells they
consecutively transverse without being touched. However, judges are not required to give any
rankings to robots who do not finish and may declare no winners or declare less than three
winners at their discretion.

**Alteration Criteria:**
* After the maze is disclosed, the operator shall not feed information on the maze into the robot.
However, switch positions may be changed for the purpose of changing programs within the
robot (changing algorithms is allowed)
* A contestant may not alter a robot in a manner that alters its weight
* contestants are allowed to: Change switch settings (e.g., to change algorithms), replace batteries between runs, adjust sensors, change speed settings, make repairs 

</ul>
</details>

<p align="center">
  <img src=".assets/images/NRC_maze_example.png" width="600">
</p> 

> *Example maze provided in 2026 NRC Contest Manual.*

---

## Repository Structure

| Folder | Purpose |
| :--- | :--- |
| [`/Competition Notebook`](./Competition%20Notebook) | Project documention required upon NRC submission. |
| [`/ESP32 Firmware`](./Past%20Firmware) | ESP32 source code, kept for reference when an ESP32-based design is revisited. |
| [`/Hardware`](./Hardware) | Schematics for all current components and STP print files for the robot chassis. |
| [`/NRC 2025 Code`](./NRC%202025%20Code) | Sketches and reference files from ARC's 2025 Micromouse competition attempt. |
| [`/Romi32u4`](./Romi32u4) | Code for the current Romi 32U4-based platform. |
| [`/Simulations`](./Simulations) | Flood Fill logic and supporting files for simulating maze navigation. |
| **[OneDrive Hub]** | [**Click here for OneDrive**](https://siuecougars-my.sharepoint.com/:f:/r/personal/ngarmon_siue_edu/Documents/ARC/Micro%20Mouse?csf=1&web=1&e=Olx0Ui) |

---

## Current Tech Stack
> *Current iteration uses a Romi 32U4 + Raspberry Pi combination.*

* **Primary Controller:** [Romi 32U4 Control Board](https://www.pololu.com/docs/0J69/all) — handles low-level motor control, encoder feedback, and sensor interfacing.
* **Companion Computer:** Raspberry Pi — runs high-level navigation logic and communicates with the Romi over I²C.
* **Platform:** [Romi Chassis & Motor Kit](https://www.pololu.com/product/3544) — motors, encoders, and drivetrain are integrated into the Romi platform.
* **Sensors:** Wall detection uses a mix of sensor types:
  * *Left & Right:* [GP2Y0A41YK0F](https://global.sharp/products/device/lineup/data/pdf/datasheet/gp2y0a41sk_e.pdf) Sharp IR Analog Distance Sensors.
  * *Front:* Generic digital IR obstacle avoidance sensor.

---

## Navigation Process
The mouse uses **Flood Fill** as its core algorithm — a form of Breadth-First Searching that works by "pouring water" from the goal back to the start, assigning distance values to each cell. The mouse always moves toward lower-value cells, driving it toward the goal. ([**Video explanation**](https://www.youtube.com/watch?v=ktn3C7aXVR0))

Maze solving is broken into three sequential phases:

1. **Search Phase:** The mouse explores the maze from the starting corner, continuously updating its internal map as new walls are discovered. Flood Fill is recalculated after each new wall is detected, always routing the mouse toward the goal via the best known path.
2. **Return Phase:** Once the goal is reached, the mouse navigates back to the starting position using the map it has built, again guided by Flood Fill.
3. **Goal Run:** With a complete map in hand, the mouse executes a final optimized run from start to goal, following the shortest path determined during the search phase.

<table border="0">
  <tr>
    <td width="50%" align="center">
      <img src=".assets/images/mouse-running2.gif" width="65%">
      </img>
    </td>
  </tr>
  <tr>
    <td align="center">
      <i>Visualization of the <b>Flood Fill Algorithm</b> mapping the 10x10 maze.</i>
    </td>
  </tr>
</table>

---

## Development Setup

<details>
<summary><b>Click to expand: Software Installation & Setup</b></summary>

### Cloning the Repository (GitHub Desktop)

1. Download and install [GitHub Desktop](https://desktop.github.com/).
2. Sign in with your GitHub account.
3. Click **File → Clone Repository**.
4. Select the **URL** tab and paste:
   ```
   https://github.com/SIUE-ARC/SIUE-ARC-Micromouse.git
   ```
5. Choose a local path and click **Clone**.

---

### Romi 32U4 Setup (Arduino IDE)

1. Download and install the [Arduino IDE](https://www.arduino.cc/en/software).
2. Open Arduino IDE and go to **File → Preferences**.
3. Under *Additional Boards Manager URLs*, add:
   ```
   https://files.pololu.com/arduino/package_pololu_index.json
   ```
4. Go to **Tools → Board → Boards Manager**, search for **Pololu A-Star**, and install it.
5. Select **Tools → Board → Pololu A-Star 32U4** as your target board.
6. Install the required libraries via **Tools → Manage Libraries**:
   * Search for **Romi32U4** and install it.
   * Search for **PololuRPiSlave** and install it.
7. Open the sketch from the [`/Romi32u4`](./Romi32u4) folder and upload to the board.

---

### Raspberry Pi SSH Setup (Windows)

1. Ensure SSH is enabled on the Raspberry Pi (via `raspi-config` or the Pi OS desktop under **Preferences → Raspberry Pi Configuration → Interfaces**).
2. Connect the Pi to the same network as your Windows machine (or directly via USB/Ethernet/Hotspot).
3. Open **Windows Terminal** or **Command Prompt** and run:
   ```bash
   ssh pi@<raspberry-pi-ip-address>
   ```
   Replace `<raspberry-pi-ip-address>` with the Pi's actual IP (find it by running `hostname -I` on the Pi).
4. Enter the Pi's password when prompted (default is `raspberry` unless changed).
5. Once connected, navigate to the project files and run the Python scripts from the [`/RPi`](./RPi) folder as needed.

---

### ESP32 Setup (VS Code + PlatformIO)

> *This setup applies to the code found in [`/Past Firmware`](./Past%20Firmware) only.*

1. Download and install [VS Code](https://code.visualstudio.com/).
2. Install the **PlatformIO IDE** extension from the VS Code Extensions Marketplace.
3. Open the desired project folder from `/Past Firmware` in VS Code.
4. PlatformIO will automatically detect the `platformio.ini` config and install dependencies.
5. Connect the ESP32 and use the PlatformIO **Upload** button to flash the board.

</details>

---

## Competition Results

We competed at the **National Robotics Challenge** on April 17, 2026. Here's a look at how it went.

<table border="0">
  <tr>
    <td width="50%" align="center">
      <img src=".assets/images/currentbot.jpg" width="90%">
      <br><i>Our robot heading into competition.</i>
    </td>
    <td width="50%" align="center">
      <img src=".assets/images/competitionmaze.jpg" width="90%">
      <br><i>The maze we faced on competition day.</i>
    </td>
  </tr>
  <tr>
    <td width="50%" align="center">
      <img src=".assets/images/2026-Finishing.png" width="90%">
      <br><i>Our official final standings (Gold, Woo Hoo!).</i>
    </td>
    <td width="50%" align="center">
      <img src=".assets/images/results.jpg" width="90%">
      <br><i>Our official final time.</i>
    </td>
  </tr>
  <tr>
    <td width="50%" align="center">
      <img src=".assets/images/allrobots.jpg" width="90%">
      <br><i>All robots competing at NRC 2026.</i>
    </td>
    <td width="50%" align="center">
      <img src=".assets/images/guysandmaze.jpg" width="90%">
      <br><i>The team after the competition.</i>
    </td>
  </tr>
</table>


## Key Takeaways and Potential Improvements

<details>
<summary><b>Click for knowledge</b></summary>

* **Our current design could be improved upon with these alterations (most likely):**
  1. It was noted at competition that the Romi32 could be controlled by an ESP32 via serial communication. This was not previously known and is definitely worth looking into for future iterations. While the raspberry pi was nice it was rather tedious.
  2. The wall-centering logic based on PID control was not the most accurate. We think that if the robot were to move continuously, that would give better sensor readings, thus resulting in better PID tuning in the long run.
  3. We also faced issues with inconsistent turning parameters. We are unsure if this is related to the batteries dropping voltage during runs, but it is worth noting and looking into.

* **Some designs worth mentioning that we saw at competition:**
  1. The 1st place robot was equipped with omni-directional wheels. This allowed it to never change orientation during its run, leading to less encoder error over time.
  2. Another robot (that also used the Romi32 microcontroller), when performing the RETURN and QUICK RUN phases, would perform a multi-cell movement if possible. This means that if there was a corridor 2+ cells long, instead of moving through one cell at a time, it would move the entire length of the corridor in one movement, saving overall time. 
  3. It was also noted that it would potentially be beneficial to add the ability for the robot to remember the maze between different trial runs. This came about from the "30-second penalty" rule that occurred when you removed the robot from the maze at any time. Since this would most likely happen regardless unless you have a perfect robot, the robot would find the center in one run, get removed from the maze, then start the next run with the maze information it gathered from previous runs.

  </details>