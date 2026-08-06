#pragma once

#include <cmath>

inline bool shouldRunButtonAnimationTimer(bool interactionActive,
                                          float currentScale,
                                          float targetScale,
                                          float currentAlpha,
                                          float targetAlpha) {
  return interactionActive || std::abs(currentScale - targetScale) > 0.01f ||
         std::abs(currentAlpha - targetAlpha) > 0.01f;
}
