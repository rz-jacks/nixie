#pragma once
#include <Arduino.h>
#include "NixieDisplay.h"

class DisplayEngine {
public:
  static const uint8_t FRAME_INTERVAL_MS = 1;
  static const uint8_t FADE_STEPS        = 16;  // PWM resolution

  enum Mode : uint8_t {
    MODE_IDLE = 0,
    MODE_CROSSFADE,
    MODE_SLOT
  };

  DisplayEngine(NixieDisplay& display);
  void begin();
  void tick1ms();

  void setAllDigits(const uint8_t* digits, uint8_t count);
  void setAllInstant(uint8_t digit);
  void startCrossfade(const uint8_t* targetDigits, uint16_t durationMs);
  void startSlot(const uint8_t* targetDigits, uint16_t durationMs);

  // Global brightness: 0..FADE_STEPS (0=off, FADE_STEPS=full)
  void setGlobalBrightness(uint8_t level) {
    if (level > FADE_STEPS) level = FADE_STEPS;
    _globalBrightness = level;
  }

  uint8_t getGlobalBrightness() const { return _globalBrightness; }
  bool isAnimating() const { return _mode != MODE_IDLE; }

private:
  NixieDisplay& _display;

  uint8_t _currentDigits[6];
  uint8_t _targetDigits[6];
  uint8_t _lastRenderedDigits[6];  // Track actual display state for optimization

  Mode     _mode;
  uint16_t _animationMs;
  uint16_t _elapsedMs;
  uint8_t  _pwmPhase;

  // Slot animation control
  uint16_t _slotStepInterval;      // ms between steps
  uint16_t _slotStepCounter;       // counts ms
  uint8_t  _slotStepsRemaining[6]; // per-digit steps left (0..10)

  // Global brightness (0..FADE_STEPS)
  uint8_t  _globalBrightness;

  void updateCrossfade();
  void updateSlot();
  void renderFrame();
};
