#pragma once

#include "../Utils/UserSettings.h"
#include "CustomControls.h"
#include "FluentSettingsStyle.h"

struct FontSettingsContent : public juce::Component {
  std::function<void()> onSettingsChanged;

  FontSettingsContent(FluentLookAndFeel &laf);

  void paint(juce::Graphics &g) override;

  ~FontSettingsContent() override;

  void populateFontCombo(juce::ComboBox &combo, const juce::String &currentFont,
                         bool isPlaylist = false);

  juce::String getDisplayName(const juce::String &name);

  void refreshTypography();

  void resized() override;

private:
  FluentLookAndFeel &fluentLookAndFeel;
  juce::StringArray availableFonts;
  juce::StringArray fontRealNames_UI;
  juce::StringArray fontRealNames_Playlist;

  juce::Label interfaceSectionLabel, playlistSectionLabel, uiFontLabel,
      uiFontSizeLabel, playlistFontLabel, playlistFontSizeLabel,
      playlistRowSpacingLabel;
  juce::ToggleButton playlistRowSpacingAutoToggle;
  juce::ComboBox uiFontCombo, playlistFontCombo;
  juce::Slider uiFontSizeSlider, playlistFontSizeSlider,
      playlistRowSpacingSlider;
  juce::Rectangle<int> interfaceCardBounds, playlistCardBounds;
};
