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

inline float dialogSurfaceAlpha(WindowMaterial::Config config) {
  config = WindowMaterial::normalise(config);
  switch (config.type) {
  case WindowMaterial::Type::Transparent:
    return config.opacity;
  case WindowMaterial::Type::GaussianBlur:
    return juce::jlimit(0.18f, 0.88f, 0.08f + config.opacity * 0.72f);
  case WindowMaterial::Type::FrostedGlass:
    return juce::jlimit(0.16f, 0.82f, 0.06f + config.opacity * 0.66f);
  case WindowMaterial::Type::Acrylic:
    return juce::jlimit(0.12f, 0.72f, 0.03f + config.opacity * 0.58f);
  }
  return config.opacity;
}

inline float dialogSurfaceAlpha() {
  return dialogSurfaceAlpha(getAppSettings().getDialogMaterialConfig());
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

inline void paintMaterialTexture(juce::Graphics &g,
                                 FluentLookAndFeel &lookAndFeel) {
  const auto config = getAppSettings().getDialogMaterialConfig();
  const float effect = config.strength / 50.0f;
  const auto clip = g.getClipBounds();

  if (config.type == WindowMaterial::Type::GaussianBlur) {
    g.setColour(juce::Colours::black.withAlpha(0.015f + effect * 0.08f));
    g.fillRect(clip);
    return;
  }

  if (config.type == WindowMaterial::Type::FrostedGlass) {
    g.setColour(juce::Colours::white.withAlpha(0.02f + effect * 0.16f));
    g.fillRect(clip);
    return;
  }

  if (config.type != WindowMaterial::Type::Acrylic)
    return;

  g.setColour(lookAndFeel.getColors().accentPrimary.withAlpha(
      0.01f + effect * 0.07f));
  g.fillRect(clip);

  const float noiseAlpha = 0.012f + effect * 0.065f;
  g.setOpacity(noiseAlpha);
  const auto &texture = acrylicNoiseTexture();
  for (int y = clip.getY(); y < clip.getBottom(); y += texture.getHeight())
    for (int x = clip.getX(); x < clip.getRight(); x += texture.getWidth())
      g.drawImageAt(texture, x, y);
  g.setOpacity(1.0f);
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
  paintMaterialTexture(g, lookAndFeel);
}

inline void paintCard(juce::Graphics &g, FluentLookAndFeel &lookAndFeel,
                      juce::Rectangle<int> bounds) {
  const auto &colors = lookAndFeel.getColors();
  const auto card = bounds.toFloat();
  const auto surface =
      colors.cardBackground.interpolatedWith(colors.textPrimary, 0.025f)
          .withAlpha(juce::jmin(0.96f, dialogSurfaceAlpha() + 0.18f));
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

inline void refreshDialogSurface(juce::DialogWindow *window) {
  if (window == nullptr)
    return;
  window->getProperties().set("fluentDialogSurfaceAlpha",
                              (double)dialogSurfaceAlpha());
  window->repaint();
  if (auto *content = window->getContentComponent())
    content->repaint();
}

inline void refreshDialogMaterial(juce::DialogWindow *window) {
  if (window == nullptr)
    return;

  const auto config = getAppSettings().getDialogMaterialConfig();
  window->setColour(juce::ResizableWindow::backgroundColourId,
                    juce::Colours::transparentBlack);
  Win11Helpers::applyDialogMaterial(window, config);
  refreshDialogSurface(window);
}

inline void applyDialogWindowStyle(juce::DialogWindow *window) {
  if (window == nullptr)
    return;

  const auto policy =
      getFluentDialogWindowPolicy(window->isUsingNativeTitleBar());
  if (window->isDropShadowEnabled() != policy.useSystemDropShadow)
    window->setDropShadowEnabled(policy.useSystemDropShadow);

  if (window->isOpaque())
    window->setOpaque(false);

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
