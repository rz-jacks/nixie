
// ============================================================================
// FILE 2: Hv5622DriverFast.h
// ============================================================================

#pragma once
#include <Arduino.h>
#include "NixieConfig.h"

class Hv5622DriverFast {
public:
  Hv5622DriverFast(uint8_t lePin, uint8_t dataPin, uint8_t clkPin,
                   uint8_t blPin, uint8_t polPin);
  void begin();
  void clearFrame();
  void setBit(uint8_t chip, uint8_t bitIndex, bool on);
  uint32_t getFrame(uint8_t chip) const { return _frame[chip]; }
  void flush();
  void setBlank(bool blank);
  void setPolarity(bool inverted);

private:
  uint8_t _le, _data, _clk, _bl, _pol;
  uint32_t _frame[NIXIE_NUM_CHIPS];
  
  volatile uint8_t* _portLE;
  volatile uint8_t* _portDATA;
  volatile uint8_t* _portCLK;
  volatile uint8_t* _portBL;
  volatile uint8_t* _portPOL;
  uint8_t _maskLE, _maskDATA, _maskCLK, _maskBL, _maskPOL;
  
  void shift64(uint32_t chip0, uint32_t chip1);
};