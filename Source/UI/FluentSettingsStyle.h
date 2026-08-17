#pragma once

#include "../Utils/UserSettings.h"
#include "../Utils/Win11Helpers.h"
#include "CustomLookAndFeel.h"
#include "FluentDialogSupport.h"

namespace FluentSettingsStyle {
constexpr int panelMargin = 16;
constexpr int cardPadding = 16;
constexpr int rowGap = 10;

class FluentDialogWindow final : public juce::DialogWindow,
                                 private juce::Timer {
public:
  FluentDialogWindow(juce::DialogWindow::LaunchOptions &options,
                     juce::Component *constraintOwner,
                     juce::Component *nativeOwner)
      : juce::DialogWindow(
            options.dialogTitle, options.dialogBackgroundColour,
            options.escapeKeyTriggersCloseButton, true,
            options.componentToCentreAround != nullptr
                ? juce::Component::getApproximateScaleFactorForComponent(
                      options.componentToCentreAround)
                : 1.0f),
        constraintOwner(constraintOwner), nativeOwner(nativeOwner) {
    if (options.content.willDeleteObject())
      setContentOwned(options.content.release(), true);
    else
      setContentNonOwned(options.content.release(), true);

    setResizable(false, false);
    setUsingNativeTitleBar(options.useNativeTitleBar);
    setDraggable(false);
    // JUCE's Windows peer blocks client and non-client owner input while this
    // component is modal. Disabling the owner HWND separately breaks focus and
    // Z-order restoration when the dialog is destroyed.
  }

  ~FluentDialogWindow() override {
    stopTimer();
    if (!nativeWindowHidden)
      Win11Helpers::hideNativeWindowAndFlush(this);
  }

  void closeButtonPressed() override {
    if (closing)
      return;
    closing = true;
    animationPhase = AnimationPhase::closing;
    animationStartAlpha = getAlpha();
    animationStartTimeMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(60);
  }

  bool escapeKeyPressed() override {
    closeButtonPressed();
    return true;
  }

  juce::Component *getConstraintOwner() const {
    return constraintOwner.getComponent();
  }

  juce::Component *getNativeOwner() const {
    return nativeOwner.getComponent();
  }

  void startOpenAnimation() {
    animationPhase = AnimationPhase::opening;
    animationStartAlpha = dialogTransitionAlphaFloor;
    animationStartTimeMs = juce::Time::getMillisecondCounterHiRes();
    setAlpha(dialogTransitionAlphaFloor);
    startTimerHz(60);
  }

  void centreOnOwner() {
    if (nativeOwner != nullptr &&
        Win11Helpers::centreWindowOnOwner(this, nativeOwner.getComponent()))
      return;
    if (constraintOwner != nullptr)
      centreAroundComponent(constraintOwner.getComponent(), getWidth(),
                            getHeight());
  }

private:
  enum class AnimationPhase { idle, opening, closing };

  void timerCallback() override {
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    const double durationMs = animationPhase == AnimationPhase::opening
                                  ? openAnimationDurationMs
                                  : closeAnimationDurationMs;
    const auto progress = static_cast<float>(juce::jlimit(
        0.0, 1.0, (nowMs - animationStartTimeMs) / durationMs));

    if (animationPhase == AnimationPhase::opening) {
      setAlpha(getDialogEnterAlpha(progress));
      if (progress >= 1.0f) {
        setAlpha(1.0f);
        animationPhase = AnimationPhase::idle;
        stopTimer();
      }
      return;
    }

    if (animationPhase == AnimationPhase::closing) {
      setAlpha(getDialogExitAlpha(animationStartAlpha, progress));
      if (progress >= 1.0f) {
        stopTimer();
        animationPhase = AnimationPhase::idle;
        nativeWindowHidden = Win11Helpers::hideNativeWindowAndFlush(this);
        setVisible(false);
      }
    }
  }

  WindowControlKind
  findControlAtPoint(juce::Point<float> point) const override {
    return getNonDraggableDialogControlKind(
        juce::DialogWindow::findControlAtPoint(point));
  }

  juce::Component::SafePointer<juce::Component> constraintOwner;
  juce::Component::SafePointer<juce::Component> nativeOwner;
  bool closing = false;
  bool nativeWindowHidden = false;
  AnimationPhase animationPhase = AnimationPhase::idle;
  float animationStartAlpha = 1.0f;
  double animationStartTimeMs = 0.0;
  static constexpr double openAnimationDurationMs = 140.0;
  static constexpr double closeAnimationDurationMs = 100.0;
};

inline int controlHeight(const FluentLookAndFeel &lookAndFeel) {
  return LegacyDesignTokens::Layout::controlHeight(
      lookAndFeel.getUIFontSize());
}

inline float dialogSurfaceAlpha(WindowMaterial::Config config) {
  return WindowMaterial::surfaceAlpha(config);
}

inline float dialogSurfaceAlpha() {
  return dialogSurfaceAlpha(getAppSettings().getDialogMaterialConfig());
}

inline const juce::Image &acrylicNoiseTexture() {
  return WindowMaterial::acrylicNoiseTexture();
}

inline void paintMaterialTexture(juce::Graphics &g,
                                 FluentLookAndFeel &lookAndFeel) {
  WindowMaterial::paintTexture(
      g, getAppSettings().getDialogMaterialConfig(),
      lookAndFeel.getColors().accentPrimary);
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

class FluentMessageDialogContent final : public juce::Component {
public:
  FluentMessageDialogContent(FluentLookAndFeel &lookAndFeel,
                             juce::String message,
                             juce::String buttonText)
      : fluentLookAndFeel(lookAndFeel), message(std::move(message)) {
    setLookAndFeel(&fluentLookAndFeel);
    setOpaque(false);
    hasPrimaryButton = buttonText.isNotEmpty();
    if (hasPrimaryButton) {
      addAndMakeVisible(primaryButton);
      primaryButton.setButtonText(std::move(buttonText));
      primaryButton.onClick = [this]() {
        if (auto *window = findParentComponentOfClass<FluentDialogWindow>())
          window->closeButtonPressed();
      };
    }
    setSize(460, hasPrimaryButton ? 180 : 130);
  }

  ~FluentMessageDialogContent() override { setLookAndFeel(nullptr); }

  void paint(juce::Graphics &graphics) override {
    paintPanel(graphics, fluentLookAndFeel);
    graphics.setColour(fluentLookAndFeel.getColors().textPrimary);
    graphics.setFont(fluentLookAndFeel.getBodyLargeFont());
    graphics.drawFittedText(
        message, getLocalBounds().reduced(24).removeFromTop(hasPrimaryButton ? 90 : 70),
        juce::Justification::centred, 4, 1.0f);
  }

  void resized() override {
    if (!hasPrimaryButton)
      return;
    constexpr int buttonWidth = 96;
    constexpr int buttonHeight = 40;
    primaryButton.setBounds((getWidth() - buttonWidth) / 2,
                            getHeight() - buttonHeight - 20, buttonWidth,
                            buttonHeight);
  }

private:
  FluentLookAndFeel &fluentLookAndFeel;
  juce::String message;
  juce::TextButton primaryButton;
  bool hasPrimaryButton = false;
};

inline void applyTypography(juce::Component &component,
                            FluentLookAndFeel &lookAndFeel) {
  for (int index = 0; index < component.getNumChildComponents(); ++index) {
    auto *child = component.getChildComponent(index);
    if (auto *label = dynamic_cast<juce::Label *>(child))
      label->setFont(lookAndFeel.getBodyFont());
    applyTypography(*child, lookAndFeel);
  }
}

inline void centreDialogNow(juce::DialogWindow *window) {
  if (window == nullptr)
    return;

  if (auto *ownedWindow = dynamic_cast<FluentDialogWindow *>(window)) {
    if (ownedWindow->getConstraintOwner() != nullptr) {
      ownedWindow->centreOnOwner();
      return;
    }
  }

  auto &displays = juce::Desktop::getInstance().getDisplays();
  auto *display =
      displays.getDisplayForRect(window->getScreenBounds(), false);
  if (display == nullptr)
    display = displays.getPrimaryDisplay();
  if (display == nullptr)
    return;

  window->setCentrePosition(
      display->userBounds.getSmallestIntegerContainer().getCentre());
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

inline void refreshDialogNativeStyleLater(juce::DialogWindow *window) {
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
      if (auto *ownedWindow =
              dynamic_cast<FluentDialogWindow *>(safeWindow.getComponent()))
        Win11Helpers::setOwnedWindow(ownedWindow,
                                     ownedWindow->getNativeOwner());
    }
  });
}

inline juce::DialogWindow *
launchDialogAsync(juce::DialogWindow::LaunchOptions &options,
                  bool animateWindow = true) {
  options.dialogBackgroundColour = juce::Colours::transparentBlack;
  auto *anchor = options.componentToCentreAround;
  auto *nativeOwner = anchor != nullptr ? anchor->getTopLevelComponent()
                                       : nullptr;
  auto *constraintOwner = anchor;
  if (auto *parentDialog = dynamic_cast<FluentDialogWindow *>(nativeOwner))
    constraintOwner = parentDialog->getConstraintOwner();

  auto *window =
      new FluentDialogWindow(options, constraintOwner, nativeOwner);
  applyDialogWindowStyle(window);
  Win11Helpers::setOwnedWindow(window, nativeOwner);
  centreDialogNow(window);
  window->setAlpha(animateWindow ? dialogTransitionAlphaFloor : 1.0f);
  window->enterModalState(true, nullptr, true);
  if (animateWindow)
    window->startOpenAnimation();
  refreshDialogNativeStyleLater(window);
  return window;
}

inline juce::DialogWindow *
showMessageDialogAsync(const juce::String &title, const juce::String &message,
                       juce::Component *anchor,
                       FluentLookAndFeel &lookAndFeel,
                       const juce::String &buttonText = L"确定",
                       bool animateWindow = true) {
  auto *content =
      new FluentMessageDialogContent(lookAndFeel, message, buttonText);
  juce::DialogWindow::LaunchOptions options;
  options.content.setOwned(content);
  options.dialogTitle = title;
  options.dialogBackgroundColour = juce::Colours::transparentBlack;
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = false;
  options.resizable = false;
  options.componentToCentreAround = anchor;
  return launchDialogAsync(options, animateWindow);
}
} // namespace FluentSettingsStyle
