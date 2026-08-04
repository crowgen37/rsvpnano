#include "morse/MorseInput.h"

#include <algorithm>

namespace {

// Standard morse timing ratios, all relative to the running dit-length
// estimate (unitMs_) rather than fixed values: a dash is ~3 units, so the
// dot/dash boundary sits at the midpoint (2 units); an inter-letter gap is
// ~3 units, an inter-word gap ~7 units.
constexpr float kDotDashBoundaryUnits = 2.0f;
constexpr float kLetterGapUnits = 3.0f;
constexpr float kWordGapUnits = 7.0f;
// How quickly the unit estimate follows each new press (0 = never moves,
// 1 = jumps straight to the latest press) and the range it's clamped to so
// one stray very-long/very-short tap can't derail it entirely.
constexpr float kUnitSmoothing = 0.25f;
constexpr float kMinUnitMs = 60.0f;
constexpr float kMaxUnitMs = 600.0f;
constexpr uint8_t kErrorProsignDots = 8;

struct MorseEntry {
  const char *symbols;
  char letter;
};

constexpr MorseEntry kMorseTable[] = {
    {".-", 'A'},   {"-...", 'B'}, {"-.-.", 'C'}, {"-..", 'D'},  {".", 'E'},
    {"..-.", 'F'}, {"--.", 'G'},  {"....", 'H'}, {"..", 'I'},   {".---", 'J'},
    {"-.-", 'K'},  {".-..", 'L'}, {"--", 'M'},   {"-.", 'N'},   {"---", 'O'},
    {".--.", 'P'}, {"--.-", 'Q'}, {".-.", 'R'},  {"...", 'S'},  {"-", 'T'},
    {"..-", 'U'},  {"...-", 'V'}, {".--", 'W'},  {"-..-", 'X'}, {"-.--", 'Y'},
    {"--..", 'Z'}, {"-----", '0'}, {".----", '1'}, {"..---", '2'}, {"...--", '3'},
    {"....-", '4'}, {".....", '5'}, {"-....", '6'}, {"--...", '7'}, {"---..", '8'},
    {"----.", '9'},
};
constexpr size_t kMorseTableSize = sizeof(kMorseTable) / sizeof(kMorseTable[0]);

}  // namespace

void MorseInput::reset() {
  pressActive_ = false;
  havePending_ = false;
  pressStartMs_ = 0;
  lastReleaseMs_ = 0;
  symbols_ = "";
  text_ = "";
}

void MorseInput::onPressStart(uint32_t nowMs) {
  pressActive_ = true;
  pressStartMs_ = nowMs;
}

void MorseInput::cancelPress() { pressActive_ = false; }

void MorseInput::onPressEnd(uint32_t nowMs) {
  if (!pressActive_) {
    return;
  }
  pressActive_ = false;

  const float pressDurationMs = static_cast<float>(nowMs - pressStartMs_);
  const bool isDash = pressDurationMs >= unitMs_ * kDotDashBoundaryUnits;
  symbols_ += isDash ? '-' : '.';
  havePending_ = true;
  lastReleaseMs_ = nowMs;

  // Fold this press into the running unit estimate -- a dash gets divided
  // back down to its dit-equivalent length first, so both symbol types
  // keep the estimate tracking the user's actual current speed.
  const float observedUnitMs = isDash ? (pressDurationMs / 3.0f) : pressDurationMs;
  unitMs_ = unitMs_ * (1.0f - kUnitSmoothing) + observedUnitMs * kUnitSmoothing;
  unitMs_ = std::max(kMinUnitMs, std::min(kMaxUnitMs, unitMs_));

  if (symbols_.length() >= kErrorProsignDots && isAllDots(symbols_)) {
    deleteLastWord();
    symbols_ = "";
    havePending_ = false;
  }
}

void MorseInput::update(uint32_t nowMs) {
  if (pressActive_ || !havePending_) {
    return;
  }

  const float silenceMs = static_cast<float>(nowMs - lastReleaseMs_);
  if (silenceMs < unitMs_ * kLetterGapUnits) {
    return;
  }

  flushPendingLetter(silenceMs >= unitMs_ * kWordGapUnits);
}

void MorseInput::flushPendingLetter(bool addWordSpace) {
  if (symbols_.length() > 0) {
    const char decoded = decodeSymbols(symbols_);
    if (decoded != '\0') {
      text_ += decoded;
    }
  }
  symbols_ = "";
  havePending_ = false;
  if (addWordSpace && text_.length() > 0 && text_[text_.length() - 1] != ' ') {
    text_ += ' ';
  }
}

void MorseInput::deleteLastWord() {
  while (text_.length() > 0 && text_[text_.length() - 1] == ' ') {
    text_.remove(text_.length() - 1);
  }
  const int lastSpace = text_.lastIndexOf(' ');
  if (lastSpace < 0) {
    text_ = "";
  } else {
    text_ = text_.substring(0, lastSpace + 1);
  }
}

char MorseInput::decodeSymbols(const String &symbols) {
  for (size_t i = 0; i < kMorseTableSize; ++i) {
    if (symbols == kMorseTable[i].symbols) {
      return kMorseTable[i].letter;
    }
  }
  return '\0';
}

bool MorseInput::isAllDots(const String &symbols) {
  if (symbols.length() == 0) {
    return false;
  }
  for (size_t i = 0; i < symbols.length(); ++i) {
    if (symbols[i] != '.') {
      return false;
    }
  }
  return true;
}
