#include "DisplayEngine.h"

DisplayEngine::DisplayEngine(NixieDisplay& display)
  : _display(display),
    _mode(MODE_IDLE),
    _animationMs(0),
    _elapsedMs(0),
    _pwmPhase(0),
    _slotStepInterval(50),
    _slotStepCounter(0),
    _globalBrightness(FADE_STEPS)    // default: full brightness
{
  for (uint8_t i = 0; i < 6; i++) {
    _currentDigits[i]      = 0;
    _targetDigits[i]       = 0;
    _slotStepsRemaining[i] = 0;
    _lastRenderedDigits[i] = 255;  // Initialize to "invalid" to force first render
  }
}

void DisplayEngine::begin() {
  _display.begin();
  // Initial setup - render immediately to show something on startup
  _display.setDigits(_currentDigits, 6);
  _display.render();
  // Update tracking state
  for (uint8_t i = 0; i < 6; i++) {
    _lastRenderedDigits[i] = _currentDigits[i];
  }
}

void DisplayEngine::setAllInstant(uint8_t digit) {
  if (digit > 9) digit = 0;
  for (uint8_t i = 0; i < 6; i++) {
    _currentDigits[i]      = digit;
    _targetDigits[i]       = digit;
    _slotStepsRemaining[i] = 0;
  }
  _mode = MODE_IDLE;
  // Don't render directly - let tick1ms() -> renderFrame() handle it smoothly
  // This prevents brightness glitches during PWM cycling
}

void DisplayEngine::setAllDigits(const uint8_t* digits, uint8_t count) {
  if (count > 6) count = 6;
  for (uint8_t i = 0; i < count; i++) {
    uint8_t d = digits[i];
    if (d == 255) {
      // preserve "blank" digit
      _currentDigits[i]      = 255;
      _targetDigits[i]       = 255;
      _slotStepsRemaining[i] = 0;
    } else {
      _currentDigits[i]      = (d > 9) ? 0 : d;
      _targetDigits[i]       = _currentDigits[i];
      _slotStepsRemaining[i] = 0;
    }
  }
  _mode = MODE_IDLE;
  // Don't render directly - let tick1ms() -> renderFrame() handle it smoothly
  // This prevents brightness glitches during PWM cycling
}

void DisplayEngine::startCrossfade(const uint8_t* targetDigits, uint16_t durationMs) {
  if (durationMs < FADE_STEPS) durationMs = FADE_STEPS;

  for (uint8_t i = 0; i < 6; i++) {
    _targetDigits[i]       = (targetDigits[i] > 9) ? 0 : targetDigits[i];
    _slotStepsRemaining[i] = 0;
  }

  _mode        = MODE_CROSSFADE;
  _animationMs = durationMs;
  _elapsedMs   = 0;
  // DON'T reset _pwmPhase - let it continue cycling to avoid brightness dips
}

void DisplayEngine::startSlot(const uint8_t* targetDigits, uint16_t durationMs) {
  // durationMs ≈ total time for 10 steps (slot machine animation)
  if (durationMs == 0) durationMs = 300;  // default ~300 ms

  bool anyChanging = false;

  for (uint8_t i = 0; i < 6; i++) {
    uint8_t t = (targetDigits[i] > 9) ? 0 : targetDigits[i];
    _targetDigits[i] = t;

    if (_currentDigits[i] == 255) {
      // If blank, just snap to target later
      _slotStepsRemaining[i] = 0;
    } else if (_currentDigits[i] != t) {
      // This digit will do a full round: 10 steps
      _slotStepsRemaining[i] = 10;
      anyChanging = true;
    } else {
      _slotStepsRemaining[i] = 0;
    }
  }

  if (!anyChanging) {
    // Nothing to animate - targets already set in loop above
    // Don't modify _currentDigits to avoid conflicts with PWM tracking
    // Just stay in current mode (likely already IDLE)
    return;
  }

  // Step interval = duration / 10 steps (clamped)
  uint16_t step = durationMs / 10;
  if (step < 20)  step = 20;   // minimum speed (not too fast)
  if (step > 250) step = 250;  // maximum speed (not too slow)

  _slotStepInterval = step;
  _slotStepCounter  = 0;

  _mode        = MODE_SLOT;
  _animationMs = durationMs;   // kept for consistency
  _elapsedMs   = 0;
}

void DisplayEngine::tick1ms() {
  _pwmPhase = (_pwmPhase + 1) % FADE_STEPS;

  bool needsRender = false;

  if (_mode == MODE_CROSSFADE) {
    updateCrossfade();
    needsRender = true;  // Always render during crossfade animation
  } else if (_mode == MODE_SLOT) {
    updateSlot();
    needsRender = true;  // Always render during slot animation
  } else if (_globalBrightness > 0) {
    // MODE_IDLE with brightness > 0: render for PWM cycling
    needsRender = true;
  }
  // If brightness is 0, skip rendering entirely (display is off)

  if (needsRender) {
    renderFrame();
  }
}

void DisplayEngine::updateCrossfade() {
  _elapsedMs++;

  if (_elapsedMs >= _animationMs) {
    // Animation complete - snap to target
    for (uint8_t i = 0; i < 6; i++) {
      _currentDigits[i] = _targetDigits[i];
    }
    _mode = MODE_IDLE;
  }
}

void DisplayEngine::updateSlot() {
  _elapsedMs++;
  _slotStepCounter++;

  if (_slotStepCounter >= _slotStepInterval) {
    _slotStepCounter = 0;

    bool anyActive = false;

    for (uint8_t i = 0; i < 6; i++) {
      if (_slotStepsRemaining[i] > 0) {
        anyActive = true;

        // Decrement digit with wrap: 3→2→1→0→9→8→...
        // Creates "slot machine" rolling effect
        if (_currentDigits[i] <= 9) {
          _currentDigits[i] = (_currentDigits[i] + 9) % 10; // -1 mod 10
        }
        _slotStepsRemaining[i]--;
      }
    }

    if (!anyActive) {
      // All digits completed 10 steps → snap to targets
      for (uint8_t i = 0; i < 6; i++) {
        _currentDigits[i] = _targetDigits[i];
      }
      _mode = MODE_IDLE;
      return;
    }
  }
}

void DisplayEngine::renderFrame() {
  uint8_t displayDigits[6];

  // Precompute fade progress if in crossfade
  uint16_t fadeProgress  = 0;
  uint8_t  thresholdFade = 0;
  if (_mode == MODE_CROSSFADE && _animationMs > 0) {
    // Use 32-bit math to avoid overflow: (_elapsedMs * FADE_STEPS)
    uint32_t num = (uint32_t)_elapsedMs * (uint32_t)FADE_STEPS;
    fadeProgress = (uint16_t)(num / (uint32_t)_animationMs);
    if (fadeProgress > FADE_STEPS) fadeProgress = FADE_STEPS;
    thresholdFade = FADE_STEPS - fadeProgress;
  }

  for (uint8_t i = 0; i < 6; i++) {
    uint8_t effectiveBrightness = _globalBrightness;
    if (effectiveBrightness > FADE_STEPS) effectiveBrightness = FADE_STEPS;

    if (effectiveBrightness == 0) {
      // Completely off regardless of fade
      displayDigits[i] = 255;
      continue;
    }

    // If we're beyond the allowed brightness duty, blank
    if (_pwmPhase >= effectiveBrightness) {
      displayDigits[i] = 255;
      continue;
    }

    // Decide what to show based on mode
    if (_mode == MODE_CROSSFADE) {
      // Fade between current and target using same PWM phase
      // This creates a smooth crossfade at any brightness level
      bool useOld = (_pwmPhase < thresholdFade);
      displayDigits[i] = useOld ? _currentDigits[i] : _targetDigits[i];
    } else {
      // SLOT or IDLE: just show current digit (already animated)
      displayDigits[i] = _currentDigits[i];
    }

    // If the chosen digit is 255 (blank), it stays blank regardless
  }

  // Optimization: Only update hardware if display state actually changed
  bool changed = false;
  for (uint8_t i = 0; i < 6; i++) {
    if (displayDigits[i] != _lastRenderedDigits[i]) {
      changed = true;
      break;  // Early exit - no need to check remaining digits
    }
  }

  if (changed) {
    _display.setDigits(displayDigits, 6);
    _display.render();
    // Update tracking AFTER successful render
    for (uint8_t i = 0; i < 6; i++) {
      _lastRenderedDigits[i] = displayDigits[i];
    }
  }
}
