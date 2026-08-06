#pragma once

#include <algorithm>
#include <cmath>

class AudioStatusAnimation {
public:
  enum class Phase { idle, busy, success, failure };

  void beginBusy() {
    phase = Phase::busy;
    progress = 0.0f;
    successGlow = 0.0f;
    shakeOffset = 0.0f;
  }

  void complete(bool succeeded) {
    phase = succeeded ? Phase::success : Phase::failure;
    progress = 0.0f;
    successGlow = succeeded ? 1.0f : 0.0f;
    shakeOffset = 0.0f;
  }

  bool tick() {
    switch (phase) {
    case Phase::busy:
      progress = std::fmod(progress + 0.045f, 1.0f);
      return true;

    case Phase::success:
      progress = std::min(1.0f, progress + 0.055f);
      successGlow = (1.0f - progress) * (1.0f - progress);
      if (progress >= 1.0f)
        phase = Phase::idle;
      break;

    case Phase::failure:
      progress = std::min(1.0f, progress + 0.075f);
      shakeOffset =
          std::sin(progress * 6.0f * 3.14159265358979323846f) *
          (1.0f - progress) * 5.0f;
      if (progress >= 1.0f) {
        shakeOffset = 0.0f;
        phase = Phase::idle;
      }
      break;

    case Phase::idle:
      return false;
    }

    return phase != Phase::idle;
  }

  Phase getPhase() const { return phase; }
  float getBusyRotation() const { return progress; }
  float getSuccessGlow() const { return successGlow; }
  float getShakeOffset() const { return shakeOffset; }

private:
  Phase phase = Phase::idle;
  float progress = 0.0f;
  float successGlow = 0.0f;
  float shakeOffset = 0.0f;
};
