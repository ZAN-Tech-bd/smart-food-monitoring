/*
  Isolated test: 16x2 I2C LCD ONLY.

  No WiFi, no sensors - just initializes the LCD and cycles through a few
  test screens so you can confirm it's wired correctly and responding,
  independent of anything else on the board.

  Required library: LiquidCrystal I2C by Frank de Brabander

  Wiring: LCD SDA -> GPIO 21, SCL -> GPIO 22 (see docs/wiring.md)

  If this hangs or the Serial Monitor shows nothing past "BOOT: Wire (I2C)
  started", the I2C bus itself is stuck (bad wiring, wrong address, or a
  power issue on the LCD) - Wire.setTimeOut() below should still let it
  fail fast rather than hang forever, so a hang here would point to
  something more fundamental (e.g. a genuinely shorted SDA/SCL line).
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#include "config.h"

LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, 16, 2);

unsigned long lastSwitchMs = 0;
uint8_t screen = 0;
#define SCREEN_INTERVAL_MS 2000

void showScreen(uint8_t which) {
  lcd.clear();
  switch (which) {
    case 0:
      lcd.setCursor(0, 0);
      lcd.print("LCD test OK");
      lcd.setCursor(0, 1);
      lcd.print("Screen 1/3");
      break;
    case 1:
      lcd.setCursor(0, 0);
      lcd.print("0123456789ABCDEF");
      lcd.setCursor(0, 1);
      lcd.print("Screen 2/3");
      break;
    case 2:
      lcd.setCursor(0, 0);
      lcd.print("Addr: 0x");
      lcd.print(LCD_I2C_ADDRESS, HEX);
      lcd.setCursor(0, 1);
      lcd.print("Screen 3/3");
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("BOOT: Serial up");

  Wire.begin();
  Wire.setTimeOut(1000);
  Serial.println("BOOT: Wire (I2C) started");

  lcd.init();
  lcd.backlight();
  Serial.printf("BOOT: LCD initialized at address 0x%02X\n", LCD_I2C_ADDRESS);

  showScreen(screen);
  Serial.println("BOOT: setup complete, cycling test screens every 2s");
}

void loop() {
  if (millis() - lastSwitchMs >= SCREEN_INTERVAL_MS) {
    lastSwitchMs = millis();
    screen = (screen + 1) % 3;
    showScreen(screen);
    Serial.printf("Showing screen %d\n", screen + 1);
  }
}
