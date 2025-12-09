#pragma once
#include <Arduino.h>
#include <DS3231.h>
#include "DisplayEngine.h"
#include "SettingsStore.h"

// Forward declaration to avoid circular include
class ClockMenu;

class ClockApp {
public:
  // CP interval table info
  static const uint8_t CP_INTERVAL_COUNT;
  static const uint8_t CP_INTERVAL_MINUTES[];

  ClockApp(DisplayEngine& engine,
           DS3231&        rtc,
           SettingsStore& settingsStore,
           ClockMenu&     menu,
           uint8_t        pinMode,
           uint8_t        pinUp);

  void begin();
  void tick();   // call from loop()

private:
  // References to shared hardware objects
  DisplayEngine& _engine;
  DS3231&        _rtc;
  SettingsStore& _settingsStore;
  ClockSettings& _settings;
  ClockSettings  _lastAppliedSettings;
  ClockMenu&     _menu;

  uint8_t        _pinMode;
  uint8_t        _pinUp;

  // Track menu active transitions (for state reset)
  bool           _menuWasActive;

  // Clock state machine (runtime only)
  enum ClockState : uint8_t {
    CLOCK_NORMAL = 0,
    CLOCK_SET_HOUR,
    CLOCK_SET_MINUTE,
    CLOCK_CP_RUNNING
  };
  ClockState _state;

  // Editable time while in set mode
  uint8_t _editHour;
  uint8_t _editMinute;
  uint8_t _editSecond;

  // Software clock (AVR-driven time)
  uint8_t  _softHour;
  uint8_t  _softMinute;
  uint8_t  _softSecond;
  uint32_t _lastSoftTickMs;
  uint32_t _lastRtcSyncMs;

  // Button debounce state
  bool     _lastModeLevel;
  bool     _lastUpLevel;
  uint32_t _lastModeChangeMs;
  uint32_t _lastUpChangeMs;
  static const uint32_t DEBOUNCE_MS = 30;

  // Mode button press tracking
  bool     _modeWasDown;
  uint32_t _modePressStartMs;
  bool     _modeLongHandled;
  static const uint32_t MODE_LONG_PRESS_MS = 1200; // 1.2 s

  // Auto-repeat for UP button (time adjust)
  uint32_t _upPressStartMs;
  uint32_t _upLastRepeatMs;
  bool     _upIsHeld;
  static const uint32_t REPEAT_START_MS    = 500;
  static const uint32_t REPEAT_INTERVAL_MS = 120;

  // Display effect selection (runtime)
  uint8_t  _effectType;   // 0=instant, 1=crossfade, 2=slot
  uint16_t _effectSpeed;  // ms

  // Brightness management
  uint8_t  _dayBrightness;
  uint8_t  _nightBrightness;
  uint8_t  _currentAppliedBrightness;

  // Wake-from-blank window
  uint32_t _blankWakeUntilMs;   // 0 = no wake; >now = temporarily ignoring blank window

  // Blink timing for time-set UI
  static const uint32_t BLINK_CYCLE_MS = 600;
  static const uint32_t BLINK_ON_MS    = 450;

  // Cathode protection (CP) scheduling + state
  uint32_t _cpIntervalMs;
  uint32_t _lastCpRunMs;
  uint8_t  _cpTubeIndex;
  uint8_t  _cpFinalDigits[6];
  uint32_t _cpStartMs;
  uint32_t _cpLastFrameMs;

  // Internal helpers
  bool checkButtonPress(uint8_t pin, bool &lastLevel, uint32_t &lastChangeMs);
  void incrementHour();
  void incrementMinute();

  // Time / display
  void updateTimeDisplay();
  void handleTimeSetAndMenuUI();
  void handleCathodeProtection();
  void cancelCathodeProtection();

  // Settings → runtime application
  void applySettingsToRuntime();

  // Brightness decision based on time + night/blank settings
  void updateEffectiveBrightness();
  bool isNightNow() const;
  bool isInWindow(uint8_t startHour, uint8_t endHour, uint8_t h) const;

  // Soft clock helpers
  void syncFromRtc();          // one-shot: read RTC into _soft*
  void softClockUpdate();      // advance _soft* using millis()
  void periodicRtcCorrection();// optional: slow re-sync from RTC

  // CP helpers
  void startCpRun();
  void runCpModeSpinWave(uint32_t now);
  void runCpModeSineRoll(uint32_t now);
  void runCpModeRandomFlicker(uint32_t now);
};
