#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <vector>

#include "board/BoardTypes.h"
#include "sensors/Accelerometer.h"

// Matrix-style falling-glyph rain mode. Board-independent: sizes its own
// column grid off the board's portrait-orientation panel dimensions
// internally (mirrors FocusTimer's shape: begin/open/update owned by a
// self-contained model class, rendered by a dedicated DisplayManager
// method). Tracks tilt via the onboard IMU so the fall direction always
// reads as "down" whether the device is held right-side up or flipped.
class DigitalRain {
 public:
  enum class Hue : uint8_t {
    Green = 0,
    Blue,
    Yellow,
    Red,
  };

  enum class FontSize : uint8_t {
    Small = 0,
    Medium,
    Large,
  };

  enum class RotateMode : uint8_t {
    Landscape = 0,
    Portrait,
    Auto,
  };

  // Sized to match DisplayManager's tiny bitmap font (kTinyGlyphWidth=5 +
  // kTinyGlyphSpacing=1 wide, kTinyGlyphHeight=7 tall, drawn at scale 1) plus
  // a little row gap. Actual on-screen cell size scales with fontSize_; see
  // cellWidth()/cellHeight()/glyphScale().
  static constexpr uint8_t kCellWidth = 6;
  static constexpr uint8_t kCellHeight = 8;
  static constexpr uint8_t kTrailLength = 16;

  bool begin();
  void open();
  void close();
  void update(uint32_t nowMs);
  void onTouch(uint16_t x, uint16_t y, uint32_t nowMs);

  void setHue(Hue hue) { hue_ = hue; }
  Hue hue() const { return hue_; }

  void setFontSize(FontSize size);
  FontSize fontSize() const { return fontSize_; }
  uint8_t glyphScale() const { return static_cast<uint8_t>(fontSize_) + 1; }
  uint16_t cellWidth() const { return static_cast<uint16_t>(kCellWidth * glyphScale()); }
  uint16_t cellHeight() const { return static_cast<uint16_t>(kCellHeight * glyphScale()); }

  void setHighlights(bool enabled) { highlightsEnabled_ = enabled; }
  bool highlightsEnabled() const { return highlightsEnabled_; }

  void setRotateMode(RotateMode mode);
  RotateMode rotateMode() const { return rotateMode_; }

  Board::UiOrientation uiOrientation() const { return uiOrientation_; }

  uint16_t gridColumns() const { return gridColumns_; }
  uint16_t gridRows() const { return gridRows_; }
  uint32_t frameCounter() const { return frameCounter_; }

  // distanceAboveHead: 0 = the lead (brightest) glyph, up to kTrailLength-1
  // for the dimmest trailing glyph. Returns '\0'/0 outside the trail.
  char glyphAt(uint16_t column, uint16_t row) const;
  uint8_t brightnessAt(uint16_t column, uint16_t row) const;
  bool isLeadGlyph(uint16_t column, uint16_t row) const;

 private:
  struct Column {
    float headRow = 0.0f;
    float speed = 0.0f;
    uint8_t boostFrames = 0;
    char glyphs[kTrailLength] = {};
  };

  void seedColumns();
  void resetColumn(Column &column, bool staggeredStart);
  char nextRandomGlyph();
  float nextRandomUnit();
  void updateTiltOrientation(uint32_t nowMs);
  void resizeGrid();

  std::vector<Column> columns_;
  uint16_t gridColumns_ = 0;
  uint16_t gridRows_ = 0;
  uint32_t lastFrameMs_ = 0;
  uint32_t frameCounter_ = 0;
  uint32_t rngState_ = 0x2545F491u;
  Hue hue_ = Hue::Green;
  FontSize fontSize_ = FontSize::Small;
  bool highlightsEnabled_ = true;
  RotateMode rotateMode_ = RotateMode::Auto;

  Accelerometer accel_;
  Board::UiOrientation uiOrientation_ = Board::UiOrientation::Portrait;
  Board::UiOrientation orientationCandidate_ = Board::UiOrientation::Portrait;
  uint32_t orientationCandidateSinceMs_ = 0;
};
