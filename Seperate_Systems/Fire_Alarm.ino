/*
  Fire / Flame Alarm (ESP32) - DIGITAL FLAME SENSOR (DO)
  ------------------------------------------------------------
  - Digital flame sensor module output (DO)
  - If flame detected:
      * Buzzer ON (1kHz)
      * Red LED blinks (non-blocking)

  Board: ESP32 DevKit (Arduino-ESP32)

  Notes:
  - Many flame modules: DO is LOW when flame detected.
    If yours is HIGH on flame, set FLAME_ACTIVE_LOW = false.
*/

static const int PIN_FLAME_DO = 39;   // digital flame sensor DO (input-only pin)
static const int PIN_BUZZER   = 15;   // PWM buzzer
static const int PIN_RED_LED  = 2;    // warning LED (often onboard)

static const bool FLAME_ACTIVE_LOW = true;

// Debounce/confirm (prevents random noise triggering)
static const unsigned long SAMPLE_MS = 50;
static const uint8_t FLAME_CONFIRM_N = 3; // require N consecutive "flame" readings

static const unsigned long BLINK_MS = 500;

// LEDC buzzer PWM
static const int LEDC_CH_BUZZER = 0;
static const int BUZZ_FREQ_HZ   = 1000;
static const int PWM_RES_BITS   = 8;   // 0..255

static unsigned long lastSample = 0;
static uint8_t flameCount = 0;
static bool fireActive = false;

static unsigned long lastBlink = 0;
static bool ledState = false;

static void buzzerOn()  { ledcWrite(LEDC_CH_BUZZER, 128); } // 50% duty
static void buzzerOff() { ledcWrite(LEDC_CH_BUZZER, 0);   }

static bool rawFlameDetected() {
  int v = digitalRead(PIN_FLAME_DO);
  bool detected = FLAME_ACTIVE_LOW ? (v == LOW) : (v == HIGH);
  return detected;
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_FLAME_DO, INPUT);   // DO is usually push-pull from module comparator
  pinMode(PIN_RED_LED, OUTPUT);

  ledcSetup(LEDC_CH_BUZZER, BUZZ_FREQ_HZ, PWM_RES_BITS);
  ledcAttachPin(PIN_BUZZER, LEDC_CH_BUZZER);

  buzzerOff();
  digitalWrite(PIN_RED_LED, LOW);

  Serial.println("Fire Alarm (Digital Flame DO) boot OK");
}

void loop() {
  unsigned long now = millis();

  if (now - lastSample >= SAMPLE_MS) {
    lastSample = now;

    bool flame = rawFlameDetected();

    if (flame) {
      if (flameCount < 255) flameCount++;
    } else {
      flameCount = 0;
    }

    fireActive = (flameCount >= FLAME_CONFIRM_N);

    if (!fireActive) {
      buzzerOff();
      digitalWrite(PIN_RED_LED, LOW);
      ledState = false;
    }
  }

  if (fireActive) {
    buzzerOn();

    if (now - lastBlink >= BLINK_MS) {
      lastBlink = now;
      ledState = !ledState;
      digitalWrite(PIN_RED_LED, ledState ? HIGH : LOW);
    }
  }

  // Debug (optional)
  /*
  static unsigned long lastPrint = 0;
  if (now - lastPrint > 500) {
    lastPrint = now;
    Serial.print("raw="); Serial.print(rawFlameDetected() ? "FLAME" : "----");
    Serial.print("  count="); Serial.print(flameCount);
    Serial.print("  active="); Serial.println(fireActive ? "YES" : "NO");
  }
  */
}
