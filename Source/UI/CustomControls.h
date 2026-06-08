#pragma once

#include "CustomLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

// Button rendered from an SVG string
class SvgButton : public juce::Button, public juce::Timer {
public:
  SvgButton(const juce::String& svgString) : juce::Button("") {
    auto xml = juce::XmlDocument::parse(svgString);
    if (xml != nullptr) {
      drawable = juce::Drawable::createFromSVG(*xml);
    }
    startTimerHz(60);
  }
  ~SvgButton() override { stopTimer(); }

  void paintButton(juce::Graphics &g, bool isMouseOver,
                   bool isButtonDown) override {
    float targetScale = isButtonDown ? 0.95f : (isMouseOver ? 1.05f : 1.0f);
    currentScale += (targetScale - currentScale) * 0.2f;

    float targetAlpha = isEnabled() ? (isMouseOver ? 1.0f : 0.7f) : 0.3f;
    currentAlpha += (targetAlpha - currentAlpha) * 0.2f;

    auto bounds = getLocalBounds().toFloat();
    auto center = bounds.getCentre();

    juce::AffineTransform transform =
        juce::AffineTransform::translation(-center)
            .scaled(currentScale)
            .translated(center);

    g.saveState();
    g.addTransform(transform);

    // Draw Fluent hover/pressed background
    if (isEnabled() && (isMouseOver || isButtonDown)) {
      if (auto *laf = dynamic_cast<FluentLookAndFeel *>(&getLookAndFeel())) {
        auto &colors = laf->getColors();
        g.setColour(isButtonDown ? colors.controlPressed : colors.controlHover);
        g.fillRoundedRectangle(bounds.reduced(2.0f), 6.0f);
      }
    }

    g.setOpacity(currentAlpha);

    if (drawable != nullptr) {
      // Create a white version of the SVG for dark mode UI
      drawable->replaceColour(juce::Colours::black, juce::Colours::white);
      drawable->drawWithin(g, bounds.reduced(6.0f), juce::RectanglePlacement::centred, currentAlpha);
    }
    g.restoreState();
  }

  void timerCallback() override {
    float targetScale = isEnabled() ? (isMouseButtonDown() ? 0.95f : (isMouseOver() ? 1.05f : 1.0f)) : 1.0f;
    float targetAlpha = isEnabled() ? (isMouseOver() ? 1.0f : 0.7f) : 0.3f;

    if (std::abs(currentScale - targetScale) > 0.005f ||
        std::abs(currentAlpha - targetAlpha) > 0.005f) {
      repaint();
    }
  }

private:
  std::unique_ptr<juce::Drawable> drawable;
  float currentScale = 1.0f;
  float currentAlpha = 0.7f;
};

// Animated icon button (like play, prev, next)
class AnimatedIconButton : public juce::Button, public juce::Timer {
public:
  AnimatedIconButton() : juce::Button("") { startTimerHz(60); }
  ~AnimatedIconButton() override { stopTimer(); }

  void paintButton(juce::Graphics &g, bool isMouseOver,
                   bool isButtonDown) override {
    // Simplified animation (very subtle)
    float targetScale = isButtonDown ? 0.98f : (isMouseOver ? 1.01f : 1.0f);
    currentScale += (targetScale - currentScale) * 0.2f;

    auto bounds = getLocalBounds().toFloat();
    auto center = bounds.getCentre();

    juce::AffineTransform transform =
        juce::AffineTransform::translation(-center)
            .scaled(currentScale)
            .translated(center);

    g.addTransform(transform);

    if (auto *laf =
            dynamic_cast<class FluentLookAndFeel *>(&getLookAndFeel())) {
      laf->drawCircularButton(g, bounds, icon, isPlaying, isMouseOver,
                              isButtonDown);
    }
  }

  void timerCallback() override {
    if (isMouseOver() || isMouseButtonDown() ||
        std::abs(currentScale - 1.0f) > 0.01f) {
      repaint();
    }
  }

  void setIcon(const juce::String &newIcon) { icon = newIcon; }
  void setPlaying(bool playing) { isPlaying = playing; }

private:
  juce::String icon;
  bool isPlaying = false;
  float currentScale = 1.0f;
};

// Transparent button with rounded rect and text
class TransparentButton : public juce::Button, public juce::Timer {
public:
  TransparentButton(const juce::String &btnText = "") : juce::Button(btnText) {
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
      const auto text = getButtonText();
      if (text.isNotEmpty()) {
        const auto firstChar = (uint32_t)text.getCharPointer().getAndAdvance();
        g.setFont(firstChar >= 0xE000 ? laf->getIconFont(16.0f)
                                      : laf->getDefaultFont(14.0f));
        g.setColour(laf->getColors().textPrimary);
        g.drawText(text, getLocalBounds(), juce::Justification::centred,
                   false);
      }
    }
  }

  void timerCallback() override {
    if (isMouseOver() || isMouseButtonDown() ||
        std::abs(currentScale - 1.0f) > 0.01f ||
        std::abs(currentAlpha - 0.8f) > 0.01f) {
      repaint();
    }
  }

  void setTooltip(const juce::String &tip) { tooltip = tip; }
  juce::String getTooltip() override { return tooltip; }
  void setButtonText(const juce::String &newText) {
    customText = newText;
    repaint();
  }
  juce::String getButtonText() const { return customText; }

private:
  juce::String tooltip;
  juce::String customText;
  float currentScale = 1.0f;
  float currentAlpha = 0.8f;
};

/**
    EmbeddedTooltip: 嵌入式悬浮提示框

    与 JUCE TooltipWindow 不同，此组件直接嵌入父窗口中绘制，
    完全避免系统窗口边框问题，确保圆角完美无残留。
    工作原理与 ToastComponent 相同。
*/
class EmbeddedTooltip : public juce::Component, public juce::Timer {
public:
  EmbeddedTooltip() {
    setInterceptsMouseClicks(false, false);
    setAlwaysOnTop(true);
    setVisible(false);
  }

  // --- MouseListener ---
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

// Implementation for LookAndFeel (to avoid circular dependency)
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
