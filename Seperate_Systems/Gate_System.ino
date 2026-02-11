#include <ESP32Servo.h>

#define IR1 14           // outside sensor
#define IR2 12           // inside sensor
#define MOTOR_PIN 13

// ----------------- TUNING -----------------
const int CLOSED_ANGLE = 0;
const int OPEN_ANGLE   = 90;

// If a car triggers the first sensor but never reaches the second (backs out / stuck),
// we cancel after this many ms (prevents staying open forever).
const unsigned long APPROACH_TIMEOUT_MS = 1000000;

// Once we think the lane is clear, require BOTH sensors to be clear continuously
// for this long before closing (prevents bounce / flicker).
const unsigned long CLEAR_HOLD_MS = 500;

// Simple debounce for IR sensor reading stability
const unsigned long DEBOUNCE_MS = 30;

// Servo smoothing
const unsigned long SERVO_STEP_MS = 5;
const int SERVO_STEP_DEG = 3;
// ------------------------------------------

Servo gate;

// Direction / transaction state
enum State {
  IDLE = 0,

  ENTER_WAIT_IR2,     // saw IR1 first, gate open, waiting for IR2 (commit) OR cancel
  ENTER_WAIT_CLEAR,   // committed (reached IR2), now wait until both clear to close

  EXIT_WAIT_IR1,      // saw IR2 first, gate open, waiting for IR1 (commit) OR cancel
  EXIT_WAIT_CLEAR     // committed (reached IR1), now wait until both clear to close
};

State systemState = IDLE;

bool reachedEnd = false;          // "commit" flag (reached the second sensor)
bool reversalDetected = false;    // detected turn-back after commit (optional use)

unsigned long stateStartMs = 0;   // when this transaction started
unsigned long clearStartMs = 0;   // when both sensors first became clear

// Servo control
int currentAngle = CLOSED_ANGLE;
int targetAngle  = CLOSED_ANGLE;
unsigned long lastServoStepMs = 0;

// ----------------- Debounced input helper -----------------
struct DebouncedInput {
  int pin;
  bool stable;          // stable value (true/false)
  bool lastRaw;
  unsigned long lastChangeMs;

  void begin(int p) {
    pin = p;
    bool raw = (digitalRead(pin) == LOW);  // LOW = detected
    stable = raw;
    lastRaw = raw;
    lastChangeMs = millis();
  }

  void update() {
    bool raw = (digitalRead(pin) == LOW);
    unsigned long now = millis();

    if (raw != lastRaw) {
      lastRaw = raw;
      lastChangeMs = now;
    }

    if ((now - lastChangeMs) >= DEBOUNCE_MS) {
      stable = raw;
    }
  }
};

DebouncedInput s1In;
DebouncedInput s2In;
// ----------------------------------------------------------

void enterState(State s) {
  systemState = s;
  stateStartMs = millis();
  clearStartMs = 0;
}

bool bothClear() {
  return (!s1In.stable && !s2In.stable);
}

void handleClearToClose() {
  // Gate should stay open until BOTH sensors are clear for CLEAR_HOLD_MS
  if (bothClear()) {
    if (clearStartMs == 0) clearStartMs = millis();
    if (millis() - clearStartMs >= CLEAR_HOLD_MS) {
      reachedEnd = false;
      reversalDetected = false;
      enterState(IDLE);
    }
  } else {
    clearStartMs = 0; // reset hold timer if anything becomes active again
  }
}

void setup() {
  // IMPORTANT: Most IR obstacle modules output LOW when blocked and HIGH when clear.
  // Many work best with INPUT_PULLUP (more stable) if your module output is open-collector.
  // If your readings become inverted, switch back to INPUT.
  pinMode(IR1, INPUT_PULLUP);
  pinMode(IR2, INPUT_PULLUP);

  gate.attach(MOTOR_PIN);
  gate.write(CLOSED_ANGLE);
  currentAngle = CLOSED_ANGLE;
  targetAngle = CLOSED_ANGLE;

  // Init debouncers after pinMode
  s1In.begin(IR1);
  s2In.begin(IR2);
}

void loop() {
  unsigned long now = millis();

  // Update debounced sensors (true = detected)
  s1In.update();
  s2In.update();

  bool s1 = s1In.stable; // IR1 detected
  bool s2 = s2In.stable; // IR2 detected

  // ----------------- STATE MACHINE -----------------
  switch (systemState) {
    case IDLE:
      targetAngle = CLOSED_ANGLE;

      // Start a transaction based on who sees the car first
      if (s1 && !s2) {
        reachedEnd = false;
        reversalDetected = false;
        targetAngle = OPEN_ANGLE;
        enterState(ENTER_WAIT_IR2);
      }
      else if (s2 && !s1) {
        reachedEnd = false;
        reversalDetected = false;
        targetAngle = OPEN_ANGLE;
        enterState(EXIT_WAIT_IR1);
      }
      // If both trigger at same time in IDLE, just open and wait to clear
      else if (s1 && s2) {
        reachedEnd = true; // treat as "in the lane already"
        targetAngle = OPEN_ANGLE;
        enterState(ENTER_WAIT_CLEAR);
      }
      break;

    // ---------- ENTER FLOW: IR1 -> IR2 ----------
    case ENTER_WAIT_IR2:
      targetAngle = OPEN_ANGLE;

      // Commit if car reaches IR2
      if (s2) {
        reachedEnd = true;
        enterState(ENTER_WAIT_CLEAR);
      }

      // Cancel if car backs out (IR1 clears) before reaching IR2
      if (!reachedEnd && !s1 && !s2) {
        enterState(IDLE); // close
      }

      // Timeout protection
      if (!reachedEnd && (now - stateStartMs >= APPROACH_TIMEOUT_MS)) {
        enterState(IDLE); // close
      }
      break;

    case ENTER_WAIT_CLEAR:
      targetAngle = OPEN_ANGLE;

      // OPTIONAL: detect reversal after commit
      // Example: after reaching IR2, if IR1 triggers again while IR2 is not active,
      // that suggests the car turned back toward entry.
      if (reachedEnd && s1 && !s2) {
        reversalDetected = true;
        // You can trigger buzzer/LED here if you want
      }

      handleClearToClose();
      break;

    // ---------- EXIT FLOW: IR2 -> IR1 ----------
    case EXIT_WAIT_IR1:
      targetAngle = OPEN_ANGLE;

      // Commit if car reaches IR1
      if (s1) {
        reachedEnd = true;
        enterState(EXIT_WAIT_CLEAR);
      }

      // Cancel if car backs out (IR2 clears) before reaching IR1
      if (!reachedEnd && !s1 && !s2) {
        enterState(IDLE);
      }

      // Timeout protection
      if (!reachedEnd && (now - stateStartMs >= APPROACH_TIMEOUT_MS)) {
        enterState(IDLE);
      }
      break;

    case EXIT_WAIT_CLEAR:
      targetAngle = OPEN_ANGLE;

      // OPTIONAL reversal detect (turned back toward exit side)
      if (reachedEnd && s2 && !s1) {
        reversalDetected = true;
        // buzzer/LED hook here too
      }

      handleClearToClose();
      break;
  }

  // ----------------- SERVO SMOOTHING -----------------
  if (now - lastServoStepMs >= SERVO_STEP_MS) {
    lastServoStepMs = now;

    if (currentAngle < targetAngle) {
      currentAngle += SERVO_STEP_DEG;
      if (currentAngle > targetAngle) currentAngle = targetAngle;
      gate.write(currentAngle);
    } else if (currentAngle > targetAngle) {
      currentAngle -= SERVO_STEP_DEG;
      if (currentAngle < targetAngle) currentAngle = targetAngle;
      gate.write(currentAngle);
    }
  }
}
