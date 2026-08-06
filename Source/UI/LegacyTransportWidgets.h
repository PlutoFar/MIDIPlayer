#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class ProgressTimeTooltip final : public juce::Component {
public:
  ProgressTimeTooltip() {
    setInterceptsMouseClicks(false, false);
    setAlwaysOnTop(true);
    setVisible(false);
  }

  void showAt(const juce::String &text, int anchorX, int topY,
              juce::Rectangle<int> parentArea) {
    if (text.isEmpty() || parentArea.isEmpty()) {
      hide();
      return;
    }

    currentText = text;
    const auto font = juce::Font(juce::FontOptions(14.0f));
    const int width =
        juce::GlyphArrangement::getStringWidthInt(font, currentText) + 18;
    constexpr int height = 28;
    auto bounds = juce::Rectangle<int>(anchorX - width / 2,
                                       topY - height - 8, width, height)
                      .constrainedWithin(parentArea.reduced(8));
    setBounds(bounds);
    setVisible(true);
    toFront(false);
    repaint();
  }

  void hide() {
    if (!isVisible())
      return;
    currentText.clear();
    setVisible(false);
    setBounds(0, 0, 0, 0);
  }

  void paint(juce::Graphics &graphics) override {
    const auto bounds = getLocalBounds().toFloat();
    graphics.setColour(juce::Colour(0xE6202020));
    graphics.fillRoundedRectangle(bounds, 6.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.1f));
    graphics.drawRoundedRectangle(bounds.reduced(0.5f), 6.0f, 1.0f);
    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::Font(juce::FontOptions(14.0f)));
    graphics.drawText(currentText, getLocalBounds(),
                      juce::Justification::centred, false);
  }

private:
  juce::String currentText;
};

class ScrollingLabel final : public juce::Component, private juce::Timer {
public:
  ScrollingLabel() = default;
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

  void paint(juce::Graphics &graphics) override {
    graphics.setFont(font);
    graphics.setColour(textColour);
    const float textWidth = getTextWidth(text);
    if (textWidth <= static_cast<float>(getWidth()) || !hovered) {
      graphics.drawText(text, getLocalBounds(),
                        juce::Justification::centredLeft, true);
      return;
    }
    graphics.drawText(text, static_cast<int>(-scrollOffset), 0,
                      static_cast<int>(textWidth) + 20, getHeight(),
                      juce::Justification::centredLeft, false);
  }

  void mouseDown(const juce::MouseEvent &event) override {
    if (auto *parent = getParentComponent())
      parent->mouseDown(event.getEventRelativeTo(parent));
  }

  void mouseEnter(const juce::MouseEvent &) override {
    hovered = true;
    if (getTextWidth(text) > static_cast<float>(getWidth())) {
      scrollOffset = 0.0f;
      startTimerHz(30);
    }
  }

  void mouseExit(const juce::MouseEvent &) override {
    hovered = false;
    stopTimer();
    scrollOffset = 0.0f;
    repaint();
  }

private:
  void timerCallback() override {
    const float maxScroll = getTextWidth(text) - static_cast<float>(getWidth()) + 20.0f;
    scrollOffset += 1.5f;
    if (scrollOffset >= maxScroll + 50.0f)
      scrollOffset = -50.0f;
    repaint();
  }

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
  bool hovered = false;
};
