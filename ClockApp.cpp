#include "ClockApp.h"
#include <EEPROM.h>

static const int EEPROM_ADDR_SETTINGS = 0;

// CP interval table in minutes → converted to ms in applySettingsToRuntime
static const uint8_t CP_INTERVAL_MINUTES[ClockApp::CP_INTERVAL_COUNT] = {
  1,   // idx 0 -> 1 min  (nice for testing)
  5,   // idx 1 -> 5 min
  10,  // idx 2 -> 10 min (good default)
  30,  // idx 3 -> 30 min
  60   // idx 4 -> 60 min
};

// Sine-wave table for CP Mode 2 (outside function for efficiency)
static const int8_t CP_SINE_WAVE[16] = {
  0, 1, 2, 1,
  0,-1,-2,-1,
  0, 1, 2, 1,
  0,-1,-2,-1
};

ClockApp::ClockApp(DisplayEngine& engine, DS3231& rtc,
                   uint8_t pinMode, uint8_t pinUp)
  : _engine(engine),
    _rtc(rtc),
    _pinMode(pinMode),
    _pinUp(pinUp),
    _state(CLOCK_NORMAL),
    _editHour(0),
    _editMinute(0),
    _editSecond(0),
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
    _effectType(1),           // default: crossfade
    _effectSpeed(500),        // default: 500 ms
    _cpIntervalMs(600000UL),  // 10 minutes default
    _lastCpRunMs(0),
    _cpTubeIndex(0),
    _cpStartMs(0),
    _cpLastFrameMs(0)
{
  // Default settings
  _settings.magic         = SETTINGS_MAGIC;
  _settings.version       = SETTINGS_VERSION;
  _settings.timeFormat    = 0;   // 24h
  _settings.effectType    = 1;   // crossfade
  _settings.brightness    = DisplayEngine::FADE_STEPS; // full
  _settings.cpMode        = 1;   // CP-A enabled by default
  _settings.cpIntervalIdx = 2;   // index 2 -> 10 min
  _settings.reserved      = 0;

  // Initialize last saved settings to detect changes
  _lastSavedSettings = _settings;

  // Initialize CP digits
  for (uint8_t i = 0; i < 6; i++) {
    _cpFinalDigits[i] = 0;
  }
}

void ClockApp::begin() {
  // Seed random number generator for CP mode 3 (flicker)
  // Note: All analog pins (A0-A3) are used as digital outputs for HV5622
  // So we just use millis() which varies based on boot time
  randomSeed(millis());
  
  loadSettings();
  applySettingsToRuntime();
  
  // Always start in NORMAL state (not CP_RUNNING after reboot)
  _state = CLOCK_NORMAL;
  
  _lastCpRunMs = millis();  // start CP timer from power-up
}

// Called frequently from loop()
void ClockApp::tick() {
  updateTimeDisplay();
  handleTimeSetAndMenuUI();
  handleCathodeProtection();
}

// ----------------------------
// Settings load/save
// ----------------------------
void ClockApp::loadSettings() {
  ClockSettings tmp;
  EEPROM.get(EEPROM_ADDR_SETTINGS, tmp);

  if (tmp.magic != SETTINGS_MAGIC || tmp.version != SETTINGS_VERSION) {
    // Invalid or wrong version - use defaults and force write
    // _settings already contains defaults from constructor
    EEPROM.put(EEPROM_ADDR_SETTINGS, _settings);  // Force write defaults
    _lastSavedSettings = _settings;  // Now they match
  } else {
    // Valid settings found - load them
    _settings = tmp;
    _lastSavedSettings = _settings;  // Track what's in EEPROM
  }
}

bool ClockApp::settingsChanged() const {
  // Compare current settings with last saved
  return (_settings.timeFormat    != _lastSavedSettings.timeFormat   ||
          _settings.effectType    != _lastSavedSettings.effectType   ||
          _settings.brightness    != _lastSavedSettings.brightness   ||
          _settings.cpMode        != _lastSavedSettings.cpMode       ||
          _settings.cpIntervalIdx != _lastSavedSettings.cpIntervalIdx);
}

void ClockApp::saveSettings() {
  // Only write to EEPROM if settings actually changed (reduce wear)
  if (settingsChanged()) {
    EEPROM.put(EEPROM_ADDR_SETTINGS, _settings);
    _lastSavedSettings = _settings;
  }
}

void ClockApp::applySettingsToRuntime() {
  // Effect type from settings
  if (_settings.effectType > 2) _settings.effectType = 1; // clamp
  _effectType = _settings.effectType;

  // Time format: 0 = 24h (RTC stays 24h), 1 = 12h (display only)
  if (_settings.timeFormat > 1) _settings.timeFormat = 0;

  // Brightness: 0..FADE_STEPS
  if (_settings.brightness > DisplayEngine::FADE_STEPS) {
    _settings.brightness = DisplayEngine::FADE_STEPS;
  }
  _engine.setGlobalBrightness(_settings.brightness);

  // CP mode: 0 = off, 1 = CP-A, 2 = sine roll, 3 = random flicker
  if (_settings.cpMode > 3) _settings.cpMode = 1;

  // CP interval index
  if (_settings.cpIntervalIdx >= CP_INTERVAL_COUNT) {
    _settings.cpIntervalIdx = 2; // default back to 10 min
  }
  uint8_t mins = CP_INTERVAL_MINUTES[_settings.cpIntervalIdx];
  _cpIntervalMs = (uint32_t)mins * 60UL * 1000UL;
}

// ----------------------------
// Debounced "new press" detector (active LOW)
// ----------------------------
bool ClockApp::checkButtonPress(uint8_t pin, bool &lastLevel, uint32_t &lastChangeMs) {
  bool raw = (digitalRead(pin) == LOW);  // active LOW with INPUT_PULLUP
  uint32_t now = millis();

  if (raw != lastLevel) {
    if (now - lastChangeMs >= DEBOUNCE_MS) {
      lastLevel    = raw;
      lastChangeMs = now;
      if (raw) {
        // LOW edge = new press
        return true;
      }
    }
  }
  return false;
}

// ----------------------------
// Helpers to increment hour/minute (used by auto-repeat)
// ----------------------------
void ClockApp::incrementHour() {
  _editHour = (_editHour + 1) % 24;
}

void ClockApp::incrementMinute() {
  _editMinute = (_editMinute + 1) % 60;
}

// ----------------------------
// Menu helpers
// ----------------------------
void ClockApp::enterMenu() {
  _state = CLOCK_MENU_EFFECT;
}

void ClockApp::showMenuEffectDigits(uint8_t* digits) {
  // 01 00 0X  (X = effectType 0..2)
  digits[0] = 0;
  digits[1] = 1;
  digits[2] = 0;
  digits[3] = 0;
  digits[4] = 0;
  digits[5] = _settings.effectType;  // 0,1,2
}

void ClockApp::showMenuTimeFormatDigits(uint8_t* digits) {
  // 02 00 24  or  02 00 12
  digits[0] = 0;
  digits[1] = 2;
  digits[2] = 0;
  digits[3] = 0;

  if (_settings.timeFormat == 0) {
    // 24h
    digits[4] = 2;
    digits[5] = 4;
  } else {
    // 12h
    digits[4] = 1;
    digits[5] = 2;
  }
}

void ClockApp::showMenuBrightnessDigits(uint8_t* digits) {
  // 03 00 BB  (BB = brightness 00..16)
  digits[0] = 0;
  digits[1] = 3;
  digits[2] = 0;
  digits[3] = 0;

  uint8_t b = _settings.brightness;
  if (b > 99) b = 99;  // just in case
  digits[4] = b / 10;
  digits[5] = b % 10;
}

void ClockApp::showMenuCpModeDigits(uint8_t* digits) {
  // 04 00 0M  (M = CP mode 0..3)
  digits[0] = 0;
  digits[1] = 4;
  digits[2] = 0;
  digits[3] = 0;
  digits[4] = 0;
  digits[5] = _settings.cpMode;  // 0=OFF,1=spin,2=sine,3=flicker
}

void ClockApp::showMenuCpIntervalDigits(uint8_t* digits) {
  // 05 00 II  (II = interval minutes, e.g. 01,05,10,30,60)
  digits[0] = 0;
  digits[1] = 5;
  digits[2] = 0;
  digits[3] = 0;

  uint8_t mins = CP_INTERVAL_MINUTES[_settings.cpIntervalIdx];
  digits[4] = mins / 10;
  digits[5] = mins % 10;
}

// ----------------------------
// Cancel CP and return to normal clock
// ----------------------------
void ClockApp::cancelCathodeProtection() {
  if (_state == CLOCK_CP_RUNNING) {
    _state = CLOCK_NORMAL;
    _lastCpRunMs = millis(); // reset timer so it doesn't immediately restart
  }
}

// ----------------------------
// Update display digits from RTC or menu/edit buffer
// + Blink hour/minute tubes in set modes
// ----------------------------
void ClockApp::updateTimeDisplay() {
  static uint32_t lastUpdate = 0;

  // During cathode protection, the CP routine drives DisplayEngine
  if (_state == CLOCK_CP_RUNNING) {
    return;
  }

  uint32_t now = millis();
  bool isMenuState = (_state == CLOCK_MENU_EFFECT     ||
                      _state == CLOCK_MENU_TIMEFORMAT ||
                      _state == CLOCK_MENU_BRIGHTNESS ||
                      _state == CLOCK_MENU_CP_MODE    ||
                      _state == CLOCK_MENU_CP_INTERVAL);

  // Throttle only NORMAL + MENU; edit modes update every call for smooth blink
  if (_state == CLOCK_NORMAL) {
    if (now - lastUpdate < NORMAL_UPDATE_MS) return;
    lastUpdate = now;
  } else if (isMenuState) {
    if (now - lastUpdate < MENU_UPDATE_MS) return;
    lastUpdate = now;
  }
  // CLOCK_SET_HOUR / CLOCK_SET_MINUTE: no throttling for smooth blink

  uint8_t digits[6];

  // If in menu, show menu digits and return
  if (_state == CLOCK_MENU_EFFECT) {
    showMenuEffectDigits(digits);
    _engine.setAllDigits(digits, 6);
    return;
  } else if (_state == CLOCK_MENU_TIMEFORMAT) {
    showMenuTimeFormatDigits(digits);
    _engine.setAllDigits(digits, 6);
    return;
  } else if (_state == CLOCK_MENU_BRIGHTNESS) {
    showMenuBrightnessDigits(digits);
    _engine.setAllDigits(digits, 6);
    return;
  } else if (_state == CLOCK_MENU_CP_MODE) {
    showMenuCpModeDigits(digits);
    _engine.setAllDigits(digits, 6);
    return;
  } else if (_state == CLOCK_MENU_CP_INTERVAL) {
    showMenuCpIntervalDigits(digits);
    _engine.setAllDigits(digits, 6);
    return;
  }

  // Normal / time-set states
  uint8_t hour, minute, second;

  if (_state == CLOCK_NORMAL) {
    // Read time from RTC
    bool h12, pm;
    hour   = _rtc.getHour(h12, pm);
    minute = _rtc.getMinute();
    second = _rtc.getSecond();

    // Normalize to 24h if RTC is in 12h mode
    if (h12) {
      if (pm && hour != 12) hour += 12;
      if (!pm && hour == 12) hour = 0;
    }

    // Keep edit buffer in sync with real time when not editing
    _editHour   = hour;
    _editMinute = minute;
    _editSecond = second;
  } else {
    // In set mode → use editable buffer
    hour   = _editHour;
    minute = _editMinute;
    second = _editSecond;
  }

  // Apply time format for display
  uint8_t dispHour = hour;
  if (_settings.timeFormat == 1) { // 12h display
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

  // Blink logic in set modes
  if (_state == CLOCK_SET_HOUR || _state == CLOCK_SET_MINUTE) {
    uint32_t t = millis() % BLINK_CYCLE_MS;
    bool blinkOn = (t < BLINK_ON_MS);

    if (!blinkOn) {
      if (_state == CLOCK_SET_HOUR) {
        // Blank the hour digits only
        digits[0] = 255;
        digits[1] = 255;
      } else if (_state == CLOCK_SET_MINUTE) {
        // Blank the minute digits only
        digits[2] = 255;
        digits[3] = 255;
      }
    }
  }

  // Effects only in normal mode
  if (_state == CLOCK_NORMAL) {
    if (_effectType == 1) {
      _engine.startCrossfade(digits, _effectSpeed);
    } else if (_effectType == 2) {
      _engine.startSlot(digits, _effectSpeed);   // per-digit slot
    } else {
      _engine.setAllDigits(digits, 6);
    }
  } else {
    // In set/menu modes, always write directly (no animation)
    _engine.setAllDigits(digits, 6);
  }
}

// ----------------------------
// Handle time setting + menu UI from buttons
// ----------------------------
void ClockApp::handleTimeSetAndMenuUI() {
  uint32_t now = millis();

  // Raw button levels
  bool modeLevel = (digitalRead(_pinMode) == LOW);  // active LOW
  bool upLevel   = (digitalRead(_pinUp)   == LOW);

  // Debounced UP "press" (edge)
  bool upPressed = checkButtonPress(_pinUp, _lastUpLevel, _lastUpChangeMs);

  // --- MODE short vs long press handling ---
  bool modeShortPress = false;
  bool modeLongPress  = false;

  if (modeLevel && !_modeWasDown) {
    // Just pressed
    _modeWasDown       = true;
    _modePressStartMs  = now;
    _modeLongHandled   = false;
  } else if (modeLevel && _modeWasDown) {
    // Held
    if (!_modeLongHandled &&
        (now - _modePressStartMs >= MODE_LONG_PRESS_MS) &&
        (_state == CLOCK_NORMAL || _state == CLOCK_CP_RUNNING)) {
      modeLongPress    = true;
      _modeLongHandled = true;
    }
  } else if (!modeLevel && _modeWasDown) {
    // Just released
    _modeWasDown = false;
    if (!_modeLongHandled) {
      modeShortPress = true;
    }
    _modePressStartMs = 0;
    _modeLongHandled  = false;
  }

  // ----------------------------
  // State machine
  // ----------------------------
  switch (_state) {
    case CLOCK_NORMAL:
      if (modeLongPress) {
        // Long press from normal → enter menu
        enterMenu();
      } else if (modeShortPress) {
        // Short press from normal → enter time-set (hour)
        bool h12, pm;
        uint8_t h = _rtc.getHour(h12, pm);
        uint8_t m = _rtc.getMinute();
        uint8_t s = _rtc.getSecond();
        if (h12) {
          if (pm && h != 12) h += 12;
          if (!pm && h == 12) h = 0;
        }
        _editHour   = h;
        _editMinute = m;
        _editSecond = s;

        _state    = CLOCK_SET_HOUR;
        _upIsHeld = false;
      }
      // UP in normal mode does nothing
      break;

    case CLOCK_SET_HOUR:
      // Single step on fresh UP press
      if (upPressed) {
        incrementHour();
        _upPressStartMs = now;
        _upLastRepeatMs = now;
        _upIsHeld       = true;
      }

      // Track release
      if (!upLevel) {
        _upIsHeld = false;
      }

      // Auto-repeat while held
      if (_upIsHeld && upLevel &&
          (now - _upPressStartMs >= REPEAT_START_MS) &&
          (now - _upLastRepeatMs >= REPEAT_INTERVAL_MS)) {
        incrementHour();
        _upLastRepeatMs = now;
      }

      if (modeShortPress) {
        // Advance to minute setting on MODE short press
        _state    = CLOCK_SET_MINUTE;
        _upIsHeld = false;
      }
      break;

    case CLOCK_SET_MINUTE:
      // Single step on fresh UP press
      if (upPressed) {
        incrementMinute();
        _upPressStartMs = now;
        _upLastRepeatMs = now;
        _upIsHeld       = true;
      }

      // Track release
      if (!upLevel) {
        _upIsHeld = false;
      }

      // Auto-repeat while held
      if (_upIsHeld && upLevel &&
          (now - _upPressStartMs >= REPEAT_START_MS) &&
          (now - _upLastRepeatMs >= REPEAT_INTERVAL_MS)) {
        incrementMinute();
        _upLastRepeatMs = now;
      }

      if (modeShortPress) {
        // Commit to RTC and return to normal mode
        _rtc.setClockMode(false); // 24-hour in RTC
        _rtc.setHour(_editHour);
        _rtc.setMinute(_editMinute);
        _rtc.setSecond(0);

        _state    = CLOCK_NORMAL;
        _upIsHeld = false;
      }
      break;

    case CLOCK_MENU_EFFECT:
      // UP: cycle effect type 0..2
      if (upPressed) {
        _settings.effectType = (_settings.effectType + 1) % 3;
        applySettingsToRuntime();
      }
      // MODE short: go to next menu item (time format)
      if (modeShortPress) {
        _state = CLOCK_MENU_TIMEFORMAT;
      }
      break;

    case CLOCK_MENU_TIMEFORMAT:
      // UP: toggle time format 24h/12h
      if (upPressed) {
        _settings.timeFormat ^= 1; // 0↔1
        applySettingsToRuntime();
      }
      // MODE short: go to next menu item (brightness)
      if (modeShortPress) {
        _state = CLOCK_MENU_BRIGHTNESS;
      }
      break;

    case CLOCK_MENU_BRIGHTNESS:
      // UP: cycle brightness 0..FADE_STEPS
      if (upPressed) {
        uint8_t maxB = DisplayEngine::FADE_STEPS;
        _settings.brightness = (_settings.brightness + 1) % (maxB + 1);
        applySettingsToRuntime();
      }
      // MODE short: go to next menu item (CP mode)
      if (modeShortPress) {
        _state = CLOCK_MENU_CP_MODE;
      }
      break;

    case CLOCK_MENU_CP_MODE:
      // UP: cycle cpMode 0..3 (0=off,1=spin,2=sine,3=flicker)
      if (upPressed) {
        _settings.cpMode = (_settings.cpMode + 1) % 4;
        applySettingsToRuntime();
      }
      // MODE short: go to next menu item (CP interval)
      if (modeShortPress) {
        _state = CLOCK_MENU_CP_INTERVAL;
      }
      break;

    case CLOCK_MENU_CP_INTERVAL:
      // UP: cycle cpIntervalIdx 0..CP_INTERVAL_COUNT-1
      if (upPressed) {
        _settings.cpIntervalIdx =
          (_settings.cpIntervalIdx + 1) % CP_INTERVAL_COUNT;
        applySettingsToRuntime();
      }
      // MODE short: exit menu and save settings
      if (modeShortPress) {
        saveSettings();  // only writes if changed
        _state = CLOCK_NORMAL;
      }
      break;

    case CLOCK_CP_RUNNING:
      // Allow MODE long press to cancel CP and return to clock
      if (modeLongPress) {
        cancelCathodeProtection();
      }
      // Ignore other buttons while CP is running
      break;
  }
}

// ----------------------------
// CP helper: start a new run (freeze digits, reset timers)
// ----------------------------
void ClockApp::startCpRun() {
  // Freeze current display digits from RTC as the "final" digits
  bool h12, pm;
  uint8_t hour   = _rtc.getHour(h12, pm);
  uint8_t minute = _rtc.getMinute();
  uint8_t second = _rtc.getSecond();

  if (h12) {
    if (pm && hour != 12) hour += 12;
    if (!pm && hour == 12) hour = 0;
  }

  uint8_t dispHour = hour;
  if (_settings.timeFormat == 1) { // 12h
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

  _cpTubeIndex   = 0;
  _cpStartMs     = millis();
  _cpLastFrameMs = _cpStartMs;

  _state = CLOCK_CP_RUNNING;
}

// ----------------------------
// CP Mode 1: Spin Wave
// ----------------------------
void ClockApp::runCpModeSpinWave(uint32_t now) {
  // Wait for current tube animation to finish
  if (!_engine.isAnimating()) {
    _cpTubeIndex++;
    if (_cpTubeIndex >= 6) {
      // Finished last tube → back to normal clock
      _state       = CLOCK_NORMAL;
      _lastCpRunMs = now;   // record completion time
    } else {
      // Start slot animation for next tube only
      uint8_t slotCurrent[6];
      for (uint8_t i = 0; i < 6; i++) {
        slotCurrent[i] = _cpFinalDigits[i];
      }
      // Make this one digit "different" so startSlot() animates only it
      // Use +5 offset to guarantee it's different (won't fail even if digit is 5-9)
      slotCurrent[_cpTubeIndex] = (_cpFinalDigits[_cpTubeIndex] + 5) % 10;

      _engine.setAllDigits(slotCurrent, 6);
      _engine.startSlot(_cpFinalDigits, 350); // ~nice speed per tube
    }
  }
}

// ----------------------------
// CP Mode 2: Sine-wave roll
// ----------------------------
void ClockApp::runCpModeSineRoll(uint32_t now) {
  // Total duration for this CP mode
  const uint32_t DURATION_MS = 3500UL;
  const uint32_t FRAME_MS    = 100UL;  // Slower to avoid PWM timing issues at low brightness

  uint32_t elapsed = now - _cpStartMs;
  if (elapsed >= DURATION_MS) {
    // End CP and land on final digits
    _engine.setAllDigits(_cpFinalDigits, 6);
    _state       = CLOCK_NORMAL;
    _lastCpRunMs = now;
    return;
  }

  // Frame timing with drift compensation
  if (now - _cpLastFrameMs < FRAME_MS) {
    return; // wait until next frame
  }
  _cpLastFrameMs += FRAME_MS; // compensate for processing time

  uint8_t digits[6];

  // Phase progresses with time; each tube offset is phase-shifted
  uint8_t basePhase = (elapsed / FRAME_MS) & 0x0F; // 0..15

  for (uint8_t i = 0; i < 6; i++) {
    uint8_t phaseIndex = (basePhase + i * 2) & 0x0F; // shift per tube
    int8_t offset = CP_SINE_WAVE[phaseIndex];

    uint8_t base = _cpFinalDigits[i];
    if (base <= 9) {
      int8_t tmp = (int8_t)base + offset;
      // wrap into 0..9
      while (tmp < 0)  tmp += 10;
      while (tmp > 9)  tmp -= 10;
      digits[i] = (uint8_t)tmp;
    } else {
      // if "blank" or out-of-range, just keep it
      digits[i] = _cpFinalDigits[i];
    }
  }

  _engine.setAllDigits(digits, 6);
}

// ----------------------------
// CP Mode 3: Random flicker
// ----------------------------
void ClockApp::runCpModeRandomFlicker(uint32_t now) {
  // Total duration and final settle time
  const uint32_t TOTAL_MS   = 2500UL;
  const uint32_t SETTLE_MS  = 400UL;
  const uint32_t FLICKER_MS = 100UL;  // Slower to avoid PWM timing issues at low brightness

  uint32_t elapsed = now - _cpStartMs;
  if (elapsed >= TOTAL_MS) {
    // End CP and land on final digits
    _engine.setAllDigits(_cpFinalDigits, 6);
    _state       = CLOCK_NORMAL;
    _lastCpRunMs = now;
    return;
  }

  // In last SETTLE_MS, show the true time digits (calm down)
  if (elapsed >= (TOTAL_MS - SETTLE_MS)) {
    _engine.setAllDigits(_cpFinalDigits, 6);
    return;
  }

  // Otherwise, flicker random digits at FLICKER_MS interval
  if (now - _cpLastFrameMs < FLICKER_MS) {
    return;
  }
  _cpLastFrameMs += FLICKER_MS; // drift compensation

  uint8_t digits[6];
  for (uint8_t i = 0; i < 6; i++) {
    // 80% chance random, 20% chance actual digit, so it's not pure noise
    if (random(10) < 8) {
      digits[i] = (uint8_t)random(10);
    } else {
      digits[i] = _cpFinalDigits[i];
    }
  }

  _engine.setAllDigits(digits, 6);
}

// ----------------------------
// Cathode Protection: scheduler + mode selection
// ----------------------------
void ClockApp::handleCathodeProtection() {
  uint32_t now = millis();

  // If CP is disabled, do nothing
  if (_settings.cpMode == 0) {
    return;
  }

  // If CP is currently running, run the appropriate mode
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
        // Unknown mode -> just stop
        _engine.setAllDigits(_cpFinalDigits, 6);
        _state       = CLOCK_NORMAL;
        _lastCpRunMs = now;
        break;
    }
    return;
  }

  // Only start CP from NORMAL clock state, when not animating and not in menu
  if (_state != CLOCK_NORMAL) {
    return;
  }

  // Not enough time elapsed since last CP (handle millis() overflow correctly)
  if ((uint32_t)(now - _lastCpRunMs) < _cpIntervalMs) {
    return;
  }

  // Don't interrupt ongoing effect animation
  if (_engine.isAnimating()) {
    return;
  }

  // --- Start a new CP run ---
  startCpRun();

  // For CP-1 (spin wave) we need to kick off first tube immediately
  if (_settings.cpMode == 1) {
    uint8_t slotCurrent[6];
    for (uint8_t i = 0; i < 6; i++) {
      slotCurrent[i] = _cpFinalDigits[i];
    }
    // Make tube 0 different so startSlot() animates only it
    // Use +5 offset to guarantee it's different
    slotCurrent[0] = (_cpFinalDigits[0] + 5) % 10;

    _engine.setAllDigits(slotCurrent, 6);
    _engine.startSlot(_cpFinalDigits, 350);
  }
  // Modes 2 and 3 start animating in their runCpMode* functions
}
