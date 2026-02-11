/*
THIS IS A TEST SCRIPT JUST USED TO SEE IF THE OLED IS WORKINF
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define W 128
#define H 64
Adafruit_SSD1306 display(W, H, &Wire, -1);

const int TOTAL_SPOTS = 4;
int openSpots = 4;

enum ScreenMode { SCREEN_STATUS, SCREEN_WELCOME, SCREEN_GOODBYE };
ScreenMode mode = SCREEN_STATUS;
unsigned long modeUntil = 0;

void drawStatus() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Smart Garage");

  display.setTextSize(2);
  display.setCursor(0, 20);
  display.print("Open: ");
  display.print(openSpots);

  display.setTextSize(1);
  display.setCursor(0, 50);
  display.print("Total: ");
  display.print(TOTAL_SPOTS);

  display.display();
}

void drawCentered(const char* msg, uint8_t size) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(size);

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);

  int x = (W - (int)w) / 2;
  int y = (H - (int)h) / 2;

  display.setCursor(x, y);
  display.print(msg);
  display.display();
}

void showWelcome(unsigned long ms=2000) {
  mode = SCREEN_WELCOME;
  modeUntil = millis() + ms;
  drawCentered("WELCOME", 2);
}

void showGoodbye(unsigned long ms=2000) {
  mode = SCREEN_GOODBYE;
  modeUntil = millis() + ms;
  drawCentered("GOODBYE", 2);
}

void setup() {
  Wire.begin(21, 22);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for (;;) {}
  }

  drawStatus();
}

void loop() {
  static unsigned long lastEvent = 0;
  unsigned long now = millis();

  // Fake cycle: Enter -> Status -> Exit -> Status ...
  if (now - lastEvent >= 4000) {
    lastEvent = now;

    static bool entering = true;
    if (entering) {
      if (openSpots > 0) openSpots--;
      showWelcome(1800);
    } else {
      if (openSpots < TOTAL_SPOTS) openSpots++;
      showGoodbye(1800);
    }
    entering = !entering;
  }

  if (mode != SCREEN_STATUS && now > modeUntil) {
    mode = SCREEN_STATUS;
    drawStatus();
  }
}
