#pragma once

struct FluentDialogWindowPolicy {
  bool useSystemDropShadow = false;
  bool useDwmRoundedCorners = false;
  bool useWindowRegion = true;
  bool configureBeforeEnteringModal = true;
};

constexpr FluentDialogWindowPolicy
getFluentDialogWindowPolicy(bool usesNativeTitleBar) {
  return {usesNativeTitleBar, usesNativeTitleBar, !usesNativeTitleBar, true};
}
