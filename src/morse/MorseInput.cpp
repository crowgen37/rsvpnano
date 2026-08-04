#include "morse/MorseInput.h"

namespace {

// Prototype tuning: capacitive-touch morse timing feel is genuinely unknown
// until tried on real hardware, per the design note this mode implements.
// Adjust freely once that's been tested.
constexpr uint32_t kDotMaxPressMs = 200;
constexpr uint32_t kLetterGapMs = 400;
constexpr uint32_t kWordGapMs = 1000;
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

  const uint32_t pressDurationMs = nowMs - pressStartMs_;
  symbols_ += (pressDurationMs < kDotMaxPressMs) ? '.' : '-';
  havePending_ = true;
  lastReleaseMs_ = nowMs;

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

  const uint32_t silenceMs = nowMs - lastReleaseMs_;
  if (silenceMs < kLetterGapMs) {
    return;
  }

  flushPendingLetter(silenceMs >= kWordGapMs);
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
