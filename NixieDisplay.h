// ============================================================================
// FILE 4: NixieDisplay.h
// ============================================================================

#pragma once
#include <Arduino.h>
#include "NixieConfig.h"
#include "Hv5622DriverFast.h"

// Note: digit value 255 = "blank tube"

class NixieDisplay {
public:
  NixieDisplay(Hv5622DriverFast& driver);
  void begin();
  void setDigit(uint8_t tube, uint8_t digit);
  void setDigits(const uint8_t* digits, uint8_t count);
  void render();
  uint8_t getDigit(uint8_t tube) const { return _digits[tube]; }

private:
  Hv5622DriverFast& _drv;
  uint8_t _digits[NIXIE_NUM_TUBES];
};
