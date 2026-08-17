#pragma once

#include "CustomLookAndFeel.h"
#include "TooltipPlacement.h"
#include <juce_gui_basics/juce_gui_basics.h>

inline juce::String getVolumeIconGlyph(float volume) {
  if (volume <= 0.0f)
    return L"\uE992";
  if (volume <= 1.0f / 3.0f)
    return L"\uE993";
  if (volume <= 2.0f / 3.0f)
    return L"\uE994";
  return L"\uE995";
}

class ProgressTimeTooltip final : public juce::Component {
public:
  explicit ProgressTimeTooltip(FluentLookAndFeel &laf) : lookAndFeel(laf) {
    setInterceptsMouseClicks(false, false);
    setAlwaysOnTop(true);
    setVisible(false);
  }

  void showAt(const juce::String &text, juce::Rectangle<int> anchor,
              juce::Rectangle<int> parentArea,
              const juce::Array<juce::Rectangle<int>> &avoidAreas = {}) {
    if (text.isEmpty() || parentArea.isEmpty()) {
      hide();
      return;
    }

    currentText = text;
    const auto bounds = TooltipPlacement::place(
        lookAndFeel.getFluentTooltipSize(currentText), anchor, parentArea,
        avoidAreas);
    if (bounds.isEmpty()) {
      hide();
      return;
    }
    setBounds(bounds);
    setVisible(true);
    toFront(false);
    repaint();
  }

  void hide() {
    currentText.clear();
    setVisible(false);
    setBounds(0, 0, 0, 0);
  }

  void paint(juce::Graphics &graphics) override {
    lookAndFeel.drawFluentTooltip(graphics, currentText,
                                  getLocalBounds().toFloat());
  }

private:
  FluentLookAndFeel &lookAndFeel;
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
    return juce::GlyphArrangement::getStringWidth(font, value);
  }

  juce::String text;
  juce::Font font{juce::FontOptions(14.0f)};
  juce::Colour textColour{juce::Colours::white};
  float scrollOffset = 0.0f;
  bool hovered = false;
};
