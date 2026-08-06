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
    overscroll = 0.0f;
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

    const float rawPosition = position + overscroll + velocity;
    if (rawPosition < minimum) {
      position = minimum;
      overscroll = rawPosition - minimum;
      velocity += -overscroll * edgeSpring;
      target = minimum;
      hasTarget = true;
    } else if (rawPosition > maximum) {
      position = maximum;
      overscroll = rawPosition - maximum;
      velocity += -overscroll * edgeSpring;
      target = maximum;
      hasTarget = true;
    } else {
      position = rawPosition;
      overscroll = 0.0f;
    }

    velocity *= friction;

    if (hasTarget && std::abs(target - position) < settleDistance &&
        std::abs(velocity) < settleVelocity &&
        std::abs(overscroll) < settleDistance) {
      position = target;
      velocity = 0.0f;
      overscroll = 0.0f;
      hasTarget = false;
    }

    if (!hasTarget && std::abs(velocity) < settleVelocity &&
        std::abs(overscroll) < settleDistance) {
      velocity = 0.0f;
      overscroll = 0.0f;
      position = std::clamp(position, minimum, maximum);
      return false;
    }

    return true;
  }

  float getPosition() const { return position; }
  float getOverscroll() const { return overscroll; }

private:
  static constexpr float friction = 0.82f;
  static constexpr float edgeSpring = 0.22f;
  static constexpr float targetSpring = 0.13f;
  static constexpr float settleDistance = 0.05f;
  static constexpr float settleVelocity = 0.05f;

  float minimum = 0.0f;
  float maximum = 0.0f;
  float position = 0.0f;
  float overscroll = 0.0f;
  float velocity = 0.0f;
  float target = 0.0f;
  bool hasTarget = false;
};
