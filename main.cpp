#include <Arduino.h>
#include <LiquidCrystal.h>

// Source: https://www.circuitschools.com/interfacing-16x2-lcd-module-with-esp32-with-and-without-i2c/?utm_source=copilot.com
// This source was used to access functions to write to the LCD screen as well as the pinouts needed to turn the lcd screen on
LiquidCrystal LCD(19, 23, 18, 5, 15, 4);


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200); // Serial Communication Intialization
  LCD.begin(16, 2); // LCD Column and Row Initialization
  LCD.clear(); // Clear image on screen
  LCD.print("System Started.."); // Print System Started on LCD
}

void loop() {
  // put your main code here, to run repeatedly:
}

