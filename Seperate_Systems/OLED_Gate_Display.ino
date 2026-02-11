#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define IR1 14
#define IR2 12
#define MOTOR_PIN 13

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ===== FAKE SPOT DATA (TEMPORARY) =====
// 1 = occupied, 0 = free
bool fakeOccupied[5] = {
  0,  // spot 1 (ground)
  1,  // spot 2 (ground)
  0,  // spot 3 (ground)
  1,  // spot 4 (first floor)
  0   // spot 5 (first floor)
};

// -------- Spots (5 total) --------
// Put your ADC pins here (ESP32 ADC pins like 32,33,34,35,36,39)
const int LDR_PINS[5] = { 34, 35, 32, 33, 36 };

// Thresholds: below = car (darker), above = empty (brighter)
// You must tune these numbers from Serial (I left reasonable placeholders).
int LDR_THRESH[5] = { 1800, 1800, 1800, 1800, 1800 };

bool occupied[5] = {0,0,0,0,0};
int availableSpots = 5;

const int MAX_SPOTS = 5;

// ---- Gate angles ----
const int CLOSED_ANGLE = 0;
const int OPEN_ANGLE   = 90;

// ---- Timing ----
const unsigned long DEBOUNCE_MS = 30;
const unsigned long CLEAR_HOLD_MS = 400;
const unsigned long APPROACH_TIMEOUT_MS = 8000;

const unsigned long MESSAGE_MS = 1500;
unsigned long messageUntil = 0;
int screenMode = 0; // 0=status, 1=welcome, 2=exit

// OLED throttling
unsigned long lastOledUpdate = 0;
const unsigned long OLED_MIN_UPDATE_MS = 200;

// change tracking
int lastShownAvail = -999;
int lastShownMode  = -999;
bool lastShownOcc[5] = {0,0,0,0,0};

// Servo + logic
Servo gate;
int systemState = 0; // 0 idle, 1 enter wait IR2, 2 exit wait IR1, 3 wait clear
unsigned long stateStart = 0;
unsigned long clearStart = 0;

// servo smoothing
int currentAngle = CLOSED_ANGLE;
int targetAngle  = CLOSED_ANGLE;
unsigned long lastServoStep = 0;
const unsigned long SERVO_STEP_MS = 8;
const int SERVO_STEP_DEG = 3;

// debounce
bool s1 = false, s2 = false;
bool s1RawLast = false, s2RawLast = false;
unsigned long s1Change = 0, s2Change = 0;

bool readStable(int pin, bool &rawLast, unsigned long &tChange, bool &stable) {
  bool raw = (digitalRead(pin) == LOW); // LOW = car (with PULLUP)
  unsigned long now = millis();

  if (raw != rawLast) {
    rawLast = raw;
    tChange = now;
  }
  if (now - tChange >= DEBOUNCE_MS) stable = raw;
  return stable;
}

bool bothClear() { return (!s1 && !s2); }
/*
// -------- Spots update --------
void updateSpots() {
  int occCount = 0;

  for (int i = 0; i < 5; i++) {
    int v = analogRead(LDR_PINS[i]);

    // simple: darker -> occupied
    occupied[i] = (v < LDR_THRESH[i]);

    if (occupied[i]) occCount++;
  }

  availableSpots = MAX_SPOTS - occCount;
}*/

void updateSpots() {
  int occCount = 0;

  for (int i = 0; i < 5; i++) {
    occupied[i] = fakeOccupied[i];  // <-- FAKE DATA
    if (occupied[i]) occCount++;
  }

  availableSpots = MAX_SPOTS - occCount;
}

// -------- OLED --------
void drawStatus() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // header
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Spots: ");
  display.print(availableSpots);
  display.print("/");
  display.print(MAX_SPOTS);

  // Ground floor: 1 2 3
  display.setCursor(0, 16);
  display.print("G: ");
  for (int i = 0; i < 2; i++) {
    display.print(i + 1);
    display.print(occupied[i] ? "O " : ". ");
  }

  // First floor: 4 5
  display.setCursor(0, 32);
  display.print("F1: ");
  for (int i = 2; i < 5; i++) {
    display.print(i + 1);
    display.print(occupied[i] ? "O " : ". ");
  }

  // legend
  display.setCursor(0, 56);
  display.print("O=full  .=free");

  display.display();
}

void drawMessage(int type) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(2);
  if (type == 1) {
    display.setCursor(10, 18);
    display.print("WELCOME");
  } else {
    display.setCursor(22, 18);
    display.print("GOODBYE");
  }

  display.setTextSize(1);
  display.setCursor(0, 52);
  display.print("Spots: ");
  display.print(availableSpots);
  display.print("/");
  display.print(MAX_SPOTS);

  display.display();
}

void setScreenMode(int mode) {
  screenMode = mode;
  lastOledUpdate = 0;
}

void oledUpdateIfNeeded() {
  unsigned long now = millis();

  if (screenMode != 0 && now > messageUntil) {
    screenMode = 0;
    lastOledUpdate = 0;
  }

  bool changed = false;
  if (availableSpots != lastShownAvail) changed = true;
  if (screenMode != lastShownMode) changed = true;

  for (int i = 0; i < 5; i++) {
    if (occupied[i] != lastShownOcc[i]) changed = true;
  }

  if (!changed && (now - lastOledUpdate < OLED_MIN_UPDATE_MS)) return;

  lastOledUpdate = now;
  lastShownAvail = availableSpots;
  lastShownMode  = screenMode;
  for (int i = 0; i < 5; i++) lastShownOcc[i] = occupied[i];

  if (screenMode == 0) drawStatus();
  else drawMessage(screenMode);
}

void showWelcome() {
  setScreenMode(1);
  messageUntil = millis() + MESSAGE_MS;
}

void showExit() {
  setScreenMode(2);
  messageUntil = millis() + MESSAGE_MS;
}
// ----------------------

void setup() {
  Serial.begin(115200);

  pinMode(IR1, INPUT_PULLUP);
  pinMode(IR2, INPUT_PULLUP);

  gate.attach(MOTOR_PIN);
  gate.write(CLOSED_ANGLE);

  currentAngle = CLOSED_ANGLE;
  targetAngle  = CLOSED_ANGLE;

  s1RawLast = (digitalRead(IR1) == LOW);
  s2RawLast = (digitalRead(IR2) == LOW);
  s1 = s1RawLast;
  s2 = s2RawLast;
  s1Change = s2Change = millis();

  // ADC pins
  for (int i = 0; i < 5; i++) {
    pinMode(LDR_PINS[i], INPUT);
  }

  Wire.begin();
  Wire.setClock(400000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (true) {}
  }

  updateSpots();
  setScreenMode(0);
  oledUpdateIfNeeded();

  Serial.println("Boot OK");
}

void loop() {
  unsigned long now = millis();

  // sensors
  readStable(IR1, s1RawLast, s1Change, s1);
  readStable(IR2, s2RawLast, s2Change, s2);

  // spots
  updateSpots();

  // gate logic
  if (systemState == 0) {
    targetAngle = CLOSED_ANGLE;

    if (s1 && !s2) {
      targetAngle = OPEN_ANGLE;
      systemState = 1;
      stateStart = now;
    } else if (s2 && !s1) {
      targetAngle = OPEN_ANGLE;
      systemState = 2;
      stateStart = now;
    } else if (s1 && s2) {
      targetAngle = OPEN_ANGLE;
      systemState = 3;
      clearStart = 0;
    }
  }

  else if (systemState == 1) { // enter wait IR2
    targetAngle = OPEN_ANGLE;

    if (s2) {
      showWelcome();
      systemState = 3;
      clearStart = 0;
    }

    if (bothClear()) systemState = 0;
    if (now - stateStart >= APPROACH_TIMEOUT_MS) systemState = 0;
  }

  else if (systemState == 2) { // exit wait IR1
    targetAngle = OPEN_ANGLE;

    if (s1) {
      showExit();
      systemState = 3;
      clearStart = 0;
    }

    if (bothClear()) systemState = 0;
    if (now - stateStart >= APPROACH_TIMEOUT_MS) systemState = 0;
  }

  else if (systemState == 3) { // wait clear
    targetAngle = OPEN_ANGLE;

    if (bothClear()) {
      if (clearStart == 0) clearStart = now;
      if (now - clearStart >= CLEAR_HOLD_MS) systemState = 0;
    } else {
      clearStart = 0;
    }
  }

  // servo smoothing
  if (now - lastServoStep >= SERVO_STEP_MS) {
    lastServoStep = now;

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

  // oled
  oledUpdateIfNeeded();

  // optional: print LDR values for tuning
  /*
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 500) {
    lastPrint = millis();
    for (int i = 0; i < 5; i++) {
      Serial.print(analogRead(LDR_PINS[i]));
      Serial.print(i == 4 ? "\n" : "  ");
    }
  }
  */
}
