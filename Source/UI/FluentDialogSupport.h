#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

struct FluentDialogWindowPolicy {
  bool useSystemDropShadow = false;
  bool useDwmRoundedCorners = false;
  bool useWindowRegion = true;
  bool configureBeforeEnteringModal = true;
  bool constrainToOwner = true;
  bool useNativeOwner = true;
  bool allowAlwaysOnTop = false;
};

constexpr FluentDialogWindowPolicy
getFluentDialogWindowPolicy(bool) {
  return {true, true, false, true, true, true, false};
}

inline juce::Rectangle<int>
constrainDialogBoundsToOwner(juce::Rectangle<int> bounds,
                             juce::Rectangle<int> ownerBounds) {
  if (ownerBounds.isEmpty())
    return bounds;

  bounds.setSize(juce::jmin(bounds.getWidth(), ownerBounds.getWidth()),
                 juce::jmin(bounds.getHeight(), ownerBounds.getHeight()));
  bounds.setPosition(
      juce::jlimit(ownerBounds.getX(),
                   ownerBounds.getRight() - bounds.getWidth(), bounds.getX()),
      juce::jlimit(ownerBounds.getY(),
                   ownerBounds.getBottom() - bounds.getHeight(),
                   bounds.getY()));
  return bounds;
}
