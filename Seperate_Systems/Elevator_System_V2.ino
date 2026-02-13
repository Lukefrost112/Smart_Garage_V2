/*
  Elevator System (ESP32)
  ------------------------------------------------------------
  Hold-button control:
   - Hold UP  -> motor UP
   - Hold DOWN-> motor DOWN
   - Release  -> stop

  Uses an H-Bridge / L298N style driver:
    IN1/IN2 set direction, EN pin tied HIGH (or PWM if you add it)

  Buttons use INPUT_PULLUP:
    - One side of button to GPIO
    - Other side to GND
    - Pressed = LOW

  Board: ESP32 DevKit (Arduino-ESP32)
*/

static const int PIN_BTN_UP   = 4;
static const int PIN_BTN_DOWN = 5;

static const int PIN_MOTOR_IN1 = 19;
static const int PIN_MOTOR_IN2 = 23;

static const unsigned long DEBOUNCE_MS = 30;

struct DebouncedButton {
  int pin;
  bool rawLast;
  bool stable;           // true = pressed
  unsigned long tChange;
};

static DebouncedButton btnUp   = { PIN_BTN_UP,   false, false, 0 };
static DebouncedButton btnDown = { PIN_BTN_DOWN, false, false, 0 };

static bool readPressed(DebouncedButton &b) {
  bool rawPressed = (digitalRead(b.pin) == LOW);
  unsigned long now = millis();

  if (rawPressed != b.rawLast) {
    b.rawLast = rawPressed;
    b.tChange = now;
  }
  if (now - b.tChange >= DEBOUNCE_MS) {
    b.stable = rawPressed;
  }
  return b.stable;
}

static void motorStop() {
  digitalWrite(PIN_MOTOR_IN1, LOW);
  digitalWrite(PIN_MOTOR_IN2, LOW);
}

static void motorUp() {
  digitalWrite(PIN_MOTOR_IN1, HIGH);
  digitalWrite(PIN_MOTOR_IN2, LOW);
}

static void motorDown() {
  digitalWrite(PIN_MOTOR_IN1, LOW);
  digitalWrite(PIN_MOTOR_IN2, HIGH);
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);

  pinMode(PIN_MOTOR_IN1, OUTPUT);
  pinMode(PIN_MOTOR_IN2, OUTPUT);
  motorStop();

  Serial.println("Elevator System boot OK");
}

void loop() {
  bool up = readPressed(btnUp);
  bool down = readPressed(btnDown);

  if (up && !down) motorUp();
  else if (down && !up) motorDown();
  else motorStop();

  // Debug
  // Serial.printf("UP=%d DOWN=%d\n", up, down);
}
