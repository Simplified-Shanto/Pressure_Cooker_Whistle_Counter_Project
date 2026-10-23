/* LCD-only test. Generic ESP8266 Module, GPIO4 SDA, GPIO5 SCL, 16x2 LCD.
   No Wi-Fi or filesystem access. Upload the main counter again afterward;
   keep Erase Flash: Only Sketch to preserve its outbox and settings. */
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <memory>

std::unique_ptr<LiquidCrystal_I2C> lcd;
uint8_t address = 0;
uint32_t lastUpdate = 0, seconds = 0;

void setup() {
  Serial.begin(115200);
  delay(200);
  Wire.begin(4, 5);
  Wire.setClock(100000);
  Wire.setClockStretchLimit(1500);
  Serial.println(F("LCD-only test: SDA GPIO4, SCL GPIO5. Scanning I2C..."));
  for (uint8_t candidate = 1; candidate < 127; ++candidate) {
    Wire.beginTransmission(candidate);
    if (Wire.endTransmission() == 0) {
      Serial.printf("I2C responder: 0x%02X\n", candidate);
      if (!address && (candidate == 0x27 || candidate == 0x3F)) address = candidate;
    }
    yield();
  }
  if (!address) {
    Serial.println(F("No responder at 0x27/0x3F. Check power, common GND, SDA/SCL, or the reported address."));
    return;
  }
  lcd.reset(new LiquidCrystal_I2C(address, 16, 2));
  lcd->init();
  Wire.setClock(100000);
  lcd->backlight();
  lcd->leftToRight();
  lcd->noAutoscroll();
  lcd->clear();
  lcd->setCursor(0, 0);
  lcd->print("0123456789ABCDEF");
  lcd->setCursor(0, 1);
  lcd->print("LCD text test   ");
  Serial.printf("Testing LCD at 0x%02X. First row should read 0123456789ABCDEF.\n", address);
  Serial.println(F("If this also shows random symbols, check wiring/power and backpack pin mapping. Fully power-cycle the display."));
}

void loop() {
  if (lcd && millis() - lastUpdate >= 1000) {
    lastUpdate = millis();
    char line[17];
    snprintf(line, sizeof(line), "Tick: %-10lu", (unsigned long)++seconds);
    lcd->setCursor(0, 1);
    lcd->print(line);
  }
  delay(1);
}
