/*
  OLED + Gate + Spots (ESP32)
  ------------------------------------------------------------
  - IR1/IR2 detect a car at the gate (active LOW)
  - Servo opens/closes gate smoothly
  - OLED shows:
      * Live available spots
      * Spot occupancy (5 spots)
      * WELCOME / GOODBYE messages
  - Spot occupancy uses 5x LDRs (dark = occupied)

  Board: ESP32 DevKit (Arduino-ESP32)
*/

#include <Wire.h>
#include <ESP32Servo.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// -------------------- PINS --------------------
static const int PIN_IR_OUTSIDE = 14;
static const int PIN_IR_INSIDE  = 12;
static const int PIN_GATE_SERVO = 13;

static const int PIN_I2C_SDA = 21;
static const int PIN_I2C_SCL = 22;
static const uint8_t OLED_ADDR = 0x3C;

static const uint8_t MAX_SPOTS = 5;
static const int PIN_LDR_SPOTS[MAX_SPOTS] = { 34, 35, 32, 33, 36 };

// -------------------- OLED --------------------
static const int SCREEN_WIDTH = 128;
static const int SCREEN_HEIGHT = 64;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// -------------------- Gate / Servo --------------------
static const int GATE_CLOSED_ANGLE = 0;
static const int GATE_OPEN_ANGLE   = 90;

Servo gateServo;

static const unsigned long IR_DEBOUNCE_MS        = 30;
static const unsigned long CLEAR_HOLD_MS         = 400;
static const unsigned long APPROACH_TIMEOUT_MS   = 8000;

static const unsigned long SERVO_STEP_MS         = 8;
static const int SERVO_STEP_DEG                  = 3;

enum GateState : uint8_t { GATE_IDLE=0, GATE_ENTER_WAIT_IR2=1, GATE_EXIT_WAIT_IR1=2, GATE_WAIT_CLEAR=3 };
static GateState gateState = GATE_IDLE;

static int currentGateAngle = GATE_CLOSED_ANGLE;
static int targetGateAngle  = GATE_CLOSED_ANGLE;
static unsigned long lastServoStep = 0;

static unsigned long gateStateStart = 0;
static unsigned long clearStart = 0;

// debounced IR states (true = car detected)
static bool irOutside = false, irInside = false;
static bool irOutsideRawLast = false, irInsideRawLast = false;
static unsigned long irOutsideChange = 0, irInsideChange = 0;

// -------------------- Screen Modes --------------------
enum ScreenMode : uint8_t { SCREEN_STATUS=0, SCREEN_WELCOME=1, SCREEN_EXIT=2 };
static ScreenMode screenMode = SCREEN_STATUS;
static unsigned long messageUntil = 0;
static const unsigned long MESSAGE_MS = 1500;

static unsigned long lastOledUpdate = 0;
static const unsigned long OLED_MIN_UPDATE_MS = 200;

// change tracking (to avoid OLED flicker)
static int lastShownAvail = -999;
static int lastShownMode  = -999;
static bool lastShownOcc[MAX_SPOTS] = {0};

// -------------------- Spots (LDR occupancy) --------------------
static bool spotOccupied[MAX_SPOTS] = {0};
static int availableSpots = MAX_SPOTS;

static int spotBaseline[MAX_SPOTS] = {0};
static int spotThreshold[MAX_SPOTS] = {0};
static int spotSmooth[MAX_SPOTS] = {0};
static uint8_t spotConfirmCount[MAX_SPOTS] = {0};

static const int SPOT_MARGIN = 350;      // baseline - margin => occupied threshold (tune)
static const int SPOT_HYST   = 120;      // hysteresis band (tune)
static const uint8_t SPOT_CONFIRM_N = 3; // consecutive readings to confirm change
static const unsigned long SPOT_UPDATE_MS = 80;
static unsigned long lastSpotUpdate = 0;

// -------------------- Helpers --------------------
static bool readIrStable(int pin, bool &rawLast, unsigned long &tChange, bool &stable) {
  bool raw = (digitalRead(pin) == LOW); // active LOW
  unsigned long now = millis();

  if (raw != rawLast) {
    rawLast = raw;
    tChange = now;
  }
  if (now - tChange >= IR_DEBOUNCE_MS) stable = raw;
  return stable;
}

static bool bothIrClear() { return (!irOutside && !irInside); }

static int analogRead12(int pin) {
  return analogRead(pin); // 0..4095 on ESP32
}

// -------------------- Spots --------------------
static void calibrateSpots() {
  const int samples = 40;
  for (int i = 0; i < MAX_SPOTS; i++) {
    long sum = 0;
    for (int s = 0; s < samples; s++) {
      sum += analogRead12(PIN_LDR_SPOTS[i]);
      delay(5);
    }
    int avg = (int)(sum / samples);
    spotBaseline[i] = avg;
    spotThreshold[i] = max(0, avg - SPOT_MARGIN);
    spotSmooth[i] = avg;
    spotOccupied[i] = false;
    spotConfirmCount[i] = 0;
  }
}

static void updateSpots() {
  unsigned long now = millis();
  if (now - lastSpotUpdate < SPOT_UPDATE_MS) return;
  lastSpotUpdate = now;

  int occCount = 0;

  for (int i = 0; i < MAX_SPOTS; i++) {
    int raw = analogRead12(PIN_LDR_SPOTS[i]);
    spotSmooth[i] = (spotSmooth[i] * 3 + raw) / 4;

    bool candidate;
    if (spotOccupied[i]) {
      candidate = !(spotSmooth[i] > (spotThreshold[i] + SPOT_HYST));
    } else {
      candidate = (spotSmooth[i] < spotThreshold[i]);
    }

    if (candidate != spotOccupied[i]) {
      spotConfirmCount[i]++;
      if (spotConfirmCount[i] >= SPOT_CONFIRM_N) {
        spotOccupied[i] = candidate;
        spotConfirmCount[i] = 0;
      }
    } else {
      spotConfirmCount[i] = 0;
    }

    if (!spotOccupied[i]) {
      spotBaseline[i] = (spotBaseline[i] * 99 + spotSmooth[i]) / 100;
      spotThreshold[i] = max(0, spotBaseline[i] - SPOT_MARGIN);
    }

    if (spotOccupied[i]) occCount++;
  }

  availableSpots = (int)MAX_SPOTS - occCount;
}

// -------------------- OLED Drawing --------------------
static void drawStatus() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Spots: ");
  display.print(availableSpots);
  display.print("/");
  display.print(MAX_SPOTS);

  // Ground floor: 1 2 3
  display.setCursor(0, 16);
  display.print("G: ");
  for (int i = 0; i <= 2; i++) {
    display.print(i + 1);
    display.print(spotOccupied[i] ? "O " : ". ");
  }

  // First floor: 4 5
  display.setCursor(0, 32);
  display.print("F1: ");
  for (int i = 3; i <= 4; i++) {
    display.print(i + 1);
    display.print(spotOccupied[i] ? "O " : ". ");
  }

  display.setCursor(0, 56);
  display.print("O=full  .=free");

  display.display();
}

static void drawMessage(ScreenMode mode) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(2);
  if (mode == SCREEN_WELCOME) {
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

static void setScreenMode(ScreenMode mode, unsigned long durationMs = 0) {
  screenMode = mode;
  lastOledUpdate = 0;
  if (durationMs > 0) messageUntil = millis() + durationMs;
}

static void oledUpdateIfNeeded() {
  unsigned long now = millis();

  if (screenMode != SCREEN_STATUS && now > messageUntil) {
    screenMode = SCREEN_STATUS;
    lastOledUpdate = 0;
  }

  bool changed = false;
  if (availableSpots != lastShownAvail) changed = true;
  if ((int)screenMode != lastShownMode) changed = true;

  for (int i = 0; i < MAX_SPOTS; i++) {
    if (spotOccupied[i] != lastShownOcc[i]) changed = true;
  }

  if (!changed && (now - lastOledUpdate < OLED_MIN_UPDATE_MS)) return;

  lastOledUpdate = now;
  lastShownAvail = availableSpots;
  lastShownMode  = (int)screenMode;
  for (int i = 0; i < MAX_SPOTS; i++) lastShownOcc[i] = spotOccupied[i];

  if (screenMode == SCREEN_STATUS) drawStatus();
  else drawMessage(screenMode);
}

// -------------------- Gate Logic --------------------
static void showWelcome() { setScreenMode(SCREEN_WELCOME, MESSAGE_MS); }
static void showExit()    { setScreenMode(SCREEN_EXIT, MESSAGE_MS);   }

static void updateGateLogic() {
  unsigned long now = millis();

  if (gateState == GATE_IDLE) {
    targetGateAngle = GATE_CLOSED_ANGLE;

    if (irOutside && !irInside) {
      targetGateAngle = GATE_OPEN_ANGLE;
      gateState = GATE_ENTER_WAIT_IR2;
      gateStateStart = now;
    } else if (irInside && !irOutside) {
      targetGateAngle = GATE_OPEN_ANGLE;
      gateState = GATE_EXIT_WAIT_IR1;
      gateStateStart = now;
    } else if (irOutside && irInside) {
      targetGateAngle = GATE_OPEN_ANGLE;
      gateState = GATE_WAIT_CLEAR;
      clearStart = 0;
    }
  }
  else if (gateState == GATE_ENTER_WAIT_IR2) {
    targetGateAngle = GATE_OPEN_ANGLE;

    if (irInside) {
      showWelcome();
      gateState = GATE_WAIT_CLEAR;
      clearStart = 0;
    }

    if (bothIrClear()) gateState = GATE_IDLE;
    if (now - gateStateStart >= APPROACH_TIMEOUT_MS) gateState = GATE_IDLE;
  }
  else if (gateState == GATE_EXIT_WAIT_IR1) {
    targetGateAngle = GATE_OPEN_ANGLE;

    if (irOutside) {
      showExit();
      gateState = GATE_WAIT_CLEAR;
      clearStart = 0;
    }

    if (bothIrClear()) gateState = GATE_IDLE;
    if (now - gateStateStart >= APPROACH_TIMEOUT_MS) gateState = GATE_IDLE;
  }
  else { // GATE_WAIT_CLEAR
    targetGateAngle = GATE_OPEN_ANGLE;

    if (bothIrClear()) {
      if (clearStart == 0) clearStart = now;
      if (now - clearStart >= CLEAR_HOLD_MS) gateState = GATE_IDLE;
    } else {
      clearStart = 0;
    }
  }
}

static void updateServoSmoothing() {
  unsigned long now = millis();
  if (now - lastServoStep < SERVO_STEP_MS) return;
  lastServoStep = now;

  if (currentGateAngle < targetGateAngle) {
    currentGateAngle += SERVO_STEP_DEG;
    if (currentGateAngle > targetGateAngle) currentGateAngle = targetGateAngle;
    gateServo.write(currentGateAngle);
  } else if (currentGateAngle > targetGateAngle) {
    currentGateAngle -= SERVO_STEP_DEG;
    if (currentGateAngle < targetGateAngle) currentGateAngle = targetGateAngle;
    gateServo.write(currentGateAngle);
  }
}

// -------------------- Setup / Loop --------------------
void setup() {
  Serial.begin(115200);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    while (true) { delay(100); }
  }

  pinMode(PIN_IR_OUTSIDE, INPUT_PULLUP);
  pinMode(PIN_IR_INSIDE,  INPUT_PULLUP);

  irOutsideRawLast = (digitalRead(PIN_IR_OUTSIDE) == LOW);
  irInsideRawLast  = (digitalRead(PIN_IR_INSIDE)  == LOW);
  irOutside = irOutsideRawLast;
  irInside  = irInsideRawLast;
  irOutsideChange = irInsideChange = millis();

  gateServo.attach(PIN_GATE_SERVO);
  gateServo.write(GATE_CLOSED_ANGLE);

  for (int i = 0; i < MAX_SPOTS; i++) pinMode(PIN_LDR_SPOTS[i], INPUT);

  calibrateSpots();
  updateSpots();

  setScreenMode(SCREEN_STATUS);
  oledUpdateIfNeeded();

  Serial.println("OLED + Gate + Spots boot OK");
}

void loop() {
  readIrStable(PIN_IR_OUTSIDE, irOutsideRawLast, irOutsideChange, irOutside);
  readIrStable(PIN_IR_INSIDE,  irInsideRawLast,  irInsideChange,  irInside);

  updateSpots();
  updateGateLogic();
  updateServoSmoothing();
  oledUpdateIfNeeded();

  // Debug: print spot values every 800ms (for threshold tuning)
  /*
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 800) {
    lastPrint = millis();
    for (int i = 0; i < MAX_SPOTS; i++) {
      Serial.print(analogRead12(PIN_LDR_SPOTS[i]));
      Serial.print(i == MAX_SPOTS-1 ? "\n" : "  ");
    }
  }
  */
}
