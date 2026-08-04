#pragma once

#include <Arduino.h>
#include <stdint.h>

// Decodes touch press/release timing into International Morse Code text.
// Thresholds are all relative to a running estimate of the user's own "dit"
// length (unitMs_), not fixed millisecond values -- every press updates
// that estimate, so the decoder adapts to however fast or slow someone
// actually taps rather than assuming one fixed speed. A press shorter than
// ~2 units is a dot, longer is a dash; a pause of ~3 units closes the
// pending letter, ~7 units also inserts a word space (standard morse
// timing ratios). Eight dots in a row (morse's own error prosign) deletes
// the last word instead of decoding as a letter -- there's no separate
// backspace control.
class MorseInput {
 public:
  void reset();
  void onPressStart(uint32_t nowMs);
  void onPressEnd(uint32_t nowMs);
  // Clears an in-progress press without recording a dot/dash -- used when a
  // press ends up classified as something other than a plain tap (e.g. the
  // bottom-edge swipe that opens the settings menu).
  void cancelPress();
  void update(uint32_t nowMs);

  const String &composedText() const { return text_; }
  const String &pendingSymbols() const { return symbols_; }
  float unitMs() const { return unitMs_; }

 private:
  void flushPendingLetter();
  void deleteLastWord();
  static char decodeSymbols(const String &symbols);
  static bool isAllDots(const String &symbols);

  bool pressActive_ = false;
  bool havePending_ = false;
  // True from the moment a letter gets flushed until either a word space
  // gets inserted or a new press starts -- lets the word-gap check keep
  // watching the same silence after the letter-gap check has already fired
  // and cleared havePending_.
  bool awaitingWordGap_ = false;
  uint32_t pressStartMs_ = 0;
  uint32_t lastReleaseMs_ = 0;
  float unitMs_ = 150.0f;
  String symbols_;
  String text_;
};
