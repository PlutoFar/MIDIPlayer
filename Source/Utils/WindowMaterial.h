#pragma once

#include <algorithm>

namespace WindowMaterial {

enum class Type {
  Transparent = 1,
  GaussianBlur = 2,
  FrostedGlass = 3,
  Acrylic = 4
};

struct Config {
  Type type = Type::Acrylic;
  float opacity = 0.78f;
  int strength = 24;
};

inline bool supportsStrength(Type type) {
  return type != Type::Transparent;
}

inline Config normalise(Config config) {
  config.opacity = std::clamp(config.opacity, 0.25f, 0.98f);
  config.strength = std::clamp(config.strength, 1, 50);
  return config;
}

} // namespace WindowMaterial
