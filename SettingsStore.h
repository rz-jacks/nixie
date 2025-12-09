#pragma once
#include <Arduino.h>
#include <EEPROM.h>

// Central settings struct for the clock.
// Version 4: supports full night/blank windows.
struct ClockSettings {
  uint8_t magic;             // 0x42 = valid
  uint8_t version;           // 4 = current layout

  uint8_t timeFormat;        // 0 = 24h, 1 = 12h
  uint8_t effectType;        // 0 = instant, 1 = crossfade, 2 = slot

  uint8_t dayBrightness;     // 1..FADE_STEPS
  uint8_t nightBrightness;   // 1..FADE_STEPS

  uint8_t nightModeEnabled;  // 0 = off, 1 = on
  uint8_t masterBlankEnabled;// 0 = off, 1 = on

  uint8_t cpMode;            // 0 = off, 1..3 = CP modes
  uint8_t cpIntervalIdx;     // 0..N-1 index into CP interval table

  uint8_t nightStartHour;    // 0..23
  uint8_t nightEndHour;      // 0..23

  uint8_t blankStartHour;    // 0..23
  uint8_t blankEndHour;      // 0..23

  uint8_t reserved1;         // future use
  uint8_t reserved2;         // future use
};

class SettingsStore {
public:
  static const uint8_t SETTINGS_MAGIC   = 0x42;
  static const uint8_t SETTINGS_VERSION = 4;
  static const int     EEPROM_ADDR_SETTINGS = 0;

  SettingsStore();

  void begin();          // call once from setup()

  ClockSettings&       settings()       { return _settings; }
  const ClockSettings& settings() const { return _settings; }

  void markDirty();
  void saveIfDirty();

private:
  ClockSettings _settings;
  ClockSettings _savedCopy;
  bool          _dirty;

  void loadFromEepromOrInitDefaults();
  void writeToEeprom();
};
