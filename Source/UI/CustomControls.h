#pragma once

#include "ButtonAnimationSupport.h"
#include "CustomLookAndFeel.h"
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

    if (isEnabled() && (isMouseOver || isButtonDown)) {
      if (auto *laf = dynamic_cast<FluentLookAndFeel *>(&getLookAndFeel())) {
        auto &colors = laf->getColors();
        g.setColour(isButtonDown ? colors.controlPressed : colors.controlHover);
        g.fillRoundedRectangle(bounds.reduced(2.0f), 6.0f);
      }
    }

    g.setOpacity(currentAlpha);

    if (drawable != nullptr) {
      // 源 SVG 应使用黑色图标，深色主题下统一替换为白色。
      drawable->replaceColour(juce::Colours::black, juce::Colours::white);
      drawable->drawWithin(
          g, bounds.withSizeKeepingCentre(iconVisualSize, iconVisualSize),
          juce::RectanglePlacement::centred, currentAlpha);
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

  void paintButton(juce::Graphics &g, bool isMouseOver,
                   bool isButtonDown) override {
    float targetScale = isButtonDown ? 0.99f : 1.0f;
    currentScale += (targetScale - currentScale) * 0.2f;

    float targetAlpha = isMouseOver ? 1.0f : 0.8f;
    currentAlpha += (targetAlpha - currentAlpha) * 0.2f;

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
      const bool showDestructiveState =
          destructiveStyle && isEnabled() && (isMouseOver || isButtonDown);
      if (showDestructiveState) {
        g.setColour(isButtonDown ? juce::Colour(0xFFA80000)
                                 : juce::Colour(0xFFC42B1C));
        g.fillRect(bounds);
      } else {
        laf->drawButtonBackground(g, *this, juce::Colours::transparentBlack,
                                  isMouseOver, isButtonDown);
      }
      const auto buttonText = getButtonText();
      if (buttonText.isNotEmpty()) {
        const auto firstChar =
            (uint32_t)buttonText.getCharPointer().getAndAdvance();
        g.setColour(showDestructiveState ? juce::Colours::white
                                         : laf->getColors().textPrimary);
        if (firstChar >= 0xE000) {
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
    if (shouldRunButtonAnimationTimer(isMouseOver() || isMouseButtonDown(),
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
  float currentScale = 1.0f;
  float currentAlpha = 0.8f;
};

/**
    嵌入式悬浮提示框。

    直接作为父窗口子组件绘制，不创建原生 TooltipWindow，
    避免独立窗口在圆角和透明区域留下残影。
*/
class EmbeddedTooltip : public juce::Component, public juce::Timer {
public:
  explicit EmbeddedTooltip(FluentLookAndFeel &laf) : lookAndFeel(laf) {
    setInterceptsMouseClicks(false, false);
    setAlwaysOnTop(true);
    setVisible(false);
  }

  void mouseMove(const juce::MouseEvent &event) override {
    auto *comp = event.eventComponent;
    if (comp != nullptr && comp != this && comp != getParentComponent()) {
      if (auto *tooltipClient = dynamic_cast<juce::TooltipClient *>(comp)) {
        auto tip = tooltipClient->getTooltip();
        if (tip.isNotEmpty()) {
          showForComponent(comp, tip);
          return;
        }
      }
    }
    hideTooltip();
  }

  void mouseExit(const juce::MouseEvent &event) override {
    auto *comp = event.eventComponent;
    if (auto *tooltipClient = dynamic_cast<juce::TooltipClient *>(comp)) {
      if (tooltipClient->getTooltip().isNotEmpty()) {
        hideTooltip();
      }
    }
  }

  // 为特定组件显示 tooltip，需要转换到父组件坐标系。
  void showForComponent(juce::Component *target, const juce::String &text) {
    if (target == nullptr || text.isEmpty()) {
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

    isWaitingToShow = false;
    pendingText.clear();
    currentTarget = nullptr;
    setVisible(false);
    stopTimer();

    // 重置为 0x0，避免隐藏后影响点击命中。
    setBounds(0, 0, 0, 0);
  }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();

    g.setColour(juce::Colour(0xF0202020));
    g.fillRoundedRectangle(bounds, 8.0f);

    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);

    g.setColour(juce::Colours::white);
    g.setFont(lookAndFeel.getBodyFont());
    g.drawText(currentText, bounds, juce::Justification::centred, false);
  }

  void timerCallback() override {
    if (isWaitingToShow) {
      delayCounter++;
      // 60 Hz 下 30 帧约等于 500 ms。
      if (delayCounter >= 30) {
        showTooltipNow();
      }
      return;
    }
  }

private:
  void showTooltipNow() {
    if (pendingText.isEmpty() || currentTarget == nullptr) {
      isWaitingToShow = false;
      setVisible(false);
      stopTimer();
      return;
    }

    currentText = pendingText;
    isWaitingToShow = false;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    auto font = lookAndFeel.getBodyFont();
    int w = font.getStringWidth(currentText) + 24;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    int h = LegacyDesignTokens::Layout::controlHeight(
        lookAndFeel.getUIFontSize());

    auto *topLevel = getParentComponent();
    if (topLevel == nullptr)
      return;

    w = juce::jmin(w, juce::jmax(0, topLevel->getWidth() - 8));
    if (w <= 0 || h <= 0) {
      setVisible(false);
      stopTimer();
      return;
    }

    auto targetBounds = topLevel->getLocalArea(currentTarget.getComponent(),
                                               currentTarget->getLocalBounds());

    int x = targetBounds.getCentreX() - w / 2;
    int y = targetBounds.getY() - h - 8;

    if (y < 4) {
      y = targetBounds.getBottom() + 8;
    }

    x = juce::jlimit(4, topLevel->getWidth() - w - 4, x);
    y = juce::jlimit(4, topLevel->getHeight() - h - 4, y);

    setBounds(x, y, w, h);
    setVisible(true);
    toFront(false);
    repaint();
    stopTimer();
  }

private:
  FluentLookAndFeel &lookAndFeel;
  juce::Component::SafePointer<juce::Component> currentTarget;
  juce::String currentText;
  juce::String pendingText;
  int delayCounter = 0;
  bool isWaitingToShow = false;
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
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const int width = label.getFont().getStringWidth(text) + 32;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    return width;
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

  return b;
}
