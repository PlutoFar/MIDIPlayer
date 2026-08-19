#pragma once

#include "CustomLookAndFeel.h"
#include "TooltipPlacement.h"
#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>

inline float volumeLevelToGain(float level) {
  return std::pow(juce::jlimit(0.0f, 1.0f, level),
                  LegacyDesignTokens::Slider::volumeGainExponent);
}

inline juce::String getVolumeIconGlyph(float volume) {
  if (volume <= 0.001f)
    return L"\uE74F";
  if (volume <= 1.0f / 3.0f)
    return L"\uE993";
  if (volume <= 2.0f / 3.0f)
    return L"\uE994";
  return L"\uE995";
}

class VolumeSlider final : public juce::Slider {
public:
  VolumeSlider() {
    setSliderStyle(juce::Slider::LinearHorizontal);
    setRange(0.0, 1.0, LegacyDesignTokens::Slider::volumeStep);
    setSliderSnapsToMousePosition(true);
    setVelocityBasedMode(false);
    getProperties().set("fluentPreciseThumbHover", true);
  }

  bool isPointOverThumb(juce::Point<float> point) const {
    if (!isEnabled() || getWidth() <= 0 || getHeight() <= 0)
      return false;

    const auto centre = getThumbCentre();
    const float radius = LegacyDesignTokens::Slider::thumbDiameter * 0.5f;
    const auto delta = point - centre;
    return delta.x * delta.x + delta.y * delta.y <= radius * radius;
  }

  juce::Rectangle<float> getThumbBounds() const {
    const auto centre = getThumbCentre();
    const float diameter = LegacyDesignTokens::Slider::thumbDiameter;
    const float radius = diameter * 0.5f;
    return {centre.x - radius, centre.y - radius, diameter, diameter};
  }

  juce::Rectangle<int> getThumbBoundsInParent() const {
    return getThumbBounds()
        .getSmallestIntegerContainer()
        .translated(getX(), getY());
  }

  bool isThumbDragActive() const { return thumbDragActive; }

  void mouseDown(const juce::MouseEvent &event) override {
    thumbDragActive = true;
    juce::Slider::mouseDown(event);
    repaint();
  }

  void mouseDrag(const juce::MouseEvent &event) override {
    juce::Slider::mouseDrag(event);
    repaint();
  }

  void mouseUp(const juce::MouseEvent &event) override {
    juce::Slider::mouseUp(event);
    thumbDragActive = false;
    repaint();
  }

  void mouseEnter(const juce::MouseEvent &event) override {
    juce::Slider::mouseEnter(event);
    repaint();
  }

  void mouseMove(const juce::MouseEvent &event) override {
    juce::Slider::mouseMove(event);
    repaint();
  }

  void mouseExit(const juce::MouseEvent &event) override {
    juce::Slider::mouseExit(event);
    repaint();
  }

private:
  juce::Point<float> getThumbCentre() const {
    return {(float)getPositionOfValue(getValue()), (float)getHeight() * 0.5f};
  }

  bool thumbDragActive = false;
};

struct VolumeTooltipInfo {
  juce::String text;
  juce::Rectangle<int> anchorBounds;
};

inline VolumeTooltipInfo getVolumeTooltipInfo(const VolumeSlider &slider) {
  const auto minimum = slider.getMinimum();
  const auto maximum = slider.getMaximum();
  const auto value = juce::jlimit(minimum, maximum, slider.getValue());
  const auto range = maximum - minimum;
  const auto normalized =
      range > 0.0 ? juce::jlimit(0.0, 1.0, (value - minimum) / range) : 0.0;
  return {juce::String(juce::roundToInt(normalized * 100.0)) + "%",
          slider.getThumbBoundsInParent()};
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
              const juce::Array<juce::Rectangle<int>> &hardAvoidAreas = {},
              const juce::Array<juce::Rectangle<int>> &softAvoidAreas = {}) {
    showTooltip(text, anchor, parentArea, hardAvoidAreas, softAvoidAreas,
                Presentation::standard, 8, 4);
  }

  void showValueAt(const juce::String &text, juce::Rectangle<int> anchor,
                   juce::Rectangle<int> parentArea) {
    showTooltip(text, anchor, parentArea, {}, {}, Presentation::compactValue,
                LegacyDesignTokens::Slider::volumeTooltipGap, 8);
  }

  void showForTarget(const juce::String &text, juce::Rectangle<int> anchor,
                     juce::Component &coordinateSpace) {
    showAt(text, anchor, coordinateSpace.getLocalBounds());
  }

  void hide() {
    currentText.clear();
    setVisible(false);
    setBounds(0, 0, 0, 0);
  }

  void paint(juce::Graphics &graphics) override {
    if (presentation == Presentation::compactValue) {
      lookAndFeel.drawFluentValueTooltip(graphics, currentText,
                                         getLocalBounds().toFloat());
      return;
    }

    lookAndFeel.drawFluentTooltip(graphics, currentText,
                                  getLocalBounds().toFloat());
  }

private:
  enum class Presentation { standard, compactValue };

  void showTooltip(
      const juce::String &text, juce::Rectangle<int> anchor,
      juce::Rectangle<int> parentArea,
      const juce::Array<juce::Rectangle<int>> &hardAvoidAreas,
      const juce::Array<juce::Rectangle<int>> &softAvoidAreas,
      Presentation requestedPresentation, int gap, int edgeMargin) {
    if (text.isEmpty() || parentArea.isEmpty()) {
      hide();
      return;
    }

    currentText = text;
    presentation = requestedPresentation;
    const auto tooltipSize =
        presentation == Presentation::compactValue
            ? lookAndFeel.getFluentValueTooltipSize(currentText)
            : lookAndFeel.getFluentTooltipSize(currentText);
    const auto bounds = TooltipPlacement::place(
        tooltipSize, anchor, parentArea, hardAvoidAreas, softAvoidAreas, gap,
        edgeMargin);
    if (bounds.isEmpty()) {
      hide();
      return;
    }
    setBounds(bounds);
    setVisible(true);
    toFront(false);
    repaint();
  }
  FluentLookAndFeel &lookAndFeel;
  juce::String currentText;
  Presentation presentation = Presentation::standard;
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
