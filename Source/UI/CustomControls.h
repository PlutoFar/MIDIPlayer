#pragma once

#include "ButtonAnimationSupport.h"
#include "CustomLookAndFeel.h"
#include "TooltipPlacement.h"
#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>

class SvgButton : public juce::Button, public juce::Timer {
public:
  explicit SvgButton(const juce::String& svgString,
                     float visualSize = LegacyDesignTokens::Icon::toolbar)
      : juce::Button(""), iconVisualSize(visualSize) {
    setMouseClickGrabsKeyboardFocus(false);
    auto xml = juce::XmlDocument::parse(svgString);
    if (xml != nullptr) {
      drawable = juce::Drawable::createFromSVG(*xml);
    }
  }
  ~SvgButton() override { stopTimer(); }

  void paintButton(juce::Graphics &g, bool isMouseOver,
                   bool isButtonDown) override {
    float targetScale = isButtonDown ? 0.95f : (isMouseOver ? 1.05f : 1.0f);
    currentScale += (targetScale - currentScale) * 0.2f;

    float targetAlpha = isEnabled() ? (isMouseOver ? 1.0f : 1.0f) : 0.3f;
    currentAlpha += (targetAlpha - currentAlpha) * 0.2f;

    auto bounds = getLocalBounds().toFloat();
    auto center = bounds.getCentre();

    juce::AffineTransform transform =
        juce::AffineTransform::translation(-center)
            .scaled(currentScale)
            .translated(center);

    g.saveState();
    g.addTransform(transform);

    auto *laf = dynamic_cast<FluentLookAndFeel *>(&getLookAndFeel());
    if (isEnabled() && (isMouseOver || isButtonDown)) {
      if (laf != nullptr) {
        auto &colors = laf->getColors();
        g.setColour(isButtonDown ? colors.controlPressed : colors.controlHover);
        g.fillRoundedRectangle(bounds.reduced(2.0f), 6.0f);
      }
    }

    g.setOpacity(currentAlpha);

    if (drawable != nullptr) {
      // 源 SVG 应使用黑色图标，深色主题下统一替换为白色。
      drawable->replaceColour(juce::Colours::black, juce::Colours::white);
      if (laf != nullptr)
        laf->drawDrawableIcon(g, *drawable, bounds, iconVisualSize,
                              currentAlpha);
      else
        drawable->drawWithin(
            g, bounds.withSizeKeepingCentre(iconVisualSize, iconVisualSize),
            juce::RectanglePlacement::stretchToFit, currentAlpha);
    }
    g.restoreState();
    paintKeyboardFocus(g, bounds);
  }

  void timerCallback() override {
    float targetScale = isEnabled() ? (isMouseButtonDown() ? 0.95f : (isMouseOver() ? 1.05f : 1.0f)) : 1.0f;
    float targetAlpha = isEnabled() ? (isMouseOver() ? 1.0f : 1.0f) : 0.3f;

    if (shouldRunButtonAnimationTimer(isMouseOver() || isMouseButtonDown(),
                                      currentScale, targetScale, currentAlpha,
                                      targetAlpha)) {
      repaint();
    } else {
      stopTimer();
    }
  }

  void buttonStateChanged() override {
    updateTimerState();
    repaint();
  }

  void enablementChanged() override {
    updateTimerState();
    repaint();
  }

private:
  void paintKeyboardFocus(juce::Graphics &g, juce::Rectangle<float> bounds) {
    if (!hasKeyboardFocus(false))
      return;
    g.setColour(findColour(juce::TextButton::textColourOnId, true));
    g.drawRoundedRectangle(bounds.reduced(2.0f), 6.0f, 2.0f);
  }

  void updateTimerState() {
    const float targetScale =
        isEnabled() ? (isMouseButtonDown() ? 0.95f
                                           : (isMouseOver() ? 1.05f : 1.0f))
                    : 1.0f;
    const float targetAlpha = isEnabled() ? 1.0f : 0.3f;
    if (shouldRunButtonAnimationTimer(isMouseOver() || isMouseButtonDown(),
                                      currentScale, targetScale, currentAlpha,
                                      targetAlpha)) {
      if (!isTimerRunning())
        startTimerHz(60);
    } else {
      stopTimer();
    }
  }

  std::unique_ptr<juce::Drawable> drawable;
  float iconVisualSize = 16.0f;
  float currentScale = 1.0f;
  float currentAlpha = 1.0f;
};

class TransparentButton : public juce::Button, public juce::Timer {
public:
  TransparentButton(const juce::String &btnText = "") : juce::Button(btnText) {
    setMouseClickGrabsKeyboardFocus(false);
  }
  ~TransparentButton() override { stopTimer(); }

  void setDestructiveStyle(bool shouldUseDestructiveStyle) {
    destructiveStyle = shouldUseDestructiveStyle;
    repaint();
  }

  void setSystemGlyphSize(float size) {
    systemGlyphSize = juce::jmax(0.0f, size);
    repaint();
  }

  void paintButton(juce::Graphics &g, bool isMouseOver,
                   bool isButtonDown) override {
    float targetScale = isButtonDown ? 0.99f : 1.0f;
    currentScale += (targetScale - currentScale) * 0.2f;

    float targetAlpha = isMouseOver ? 1.0f : 0.8f;
    currentAlpha += (targetAlpha - currentAlpha) * 0.2f;

    const float destructiveHoverTarget =
        destructiveStyle && isEnabled() && isMouseOver ? 1.0f : 0.0f;
    const float destructivePressTarget =
        destructiveStyle && isEnabled() && isButtonDown ? 1.0f : 0.0f;
    destructiveHoverProgress +=
        (destructiveHoverTarget - destructiveHoverProgress) * 0.22f;
    destructivePressProgress +=
        (destructivePressTarget - destructivePressProgress) * 0.28f;

    auto bounds = getLocalBounds().toFloat();
    auto center = bounds.getCentre();

    juce::AffineTransform transform =
        juce::AffineTransform::translation(-center)
            .scaled(currentScale)
            .translated(center);

    g.addTransform(transform);
    g.setOpacity(currentAlpha);

    if (auto *laf =
            dynamic_cast<class FluentLookAndFeel *>(&getLookAndFeel())) {
      const bool showDestructiveState = destructiveHoverProgress > 0.001f;
      if (showDestructiveState) {
        const auto closeHover = juce::Colour(0xFFC42B1C);
        const auto closePressed = juce::Colour(0xFFA80000);
        g.setColour(closeHover
                        .interpolatedWith(closePressed,
                                          destructivePressProgress)
                        .withMultipliedAlpha(destructiveHoverProgress));
        juce::Path closeSurface;
        closeSurface.addRoundedRectangle(
            bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
            fluentDialogCornerRadius, fluentDialogCornerRadius, false, true,
            false, false);
        g.fillPath(closeSurface);
      } else {
        laf->drawButtonBackground(g, *this, juce::Colours::transparentBlack,
                                  isMouseOver, isButtonDown);
      }
      const auto buttonText = getButtonText();
      if (buttonText.isNotEmpty()) {
        const auto firstChar =
            (uint32_t)buttonText.getCharPointer().getAndAdvance();
        g.setColour(laf->getColors().textPrimary.interpolatedWith(
            juce::Colours::white, destructiveHoverProgress));
        if (firstChar >= 0xE000) {
          if (systemGlyphSize > 0.0f)
            laf->drawSystemIconGlyph(g, buttonText,
                                     getLocalBounds().toFloat(),
                                     systemGlyphSize);
          else
            laf->drawIconGlyph(g, buttonText, getLocalBounds().toFloat(),
                               LegacyDesignTokens::Icon::toolbar);
        } else {
          g.setFont(laf->getBodyFont());
          g.drawText(buttonText, getLocalBounds(),
                     juce::Justification::centred, false);
        }
      }
    }
    if (hasKeyboardFocus(false)) {
      g.setColour(findColour(juce::TextButton::textColourOnId, true));
      g.drawRoundedRectangle(bounds.reduced(2.0f), 6.0f, 2.0f);
    }
  }

  void timerCallback() override {
    const float targetScale = isMouseButtonDown() ? 0.99f : 1.0f;
    const float targetAlpha = isMouseOver() ? 1.0f : 0.8f;
    const float hoverTarget =
        destructiveStyle && isEnabled() && isMouseOver() ? 1.0f : 0.0f;
    const float pressTarget =
        destructiveStyle && isEnabled() && isMouseButtonDown() ? 1.0f : 0.0f;
    const bool destructiveAnimating =
        std::abs(destructiveHoverProgress - hoverTarget) > 0.01f ||
        std::abs(destructivePressProgress - pressTarget) > 0.01f;
    if (destructiveAnimating ||
        shouldRunButtonAnimationTimer(isMouseOver() || isMouseButtonDown(),
                                      currentScale, targetScale, currentAlpha,
                                      targetAlpha)) {
      repaint();
    } else {
      stopTimer();
    }
  }

  void buttonStateChanged() override {
    updateTimerState();
    repaint();
  }

  void setTooltip(const juce::String &tip) { tooltip = tip; }
  juce::String getTooltip() override { return tooltip; }
  void setButtonText(const juce::String &newText) {
    customText = newText;
    repaint();
  }
  juce::String getButtonText() const { return customText; }

private:
  void updateTimerState() {
    const float targetScale = isMouseButtonDown() ? 0.99f : 1.0f;
    const float targetAlpha = isMouseOver() ? 1.0f : 0.8f;
    const float hoverTarget =
        destructiveStyle && isEnabled() && isMouseOver() ? 1.0f : 0.0f;
    const float pressTarget =
        destructiveStyle && isEnabled() && isMouseButtonDown() ? 1.0f : 0.0f;
    const bool destructiveAnimating =
        std::abs(destructiveHoverProgress - hoverTarget) > 0.01f ||
        std::abs(destructivePressProgress - pressTarget) > 0.01f;
    if (destructiveAnimating ||
        shouldRunButtonAnimationTimer(isMouseOver() || isMouseButtonDown(),
                                      currentScale, targetScale, currentAlpha,
                                      targetAlpha)) {
      if (!isTimerRunning())
        startTimerHz(60);
    } else {
      stopTimer();
    }
  }

  juce::String tooltip;
  juce::String customText;
  bool destructiveStyle = false;
  float systemGlyphSize = 0.0f;
  float currentScale = 1.0f;
  float currentAlpha = 0.8f;
  float destructiveHoverProgress = 0.0f;
  float destructivePressProgress = 0.0f;
};

/**
    嵌入式悬浮提示框。

    直接作为父窗口子组件绘制，不创建原生 TooltipWindow，
    避免独立窗口在圆角和透明区域留下残影。
*/
class EmbeddedTooltip : public juce::Component,
                        public juce::Timer,
                        private juce::FocusChangeListener,
                        private juce::KeyListener {
public:
  explicit EmbeddedTooltip(FluentLookAndFeel &laf) : lookAndFeel(laf) {
    setInterceptsMouseClicks(true, false);
    setAlwaysOnTop(true);
    setVisible(false);
    juce::Desktop::getInstance().addFocusChangeListener(this);
  }

  ~EmbeddedTooltip() override {
    if (auto *desktop = juce::Desktop::getInstanceWithoutCreating())
      desktop->removeFocusChangeListener(this);
    if (auto *parent = keyListenerParent.getComponent())
      parent->removeKeyListener(this);
  }

  void setPaintedByParentOverlay(bool shouldDeferPainting) {
    paintedByParentOverlay = shouldDeferPainting;
    repaint();
  }

  void mouseMove(const juce::MouseEvent &event) override {
    if (event.eventComponent != this) {
      if (auto *target = findTooltipTarget(event.eventComponent)) {
        const auto tip =
            dynamic_cast<juce::TooltipClient *>(target)->getTooltip();
        showForComponent(target, tip);
        return;
      }
    }

    if (isVisible() &&
        (event.eventComponent == this ||
         isPointerInInteractionBridge(event.getScreenPosition())))
      return;

    hideTooltip();
  }

  void mouseExit(const juce::MouseEvent &event) override {
    juce::ignoreUnused(event);
    // 可见提示框由定时状态检查处理退出，保留穿过锚点间隙的指针路径。
  }

  void mouseDown(const juce::MouseEvent &) override {
    hideTooltip();
  }

  void mouseUp(const juce::MouseEvent &) override {
    hideTooltip();
  }

  void parentHierarchyChanged() override { updateKeyListenerParent(); }

  // 为特定组件显示 tooltip，需要转换到父组件坐标系。
  void showForComponent(juce::Component *target, const juce::String &text) {
    if (target == nullptr || text.isEmpty()) {
      return;
    }

    if (shouldDismissTooltipForCurrentState(target)) {
      hideTooltip();
      return;
    }

    const bool sameTarget = currentTarget.getComponent() == target;
    if ((isVisible() && sameTarget && currentText == text) ||
        (isWaitingToShow && sameTarget && pendingText == text)) {
      return;
    }

    setVisible(false);
    currentText.clear();
    currentTarget = juce::Component::SafePointer<juce::Component>(target);
    pendingText = text;
    delayCounter = 0;
    isWaitingToShow = true;

    if (!isTimerRunning()) {
      startTimerHz(60);
    }
  }

  void hideTooltip() {
    if (!isVisible() && !isWaitingToShow) {
      return;
    }

    auto *parent = getParentComponent();
    const auto previousBounds = getBounds();
    const bool wasVisible = isVisible();
    isWaitingToShow = false;
    pendingText.clear();
    currentTarget = nullptr;
    setVisible(false);
    stopTimer();

    if (wasVisible && parent != nullptr && !previousBounds.isEmpty())
      parent->repaint(previousBounds);

    setBounds(0, 0, 0, 0);

    if (wasVisible && parent != nullptr && !previousBounds.isEmpty())
      parent->repaint(previousBounds);
  }

  void paint(juce::Graphics &g) override {
    if (paintedByParentOverlay)
      return;
    paintOverlay(g);
  }

  void paintOverlay(juce::Graphics &g) {
    lookAndFeel.drawFluentTooltip(g, currentText,
                                  getLocalBounds().toFloat());
  }

  void timerCallback() override {
    if (isWaitingToShow) {
      if (shouldDismissTooltipForCurrentState()) {
        hideTooltip();
        return;
      }

      delayCounter++;
      // 60 Hz 下 30 帧约等于 500 ms。
      if (delayCounter >= 30) {
        showTooltipNow();
      }
      return;
    }

    if (isVisible() && shouldDismissTooltipForCurrentState())
      hideTooltip();
  }

private:
  void globalFocusChanged(juce::Component *focusedComponent) override {
    if (auto *target = findTooltipTarget(focusedComponent)) {
      const auto tip =
          dynamic_cast<juce::TooltipClient *>(target)->getTooltip();
      showForComponent(target, tip);
      return;
    }

    if (shouldDismissTooltipForCurrentState())
      hideTooltip();
  }

  bool keyPressed(const juce::KeyPress &key,
                  juce::Component *) override {
    if (key == juce::KeyPress::escapeKey &&
        (isVisible() || isWaitingToShow)) {
      hideTooltip();
      return true;
    }
    return false;
  }

  void updateKeyListenerParent() {
    auto *newParent = getParentComponent();
    if (keyListenerParent.getComponent() == newParent)
      return;
    if (auto *oldParent = keyListenerParent.getComponent())
      oldParent->removeKeyListener(this);
    keyListenerParent =
        juce::Component::SafePointer<juce::Component>(newParent);
    if (newParent != nullptr)
      newParent->addKeyListener(this);
  }

  juce::Component *findTooltipTarget(juce::Component *component) const {
    return TooltipPlacement::findAnchorTarget(component, getParentComponent());
  }

  bool isPointerInInteractionBridge(juce::Point<int> screenPosition) const {
    auto *target = currentTarget.getComponent();
    auto *parent = getParentComponent();
    if (!isVisible() || target == nullptr || parent == nullptr)
      return false;

    const auto targetBounds =
        parent->getLocalArea(target, target->getLocalBounds());
    const auto bridge = targetBounds.getUnion(getBounds()).expanded(2);
    return bridge.contains(parent->getLocalPoint(nullptr, screenPosition));
  }

  bool shouldDismissTooltipForCurrentState(
      juce::Component *targetOverride = nullptr) const {
    auto *target = targetOverride != nullptr
                       ? targetOverride
                       : currentTarget.getComponent();
    auto *parent = getParentComponent();
    if (target == nullptr || parent == nullptr || !target->isShowing() ||
        !parent->isShowing())
      return true;

    const bool targetIsActive = target->isMouseOver(true) ||
                                target->hasKeyboardFocus(true);
    const bool tooltipInteractionActive =
        isVisible() &&
        (isMouseOver(false) || isPointerInInteractionBridge(
                                   juce::Desktop::getMousePosition()));
    if (!targetIsActive && !tooltipInteractionActive)
      return true;

    if (auto *topLevel = dynamic_cast<juce::TopLevelWindow *>(
            target->getTopLevelComponent())) {
      if (!topLevel->isActiveWindow())
        return true;
    }

    auto *tooltipClient = dynamic_cast<juce::TooltipClient *>(target);
    if (tooltipClient == nullptr || tooltipClient->getTooltip().isEmpty() ||
        target->isCurrentlyBlockedByAnotherModalComponent())
      return true;

    return juce::ModifierKeys::getCurrentModifiers()
        .isAnyMouseButtonDown();
  }

  void showTooltipNow() {
    if (pendingText.isEmpty() || currentTarget == nullptr ||
        shouldDismissTooltipForCurrentState()) {
      hideTooltip();
      return;
    }

    currentText = pendingText;
    isWaitingToShow = false;

    const auto tooltipSize = lookAndFeel.getFluentTooltipSize(currentText);

    auto *topLevel = getParentComponent();
    if (topLevel == nullptr) {
      hideTooltip();
      return;
    }

    if (tooltipSize.x <= 0 || tooltipSize.y <= 0) {
      hideTooltip();
      return;
    }

    auto targetBounds = topLevel->getLocalArea(currentTarget.getComponent(),
                                               currentTarget->getLocalBounds());

    const auto bounds = TooltipPlacement::place(
        tooltipSize, targetBounds, topLevel->getLocalBounds());
    if (bounds.isEmpty()) {
      hideTooltip();
      return;
    }
    setBounds(bounds);
    setVisible(true);
    toFront(false);
    repaint();
    startTimerHz(30);
  }

private:
  FluentLookAndFeel &lookAndFeel;
  juce::Component::SafePointer<juce::Component> currentTarget;
  juce::String currentText;
  juce::String pendingText;
  int delayCounter = 0;
  bool isWaitingToShow = false;
  bool paintedByParentOverlay = false;
  juce::Component::SafePointer<juce::Component> keyListenerParent;
};

class ToastComponent : public juce::Component, public juce::Timer {
public:
  ToastComponent() {
    setInterceptsMouseClicks(false, false);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colours::white);
    label.setFont(juce::Font(juce::FontOptions(14.0f).withStyle("Bold")));
    addAndMakeVisible(label);
  }

  void show(const juce::String &text, juce::Rectangle<int> targetBounds,
            int holdDurationMs = 0) {
    showAt(text, targetBounds.getCentreX() - getToastWidth(text) / 2,
           targetBounds.getY() - toastHeight - 10, holdDurationMs);
  }

  void showTopCenter(const juce::String &text, juce::Rectangle<int> parentBounds,
                     int holdDurationMs = 0) {
    showAt(text, parentBounds.getCentreX() - getToastWidth(text) / 2,
           parentBounds.getY() + 16, holdDurationMs);
  }

  void paint(juce::Graphics &g) override {
    g.setOpacity(alpha);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 18.0f);
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 18.0f,
                           1.0f);
  }

  void timerCallback() override {
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();

    if (nowMs < fadeStartTimeMs)
      return;

    if (closeTimeMs > 0.0) {
      if (nowMs >= closeTimeMs) {
        alpha = 0.0f;
        setVisible(false);
        stopTimer();
        repaint();
        return;
      }

      const auto fadeWindowMs = juce::jmax(1.0, closeTimeMs - fadeStartTimeMs);
      alpha = static_cast<float>(
          juce::jlimit(0.0, 1.0, (closeTimeMs - nowMs) / fadeWindowMs));
    } else {
      alpha -= 0.05f;
    }

    if (alpha <= 0.0f) {
      alpha = 0.0f;
      setVisible(false);
      stopTimer();
    }
    repaint();
  }

  void resized() override { label.setBounds(getLocalBounds()); }

private:
  static constexpr int toastHeight = 36;

  int getToastWidth(const juce::String &text) const {
    return juce::GlyphArrangement::getStringWidthInt(label.getFont(), text) +
           32;
  }

  void showAt(const juce::String &text, int x, int y, int holdDurationMs) {
    label.setText(text, juce::dontSendNotification);
    setBounds(x, y, getToastWidth(text), toastHeight);

    alpha = 1.0f;
    const auto nowMs = juce::Time::getMillisecondCounterHiRes();
    if (holdDurationMs > 0) {
      closeTimeMs = nowMs + holdDurationMs;
      fadeStartTimeMs = juce::jmax(nowMs, closeTimeMs - fadeDurationMs);
    } else {
      closeTimeMs = 0.0;
      fadeStartTimeMs = nowMs;
    }
    setVisible(true);
    startTimerHz(30);
  }
  juce::Label label;
  float alpha = 0.0f;
  static constexpr double fadeDurationMs = 650.0;
  double fadeStartTimeMs = 0.0;
  double closeTimeMs = 0.0;
};

// LookAndFeel 的窗口按钮工厂放在文件尾部，避免与 TransparentButton 循环依赖。
inline juce::Button *
FluentLookAndFeel::createDocumentWindowButton(int buttonType) {
  auto *b = new TransparentButton("");

  if (buttonType == juce::DocumentWindow::closeButton) {
    b->setButtonText(L"\uE8BB");
    b->setDestructiveStyle(true);
  } else if (buttonType == juce::DocumentWindow::minimiseButton) {
    b->setButtonText(L"\uE921");
  } else if (buttonType == juce::DocumentWindow::maximiseButton) {
    b->setButtonText(L"\uE922");
  }

  b->setSystemGlyphSize(LegacyDesignTokens::Icon::windowCaption);

  return b;
}

inline void FluentLookAndFeel::positionDocumentWindowButtons(
    juce::DocumentWindow &window, int titleBarX, int titleBarY, int titleBarW,
    int titleBarH, juce::Button *minimiseButton, juce::Button *maximiseButton,
    juce::Button *closeButton, bool positionTitleBarButtonsOnLeft) {
  const int margin =
      LegacyDesignTokens::Layout::dialogCaptionButtonOuterMargin;
  const int buttonHeight = juce::jmax(1, titleBarH);
  const int buttonWidth =
      LegacyDesignTokens::Layout::dialogCaptionButtonWidth;
  const int outerRight = juce::jmax(window.getWidth(), titleBarX + titleBarW);
  const int rightAlignedY = 0;
  const int rightAlignedHeight = buttonHeight + juce::jmax(0, titleBarY);
  int x = positionTitleBarButtonsOnLeft
              ? titleBarX + margin
              : outerRight - margin - buttonWidth;

  auto position = [&](juce::Button *button) {
    if (button == nullptr)
      return;
    button->setBounds(x,
                      positionTitleBarButtonsOnLeft ? titleBarY : rightAlignedY,
                      buttonWidth,
                      positionTitleBarButtonsOnLeft ? buttonHeight
                                                    : rightAlignedHeight);
    x += positionTitleBarButtonsOnLeft ? buttonWidth : -buttonWidth;
  };

  position(closeButton);
  if (positionTitleBarButtonsOnLeft)
    std::swap(minimiseButton, maximiseButton);
  position(maximiseButton);
  position(minimiseButton);
}
