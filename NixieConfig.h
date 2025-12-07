// ============================================================================
// FILE 1: NixieConfig.h
// ============================================================================

#pragma once
#include <Arduino.h>

static const uint8_t PIN_LE   = A1;
static const uint8_t PIN_DATA = A0;
static const uint8_t PIN_CLK  = A2;
static const uint8_t PIN_BL   = A3;
static const uint8_t PIN_POL  = 2;

static const uint8_t NIXIE_NUM_TUBES = 6;
static const uint8_t NIXIE_NUM_CHIPS = 2;

static const uint8_t NIXIE_TUBE_CHIP[NIXIE_NUM_TUBES] = {
  1, 1, 1, 0, 0, 0
};

static const uint8_t NIXIE_BITMAP[NIXIE_NUM_TUBES][10] = {
  { 5, 4, 3, 9, 8, 7, 6, 1, 0, 2 },
  { 15, 14, 13, 19, 18, 17, 16, 11, 10, 12 },
  { 27, 26, 25, 31, 30, 29, 28, 23, 22, 24 },
  { 5, 4, 3, 9, 8, 7, 6, 1, 0, 2 },
  { 17, 16, 15, 21, 20, 19, 18, 13, 12, 14 },
  { 27, 26, 25, 31, 30, 29, 28, 23, 22, 24 }
};