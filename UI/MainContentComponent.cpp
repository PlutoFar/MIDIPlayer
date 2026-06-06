#include "MainContentComponent.h"

struct MainContentComponent::AudioSettingsContent : public juce::Component {
  explicit AudioSettingsContent(AudioEngine &audioEngine)
      : engine(audioEngine),
        selector(audioEngine.getDeviceManager(), 0, 2, 0, 2, true, true, true,
                 false) {
    setSize(520, 500);
    setOpaque(false);

    auto selectorBackground = juce::Colour(0xFF2D2D2D);
    auto text = juce::Colour(0xFFE0E0E0);
    selector.setColour(juce::ComboBox::backgroundColourId,
                       selectorBackground);
    selector.setColour(juce::ComboBox::textColourId, text);
    selector.setColour(juce::ComboBox::outlineColourId,
                       juce::Colour(0xFF4A4A4A));
    selector.setColour(juce::TextButton::buttonColourId, selectorBackground);
    selector.setColour(juce::TextButton::textColourOnId, text);
    selector.setColour(juce::TextButton::textColourOffId, text);
    selector.setColour(juce::Label::textColourId, text);
    selector.setColour(juce::ListBox::backgroundColourId,
                       selectorBackground);
    selector.setColour(juce::ListBox::textColourId, text);
    addAndMakeVisible(selector);
  }

  ~AudioSettingsContent() override { engine.saveAudioDeviceSettings(); }

  void paint(juce::Graphics &g) override {
    if (auto *fluentTheme =
            dynamic_cast<FluentLookAndFeel *>(&getLookAndFeel())) {
      g.fillAll(fluentTheme->getColors().cardBackground);
    } else {
      g.fillAll(juce::Colour(0xFF1F1F1F));
    }
  }

  void resized() override { selector.setBounds(getLocalBounds().reduced(10)); }

  AudioEngine &engine;
  juce::AudioDeviceSelectorComponent selector;
};

struct MainContentComponent::FontSettingsContent : public juce::Component {
  explicit FontSettingsContent(FluentLookAndFeel &lookAndFeel)
      : fluentLookAndFeel(lookAndFeel) {
    setSize(420, 360);
    setOpaque(false);
    availableFonts = juce::Font::findAllTypefaceNames();

    auto currentFont = getAppSettings().getPlaylistFontName();
    if (!availableFonts.contains(currentFont)) {
      if (availableFonts.contains("Microsoft YaHei UI")) {
        currentFont = "Microsoft YaHei UI";
      } else if (availableFonts.contains("Microsoft YaHei")) {
        currentFont = "Microsoft YaHei";
      }

      if (availableFonts.contains(currentFont)) {
        getAppSettings().setPlaylistFontName(currentFont);
        fluentLookAndFeel.setPlaylistFont(currentFont);
      }
    }

    addAndMakeVisible(uiFontLabel);
    uiFontLabel.setText(L"界面字体:", juce::dontSendNotification);
    uiFontLabel.setColour(juce::Label::textColourId, juce::Colours::white);

    addAndMakeVisible(uiFontCombo);
    populateFontCombo(uiFontCombo, getAppSettings().getUIFontName());
    uiFontCombo.onChange = [this]() {
      auto id = uiFontCombo.getSelectedId();
      if (id <= 0 || id > uiFontNames.size())
        return;

      auto fontName = uiFontNames[id - 1];
      getAppSettings().setUIFontName(fontName);
      fluentLookAndFeel.setUIFont(fontName);
      notifySettingsChanged();
    };

    addAndMakeVisible(playlistFontLabel);
    playlistFontLabel.setText(L"列表字体:", juce::dontSendNotification);
    playlistFontLabel.setColour(juce::Label::textColourId,
                                juce::Colours::white);

    addAndMakeVisible(playlistFontCombo);
    populateFontCombo(playlistFontCombo,
                      getAppSettings().getPlaylistFontName(), true);
    playlistFontCombo.onChange = [this]() {
      auto id = playlistFontCombo.getSelectedId();
      if (id <= 0 || id > playlistFontNames.size())
        return;

      auto fontName = playlistFontNames[id - 1];
      getAppSettings().setPlaylistFontName(fontName);
      getAppSettings().addRecentFont(fontName);
      fluentLookAndFeel.setPlaylistFont(fontName);
      notifySettingsChanged();
    };

    addAndMakeVisible(fontSizeLabel);
    fontSizeLabel.setText(L"列表字号:", juce::dontSendNotification);
    fontSizeLabel.setColour(juce::Label::textColourId, juce::Colours::white);

    addAndMakeVisible(fontSizeSlider);
    fontSizeSlider.setRange(12.0, 36.0, 1.0);
    fontSizeSlider.setValue(getAppSettings().getPlaylistFontSize());
    fontSizeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    fontSizeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    fontSizeSlider.onValueChange = [this]() {
      getAppSettings().setPlaylistFontSize(
          static_cast<float>(fontSizeSlider.getValue()));
      notifySettingsChanged();
    };

    addAndMakeVisible(infoLabel);
    infoLabel.setText(L"更改字体后可能需要重启应用才能完全生效",
                      juce::dontSendNotification);
    infoLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    infoLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
  }

  ~FontSettingsContent() override { getAppSettings().save(); }

  void paint(juce::Graphics &g) override {
    if (auto *fluentTheme =
            dynamic_cast<FluentLookAndFeel *>(&getLookAndFeel())) {
      g.fillAll(fluentTheme->getColors().cardBackground);
    } else {
      g.fillAll(juce::Colour(0xFF202020));
    }
  }

  void resized() override {
    auto area = getLocalBounds().reduced(20);
    constexpr int labelWidth = 80;
    constexpr int rowHeight = 40;
    constexpr int gap = 10;

    auto uiRow = area.removeFromTop(rowHeight);
    uiFontLabel.setBounds(uiRow.removeFromLeft(labelWidth));
    uiFontCombo.setBounds(uiRow.reduced(0, 5));
    area.removeFromTop(gap);

    auto playlistRow = area.removeFromTop(rowHeight);
    playlistFontLabel.setBounds(playlistRow.removeFromLeft(labelWidth));
    playlistFontCombo.setBounds(playlistRow.reduced(0, 5));
    area.removeFromTop(gap);

    auto sizeRow = area.removeFromTop(rowHeight);
    fontSizeLabel.setBounds(sizeRow.removeFromLeft(labelWidth));
    fontSizeSlider.setBounds(sizeRow.reduced(0, 5));
    area.removeFromTop(gap * 2);
    infoLabel.setBounds(area.removeFromTop(30));
  }

  void populateFontCombo(juce::ComboBox &combo,
                         const juce::String &currentFont,
                         bool isPlaylist = false) {
    combo.clear();
    auto &fontNames = isPlaylist ? playlistFontNames : uiFontNames;
    fontNames.clear();

    int itemId = 1;
    auto addFont = [&](const juce::String &fontName) {
      if (fontName.isEmpty() || fontNames.contains(fontName))
        return;

      combo.addItem(fontName, itemId);
      fontNames.add(fontName);
      if (fontName == currentFont)
        combo.setSelectedId(itemId, juce::dontSendNotification);
      ++itemId;
    };

    juce::StringArray topFonts;
    if (isPlaylist) {
      combo.addSectionHeading(L"常用 & 最近");
      auto addTopFont = [&](const juce::String &fontName) {
        if (availableFonts.contains(fontName) && !topFonts.contains(fontName)) {
          addFont(fontName);
          topFonts.add(fontName);
        }
      };

      for (const auto &fontName :
           juce::StringArray{"Microsoft YaHei UI", "SimHei", "SimSun"})
        addTopFont(fontName);

      for (const auto &fontName : getAppSettings().getRecentFonts())
        addTopFont(fontName);

      combo.addSeparator();
    }

    for (const auto &fontName : availableFonts)
      if (!topFonts.contains(fontName))
        addFont(fontName);
  }

  void notifySettingsChanged() {
    if (onSettingsChanged)
      onSettingsChanged();
  }

  std::function<void()> onSettingsChanged;
  FluentLookAndFeel &fluentLookAndFeel;
  juce::StringArray availableFonts;
  juce::StringArray uiFontNames;
  juce::StringArray playlistFontNames;
  juce::Label uiFontLabel;
  juce::Label playlistFontLabel;
  juce::Label fontSizeLabel;
  juce::Label infoLabel;
  juce::ComboBox uiFontCombo;
  juce::ComboBox playlistFontCombo;
  juce::Slider fontSizeSlider;
};

class MainContentComponent::PluginScanThread : public juce::Thread {
public:
  PluginScanThread(AudioEngine &audioEngine, MainContentComponent &component)
      : juce::Thread("VST3 plugin scan"), engine(audioEngine),
        owner(&component) {}

  void run() override {
    engine.scanPlugins([this]() { return threadShouldExit(); });

    juce::MessageManager::callAsync([safeOwner = owner]() {
      if (safeOwner == nullptr)
        return;

      safeOwner->isScanningPlugins = false;
      safeOwner->scanSpinner.setVisible(false);
      safeOwner->pluginSelector.setEnabled(true);
      safeOwner->updatePluginList();
      safeOwner->resized();
    });
  }

private:
  AudioEngine &engine;
  juce::Component::SafePointer<MainContentComponent> owner;
};

void MainContentComponent::PluginScanThreadDeleter::operator()(
    PluginScanThread *thread) const {
  delete thread;
}

MainContentComponent::~MainContentComponent() {
  stopTimer();

  if (pluginScanThread != nullptr) {
    pluginScanThread->signalThreadShouldExit();
    pluginScanThread->waitForThreadToExit(-1);
    pluginScanThread.reset();
  }

  saveSettings();
  engine.removeChangeListener(this);
  closePluginWindow();
  setLookAndFeel(nullptr);
}

void MainContentComponent::showPage(const juce::String &pageId,
                                    const juce::String &title) {
  currentPage = pageId;
  pageTitle.setText(title, juce::dontSendNotification);
  navigation.setSelectedItem(pageId);
  resized();
}

void MainContentComponent::layoutTransportBar(juce::Rectangle<int> area) {
  area = area.reduced(20, 8);

  progressSlider.setBounds(area.removeFromTop(24));
  area.removeFromTop(4);

  auto controlRow = area;
  int btnSize = 36;
  int playBtnSize = 44;

  auto volumeArea = controlRow.removeFromRight(180);
  loopModeBtn.setBounds(volumeArea.removeFromLeft(btnSize).reduced(2));
  volumeBtn.setBounds(volumeArea.removeFromLeft(btnSize).reduced(2));
  volumeSlider.setBounds(volumeArea.reduced(4, 4));

  int gap = 8;
  int controlsWidth = btnSize * 3 + playBtnSize + gap * 3;
  int minTrackWidth = 200;
  int centerPadding = 40;
  int availableForTrack =
      (controlRow.getWidth() - controlsWidth) / 2 - centerPadding;
  int trackInfoWidth = juce::jmax(minTrackWidth, availableForTrack);

  auto leftInfo = controlRow.removeFromLeft(trackInfoWidth);
  trackLabel.setBounds(leftInfo.removeFromTop(22));
  timeLabel.setBounds(leftInfo);

  int controlsHeight = playBtnSize;
  auto centerArea =
      controlRow.withSizeKeepingCentre(controlsWidth, controlsHeight);

  prevBtn.setBounds(centerArea.removeFromLeft(btnSize).withSizeKeepingCentre(
      btnSize, btnSize));
  centerArea.removeFromLeft(gap);
  playBtn.setBounds(centerArea.removeFromLeft(playBtnSize)
                        .withSizeKeepingCentre(playBtnSize, playBtnSize));
  centerArea.removeFromLeft(gap);
  nextBtn.setBounds(centerArea.removeFromLeft(btnSize).withSizeKeepingCentre(
      btnSize, btnSize));
  centerArea.removeFromLeft(gap);
  stopBtn.setBounds(centerArea.removeFromLeft(btnSize).withSizeKeepingCentre(
      btnSize, btnSize));
}

void MainContentComponent::startPluginScan() {
  if (isScanningPlugins)
    return;

  if (pluginScanThread != nullptr &&
      !pluginScanThread->isThreadRunning()) {
    pluginScanThread.reset();
  }

  isScanningPlugins = true;
  pluginSelector.setEnabled(false);
  scanSpinner.setVisible(true);
  resized();

  pluginScanThread.reset(new PluginScanThread(engine, *this));
  if (!pluginScanThread->startThread()) {
    pluginScanThread.reset();
    isScanningPlugins = false;
    scanSpinner.setVisible(false);
    pluginSelector.setEnabled(true);
    resized();
  }
}

void MainContentComponent::triggerSeekUpdate(double normalizedPos) {
  pendingSeekValue.store(normalizedPos);
  seekUpdater.triggerAsyncUpdate();
}

void MainContentComponent::confirmUnloadPlugin() {
  if (engine.getVst3Instance() == nullptr)
    return;

  juce::AlertWindow::showOkCancelBox(
      juce::AlertWindow::QuestionIcon, L"Unload plugin",
      L"Unload the current plugin?", L"Unload", L"Cancel", this,
      juce::ModalCallbackFunction::create(
          [safeThis = juce::Component::SafePointer<MainContentComponent>(
               this)](int result) {
            if (result != 0 && safeThis != nullptr)
              safeThis->unloadPlugin();
          }));
}

#if JUCE_WINDOWS
bool MainContentComponent::isFileAssociatedToSelf() {
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.mid", 0, KEY_READ,
                    &key) != ERROR_SUCCESS) {
    return false;
  }

  wchar_t value[256] = {};
  DWORD size = sizeof(value);
  DWORD type = 0;
  bool result = false;

  if (RegQueryValueExW(key, nullptr, nullptr, &type, (LPBYTE)value, &size) ==
      ERROR_SUCCESS) {
    if (juce::String(value) == "ModernMidiPlayer.MIDIFile") {
      RegCloseKey(key);
      key = nullptr;

      if (RegOpenKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Classes\\ModernMidiPlayer.MIDIFile"
                        L"\\shell\\open\\command",
                        0, KEY_READ, &key) == ERROR_SUCCESS) {
        wchar_t commandValue[1024] = {};
        DWORD commandSize = sizeof(commandValue);
        if (RegQueryValueExW(key, nullptr, nullptr, &type,
                             (LPBYTE)commandValue,
                             &commandSize) == ERROR_SUCCESS) {
          juce::String command(commandValue);
          auto exePath = juce::File::getSpecialLocation(
                             juce::File::currentExecutableFile)
                             .getFullPathName();
          result = command.containsIgnoreCase(exePath);
        }
      }
    }
  }

  if (key != nullptr)
    RegCloseKey(key);

  return result;
}

bool MainContentComponent::registerFileAssociation() {
  auto exePath =
      juce::File::getSpecialLocation(juce::File::currentExecutableFile)
          .getFullPathName();
  juce::String command = "\"" + exePath + "\" \"%1\"";

  auto setRegKey = [](const wchar_t *subKey,
                      const juce::String &value) -> bool {
    HKEY key = nullptr;
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subKey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key,
                        &disposition) != ERROR_SUCCESS) {
      return false;
    }

    auto wideValue = value.toWideCharPointer();
    auto byteLength =
        (DWORD)((wcslen(wideValue) + 1) * sizeof(wchar_t));
    bool success =
        RegSetValueExW(key, nullptr, 0, REG_SZ, (const BYTE *)wideValue,
                       byteLength) == ERROR_SUCCESS;
    RegCloseKey(key);
    return success;
  };

  bool ok = true;
  ok &= setRegKey(L"Software\\Classes\\.mid", "ModernMidiPlayer.MIDIFile");
  ok &= setRegKey(L"Software\\Classes\\.midi", "ModernMidiPlayer.MIDIFile");
  ok &= setRegKey(L"Software\\Classes\\ModernMidiPlayer.MIDIFile",
                  L"MIDI \u97F3\u4E50\u6587\u4EF6");
  ok &= setRegKey(L"Software\\Classes\\ModernMidiPlayer.MIDIFile"
                  L"\\shell\\open\\command",
                  command);
  ok &= setRegKey(L"Software\\Classes\\ModernMidiPlayer.MIDIFile"
                  L"\\DefaultIcon",
                  "\"" + exePath + "\",0");

  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return ok;
}

void MainContentComponent::removeFileAssociation() {
  auto deleteRegValue = [](const wchar_t *subKey) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_WRITE, &key) ==
        ERROR_SUCCESS) {
      RegDeleteValueW(key, nullptr);
      RegCloseKey(key);
    }
  };

  deleteRegValue(L"Software\\Classes\\.mid");
  deleteRegValue(L"Software\\Classes\\.midi");
  RegDeleteTreeW(HKEY_CURRENT_USER,
                 L"Software\\Classes\\ModernMidiPlayer.MIDIFile");
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
#endif

std::unique_ptr<juce::ToggleButton>
MainContentComponent::createDontShowAgainToggle() {
  auto toggle =
      std::make_unique<juce::ToggleButton>(L"\u4e0d\u518d\u63d0\u793a");
  toggle->setColour(juce::ToggleButton::textColourId, juce::Colours::white);
  toggle->setSize(200, 24);
  return toggle;
}

void MainContentComponent::showFileAssociationPrompt() {
#if JUCE_WINDOWS
  if (isFileAssociatedToSelf() ||
      getAppSettings().getDontShowFileAssocPrompt())
    return;

  auto *alertWindow = new juce::AlertWindow(
      L"\u6587\u4EF6\u5173\u8054",
      L"\u662F\u5426\u5C06 .mid \u548C .midi \u6587\u4EF6\u5173\u8054\u5230 "
      L"MIDI \u64AD\u653E\u5668\uFF1F\n\n"
      L"\u5173\u8054\u540E\uFF0C\u53CC\u51FB MIDI "
      L"\u6587\u4EF6\u5373\u53EF\u81EA\u52A8\u6253\u5F00\u672C\u5E94\u7528"
      L"\u8FDB\u884C\u64AD\u653E\u3002",
      juce::MessageBoxIconType::QuestionIcon);

  alertWindow->addButton(L"\u5173\u8054", 1);
  alertWindow->addButton(L"\u4e0d\u5173\u8054", 0);

  auto dontShowToggle = createDontShowAgainToggle();
  auto *togglePtr = dontShowToggle.get();
  alertWindow->addCustomComponent(dontShowToggle.release());

  auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
  alertWindow->enterModalState(
      true,
      juce::ModalCallbackFunction::create([safeThis, togglePtr](int result) {
        if (safeThis == nullptr)
          return;

        bool dontShowAgain =
            (togglePtr != nullptr) ? togglePtr->getToggleState() : false;

        if (result == 1) {
          if (safeThis->registerFileAssociation()) {
            getAppSettings().setFileAssociated(true);
            getAppSettings().setDontShowFileAssocPrompt(true);
            getAppSettings().save();
          }
        } else if (dontShowAgain) {
          getAppSettings().setDontShowFileAssocPrompt(true);
          getAppSettings().save();
        }
      }),
      true);
#endif
}

void MainContentComponent::onAccentColorChanged(juce::Colour newColor) {
  getAppSettings().setThemeAccentColor(newColor.toString());
  fluentLookAndFeel.updateAccentColor(newColor);

  navigation.repaint();
  playlistPanel.repaint();
  repaint();

  if (newColor == background.getTargetAccentColor())
    sendLookAndFeelChange();
}

void MainContentComponent::openMidiFileFromShell(const juce::File &file) {
  if (!file.existsAsFile())
    return;

  auto extension = file.getFileExtension().toLowerCase();
  if (extension != ".mid" && extension != ".midi")
    return;

  showPage("playlist", L"\u97F3\u4E50\u5217\u8868");
  bool hasExistingPlaylist = playlist.size() > 0;

  if (hasExistingPlaylist) {
    if (playlist.contains(file)) {
      int index = playlist.findTrackIndex(file);
      if (index >= 0)
        setCurrentTrackIndex(index);
    } else {
      playlist.addFile(file);
      playlistPanel.refresh();
      setCurrentTrackIndex(playlist.size() - 1);
    }
  } else {
    playlist.clear();
    currentPlaylistFile = juce::File();
    setCurrentTrackIndex(-1);
    playlist.addFile(file);
    playlistPanel.refresh();
    setCurrentTrackIndex(0);
  }

  if (loadMidiFile(file)) {
    if (engine.getVst3Instance() != nullptr) {
      runLater(150, [](MainContentComponent &self) {
        self.engine.getMidiPlayer().setPlaying(true);
      });
    } else {
      tryLoadLastPluginWithDialog();
    }
  }

  pendingShellOpen = false;
  getAppSettings().setLastMidiDirectory(
      file.getParentDirectory().getFullPathName());
}

void MainContentComponent::setPendingShellOpen(bool pending) {
  pendingShellOpen = pending;
}

void MainContentComponent::runLater(
    int delayMs, std::function<void(MainContentComponent &)> fn) {
  auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
  juce::Timer::callAfterDelay(delayMs,
                              [safeThis, fn = std::move(fn)]() mutable {
                                if (safeThis != nullptr)
                                  fn(*safeThis);
                              });
}

void MainContentComponent::runAsync(
    std::function<void(MainContentComponent &)> fn) {
  auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
  juce::MessageManager::callAsync([safeThis, fn = std::move(fn)]() mutable {
    if (safeThis != nullptr)
      fn(*safeThis);
  });
}

void MainContentComponent::setupIconButton(juce::Button &btn,
                                           const juce::String &,
                                           const juce::String &tooltip) {
  addAndMakeVisible(btn);
  btn.addListener(this);
  btn.setTooltip(tooltip);
}

void MainContentComponent::drawIconButton(juce::Graphics &g,
                                          juce::Button &btn,
                                          const juce::String &icon) {
  if (!btn.isVisible())
    return;

  auto bounds = btn.getBounds().toFloat();
  auto &colors = fluentLookAndFeel.getColors();

  if (btn.isEnabled() && (btn.isMouseOver() || btn.isMouseButtonDown())) {
    g.setColour(btn.isMouseButtonDown() ? colors.controlPressed
                                        : colors.controlHover);
    g.fillRoundedRectangle(bounds.reduced(2.0f), 6.0f);
  }

  g.setFont(fluentLookAndFeel.getIconFont(16.0f));
  g.setColour(btn.isEnabled() ? colors.textPrimary
                              : colors.textSecondary.withAlpha(0.5f));
  g.drawText(icon, btn.getBounds(), juce::Justification::centred, false);
}

void MainContentComponent::drawIconButtonCombined(
    juce::Graphics &g, juce::Button &btn, const juce::String &mainIcon,
    const juce::String &subIcon) {
  if (!btn.isVisible())
    return;

  auto bounds = btn.getBounds().toFloat();
  auto &colors = fluentLookAndFeel.getColors();

  if (btn.isEnabled() && (btn.isMouseOver() || btn.isMouseButtonDown())) {
    g.setColour(btn.isMouseButtonDown() ? colors.controlPressed
                                        : colors.controlHover);
    g.fillRoundedRectangle(bounds.reduced(2.0f), 6.0f);
  }

  auto iconColor = btn.isEnabled() ? colors.textPrimary
                                   : colors.textSecondary.withAlpha(0.5f);
  g.setColour(iconColor);

  g.setFont(fluentLookAndFeel.getIconFont(16.0f));
  auto mainArea = btn.getBounds().translated(-2, -1);
  g.drawText(mainIcon, mainArea, juce::Justification::centred, false);

  g.setFont(fluentLookAndFeel.getIconFont(10.0f));
  auto subArea = btn.getBounds().translated(6, 6);
  g.drawText(subIcon, subArea, juce::Justification::centred, false);
}

void MainContentComponent::drawSequentialIcon(juce::Graphics &g,
                                              juce::Button &btn) {
  if (!btn.isVisible())
    return;

  auto bounds = btn.getBounds().toFloat();
  auto &colors = fluentLookAndFeel.getColors();

  if (btn.isEnabled() && (btn.isMouseOver() || btn.isMouseButtonDown())) {
    g.setColour(btn.isMouseButtonDown() ? colors.controlPressed
                                        : colors.controlHover);
    g.fillRoundedRectangle(bounds.reduced(2.0f), 6.0f);
  }

  auto iconColor = btn.isEnabled() ? colors.textPrimary
                                   : colors.textSecondary.withAlpha(0.5f);
  g.setColour(iconColor);

  if (getAppSettings().getSequentialIconListStyle()) {
    g.setFont(fluentLookAndFeel.getIconFont(16.0f));
    g.drawText(L"\uEA42", btn.getBounds(), juce::Justification::centred,
               false);
  } else {
    g.setFont(fluentLookAndFeel.getIconFont(9.0f));
    auto b = btn.getBounds();
    g.drawText(L"\uEBE7", b.translated(0, -8), juce::Justification::centred,
               false);
    g.drawText(L"\uEBE7", b.translated(0, 8), juce::Justification::centred,
               false);
  }
}

void MainContentComponent::drawPlayButton(juce::Graphics &g,
                                          juce::Button &btn,
                                          bool isPlaying) {
  auto bounds = btn.getBounds().toFloat().reduced(4.0f);
  auto &colors = fluentLookAndFeel.getColors();
  bool isEnabled = btn.isEnabled();

  juce::ColourGradient gradient(colors.accentLight, bounds.getTopLeft(),
                                colors.accentPrimary, bounds.getBottomRight(),
                                false);
  g.setGradientFill(gradient);
  g.drawEllipse(bounds, 2.5f);

  if (isEnabled && (btn.isMouseOver() || btn.isMouseButtonDown())) {
    g.setColour(colors.accentPrimary.withAlpha(
        btn.isMouseButtonDown() ? 0.25f : 0.15f));
    g.fillEllipse(bounds.reduced(3.0f));
  }

  g.setFont(fluentLookAndFeel.getIconFont(18.0f));
  g.setColour(isEnabled ? colors.textPrimary
                        : colors.textSecondary.withAlpha(0.4f));
  g.drawText(isPlaying ? L"\uE769" : L"\uE768", btn.getBounds(),
             juce::Justification::centred, false);

  if (!isEnabled) {
    g.setColour(colors.background.withAlpha(0.3f));
    g.drawEllipse(bounds, 2.5f);
  }
}

void MainContentComponent::toggleMute() {
  isMuted = !isMuted;
  if (isMuted) {
    volumeBeforeMute = volumeSlider.getValue();
    volumeSlider.setValue(0.0, juce::sendNotification);
  } else {
    volumeSlider.setValue(volumeBeforeMute, juce::sendNotification);
  }
  repaint();
}

void MainContentComponent::toggleLoopMode() {
  auto current = playlist.getPlaybackMode();
  auto next = static_cast<PlaylistManager::PlaybackMode>(
      (static_cast<int>(current) % 4) + 1);
  playlist.setPlaybackMode(next);
  getAppSettings().setPlayMode(static_cast<int>(next));

  juce::String tooltip;
  juce::String toastText;
  switch (next) {
  case PlaylistManager::PlaybackMode::Sequential:
    tooltip = L"播放模式: 连续播放";
    toastText = L"连续播放";
    break;
  case PlaylistManager::PlaybackMode::LoopList:
    tooltip = L"播放模式: 列表循环";
    toastText = L"列表循环";
    break;
  case PlaylistManager::PlaybackMode::LoopSingle:
    tooltip = L"播放模式: 单曲循环";
    toastText = L"单曲循环";
    break;
  case PlaylistManager::PlaybackMode::Shuffle:
    tooltip = L"播放模式: 随机播放";
    toastText = L"随机播放";
    break;
  }

  loopModeBtn.setTooltip(tooltip);
  embeddedTooltip.hideTooltip();
  modeToast.show(toastText, loopModeBtn.getBounds());
  playbackModeAnimationScale = 0.8f;
  repaint();
}

void MainContentComponent::updateLoopButtonTooltip() {
  juce::String tooltip;
  switch (playlist.getPlaybackMode()) {
  case PlaylistManager::PlaybackMode::Sequential:
    tooltip = L"播放模式: 连续播放";
    break;
  case PlaylistManager::PlaybackMode::LoopList:
    tooltip = L"播放模式: 列表循环";
    break;
  case PlaylistManager::PlaybackMode::LoopSingle:
    tooltip = L"播放模式: 单曲循环";
    break;
  case PlaylistManager::PlaybackMode::Shuffle:
    tooltip = L"播放模式: 随机播放";
    break;
  }
  loopModeBtn.setTooltip(tooltip);
}

void MainContentComponent::playlistTrackDoubleClicked(int index) {
  if (engine.getVst3Instance() == nullptr) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon, L"无法播放",
        L"请先加载一个乐器插件以开始播放。");
    return;
  }

  isHandlingTrackEnd = false;
  engine.getMidiPlayer().setPlaying(false);
  setCurrentTrackIndex(index);

  if (const auto *track = playlist.getTrack(index)) {
    if (loadMidiFile(track->file)) {
      ++trackSwitchGeneration;
      int generation = trackSwitchGeneration;
      runLater(100, [generation](MainContentComponent &self) {
        if (self.trackSwitchGeneration == generation)
          self.engine.getMidiPlayer().setPlaying(true);
      });
    }
  }
}

void MainContentComponent::playlistFilesDropped(
    const juce::StringArray &files) {
  juce::StringArray newFiles;
  juce::StringArray duplicateFiles;
  juce::File currentPlayingFile;

  if (const auto *track = playlist.getTrack(currentTrackIndex))
    currentPlayingFile = track->file;

  for (const auto &path : files) {
    auto file = juce::File(path);
    (playlist.contains(file) ? duplicateFiles : newFiles).add(path);
  }

  if (duplicateFiles.isEmpty()) {
    applyDroppedPlaylistFiles(newFiles, duplicateFiles, currentPlayingFile,
                              false);
    return;
  }

  auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
  juce::AlertWindow::showYesNoCancelBox(
      juce::AlertWindow::QuestionIcon, L"发现重复文件",
      L"检测到 " + juce::String(duplicateFiles.size()) +
          L" 个文件已在列表中。\n\n"
          L"「仅保存新的」= 跳过重复，只添加新文件\n"
          L"「保存并覆盖」= 添加新文件，并刷新已有条目",
      L"仅保存新的", L"保存并覆盖", L"取消", this,
      juce::ModalCallbackFunction::create(
          [safeThis, newFiles, duplicateFiles, currentPlayingFile](int result) {
            if (safeThis == nullptr || result == 0)
              return;

            safeThis->applyDroppedPlaylistFiles(
                newFiles, duplicateFiles, currentPlayingFile, result == 2);
          }));
}

void MainContentComponent::applyDroppedPlaylistFiles(
    const juce::StringArray &newFiles,
    const juce::StringArray &duplicateFiles,
    const juce::File &currentPlayingFile, bool overwriteDuplicates) {
  bool changed = false;
  for (const auto &path : newFiles)
    changed = playlist.addFile(juce::File(path), false) || changed;

  std::vector<int> overwrittenRows;
  if (overwriteDuplicates) {
    for (const auto &path : duplicateFiles) {
      int index = playlist.findTrackIndex(juce::File(path));
      if (index >= 0) {
        playlist.refreshTrack(index);
        overwrittenRows.push_back(index);
        changed = true;
      }
    }
  }

  if (!changed)
    return;

  if (currentPlayingFile != juce::File()) {
    int newIndex = playlist.findTrackIndex(currentPlayingFile);
    if (newIndex != -1 && newIndex != currentTrackIndex)
      setCurrentTrackIndex(newIndex);
  }

  playlistPanel.refresh();
  if (!overwrittenRows.empty())
    playlistPanel.startDropAnimation(overwrittenRows, false);
}

void MainContentComponent::tryLoadLastPluginWithDialog() {
  if (!engine.hasAudioDevice()) {
    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::WarningIcon, L"\u97F3\u9891\u8BBE\u5907\u4E0D\u53EF\u7528",
        L"\u672A\u68C0\u6D4B\u5230\u53EF\u7528\u7684\u97F3\u9891\u8F93\u51FA"
        L"\u8BBE\u5907\uFF0C\u65E0\u6CD5\u52A0\u8F7D\u4E50\u5668\u63D2\u4EF6\u3002"
        L"\n\n\u8BF7\u5148\u5728\u97F3\u9891\u8BBE\u7F6E\u4E2D\u914D\u7F6E"
        L"\u8F93\u51FA\u8BBE\u5907\u3002",
        L"\u6253\u5F00\u8BBE\u7F6E", L"\u53D6\u6D88", nullptr,
        juce::ModalCallbackFunction::create(
            [safeThis = juce::Component::SafePointer<MainContentComponent>(
                 this)](int result) {
              if (result == 1 && safeThis != nullptr)
                safeThis->showAudioSettings();
            }));
    return;
  }

  auto lastPluginId = getAppSettings().getLastPluginId();
  if (lastPluginId.isEmpty()) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon, L"\u65E0\u6CD5\u64AD\u653E",
        L"\u8BF7\u5148\u52A0\u8F7D\u4E00\u4E2A\u4E50\u5668\u63D2\u4EF6\u4EE5"
        L"\u5F00\u59CB\u64AD\u653E\u3002");
    return;
  }

  auto &list = engine.getPluginList();
  int pluginIndex = -1;
  for (int i = 0; i < list.getNumTypes(); ++i) {
    if (list.getTypes()[i].createIdentifierString() == lastPluginId) {
      pluginIndex = i;
      break;
    }
  }

  if (pluginIndex < 0) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon, L"\u65E0\u6CD5\u64AD\u653E",
        L"\u4E0A\u6B21\u4F7F\u7528\u7684\u4E50\u5668\u63D2\u4EF6\u672A\u627E"
        L"\u5230\uFF0C\u8BF7\u624B\u52A8\u52A0\u8F7D\u4E00\u4E2A\u4E50\u5668"
        L"\u63D2\u4EF6\u3002");
    return;
  }

  auto pluginName = list.getTypes()[pluginIndex].name;
  auto *loadingWindow = new juce::AlertWindow(
      L"\u6B63\u5728\u52A0\u8F7D\u4E50\u5668",
      L"\u6B63\u5728\u52A0\u8F7D\u63D2\u4EF6: " + pluginName +
          L"\n\u8BF7\u7A0D\u5019...",
      juce::MessageBoxIconType::InfoIcon);
  auto safeLoadingWindow =
      juce::Component::SafePointer<juce::AlertWindow>(loadingWindow);
  loadingWindow->enterModalState(false, nullptr, true);

  auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
  juce::Timer::callAfterDelay(100, [safeThis, safeLoadingWindow, pluginIndex]() {
    if (safeThis == nullptr) {
      if (safeLoadingWindow != nullptr)
        safeLoadingWindow->exitModalState(0);
      return;
    }

    auto &pluginList = safeThis->engine.getPluginList();
    if (pluginIndex >= pluginList.getNumTypes()) {
      if (safeLoadingWindow != nullptr)
        safeLoadingWindow->exitModalState(0);
      return;
    }

    auto description = pluginList.getTypes()[pluginIndex];
    safeThis->pluginSelector.setSelectedId(pluginIndex + 1,
                                           juce::dontSendNotification);

    if (safeThis->engine.loadPlugin(description)) {
      safeThis->openPluginBtn.setEnabled(true);
      safeThis->unloadBtn.setEnabled(true);
      safeThis->contentLabel.setText(L"\u5DF2\u52A0\u8F7D: " + description.name,
                                     juce::dontSendNotification);
      safeThis->openPluginWindow();
    }

    if (safeLoadingWindow != nullptr)
      safeLoadingWindow->exitModalState(0);
  });
}

void MainContentComponent::showAudioSettings() {
  auto *content = new AudioSettingsContent(engine);

  juce::DialogWindow::LaunchOptions options;
  options.content.setOwned(content);
  options.dialogTitle = L"\u97F3\u9891\u8BBE\u7F6E";
  options.dialogBackgroundColour = fluentLookAndFeel.getColors().cardBackground;
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = true;
  options.resizable = true;
  options.launchAsync();
}

void MainContentComponent::showBackgroundSettings() {
  auto *content = new BackgroundSettingsDialog(background, this);
  content->setLookAndFeel(&fluentLookAndFeel);

  juce::DialogWindow::LaunchOptions options;
  options.content.setOwned(content);
  options.dialogTitle = L"\u80CC\u666F\u8BBE\u7F6E";
  options.dialogBackgroundColour = juce::Colours::transparentBlack;
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = false;
  options.resizable = false;
  options.launchAsync();
}

void MainContentComponent::showFontSettings() {
  auto *content = new FontSettingsContent(fluentLookAndFeel);
  content->onSettingsChanged =
      [safeThis = juce::Component::SafePointer<MainContentComponent>(this)]() {
        if (safeThis == nullptr)
          return;

        safeThis->playlistPanel.refresh();
        safeThis->playlistPanel.repaint();
      };

  juce::DialogWindow::LaunchOptions options;
  options.content.setOwned(content);
  options.dialogTitle = L"\u5B57\u4F53\u8BBE\u7F6E";
  options.dialogBackgroundColour = juce::Colours::transparentBlack;
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = false;
  options.resizable = false;
  options.launchAsync();
}

void MainContentComponent::updatePluginList() {
  juce::String idToRestore;
  if (engine.getVst3Instance() != nullptr)
    idToRestore = getAppSettings().getLastPluginId();

  pluginSelector.clear(juce::dontSendNotification);
  auto &list = engine.getPluginList();

  int idToSelect = 0;
  for (int i = 0; i < list.getNumTypes(); ++i) {
    auto description = list.getTypes()[i];
    pluginSelector.addItem(description.name, i + 1);

    if (idToRestore.isNotEmpty() &&
        description.createIdentifierString() == idToRestore) {
      idToSelect = i + 1;
    }
  }

  if (idToSelect > 0)
    pluginSelector.setSelectedId(idToSelect, juce::dontSendNotification);
}

void MainContentComponent::tryLoadLastPlugin() {
  auto lastPluginId = getAppSettings().getLastPluginId();
  if (lastPluginId.isEmpty())
    return;

  auto &list = engine.getPluginList();
  for (int i = 0; i < list.getNumTypes(); ++i) {
    if (list.getTypes()[i].createIdentifierString() == lastPluginId) {
      pluginSelector.setSelectedId(i + 1, juce::dontSendNotification);
      if (engine.loadPlugin(list.getTypes()[i])) {
        openPluginBtn.setEnabled(true);
        contentLabel.setText(L"\u5DF2\u52A0\u8F7D: " + list.getTypes()[i].name,
                             juce::dontSendNotification);
      }
      return;
    }
  }
}

juce::String MainContentComponent::formatTime(int seconds) {
  return juce::String(seconds / 60) + ":" +
         juce::String(seconds % 60).paddedLeft('0', 2);
}

void MainContentComponent::loadSettings() {
  auto &settings = getAppSettings();
  volumeSlider.setValue(settings.getMasterVolume(),
                        juce::dontSendNotification);
  engine.setMasterVolume(settings.getMasterVolume());

  int savedMode = settings.getPlayMode();
  playlist.setPlaybackMode(
      static_cast<PlaylistManager::PlaybackMode>(savedMode));

  fluentLookAndFeel.setUIFont(settings.getUIFontName());
  fluentLookAndFeel.setPlaylistFont(settings.getPlaylistFontName());
  playlistPanel.refresh();
}

void MainContentComponent::saveSettings() {
  getAppSettings().setMasterVolume((float)volumeSlider.getValue());
  getAppSettings().setPlayMode(static_cast<int>(playlist.getPlaybackMode()));
  getAppSettings().save();
}

void MainContentComponent::savePlaylist(
    std::function<void(bool)> completion) {
  if (currentPlaylistFile.existsAsFile()) {
    if (playlist.save(currentPlaylistFile)) {
      getAppSettings().setLastPlaylistPath(
          currentPlaylistFile.getFullPathName());
      if (completion)
        completion(true);
      return;
    }

    if (completion)
      completion(false);
    return;
  }

  savePlaylistAs(std::move(completion));
}

void MainContentComponent::savePlaylistAs(
    std::function<void(bool)> completion) {
  fileChooser = std::make_unique<juce::FileChooser>(
      L"Save playlist",
      juce::File(getAppSettings().getLastMidiDirectory())
          .getChildFile("playlist.json"),
      "*.json");

  auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
  fileChooser->launchAsync(
      juce::FileBrowserComponent::saveMode |
          juce::FileBrowserComponent::canSelectFiles |
          juce::FileBrowserComponent::warnAboutOverwriting,
      [safeThis, completion = std::move(completion)](
          const juce::FileChooser &chooser) mutable {
        if (safeThis == nullptr) {
          if (completion)
            completion(false);
          return;
        }

        auto result = chooser.getResult();
        if (result == juce::File()) {
          if (completion)
            completion(false);
          return;
        }

        if (!result.hasFileExtension(".json"))
          result = result.withFileExtension(".json");

        safeThis->currentPlaylistFile = result;
        const bool saved =
            safeThis->playlist.save(safeThis->currentPlaylistFile);
        if (saved) {
          getAppSettings().setLastPlaylistPath(
              safeThis->currentPlaylistFile.getFullPathName());
        }
        if (completion)
          completion(saved);
      });
}

void MainContentComponent::setCurrentTrackIndex(int index) {
  currentTrackIndex = index;
  if (index >= 0)
    playlistPanel.setCurrentTrackIndex(index);
  else
    playlistPanel.deselectAllRows();
}

void MainContentComponent::unloadPlugin() {
  closePluginWindow();
  engine.unloadPlugin();
  pluginSelector.setSelectedId(0, juce::dontSendNotification);
  openPluginBtn.setEnabled(false);
  unloadBtn.setEnabled(false);
  contentLabel.setText(L"Select a VST3 instrument plugin to begin.",
                       juce::dontSendNotification);

  progressSlider.setValue(0.0, juce::dontSendNotification);
  progressSlider.setEnabled(false);
  timeLabel.setText("0:00 / 0:00", juce::dontSendNotification);
}

void MainContentComponent::loadSelectedPlugin() {
  if (isScanningPlugins)
    return;

  int selectedIndex = pluginSelector.getSelectedItemIndex();
  if (selectedIndex < 0 ||
      selectedIndex >= engine.getPluginList().getNumTypes())
    return;

  auto description = engine.getPluginList().getTypes()[selectedIndex];
  auto *loadingWindow = new juce::AlertWindow(
      L"Loading instrument",
      L"Loading plugin: " + description.name + L"\nPlease wait...",
      juce::MessageBoxIconType::InfoIcon);
  auto safeLoadingWindow =
      juce::Component::SafePointer<juce::AlertWindow>(loadingWindow);
  loadingWindow->enterModalState(false, nullptr, true);

  auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
  juce::Timer::callAfterDelay(
      10, [safeThis, safeLoadingWindow, description]() {
        if (safeThis == nullptr) {
          if (safeLoadingWindow != nullptr)
            safeLoadingWindow->exitModalState(0);
          return;
        }

        if (safeThis->engine.loadPlugin(description)) {
          safeThis->openPluginBtn.setEnabled(true);
          safeThis->unloadBtn.setEnabled(true);
          safeThis->contentLabel.setText(
              L"Loaded: " + description.name, juce::dontSendNotification);
          getAppSettings().setLastPluginId(
              description.createIdentifierString());
          safeThis->openPluginWindow();
        } else {
          juce::AlertWindow::showMessageBoxAsync(
              juce::AlertWindow::WarningIcon, L"Plugin load failed",
              safeThis->engine.getLastPluginError());
        }

        if (safeLoadingWindow != nullptr)
          safeLoadingWindow->exitModalState(0);
      });
}

void MainContentComponent::openPluginWindow() {
  auto *instance = engine.getVst3Instance();
  if (instance == nullptr)
    return;

  closePluginWindow();

  auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
  juce::Timer::callAfterDelay(300, [safeThis]() {
    if (safeThis == nullptr)
      return;

    auto *pluginInstance = safeThis->engine.getVst3Instance();
    if (pluginInstance == nullptr)
      return;

    try {
      auto *editor = pluginInstance->createEditor();
      if (editor != nullptr) {
        auto editorBounds = editor->getBounds();
        int width = editorBounds.getWidth();
        int height = editorBounds.getHeight();
        if (width < 100)
          width = 800;
        if (height < 100)
          height = 600;

        safeThis->pluginWindow = std::make_unique<PluginWindow>(
            pluginInstance->getName(), editor, width, height);
      }
    } catch (...) {
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::WarningIcon, L"Plugin window failed",
          L"Could not create the plugin editor window. Try reloading the "
          L"plugin.");
    }
  });
}

void MainContentComponent::closePluginWindow() { pluginWindow.reset(); }

void MainContentComponent::togglePlayPause() {
  if (!engine.getMidiPlayer().hasSequence() || engine.getVst3Instance() == nullptr)
    return;

  pendingResumePlayback = false;
  engine.getMidiPlayer().setPlaying(!engine.getMidiPlayer().getPlaying());
}

void MainContentComponent::stopPlayback() {
  pendingResumePlayback = false;
  engine.getMidiPlayer().setPlaying(false);
  engine.getMidiPlayer().seekTo(0);
}

void MainContentComponent::playNextTrack() {
  if (playlist.isEmpty() || engine.getVst3Instance() == nullptr)
    return;

  isHandlingTrackEnd = false;
  engine.getMidiPlayer().setPlaying(false);

  setCurrentTrackIndex(playlist.getNextIndex(currentTrackIndex));
  if (currentTrackIndex == -1) {
    stopPlayback();
    return;
  }

  if (const auto *track = playlist.getTrack(currentTrackIndex)) {
    if (loadMidiFile(track->file)) {
      setCurrentTrackIndex(currentTrackIndex);
      ++trackSwitchGeneration;
      int generation = trackSwitchGeneration;
      runLater(100, [generation](MainContentComponent &self) {
        if (self.trackSwitchGeneration != generation)
          return;

        self.engine.getMidiPlayer().setPlaying(true);
      });
    }
  }
}

void MainContentComponent::playPreviousTrack() {
  if (playlist.isEmpty() || engine.getVst3Instance() == nullptr)
    return;

  isHandlingTrackEnd = false;
  engine.getMidiPlayer().setPlaying(false);

  setCurrentTrackIndex(playlist.getPreviousIndex(currentTrackIndex));
  if (currentTrackIndex == -1) {
    stopPlayback();
    return;
  }

  if (const auto *track = playlist.getTrack(currentTrackIndex)) {
    if (loadMidiFile(track->file)) {
      setCurrentTrackIndex(currentTrackIndex);
      ++trackSwitchGeneration;
      int generation = trackSwitchGeneration;
      runLater(100, [generation](MainContentComponent &self) {
        if (self.trackSwitchGeneration != generation)
          return;

        self.engine.getMidiPlayer().setPlaying(true);
      });
    }
  }
}

void MainContentComponent::handleTrackEnd() {
  if (isHandlingTrackEnd)
    return;

  isHandlingTrackEnd = true;
  ++trackSwitchGeneration;
  int generation = trackSwitchGeneration;
  engine.getMidiPlayer().setPlaying(false);
  engine.getMidiPlayer().seekTo(0);

  runAsync([generation](MainContentComponent &self) {
    if (self.trackSwitchGeneration != generation) {
      self.isHandlingTrackEnd = false;
      return;
    }

    int nextIndex = self.playlist.getNextIndex(self.currentTrackIndex);
    if (nextIndex != -1) {
      self.setCurrentTrackIndex(nextIndex);
      if (const auto *track = self.playlist.getTrack(self.currentTrackIndex)) {
        if (self.loadMidiFile(track->file)) {
          self.runLater(100, [generation](MainContentComponent &delayedSelf) {
            if (delayedSelf.trackSwitchGeneration != generation)
              return;

            delayedSelf.engine.getMidiPlayer().setPlaying(true);
            delayedSelf.isHandlingTrackEnd = false;
          });
          return;
        }
      }
    }

    self.isHandlingTrackEnd = false;
  });
}

bool MainContentComponent::loadMidiFile(const juce::File &file) {
  if (!file.existsAsFile())
    return false;

  juce::MidiFile midiFile;
  auto stream = file.createInputStream();
  if (stream == nullptr || !midiFile.readFrom(*stream))
    return false;

  double sampleRate =
      engine.getSampleRate() > 0 ? engine.getSampleRate() : 44100.0;
  midiFile.convertTimestampTicksToSeconds();

  auto sequence = std::make_shared<juce::MidiMessageSequence>();
  for (int trackIndex = 0; trackIndex < midiFile.getNumTracks(); ++trackIndex) {
    if (auto *track = midiFile.getTrack(trackIndex)) {
      for (int eventIndex = 0; eventIndex < track->getNumEvents();
           ++eventIndex) {
        auto message = track->getEventPointer(eventIndex)->message;
        message.setTimeStamp(message.getTimeStamp() * sampleRate);
        sequence->addEvent(message);
      }
    }
  }

  sequence->updateMatchedPairs();
  sequence->sort();
  engine.getMidiPlayer().setSequence(std::move(sequence), sampleRate);
  trackLabel.setText(file.getFileNameWithoutExtension(),
                     juce::dontSendNotification);
  return true;
}

void MainContentComponent::showOpenFileDialog() {
  fileChooser = std::make_unique<juce::FileChooser>(
      L"Open MIDI file", juce::File(getAppSettings().getLastMidiDirectory()),
      "*.mid;*.midi");

  fileChooser->launchAsync(
      juce::FileBrowserComponent::openMode,
      [safeThis = juce::Component::SafePointer<MainContentComponent>(this)](
          const juce::FileChooser &chooser) {
        if (safeThis == nullptr)
          return;

        auto result = chooser.getResult();
        if (result.existsAsFile()) {
          getAppSettings().setLastMidiDirectory(
              result.getParentDirectory().getFullPathName());
          if (safeThis->loadMidiFile(result))
            safeThis->engine.getMidiPlayer().setPlaying(true);
        }
      });
}
