#include "timer/FocusTimer.h"

#include <math.h>

#include "board/BoardImu.h"

namespace {

constexpr uint32_t kOrientationStableMs = 700;
constexpr uint32_t kTouchStartArmDelayMs = 350;
constexpr uint32_t kPostTimerFlipGraceMs = 900;
constexpr uint32_t kFeedbackMs = 900;
constexpr uint32_t kTouchDurationMs = 2UL * 60UL * 1000UL;
constexpr uint32_t kWorkDurationMs = 20UL * 60UL * 1000UL;
constexpr uint32_t kBreakDurationMs = 5UL * 60UL * 1000UL;

constexpr float kSideAxisThreshold = 0.78f;
constexpr float kCrossAxisLimit = 0.42f;
constexpr float kFlatAxisThreshold = 0.84f;

}  // namespace

bool FocusTimer::begin() { return accel_.begin(); }

void FocusTimer::open() {
  if (!accel_.available()) {
    accel_.begin();
  }

  clearSession();
  resetOrientationStability();
  state_ = accel_.available() ? State::GenreSelect : State::Unavailable;
  stateStartedMs_ = millis();
}

void FocusTimer::update(uint32_t nowMs) {
  if (accel_.available()) {
    updateOrientation(nowMs);
  }

  switch (state_) {
    case State::Unavailable:
    case State::GenreSelect:
      break;

    case State::WaitForTouchStart:
      if (orientationInputArmed(nowMs) && isShortSide(stableOrientation_)) {
        startMode(TimerMode::Touch, nowMs, kTouchDurationMs, stableOrientation_);
        transitionTo(State::TouchRunning, nowMs);
      }
      break;

    case State::TouchRunning:
      if (timerExpired(nowMs)) {
        completeActiveTimer();
        resetOrientationStability();
        transitionTo(State::WaitAfterTouch, nowMs);
      }
      break;

    case State::WaitAfterTouch:
      if (!orientationInputArmed(nowMs)) {
        break;
      }
      if (stableOrientation_ == oppositeShortSide(lastShortSide_)) {
        startMode(TimerMode::Work, nowMs, kWorkDurationMs, stableOrientation_);
        transitionTo(State::WorkRunning, nowMs);
      } else if (stableOrientation_ == OrientationState::LongSide) {
        startMode(TimerMode::Break, nowMs, kBreakDurationMs, OrientationState::LongSide);
        transitionTo(State::BreakRunning, nowMs);
      }
      break;

    case State::WorkRunning:
      if (timerExpired(nowMs)) {
        completeActiveTimer();
        resetOrientationStability();
        transitionTo(State::WaitAfterWork, nowMs);
      }
      break;

    case State::WaitAfterWork:
      if (!orientationInputArmed(nowMs)) {
        break;
      }
      if (stableOrientation_ == oppositeShortSide(lastShortSide_)) {
        startMode(TimerMode::Work, nowMs, kWorkDurationMs, stableOrientation_);
        transitionTo(State::WorkRunning, nowMs);
      } else if (stableOrientation_ == OrientationState::LongSide) {
        startMode(TimerMode::Break, nowMs, kBreakDurationMs, OrientationState::LongSide);
        transitionTo(State::BreakRunning, nowMs);
      }
      break;

    case State::BreakRunning:
      if (timerExpired(nowMs)) {
        completeActiveTimer();
        resetOrientationStability();
        transitionTo(State::WaitAfterBreak, nowMs);
      }
      break;

    case State::WaitAfterBreak:
      if (orientationInputArmed(nowMs) && isShortSide(stableOrientation_)) {
        startMode(TimerMode::Work, nowMs, kWorkDurationMs, stableOrientation_);
        transitionTo(State::WorkRunning, nowMs);
      }
      break;

    case State::Cancelled:
      if (nowMs - feedbackStartedMs_ >= kFeedbackMs) {
        resetOrientationStability();
        transitionTo(State::WaitForTouchStart, nowMs);
      }
      break;

    case State::Complete:
      if (nowMs - feedbackStartedMs_ >= kFeedbackMs) {
        clearSession();
        resetOrientationStability();
        transitionTo(accel_.available() ? State::GenreSelect : State::Unavailable, nowMs);
      }
      break;
  }
}

void FocusTimer::chooseGenre(Genre genre, uint32_t nowMs) {
  if (genre == Genre::None) {
    return;
  }

  clearSession();
  genre_ = genre;
  resetOrientationStability();
  transitionTo(State::WaitForTouchStart, nowMs);
}

void FocusTimer::cancelActiveTimer(uint32_t nowMs) {
  if (!timerRunning_) {
    return;
  }

  stopActiveTimer();
  resetOrientationStability();
  feedbackStartedMs_ = nowMs;
  transitionTo(State::Cancelled, nowMs);
}

void FocusTimer::abandon() {
  clearSession();
  resetOrientationStability();
  state_ = accel_.available() ? State::GenreSelect : State::Unavailable;
  stateStartedMs_ = millis();
}

bool FocusTimer::available() const { return accel_.available(); }

bool FocusTimer::isActiveTimerRunning() const { return timerRunning_; }

FocusTimer::State FocusTimer::state() const { return state_; }

FocusTimer::Genre FocusTimer::genre() const { return genre_; }

Board::UiOrientation FocusTimer::uiOrientation() const {
  switch (state_) {
    case State::GenreSelect:
    case State::Unavailable:
    case State::Complete:
      return Board::UiOrientation::Landscape;

    case State::WaitForTouchStart:
    case State::TouchRunning:
    case State::Cancelled:
      return portraitOrientationForShortSide(activeStartOrientation_);

    case State::WaitAfterTouch:
    case State::WorkRunning:
    case State::WaitAfterBreak:
      return portraitOrientationForShortSide(lastShortSide_);

    case State::BreakRunning:
    case State::WaitAfterWork:
      return Board::UiOrientation::Landscape;

    default:
      return Board::UiOrientation::Portrait;
  }
}

uint32_t FocusTimer::remainingMs(uint32_t nowMs) const {
  if (!timerRunning_) {
    return 0;
  }

  const uint32_t elapsed = nowMs - timerStartedMs_;
  return (elapsed >= timerDurationMs_) ? 0 : (timerDurationMs_ - elapsed);
}

uint8_t FocusTimer::progressPercent(uint32_t nowMs) const {
  if (!timerRunning_ || timerDurationMs_ == 0) {
    return 0;
  }

  const uint32_t elapsed = nowMs - timerStartedMs_;
  const uint32_t clamped = (elapsed >= timerDurationMs_) ? timerDurationMs_ : elapsed;
  return static_cast<uint8_t>((clamped * 100U) / timerDurationMs_);
}

uint8_t FocusTimer::completedTouchBlocks() const { return completedTouchBlocks_; }

uint8_t FocusTimer::completedWorkBlocks() const { return completedWorkBlocks_; }

uint8_t FocusTimer::completedBreakBlocks() const { return completedBreakBlocks_; }

bool FocusTimer::consumeCompletionCue() {
  const bool pending = completionCuePending_;
  completionCuePending_ = false;
  return pending;
}

const char *FocusTimer::genreLabel(Genre genre) {
  switch (genre) {
    case Genre::Chores:
      return "Chores";
    case Genre::RsvpNano:
      return "Work";
    case Genre::StrengthLabs:
      return "Fitness";
    case Genre::SelfCare:
      return "Self Care";
    case Genre::Other:
      return "Other";
    case Genre::None:
    default:
      return "";
  }
}

void FocusTimer::updateOrientation(uint32_t nowMs) {
  if (!accel_.available()) {
    rawOrientation_ = OrientationState::Unknown;
    stableOrientation_ = OrientationState::Unknown;
    return;
  }

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  if (!accel_.read(x, y, z)) {
    return;
  }

  rawOrientation_ = classify(x, y, z);
  if (rawOrientation_ != candidateOrientation_) {
    candidateOrientation_ = rawOrientation_;
    candidateSinceMs_ = nowMs;
    return;
  }

  if ((nowMs - candidateSinceMs_) >= kOrientationStableMs) {
    stableOrientation_ = candidateOrientation_;
  }
}

void FocusTimer::resetOrientationStability() {
  rawOrientation_ = OrientationState::Unknown;
  stableOrientation_ = OrientationState::Unknown;
  candidateOrientation_ = OrientationState::Unknown;
  candidateSinceMs_ = 0;
}

FocusTimer::OrientationState FocusTimer::classify(float x, float y, float z) const {
  if (fabsf(z) >= kFlatAxisThreshold && fabsf(x) <= 0.30f && fabsf(y) <= 0.30f) {
    return OrientationState::FlatBack;
  }

  if (x >= kSideAxisThreshold && fabsf(y) <= kCrossAxisLimit &&
      fabsf(z) <= kCrossAxisLimit) {
    return OrientationState::ShortSideA;
  }

  if (x <= -kSideAxisThreshold && fabsf(y) <= kCrossAxisLimit &&
      fabsf(z) <= kCrossAxisLimit) {
    return OrientationState::ShortSideB;
  }

  if (fabsf(y) >= kSideAxisThreshold && fabsf(x) <= kCrossAxisLimit &&
      fabsf(z) <= kCrossAxisLimit) {
    return OrientationState::LongSide;
  }

  return OrientationState::Unknown;
}

bool FocusTimer::orientationInputArmed(uint32_t nowMs) const {
  switch (state_) {
    case State::WaitForTouchStart:
      return (nowMs - stateStartedMs_) >= kTouchStartArmDelayMs;
    case State::WaitAfterTouch:
    case State::WaitAfterWork:
    case State::WaitAfterBreak:
      return (nowMs - stateStartedMs_) >= kPostTimerFlipGraceMs;
    default:
      return true;
  }
}

void FocusTimer::transitionTo(State nextState, uint32_t nowMs) {
  state_ = nextState;
  stateStartedMs_ = nowMs;
}

void FocusTimer::clearSession() {
  genre_ = Genre::None;
  activeMode_ = TimerMode::None;
  activeStartOrientation_ = OrientationState::Unknown;
  lastShortSide_ = OrientationState::Unknown;
  timerStartedMs_ = 0;
  timerDurationMs_ = 0;
  timerRunning_ = false;
  feedbackStartedMs_ = 0;
  completionCuePending_ = false;
  completedTouchBlocks_ = 0;
  completedWorkBlocks_ = 0;
  completedBreakBlocks_ = 0;
}

void FocusTimer::startMode(TimerMode mode, uint32_t nowMs, uint32_t durationMs,
                           OrientationState startOrientation) {
  activeMode_ = mode;
  activeStartOrientation_ = startOrientation;
  timerStartedMs_ = nowMs;
  timerDurationMs_ = durationMs;
  timerRunning_ = true;

  if (isShortSide(startOrientation)) {
    lastShortSide_ = startOrientation;
  }
}

void FocusTimer::stopActiveTimer() {
  timerRunning_ = false;
  activeMode_ = TimerMode::None;
  activeStartOrientation_ = OrientationState::Unknown;
  timerStartedMs_ = 0;
  timerDurationMs_ = 0;
  lastShortSide_ = OrientationState::Unknown;
}

void FocusTimer::completeActiveTimer() {
  if (!timerRunning_) {
    return;
  }

  switch (activeMode_) {
    case TimerMode::Touch:
      ++completedTouchBlocks_;
      break;
    case TimerMode::Work:
      ++completedWorkBlocks_;
      break;
    case TimerMode::Break:
      ++completedBreakBlocks_;
      break;
    case TimerMode::None:
    default:
      break;
  }

  timerRunning_ = false;
  activeMode_ = TimerMode::None;
  activeStartOrientation_ = OrientationState::Unknown;
  timerStartedMs_ = 0;
  timerDurationMs_ = 0;
  completionCuePending_ = true;
}

bool FocusTimer::timerExpired(uint32_t nowMs) const {
  return timerRunning_ && (nowMs - timerStartedMs_ >= timerDurationMs_);
}

bool FocusTimer::isShortSide(OrientationState orientation) {
  return orientation == OrientationState::ShortSideA ||
         orientation == OrientationState::ShortSideB;
}

FocusTimer::OrientationState FocusTimer::oppositeShortSide(
    OrientationState orientation) {
  switch (orientation) {
    case OrientationState::ShortSideA:
      return OrientationState::ShortSideB;
    case OrientationState::ShortSideB:
      return OrientationState::ShortSideA;
    default:
      return OrientationState::Unknown;
  }
}

Board::UiOrientation FocusTimer::portraitOrientationForShortSide(OrientationState orientation) {
  return orientation == OrientationState::ShortSideB ? Board::UiOrientation::PortraitFlipped
                                                     : Board::UiOrientation::Portrait;
}
