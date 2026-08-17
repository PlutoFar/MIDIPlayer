#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

struct FluentDialogWindowPolicy {
  bool useSystemDropShadow = false;
  bool useDwmRoundedCorners = false;
  bool useWindowRegion = true;
  bool configureBeforeEnteringModal = true;
  bool centreOnOwner = true;
  bool useNativeOwner = true;
  bool allowAlwaysOnTop = false;
  bool allowDragging = false;
};

constexpr FluentDialogWindowPolicy
getFluentDialogWindowPolicy(bool) {
  return {false, false, true, true, true, true, false, false};
}

inline juce::Component::WindowControlKind getNonDraggableDialogControlKind(
    juce::Component::WindowControlKind control) {
  using Kind = juce::Component::WindowControlKind;
  return control == Kind::close ? Kind::close : Kind::client;
}

inline float getDialogEnterAlpha(float progress) {
  progress = juce::jlimit(0.0f, 1.0f, progress);
  const float remaining = 1.0f - progress;
  return 1.0f - remaining * remaining * remaining;
}

inline float getDialogExitAlpha(float progress) {
  progress = juce::jlimit(0.0f, 1.0f, progress);
  return 1.0f - progress * progress * progress;
}
