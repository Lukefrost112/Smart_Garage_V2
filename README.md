# Smart Garage (ESP32) — Modular Systems + Full Integrated Build

A smart garage project built on **ESP32**, designed in two ways:

- **Modular subsystems**: each feature can run by itself (standalone sketches)
- **Full integrated system**: everything combined in one sketch (non-blocking, no pin conflicts)

---

## Features

- **Gate system** using **2 IR sensors** (entry/exit direction) + **servo**
- **OLED UI** (welcome/exit messages + live parking status)
- **Parking occupancy** using **5 LDR sensors** (**dark = occupied**)
- **Smart lighting**
  - Pot controls brightness
  - Ambient LDR turns lights **off** when it’s bright
- **Fire/Flame alarm** using **digital flame sensor (DO)** + buzzer + red LED + OLED alarm screen
- **Elevator control** (hold-to-move up/down buttons + motor driver)

---

## Hardware Needed

- ESP32 DevKit board
- 0.96" OLED SSD1306 (I2C, usually address `0x3C`)
- Servo motor (gate)
- 2× IR obstacle sensors (gate direction logic, usually **active LOW**)
- 5× LDR sensors (parking spots) + resistors (voltage divider)
- Potentiometer (brightness control)
- 3× LEDs (PWM dimming) + resistors
- Digital flame sensor module (**DO output**)
- Buzzer + red LED
- Motor driver (L298N / TB6612 / similar) for elevator DC motor
- DC motor for elevator mechanism
- 2 buttons for elevator control

---

## Power Notes (Important)

For stable behavior (no OLED glitching / no random resets):

- Servo + DC motor **MUST** use **external power**
  - Servo: 5V / 6V supply (as required)
  - Motor driver: external motor supply (as required)
- ESP32 **3.3V is for sensors + OLED only**
- **All grounds must be common**
  - Connect external power GND to ESP32 GND

---

## Full System Pin Map (Default)

### Gate + IR + Servo

| Function | Pin |
|---|---|
| IR outside (active LOW) | GPIO14 |
| IR inside (active LOW) | GPIO12 |
| Gate servo signal | GPIO13 |

### OLED (I2C)

| Function | Pin |
|---|---|
| SDA | GPIO21 |
| SCL | GPIO22 |
| Address | `0x3C` |

### Parking Spots (LDRs) — 5 spots (ADC1 pins recommended)

| Spot | Pin |
|---|---|
| Spot 1 | GPIO34 |
| Spot 2 | GPIO35 |
| Spot 3 | GPIO32 |
| Spot 4 | GPIO33 |
| Spot 5 | GPIO36 |

### Smart Lighting

| Function | Pin |
|---|---|
| Brightness Pot | GPIO25 |
| Ambient LDR | GPIO26 |
| LED 1 PWM | GPIO16 |
| LED 2 PWM | GPIO17 |
| LED 3 PWM | GPIO18 |

### Flame Alarm (Digital)

| Function | Pin |
|---|---|
| Flame sensor DO (digital) | GPIO39 (input-only) |
| Buzzer | GPIO15 |
| Fire LED | GPIO2 |

> Note: GPIO39 is input-only (great for sensors). Flame module DO is usually push-pull, so `INPUT` is fine.

### Elevator

| Function | Pin |
|---|---|
| Motor IN1 | GPIO19 |
| Motor IN2 | GPIO23 |
| Button UP (`INPUT_PULLUP`) | GPIO4 |
| Button DOWN (`INPUT_PULLUP`) | GPIO5 |

---

## Wiring Guide

### 1) IR Sensors (Gate)

Typical IR obstacle modules:

- VCC → 3.3V or 5V (check your module)
- GND → GND
- OUT → ESP32 pin

Most modules output **LOW when obstacle detected** (active LOW).

---

### 2) OLED (SSD1306 I2C)

- VCC → 3.3V
- GND → GND
- SDA → GPIO21
- SCL → GPIO22

---

### 3) LDR Spot Sensors (each spot)

Wire each LDR as a voltage divider so ESP32 can read an analog value.

**Common wiring:**

- 3.3V → LDR → Analog Pin → Resistor (10k) → GND

**Project logic:**

- DARK (low reading) = **occupied**
- BRIGHT (high reading) = **empty**

---

### 4) Digital Flame Sensor (DO)

- VCC → 3.3V or 5V (check module)
- GND → GND
- DO → GPIO39

Most flame modules: **DO becomes LOW when flame detected**.  
If yours is the opposite, flip `FLAME_ACTIVE_LOW` in code.

---

### 5) Elevator Motor Driver

- Motor power from external supply (not ESP32)
- IN1/IN2 from ESP32 (GPIO19, GPIO23)
- Driver GND tied to ESP32 GND (common ground)

**Buttons:**

- One side → GPIO4 / GPIO5
- Other side → GND  
(Using `INPUT_PULLUP`, pressed = **LOW**)

---

## Software Setup

### Arduino IDE (ESP32)

Install libraries:

- Adafruit GFX Library
- Adafruit SSD1306
- ESP32Servo

---

## Running the Full System

Upload the integrated sketch to ESP32.

On boot:

- Code calibrates LDR baselines (best done with spots empty).

---

## Tuning

### LDR Spot Detection

Tune these constants in code if needed:

- `SPOT_MARGIN`
- `SPOT_HYST`
- `SPOT_CONFIRM_N`

If occupancy flickers:

- Increase `SPOT_CONFIRM_N` (e.g., 4–6)
- Increase `SPOT_HYST`
- Adjust `SPOT_MARGIN`

---

### Flame Sensor False Triggers

Tune:

- `FLAME_CONFIRM_N` (requires flame for N consecutive samples)
- The sensitivity potentiometer on the flame module

---

### Ambient Light Threshold

Tune:

- `ambientLdrThreshold`

Raise it if LEDs turn off too early, lower it if they stay on in daylight.

---

## Troubleshooting

### OLED flicker / random resets

Usually power noise from servo/motor:

- Use external power for servo + motor
- Ensure shared ground with ESP32
- Keep wiring short / solid connections

### Spots always occupied / always empty

- Divider wired differently than expected OR thresholds need tuning
- Print raw readings and compare covered vs uncovered

### Flame alarm always ON

- Your module may be active HIGH → flip `FLAME_ACTIVE_LOW`
- Reduce sensitivity on the flame module (turn the pot)

---

## Notes / Suggestions

- Avoid using ADC2 pins for analog reads if you plan to use Wi-Fi/Bluetooth (ESP32 ADC2 conflicts).
- Parking spot pins listed here use ADC1-friendly pins for stability.
