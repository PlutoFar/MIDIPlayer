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
  return {true, false, true, true, true, true, false, false};
}

inline juce::Component::WindowControlKind getNonDraggableDialogControlKind(
    juce::Component::WindowControlKind control) {
  using Kind = juce::Component::WindowControlKind;
  return control == Kind::close ? Kind::close : Kind::client;
}
