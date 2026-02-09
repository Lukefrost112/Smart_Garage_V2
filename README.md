# Arduino Smart Garage 🚗🏢

An Arduino-based **Smart Garage system** that combines safety, automation, and live status display.  
This project includes:

- 🔥 **Fire alarm system** (buzzer + warning indication)
- 🅿️ **Open spot counter** (tracks available parking spots)
- 🚧 **Automatic gate** (opens/closes based on entry/exit detection)
- 📟 **LCD UI** (welcome screen, exit screen, and live open-spots display)
- 🛗 **Elevator system** (basic elevator logic for moving between levels / platforms)

---

## Demo (What it does)

### LCD Screens
- **Welcome screen** (on entry)
- **Exit screen** (on leaving)
- **Status screen** showing:
  - Total spots
  - Open spots (available)

### Gate Logic
- Detects a car at **entry** → shows welcome → opens gate → updates count.
- Detects a car at **exit** → shows exit message → opens gate → updates count.

### Fire Alarm
- Fire sensor triggers:
  - buzzer / siren
  - warning output
  - (optional) override behavior like stopping the gate/elevator for safety

### Elevator System
- Moves between floors/positions based on requested direction or target floor.
- (Implementation depends on your hardware: DC motor + limit switches, stepper, or servo.)

---

## Hardware (Typical Setup)

> Adjust this list based on what you actually used.

**Core**
- Arduino (Uno / Nano / Mega)
- 16x2 LCD (parallel or I2C)
- Breadboard + jumper wires
- Power supply (external recommended for motors/servos)

**Sensors (examples)**
- IR sensors / ultrasonic sensors (entry & exit detection)
- Spot sensors (one per spot OR zone-based counting)
- Flame sensor / smoke sensor (fire detection)

**Actuators**
- Servo motor or DC motor + driver (gate)
- Buzzer (alarm)
- Elevator motor (servo/stepper/DC motor) + driver
- LEDs (status indicators)

---

## How It Works (System Overview)

### 1) Parking Spot Counter
- Garage starts with a configured `TOTAL_SPOTS`.
- When a car enters successfully, available spots decrease.
- When a car exits successfully, available spots increase.
- LCD always shows the current available count.

### 2) Gate Control
- Entry sensor triggers gate opening **only if spots > 0**.
- Exit sensor triggers gate opening regardless of availability.
- Gate closes automatically after a delay or after sensor clears.

### 3) LCD UI Flow
- Default: shows **Open Spots**
- On entry: shows **Welcome**
- On exit: shows **Goodbye / Exit**
- Returns to status screen after a short delay

### 4) Fire Alarm Priority
- If fire is detected:
  - alarm triggers immediately
  - optional: gate/elevator behavior changes (failsafe mode)

### 5) Elevator Logic
- Receives a request (button/floor select or sensor-based).
- Moves motor until it reaches target (limit switches / encoder / timed move).
- Stops safely and updates state.

---

## Pinout

Fill this table with your actual pins (recommended for clarity):

| Module | Signal | Arduino Pin |
|-------|--------|-------------|
| LCD | RS / EN / SDA / SCL | `...` |
| Entry Sensor | OUT | `...` |
| Exit Sensor | OUT | `...` |
| Spot Sensors | OUT | `...` |
| Gate Servo/Motor | PWM/IN | `...` |
| Fire Sensor | OUT/AO | `...` |
| Buzzer | + | `...` |
| Elevator Motor | IN/STEP/DIR | `...` |
| Limit Switches | Floor 1 / Floor 2 | `...` |

## Setup & Run

1. Clone the repo:
   ```bash
   git clone https://github.com/<your-username>/arduino-smart-garage.git
   cd arduino-smart-garage
