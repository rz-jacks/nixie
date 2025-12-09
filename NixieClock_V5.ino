// ============================================================================
// NixieClock_V6.ino
// ============================================================================

#define F_CPU 16000000UL

#include <Arduino.h>
#include <Wire.h>
#include <DS3231.h>

#include "NixieConfig.h"
#include "Hv5622DriverFast.h"
#include "NixieDisplay.h"
#include "DisplayEngine.h"
#include "SettingsStore.h"
#include "ClockMenu.h"
#include "ClockApp.h"

// Buttons on PD4 and PD5 with internal pull-ups
const uint8_t PIN_MODE = 4;   // MODE / MENU
const uint8_t PIN_UP   = 5;   // UP / INCREMENT

Hv5622DriverFast hvDriver(PIN_LE, PIN_DATA, PIN_CLK, PIN_BL, PIN_POL);
NixieDisplay     nixieDisplay(hvDriver);
DisplayEngine    displayEngine(nixieDisplay);
DS3231           rtc;
SettingsStore    settings;
ClockMenu        clockMenu(displayEngine, settings);
ClockApp         clockApp(displayEngine, rtc, settings, clockMenu, PIN_MODE, PIN_UP);

// 1 kHz system tick
volatile uint32_t g_msTicks = 0;

ISR(TIMER2_COMPA_vect) {
  g_msTicks++;
}

void initTimer2_1kHz() {
  cli();

  TCCR2A = 0;
  TCCR2B = 0;

  TCCR2A |= (1 << WGM21);   // CTC
  TCCR2B |= (1 << CS22);    // prescaler 64
  OCR2A   = 249;            // 16MHz/64/(1+249) = 1kHz

  TIMSK2 |= (1 << OCIE2A);  // enable compare A interrupt

  sei();
}

void setup() {
  Wire.begin();

  pinMode(PIN_MODE, INPUT_PULLUP);
  pinMode(PIN_UP,   INPUT_PULLUP);

  displayEngine.begin();

  settings.begin();    // load settings from EEPROM
  clockMenu.begin();   // (currently no-op, kept for symmetry)
  clockApp.begin();    // apply settings to runtime

  initTimer2_1kHz();
}

void loop() {
  static uint32_t lastTick = 0;
  while (lastTick != g_msTicks) {
    lastTick++;
    displayEngine.tick1ms();
  }

  clockApp.tick();
}
