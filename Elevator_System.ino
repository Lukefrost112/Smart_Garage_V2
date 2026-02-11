#define button2 6
#define button1 2
#define in1 4
#define in2 5

unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 30;

int stableB1 = LOW;
int stableB2 = LOW;

int lastReadingB1 = LOW;
int lastReadingB2 = LOW;

void setup()
{
  pinMode(button1, INPUT);
  pinMode(button2, INPUT);
  pinMode(in1, OUTPUT);
  pinMode(in2, OUTPUT);
  Serial.begin(9600);
}

void loop()
{
  int readingB1 = digitalRead(button1);
  int readingB2 = digitalRead(button2);

  if (readingB1 != lastReadingB1 || readingB2 != lastReadingB2) {
    lastDebounceTime = millis();
  }

  if (millis() - lastDebounceTime > debounceDelay) {
    stableB1 = readingB1;
    stableB2 = readingB2;
  }

  lastReadingB1 = readingB1;
  lastReadingB2 = readingB2;

  if (stableB1 == HIGH) {
    digitalWrite(in1, HIGH);
    digitalWrite(in2, LOW);
  }
  else if (stableB2 == HIGH) {
    digitalWrite(in1, LOW);
    digitalWrite(in2, HIGH);
  }
  else {
    digitalWrite(in1, LOW);
    digitalWrite(in2, LOW);
  }
}
