// === Fire Alarm System ===
// If temperature >= threshold => buzzer ON + red LED blinks every 0.5s (non-blocking).

#define TEMP_PIN   A2
#define BUZZER_PIN 10
#define RED_LED    11

const float TEMP_THRESHOLD_C = 50.0; // change to what you want
const unsigned long BLINK_MS = 500;

unsigned long lastBlink = 0;
bool ledState = false;

float readTempC_LM35() {
  int raw = analogRead(TEMP_PIN);              // 0..1023
  float voltage = raw * (5.0 / 1023.0);        // volts
  float tempC = voltage * 100.0;               // LM35: 10mV per °C => 0.01V per °C => V*100
  return tempC;
}

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  Serial.begin(9600);
}

void loop() {
  float tempC = readTempC_LM35();
  bool fire = (tempC >= TEMP_THRESHOLD_C);

  if (fire) {
    tone(BUZZER_PIN, 1000); // 1kHz alarm tone

    unsigned long now = millis();
    if (now - lastBlink >= BLINK_MS) {
      lastBlink = now;
      ledState = !ledState;
      digitalWrite(RED_LED, ledState);
    }
  } else {
    noTone(BUZZER_PIN);
    digitalWrite(RED_LED, LOW);
    ledState = false;
  }

  Serial.print("TempC=");
  Serial.print(tempC);
  Serial.print("  Fire=");
  Serial.println(fire ? "YES" : "NO");

  delay(100);
}
