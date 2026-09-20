#include "MainContentComponent.h"

MainContentComponent::MainContentComponent(
    midi::Core &c, FluentLookAndFeel &applicationLookAndFeel)
    : core(c), fluentLookAndFeel(applicationLookAndFeel),
      navigation(fluentLookAndFeel), playlistPanel(core),
      progressTimeTooltip(fluentLookAndFeel),
      tooltipOverlay(fluentLookAndFeel) {
  setLookAndFeel(&fluentLookAndFeel);
  setWantsKeyboardFocus(true);
  loadSettings();

  auto xml = juce::XmlDocument::parse(LegacyIconAssets::sequentialPlaybackSvg);
  if (xml != nullptr) {
    sequentialIconDrawable = juce::Drawable::createFromSVG(*xml);
  }

  addAndMakeVisible(background);
  background.toBack();
  background.onAccentColorChanged =
      [safeThis = juce::Component::SafePointer<MainContentComponent>(this)](
          juce::Colour c) {
        if (safeThis != nullptr)
          safeThis->onAccentColorChanged(c);
      };

  addAndMakeVisible(navigation);
  navigation.setListener(this);

  addAndMakeVisible(pageTitle);
  pageTitle.setText(L"乐器库", juce::dontSendNotification);
  pageTitle.setFont(fluentLookAndFeel.getTitleFont());
  pageTitle.setColour(juce::Label::textColourId,
                      fluentLookAndFeel.getColors().textPrimary);

  addAndMakeVisible(pluginSelector);
  pluginSelector.setTextWhenNothingSelected(L"选择乐器...");
  pluginSelector.addListener(this);
  pluginSelector.setTooltip(L"选择 VST3 插件");

  addAndMakeVisible(scanBtn);
  scanBtn.addListener(this);
  scanBtn.setTooltip(L"扫描系统中的 VST3 插件");

  addAndMakeVisible(unloadBtn);
  unloadBtn.addListener(this);
  unloadBtn.setTooltip(L"卸载插件");

  addAndMakeVisible(openPluginBtn);
  openPluginBtn.addListener(this);
  openPluginBtn.setEnabled(false);
  openPluginBtn.setTooltip(L"在新窗口中打开插件界面");

  unloadBtn.setEnabled(false);

  addAndMakeVisible(contentLabel);
  contentLabel.setFont(fluentLookAndFeel.getBodyLargeFont());
  contentLabel.setColour(juce::Label::textColourId,
                         fluentLookAndFeel.getColors().textSecondary);
  contentLabel.setInterceptsMouseClicks(false, false);
  contentLabel.setJustificationType(juce::Justification::centred);
  contentLabel.setText(L"选择一个 VST3 乐器插件开始演奏",
                       juce::dontSendNotification);

  addChildComponent(playlistPanel);
  playlistPanel.setListener(this);

  addAndMakeVisible(transportBar);
  transportBar.setInterceptsMouseClicks(false, true);

  addAndMakeVisible(trackLabel);
  trackLabel.setFont(fluentLookAndFeel.getBodyFont(true));
  trackLabel.setColour(juce::Label::textColourId,
                       fluentLookAndFeel.getColors().textPrimary);
  trackLabel.setInterceptsMouseClicks(true, false);

  addAndMakeVisible(timeLabel);
  timeLabel.setText("0:00 / 0:00", juce::dontSendNotification);
  timeLabel.setFont(fluentLookAndFeel.getBodyFont());
  timeLabel.setColour(juce::Label::textColourId,
                      fluentLookAndFeel.getColors().textSecondary);
  timeLabel.setMinimumHorizontalScale(1.0f);
  timeLabel.setJustificationType(juce::Justification::centredLeft);

  addAndMakeVisible(progressSlider);
  progressSlider.setRange(0.0, 1.0);
  progressSlider.addListener(this);
  progressSlider.addMouseListener(this, false);
  progressSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
  progressSlider.setPopupDisplayEnabled(false, false, this);
  progressSlider.setSliderStyle(juce::Slider::LinearHorizontal);
  progressSlider.setVelocityBasedMode(false);
  progressSlider.setScrollWheelEnabled(false);
  progressSlider.textFromValueFunction = [this](double v) {
    const auto state = core.state();
    const double dur = state.transport.durationSamples;
    const double sr = core.sampleRate();
    return formatTime((int)(v * dur / sr));
  };

  addAndMakeVisible(progressTimeTooltip);
  progressTimeTooltip.setVisible(false);

  setupIconButton(prevBtn, L"\uE892", L"上一首 (←)");
  setupIconButton(playBtn, L"\uE768", L"播放/暂停 (空格)");
  setupIconButton(nextBtn, L"\uE893", L"下一首 (→)");
  setupIconButton(stopBtn, L"\uE71A", L"停止");

  addAndMakeVisible(loopModeBtn);
  loopModeBtn.addListener(this);
  loopModeBtn.setTooltip(L"播放模式: 连续播放");

  addAndMakeVisible(exportBtn);
  exportBtn.addListener(this);
  exportBtn.setTooltip(L"离线渲染导出高保真音频");

  addAndMakeVisible(volumeBtn);
  volumeBtn.addListener(this);
  volumeBtn.setTooltip(L"点击切换静音");

  addAndMakeVisible(volumeSlider);

  volumeSlider.addListener(this);
  volumeSlider.addMouseListener(this, false);
  volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
  volumeSlider.setPopupDisplayEnabled(false, false, this);
  volumeSlider.setScrollWheelEnabled(false);
  volumeSlider.setNumDecimalPlacesToDisplay(0);
  volumeSlider.textFromValueFunction = [](double v) {
    return juce::String(juce::roundToInt(v * 100)) + "%";
  };

  updateLoopButtonTooltip();

  background.addMouseListener(this, false);

  progressSlider.setEnabled(false);

  startTimerHz(30);
  updatePluginList();

  auto startupColor = background.getTargetAccentColor();
  getAppSettings().setThemeAccentColor(startupColor.toString());
  fluentLookAndFeel.updateAccentColor(startupColor);
  fluentLookAndFeel.updateAccentColor(startupColor);

  runLater(150, [](MainContentComponent &self) {
    if (auto *topLevel = self.getTopLevelComponent())
      Win11Helpers::applyWin11Style(
          topLevel, juce::Desktop::getInstance().isDarkModeActive());

    // Shell-open 流程统一处理音频设备错误，避免启动期对话框堆叠。
    if (self.pendingShellOpen)
      return;

    if (self.core.isFirstRunAudio()) {
      juce::AlertWindow::showOkCancelBox(
          juce::AlertWindow::QuestionIcon, L"首次运行 - 音频设置",
          L"检测到当前未配置音频输出设备。为了正确加载乐器插件，建议"
          L"立即进行音频配置。",
          L"立即设置", L"以后再说", nullptr,
          juce::ModalCallbackFunction::create(
              [safeThis = juce::Component::SafePointer<MainContentComponent>(
                   &self)](int result) {
                if (result == 1 && safeThis != nullptr)
                  safeThis->showAudioSettings();
              }));
    } else if (!self.core.hasAudioDevice()) {
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::WarningIcon, L"音频设备不可用",
          L"未检测到可用的音频输出设备，请检查您的音频设备是否正常工作。"
          L"\n\n您可以在侧栏「设置」中手动配置音频输出。");
    } else if (self.core.wasDeviceRestoredWithFallback()) {
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::InfoIcon, L"音频设备已重置",
          L"由于上次使用的音频设备未找到，已自动切换到系统的默认播放设备。");
    }
  });

  addAndMakeVisible(modeToast);
  addChildComponent(tooltipOverlay);
  tooltipOverlay.setPaintedByParentOverlay(true);
  addMouseListener(&tooltipOverlay, true);

  runLater(200, [](MainContentComponent &self) {
    self.playlistPanel.autoLoadLastPlaylist();
  });

  runLater(500, [](MainContentComponent &self) {
    self.showFileAssociationPrompt();
  });
}

MainContentComponent::~MainContentComponent() {
  removeMouseListener(&tooltipOverlay);
  tooltipOverlay.hideTooltip();
  stopTimer();
  cancelPendingUpdate();
  closeSettingsWindows();
  fileChooser.reset();
  saveSettings();
  core.cancelPendingPluginOperation();
  pluginLoadingWindow.deleteAndZero();
  pluginMessageWindow.deleteAndZero();
  pluginLifecycle.closeWindow();
  setLookAndFeel(nullptr);
}

void MainContentComponent::timerCallback() {
  static uint32_t lastCallTime = 0;
  static uint32_t freezeDiagCounter = 0;
  auto now = juce::Time::getMillisecondCounter();

  if (++freezeDiagCounter >= 500) {
    LOG_DEBUG("[FREEZE_DIAG] timerCallback heartbeat #" +
              juce::String(freezeDiagCounter));
    freezeDiagCounter = 0;
  }

  if (lastCallTime != 0) {
    auto interval = now - lastCallTime;
    if (interval > 100) {
      LOG_DEBUG("TIMER LAG: interval was " + juce::String(interval) + " ms");
    }
  }
  lastCallTime = now;

  SCOPED_TIMER_SLOW("MainContentComponent::timerCallback", 20);

  core.tick(isUserDraggingProgress);
  const auto state = core.state();
  const auto &transport = state.transport;
  const double currentSampleRate = core.sampleRate();

  auto currentTime = juce::Time::getMillisecondCounter();
  const bool isPlaying = transport.playing;

  const bool hasTrack = transport.hasSequence;
  if (!hasTrack)
    progressTimeTooltip.hide();

  // 拖动或刚完成 seek 时暂缓刷新，避免进度条被计时器回拉。
  if (!isUserDraggingProgress &&
      (currentTime - lastSeekRequestTime.load() > 250)) {
    const double pos = transport.positionSamples;
    const double dur = transport.durationSamples;
    if (dur > 0) {
      progressSlider.setValue(pos / dur, juce::dontSendNotification);
      timeLabel.setText(formatTime((int)(pos / currentSampleRate)) + " / " +
                            formatTime((int)(dur / currentSampleRate)),
                        juce::dontSendNotification);
    } else {
      progressSlider.setValue(0.0, juce::dontSendNotification);
      timeLabel.setText("0:00 / 0:00", juce::dontSendNotification);
    }
  }

  if (isPlaying != lastPlayingState) {
    lastPlayingState = isPlaying;
    repaint();
  }

  auto tcEndTime = juce::Time::getMillisecondCounter();
  auto tcDuration = tcEndTime - now;
  if (tcDuration > 20) { // 超过 20ms 视为慢速执行。
    LOG_DEBUG("[FREEZE_DIAG] TC completed (slow: " + juce::String(tcDuration) +
              "ms)");
  }

  const bool hasPlugin = state.plugin.loaded && !state.plugin.workerCrashed &&
                         !state.plugin.loadInProgress;
  scanBtn.setEnabled(!state.plugin.operationInProgress &&
                     !state.task.exportActive);
  unloadBtn.setEnabled(hasPlugin && !state.plugin.operationInProgress);
  openPluginBtn.setEnabled(hasPlugin && !state.plugin.operationInProgress);
  pluginSelector.setEnabled(!isScanningPlugins && !pluginLoadInProgress &&
                            !state.plugin.operationInProgress);
  if (state.plugin.workerCrashed && !state.plugin.operationInProgress) {
    handlePluginWorkerCrash();
  } else if (hasPlugin) {
    pluginWorkerCrashAlertShown = false;
  }

  const bool canControlPlayback = hasPlugin && !state.task.exportActive;
  playBtn.setEnabled(canControlPlayback);
  stopBtn.setEnabled(canControlPlayback);
  prevBtn.setEnabled(canControlPlayback);
  nextBtn.setEnabled(canControlPlayback);
  progressSlider.setEnabled(canControlPlayback && hasTrack);
  exportBtn.setEnabled(canControlPlayback && hasTrack &&
                       !state.plugin.operationInProgress);

  if (!hasPlugin) {
    progressSlider.setValue(0.0, juce::dontSendNotification);
    timeLabel.setText("0:00 / 0:00", juce::dontSendNotification);
  }

  if (playlistPanel.getCurrentTrackIndex() != transport.currentTrackIndex)
    playlistPanel.setCurrentTrackIndex(transport.currentTrackIndex);
  trackLabel.setText(juce::String(transport.currentMidiName.c_str()),
                     juce::dontSendNotification);

  if (playbackModeAnimationScale < 1.0f) {
    playbackModeAnimationScale += 0.05f;
    if (playbackModeAnimationScale >= 1.0f)
      playbackModeAnimationScale = 1.0f;
    repaint();
  }
}

void MainContentComponent::paint(juce::Graphics &g) {
  SCOPED_TIMER_SLOW("MainContentComponent::paint", 10);
  auto &colors = fluentLookAndFeel.getColors();

  g.setColour(colors.transportBackground);
  g.fillRect(transportBar.getBounds());

  g.setColour(colors.cardBorder);
  g.drawHorizontalLine(transportBar.getY(), 0.0f, (float)getWidth());

  if (isDragOver) {
    g.setColour(colors.accentPrimary.withAlpha(0.15f));
    g.fillAll();
    g.setColour(colors.accentPrimary);
    g.drawRect(getLocalBounds(), 3);
  }
}

void MainContentComponent::paintOverChildren(juce::Graphics &g) {
  const bool isPlaying = core.state().transport.playing;

  drawIconButton(g, scanBtn, L"\uE9A1");
  drawIconButton(g, unloadBtn, L"\uE74D");
  drawIconButton(g, openPluginBtn, L"\uE8A7");
  drawIconButton(g, exportBtn, L"\uE896");

  drawIconButton(g, prevBtn, L"\uE892", LegacyDesignTokens::Icon::transport);
  drawPlayButton(g, playBtn, isPlaying);
  drawIconButton(g, nextBtn, L"\uE893", LegacyDesignTokens::Icon::transport);
  drawIconButton(g, stopBtn, L"\uE71A", LegacyDesignTokens::Icon::transport);

  float vol = (float)volumeSlider.getValue();
  const auto volIcon = getVolumeIconGlyph(vol);
  drawIconButton(g, volumeBtn, volIcon, LegacyDesignTokens::Icon::transport);

  juce::String loopIcon;
  bool isSequential = false;
  switch (static_cast<midi::PlaybackMode>(core.state().playlist.playMode)) {
  case midi::PlaybackMode::Sequential:
    isSequential = true;
    break;
  case midi::PlaybackMode::LoopList:
    loopIcon = L"\uE8EE";
    break;
  case midi::PlaybackMode::LoopSingle:
    loopIcon = L"\uE8ED";
    break;
  case midi::PlaybackMode::Shuffle:
    loopIcon = L"\uE8B1";
    break;
  }

  if (isSequential) {
    g.saveState();
    auto b = loopModeBtn.getBounds().toFloat();
    g.addTransform(juce::AffineTransform::scale(
        playbackModeAnimationScale, playbackModeAnimationScale, b.getCentreX(),
        b.getCentreY()));
    drawSequentialIcon(g, loopModeBtn);
    g.restoreState();
  } else {
    g.saveState();
    auto b = loopModeBtn.getBounds().toFloat();
    g.addTransform(juce::AffineTransform::scale(
        playbackModeAnimationScale, playbackModeAnimationScale, b.getCentreX(),
        b.getCentreY()));
    drawIconButton(g, loopModeBtn, loopIcon,
                   LegacyDesignTokens::Icon::transport);
    g.restoreState();
  }

  modeToast.toFront(false);
  if (tooltipOverlay.isVisible()) {
    const auto tooltipBounds = tooltipOverlay.getBounds();
    juce::Graphics::ScopedSaveState saveState(g);
    g.reduceClipRegion(tooltipBounds);
    g.setOrigin(tooltipBounds.getPosition());
    tooltipOverlay.paintOverlay(g);
  }
}

void MainContentComponent::resized() {
  SCOPED_TIMER_SLOW("MainContentComponent::resized", 10);
  triggerAsyncUpdate(); // 合并频繁布局请求。
}

void MainContentComponent::handleAsyncUpdate() {
  SCOPED_TIMER_ALWAYS("MainContentComponent::performLayout");
  auto area = getLocalBounds();

  background.setBounds(area);

  int navWidth = navigation.getPreferredWidth();
  navigation.setBounds(area.removeFromLeft(navWidth));

  const int transportHeight = LegacyDesignTokens::Layout::transportHeight(
      fluentLookAndFeel.getUIFontSize());
  auto transportArea = area.removeFromBottom(transportHeight);
  transportBar.setBounds(transportArea);
  layoutTransportBar(transportArea);

  constexpr int padding = LegacyDesignTokens::Layout::contentHorizontalPadding;
  auto content =
      area.reduced(padding, LegacyDesignTokens::Layout::contentVerticalPadding);

  auto header =
      content.removeFromTop(LegacyDesignTokens::Layout::pageHeaderHeight(
          fluentLookAndFeel.getUIFontSize()));

  constexpr int btnSize = LegacyDesignTokens::Layout::toolbarButtonSize;
  constexpr int comboWidth = LegacyDesignTokens::Layout::pluginSelectorWidth;
  const int availableComboWidth =
      header.getWidth() - btnSize * 4 -
      LegacyDesignTokens::Layout::pageTitleMinimumWidth;
  const int actualComboWidth =
      juce::jlimit(LegacyDesignTokens::Layout::pluginSelectorMinimumWidth,
                   comboWidth, availableComboWidth);

  openPluginBtn.setBounds(
      header.removeFromRight(btnSize).withSizeKeepingCentre(btnSize, btnSize));
  exportBtn.setBounds(
      header.removeFromRight(btnSize).withSizeKeepingCentre(btnSize, btnSize));
  unloadBtn.setBounds(
      header.removeFromRight(btnSize).withSizeKeepingCentre(btnSize, btnSize));
  scanBtn.setBounds(
      header.removeFromRight(btnSize).withSizeKeepingCentre(btnSize, btnSize));
  const int controlHeight = LegacyDesignTokens::Layout::controlHeight(
      fluentLookAndFeel.getUIFontSize());
  pluginSelector.setBounds(
      header.removeFromRight(actualComboWidth)
          .withSizeKeepingCentre(actualComboWidth, controlHeight));
  pageTitle.setBounds(header.removeFromLeft(juce::jmin(
      LegacyDesignTokens::Layout::pageTitleWidth, header.getWidth())));

  content.removeFromTop(12);

  if (currentPage == "playlist") {
    playlistPanel.setVisible(true);
    contentLabel.setVisible(false);
    playlistPanel.setBounds(content);
  } else {
    playlistPanel.setVisible(false);
    contentLabel.setVisible(true);
    contentLabel.setBounds(content);
  }
}

void MainContentComponent::layoutTransportBar(juce::Rectangle<int> area) {
  area = area.reduced(LegacyDesignTokens::Layout::transportHorizontalPadding,
                      LegacyDesignTokens::Layout::transportVerticalPadding);

  progressSlider.setBounds(
      area.removeFromTop(LegacyDesignTokens::Layout::transportProgressHeight));
  area.removeFromTop(LegacyDesignTokens::Layout::transportProgressGap);

  auto controlRow = area;
  constexpr int btnSize = LegacyDesignTokens::Layout::transportButtonSize;
  constexpr int playBtnSize =
      LegacyDesignTokens::Layout::transportPrimaryButtonSize;

  auto volumeArea = controlRow.removeFromRight(
      LegacyDesignTokens::Layout::transportVolumeAreaWidth);
  loopModeBtn.setBounds(volumeArea.removeFromLeft(btnSize).reduced(2));
  volumeBtn.setBounds(volumeArea.removeFromLeft(btnSize).reduced(2));

  volumeSlider.setBounds(volumeArea.reduced(4, 4));

  constexpr int gap = LegacyDesignTokens::Layout::controlGap;
  int controlsWidth = btnSize * 3 + playBtnSize + gap * 3;

  // 为居中的播放控制预留空间后，动态分配左侧曲目信息宽度。
  constexpr int minTrackWidth =
      LegacyDesignTokens::Layout::transportMinimumTrackWidth;
  constexpr int centerPadding =
      LegacyDesignTokens::Layout::transportCentrePadding;
  int availableForTrack =
      (controlRow.getWidth() - controlsWidth) / 2 - centerPadding;
  const int maximumTrackWidth =
      juce::jmax(0, controlRow.getWidth() - controlsWidth - gap);
  int trackInfoWidth = juce::jmin(maximumTrackWidth,
                                  juce::jmax(minTrackWidth, availableForTrack));

  auto leftInfo = controlRow.removeFromLeft(trackInfoWidth);
  trackLabel.setBounds(
      leftInfo.removeFromTop(LegacyDesignTokens::Typography::lineHeight(
          LegacyDesignTokens::Typography::body,
          fluentLookAndFeel.getUIFontSize())));
  timeLabel.setBounds(
      leftInfo.removeFromTop(LegacyDesignTokens::Typography::lineHeight(
          LegacyDesignTokens::Typography::body,
          fluentLookAndFeel.getUIFontSize())));

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

void MainContentComponent::navigationItemSelected(const juce::String &itemId) {
  if (itemId == "settings")
    showAudioSettings();
  else if (itemId == "background")
    showBackgroundSettings();
  else if (itemId == "fonts")
    showFontSettings();
  else if (itemId == "playlist")
    showPage("playlist", L"音乐列表");
  else if (itemId == "library")
    showPage("library", L"乐器库");
}

void MainContentComponent::navigationPinToggled(bool isPinned) {
  if (auto *tlw = getTopLevelComponent())
    tlw->setAlwaysOnTop(isPinned);
}

void MainContentComponent::navigationBackgroundClicked() {
  playlistPanel.deselectAllRows();
}

void MainContentComponent::buttonClicked(juce::Button *b) {
  if (b == &scanBtn)
    startPluginScan();
  else if (b == &unloadBtn)
    confirmUnloadPlugin();
  else if (b == &openPluginBtn)
    openPluginWindow();
  else if (b == &playBtn)
    togglePlayPause();
  else if (b == &prevBtn)
    playPreviousTrack();
  else if (b == &nextBtn)
    playNextTrack();
  else if (b == &stopBtn)
    stopPlayback();
  else if (b == &volumeBtn)
    toggleMute();
  else if (b == &loopModeBtn)
    toggleLoopMode();
  else if (b == &exportBtn)
    showExportDialog();
}

void MainContentComponent::toggleLoopMode() {
  auto current =
      static_cast<midi::PlaybackMode>(core.state().playlist.playMode);
  auto next =
      static_cast<midi::PlaybackMode>((static_cast<int>(current) % 4) + 1);
  core.setPlayMode((int)next);

  juce::String tip;
  juce::String toastText;
  switch (next) {
  case midi::PlaybackMode::Sequential:
    tip = L"播放模式: 连续播放";
    toastText = L"连续播放";
    break;
  case midi::PlaybackMode::LoopList:
    tip = L"播放模式: 列表循环";
    toastText = L"列表循环";
    break;
  case midi::PlaybackMode::LoopSingle:
    tip = L"播放模式: 单曲循环";
    toastText = L"单曲循环";
    break;
  case midi::PlaybackMode::Shuffle:
    tip = L"播放模式: 随机播放";
    toastText = L"随机播放";
    break;
  }
  loopModeBtn.setTooltip(tip);

  // 切换模式时隐藏悬浮提示，避免与 ToastComponent 重叠。

  modeToast.show(toastText, loopModeBtn.getBounds());
  playbackModeAnimationScale =
      LegacyDesignTokens::Motion::playbackModeInitialScale;

  repaint();
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

void MainContentComponent::updateLoopButtonTooltip() {
  juce::String tip;
  switch (static_cast<midi::PlaybackMode>(core.state().playlist.playMode)) {
  case midi::PlaybackMode::Sequential:
    tip = L"播放模式: 连续播放";
    break;
  case midi::PlaybackMode::LoopList:
    tip = L"播放模式: 列表循环";
    break;
  case midi::PlaybackMode::LoopSingle:
    tip = L"播放模式: 单曲循环";
    break;
  case midi::PlaybackMode::Shuffle:
    tip = L"播放模式: 随机播放";
    break;
  }
  loopModeBtn.setTooltip(tip);
}

void MainContentComponent::sliderValueChanged(juce::Slider *s) {
  if (s == &volumeSlider) {
    float vol = (float)s->getValue();
    // Persist the slider level; Core receives the converted audio gain only.
    getAppSettings().setMasterVolume(vol);
    core.volume(volumeLevelToGain(vol));
    if (vol > 0.0f) {
      isMuted = false;
      volumeBeforeMute = vol;
    }
    if (volumeSlider.isThumbDragActive())
      updateVolumeTooltip();
    repaint();
  } else if (s == &progressSlider) {
    // 点击跳转时记录 seek 时间，避免 timerCallback 立即回写旧位置。
    if (!isUserDraggingProgress)
      lastSeekRequestTime.store(juce::Time::getMillisecondCounter());

    // 拖动中只更新显示时间，真正的 seek 在拖动结束后异步执行。
    const auto state = core.state();
    const double dur = state.transport.durationSamples;
    if (dur > 0) {
      double currentVal = s->getValue();
      const double sr = core.sampleRate();
      int seconds = (int)(currentVal * dur / sr);
      int totalSeconds = (int)(dur / sr);
      timeLabel.setText(formatTime(seconds) + " / " + formatTime(totalSeconds),
                        juce::dontSendNotification);
    }
  }
}

void MainContentComponent::sliderDragStarted(juce::Slider *s) {
  if (s == &progressSlider)
    isUserDraggingProgress = true;
}

void MainContentComponent::sliderDragEnded(juce::Slider *s) {
  if (s == &progressSlider) {
    isUserDraggingProgress = false;
    lastSeekRequestTime.store(juce::Time::getMillisecondCounter());

    // 使用 AsyncUpdater 把 seek 移出拖动事件栈。
    triggerSeekUpdate(s->getValue());
  }
}

void MainContentComponent::triggerSeekUpdate(double normalizedPos) {
  auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
  juce::MessageManager::callAsync([safeThis, normalizedPos]() {
    if (safeThis != nullptr)
      safeThis->core.seek(normalizedPos);
  });
}

void MainContentComponent::comboBoxChanged(juce::ComboBox *c) {
  if (c == &pluginSelector)
    loadSelectedPlugin();
}

void MainContentComponent::playlistTrackSelected(int index) {}

void MainContentComponent::playlistLoaded(const juce::File &playlistFile) {
  currentPlaylistFile =
      playlistFile.existsAsFile() ? playlistFile : juce::File();
}

bool MainContentComponent::playlistSaveRequested() { return savePlaylist(); }

bool MainContentComponent::playlistClearRequested() {
  if (!core.clearPlaylist())
    return false;
  currentPlaylistFile = {};
  return true;
}

bool MainContentComponent::playlistLoadRequested(
    const juce::File &playlistFile) {
  const bool loaded = core.loadList(
      std::wstring(playlistFile.getFullPathName().toWideCharPointer()));
  if (loaded) {
    currentPlaylistFile = playlistFile;
  } else {
    showOperationError(L"加载播放列表失败", core.lastPlaylistError());
  }
  return loaded;
}

bool MainContentComponent::playlistTrackMoveRequested(int fromIndex,
                                                      int toIndex,
                                                      int newCurrentIndex) {
  juce::ignoreUnused(newCurrentIndex);
  return core.moveTrack(fromIndex, toIndex);
}

bool MainContentComponent::playlistTrackRemoveRequested(int index,
                                                        int newCurrentIndex) {
  juce::ignoreUnused(newCurrentIndex);
  return core.removeTrack(index);
}

void MainContentComponent::playlistTrackReordered(int newCurrentIndex) {
  juce::ignoreUnused(newCurrentIndex);
}

void MainContentComponent::playlistTrackDoubleClicked(int index) {
  LOG_DEBUG("Playlist double-clicked index: " + juce::String(index));
  if (!core.hasPluginLoaded()) {
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                           L"无法播放",
                                           L"请先加载一个乐器插件以开始播放。");
    return;
  }
  core.playTrackAt(index);
  playlistPanel.setCurrentTrackIndex(core.currentTrackIndex());
}

bool MainContentComponent::isInterestedInFileDrag(
    const juce::StringArray &files) {
  for (auto &f : files)
    if (f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi"))
      return true;
  return false;
}

void MainContentComponent::filesDropped(const juce::StringArray &files, int,
                                        int) {
  isDragOver = false;
  playlistFilesDropped(files);
  repaint();
}

void MainContentComponent::backgroundSettingsChanged(bool reapplyEffects) {
  if (reapplyEffects)
    background.loadAsync();

  auto accentColor =
      juce::Colour::fromString(getAppSettings().getThemeAccentColor());
  fluentLookAndFeel.updateAccentColor(accentColor);
  repaint();
}

void MainContentComponent::dialogMaterialChanged(bool backdropChanged) {
  refreshDialogMaterials(backdropChanged);
}

void MainContentComponent::backgroundSettingsClosed() {}

void MainContentComponent::onAccentColorChanged(juce::Colour newColor) {
  getAppSettings().setThemeAccentColor(newColor.toString());
  fluentLookAndFeel.updateAccentColor(newColor);

  navigation.repaint();
  playlistPanel.repaint();
  repaint();

  // 仅在 BackgroundComponent 过渡到目标色后广播，避免频繁刷新 LookAndFeel。
  if (newColor == background.getTargetAccentColor()) {
    sendLookAndFeelChange();
  }
}

bool MainContentComponent::keyPressed(const juce::KeyPress &key) {
  if (key == juce::KeyPress::spaceKey) {
    togglePlayPause();
    return true;
  } else if (key == juce::KeyPress::leftKey) {
    playPreviousTrack();
    return true;
  } else if (key == juce::KeyPress::rightKey) {
    playNextTrack();
    return true;
  } else if (key.getModifiers().isCtrlDown() && key.getKeyCode() == 'O') {
    showOpenFileDialog();
    return true;
  } else if (key == juce::KeyPress::escapeKey) {
    closePluginWindow();
    return true;
  }
  return false;
}

void MainContentComponent::fileDragEnter(const juce::StringArray &, int, int) {
  isDragOver = true;
  repaint();
}

void MainContentComponent::fileDragExit(const juce::StringArray &) {
  isDragOver = false;
  repaint();
}

void MainContentComponent::mouseMove(const juce::MouseEvent &e) {
  if (e.eventComponent == &progressSlider)
    updateProgressTimeTooltip(e);
  else if (e.eventComponent == &volumeSlider) {
    if (volumeSlider.isPointOverThumb(e.position))
      updateVolumeTooltip();
    else
      progressTimeTooltip.hide();
  }
}

void MainContentComponent::mouseDrag(const juce::MouseEvent &e) {
  if (e.eventComponent == &progressSlider)
    updateProgressTimeTooltip(e);
  else if (e.eventComponent == &volumeSlider) {
    if (volumeSlider.isThumbDragActive())
      updateVolumeTooltip();
    else
      progressTimeTooltip.hide();
  }
}

void MainContentComponent::mouseExit(const juce::MouseEvent &e) {
  if (e.eventComponent == &progressSlider || e.eventComponent == &volumeSlider)
    progressTimeTooltip.hide();
}

void MainContentComponent::mouseUp(const juce::MouseEvent &e) {
  if (e.eventComponent != &volumeSlider)
    return;

  if (volumeSlider.isPointOverThumb(e.position))
    updateVolumeTooltip();
  else
    progressTimeTooltip.hide();
}

void MainContentComponent::mouseDown(const juce::MouseEvent &e) {
  if (e.eventComponent == &progressSlider) {
    updateProgressTimeTooltip(e);
    return;
  }
  if (e.eventComponent == &volumeSlider) {
    updateVolumeTooltip();
    return;
  }

  playlistPanel.deselectAllRows();
}

bool MainContentComponent::getProgressHoverInfo(const juce::MouseEvent &e,
                                                juce::String &text,
                                                int &anchorX) {
  const auto state = core.state();
  const double durationSamples = state.transport.durationSamples;
  const double sampleRate = core.sampleRate();

  if (!progressSlider.isEnabled() || durationSamples <= 0.0 ||
      sampleRate <= 0.0 || progressSlider.getWidth() <= 0)
    return false;

  const double minPos = progressSlider.getPositionOfValue(0.0);
  const double maxPos = progressSlider.getPositionOfValue(1.0);
  const double span = maxPos - minPos;
  if (std::abs(span) < 0.001)
    return false;

  const double sliderX = (double)e.getPosition().x;
  const double ratio = juce::jlimit(0.0, 1.0, (sliderX - minPos) / span);
  const double clampedSliderX = minPos + ratio * span;
  const double seconds = juce::jlimit(0.0, durationSamples / sampleRate,
                                      ratio * durationSamples / sampleRate);

  anchorX = progressSlider.getX() + (int)std::round(clampedSliderX);
  text = formatTime((int)std::round(seconds));
  return true;
}

void MainContentComponent::updateProgressTimeTooltip(
    const juce::MouseEvent &e) {
  juce::String text;
  int anchorX = 0;
  if (!getProgressHoverInfo(e, text, anchorX)) {
    progressTimeTooltip.hide();
    return;
  }

  progressTimeTooltip.showForTarget(
      text, {anchorX, progressSlider.getY(), 1, progressSlider.getHeight()},
      *this);
}

void MainContentComponent::updateVolumeTooltip() {
  if (!volumeSlider.isEnabled() || volumeSlider.getWidth() <= 0) {
    progressTimeTooltip.hide();
    return;
  }

  const auto info = getVolumeTooltipInfo(volumeSlider);
  progressTimeTooltip.showValueAt(info.text, info.anchorBounds,
                                  getLocalBounds());
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

void MainContentComponent::setupIconButton(juce::Button &btn,
                                           const juce::String &,
                                           const juce::String &tooltip) {
  addAndMakeVisible(btn);
  btn.addListener(this);
  btn.setTooltip(tooltip);
}

void MainContentComponent::drawIconButton(juce::Graphics &g, juce::Button &btn,
                                          const juce::String &icon,
                                          float iconSize) {
  if (!btn.isVisible())
    return;

  auto bounds = btn.getBounds().toFloat();
  auto &colors = fluentLookAndFeel.getColors();

  if (btn.isEnabled() && (btn.isMouseOver() || btn.isMouseButtonDown())) {
    g.setColour(btn.isMouseButtonDown() ? colors.controlPressed
                                        : colors.controlHover);
    g.fillRoundedRectangle(bounds.reduced(2.0f), 6.0f);
  }

  g.setColour(btn.isEnabled() ? colors.textPrimary
                              : colors.textSecondary.withAlpha(0.5f));
  fluentLookAndFeel.drawIconGlyph(g, icon, btn.getBounds().toFloat(), iconSize);
}

void MainContentComponent::drawIconButtonCombined(juce::Graphics &g,
                                                  juce::Button &btn,
                                                  const juce::String &mainIcon,
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

  auto mainArea = btn.getBounds().translated(-2, -1);
  fluentLookAndFeel.drawIconGlyph(g, mainIcon, mainArea.toFloat(),
                                  LegacyDesignTokens::Icon::toolbar);

  auto subArea = btn.getBounds().translated(6, 6);
  fluentLookAndFeel.drawIconGlyph(g, subIcon, subArea.toFloat(),
                                  LegacyDesignTokens::Icon::overlay);
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
    fluentLookAndFeel.drawIconGlyph(g, L"\uEA42", btn.getBounds().toFloat(),
                                    LegacyDesignTokens::Icon::transport);
  } else {
    if (sequentialIconDrawable != nullptr) {
      if (lastSequentialIconColor != iconColor) {
        sequentialIconDrawable->replaceColour(lastSequentialIconColor,
                                              iconColor);
        lastSequentialIconColor = iconColor;
      }
      fluentLookAndFeel.drawDrawableIcon(
          g, *sequentialIconDrawable, btn.getBounds().toFloat(),
          LegacyDesignTokens::Icon::transport *
              LegacyIconAssets::sequentialPlaybackOpticalScale);
    }
  }
}

void MainContentComponent::drawPlayButton(juce::Graphics &g, juce::Button &btn,
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

  g.setColour(isEnabled ? colors.textPrimary
                        : colors.textSecondary.withAlpha(0.4f));
  fluentLookAndFeel.drawIconGlyph(g, isPlaying ? L"\uE769" : L"\uE768",
                                  btn.getBounds().toFloat(),
                                  LegacyDesignTokens::Icon::primary);

  if (!isEnabled) {
    g.setColour(colors.background.withAlpha(0.3f));
    g.drawEllipse(bounds, 2.5f);
  }
}

void MainContentComponent::showPage(const juce::String &pageId,
                                    const juce::String &title) {
  currentPage = pageId;
  pageTitle.setText(title, juce::dontSendNotification);
  navigation.setSelectedItem(pageId);
  resized();
}

void MainContentComponent::showOperationError(const juce::String &title,
                                              const std::wstring &message) {
  const auto text = message.empty() ? juce::String(L"操作失败。")
                                    : juce::String(message.c_str());
  showPluginMessage(title, text);
}

void MainContentComponent::showPluginMessage(const juce::String &title,
                                             const juce::String &message) {
  if (pluginMessageWindow != nullptr)
    return;
  pluginMessageWindow = FluentSettingsStyle::showMessageDialogAsync(
      title, message, this, fluentLookAndFeel);
}

void MainContentComponent::togglePlayPause() {
  playbackPausedByPluginSwitch = false;
  core.togglePlay();
}

void MainContentComponent::stopPlayback() {
  playbackPausedByPluginSwitch = false;
  core.stop();
}

void MainContentComponent::playNextTrack() {
  SCOPED_TIMER_ALWAYS("MainContentComponent::playNextTrack");
  core.next();
  playlistPanel.setCurrentTrackIndex(core.currentTrackIndex());
}

void MainContentComponent::playPreviousTrack() {
  SCOPED_TIMER_ALWAYS("MainContentComponent::playPreviousTrack");
  core.prev();
  playlistPanel.setCurrentTrackIndex(core.currentTrackIndex());
}

void MainContentComponent::showOpenFileDialog() {
  fileChooser = std::make_unique<juce::FileChooser>(
      L"打开 MIDI 文件", juce::File(getAppSettings().getLastMidiDirectory()),
      "*.mid;*.midi");

  fileChooser->launchAsync(
      juce::FileBrowserComponent::openMode,
      [safeThis = juce::Component::SafePointer<MainContentComponent>(this)](
          const juce::FileChooser &fc) {
        if (safeThis == nullptr)
          return;

        auto result = fc.getResult();
        if (result.existsAsFile()) {
          getAppSettings().setLastMidiDirectory(
              result.getParentDirectory().getFullPathName());
          safeThis->core.openMidi(
              std::wstring(result.getFullPathName().toWideCharPointer()));
          safeThis->playlistPanel.refresh();
          safeThis->playlistPanel.setCurrentTrackIndex(
              safeThis->core.currentTrackIndex());
        }
      });
}

bool MainContentComponent::isFileAssociatedToSelf() {
  return isMidiFileAssociatedToSelf();
}

void MainContentComponent::removeFileAssociation() {
  removeMidiFileAssociation();
}

std::unique_ptr<juce::ToggleButton>
MainContentComponent::createDontShowAgainToggle() {
  auto toggle =
      std::make_unique<juce::ToggleButton>(L"\u4e0d\u518d\u63d0\u793a");
  toggle->setColour(juce::ToggleButton::textColourId, juce::Colours::white);
  toggle->setSize(200, 24);
  return toggle;
}

void MainContentComponent::setPendingShellOpen(bool pending) {
  pendingShellOpen = pending;
}

juce::String MainContentComponent::formatTime(int seconds) {
  return juce::String(seconds / 60) + ":" +
         juce::String(seconds % 60).paddedLeft('0', 2);
}
