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
  return {true, true, false, true, true, true, false, false};
}
