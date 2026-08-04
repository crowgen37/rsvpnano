#pragma once

#include <Arduino.h>
#include <stdint.h>

// Decodes touch press/release timing into International Morse Code text.
// A press shorter than kDotMaxPressMs is a dot, longer is a dash; a pause
// longer than kLetterGapMs closes out the pending letter, and one longer
// than kWordGapMs also inserts a word space. Eight dots in a row (morse's
// own error prosign) deletes the last word instead of decoding as a
// letter -- there's no separate backspace control.
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

 private:
  void flushPendingLetter(bool addWordSpace);
  void deleteLastWord();
  static char decodeSymbols(const String &symbols);
  static bool isAllDots(const String &symbols);

  bool pressActive_ = false;
  bool havePending_ = false;
  uint32_t pressStartMs_ = 0;
  uint32_t lastReleaseMs_ = 0;
  String symbols_;
  String text_;
};
