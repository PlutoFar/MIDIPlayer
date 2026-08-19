#pragma once

#include <algorithm>
#include <juce_graphics/juce_graphics.h>

namespace WindowMaterial {

constexpr int maximumSoftwareBlurRadius = 40;

enum class Type {
  Transparent = 1,
  GaussianBlur = 2,
  Aero = 3,
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

inline int softwareBlurRadius(Config config) {
  config = normalise(config);
  switch (config.type) {
  case Type::Transparent:
    return 0;
  case Type::GaussianBlur:
    return juce::jlimit(2, 32,
                        juce::roundToInt(config.strength * 0.55f));
  case Type::Aero:
    return juce::jlimit(4, 36,
                        juce::roundToInt(config.strength * 0.65f));
  case Type::Acrylic:
    return juce::jlimit(4, maximumSoftwareBlurRadius,
                        juce::roundToInt(config.strength * 0.70f));
  }
  return 0;
}

inline float surfaceAlpha(Config config) {
  config = normalise(config);
  switch (config.type) {
  case Type::Transparent:
    return config.opacity;
  case Type::GaussianBlur:
    return juce::jlimit(0.18f, 0.88f, 0.08f + config.opacity * 0.72f);
  case Type::Aero:
    return juce::jlimit(0.16f, 0.82f, 0.06f + config.opacity * 0.66f);
  case Type::Acrylic:
    return juce::jlimit(0.12f, 0.72f, 0.03f + config.opacity * 0.58f);
  }
  return config.opacity;
}

inline const juce::Image &acrylicNoiseTexture() {
  static const juce::Image texture = [] {
    juce::Image image(juce::Image::ARGB, 128, 128, true);
    juce::Graphics graphics(image);
    for (int y = 0; y < image.getHeight(); y += 4) {
      for (int x = 0; x < image.getWidth(); x += 4) {
        const auto hash = static_cast<unsigned int>(x * 73856093u) ^
                          static_cast<unsigned int>(y * 19349663u);
        graphics.setColour((hash & 1u) != 0u
                               ? juce::Colours::white.withAlpha(0.9f)
                               : juce::Colours::black.withAlpha(0.6f));
        graphics.fillRect(x, y, 1, 1);
      }
    }
    return image;
  }();
  return texture;
}

inline void paintTexture(juce::Graphics &graphics, Config config,
                         juce::Colour accent) {
  config = normalise(config);
  const float effect = config.strength / 50.0f;
  const auto clip = graphics.getClipBounds();

  if (config.type == Type::GaussianBlur) {
    graphics.setColour(
        juce::Colours::black.withAlpha(0.015f + effect * 0.08f));
    graphics.fillRect(clip);
    return;
  }

  if (config.type == Type::Aero) {
    graphics.setColour(
        juce::Colours::white.withAlpha(0.02f + effect * 0.16f));
    graphics.fillRect(clip);
    return;
  }

  if (config.type != Type::Acrylic)
    return;

  graphics.setColour(accent.withAlpha(0.01f + effect * 0.07f));
  graphics.fillRect(clip);

  const float noiseAlpha = 0.012f + effect * 0.065f;
  graphics.setOpacity(noiseAlpha);
  const auto &texture = acrylicNoiseTexture();
  for (int y = clip.getY(); y < clip.getBottom(); y += texture.getHeight())
    for (int x = clip.getX(); x < clip.getRight(); x += texture.getWidth())
      graphics.drawImageAt(texture, x, y);
  graphics.setOpacity(1.0f);
}

} // namespace WindowMaterial
