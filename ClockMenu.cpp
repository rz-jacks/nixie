#include "ClockMenu.h"
#include "ClockApp.h"   // for CP_INTERVAL_MINUTES, CP_INTERVAL_COUNT

ClockMenu::ClockMenu(DisplayEngine& engine, SettingsStore& store)
  : _engine(engine),
    _store(store),
    _settings(store.settings()),
    _state(MENU_INACTIVE),
    _lastUpdateMs(0)
{
}

void ClockMenu::begin() {
  // no-op for now
}

void ClockMenu::start() {
  _state        = MENU_EFFECT;
  _lastUpdateMs = 0;
  showCurrentPage();
}

void ClockMenu::tick(bool modeShortPress, bool upPressed) {
  if (_state == MENU_INACTIVE) return;

  uint32_t now = millis();

  // Advance page on MODE short press
  if (modeShortPress) {
    uint8_t s = static_cast<uint8_t>(_state);
    s++;
    if (s >= MENU_STATE_COUNT) {
      _state = MENU_INACTIVE;
      return;
    } else {
      _state = static_cast<MenuState>(s);
    }
  }

  // Adjust value on UP press
  if (upPressed) {
    switch (_state) {
      case MENU_EFFECT:
        _settings.effectType = (_settings.effectType + 1) % 3; // 0..2
        _store.markDirty();
        break;

      case MENU_TIMEFORMAT:
        _settings.timeFormat ^= 1; // 0 <-> 1
        _store.markDirty();
        break;

      case MENU_BRIGHTNESS: {
        uint8_t maxB = DisplayEngine::FADE_STEPS;
        uint8_t b    = _settings.dayBrightness;
        if (b < 1) b = 1;
        b++;
        if (b > maxB) b = 1;
        _settings.dayBrightness = b;
        _store.markDirty();
        break;
      }

      case MENU_CP_MODE:
        _settings.cpMode = (_settings.cpMode + 1) % 4; // 0..3
        _store.markDirty();
        break;

      case MENU_CP_INTERVAL:
        _settings.cpIntervalIdx =
          (uint8_t)((_settings.cpIntervalIdx + 1) % ClockApp::CP_INTERVAL_COUNT);
        _store.markDirty();
        break;

      case MENU_NIGHT_MODE:
        _settings.nightModeEnabled ^= 1; // 0/1
        _store.markDirty();
        break;

      case MENU_NIGHT_BRIGHTNESS: {
        uint8_t maxB = DisplayEngine::FADE_STEPS;
        uint8_t b    = _settings.nightBrightness;
        if (b < 1) b = 1;
        b++;
        if (b > maxB) b = 1;
        _settings.nightBrightness = b;
        _store.markDirty();
        break;
      }

      case MENU_NIGHT_START:
        _settings.nightStartHour = (uint8_t)((_settings.nightStartHour + 1) % 24);
        _store.markDirty();
        break;

      case MENU_NIGHT_END:
        _settings.nightEndHour = (uint8_t)((_settings.nightEndHour + 1) % 24);
        _store.markDirty();
        break;

      case MENU_BLANK_START:
        _settings.blankStartHour = (uint8_t)((_settings.blankStartHour + 1) % 24);
        _store.markDirty();
        break;

      case MENU_BLANK_END:
        _settings.blankEndHour = (uint8_t)((_settings.blankEndHour + 1) % 24);
        _store.markDirty();
        break;

      case MENU_LEADING_ZERO:
        // reserved – no-op for now
        break;

      case MENU_DATE_MODE:
        // reserved – no-op for now
        break;

      case MENU_MASTER_BLANK:
        _settings.masterBlankEnabled ^= 1;
        _store.markDirty();
        break;

      default:
        break;
    }
  }

  // Refresh display periodically or on changes
  if ((now - _lastUpdateMs >= MENU_UPDATE_MS) || modeShortPress || upPressed) {
    _lastUpdateMs = now;
    showCurrentPage();
  }
}

void ClockMenu::showCurrentPage() {
  if (_state == MENU_INACTIVE) return;

  uint8_t digits[6] = {0, 0, 0, 0, 0, 0};

  // First two tubes = menu page number
  uint8_t page = static_cast<uint8_t>(_state);
  digits[0]    = page / 10;
  digits[1]    = page % 10;

  switch (_state) {
    case MENU_EFFECT:          showEffectPage(digits);          break;
    case MENU_TIMEFORMAT:      showTimeFormatPage(digits);      break;
    case MENU_BRIGHTNESS:      showBrightnessPage(digits);      break;
    case MENU_CP_MODE:         showCpModePage(digits);          break;
    case MENU_CP_INTERVAL:     showCpIntervalPage(digits);      break;
    case MENU_NIGHT_MODE:      showNightModePage(digits);       break;
    case MENU_NIGHT_BRIGHTNESS:showNightBrightnessPage(digits); break;
    case MENU_NIGHT_START:     showNightStartPage(digits);      break;
    case MENU_NIGHT_END:       showNightEndPage(digits);        break;
    case MENU_BLANK_START:     showBlankStartPage(digits);      break;
    case MENU_BLANK_END:       showBlankEndPage(digits);        break;
    case MENU_LEADING_ZERO:    showLeadingZeroPage(digits);     break;
    case MENU_DATE_MODE:       showDateModePage(digits);        break;
    case MENU_MASTER_BLANK:    showMasterBlankPage(digits);     break;
    default:
      break;
  }

  _engine.setAllDigits(digits, 6);
}

// Helper to display minutes as two digits (e.g. 1,5,10,30,60)
static void encodeTwoDigit(uint8_t value, uint8_t &tens, uint8_t &ones) {
  tens = (value / 10) % 10;
  ones = value % 10;
}

// -------------------------
// Individual pages
// -------------------------
void ClockMenu::showEffectPage(uint8_t* digits) {
  digits[4] = 0;
  digits[5] = _settings.effectType;  // 0,1,2
}

void ClockMenu::showTimeFormatPage(uint8_t* digits) {
  if (_settings.timeFormat == 0) {
    digits[4] = 2;
    digits[5] = 4;
  } else {
    digits[4] = 1;
    digits[5] = 2;
  }
}

void ClockMenu::showBrightnessPage(uint8_t* digits) {
  uint8_t b = _settings.dayBrightness;
  if (b < 1) b = 1;
  if (b > DisplayEngine::FADE_STEPS) b = DisplayEngine::FADE_STEPS;

  digits[4] = b / 10;
  digits[5] = b % 10;
}

void ClockMenu::showCpModePage(uint8_t* digits) {
  digits[5] = _settings.cpMode;  // 0..3
}

void ClockMenu::showCpIntervalPage(uint8_t* digits) {
  uint8_t idx = _settings.cpIntervalIdx;
  if (idx >= ClockApp::CP_INTERVAL_COUNT) {
    idx = ClockApp::CP_INTERVAL_COUNT - 1;
  }
  uint8_t mins = ClockApp::CP_INTERVAL_MINUTES[idx];

  encodeTwoDigit(mins, digits[4], digits[5]);
}

void ClockMenu::showNightModePage(uint8_t* digits) {
  digits[5] = (_settings.nightModeEnabled ? 1 : 0);
}

void ClockMenu::showNightBrightnessPage(uint8_t* digits) {
  uint8_t b = _settings.nightBrightness;
  if (b < 1) b = 1;
  if (b > DisplayEngine::FADE_STEPS) b = DisplayEngine::FADE_STEPS;

  digits[4] = b / 10;
  digits[5] = b % 10;
}

void ClockMenu::showNightStartPage(uint8_t* digits) {
  uint8_t h = _settings.nightStartHour % 24;
  digits[4] = h / 10;
  digits[5] = h % 10;
}

void ClockMenu::showNightEndPage(uint8_t* digits) {
  uint8_t h = _settings.nightEndHour % 24;
  digits[4] = h / 10;
  digits[5] = h % 10;
}

void ClockMenu::showBlankStartPage(uint8_t* digits) {
  uint8_t h = _settings.blankStartHour % 24;
  digits[4] = h / 10;
  digits[5] = h % 10;
}

void ClockMenu::showBlankEndPage(uint8_t* digits) {
  uint8_t h = _settings.blankEndHour % 24;
  digits[4] = h / 10;
  digits[5] = h % 10;
}

void ClockMenu::showLeadingZeroPage(uint8_t* digits) {
  // reserved – show 00 for now
  digits[4] = 0;
  digits[5] = 0;
}

void ClockMenu::showDateModePage(uint8_t* digits) {
  // reserved – show 00 for now
  digits[4] = 0;
  digits[5] = 0;
}

void ClockMenu::showMasterBlankPage(uint8_t* digits) {
  digits[5] = (_settings.masterBlankEnabled ? 1 : 0);
}
