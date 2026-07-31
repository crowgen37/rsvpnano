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

// Gravity component (in g) along an axis needed to call that axis "down".
// Matches FocusTimer's side-axis threshold/cross-axis limit: the dominant
// axis must clear kFlipAxisThreshold while the other axis stays under
// kCrossAxisLimit, so a diagonal hold doesn't flicker between all four
// orientations.
constexpr float kFlipAxisThreshold = 0.55f;
constexpr float kCrossAxisLimit = 0.40f;
constexpr uint32_t kOrientationStableMs = 400;

}  // namespace

bool DigitalRain::begin() {
  accel_.begin();
  return true;
}

void DigitalRain::open() {
  frameCounter_ = 0;
  lastFrameMs_ = 0;
  uiOrientation_ =
      (rotateMode_ == RotateMode::Landscape) ? Board::UiOrientation::LandscapeFlipped : Board::UiOrientation::Portrait;
  orientationCandidate_ = uiOrientation_;
  orientationCandidateSinceMs_ = 0;
  resizeGrid();
  seedColumns();
}

void DigitalRain::close() {
  columns_.clear();
  gridColumns_ = 0;
  gridRows_ = 0;
}

void DigitalRain::resizeGrid() {
  // Portrait dims (panel-native W x H) by default — the long axis becomes
  // the fall direction, matching a real Matrix-style cascade instead of the
  // wide, short default landscape strip this board reads in. Whenever the
  // current uiOrientation_ is a landscape variant (fixed Landscape mode, or
  // Auto having tilted into one), swap to the board's landscape logical
  // dims instead so the grid always matches what's actually on screen.
  const bool landscapeShape = uiOrientation_ == Board::UiOrientation::Landscape ||
                              uiOrientation_ == Board::UiOrientation::LandscapeFlipped;
  const int baseWidth = landscapeShape ? Board::Config::DISPLAY_WIDTH : Board::Config::PANEL_NATIVE_WIDTH;
  const int baseHeight = landscapeShape ? Board::Config::DISPLAY_HEIGHT : Board::Config::PANEL_NATIVE_HEIGHT;
  gridColumns_ = static_cast<uint16_t>(std::max(1, baseWidth / cellWidth()));
  gridRows_ = static_cast<uint16_t>(std::max(1, baseHeight / cellHeight()));
}

void DigitalRain::setFontSize(FontSize size) {
  if (fontSize_ == size) {
    return;
  }
  fontSize_ = size;
  if (gridColumns_ == 0 && columns_.empty()) {
    return;
  }
  resizeGrid();
  seedColumns();
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

void DigitalRain::setRotateMode(RotateMode mode) {
  if (rotateMode_ == mode) {
    return;
  }

  rotateMode_ = mode;
  uiOrientation_ =
      (mode == RotateMode::Landscape) ? Board::UiOrientation::LandscapeFlipped : Board::UiOrientation::Portrait;
  orientationCandidate_ = uiOrientation_;
  orientationCandidateSinceMs_ = 0;

  if (!(gridColumns_ == 0 && columns_.empty())) {
    resizeGrid();
    seedColumns();
  }
}

void DigitalRain::updateTiltOrientation(uint32_t nowMs) {
  if (rotateMode_ != RotateMode::Auto || !accel_.available()) {
    return;
  }

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  if (!accel_.read(x, y, z)) {
    return;
  }

  // Full 4-way gravity follow: whichever of the two in-plane axes is
  // dominant decides portrait vs. landscape, and its sign decides which of
  // the two flipped/unflipped variants is currently "down". The raw
  // quadrant split lines up correctly with the accelerometer axes; its
  // labels just come out one 90-degree step off from the confirmed-correct
  // fixed Landscape/Portrait values, so rotate the result one step
  // counter-clockwise (Portrait -> LandscapeFlipped -> PortraitFlipped ->
  // Landscape -> Portrait) to match.
  Board::UiOrientation rawCandidate;
  if (fabsf(x) >= kFlipAxisThreshold && fabsf(y) <= kCrossAxisLimit) {
    rawCandidate = (x >= 0.0f) ? Board::UiOrientation::Landscape : Board::UiOrientation::LandscapeFlipped;
  } else if (fabsf(y) >= kFlipAxisThreshold && fabsf(x) <= kCrossAxisLimit) {
    rawCandidate = (y <= 0.0f) ? Board::UiOrientation::Portrait : Board::UiOrientation::PortraitFlipped;
  } else {
    return;
  }

  Board::UiOrientation candidate;
  switch (rawCandidate) {
    case Board::UiOrientation::Portrait:
      candidate = Board::UiOrientation::LandscapeFlipped;
      break;
    case Board::UiOrientation::LandscapeFlipped:
      candidate = Board::UiOrientation::PortraitFlipped;
      break;
    case Board::UiOrientation::PortraitFlipped:
      candidate = Board::UiOrientation::Landscape;
      break;
    case Board::UiOrientation::Landscape:
    default:
      candidate = Board::UiOrientation::Portrait;
      break;
  }

  if (candidate != orientationCandidate_) {
    orientationCandidate_ = candidate;
    orientationCandidateSinceMs_ = nowMs;
    return;
  }

  if (uiOrientation_ != candidate && (nowMs - orientationCandidateSinceMs_) >= kOrientationStableMs) {
    uiOrientation_ = candidate;
    resizeGrid();
    seedColumns();
  }
}

void DigitalRain::update(uint32_t nowMs) {
  if (gridColumns_ == 0 || columns_.empty()) {
    return;
  }

  updateTiltOrientation(nowMs);

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

  const uint16_t column = static_cast<uint16_t>(std::min<int>(gridColumns_ - 1, x / cellWidth()));
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

bool DigitalRain::isLeadGlyph(uint16_t column, uint16_t row) const {
  if (column >= gridColumns_ || row >= gridRows_) {
    return false;
  }

  return static_cast<int>(floorf(columns_[column].headRow)) == static_cast<int>(row);
}
