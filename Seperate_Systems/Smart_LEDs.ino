// === Smart Lighting System ===
// POT controls brightness of 3 LEDs.
// If LDR detects >50% light, LEDs turn OFF to save power.

#define POT_PIN   A0
#define LDR_PIN   A1

#define LED1_PIN  5
#define LED2_PIN  6
#define LED3_PIN  9

// Adjust this based on your LDR divider and testing.
// 50% "light" is not universal on analogRead.
// Start with ~512 and tweak.
const int LDR_THRESHOLD = 512;

void setup() {
  pinMode(LED1_PIN, OUTPUT);
  pinMode(LED2_PIN, OUTPUT);
  pinMode(LED3_PIN, OUTPUT);

  Serial.begin(9600);
}

void loop() {
  int pot = analogRead(POT_PIN);        // 0..1023
  int ldr = analogRead(LDR_PIN);        // 0..1023

  int brightness = map(pot, 0, 1023, 0, 255);

  bool isBrightOutside = (ldr > LDR_THRESHOLD);

  if (isBrightOutside) {
    analogWrite(LED1_PIN, 0);
    analogWrite(LED2_PIN, 0);
    analogWrite(LED3_PIN, 0);
  } else {
    analogWrite(LED1_PIN, brightness);
    analogWrite(LED2_PIN, brightness);
    analogWrite(LED3_PIN, brightness);
  }

  // Debug
  Serial.print("POT=");
  Serial.print(pot);
  Serial.print("  LDR=");
  Serial.print(ldr);
  Serial.print("  Brightness=");
  Serial.print(brightness);
  Serial.print("  LEDs=");
  Serial.println(isBrightOutside ? "OFF" : "ON");

  delay(50);
}
