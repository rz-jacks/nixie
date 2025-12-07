#pragma once
#include <Arduino.h>
#include <DS3231.h>
#include "DisplayEngine.h"

// Simple settings struct stored in EEPROM
struct ClockSettings {
  uint8_t magic;        // 0x42 = valid
  uint8_t version;      // 3 = current layout
  uint8_t timeFormat;   // 0 = 24h, 1 = 12h
  uint8_t effectType;   // 0 = instant, 1 = crossfade, 2 = slot
  uint8_t brightness;   // 0..FADE_STEPS (0 = off, FADE_STEPS = full)
  uint8_t cpMode;       // 0 = off, 1 = CP-A (spin wave), 2 = sine roll, 3 = random flicker
  uint8_t cpIntervalIdx;// 0..N-1 index into CP interval table
  uint8_t reserved;     // padding / future use
};

class ClockApp {
public:
  // Expose count so .cpp can size interval table safely
  static const uint8_t CP_INTERVAL_COUNT = 5;
  
  ClockApp(DisplayEngine& engine, DS3231& rtc,
           uint8_t pinMode, uint8_t pinUp);
  void begin();
  void tick();   // call from loop()

private:
  // References to shared hardware objects
  DisplayEngine& _engine;
  DS3231&        _rtc;
  uint8_t        _pinMode;
  uint8_t        _pinUp;

  // Clock state machine
  enum ClockState : uint8_t {
    CLOCK_NORMAL = 0,
    CLOCK_SET_HOUR,
    CLOCK_SET_MINUTE,
    CLOCK_MENU_EFFECT,      // 01: effect type
    CLOCK_MENU_TIMEFORMAT,  // 02: time format
    CLOCK_MENU_BRIGHTNESS,  // 03: brightness
    CLOCK_MENU_CP_MODE,     // 04: CP mode
    CLOCK_MENU_CP_INTERVAL, // 05: CP interval
    CLOCK_CP_RUNNING        // Cathode protection animation active
  };
  ClockState _state;

  // Settings + EEPROM
  ClockSettings _settings;
  ClockSettings _lastSavedSettings;  // Track changes to reduce EEPROM wear
  static const uint8_t SETTINGS_MAGIC   = 0x42;
  static const uint8_t SETTINGS_VERSION = 3;

  void loadSettings();
  void saveSettings();
  void applySettingsToRuntime();
  bool settingsChanged() const;

  // Editable time while in set mode
  uint8_t _editHour;
  uint8_t _editMinute;
  uint8_t _editSecond;

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
  static const uint32_t REPEAT_START_MS    = 500;  // delay before auto-repeat
  static const uint32_t REPEAT_INTERVAL_MS = 120;  // interval between repeats

  // Display effect selection (runtime)
  uint8_t  _effectType;   // 0=instant, 1=crossfade, 2=slot
  uint16_t _effectSpeed;  // ms

  // Display update timing constants
  static const uint32_t NORMAL_UPDATE_MS = 1000;  // 1 Hz for normal clock
  static const uint32_t MENU_UPDATE_MS   = 150;   // Menu refresh rate
  static const uint32_t BLINK_CYCLE_MS   = 600;   // Total blink cycle
  static const uint32_t BLINK_ON_MS      = 450;   // Blink ON duration

  // Cathode protection (CP) scheduling + state
  // CP interval table is defined in .cpp; here we store runtime state
  uint32_t _cpIntervalMs;          // currently selected interval in ms
  uint32_t _lastCpRunMs;           // last time CP finished/started
  uint8_t  _cpTubeIndex;           // used by Mode 1 (spin wave) 0..5
  uint8_t  _cpFinalDigits[6];      // frozen clock digits used during CP
  uint32_t _cpStartMs;             // start time of current CP run
  uint32_t _cpLastFrameMs;         // last frame time for flicker, etc.

  // Internal helpers
  bool checkButtonPress(uint8_t pin, bool &lastLevel, uint32_t &lastChangeMs);
  void incrementHour();
  void incrementMinute();
  void updateTimeDisplay();
  void handleTimeSetAndMenuUI();
  void handleCathodeProtection();   // CP scheduling + CP modes
  void cancelCathodeProtection();   // Cancel CP and return to clock

  // CP helpers
  void startCpRun();                // freeze digits, set state, reset timers
  void runCpModeSpinWave(uint32_t now);
  void runCpModeSineRoll(uint32_t now);
  void runCpModeRandomFlicker(uint32_t now);

  // Menu helpers
  void enterMenu();
  void showMenuEffectDigits(uint8_t* digits);
  void showMenuTimeFormatDigits(uint8_t* digits);
  void showMenuBrightnessDigits(uint8_t* digits);
  void showMenuCpModeDigits(uint8_t* digits);
  void showMenuCpIntervalDigits(uint8_t* digits);
};
