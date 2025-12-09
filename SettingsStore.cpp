#include "SettingsStore.h"
#include <string.h>

SettingsStore::SettingsStore()
  : _dirty(false)
{
  // Default baseline settings; will be overridden by EEPROM if valid.
  _settings.magic             = SETTINGS_MAGIC;
  _settings.version           = SETTINGS_VERSION;

  _settings.timeFormat        = 0;    // 24h
  _settings.effectType        = 1;    // crossfade

  _settings.dayBrightness     = 16;   // full (assuming FADE_STEPS = 16)
  _settings.nightBrightness   = 4;    // dim but visible

  _settings.nightModeEnabled  = 1;    // night dimming enabled
  _settings.masterBlankEnabled= 0;    // no blanking by default

  _settings.cpMode            = 1;    // CP-A
  _settings.cpIntervalIdx     = 2;    // index 2 (e.g., 10 minutes)

  _settings.nightStartHour    = 22;   // 22:00 → 07:00 night window
  _settings.nightEndHour      = 7;

  _settings.blankStartHour    = 23;   // 23:00 → 06:00 blank window
  _settings.blankEndHour      = 6;

  _settings.reserved1         = 0;
  _settings.reserved2         = 0;

  _savedCopy = _settings;
}

void SettingsStore::begin() {
  loadFromEepromOrInitDefaults();
}

void SettingsStore::markDirty() {
  _dirty = true;
}

void SettingsStore::saveIfDirty() {
  if (!_dirty) return;

  if (memcmp(&_settings, &_savedCopy, sizeof(ClockSettings)) != 0) {
    writeToEeprom();
    _savedCopy = _settings;
  }
  _dirty = false;
}

void SettingsStore::loadFromEepromOrInitDefaults() {
  ClockSettings tmp;
  EEPROM.get(EEPROM_ADDR_SETTINGS, tmp);

  if (tmp.magic != SETTINGS_MAGIC || tmp.version != SETTINGS_VERSION) {
    // EEPROM not initialized or wrong version → keep defaults
    writeToEeprom();   // store defaults so next boot finds valid data
    _savedCopy = _settings;
  } else {
    _settings  = tmp;
    _savedCopy = tmp;
  }
}

void SettingsStore::writeToEeprom() {
  EEPROM.put(EEPROM_ADDR_SETTINGS, _settings);
}
