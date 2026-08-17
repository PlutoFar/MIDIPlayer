#pragma once

#include "../Utils/UserSettings.h"
#include "../Utils/Win11Helpers.h"
#include "CustomLookAndFeel.h"
#include "FluentDialogSupport.h"

namespace FluentSettingsStyle {
constexpr int panelMargin = 16;
constexpr int cardPadding = 16;
constexpr int rowGap = 10;

inline int controlHeight(const FluentLookAndFeel &lookAndFeel) {
  return LegacyDesignTokens::Layout::controlHeight(
      lookAndFeel.getUIFontSize());
}

inline float dialogSurfaceAlpha() {
  const auto config = getAppSettings().getDialogMaterialConfig();
  const float effect = config.strength / 50.0f;
  switch (config.type) {
  case WindowMaterial::Type::Transparent:
    return config.opacity;
  case WindowMaterial::Type::GaussianBlur:
    return juce::jlimit(0.25f, 0.98f,
                        config.opacity * (0.82f + effect * 0.08f));
  case WindowMaterial::Type::FrostedGlass:
    return juce::jlimit(0.25f, 0.98f,
                        config.opacity * (0.74f + effect * 0.10f));
  case WindowMaterial::Type::Acrylic:
    return juce::jlimit(0.25f, 0.98f,
                        config.opacity * (0.68f + effect * 0.12f));
  }
  return config.opacity;
}

inline void configureLabel(juce::Label &label, FluentLookAndFeel &lookAndFeel,
                           bool semibold = false,
                           bool secondary = false) {
  label.setFont(lookAndFeel.getBodyFont(semibold));
  label.setColour(
      juce::Label::textColourId,
      secondary ? lookAndFeel.getColors().textSecondary
                : lookAndFeel.getColors().textPrimary);
  label.setMinimumHorizontalScale(1.0f);
}

inline void paintPanel(juce::Graphics &g, FluentLookAndFeel &lookAndFeel) {
  g.fillAll(juce::Colours::transparentBlack);
  g.setColour(lookAndFeel.getColors().cardBackground.withAlpha(
      dialogSurfaceAlpha()));
  g.fillRect(g.getClipBounds());
}

inline void paintCard(juce::Graphics &g, FluentLookAndFeel &lookAndFeel,
                      juce::Rectangle<int> bounds) {
  const auto &colors = lookAndFeel.getColors();
  const auto card = bounds.toFloat();
  const auto surface =
      colors.cardBackground.interpolatedWith(colors.textPrimary, 0.025f)
          .withAlpha(juce::jmin(0.98f, dialogSurfaceAlpha() + 0.10f));
  g.setColour(surface);
  g.fillRoundedRectangle(card, 8.0f);
  g.setColour(colors.cardBorder.withMultipliedAlpha(0.55f));
  g.drawRoundedRectangle(card.reduced(0.5f), 8.0f, 1.0f);
}

inline void applyTypography(juce::Component &component,
                            FluentLookAndFeel &lookAndFeel) {
  for (int index = 0; index < component.getNumChildComponents(); ++index) {
    auto *child = component.getChildComponent(index);
    if (auto *label = dynamic_cast<juce::Label *>(child))
      label->setFont(lookAndFeel.getBodyFont());
    applyTypography(*child, lookAndFeel);
  }
}

inline void constrainDialogToWorkAreaNow(juce::DialogWindow *window) {
  if (window == nullptr)
    return;

  auto &displays = juce::Desktop::getInstance().getDisplays();
  auto *display =
      displays.getDisplayForRect(window->getScreenBounds(), false);
  if (display == nullptr)
    display = displays.getPrimaryDisplay();
  if (display == nullptr)
    return;

  const auto workArea = display->userArea.reduced(8);
  window->setBounds(window->getBounds().constrainedWithin(workArea));
}

inline void refreshDialogMaterial(juce::DialogWindow *window) {
  if (window == nullptr)
    return;

  window->setOpaque(false);
  window->setColour(juce::ResizableWindow::backgroundColourId,
                    juce::Colours::transparentBlack);
  window->getProperties().set("fluentDialogSurfaceAlpha",
                              (double)dialogSurfaceAlpha());
  Win11Helpers::applyDialogMaterial(
      window, getAppSettings().getDialogMaterialConfig());
  window->repaint();
  if (auto *content = window->getContentComponent())
    content->repaint();
}

inline void applyDialogWindowStyle(juce::DialogWindow *window) {
  if (window == nullptr)
    return;

  const auto policy =
      getFluentDialogWindowPolicy(window->isUsingNativeTitleBar());
  if (window->isDropShadowEnabled() != policy.useSystemDropShadow)
    window->setDropShadowEnabled(policy.useSystemDropShadow);

  Win11Helpers::applyFluentDialogStyle(window, true,
                                       policy.useDwmRoundedCorners);
  refreshDialogMaterial(window);
  Win11Helpers::applyRoundedWindowRegion(window, policy.useWindowRegion);
}

inline void constrainDialogToWorkArea(juce::DialogWindow *window) {
  constrainDialogToWorkAreaNow(window);
  auto safeWindow = juce::Component::SafePointer<juce::DialogWindow>(window);
  juce::Timer::callAfterDelay(100, [safeWindow]() {
    if (safeWindow != nullptr) {
      const auto policy = getFluentDialogWindowPolicy(
          safeWindow->isUsingNativeTitleBar());
      Win11Helpers::applyFluentDialogStyle(
          safeWindow.getComponent(), true, policy.useDwmRoundedCorners);
      refreshDialogMaterial(safeWindow.getComponent());
      Win11Helpers::applyRoundedWindowRegion(
          safeWindow.getComponent(), policy.useWindowRegion);
      constrainDialogToWorkAreaNow(safeWindow.getComponent());
    }
  });
}

inline juce::DialogWindow *
launchDialogAsync(juce::DialogWindow::LaunchOptions &options) {
  options.dialogBackgroundColour = juce::Colours::transparentBlack;
  auto *window = options.create();
  applyDialogWindowStyle(window);
  constrainDialogToWorkAreaNow(window);
  window->enterModalState(true, nullptr, true);
  constrainDialogToWorkArea(window);
  return window;
}
} // namespace FluentSettingsStyle
