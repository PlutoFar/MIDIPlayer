#pragma once

#include <algorithm>
#include <cmath>

class BackgroundScrollMotion {
public:
  void setBounds(float minimumPosition, float maximumPosition) {
    minimum = minimumPosition;
    maximum = std::max(minimumPosition, maximumPosition);
    position = std::clamp(position, minimum, maximum);
  }

  void setPosition(float newPosition) {
    position = std::clamp(newPosition, minimum, maximum);
    velocity = 0.0f;
    hasTarget = false;
  }

  void addImpulse(float impulse) {
    velocity += impulse;
    hasTarget = false;
  }

  void animateTo(float newTarget) {
    target = std::clamp(newTarget, minimum, maximum);
    hasTarget = true;
  }

  bool tick() {
    if (hasTarget)
      velocity += (target - position) * targetSpring;

    const float rawPosition = position + velocity;
    position = std::clamp(rawPosition, minimum, maximum);
    if (position != rawPosition) {
      velocity = 0.0f;
      if (!hasTarget || target == position)
        hasTarget = false;
    }

    velocity *= friction;

    if (hasTarget && std::abs(target - position) < settleDistance &&
        std::abs(velocity) < settleVelocity) {
      position = target;
      velocity = 0.0f;
      hasTarget = false;
    }

    if (!hasTarget && std::abs(velocity) < settleVelocity) {
      velocity = 0.0f;
      position = std::clamp(position, minimum, maximum);
      return false;
    }

    return true;
  }

  float getPosition() const { return position; }
  float getOverscroll() const { return 0.0f; }

private:
  static constexpr float friction = 0.82f;
  static constexpr float targetSpring = 0.13f;
  static constexpr float settleDistance = 0.05f;
  static constexpr float settleVelocity = 0.05f;

  float minimum = 0.0f;
  float maximum = 0.0f;
  float position = 0.0f;
  float velocity = 0.0f;
  float target = 0.0f;
  bool hasTarget = false;
};
