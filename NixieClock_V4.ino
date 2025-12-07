#define F_CPU 16000000UL
#include <Arduino.h>
#include <Wire.h>
#include <DS3231.h>

#include "NixieConfig.h"
#include "Hv5622DriverFast.h"
#include "NixieDisplay.h"
#include "DisplayEngine.h"
#include "ClockApp.h"

// ----------------------------
// Hardware instances
// ----------------------------
Hv5622DriverFast hvDriver(PIN_LE, PIN_DATA, PIN_CLK, PIN_BL, PIN_POL);
NixieDisplay display(hvDriver);
DisplayEngine engine(display);
DS3231 rtc;

// Buttons (PD4, PD5 → D4, D5)
static const uint8_t PIN_BTN_MODE = 4;
static const uint8_t PIN_BTN_UP   = 5;

// Clock application (brain)
ClockApp clockApp(engine, rtc, PIN_BTN_MODE, PIN_BTN_UP);

// ----------------------------
// Global timing
// ----------------------------
volatile uint32_t g_msTicks = 0;

// TIMER2 ISR: 1 kHz tick for DisplayEngine
ISR(TIMER2_COMPA_vect) {
  g_msTicks++;
}

void initTimer2_1kHz() {
  cli();
  TCCR2A = 0;
  TCCR2B = 0;

  // CTC mode
  TCCR2A |= (1 << WGM21);
  // Prescaler 64
  TCCR2B |= (1 << CS22);

  // 16 MHz / (64 * (249 + 1)) = 1000 Hz
  OCR2A = 249;

  // Enable compare interrupt
  TIMSK2 |= (1 << OCIE2A);
  sei();
}

// ----------------------------
// Arduino setup
// ----------------------------
void setup() {
  Wire.begin();

  // Buttons
  pinMode(PIN_BTN_MODE, INPUT_PULLUP);
  pinMode(PIN_BTN_UP,   INPUT_PULLUP);

  // Display engine + HV driver
  engine.begin();
  hvDriver.setPolarity(true);   // POL HIGH - matches hardware
  hvDriver.setBlank(false);     // BL HIGH - outputs enabled

  // Startup animation
  for (uint8_t d = 0; d <= 9; d++) {
    engine.setAllInstant(d);
    delay(100);
  }

  initTimer2_1kHz();

  clockApp.begin();
}

// ----------------------------
// Arduino main loop
// ----------------------------
void loop() {
  static uint32_t lastTick = 0;

  // Feed the display engine at 1 kHz from TIMER2
  while (lastTick != g_msTicks) {
    lastTick++;
    engine.tick1ms();
  }

  // Let the ClockApp handle time display + time set + menu
  clockApp.tick();
}
