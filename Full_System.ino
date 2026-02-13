/*
  Smart Garage - Full System (ESP32) - DIGITAL FLAME SENSOR VERSION
  ------------------------------------------------------------
  Combines:
   1) Gate + IR entry/exit logic + Servo
   2) OLED UI (welcome/exit + live spots)
   3) Parking spot occupancy using 5x LDRs (dark = occupied)
   4) Smart lighting: POT brightness + ambient LDR auto-off
   5) Flame alarm: DIGITAL DO + buzzer + red LED + OLED alarm
   6) Elevator motor: hold button UP/DOWN to move

  Board: ESP32 DevKit (Arduino-ESP32)
*/

#include <Wire.h>
#include <ESP32Servo.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// -------------------- PIN MAP (NO CONFLICTS) --------------------
// Gate / IR
static const int PIN_IR_OUTSIDE = 14;   // IR sensor outside (active LOW)
static const int PIN_IR_INSIDE  = 12;   // IR sensor inside  (active LOW)
static const int PIN_GATE_SERVO = 13;   // Servo signal

// OLED (I2C default for ESP32)
static const int PIN_I2C_SDA = 21;
static const int PIN_I2C_SCL = 22;
static const uint8_t OLED_ADDR = 0x3C;

// Parking spots (5 LDRs) - ADC1 pins recommended
static const uint8_t MAX_SPOTS = 5;
static const int PIN_LDR_SPOTS[MAX_SPOTS] = { 34, 35, 32, 33, 36 }; // dark = occupied

// Smart lighting
static const int PIN_POT_BRIGHTNESS   = 25;
static const int PIN_LDR_AMBIENT      = 26;
static const int PIN_LED_1            = 16; // PWM
static const int PIN_LED_2            = 17; // PWM
static const int PIN_LED_3            = 18; // PWM

// Flame alarm (digital DO)
static const int PIN_FLAME_DO         = 39; // input-only, good for sensors
static const bool FLAME_ACTIVE_LOW    = true; // flip if your module is active HIGH

static const int PIN_FIRE_BUZZER      = 15; // PWM capable
static const int PIN_FIRE_RED_LED     = 2;  // often onboard LED

// Elevator motor (two direction pins, EN tied HIGH on driver)
static const int PIN_ELEV_IN1         = 19;
static const int PIN_ELEV_IN2         = 23;

// Elevator buttons (use INPUT_PULLUP, pressed = LOW)
static const int PIN_BTN_UP           = 4;
static const int PIN_BTN_DOWN         = 5;

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
enum ScreenMode : uint8_t { SCREEN_STATUS=0, SCREEN_WELCOME=1, SCREEN_EXIT=2, SCREEN_FIRE=3 };
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

// Calibration + filtering
static int spotBaseline[MAX_SPOTS] = {0};      // "bright empty" baseline
static int spotThreshold[MAX_SPOTS] = {0};     // below => occupied
static int spotSmooth[MAX_SPOTS] = {0};        // EMA filtered reading
static uint8_t spotConfirmCount[MAX_SPOTS] = {0};

static const int SPOT_MARGIN = 350;            // baseline - margin => occupied threshold (tune)
static const int SPOT_HYST   = 120;            // hysteresis band (tune)
static const uint8_t SPOT_CONFIRM_N = 3;       // consecutive readings to confirm change
static const unsigned long SPOT_UPDATE_MS = 80;
static unsigned long lastSpotUpdate = 0;

// -------------------- Smart Lighting --------------------
static const unsigned long LIGHT_UPDATE_MS = 60;
static unsigned long lastLightUpdate = 0;

// Tune for your divider/lighting
static int ambientLdrThreshold = 2200; // above => bright => LEDs OFF

// LEDC (PWM) channels
static const int LEDC_CH_LED1   = 0;
static const int LEDC_CH_LED2   = 1;
static const int LEDC_CH_LED3   = 2;
static const int LEDC_CH_BUZZER = 3;

static const int LEDC_FREQ_LED   = 5000;
static const int LEDC_RES_BITS   = 8;      // 0..255
static const int LEDC_FREQ_BUZZ  = 1000;   // 1kHz beep

// -------------------- Flame Alarm --------------------
static const unsigned long FIRE_SAMPLE_MS = 50;
static unsigned long lastFireSample = 0;

static const uint8_t FLAME_CONFIRM_N = 3;  // require N consecutive flame reads
static uint8_t flameCount = 0;

static const unsigned long FIRE_BLINK_MS = 500;
static unsigned long lastFireBlink = 0;
static bool fireLedState = false;

static bool fireActive = false;

// -------------------- Elevator --------------------
static const unsigned long BTN_DEBOUNCE_MS = 30;
struct DebouncedButton {
  int pin;
  bool rawLast;
  bool stable;           // true = pressed
  unsigned long tChange;
};

static DebouncedButton btnUp   = { PIN_BTN_UP,   false, false, 0 };
static DebouncedButton btnDown = { PIN_BTN_DOWN, false, false, 0 };

static bool readButtonPressed(DebouncedButton &b) {
  bool rawPressed = (digitalRead(b.pin) == LOW); // PULLUP
  unsigned long now = millis();

  if (rawPressed != b.rawLast) {
    b.rawLast = rawPressed;
    b.tChange = now;
  }
  if (now - b.tChange >= BTN_DEBOUNCE_MS) {
    b.stable = rawPressed;
  }
  return b.stable;
}

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
  return analogRead(pin); // 0..4095
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

    // EMA smoothing (75% prev, 25% new)
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

    // Slowly adapt baseline when EMPTY (helps with ambient changes)
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

  if (fireActive) {
    display.setCursor(88, 0);
    display.print("FIRE!");
  }

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

  if (mode == SCREEN_FIRE) {
    display.setTextSize(2);
    display.setCursor(18, 18);
    display.print("ALARM");
  } else {
    display.setTextSize(2);
    if (mode == SCREEN_WELCOME) {
      display.setCursor(10, 18);
      display.print("WELCOME");
    } else {
      display.setCursor(22, 18);
      display.print("GOODBYE");
    }
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

  // Timed messages return to status (unless FIRE)
  if (screenMode != SCREEN_STATUS && screenMode != SCREEN_FIRE) {
    if (now > messageUntil) {
      screenMode = SCREEN_STATUS;
      lastOledUpdate = 0;
    }
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

  // FIRE override: gate stays open
  if (fireActive) {
    targetGateAngle = GATE_OPEN_ANGLE;
    gateState = GATE_WAIT_CLEAR;
    return;
  }

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

// -------------------- Smart Lighting --------------------
static void setupPwmChannels() {
  ledcSetup(LEDC_CH_LED1, LEDC_FREQ_LED, LEDC_RES_BITS);
  ledcSetup(LEDC_CH_LED2, LEDC_FREQ_LED, LEDC_RES_BITS);
  ledcSetup(LEDC_CH_LED3, LEDC_FREQ_LED, LEDC_RES_BITS);

  ledcAttachPin(PIN_LED_1, LEDC_CH_LED1);
  ledcAttachPin(PIN_LED_2, LEDC_CH_LED2);
  ledcAttachPin(PIN_LED_3, LEDC_CH_LED3);

  ledcSetup(LEDC_CH_BUZZER, LEDC_FREQ_BUZZ, LEDC_RES_BITS);
  ledcAttachPin(PIN_FIRE_BUZZER, LEDC_CH_BUZZER);

  ledcWrite(LEDC_CH_LED1, 0);
  ledcWrite(LEDC_CH_LED2, 0);
  ledcWrite(LEDC_CH_LED3, 0);
  ledcWrite(LEDC_CH_BUZZER, 0);
}

static void setAllLights(uint8_t duty) {
  ledcWrite(LEDC_CH_LED1, duty);
  ledcWrite(LEDC_CH_LED2, duty);
  ledcWrite(LEDC_CH_LED3, duty);
}

static void updateSmartLighting() {
  unsigned long now = millis();
  if (now - lastLightUpdate < LIGHT_UPDATE_MS) return;
  lastLightUpdate = now;

  int pot = analogRead12(PIN_POT_BRIGHTNESS);
  int amb = analogRead12(PIN_LDR_AMBIENT);

  uint8_t brightness = (uint8_t)map(pot, 0, 4095, 0, 255);
  bool isBrightOutside = (amb > ambientLdrThreshold);

  if (isBrightOutside) setAllLights(0);
  else setAllLights(brightness);
}

// -------------------- Flame Alarm --------------------
static bool rawFlameDetected() {
  int v = digitalRead(PIN_FLAME_DO);
  return FLAME_ACTIVE_LOW ? (v == LOW) : (v == HIGH);
}

static void buzzerOn()  { ledcWrite(LEDC_CH_BUZZER, 128); } // 50% duty
static void buzzerOff() { ledcWrite(LEDC_CH_BUZZER, 0);   }

static void updateFlameAlarm() {
  unsigned long now = millis();
  if (now - lastFireSample < FIRE_SAMPLE_MS) return;
  lastFireSample = now;

  bool flame = rawFlameDetected();

  if (flame) {
    if (flameCount < 255) flameCount++;
  } else {
    flameCount = 0;
  }

  bool newFire = (flameCount >= FLAME_CONFIRM_N);

  if (newFire) {
    fireActive = true;
    buzzerOn();

    if (now - lastFireBlink >= FIRE_BLINK_MS) {
      lastFireBlink = now;
      fireLedState = !fireLedState;
      digitalWrite(PIN_FIRE_RED_LED, fireLedState ? HIGH : LOW);
    }

    if (screenMode != SCREEN_FIRE) {
      setScreenMode(SCREEN_FIRE);
    }
  } else {
    if (fireActive) {
      // just turned OFF
      buzzerOff();
      digitalWrite(PIN_FIRE_RED_LED, LOW);
      fireLedState = false;

      if (screenMode == SCREEN_FIRE) {
        setScreenMode(SCREEN_STATUS);
      }
    }
    fireActive = false;
  }
}

// -------------------- Elevator --------------------
static void elevatorStop() {
  digitalWrite(PIN_ELEV_IN1, LOW);
  digitalWrite(PIN_ELEV_IN2, LOW);
}

static void elevatorUp() {
  digitalWrite(PIN_ELEV_IN1, HIGH);
  digitalWrite(PIN_ELEV_IN2, LOW);
}

static void elevatorDown() {
  digitalWrite(PIN_ELEV_IN1, LOW);
  digitalWrite(PIN_ELEV_IN2, HIGH);
}

static void updateElevator() {
  // Fire override: stop elevator motor
  if (fireActive) {
    elevatorStop();
    return;
  }

  bool upPressed = readButtonPressed(btnUp);
  bool downPressed = readButtonPressed(btnDown);

  if (upPressed && !downPressed) elevatorUp();
  else if (downPressed && !upPressed) elevatorDown();
  else elevatorStop();
}

// -------------------- Setup / Loop --------------------
void setup() {
  Serial.begin(115200);

  // I2C
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    while (true) { delay(100); }
  }

  // IR sensors
  pinMode(PIN_IR_OUTSIDE, INPUT_PULLUP);
  pinMode(PIN_IR_INSIDE,  INPUT_PULLUP);

  irOutsideRawLast = (digitalRead(PIN_IR_OUTSIDE) == LOW);
  irInsideRawLast  = (digitalRead(PIN_IR_INSIDE)  == LOW);
  irOutside = irOutsideRawLast;
  irInside  = irInsideRawLast;
  irOutsideChange = irInsideChange = millis();

  // Servo
  gateServo.attach(PIN_GATE_SERVO);
  gateServo.write(GATE_CLOSED_ANGLE);
  currentGateAngle = GATE_CLOSED_ANGLE;
  targetGateAngle  = GATE_CLOSED_ANGLE;

  // Spot pins
  for (int i = 0; i < MAX_SPOTS; i++) pinMode(PIN_LDR_SPOTS[i], INPUT);

  // Smart lighting pins
  pinMode(PIN_POT_BRIGHTNESS, INPUT);
  pinMode(PIN_LDR_AMBIENT, INPUT);
  pinMode(PIN_LED_1, OUTPUT);
  pinMode(PIN_LED_2, OUTPUT);
  pinMode(PIN_LED_3, OUTPUT);

  // Flame + alarm outputs
  pinMode(PIN_FLAME_DO, INPUT);
  pinMode(PIN_FIRE_RED_LED, OUTPUT);
  pinMode(PIN_FIRE_BUZZER, OUTPUT);
  digitalWrite(PIN_FIRE_RED_LED, LOW);

  // Elevator
  pinMode(PIN_ELEV_IN1, OUTPUT);
  pinMode(PIN_ELEV_IN2, OUTPUT);
  elevatorStop();

  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);

  // PWM setup
  setupPwmChannels();

  // Spots calibration
  calibrateSpots();
  updateSpots();

  // Initial UI
  setScreenMode(SCREEN_STATUS);
  oledUpdateIfNeeded();

  Serial.println("Smart Garage (Digital Flame DO) boot OK");
  Serial.println("If flame logic is inverted, flip FLAME_ACTIVE_LOW.");
}

void loop() {
  // Read debounced IR sensors
  readIrStable(PIN_IR_OUTSIDE, irOutsideRawLast, irOutsideChange, irOutside);
  readIrStable(PIN_IR_INSIDE,  irInsideRawLast,  irInsideChange,  irInside);

  // Update subsystems (non-blocking)
  updateSpots();
  updateFlameAlarm();
  updateGateLogic();
  updateServoSmoothing();
  updateSmartLighting();
  updateElevator();
  oledUpdateIfNeeded();

  // Optional debug
  /*
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 800) {
    lastPrint = millis();
    Serial.print("Fire="); Serial.print(fireActive ? "YES" : "NO");
    Serial.print(" Spots="); Serial.print(availableSpots); Serial.print("/"); Serial.print(MAX_SPOTS);
    Serial.print(" IR(out,in)="); Serial.print(irOutside); Serial.print(","); Serial.println(irInside);
  }
  */
}
