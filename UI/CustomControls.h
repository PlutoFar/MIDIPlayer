#pragma once

#include "CustomLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

class TransparentButton : public juce::TextButton, public juce::Timer {
public:
  explicit TransparentButton(const juce::String &btnText = "")
      : juce::TextButton(btnText) {
    startTimerHz(60);
  }
  ~TransparentButton() override { stopTimer(); }

  void paintButton(juce::Graphics &g, bool isMouseOver,
                   bool isButtonDown) override {
    // Minimal scaling, focus on opacity
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
      laf->drawButtonBackground(g, *this, juce::Colours::transparentBlack,
                                isMouseOver, isButtonDown);
      laf->drawButtonText(g, *this, isMouseOver, isButtonDown);
    }
  }

  void timerCallback() override {
    if (isMouseOver() || isMouseButtonDown() ||
        std::abs(currentScale - 1.0f) > 0.01f ||
        std::abs(currentAlpha - 0.8f) > 0.01f) {
      repaint();
    }
  }

private:
  float currentScale = 1.0f;
  float currentAlpha = 0.8f;
};

class InvisibleButton : public juce::Button {
public:
  InvisibleButton() : juce::Button({}) {}
  void paintButton(juce::Graphics &, bool, bool) override {}
};

class ScrollingLabel : public juce::Component, public juce::Timer {
public:
  ~ScrollingLabel() override { stopTimer(); }

  void setText(const juce::String &newText, juce::NotificationType) {
    if (text == newText)
      return;

    text = newText;
    scrollOffset = 0.0f;
    repaint();
  }

  void setFont(const juce::Font &newFont) {
    font = newFont;
    repaint();
  }

  void setColour(int colourId, juce::Colour colour) {
    if (colourId == juce::Label::textColourId)
      textColour = colour;
  }

  void paint(juce::Graphics &g) override {
    g.setFont(font);
    g.setColour(textColour);

    const auto textWidth = getTextWidth(text);
    if (textWidth <= static_cast<float>(getWidth()) || !isHovered) {
      g.drawText(text, getLocalBounds(), juce::Justification::centredLeft,
                 true);
      return;
    }

    g.drawText(text, static_cast<int>(-scrollOffset), 0,
               static_cast<int>(textWidth) + 20, getHeight(),
               juce::Justification::centredLeft, false);
  }

  void mouseDown(const juce::MouseEvent &event) override {
    if (auto *parent = getParentComponent())
      parent->mouseDown(event.getEventRelativeTo(parent));
  }

  void mouseEnter(const juce::MouseEvent &) override {
    isHovered = true;
    if (getTextWidth(text) > static_cast<float>(getWidth())) {
      scrollOffset = 0.0f;
      startTimerHz(30);
    }
  }

  void mouseExit(const juce::MouseEvent &) override {
    isHovered = false;
    stopTimer();
    scrollOffset = 0.0f;
    repaint();
  }

  void timerCallback() override {
    const auto maxScroll =
        getTextWidth(text) - static_cast<float>(getWidth()) + 20.0f;
    scrollOffset += 1.5f;
    if (scrollOffset >= maxScroll + 50.0f)
      scrollOffset = -50.0f;
    repaint();
  }

private:
  float getTextWidth(const juce::String &value) const {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const auto width = static_cast<float>(font.getStringWidth(value));
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    return width;
  }

  juce::String text;
  juce::Font font{juce::FontOptions(14.0f)};
  juce::Colour textColour{juce::Colours::white};
  float scrollOffset = 0.0f;
  bool isHovered = false;
};

class SpinnerComponent : public juce::Component, public juce::Timer {
public:
  ~SpinnerComponent() override { stopTimer(); }

  void visibilityChanged() override {
    if (isVisible() && isShowing())
      startTimerHz(30);
    else
      stopTimer();
  }

  void timerCallback() override {
    angle = std::fmod(angle + 0.15f,
                      juce::MathConstants<float>::twoPi);
    repaint();
  }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat().reduced(8.0f);
    g.setColour(juce::Colour(0xFFFF8C00));
    juce::Path arc;
    arc.addArc(bounds.getX(), bounds.getY(), bounds.getWidth(),
               bounds.getHeight(), angle, angle + 4.0f, true);
    g.strokePath(arc, juce::PathStrokeType(2.5f));
  }

private:
  float angle = 0.0f;
};

class EmbeddedTooltip : public juce::Component, public juce::Timer {
public:
  EmbeddedTooltip() {
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

  // 为特定组件显示 tooltip（需要从父组件坐标系转换）
  void showForComponent(juce::Component *target, const juce::String &text) {
    if (target == nullptr || text.isEmpty()) {
      return;
    }

    // 如果已经在显示同一个目标的 tooltip，不重复
    if (isVisible() && currentTarget == target && currentText == text) {
      return;
    }

    currentTarget = target;
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

    // 立即隐藏，不使用淡出动画
    isWaitingToShow = false;
    isFadingOut = false;
    pendingText.clear();
    currentTarget = nullptr;
    setVisible(false);
    stopTimer();

    // 重置大小为 0x0 确保不影响点击判定
    setBounds(0, 0, 0, 0);
  }

  void paint(juce::Graphics &g) override {
    g.setOpacity(alpha);

    auto bounds = getLocalBounds().toFloat();

    // 深色圆角背景（与 ToastComponent 风格一致）
    g.setColour(juce::Colour(0xE6202020));
    g.fillRoundedRectangle(bounds, 6.0f);

    // 细微边框
    g.setColour(juce::Colours::white.withAlpha(0.1f * alpha));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);

    // 文本
    g.setColour(juce::Colours::white.withAlpha(alpha));
    g.setFont(juce::Font(juce::FontOptions(14.0f)));
    g.drawText(currentText, bounds, juce::Justification::centred, false);
  }

  void timerCallback() override {
    if (isWaitingToShow) {
      delayCounter++;
      // 500ms 延迟后显示 (30 frames at 60Hz)
      if (delayCounter >= 30) {
        showTooltipNow();
      }
      return;
    }

    if (isFadingOut) {
      alpha -= 0.15f;
      if (alpha <= 0.0f) {
        alpha = 0.0f;
        setVisible(false);
        isFadingOut = false;
        currentTarget = nullptr;
        currentText.clear();
        stopTimer();
      }
      repaint();
    }
  }

private:
  void showTooltipNow() {
    if (pendingText.isEmpty() || currentTarget == nullptr) {
      isWaitingToShow = false;
      return;
    }

    currentText = pendingText;
    isWaitingToShow = false;
    isFadingOut = false;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    auto font = juce::Font(juce::FontOptions(14.0f));
    int w = font.getStringWidth(currentText) + 20;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    int h = 32;

    // 获取目标组件在顶层父组件中的位置
    auto *topLevel = getParentComponent();
    if (topLevel == nullptr)
      return;

    auto targetBounds = topLevel->getLocalArea(
        currentTarget->getParentComponent(), currentTarget->getBounds());

    // 定位在目标上方
    int x = targetBounds.getCentreX() - w / 2;
    int y = targetBounds.getY() - h - 8;

    // 如果上方空间不足，显示在下方
    if (y < 4) {
      y = targetBounds.getBottom() + 8;
    }

    // 确保不超出父组件边界
    x = juce::jlimit(4, topLevel->getWidth() - w - 4, x);
    y = juce::jlimit(4, topLevel->getHeight() - h - 4, y);

    setBounds(x, y, w, h);
    alpha = 1.0f;
    setVisible(true);
    toFront(false);
    repaint();
  }

private:
  juce::Component *currentTarget = nullptr;
  juce::String currentText;
  juce::String pendingText;
  int delayCounter = 0;
  float alpha = 1.0f;
  bool isWaitingToShow = false;
  bool isFadingOut = false;
};

// Toast notification for mode changes
class ToastComponent : public juce::Component, public juce::Timer {
public:
  ToastComponent() {
    setInterceptsMouseClicks(false, false);
    label.setJustificationType(juce::Justification::centred);
    label.setColour(juce::Label::textColourId, juce::Colours::white);
    label.setFont(juce::Font(juce::FontOptions(14.0f).withStyle("Bold")));
    addAndMakeVisible(label);
  }

  void show(const juce::String &text, juce::Rectangle<int> targetBounds) {
    label.setText(text, juce::dontSendNotification);

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    int w = label.getFont().getStringWidth(text) + 32;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    int h = 36;

    // Position above the button
    setBounds(targetBounds.getCentreX() - w / 2, targetBounds.getY() - h - 10,
              w, h);

    alpha = 1.0f;
    setVisible(true);
    startTimerHz(30);
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
    alpha -= 0.05f;
    if (alpha <= 0.0f) {
      alpha = 0.0f;
      setVisible(false);
      stopTimer();
    }
    repaint();
  }

  void resized() override { label.setBounds(getLocalBounds()); }

private:
  juce::Label label;
  float alpha = 0.0f;
};

inline juce::Button *
FluentLookAndFeel::createDocumentWindowButton(int buttonType) {
  auto *b = new TransparentButton("");

  if (buttonType == juce::DocumentWindow::closeButton) {
    b->setButtonText(L"\uE8BB"); // Segoe Fluent Icons Close
  } else if (buttonType == juce::DocumentWindow::minimiseButton) {
    b->setButtonText(L"\uE921"); // Minimise
  } else if (buttonType == juce::DocumentWindow::maximiseButton) {
    b->setButtonText(L"\uE922"); // Maximise
  }

  return b;
}
