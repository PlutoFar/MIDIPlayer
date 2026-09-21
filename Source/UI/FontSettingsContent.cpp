#include "FontSettingsContent.h"
#include "SettingsPersistence.h"

FontSettingsContent::FontSettingsContent(FluentLookAndFeel &laf)
    : fluentLookAndFeel(laf) {
  setLookAndFeel(&fluentLookAndFeel);
  setSize(500, 500);
  setOpaque(false);

  availableFonts = juce::Font::findAllTypefaceNames();

  // Reason: 保存的字体可能未安装；字体选择以本机字体列表为准。
  juce::String currentPlaylistFont = getAppSettings().getPlaylistFontName();
  bool fontExists = false;
  if (availableFonts.contains(currentPlaylistFont)) {
    fontExists = true;
  } else {
    juce::String displayName = getDisplayName(currentPlaylistFont);
    if (availableFonts.contains(displayName))
      fontExists = true;
  }

  if (!fontExists) {
    juce::String fallback = "Microsoft YaHei UI";
    if (availableFonts.contains(fallback)) {
      getAppSettings().setPlaylistFontName(fallback);
      fluentLookAndFeel.setPlaylistFont(fallback);
    } else if (availableFonts.contains("Microsoft YaHei")) {
      getAppSettings().setPlaylistFontName("Microsoft YaHei");
      fluentLookAndFeel.setPlaylistFont("Microsoft YaHei");
    }
  }

  addAndMakeVisible(interfaceSectionLabel);
  interfaceSectionLabel.setText(L"界面字体", juce::dontSendNotification);
  FluentSettingsStyle::configureLabel(interfaceSectionLabel, fluentLookAndFeel,
                                      true);

  addAndMakeVisible(uiFontLabel);
  uiFontLabel.setText(L"字体", juce::dontSendNotification);
  FluentSettingsStyle::configureLabel(uiFontLabel, fluentLookAndFeel);

  addAndMakeVisible(uiFontCombo);
  populateFontCombo(uiFontCombo, getAppSettings().getUIFontName());
  uiFontCombo.onChange = [this]() {
    int id = uiFontCombo.getSelectedId();
    if (id > 0 && id <= fontRealNames_UI.size()) {
      juce::String fontName = fontRealNames_UI[id - 1];
      getAppSettings().setUIFontName(fontName);
      fluentLookAndFeel.setUIFont(fontName);
      refreshTypography();
      if (onSettingsChanged)
        onSettingsChanged();
    }
  };

  addAndMakeVisible(uiFontSizeLabel);
  uiFontSizeLabel.setText(L"基准字号", juce::dontSendNotification);
  FluentSettingsStyle::configureLabel(uiFontSizeLabel, fluentLookAndFeel);

  addAndMakeVisible(uiFontSizeSlider);
  uiFontSizeSlider.setRange(DesignTokens::Typography::minimumBody,
                            DesignTokens::Typography::maximumBody, 1.0);
  uiFontSizeSlider.setValue(getAppSettings().getUIFontSize());
  uiFontSizeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  uiFontSizeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
  uiFontSizeSlider.onValueChange = [this]() {
    const float size = (float)uiFontSizeSlider.getValue();
    getAppSettings().setUIFontSize(size);
    fluentLookAndFeel.setUIFontSize(size);
    refreshTypography();
    if (onSettingsChanged)
      onSettingsChanged();
  };

  addAndMakeVisible(playlistSectionLabel);
  playlistSectionLabel.setText(L"播放列表", juce::dontSendNotification);
  FluentSettingsStyle::configureLabel(playlistSectionLabel, fluentLookAndFeel,
                                      true);

  addAndMakeVisible(playlistFontLabel);
  playlistFontLabel.setText(L"字体", juce::dontSendNotification);
  FluentSettingsStyle::configureLabel(playlistFontLabel, fluentLookAndFeel);

  addAndMakeVisible(playlistFontCombo);
  populateFontCombo(playlistFontCombo, getAppSettings().getPlaylistFontName(),
                    true);
  playlistFontCombo.onChange = [this]() {
    int id = playlistFontCombo.getSelectedId();
    if (id > 0 && id <= fontRealNames_Playlist.size()) {
      juce::String fontName = fontRealNames_Playlist[id - 1];
      getAppSettings().setPlaylistFontName(fontName);
      fluentLookAndFeel.setPlaylistFont(fontName);
      getAppSettings().addRecentFont(fontName);
      if (onSettingsChanged)
        onSettingsChanged();

      // 不立即重建下拉列表，避免重置滚动位置和焦点。
    }
  };

  addAndMakeVisible(playlistFontSizeLabel);
  playlistFontSizeLabel.setText(L"字号", juce::dontSendNotification);
  FluentSettingsStyle::configureLabel(playlistFontSizeLabel, fluentLookAndFeel);

  addAndMakeVisible(playlistFontSizeSlider);
  playlistFontSizeSlider.setRange(12.0, 36.0, 1.0);
  playlistFontSizeSlider.setValue(getAppSettings().getPlaylistFontSize());
  playlistFontSizeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  playlistFontSizeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50,
                                         20);
  playlistFontSizeSlider.onValueChange = [this]() {
    float size = (float)playlistFontSizeSlider.getValue();
    getAppSettings().setPlaylistFontSize(size);
    if (onSettingsChanged)
      onSettingsChanged();
  };

  addAndMakeVisible(playlistRowSpacingAutoToggle);
  playlistRowSpacingAutoToggle.setButtonText(L"自动行间距");
  playlistRowSpacingAutoToggle.setToggleState(
      getAppSettings().getPlaylistRowSpacingAutomatic(),
      juce::dontSendNotification);
  playlistRowSpacingAutoToggle.onClick = [this]() {
    const bool automatic = playlistRowSpacingAutoToggle.getToggleState();
    getAppSettings().setPlaylistRowSpacingAutomatic(automatic);
    playlistRowSpacingSlider.setEnabled(!automatic);
    if (onSettingsChanged)
      onSettingsChanged();
  };

  addAndMakeVisible(playlistRowSpacingLabel);
  playlistRowSpacingLabel.setText(L"行间距", juce::dontSendNotification);
  FluentSettingsStyle::configureLabel(playlistRowSpacingLabel,
                                      fluentLookAndFeel);

  addAndMakeVisible(playlistRowSpacingSlider);
  playlistRowSpacingSlider.setRange(
      DesignTokens::Layout::playlistMinimumRowHeight,
      DesignTokens::Layout::playlistMaximumRowHeight, 1.0);
  playlistRowSpacingSlider.setValue(
      getAppSettings().getPlaylistManualRowHeight());
  playlistRowSpacingSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  playlistRowSpacingSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false,
                                           50, 20);
  playlistRowSpacingSlider.setNumDecimalPlacesToDisplay(0);
  playlistRowSpacingSlider.setEnabled(
      !playlistRowSpacingAutoToggle.getToggleState());
  playlistRowSpacingSlider.onValueChange = [this]() {
    getAppSettings().setPlaylistManualRowHeight(
        juce::roundToInt(playlistRowSpacingSlider.getValue()));
    if (onSettingsChanged)
      onSettingsChanged();
  };
}

void FontSettingsContent::paint(juce::Graphics &g) {
  FluentSettingsStyle::paintPanel(g, fluentLookAndFeel, getLocalBounds());
  FluentSettingsStyle::paintCard(g, fluentLookAndFeel, interfaceCardBounds);
  FluentSettingsStyle::paintCard(g, fluentLookAndFeel, playlistCardBounds);
}

FontSettingsContent::~FontSettingsContent() {
  // Ordering: 应用退出前已完成最终保存时，关闭子窗口只释放界面资源。
  if (getAppSettings().isAutomaticSaveEnabled())
    saveAppSettingsWithFeedback();
  setLookAndFeel(nullptr);
}

void FontSettingsContent::populateFontCombo(juce::ComboBox &combo,
                                            const juce::String &currentFont,
                                            bool isPlaylist) {
  combo.clear();
  juce::StringArray &realNames =
      isPlaylist ? fontRealNames_Playlist : fontRealNames_UI;
  realNames.clear();
  int id = 1;

  auto addItem = [&](const juce::String &realName, int) {
    if (realName.isEmpty())
      return;
    combo.addItem(getDisplayName(realName), id);
    realNames.add(realName);
    if (realName == currentFont)
      combo.setSelectedId(id, juce::dontSendNotification);
    id++;
  };

  juce::StringArray topFonts;

  if (isPlaylist) {
    combo.addSectionHeading(L"常用 & 最近");

    juce::StringArray pinned = {"Microsoft YaHei UI", "SimHei", "SimSun"};

    auto tryAdd = [&](const juce::String &name) {
      if (topFonts.contains(name))
        return;

      if (availableFonts.contains(name)) {
        addItem(name, 0);
        topFonts.add(name);
      } else {
        juce::String mapped = getDisplayName(name);
        if (mapped != name && availableFonts.contains(mapped)) {
          addItem(mapped, 0);
          topFonts.add(mapped);
        }
      }
    };

    for (const auto &p : pinned)
      tryAdd(p);

    auto recent = getAppSettings().getRecentFonts();
    for (const auto &f : recent)
      tryAdd(f);

    combo.addSeparator();
  }

  for (const auto &font : availableFonts) {
    if (!topFonts.contains(font) && !topFonts.contains(getDisplayName(font))) {
      addItem(font, 0);
    }
  }
}

juce::String FontSettingsContent::getDisplayName(const juce::String &name) {
  return name;
}

void FontSettingsContent::refreshTypography() {
  FluentSettingsStyle::configureLabel(interfaceSectionLabel, fluentLookAndFeel,
                                      true);
  FluentSettingsStyle::configureLabel(uiFontLabel, fluentLookAndFeel);
  FluentSettingsStyle::configureLabel(uiFontSizeLabel, fluentLookAndFeel);
  FluentSettingsStyle::configureLabel(playlistSectionLabel, fluentLookAndFeel,
                                      true);
  FluentSettingsStyle::configureLabel(playlistFontLabel, fluentLookAndFeel);
  FluentSettingsStyle::configureLabel(playlistFontSizeLabel, fluentLookAndFeel);
  FluentSettingsStyle::configureLabel(playlistRowSpacingLabel,
                                      fluentLookAndFeel);
  resized();
  repaint();
  if (auto *window = getTopLevelComponent())
    window->repaint();
}

void FontSettingsContent::resized() {
  auto area = getLocalBounds().reduced(FluentSettingsStyle::panelMargin);
  interfaceCardBounds = area.removeFromTop(172);
  area.removeFromTop(12);
  playlistCardBounds = area.removeFromTop(260);

  constexpr int labelWidth = 88;
  auto interfaceContent =
      interfaceCardBounds.reduced(FluentSettingsStyle::cardPadding);
  interfaceSectionLabel.setBounds(interfaceContent.removeFromTop(22));
  interfaceContent.removeFromTop(8);
  const int rowHeight = FluentSettingsStyle::controlHeight(fluentLookAndFeel);
  auto uiRow = interfaceContent.removeFromTop(rowHeight);
  uiFontLabel.setBounds(uiRow.removeFromLeft(labelWidth));
  uiFontCombo.setBounds(uiRow);
  interfaceContent.removeFromTop(FluentSettingsStyle::rowGap);
  auto uiSizeRow = interfaceContent.removeFromTop(rowHeight);
  uiFontSizeLabel.setBounds(uiSizeRow.removeFromLeft(labelWidth));
  uiFontSizeSlider.setBounds(uiSizeRow);

  auto playlistContent =
      playlistCardBounds.reduced(FluentSettingsStyle::cardPadding);
  playlistSectionLabel.setBounds(playlistContent.removeFromTop(22));
  playlistContent.removeFromTop(8);
  auto fontRow = playlistContent.removeFromTop(rowHeight);
  playlistFontLabel.setBounds(fontRow.removeFromLeft(labelWidth));
  playlistFontCombo.setBounds(fontRow);
  playlistContent.removeFromTop(FluentSettingsStyle::rowGap);
  auto sizeRow = playlistContent.removeFromTop(rowHeight);
  playlistFontSizeLabel.setBounds(sizeRow.removeFromLeft(labelWidth));
  playlistFontSizeSlider.setBounds(sizeRow);
  playlistContent.removeFromTop(FluentSettingsStyle::rowGap);
  playlistRowSpacingAutoToggle.setBounds(
      playlistContent.removeFromTop(rowHeight));
  playlistContent.removeFromTop(FluentSettingsStyle::rowGap);
  auto spacingRow = playlistContent.removeFromTop(rowHeight);
  playlistRowSpacingLabel.setBounds(spacingRow.removeFromLeft(labelWidth));
  playlistRowSpacingSlider.setBounds(spacingRow);
}
