#include "ClockApp.h"
#include "ClockMenu.h"
#include <string.h>   // for memcmp

// Define the CP interval table (in minutes)
const uint8_t ClockApp::CP_INTERVAL_COUNT = 5;
const uint8_t ClockApp::CP_INTERVAL_MINUTES[ClockApp::CP_INTERVAL_COUNT] = {
  1,   // 0 -> 1 min
  5,   // 1 -> 5 min
  10,  // 2 -> 10 min
  30,  // 3 -> 30 min
  60   // 4 -> 60 min
};

ClockApp::ClockApp(DisplayEngine& engine,
                   DS3231&        rtc,
                   SettingsStore& settingsStore,
                   ClockMenu&     menu,
                   uint8_t        pinMode,
                   uint8_t        pinUp)
  : _engine(engine),
    _rtc(rtc),
    _settingsStore(settingsStore),
    _settings(settingsStore.settings()),
    _lastAppliedSettings(),
    _menu(menu),
    _pinMode(pinMode),
    _pinUp(pinUp),
    _menuWasActive(false),
    _state(CLOCK_NORMAL),
    _editHour(0),
    _editMinute(0),
    _editSecond(0),
    _softHour(0),
    _softMinute(0),
    _softSecond(0),
    _lastSoftTickMs(0),
    _lastRtcSyncMs(0),
    _lastModeLevel(HIGH),
    _lastUpLevel(HIGH),
    _lastModeChangeMs(0),
    _lastUpChangeMs(0),
    _modeWasDown(false),
    _modePressStartMs(0),
    _modeLongHandled(false),
    _upPressStartMs(0),
    _upLastRepeatMs(0),
    _upIsHeld(false),
    _effectType(1),
    _effectSpeed(500),
    _dayBrightness(16),
    _nightBrightness(4),
    _currentAppliedBrightness(255),
    _blankWakeUntilMs(0),
    _cpIntervalMs(600000UL),
    _lastCpRunMs(0),
    _cpTubeIndex(0),
    _cpStartMs(0),
    _cpLastFrameMs(0)
{
  for (uint8_t i = 0; i < 6; i++) {
    _cpFinalDigits[i] = 0;
  }
}

void ClockApp::begin() {
  // Initial settings → runtime
  _lastAppliedSettings = _settings;
  applySettingsToRuntime();

  // Initial RTC sync → software clock
  syncFromRtc();
  _lastCpRunMs      = millis();
  _blankWakeUntilMs = 0;
}

void ClockApp::tick() {
  uint32_t now = millis();

  // Track menu transitions to clean up state when exiting
  bool menuActive     = _menu.isActive();
  bool justExitedMenu = (_menuWasActive && !menuActive);
  _menuWasActive      = menuActive;

  // Keep soft clock running
  softClockUpdate();
  periodicRtcCorrection();

  // Re-apply settings only if they changed
  if (memcmp(&_settings, &_lastAppliedSettings, sizeof(ClockSettings)) != 0) {
    _lastAppliedSettings = _settings;
    applySettingsToRuntime();
  }

  // Update brightness according to time + night/blank settings + menu overrides
  updateEffectiveBrightness();

  // Time display / animations
  updateTimeDisplay();

  // Handle input/state, but skip on the *exact* tick we exit menu
  if (!justExitedMenu) {
    handleTimeSetAndMenuUI();
  } else {
    // Just left menu → reset button + state so we don't jump straight to minutes
    _state = CLOCK_NORMAL;

    _modeWasDown       = false;
    _modeLongHandled   = false;
    _modePressStartMs  = 0;

    _upIsHeld          = false;
    _upPressStartMs    = 0;
    _upLastRepeatMs    = 0;

    // Reset debounced levels to current physical pins to avoid phantom presses
    _lastModeLevel     = (digitalRead(_pinMode) == LOW);
    _lastUpLevel       = (digitalRead(_pinUp)   == LOW);
    _lastModeChangeMs  = now;
    _lastUpChangeMs    = now;
  }

  handleCathodeProtection();
}

// ----------------------------
// Apply current settings to runtime behavior
// ----------------------------
void ClockApp::applySettingsToRuntime() {
  // Effect type
  if (_settings.effectType > 2) _settings.effectType = 1;
  _effectType = _settings.effectType;

  // Time format
  if (_settings.timeFormat > 1) _settings.timeFormat = 0;

  // Day brightness
  if (_settings.dayBrightness < 1) _settings.dayBrightness = 1;
  if (_settings.dayBrightness > DisplayEngine::FADE_STEPS) {
    _settings.dayBrightness = DisplayEngine::FADE_STEPS;
  }
  _dayBrightness = _settings.dayBrightness;

  // Night brightness
  if (_settings.nightBrightness < 1) _settings.nightBrightness = 1;
  if (_settings.nightBrightness > DisplayEngine::FADE_STEPS) {
    _settings.nightBrightness = DisplayEngine::FADE_STEPS;
  }
  _nightBrightness = _settings.nightBrightness;

  // CP mode
  if (_settings.cpMode > 3) _settings.cpMode = 1;

  // CP interval
  if (_settings.cpIntervalIdx >= CP_INTERVAL_COUNT) {
    _settings.cpIntervalIdx = 2;
  }
  {
    uint8_t mins = CP_INTERVAL_MINUTES[_settings.cpIntervalIdx];
    _cpIntervalMs = (uint32_t)mins * 60UL * 1000UL;
  }

  // Night / blank windows validation (simple clamping, allow any wrap)
  if (_settings.nightStartHour > 23)  _settings.nightStartHour  = 22;
  if (_settings.nightEndHour > 23)    _settings.nightEndHour    = 7;
  if (_settings.blankStartHour > 23)  _settings.blankStartHour  = 23;
  if (_settings.blankEndHour > 23)    _settings.blankEndHour    = 6;
}

// ----------------------------
// Window helpers
// ----------------------------
bool ClockApp::isInWindow(uint8_t startHour, uint8_t endHour, uint8_t h) const {
  if (startHour == endHour) {
    // Zero-length window = disabled
    return false;
  }
  if (startHour < endHour) {
    return (h >= startHour && h < endHour);
  } else {
    // Wrap-around window, e.g. 22 → 7
    return (h >= startHour || h < endHour);
  }
}

bool ClockApp::isNightNow() const {
  return isInWindow(_settings.nightStartHour, _settings.nightEndHour, _softHour);
}

// ----------------------------
// Brightness decision
// ----------------------------
void ClockApp::updateEffectiveBrightness() {
  uint32_t now        = millis();
  bool     menuActive = _menu.isActive();

  bool nightWindow = (_settings.nightModeEnabled != 0) &&
                     isNightNow();

  bool blankWindow = (_settings.masterBlankEnabled != 0) &&
                     isInWindow(_settings.blankStartHour,
                                _settings.blankEndHour,
                                _softHour);

  // If we're in a wake window, ignore blanking
  bool inWakeWindow = (_blankWakeUntilMs != 0 && now < _blankWakeUntilMs);
  if (!inWakeWindow && _blankWakeUntilMs != 0 && now >= _blankWakeUntilMs) {
    _blankWakeUntilMs = 0;
  }

  bool blanked = blankWindow && !inWakeWindow;

  uint8_t effective;

  if (menuActive) {
    // MENU OVERRIDES:
    // - If global blanking would apply -> brightness = 1
    // - Else if night window active   -> nightBrightness
    // - Else                          -> full brightness
    if (blanked) {
      effective = 1;
    } else if (nightWindow) {
      effective = _nightBrightness;
    } else {
      effective = DisplayEngine::FADE_STEPS;
    }
  } else {
    // NORMAL RUN:
    // - default to day brightness
    // - night window -> nightBrightness
    // - blankWindow (and not in wake) -> 0
    effective = _dayBrightness;

    if (nightWindow) {
      effective = _nightBrightness;
    }

    if (blanked) {
      effective = 0;
    }
  }

  if (effective != _currentAppliedBrightness) {
    _currentAppliedBrightness = effective;
    _engine.setGlobalBrightness(effective);
  }
}

// ----------------------------
// Software clock helpers
// ----------------------------
void ClockApp::syncFromRtc() {
  bool    h12, pm;
  uint8_t hour   = _rtc.getHour(h12, pm);
  uint8_t minute = _rtc.getMinute();
  uint8_t second = _rtc.getSecond();

  if (h12) {
    if (pm && hour != 12) hour += 12;
    if (!pm && hour == 12) hour = 0;
  }

  _softHour   = hour;
  _softMinute = minute;
  _softSecond = second;

  _lastSoftTickMs = millis();
  _lastRtcSyncMs  = millis();
}

void ClockApp::softClockUpdate() {
  uint32_t now = millis();

  if (_lastSoftTickMs == 0) {
    _lastSoftTickMs = now;
    return;
  }

  uint32_t elapsed = now - _lastSoftTickMs;
  if (elapsed < 1000) return;

  uint16_t steps = elapsed / 1000;
  _lastSoftTickMs += (uint32_t)steps * 1000UL;

  while (steps--) {
    _softSecond++;
    if (_softSecond >= 60) {
      _softSecond = 0;
      _softMinute++;
      if (_softMinute >= 60) {
        _softMinute = 0;
        _softHour++;
        if (_softHour >= 24) {
          _softHour = 0;
        }
      }
    }
  }
}

void ClockApp::periodicRtcCorrection() {
  const uint32_t RTC_SYNC_INTERVAL_MS = 60000UL; // 1 minute

  uint32_t now = millis();
  if (_lastRtcSyncMs == 0) {
    _lastRtcSyncMs = now;
    return;
  }

  if (now - _lastRtcSyncMs < RTC_SYNC_INTERVAL_MS) return;
  _lastRtcSyncMs = now;

  bool    h12, pm;
  uint8_t hour   = _rtc.getHour(h12, pm);
  uint8_t minute = _rtc.getMinute();
  uint8_t second = _rtc.getSecond();

  if (h12) {
    if (pm && hour != 12) hour += 12;
    if (!pm && hour == 12) hour = 0;
  }

  _softHour   = hour;
  _softMinute = minute;
  _softSecond = second;
  _lastSoftTickMs = millis();
}

// ----------------------------
// Debounced "new press" detector (active LOW)
// ----------------------------
bool ClockApp::checkButtonPress(uint8_t pin, bool &lastLevel, uint32_t &lastChangeMs) {
  bool raw = (digitalRead(pin) == LOW);
  uint32_t now = millis();

  if (raw != lastLevel) {
    if (now - lastChangeMs >= DEBOUNCE_MS) {
      lastLevel    = raw;
      lastChangeMs = now;
      if (raw) {
        return true;
      }
    }
  }
  return false;
}

void ClockApp::incrementHour() {
  _editHour = (_editHour + 1) % 24;
}

void ClockApp::incrementMinute() {
  _editMinute = (_editMinute + 1) % 60;
}

// ----------------------------
// Update time display
// ----------------------------
void ClockApp::updateTimeDisplay() {
  if (_state == CLOCK_CP_RUNNING) {
    return;
  }

  if (_menu.isActive()) {
    return;
  }

  if (_state == CLOCK_SET_HOUR || _state == CLOCK_SET_MINUTE) {
    uint8_t digits[6];

    uint8_t hour   = _editHour;
    uint8_t minute = _editMinute;
    uint8_t second = _editSecond;

    uint8_t dispHour = hour;
    if (_settings.timeFormat == 1) {
      uint8_t h12 = hour % 12;
      if (h12 == 0) h12 = 12;
      dispHour = h12;
    }

    digits[0] = dispHour / 10;
    digits[1] = dispHour % 10;
    digits[2] = minute / 10;
    digits[3] = minute % 10;
    digits[4] = second / 10;
    digits[5] = second % 10;

    uint32_t t       = millis() % BLINK_CYCLE_MS;
    bool     blinkOn = (t < BLINK_ON_MS);

    if (!blinkOn) {
      if (_state == CLOCK_SET_HOUR) {
        digits[0] = 255;
        digits[1] = 255;
      } else if (_state == CLOCK_SET_MINUTE) {
        digits[2] = 255;
        digits[3] = 255;
      }
    }

    _engine.setAllDigits(digits, 6);
    return;
  }

  static uint8_t lastDisplayedSecond = 0xFF;

  uint8_t hour   = _softHour;
  uint8_t minute = _softMinute;
  uint8_t second = _softSecond;

  if (second == lastDisplayedSecond) {
    return;
  }
  lastDisplayedSecond = second;

  _editHour   = hour;
  _editMinute = minute;
  _editSecond = second;

  uint8_t dispHour = hour;
  if (_settings.timeFormat == 1) {
    uint8_t h12 = hour % 12;
    if (h12 == 0) h12 = 12;
    dispHour = h12;
  }

  uint8_t digits[6];
  digits[0] = dispHour / 10;
  digits[1] = dispHour % 10;
  digits[2] = minute / 10;
  digits[3] = minute % 10;
  digits[4] = second / 10;
  digits[5] = second % 10;

  if (_effectType == 1) {
    _engine.startCrossfade(digits, _effectSpeed);
  } else if (_effectType == 2) {
    _engine.startSlot(digits, _effectSpeed);
  } else {
    _engine.setAllDigits(digits, 6);
  }
}

// ----------------------------
// Time-set + menu UI handling
// ----------------------------
void ClockApp::handleTimeSetAndMenuUI() {
  uint32_t now = millis();

  bool modeLevel = (digitalRead(_pinMode) == LOW);
  bool upLevel   = (digitalRead(_pinUp)   == LOW);

  bool upPressed = checkButtonPress(_pinUp, _lastUpLevel, _lastUpChangeMs);

  bool modeShortPress = false;
  bool modeLongPress  = false;

  if (modeLevel && !_modeWasDown) {
    _modeWasDown       = true;
    _modePressStartMs  = now;
    _modeLongHandled   = false;
  } else if (modeLevel && _modeWasDown) {
    if (!_modeLongHandled &&
        (now - _modePressStartMs >= MODE_LONG_PRESS_MS) &&
        _state == CLOCK_NORMAL) {
      modeLongPress    = true;
      _modeLongHandled = true;
    }
  } else if (!modeLevel && _modeWasDown) {
    _modeWasDown = false;
    if (!_modeLongHandled) {
      modeShortPress = true;
    }
    _modePressStartMs = 0;
    _modeLongHandled  = false;
  }

  bool menuActive   = _menu.isActive();
  bool blankWindow  = (_settings.masterBlankEnabled != 0) &&
                      isInWindow(_settings.blankStartHour,
                                 _settings.blankEndHour,
                                 _softHour);
  uint32_t nowMs    = now;
  bool inWakeWindow = (_blankWakeUntilMs != 0 && nowMs < _blankWakeUntilMs);

  // If display is currently fully blanked (no wake window) and not in menu,
  // any button press should *only* wake the display, not enter time-set/menu.
  if (!menuActive && blankWindow && !inWakeWindow) {
    if (modeShortPress || modeLongPress || upPressed) {
      // Start wake window (e.g. 15 seconds)
      _blankWakeUntilMs = now + 15000UL;

      // Consume this press; don't propagate it
      modeShortPress = false;
      modeLongPress  = false;
      upPressed      = false;

      // Reset press tracking so next presses are clean
      _modeWasDown       = false;
      _modeLongHandled   = false;
      _modePressStartMs  = 0;

      _upIsHeld          = false;
      _upPressStartMs    = 0;
      _upLastRepeatMs    = 0;

      return;
    }
  }

  if (_menu.isActive()) {
    _menu.tick(modeShortPress, upPressed);
    return;
  }

  switch (_state) {
    case CLOCK_NORMAL:
      if (modeLongPress) {
        _menu.start();
      } else if (modeShortPress) {
        _editHour   = _softHour;
        _editMinute = _softMinute;
        _editSecond = _softSecond;

        _state    = CLOCK_SET_HOUR;
        _upIsHeld = false;
      }
      break;

    case CLOCK_SET_HOUR:
      if (upPressed) {
        incrementHour();
        _upPressStartMs = now;
        _upLastRepeatMs = now;
        _upIsHeld       = true;
      }

      if (!upLevel) {
        _upIsHeld = false;
      }

      if (_upIsHeld && upLevel &&
          (now - _upPressStartMs >= REPEAT_START_MS) &&
          (now - _upLastRepeatMs >= REPEAT_INTERVAL_MS)) {
        incrementHour();
        _upLastRepeatMs = now;
      }

      if (modeShortPress) {
        _state    = CLOCK_SET_MINUTE;
        _upIsHeld = false;
      }
      break;

    case CLOCK_SET_MINUTE:
      if (upPressed) {
        incrementMinute();
        _upPressStartMs = now;
        _upLastRepeatMs = now;
        _upIsHeld       = true;
      }

      if (!upLevel) {
        _upIsHeld = false;
      }

      if (_upIsHeld && upLevel &&
          (now - _upPressStartMs >= REPEAT_START_MS) &&
          (now - _upLastRepeatMs >= REPEAT_INTERVAL_MS)) {
        incrementMinute();
        _upLastRepeatMs = now;
      }

      if (modeShortPress) {
        _rtc.setClockMode(false);
        _rtc.setHour(_editHour);
        _rtc.setMinute(_editMinute);
        _rtc.setSecond(0);

        _softHour   = _editHour;
        _softMinute = _editMinute;
        _softSecond = 0;
        _lastSoftTickMs = millis();
        _lastRtcSyncMs  = millis();

        _state    = CLOCK_NORMAL;
        _upIsHeld = false;
      }
      break;

    case CLOCK_CP_RUNNING:
      break;
  }
}

// ----------------------------
// CP helpers / engine
// ----------------------------
void ClockApp::cancelCathodeProtection() {
  _state       = CLOCK_NORMAL;
  _lastCpRunMs = millis();
}

void ClockApp::runCpModeSpinWave(uint32_t /*now*/) {
  if (!_engine.isAnimating()) {
    _cpTubeIndex++;
    if (_cpTubeIndex >= 6) {
      cancelCathodeProtection();
    } else {
      uint8_t slotCurrent[6];
      for (uint8_t i = 0; i < 6; i++) {
        slotCurrent[i] = _cpFinalDigits[i];
      }
      slotCurrent[_cpTubeIndex] = (_cpFinalDigits[_cpTubeIndex] + 1) % 10;

      _engine.setAllDigits(slotCurrent, 6);
      _engine.startSlot(_cpFinalDigits, 350);
    }
  }
}

void ClockApp::runCpModeSineRoll(uint32_t now) {
  const uint32_t DURATION_MS   = 1200;
  const uint32_t STEP_INTERVAL = 80;

  if (now - _cpStartMs > DURATION_MS) {
    _engine.setAllDigits(_cpFinalDigits, 6);
    cancelCathodeProtection();
    return;
  }

  for (uint8_t i = 0; i < 6; i++) {
    uint32_t tubePhase = _cpStartMs + i * 40;
    uint32_t localTime = (now - tubePhase);

    if ((int32_t)localTime < 0) {
      continue;
    }

    uint32_t effectiveInterval = STEP_INTERVAL + (localTime / 6);
    if (effectiveInterval < STEP_INTERVAL) effectiveInterval = STEP_INTERVAL;

    if ((localTime % effectiveInterval) < 2) {
      uint8_t digits[6];
      for (uint8_t t = 0; t < 6; t++) {
        digits[t] = _cpFinalDigits[t];
      }

      digits[i] = (_cpFinalDigits[i] + 1) % 10;
      _engine.setAllDigits(digits, 6);
    }
  }
}

void ClockApp::runCpModeRandomFlicker(uint32_t now) {
  const uint32_t FLICKER_DURATION_MS = 400;
  const uint32_t FLICKER_STEP_MS     = 40;

  if (now - _cpStartMs > FLICKER_DURATION_MS) {
    _engine.setAllDigits(_cpFinalDigits, 6);
    cancelCathodeProtection();
    return;
  }

  if (now - _cpLastFrameMs < FLICKER_STEP_MS) {
    return;
  }
  _cpLastFrameMs = now;

  uint8_t digits[6];
  for (uint8_t i = 0; i < 6; i++) {
    digits[i] = random(10);
  }
  _engine.setAllDigits(digits, 6);
}

void ClockApp::startCpRun() {
  uint8_t hour   = _softHour;
  uint8_t minute = _softMinute;
  uint8_t second = _softSecond;

  uint8_t dispHour = hour;
  if (_settings.timeFormat == 1) {
    uint8_t h12d = hour % 12;
    if (h12d == 0) h12d = 12;
    dispHour = h12d;
  }

  _cpFinalDigits[0] = dispHour / 10;
  _cpFinalDigits[1] = dispHour % 10;
  _cpFinalDigits[2] = minute / 10;
  _cpFinalDigits[3] = minute % 10;
  _cpFinalDigits[4] = second / 10;
  _cpFinalDigits[5] = second % 10;

  _cpTubeIndex   = 0xFF;
  _cpStartMs     = millis();
  _cpLastFrameMs = _cpStartMs;

  if (_settings.cpMode == 1 || _settings.cpMode == 2) {
    uint8_t slotCurrent[6];
    for (uint8_t i = 0; i < 6; i++) {
      slotCurrent[i] = _cpFinalDigits[i];
    }
    slotCurrent[0] = (_cpFinalDigits[0] + 1) % 10;

    _engine.setAllDigits(slotCurrent, 6);
    _engine.startSlot(_cpFinalDigits, 350);
    _cpTubeIndex = 0;
  }

  _state = CLOCK_CP_RUNNING;
}

void ClockApp::handleCathodeProtection() {
  uint32_t now = millis();

  if (_settings.cpMode == 0 || _menu.isActive()) {
    return;
  }

  if (_state == CLOCK_CP_RUNNING) {
    switch (_settings.cpMode) {
      case 1:
        runCpModeSpinWave(now);
        break;
      case 2:
        runCpModeSineRoll(now);
        break;
      case 3:
        runCpModeRandomFlicker(now);
        break;
      default:
        cancelCathodeProtection();
        break;
    }
    return;
  }

  if (_state != CLOCK_NORMAL) {
    return;
  }

  if (now - _lastCpRunMs < _cpIntervalMs) {
    return;
  }

  if (_engine.isAnimating()) {
    return;
  }

  startCpRun();
}
