#include "rain/DigitalRain.h"

#include <algorithm>
#include <math.h>

#include "board/BoardConfig.h"

namespace {

constexpr uint32_t kFrameIntervalMs = 60;
constexpr float kFrameDeltaSeconds = kFrameIntervalMs / 1000.0f;
constexpr float kMinSpeedRowsPerSec = 4.0f;
constexpr float kMaxSpeedRowsPerSec = 10.0f;
constexpr uint8_t kTouchBoostFrames = 18;
constexpr float kTouchBoostSpeedMultiplier = 1.8f;
constexpr uint8_t kMaxBrightness = 255;

const char kGlyphAlphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr size_t kGlyphAlphabetLength = sizeof(kGlyphAlphabet) - 1;

}  // namespace

bool DigitalRain::begin() { return true; }

void DigitalRain::open() {
  // Portrait dims (panel-native W x H) — the long axis becomes the fall
  // direction, matching a real Matrix-style cascade instead of the wide,
  // short default landscape strip this board reads in.
  gridColumns_ = static_cast<uint16_t>(std::max(1, Board::Config::PANEL_NATIVE_WIDTH / kCellWidth));
  gridRows_ = static_cast<uint16_t>(std::max(1, Board::Config::PANEL_NATIVE_HEIGHT / kCellHeight));
  frameCounter_ = 0;
  lastFrameMs_ = 0;
  seedColumns();
}

void DigitalRain::close() {
  columns_.clear();
  gridColumns_ = 0;
  gridRows_ = 0;
}

void DigitalRain::seedColumns() {
  columns_.assign(gridColumns_, Column());
  for (Column &column : columns_) {
    resetColumn(column, /*staggeredStart=*/true);
  }
}

void DigitalRain::resetColumn(Column &column, bool staggeredStart) {
  column.speed = kMinSpeedRowsPerSec + nextRandomUnit() * (kMaxSpeedRowsPerSec - kMinSpeedRowsPerSec);
  column.boostFrames = 0;
  column.headRow = staggeredStart
                       ? -nextRandomUnit() * static_cast<float>(gridRows_ + kTrailLength)
                       : -static_cast<float>(kTrailLength);
  for (uint8_t i = 0; i < kTrailLength; ++i) {
    column.glyphs[i] = nextRandomGlyph();
  }
}

float DigitalRain::nextRandomUnit() {
  // xorshift32
  rngState_ ^= rngState_ << 13;
  rngState_ ^= rngState_ >> 17;
  rngState_ ^= rngState_ << 5;
  return static_cast<float>(rngState_ & 0xFFFFFFu) / static_cast<float>(0x1000000u);
}

char DigitalRain::nextRandomGlyph() {
  const size_t index = static_cast<size_t>(nextRandomUnit() * kGlyphAlphabetLength);
  return kGlyphAlphabet[index < kGlyphAlphabetLength ? index : kGlyphAlphabetLength - 1];
}

void DigitalRain::update(uint32_t nowMs) {
  if (gridColumns_ == 0 || columns_.empty()) {
    return;
  }

  if (lastFrameMs_ != 0 && (nowMs - lastFrameMs_) < kFrameIntervalMs) {
    return;
  }
  lastFrameMs_ = nowMs;
  ++frameCounter_;

  for (uint16_t i = 0; i < gridColumns_; ++i) {
    Column &column = columns_[i];
    float speedMultiplier = 1.0f;
    if (column.boostFrames > 0) {
      speedMultiplier *= kTouchBoostSpeedMultiplier;
      --column.boostFrames;
    }

    const float previousHeadRow = column.headRow;
    column.headRow += column.speed * speedMultiplier * kFrameDeltaSeconds;

    int rowsCrossed = static_cast<int>(floorf(column.headRow)) - static_cast<int>(floorf(previousHeadRow));
    while (rowsCrossed > 0) {
      for (int8_t trail = kTrailLength - 1; trail > 0; --trail) {
        column.glyphs[trail] = column.glyphs[trail - 1];
      }
      column.glyphs[0] = nextRandomGlyph();
      --rowsCrossed;
    }

    if (column.headRow - kTrailLength > static_cast<float>(gridRows_)) {
      resetColumn(column, /*staggeredStart=*/false);
    }
  }
}

void DigitalRain::onTouch(uint16_t x, uint16_t y, uint32_t nowMs) {
  (void)y;
  (void)nowMs;
  if (gridColumns_ == 0) {
    return;
  }

  const uint16_t column = static_cast<uint16_t>(std::min<int>(gridColumns_ - 1, x / kCellWidth));
  columns_[column].boostFrames = kTouchBoostFrames;
}

char DigitalRain::glyphAt(uint16_t column, uint16_t row) const {
  if (column >= gridColumns_ || row >= gridRows_) {
    return '\0';
  }

  const int distanceAboveHead =
      static_cast<int>(floorf(columns_[column].headRow)) - static_cast<int>(row);
  if (distanceAboveHead < 0 || distanceAboveHead >= kTrailLength) {
    return '\0';
  }

  return columns_[column].glyphs[distanceAboveHead];
}

uint8_t DigitalRain::brightnessAt(uint16_t column, uint16_t row) const {
  if (column >= gridColumns_ || row >= gridRows_) {
    return 0;
  }

  const int distanceAboveHead =
      static_cast<int>(floorf(columns_[column].headRow)) - static_cast<int>(row);
  if (distanceAboveHead < 0 || distanceAboveHead >= kTrailLength) {
    return 0;
  }

  const uint8_t falloff = static_cast<uint8_t>((distanceAboveHead * kMaxBrightness) / kTrailLength);
  uint16_t brightness = kMaxBrightness - falloff;
  if (columns_[column].boostFrames > 0) {
    brightness = std::min<uint16_t>(kMaxBrightness, static_cast<uint16_t>(brightness * 1.3f));
  }
  return static_cast<uint8_t>(brightness);
}
