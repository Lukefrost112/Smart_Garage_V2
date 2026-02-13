/*
  Smart Lighting System (ESP32)
  ------------------------------------------------------------
  - POT controls brightness of 3 LEDs (PWM)
  - Ambient LDR: if bright, LEDs turn OFF to save power

  Board: ESP32 DevKit (Arduino-ESP32)

  Notes:
  - Uses ADC2 pins for POT/LDR. If you enable Wi-Fi, ADC2 reads may break.
*/

static const int PIN_POT_BRIGHTNESS = 25; // ADC2
static const int PIN_LDR_AMBIENT    = 26; // ADC2

static const int PIN_LED_1          = 16; // PWM
static const int PIN_LED_2          = 17; // PWM
static const int PIN_LED_3          = 18; // PWM

// 12-bit ADC (0..4095): ~50% is 2048. Tune this for your divider/lighting.
static int ambientLdrThreshold = 2200;

// LEDC PWM setup
static const int LEDC_CH_LED1 = 0;
static const int LEDC_CH_LED2 = 1;
static const int LEDC_CH_LED3 = 2;

static const int LEDC_FREQ_HZ = 5000;
static const int LEDC_RES_BITS = 8; // 0..255

static const unsigned long UPDATE_MS = 60;
static unsigned long lastUpdate = 0;

static int analogRead12(int pin) { return analogRead(pin); }

static void setAll(uint8_t duty) {
  ledcWrite(LEDC_CH_LED1, duty);
  ledcWrite(LEDC_CH_LED2, duty);
  ledcWrite(LEDC_CH_LED3, duty);
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_POT_BRIGHTNESS, INPUT);
  pinMode(PIN_LDR_AMBIENT, INPUT);

  ledcSetup(LEDC_CH_LED1, LEDC_FREQ_HZ, LEDC_RES_BITS);
  ledcSetup(LEDC_CH_LED2, LEDC_FREQ_HZ, LEDC_RES_BITS);
  ledcSetup(LEDC_CH_LED3, LEDC_FREQ_HZ, LEDC_RES_BITS);

  ledcAttachPin(PIN_LED_1, LEDC_CH_LED1);
  ledcAttachPin(PIN_LED_2, LEDC_CH_LED2);
  ledcAttachPin(PIN_LED_3, LEDC_CH_LED3);

  setAll(0);

  Serial.println("Smart Lighting boot OK");
}

void loop() {
  unsigned long now = millis();
  if (now - lastUpdate < UPDATE_MS) return;
  lastUpdate = now;

  int pot = analogRead12(PIN_POT_BRIGHTNESS); // 0..4095
  int ldr = analogRead12(PIN_LDR_AMBIENT);    // 0..4095

  uint8_t brightness = (uint8_t)map(pot, 0, 4095, 0, 255);
  bool isBright = (ldr > ambientLdrThreshold);

  if (isBright) setAll(0);
  else setAll(brightness);

  // Debug
  Serial.print("POT="); Serial.print(pot);
  Serial.print("  LDR="); Serial.print(ldr);
  Serial.print("  Brightness="); Serial.print((int)brightness);
  Serial.print("  LEDs="); Serial.println(isBright ? "OFF" : "ON");
}
