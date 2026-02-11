#include <Servo.h>

#define IR1 26
#define IR2 27
#define MOTOR_PIN 13

Servo gate;

int systemState = 0;      // 0=idle, 1=enter, 2=exit
bool reachedEnd = false;  // reached the 2nd sensor?

int currentAngle = 0;
int targetAngle  = 0;
unsigned long lastMoveTime = 0;

void setup() {
  pinMode(IR1, INPUT);
  pinMode(IR2, INPUT);

  gate.attach(MOTOR_PIN);
  gate.write(0);
}

void loop() {
  unsigned long now = millis();

  // LOW = car detected
  bool s1 = (digitalRead(IR1) == LOW);
  bool s2 = (digitalRead(IR2) == LOW);

  // Decide logic/state
  if (systemState == 0) {                 // idle
    targetAngle = 0;                      // closed
    if (s1) { systemState = 1; reachedEnd = false; }  // entering
    else if (s2) { systemState = 2; reachedEnd = false; } // exiting
  }
  else if (systemState == 1) {            // enter: IR1 -> IR2
    targetAngle = 90;                     // open
    if (s2) reachedEnd = true;
    if (reachedEnd && !s1 && !s2) systemState = 0;    // close when clear
  }
  else if (systemState == 2) {            // exit: IR2 -> IR1
    targetAngle = 90;                     // open
    if (s1) reachedEnd = true;
    if (reachedEnd && !s1 && !s2) systemState = 0;    // close when clear
  }

  // Smooth move
  if (now - lastMoveTime >= 15) {
    lastMoveTime = now;

    if (currentAngle < targetAngle) {
      currentAngle++;
      gate.write(currentAngle);
    } else if (currentAngle > targetAngle) {
      currentAngle--;
      gate.write(currentAngle);
    }
  }
}
