#pragma once
#include <Arduino.h>
#include "DisplayEngine.h"
#include "SettingsStore.h"

class ClockMenu {
public:
  // Each entry = one menu page.
  enum MenuState : uint8_t {
    MENU_INACTIVE = 0,

    MENU_EFFECT,          // 01: transition effect
    MENU_TIMEFORMAT,      // 02: 24/12
    MENU_BRIGHTNESS,      // 03: day brightness
    MENU_CP_MODE,         // 04: CP mode
    MENU_CP_INTERVAL,     // 05: CP interval

    MENU_NIGHT_MODE,      // 06: night mode on/off
    MENU_NIGHT_BRIGHTNESS,// 07: night brightness
    MENU_NIGHT_START,     // 08: night start hour
    MENU_NIGHT_END,       // 09: night end hour

    MENU_BLANK_START,     // 10: blanking start hour
    MENU_BLANK_END,       // 11: blanking end hour

    MENU_LEADING_ZERO,    // 12: reserved for future
    MENU_DATE_MODE,       // 13: reserved for future
    MENU_MASTER_BLANK,    // 14: master blank on/off

    MENU_STATE_COUNT      // keep this last (not a real page)
  };

  ClockMenu(DisplayEngine& engine, SettingsStore& store);

  void begin();               // currently no-op, kept for symmetry
  void start();               // enter menu at first page (01)
  void tick(bool modeShortPress, bool upPressed);  // handle input + update display

  bool isActive() const { return _state != MENU_INACTIVE; }

private:
  DisplayEngine& _engine;
  SettingsStore& _store;
  ClockSettings& _settings;
  MenuState      _state;

  uint32_t _lastUpdateMs;
  static const uint32_t MENU_UPDATE_MS = 150;

  void showCurrentPage();

  void showEffectPage(uint8_t* digits);
  void showTimeFormatPage(uint8_t* digits);
  void showBrightnessPage(uint8_t* digits);
  void showCpModePage(uint8_t* digits);
  void showCpIntervalPage(uint8_t* digits);

  void showNightModePage(uint8_t* digits);
  void showNightBrightnessPage(uint8_t* digits);
  void showNightStartPage(uint8_t* digits);
  void showNightEndPage(uint8_t* digits);

  void showBlankStartPage(uint8_t* digits);
  void showBlankEndPage(uint8_t* digits);

  void showLeadingZeroPage(uint8_t* digits);
  void showDateModePage(uint8_t* digits);
  void showMasterBlankPage(uint8_t* digits);
};
