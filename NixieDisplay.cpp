// ============================================================================
// FILE 5: NixieDisplay.cpp
// ============================================================================

#include "NixieDisplay.h"

NixieDisplay::NixieDisplay(Hv5622DriverFast& driver) : _drv(driver) {
  for (uint8_t t = 0; t < NIXIE_NUM_TUBES; t++) {
    _digits[t] = 255;   // 255 = blank
  }
}

void NixieDisplay::begin() {
  _drv.begin();
  _drv.clearFrame();
  _drv.setBlank(false);
}

void NixieDisplay::setDigit(uint8_t tube, uint8_t digit) {
  if (tube >= NIXIE_NUM_TUBES) return;
  if (digit > 9 && digit != 255) return;  // allow 0–9 or 255=blank
  _digits[tube] = digit;
}

void NixieDisplay::setDigits(const uint8_t* digits, uint8_t count) {
  if (count > NIXIE_NUM_TUBES) count = NIXIE_NUM_TUBES;
  for (uint8_t t = 0; t < count; t++) {
    uint8_t d = digits[t];
    if (d == 255) {
      _digits[t] = 255;           // blank
    } else {
      if (d > 9) d = 0;
      _digits[t] = d;
    }
  }
}

void NixieDisplay::render() {
  _drv.clearFrame();
  for (uint8_t t = 0; t < NIXIE_NUM_TUBES; t++) {
    uint8_t dig = _digits[t];

    // 255 = fully blank this tube
    if (dig == 255) continue;

    uint8_t chip = NIXIE_TUBE_CHIP[t];
    uint8_t bit  = NIXIE_BITMAP[t][dig];
    _drv.setBit(chip, bit, true);
  }
  _drv.flush();
}

