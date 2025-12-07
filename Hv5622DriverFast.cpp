// ============================================================================
// FILE 3: Hv5622DriverFast.cpp
// ============================================================================

#include "Hv5622DriverFast.h"

Hv5622DriverFast::Hv5622DriverFast(uint8_t lePin, uint8_t dataPin, uint8_t clkPin,
                                   uint8_t blPin, uint8_t polPin)
: _le(lePin), _data(dataPin), _clk(clkPin), _bl(blPin), _pol(polPin),
  _portLE(nullptr), _portDATA(nullptr), _portCLK(nullptr),
  _portBL(nullptr), _portPOL(nullptr),
  _maskLE(0), _maskDATA(0), _maskCLK(0), _maskBL(0), _maskPOL(0)
{
  clearFrame();
}

void Hv5622DriverFast::begin() {
  pinMode(_le, OUTPUT);
  pinMode(_data, OUTPUT);
  pinMode(_clk, OUTPUT);
  pinMode(_bl, OUTPUT);
  pinMode(_pol, OUTPUT);
  
  uint8_t port;
  
  port = digitalPinToPort(_le);
  _portLE = portOutputRegister(port);
  _maskLE = digitalPinToBitMask(_le);
  
  port = digitalPinToPort(_data);
  _portDATA = portOutputRegister(port);
  _maskDATA = digitalPinToBitMask(_data);
  
  port = digitalPinToPort(_clk);
  _portCLK = portOutputRegister(port);
  _maskCLK = digitalPinToBitMask(_clk);
  
  port = digitalPinToPort(_bl);
  _portBL = portOutputRegister(port);
  _maskBL = digitalPinToBitMask(_bl);
  
  port = digitalPinToPort(_pol);
  _portPOL = portOutputRegister(port);
  _maskPOL = digitalPinToBitMask(_pol);
  
  *_portLE &= ~_maskLE;
  *_portCLK &= ~_maskCLK;
  *_portDATA &= ~_maskDATA;
  
  setBlank(false);
  setPolarity(true);
}

void Hv5622DriverFast::clearFrame() {
  for (uint8_t c = 0; c < NIXIE_NUM_CHIPS; c++) {
    _frame[c] = 0;
  }
}

void Hv5622DriverFast::setBit(uint8_t chip, uint8_t bitIndex, bool on) {
  if (chip >= NIXIE_NUM_CHIPS || bitIndex > 31) return;
  uint32_t mask = (1UL << bitIndex);
  if (on) {
    _frame[chip] |= mask;
  } else {
    _frame[chip] &= ~mask;
  }
}

void Hv5622DriverFast::setBlank(bool blank) {
  if (blank) {
    *_portBL &= ~_maskBL;
  } else {
    *_portBL |= _maskBL;
  }
}

void Hv5622DriverFast::setPolarity(bool inverted) {
  if (inverted) {
    *_portPOL |= _maskPOL;
  } else {
    *_portPOL &= ~_maskPOL;
  }
}

void Hv5622DriverFast::shift64(uint32_t chip0, uint32_t chip1) {
  uint64_t combined = ((uint64_t)chip1 << 32) | chip0;
  
  noInterrupts();
  for (int8_t i = 63; i >= 0; i--) {
    bool bit = (combined >> i) & 1U;
    
    if (bit) {
      *_portDATA |= _maskDATA;
    } else {
      *_portDATA &= ~_maskDATA;
    }
    
    *_portCLK |= _maskCLK;
    *_portCLK &= ~_maskCLK;
  }
  
  *_portLE |= _maskLE;
  *_portLE &= ~_maskLE;
  interrupts();
}

void Hv5622DriverFast::flush() {
  shift64(_frame[0], _frame[1]);
}
