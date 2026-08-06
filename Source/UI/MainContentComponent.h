#pragma once

#include "../AudioEngine/AudioEngine.h"
#include "../AudioEngine/ExportFileSupport.h"
#include "../Core/Core.h"
#include "../Core/LegacyBridge.h"
#include "../Core/PluginLoadNotification.h"
#include "../Playlist/PlaylistManager.h"
#include "../Utils/UserSettings.h"
#include "../Utils/Win11Helpers.h"
#include "../Utils/WindowsFileAssociation.h"
#include "BackgroundComponent.h"
#include "AudioSettingsSupport.h"
#include "AudioStatusAnimation.h"
#include "CustomControls.h"
#include "CustomLookAndFeel.h"
#include "NavigationSidebar.h"
#include "PlaylistPanel.h"
#include "PluginWindowLifecycle.h"
#include "ExportDialog.h"
#include "FluentSettingsStyle.h"
#include "LegacyExportTask.h"
#include "LegacyTransportWidgets.h"

#include <cmath>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

class MainContentComponent : public juce::Component,
                             public juce::Button::Listener,
                             public juce::ComboBox::Listener,
                             public juce::Slider::Listener,
                             public juce::FileDragAndDropTarget,
                             public juce::DragAndDropContainer,
                             public juce::Timer,
                             public PlaylistPanel::Listener,
                             public NavigationSidebar::Listener,
                             public BackgroundSettingsDialog::Listener,
                             public juce::AsyncUpdater {
public:
  MainContentComponent(midi::Core &c, FluentLookAndFeel &applicationLookAndFeel)
      : core(c), legacy(c), engine(legacy.engine()), playlist(legacy.playlist()),
        fluentLookAndFeel(applicationLookAndFeel),
        navigation(fluentLookAndFeel), playlistPanel(playlist) {
    setLookAndFeel(&fluentLookAndFeel);
    setWantsKeyboardFocus(true);
    loadSettings();

    auto xml = juce::XmlDocument::parse (
        R"(<svg width="16" height="16" viewBox="0 0 16 16" fill="none" xmlns="http://www.w3.org/2000/svg">
  <line x1="2.5" y1="4.5" x2="13.5" y2="4.5" stroke="#000000" stroke-width="1.3" stroke-linecap="round"/>
  <line x1="2.5" y1="8" x2="13.5" y2="8" stroke="#000000" stroke-width="1.3" stroke-linecap="round"/>
  <line x1="2.5" y1="11.5" x2="13.5" y2="11.5" stroke="#000000" stroke-width="1.3" stroke-linecap="round"/>
  <path d="M11 9L13.5 11.5L11 14" stroke="#000000" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round"/>
</svg>)");
    if (xml != nullptr) {
      sequentialIconDrawable = juce::Drawable::createFromSVG (*xml);
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
    pageTitle.setFont(fluentLookAndFeel.getDefaultFont(26.0f, true));
    pageTitle.setColour(juce::Label::textColourId,
                        fluentLookAndFeel.getColors().textPrimary);

    addAndMakeVisible(pluginSelector);
    pluginSelector.setTextWhenNothingSelected(L"选择乐器...");
    pluginSelector.addListener(this);
    pluginSelector.setTooltip(L"选择要加载的 VST3 乐器插件");

    addAndMakeVisible(scanBtn);
    scanBtn.addListener(this);
    scanBtn.setTooltip(L"扫描系统中的 VST3 插件");

    addAndMakeVisible(unloadBtn);
    unloadBtn.addListener(this);
    unloadBtn.setTooltip(L"卸载当前加载的插件");

    addAndMakeVisible(openPluginBtn);
    openPluginBtn.addListener(this);
    openPluginBtn.setEnabled(false);
    openPluginBtn.setTooltip(L"在新窗口中打开插件界面");

    unloadBtn.setEnabled(false);

    addAndMakeVisible(contentLabel);
    contentLabel.setFont(fluentLookAndFeel.getDefaultFont(16.0f));
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
    trackLabel.setFont(fluentLookAndFeel.getDefaultFont(15.0f, true));
    trackLabel.setColour(juce::Label::textColourId,
                         fluentLookAndFeel.getColors().textPrimary);
    trackLabel.setInterceptsMouseClicks(true, false);

    addAndMakeVisible(timeLabel);
    timeLabel.setText("0:00 / 0:00", juce::dontSendNotification);
    timeLabel.setFont(fluentLookAndFeel.getDefaultFont(13.0f));
    timeLabel.setColour(juce::Label::textColourId,
                        fluentLookAndFeel.getColors().textSecondary);

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

    volumeSlider.setRange(0.0, 1.0);
    float savedVol = getAppSettings().getMasterVolume();
    volumeSlider.setValue(savedVol, juce::dontSendNotification);
    core.volume(savedVol);
    volumeSlider.addListener(this);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    volumeSlider.setPopupDisplayEnabled(true, true, this);
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
        Win11Helpers::applyWin11Style(topLevel);

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

    runLater(200, [](MainContentComponent &self) {
      self.playlistPanel.autoLoadLastPlaylist();
    });

    runLater(500, [](MainContentComponent &self) {
      self.showFileAssociationPrompt();
    });
  }

  ~MainContentComponent() override {
    stopTimer();
    cancelPendingUpdate();
    closeSettingsWindows();
    fileChooser.reset();
    saveSettings();
    closePluginWindow();
    setLookAndFeel(nullptr);
  }

  void timerCallback() override {
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
    if (progressSlider.isEnabled() != hasTrack) {
      progressSlider.setEnabled(hasTrack);
      if (!hasTrack)
        progressTimeTooltip.hide();
    }

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
      LOG_DEBUG("[FREEZE_DIAG] TC completed (slow: " +
                juce::String(tcDuration) + "ms)");
    }

    const bool hasPlugin = state.plugin.loaded && !state.plugin.workerCrashed;
    if (state.plugin.workerCrashed) {
      handlePluginWorkerCrash(true);
    } else if (hasPlugin) {
      pluginWorkerCrashAlertShown = false;
    }

    if (playBtn.isEnabled() != hasPlugin) {
      playBtn.setEnabled(hasPlugin);
      stopBtn.setEnabled(hasPlugin);
      prevBtn.setEnabled(hasPlugin);
      nextBtn.setEnabled(hasPlugin);
      progressSlider.setEnabled(hasPlugin && hasTrack);
    }

    if (exportBtn.isEnabled() != (hasPlugin && hasTrack)) {
      exportBtn.setEnabled(hasPlugin && hasTrack);
    }

    if (!hasPlugin) {
      progressSlider.setValue(0.0, juce::dontSendNotification);
      timeLabel.setText("0:00 / 0:00", juce::dontSendNotification);
    }

    if (playlistPanel.getCurrentTrackIndex() != transport.currentTrackIndex)
      playlistPanel.setCurrentTrackIndex(transport.currentTrackIndex);
    if (!transport.currentMidiName.empty())
      trackLabel.setText(juce::String(transport.currentMidiName.c_str()),
                         juce::dontSendNotification);

    if (playbackModeAnimationScale < 1.0f) {
      playbackModeAnimationScale += 0.05f;
      if (playbackModeAnimationScale >= 1.0f)
        playbackModeAnimationScale = 1.0f;
      repaint();
    }

  }

  void paint(juce::Graphics &g) override {
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

  void paintOverChildren(juce::Graphics &g) override {
    const bool isPlaying = core.state().transport.playing;

    drawIconButton(g, scanBtn, L"\uE9A1");
    drawIconButton(g, unloadBtn, L"\uE74D");
    drawIconButton(g, openPluginBtn, L"\uE8A7");

    drawIconButton(g, prevBtn, L"\uE892");
    drawPlayButton(g, playBtn, isPlaying);
    drawIconButton(g, nextBtn, L"\uE893");
    drawIconButton(g, stopBtn, L"\uE71A");

    float vol = (float)volumeSlider.getValue();
    juce::String volIcon =
        vol > 0.5f ? L"\uE995" : (vol > 0 ? L"\uE994" : L"\uE992");
    drawIconButton(g, volumeBtn, volIcon);

    juce::String loopIcon;
    bool isSequential = false;
    switch (static_cast<PlaylistManager::PlaybackMode>(
        core.state().playlist.playMode)) {
    case PlaylistManager::PlaybackMode::Sequential:
      isSequential = true;
      break;
    case PlaylistManager::PlaybackMode::LoopList:
      loopIcon = L"\uE8EE";
      break;
    case PlaylistManager::PlaybackMode::LoopSingle:
      loopIcon = L"\uE8ED";
      break;
    case PlaylistManager::PlaybackMode::Shuffle:
      loopIcon = L"\uE8B1";
      break;
    }

    if (isSequential) {
      g.saveState();
      auto b = loopModeBtn.getBounds().toFloat();
      g.addTransform(juce::AffineTransform::scale(
          playbackModeAnimationScale, playbackModeAnimationScale,
          b.getCentreX(), b.getCentreY()));
      drawSequentialIcon(g, loopModeBtn);
      g.restoreState();
    } else {
      g.saveState();
      auto b = loopModeBtn.getBounds().toFloat();
      g.addTransform(juce::AffineTransform::scale(
          playbackModeAnimationScale, playbackModeAnimationScale,
          b.getCentreX(), b.getCentreY()));
      drawIconButton(g, loopModeBtn, loopIcon);
      g.restoreState();
    }

    modeToast.toFront(false);
  }

  void resized() override {
    SCOPED_TIMER_SLOW("MainContentComponent::resized", 10);
    triggerAsyncUpdate(); // 合并频繁布局请求。
  }

  void handleAsyncUpdate() override {
    SCOPED_TIMER_ALWAYS("MainContentComponent::performLayout");
    auto area = getLocalBounds();

    background.setBounds(area);

    int navWidth = navigation.getPreferredWidth();
    navigation.setBounds(area.removeFromLeft(navWidth));

    int transportHeight = 80;
    auto transportArea = area.removeFromBottom(transportHeight);
    transportBar.setBounds(transportArea);
    layoutTransportBar(transportArea);

    int padding = 24;
    auto content = area.reduced(padding, 16);

    auto header = content.removeFromTop(48);
    pageTitle.setBounds(header.removeFromLeft(180));

    int btnSize = 36;
    int comboWidth = 200;

    openPluginBtn.setBounds(header.removeFromRight(btnSize).reduced(2));
    exportBtn.setBounds(header.removeFromRight(btnSize).reduced(2));
    unloadBtn.setBounds(header.removeFromRight(btnSize).reduced(2));
    scanBtn.setBounds(header.removeFromRight(btnSize).reduced(2));
    pluginSelector.setBounds(header.removeFromRight(comboWidth).reduced(2, 6));

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

  void layoutTransportBar(juce::Rectangle<int> area) {
    area = area.reduced(20, 8);

    progressSlider.setBounds(area.removeFromTop(24));
    area.removeFromTop(4);

    auto controlRow = area;
    int btnSize = 36;
    int playBtnSize = 44; // 保持播放按钮为正方形。

    auto volumeArea = controlRow.removeFromRight(180);
    loopModeBtn.setBounds(volumeArea.removeFromLeft(btnSize).reduced(2));
    volumeBtn.setBounds(volumeArea.removeFromLeft(btnSize).reduced(2));

    volumeSlider.setBounds(volumeArea.reduced(4, 4));

    int gap = 8;
    int controlsWidth = btnSize * 3 + playBtnSize + gap * 3;

    // 为居中的播放控制预留空间后，动态分配左侧曲目信息宽度。
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

  void navigationItemSelected(const juce::String &itemId) override {
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

  void navigationPinToggled(bool isPinned) override {
    if (auto *tlw = getTopLevelComponent())
      tlw->setAlwaysOnTop(isPinned);
  }

  void navigationBackgroundClicked() override {
    playlistPanel.deselectAllRows();
  }

  void buttonClicked(juce::Button *b) override {
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

  void toggleLoopMode() {
    auto current = static_cast<PlaylistManager::PlaybackMode>(
        core.state().playlist.playMode);
    auto next = static_cast<PlaylistManager::PlaybackMode>(
        (static_cast<int>(current) % 4) + 1);
    core.setPlayMode((int)next);

    juce::String tip;
    juce::String toastText;
    switch (next) {
    case PlaylistManager::PlaybackMode::Sequential:
      tip = L"播放模式: 连续播放";
      toastText = L"连续播放";
      break;
    case PlaylistManager::PlaybackMode::LoopList:
      tip = L"播放模式: 列表循环";
      toastText = L"列表循环";
      break;
    case PlaylistManager::PlaybackMode::LoopSingle:
      tip = L"播放模式: 单曲循环";
      toastText = L"单曲循环";
      break;
    case PlaylistManager::PlaybackMode::Shuffle:
      tip = L"播放模式: 随机播放";
      toastText = L"随机播放";
      break;
    }
    loopModeBtn.setTooltip(tip);

    // 切换模式时隐藏悬浮提示，避免与 ToastComponent 重叠。

    modeToast.show(toastText, loopModeBtn.getBounds());
    playbackModeAnimationScale = 0.8f;

    repaint();
  }

  void toggleMute() {
    isMuted = !isMuted;
    if (isMuted) {
      volumeBeforeMute = volumeSlider.getValue();
      volumeSlider.setValue(0.0, juce::sendNotification);
    } else {
      volumeSlider.setValue(volumeBeforeMute, juce::sendNotification);
    }
    repaint();
  }

  void updateLoopButtonTooltip() {
    juce::String tip;
    switch (static_cast<PlaylistManager::PlaybackMode>(
        core.state().playlist.playMode)) {
    case PlaylistManager::PlaybackMode::Sequential:
      tip = L"播放模式: 连续播放";
      break;
    case PlaylistManager::PlaybackMode::LoopList:
      tip = L"播放模式: 列表循环";
      break;
    case PlaylistManager::PlaybackMode::LoopSingle:
      tip = L"播放模式: 单曲循环";
      break;
    case PlaylistManager::PlaybackMode::Shuffle:
      tip = L"播放模式: 随机播放";
      break;
    }
    loopModeBtn.setTooltip(tip);
  }

  void sliderValueChanged(juce::Slider *s) override {
    if (s == &volumeSlider) {
      float vol = (float)s->getValue();
      core.volume(vol);
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
        timeLabel.setText(formatTime(seconds) + " / " +
                              formatTime(totalSeconds),
                          juce::dontSendNotification);
      }
    }
  }

  void sliderDragStarted(juce::Slider *s) override {
    if (s == &progressSlider)
      isUserDraggingProgress = true;
  }

  void sliderDragEnded(juce::Slider *s) override {
    if (s == &progressSlider) {
      isUserDraggingProgress = false;
      lastSeekRequestTime.store(juce::Time::getMillisecondCounter());

      // 使用 AsyncUpdater 把 seek 移出拖动事件栈。
      triggerSeekUpdate(s->getValue());
    }
  }

  void triggerSeekUpdate(double normalizedPos) {
    auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
    juce::MessageManager::callAsync([safeThis, normalizedPos]() {
      if (safeThis != nullptr)
        safeThis->core.seek(normalizedPos);
    });
  }

  void comboBoxChanged(juce::ComboBox *c) override {
    if (c == &pluginSelector)
      loadSelectedPlugin();
  }

  void playlistTrackSelected(int index) override {
  }

  void playlistLoaded(const juce::File &playlistFile) override {
    currentPlaylistFile =
        playlistFile.existsAsFile() ? playlistFile : juce::File();
  }

  bool playlistSaveRequested() override { return savePlaylist(); }

  bool playlistClearRequested() override {
    core.clearPlaylist();
    core.setCurrentPlaylistFile(L"");
    currentPlaylistFile = {};
    getAppSettings().setLastPlaylistPath("");
    return true;
  }

  bool playlistLoadRequested(const juce::File &playlistFile) override {
    const bool loaded = core.loadList(
        std::wstring(playlistFile.getFullPathName().toWideCharPointer()));
    if (loaded) {
      currentPlaylistFile = playlistFile;
    } else {
      showOperationError(L"加载播放列表失败", core.lastPlaylistError());
    }
    return loaded;
  }

  bool playlistTrackMoveRequested(int fromIndex, int toIndex,
                                  int newCurrentIndex) override {
    juce::ignoreUnused(newCurrentIndex);
    return core.moveTrack(fromIndex, toIndex);
  }

  bool playlistTrackRemoveRequested(int index,
                                    int newCurrentIndex) override {
    juce::ignoreUnused(newCurrentIndex);
    return core.removeTrack(index);
  }

  void playlistTrackRevealRequested(int index) override {
    const auto path = core.trackFileAt(index);
    juce::File file(juce::String(path.c_str()));
    if (file.existsAsFile())
      file.revealToUser();
  }

  void playlistTrackReordered(int newCurrentIndex) override {
    juce::ignoreUnused(newCurrentIndex);
  }

  void playlistTrackDoubleClicked(int index) {
    LOG_DEBUG("Playlist double-clicked index: " + juce::String(index));
    if (!core.hasPluginLoaded()) {
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::WarningIcon, L"无法播放",
          L"请先加载一个乐器插件以开始播放。");
      return;
    }
    core.playTrackAt(index);
    playlistPanel.setCurrentTrackIndex(core.currentTrackIndex());
  }

  void playlistFilesDropped(const juce::StringArray &files) override {
    juce::StringArray newFiles;
    juce::StringArray duplicateFiles;

    for (auto &f : files) {
      juce::File file(f);
      if (core.findTrackIndex(
              std::wstring(file.getFullPathName().toWideCharPointer())) >= 0) {
        duplicateFiles.add(f);
      } else {
        newFiles.add(f);
      }
    }

    enum class DupAction { AddNewOnly, OverwriteExisting, Cancel };
    DupAction action = DupAction::AddNewOnly;

    if (!duplicateFiles.isEmpty()) {
      int result = juce::AlertWindow::showYesNoCancelBox(
          juce::AlertWindow::QuestionIcon, L"发现重复文件",
          L"检测到 " + juce::String(duplicateFiles.size()) +
              L" 个文件已在列表中。\n\n"
              L"「仅保存新的」= 跳过重复，只添加新文件\n"
              L"「保存并覆盖」= 添加新文件，并刷新已有条目",
          L"仅保存新的", L"保存并覆盖", L"取消");

      if (result == 0)
        return;
      if (result == 1)
        action = DupAction::AddNewOnly;
      else if (result == 2)
        action = DupAction::OverwriteExisting;
    }

    bool anythingChanged = false;
    const int firstInsertedRow =
        static_cast<int>(core.state().playlist.trackNames.size());
    int insertedRowCount = 0;
    for (const auto &f : newFiles) {
      const juce::File file(f);
      if (core.addToPlaylist(
              std::wstring(file.getFullPathName().toWideCharPointer()))) {
        anythingChanged = true;
        ++insertedRowCount;
      }
    }

    std::vector<int> overwrittenRows;
    if (action == DupAction::OverwriteExisting) {
      for (const auto &f : duplicateFiles) {
        const juce::File file(f);
        const int idx = core.findTrackIndex(
            std::wstring(file.getFullPathName().toWideCharPointer()));
        if (idx >= 0) {
          core.refreshTrack(idx);
          overwrittenRows.push_back(idx);
          anythingChanged = true;
        }
      }
    }

    if (anythingChanged) {
      if (insertedRowCount > 0)
        playlistPanel.refreshWithInsertedRows(firstInsertedRow,
                                              insertedRowCount);
      else
        playlistPanel.refresh();

      if (!overwrittenRows.empty()) {
        playlistPanel.startDropAnimation(overwrittenRows, false);
      }
    }
  }

  bool isInterestedInFileDrag(const juce::StringArray &files) override {
    for (auto &f : files)
      if (f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi"))
        return true;
    return false;
  }

  void filesDropped(const juce::StringArray &files, int, int) override {
    isDragOver = false;
    playlistFilesDropped(files);
    repaint();
  }

  void backgroundSettingsChanged(bool reapplyEffects) override {
    if (reapplyEffects)
      background.loadAsync();

    auto accentColor =
        juce::Colour::fromString(getAppSettings().getThemeAccentColor());
    fluentLookAndFeel.updateAccentColor(accentColor);
    repaint();
  }

  void backgroundSettingsClosed() override {}

  void onAccentColorChanged(juce::Colour newColor) {
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

  bool keyPressed(const juce::KeyPress &key) override {
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

  void fileDragEnter(const juce::StringArray &, int, int) override {
    isDragOver = true;
    repaint();
  }

  void fileDragExit(const juce::StringArray &) override {
    isDragOver = false;
    repaint();
  }

  void mouseMove(const juce::MouseEvent &e) override {
    if (e.eventComponent == &progressSlider)
      updateProgressTimeTooltip(e);
  }

  void mouseDrag(const juce::MouseEvent &e) override {
    if (e.eventComponent == &progressSlider)
      updateProgressTimeTooltip(e);
  }

  void mouseExit(const juce::MouseEvent &e) override {
    if (e.eventComponent == &progressSlider)
      progressTimeTooltip.hide();
  }

  void mouseDown(const juce::MouseEvent &e) override {
    if (e.eventComponent == &progressSlider) {
      updateProgressTimeTooltip(e);
      return;
    }

    playlistPanel.deselectAllRows();
  }

private:
  bool getProgressHoverInfo(const juce::MouseEvent &e, juce::String &text,
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
    const double seconds = juce::jlimit(
        0.0, durationSamples / sampleRate, ratio * durationSamples / sampleRate);

    anchorX = progressSlider.getX() + (int)std::round(clampedSliderX);
    text = formatTime((int)std::round(seconds));
    return true;
  }

  void updateProgressTimeTooltip(const juce::MouseEvent &e) {
    juce::String text;
    int anchorX = 0;
    if (!getProgressHoverInfo(e, text, anchorX)) {
      progressTimeTooltip.hide();
      return;
    }

    progressTimeTooltip.showAt(text, anchorX, progressSlider.getY(),
                               getLocalBounds());
  }

  void showExportDialog() {
      juce::StringArray trackNames;
      for (const auto &name : core.state().playlist.trackNames)
          trackNames.add(juce::String(name.c_str()));

      const int initialIndex = core.currentTrackIndex();

      auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
      auto* dlg = new ExportDialog(
          fluentLookAndFeel, trackNames, initialIndex,
          [safeThis](int selectedTrackIdx, const ExportSettings& settings) {
            if (safeThis != nullptr)
              safeThis->chooseExportTarget(selectedTrackIdx, settings);
          });

      juce::DialogWindow::LaunchOptions options;
      options.content.setOwned(dlg);
      options.dialogTitle = L"高保真离线导出";
      options.dialogBackgroundColour = fluentLookAndFeel.getColors().background;
      options.escapeKeyTriggersCloseButton = true;
      options.useNativeTitleBar = false;
      options.resizable = false;
      FluentSettingsStyle::launchDialogAsync(options);
  }

  void chooseExportTarget(int selectedTrackIndex,
                          const ExportSettings& settings) {
    auto exportDir =
        UserSettings::getSettingsDirectory().getChildFile("ExportedAudio");
    exportDir.createDirectory();
    const auto extension = getExportFileExtension(settings.formatName);
    fileChooser = std::make_unique<juce::FileChooser>(
        L"保存音频文件",
        exportDir.getChildFile(settings.title.isNotEmpty()
                                   ? settings.title + extension
                                   : "export" + extension),
        "*" + extension);

    auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode,
        [safeThis, selectedTrackIndex,
         settings](const juce::FileChooser& chooser) {
          if (safeThis == nullptr)
            return;

          auto chosenFile = chooser.getResult();
          if (chosenFile == juce::File{})
            return;

          const auto targetFile =
              normaliseExportTargetFile(chosenFile, settings.formatName);
          if (!exportTargetNeedsOverwriteConfirmation(targetFile)) {
            safeThis->performExport(selectedTrackIndex, settings, targetFile);
            return;
          }

          juce::AlertWindow::showOkCancelBox(
              juce::AlertWindow::WarningIcon, L"替换现有文件？",
              L"目标文件已经存在：\n" + targetFile.getFullPathName(),
              L"替换", L"取消", safeThis.getComponent(),
              juce::ModalCallbackFunction::create(
                  [safeThis, selectedTrackIndex, settings,
                   targetFile](int result) {
                    if (result == 1 && safeThis != nullptr)
                      safeThis->performExport(selectedTrackIndex, settings,
                                              targetFile);
                  }));
        });
  }

  void performExport(int selectedTrackIndex, const ExportSettings& settings,
                     const juce::File& targetFile) {
    auto thread =
        std::make_unique<OfflineExportThread>(core, selectedTrackIndex,
                                              targetFile, settings);
    const bool completed = thread->runThread();
    if (!completed && !thread->exportFailed)
      thread->exportCancelled = true;

    if (thread->exportFailed) {
      auto error = thread->errorMessage;
      if (error.isEmpty())
        error = L"无法创建文件或渲染引擎出现错误。";
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::WarningIcon, L"导出失败", error);
    } else if (thread->exportCancelled) {
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::InfoIcon, L"导出已取消",
          L"未写入目标音频文件。");
    } else if (thread->exportSucceeded) {
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::InfoIcon, L"导出完成",
          L"音频文件已保存到:\n" + targetFile.getFullPathName());
    }
  }

  void runLater(int delayMs, std::function<void(MainContentComponent &)> fn) {
    auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
    juce::Timer::callAfterDelay(
        delayMs, [safeThis, fn = std::move(fn)]() mutable {
          if (safeThis != nullptr)
            fn(*safeThis);
        });
  }

  void setupIconButton(juce::Button &btn, const juce::String &,
                       const juce::String &tooltip) {
    addAndMakeVisible(btn);
    btn.addListener(this);
    btn.setTooltip(tooltip);
  }

  void drawIconButton(juce::Graphics &g, juce::Button &btn,
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

  void drawIconButtonCombined(juce::Graphics &g, juce::Button &btn,
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

    g.setFont(fluentLookAndFeel.getIconFont(16.0f));
    auto mainArea = btn.getBounds().translated(-2, -1);
    g.drawText(mainIcon, mainArea, juce::Justification::centred, false);

    g.setFont(fluentLookAndFeel.getIconFont(10.0f));
    auto subArea = btn.getBounds().translated(6, 6);
    g.drawText(subIcon, subArea, juce::Justification::centred, false);
  }

  void drawSequentialIcon(juce::Graphics &g, juce::Button &btn) {
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
      if (sequentialIconDrawable != nullptr) {
        if (lastSequentialIconColor != iconColor) {
          sequentialIconDrawable->replaceColour(lastSequentialIconColor, iconColor);
          lastSequentialIconColor = iconColor;
        }
        auto r = btn.getBounds().toFloat();
        auto iconBounds = r.withSizeKeepingCentre(14.0f, 14.0f);
        sequentialIconDrawable->drawWithin(g, iconBounds, juce::RectanglePlacement::centred, 1.0f);
      }
    }
  }

  void drawPlayButton(juce::Graphics &g, juce::Button &btn, bool isPlaying) {
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

  void showPage(const juce::String &pageId, const juce::String &title) {
    currentPage = pageId;
    pageTitle.setText(title, juce::dontSendNotification);
    navigation.setSelectedItem(pageId);
    resized();
  }

  void startPluginScan() {
    if (isScanningPlugins)
      return;

    // 插件扫描目前不提供单插件进度，进度窗口以不确定状态显示。
    class ScanThread : public juce::ThreadWithProgressWindow {
    public:
      ScanThread(midi::Core &c)
          : juce::ThreadWithProgressWindow(L"扫描 VST3 插件...", true, true),
            core(c) {}

      void run() override {
        setProgress(-1.0);
        scanSucceeded = core.scan([this] { return threadShouldExit(); });
      }

      midi::Core &core;
      bool scanSucceeded = false;
    };

    isScanningPlugins = true;
    pluginSelector.setEnabled(false);

    auto scanner = std::make_unique<ScanThread>(core);
    const bool completed = scanner->runThread();
    if (completed && scanner->scanSucceeded) {
      updatePluginList();
    } else if (completed) {
      showOperationError(L"插件扫描失败", core.lastPluginError());
    }

    isScanningPlugins = false;
    pluginSelector.setEnabled(true);
  }

  void confirmUnloadPlugin() {
    if (!core.hasPluginLoaded())
      return;

    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::QuestionIcon, L"确认卸载", L"确定要卸载当前插件吗？",
        L"卸载", L"取消", this,
        juce::ModalCallbackFunction::create(
            [safeThis = juce::Component::SafePointer<MainContentComponent>(
                 this)](int result) {
              if (result != 0 && safeThis != nullptr)
                safeThis->unloadPlugin();
        }));
  }

public:
  bool hasUnsavedChanges() const {
    return core.state().playlist.hasUnsavedChanges;
  }

  juce::String getPlaylistChangeSummary() const {
    return juce::String(core.state().playlist.changeSummary.c_str());
  }

  bool savePlaylist() {
    if (currentPlaylistFile.existsAsFile()) {
      const bool saved = core.saveList(
          std::wstring(currentPlaylistFile.getFullPathName().toWideCharPointer()));
      if (!saved)
        showOperationError(L"保存播放列表失败", core.lastPlaylistError());
      return saved;
    } else {
      return savePlaylistAs();
    }
  }

  bool savePlaylistAs() {
    fileChooser = std::make_unique<juce::FileChooser>(
        L"保存播放列表",
        juce::File(getAppSettings().getLastMidiDirectory())
            .getChildFile("playlist.json"),
        "*.json");

    // 关闭窗口路径需要同步得到保存结果。
    if (fileChooser->browseForFileToSave(true)) {
      auto targetFile = fileChooser->getResult().withFileExtension(".json");
      const bool saved = core.saveList(
          std::wstring(targetFile.getFullPathName().toWideCharPointer()));
      if (saved) {
        currentPlaylistFile = targetFile;
      } else {
        showOperationError(L"保存播放列表失败", core.lastPlaylistError());
      }
      return saved;
    }
    return false;
  }

  void showOperationError(const juce::String &title,
                          const std::wstring &message) {
    const auto text = message.empty() ? juce::String(L"操作失败。")
                                      : juce::String(message.c_str());
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon, title, text, L"确定", this);
  }

  void unloadPlugin() {
    beginPluginSwitch();
    playbackPausedByPluginSwitch = false;
    pluginWorkerCrashAlertShown = false;
    core.unload();
    pluginSelector.setSelectedId(0, juce::dontSendNotification);
    openPluginBtn.setEnabled(false);
    unloadBtn.setEnabled(false);
    contentLabel.setText(L"选择一个 VST3 乐器插件开始演奏",
                         juce::dontSendNotification);

    progressSlider.setValue(0.0, juce::dontSendNotification);
    progressSlider.setEnabled(false);
    timeLabel.setText("0:00 / 0:00", juce::dontSendNotification);
  }

  int beginPluginSwitch() {
    const bool wasPlayingBeforeSwitch = core.state().transport.playing;
    playbackPausedByPluginSwitch = shouldShowPluginSwitchPausedNotice(
        wasPlayingBeforeSwitch, false);
    core.pause();

    return pluginLifecycle.beginSwitch();
  }

  void finishPluginLoadUi(const midi::PluginInfo &plugin) {
    const juce::String pluginName(plugin.name.c_str());
    pluginWorkerCrashAlertShown = false;
    openPluginBtn.setEnabled(true);
    unloadBtn.setEnabled(true);
    contentLabel.setText(
        makeLoadedPluginLabel(pluginName, playbackPausedByPluginSwitch),
        juce::dontSendNotification);
    playbackPausedByPluginSwitch = false;
    getAppSettings().setLastPluginId(juce::String(plugin.id.c_str()));
  }

  void showPluginLoadSuccessToast(const juce::String &pluginName) {
    const auto title = midi::makePluginLoadSuccessToastTitle(
        std::wstring(pluginName.toWideCharPointer()));
    modeToast.showTopCenter(juce::String(title.c_str()), getLocalBounds(),
                            midi::pluginLoadSuccessToastDurationMs);
  }

  void loadPluginInfo(const midi::PluginInfo &plugin, bool openEditorAfterLoad,
                      int delayMs) {
    if (pluginLoadInProgress)
      return;

    pluginLoadInProgress = true;
    pluginSelector.setEnabled(false);
    const int switchGeneration = beginPluginSwitch();

    const juce::String pluginName(plugin.name.c_str());
    auto *loadingWindow = new juce::AlertWindow(
        L"正在加载乐器", L"正在加载插件: " + pluginName + L"\n请稍候...",
        juce::MessageBoxIconType::InfoIcon);
    auto safeLoadingWindow =
        juce::Component::SafePointer<juce::AlertWindow>(loadingWindow);
    loadingWindow->enterModalState(false, nullptr, true);

    auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
    juce::Timer::callAfterDelay(
        delayMs, [safeThis, plugin, openEditorAfterLoad, switchGeneration,
                  safeLoadingWindow]() {
      if (safeThis == nullptr) {
        if (safeLoadingWindow != nullptr)
          safeLoadingWindow->exitModalState(0);
        return;
      }

      if (!safeThis->pluginLifecycle.isGenerationCurrent(switchGeneration)) {
        safeThis->pluginLoadInProgress = false;
        safeThis->pluginSelector.setEnabled(!safeThis->isScanningPlugins);
        if (safeLoadingWindow != nullptr)
          safeLoadingWindow->exitModalState(0);
        return;
      }

      bool showLoadFailure = false;
      bool showLoadSuccessToast = false;
      juce::String loadFailureMessage;

      if (safeThis->core.load(plugin.id)) {
        safeThis->finishPluginLoadUi(plugin);
        showLoadSuccessToast = true;
        if (openEditorAfterLoad)
          safeThis->openPluginWindow();
      } else {
        const bool stillHasPlugin = safeThis->core.hasPluginLoaded();
        safeThis->playbackPausedByPluginSwitch = false;
        safeThis->openPluginBtn.setEnabled(stillHasPlugin);
        safeThis->unloadBtn.setEnabled(stillHasPlugin);
        if (!stillHasPlugin)
          safeThis->pluginSelector.setSelectedId(0, juce::dontSendNotification);

        loadFailureMessage = juce::String(safeThis->core.lastPluginError().c_str());
        if (loadFailureMessage.isEmpty())
          loadFailureMessage = L"插件加载失败。";
        showLoadFailure = true;

        if (safeThis->core.workerCrashed()) {
          safeThis->handlePluginWorkerCrash(false);
        }
      }

      if (safeLoadingWindow != nullptr)
        safeLoadingWindow->exitModalState(0);

      safeThis->pluginLoadInProgress = false;
      safeThis->pluginSelector.setEnabled(!safeThis->isScanningPlugins);

      if (showLoadSuccessToast)
        safeThis->showPluginLoadSuccessToast(juce::String(plugin.name.c_str()));

      if (showLoadFailure) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, L"插件加载失败",
            loadFailureMessage);
      }
    });
  }

  void loadSelectedPlugin() {
    if (isScanningPlugins)
      return;

    int idx = pluginSelector.getSelectedItemIndex();
    const auto plugins = core.plugins();
    if (idx >= 0 && idx < static_cast<int>(plugins.size())) {
      const auto &plugin = plugins[static_cast<size_t>(idx)];
      loadPluginInfo(
          plugin,
          shouldAutoOpenPluginEditorAfterLoad(juce::String(plugin.name.c_str())),
          10);
    }
  }

  void openPluginWindow() {
    if (!core.hasPluginLoaded())
      return;

    const int requestGeneration = pluginLifecycle.getGeneration();
    auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
    const auto pluginName = juce::String(core.loadedPluginName().c_str());
    const int editorDelayMs = getPluginEditorOpenDelayMs(pluginName);
    LOG_DEBUG("Scheduling plugin editor for " + pluginName + " in " +
              juce::String(editorDelayMs) + " ms");

    juce::Timer::callAfterDelay(editorDelayMs, [safeThis, requestGeneration]() {
      if (safeThis == nullptr)
        return;

      if (!safeThis->pluginLifecycle.isGenerationCurrent(requestGeneration) ||
          !safeThis->core.hasPluginLoaded())
        return;

      if (!safeThis->core.editor()) {
        if (safeThis->core.workerCrashed()) {
          safeThis->handlePluginWorkerCrash(true);
          return;
        }

        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, L"插件窗口打开失败",
            juce::String(safeThis->core.lastPluginError().c_str()));
      }
    });
  }

  void closePluginWindow() {
    core.closeEditor();
    pluginLifecycle.closeWindow();
  }

  void handlePluginWorkerCrash(bool showAlert) {
    core.pause();
    openPluginBtn.setEnabled(false);
    unloadBtn.setEnabled(false);
    contentLabel.setText(L"插件进程已崩溃，请重新加载插件",
                         juce::dontSendNotification);
    queueCrashedWorkerTermination();

    if (!showAlert || pluginWorkerCrashAlertShown)
      return;

    pluginWorkerCrashAlertShown = true;
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon, L"插件进程已崩溃",
        juce::String(core.pluginError().c_str()));
  }

  void queueCrashedWorkerTermination() {
    if (crashedWorkerTerminationQueued)
      return;

    crashedWorkerTerminationQueued = true;
    auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
    juce::Timer::callAfterDelay(250, [safeThis]() {
      if (safeThis == nullptr)
        return;

      if (safeThis->core.workerCrashed())
        safeThis->core.terminateCrashedWorker();

      safeThis->crashedWorkerTerminationQueued = false;
    });
  }

  void togglePlayPause() {
    playbackPausedByPluginSwitch = false;
    core.togglePlay();
  }

  void stopPlayback() {
    playbackPausedByPluginSwitch = false;
    core.stop();
  }

  void playNextTrack() {
    SCOPED_TIMER_ALWAYS("MainContentComponent::playNextTrack");
    core.next();
    playlistPanel.setCurrentTrackIndex(core.currentTrackIndex());
  }

  void playPreviousTrack() {
    SCOPED_TIMER_ALWAYS("MainContentComponent::playPreviousTrack");
    core.prev();
    playlistPanel.setCurrentTrackIndex(core.currentTrackIndex());
  }

  void showOpenFileDialog() {
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

#if JUCE_WINDOWS
  bool isFileAssociatedToSelf() { return isMidiFileAssociatedToSelf(); }

  bool registerFileAssociation() { return registerMidiFileAssociation(); }

  void removeFileAssociation() { removeMidiFileAssociation(); }
#endif // JUCE_WINDOWS

  std::unique_ptr<juce::ToggleButton> createDontShowAgainToggle() {
    auto toggle =
        std::make_unique<juce::ToggleButton>(L"\u4e0d\u518d\u63d0\u793a");
    toggle->setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    toggle->setSize(200, 24);
    return toggle;
  }

  void showFileAssociationPrompt() {
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
          } else {
            if (dontShowAgain) {
              getAppSettings().setDontShowFileAssocPrompt(true);
              getAppSettings().save();
            }
          }
        }),
        true);
#endif
  }

  // Shell-open 文件进入播放列表；已有插件时延迟播放，无插件时只尝试加载上次插件。
  void openMidiFileFromShell(const juce::File &file) {
    showPage("playlist", L"\u97F3\u4E50\u5217\u8868");

    core.openMidiFromShell(
        std::wstring(file.getFullPathName().toWideCharPointer()),
        [safeThis = juce::Component::SafePointer<MainContentComponent>(
                  this)]() {
          if (safeThis != nullptr)
            safeThis->tryLoadLastPluginWithDialog();
        });

    playlistPanel.refresh();
    playlistPanel.setCurrentTrackIndex(core.currentTrackIndex());

    pendingShellOpen = false;
  }

  void setPendingShellOpen(bool pending) { pendingShellOpen = pending; }

  // 自动插件加载只打开插件窗口，不自动播放；部分乐器插件需要先加载音色。
  void tryLoadLastPluginWithDialog() {
    if (!core.hasAudioDevice()) {
      juce::AlertWindow::showOkCancelBox(
          juce::AlertWindow::WarningIcon, L"音频设备不可用",
          L"未检测到可用的音频输出设备，无法加载乐器插件。"
          L"\n\n请先在音频设置中配置输出设备。",
          L"打开设置", L"取消", nullptr,
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
          juce::AlertWindow::WarningIcon, L"无法播放",
          L"请先加载一个乐器插件以开始播放。");
      return;
    }

    const auto plugins = core.plugins();
    int pluginIndex = -1;
    for (int i = 0; i < static_cast<int>(plugins.size()); ++i) {
      if (plugins[static_cast<size_t>(i)].id ==
          std::wstring(lastPluginId.toWideCharPointer())) {
        pluginIndex = i;
        break;
      }
    }

    if (pluginIndex < 0) {
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::WarningIcon, L"无法播放",
          L"上次使用的乐器插件未找到，请手动加载一个乐器插件。");
      return;
    }

    const auto &plugin = plugins[static_cast<size_t>(pluginIndex)];
    pluginSelector.setSelectedId(pluginIndex + 1, juce::dontSendNotification);
    loadPluginInfo(
        plugin,
        shouldAutoOpenPluginEditorAfterLoad(juce::String(plugin.name.c_str())),
        100);
  }

  void showAudioSettings() {
    audioSettingsWindow.deleteAndZero();
    auto *content = new AudioSettingsContent(engine, fluentLookAndFeel);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(content);
    options.dialogTitle = L"音频设置";
    options.dialogBackgroundColour =
        fluentLookAndFeel.getColors().cardBackground;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = false;
    audioSettingsWindow = FluentSettingsStyle::launchDialogAsync(options);
  }

  void showBackgroundSettings() {
    backgroundSettingsWindow.deleteAndZero();
    auto *content =
        new BackgroundSettingsDialog(background, this, fluentLookAndFeel);
    content->setLookAndFeel(&fluentLookAndFeel);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(content);
    options.dialogTitle = L"背景设置";
    options.dialogBackgroundColour =
        fluentLookAndFeel.getColors().cardBackground;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = false;
    backgroundSettingsWindow =
        FluentSettingsStyle::launchDialogAsync(options);
  }

  void showFontSettings() {
    fontSettingsWindow.deleteAndZero();
    auto *content = new FontSettingsContent(fluentLookAndFeel);
    content->setLookAndFeel(&fluentLookAndFeel);
    content->onSettingsChanged =
        [safeThis = juce::Component::SafePointer<MainContentComponent>(this)]() {
          if (safeThis == nullptr)
            return;

          safeThis->applyConfiguredFonts();
          safeThis->playlistPanel.refresh();
          safeThis->playlistPanel.repaint();
          safeThis->repaint();
    };

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(content);
    options.dialogTitle = L"字体设置";
    options.dialogBackgroundColour =
        fluentLookAndFeel.getColors().cardBackground;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = false;
    fontSettingsWindow = FluentSettingsStyle::launchDialogAsync(options);
  }

  void applyConfiguredFonts() {
    pageTitle.setFont(fluentLookAndFeel.getDefaultFont(26.0f, true));
    contentLabel.setFont(fluentLookAndFeel.getDefaultFont(16.0f));
    trackLabel.setFont(fluentLookAndFeel.getDefaultFont(15.0f, true));
    timeLabel.setFont(fluentLookAndFeel.getDefaultFont(13.0f));
  }

  void closeSettingsWindows() {
    audioSettingsWindow.deleteAndZero();
    backgroundSettingsWindow.deleteAndZero();
    fontSettingsWindow.deleteAndZero();
  }

  void updatePluginList() {
    juce::String idToRestore;
    if (core.hasPluginLoaded()) {
      idToRestore = getAppSettings().getLastPluginId();
    }

    pluginSelector.clear(juce::dontSendNotification);
    const auto plugins = core.plugins();

    int idToSelect = 0;
    for (int i = 0; i < static_cast<int>(plugins.size()); ++i) {
      const auto &plugin = plugins[static_cast<size_t>(i)];
      pluginSelector.addItem(juce::String(plugin.name.c_str()), i + 1);

      if (idToRestore.isNotEmpty() &&
          plugin.id == std::wstring(idToRestore.toWideCharPointer())) {
        idToSelect = i + 1;
      }
    }

    if (idToSelect > 0) {
      pluginSelector.setSelectedId(idToSelect, juce::dontSendNotification);
    }
  }

  void tryLoadLastPlugin() {
    auto lastPluginId = getAppSettings().getLastPluginId();
    if (lastPluginId.isEmpty())
      return;

    const auto plugins = core.plugins();
    for (int i = 0; i < static_cast<int>(plugins.size()); ++i) {
      const auto &plugin = plugins[static_cast<size_t>(i)];
      if (plugin.id == std::wstring(lastPluginId.toWideCharPointer())) {
        pluginSelector.setSelectedId(i + 1, juce::dontSendNotification);
        beginPluginSwitch();
        if (core.load(plugin.id)) {
          finishPluginLoadUi(plugin);
          const juce::String pluginName(plugin.name.c_str());
          showPluginLoadSuccessToast(pluginName);
          DBG("Auto-loaded last plugin: " + pluginName);
        }
        return;
      }
    }
  }

  juce::String formatTime(int seconds) {
    return juce::String(seconds / 60) + ":" +
           juce::String(seconds % 60).paddedLeft('0', 2);
  }

  void loadSettings() {
    auto &settings = getAppSettings();
    volumeSlider.setValue(settings.getMasterVolume(),
                          juce::dontSendNotification);
    core.volume(settings.getMasterVolume());

    int savedMode = settings.getPlayMode();
    core.setPlayMode(savedMode);

    fluentLookAndFeel.setUIFont(settings.getUIFontName());
    fluentLookAndFeel.setPlaylistFont(settings.getPlaylistFontName());

    playlistPanel.refresh();
  }

  void saveSettings() {
    getAppSettings().setMasterVolume((float)volumeSlider.getValue());
    getAppSettings().setPlayMode(core.state().playlist.playMode);
    getAppSettings().save();
  }

  struct AudioSettingsContent : public juce::Component,
                                private juce::ChangeListener,
                                private juce::Timer {
    AudioSettingsContent(AudioEngine &e, FluentLookAndFeel &laf)
        : engine(e), fluentLookAndFeel(laf),
          deviceManager(e.getDeviceManager()) {
      setLookAndFeel(&fluentLookAndFeel);
      setSize(660, 350);
      setOpaque(false);

      addAndMakeVisible(sectionLabel);
      sectionLabel.setText(L"输出设备", juce::dontSendNotification);
      FluentSettingsStyle::configureLabel(sectionLabel, fluentLookAndFeel,
                                          true);

      configureField(driverLabel, L"驱动类型", driverBox);
      configureField(deviceLabel, L"输出设备", deviceBox);
      configureField(sampleRateLabel, L"采样率", sampleRateBox);
      configureField(bufferSizeLabel, L"缓冲大小", bufferSizeBox);
      configureField(channelPairLabel, L"输出通道", channelPairBox);

      addAndMakeVisible(statusTitleLabel);
      addAndMakeVisible(statusDetailLabel);
      FluentSettingsStyle::configureLabel(statusTitleLabel,
                                          fluentLookAndFeel, true);
      FluentSettingsStyle::configureLabel(statusDetailLabel,
                                          fluentLookAndFeel, false, true);

      addAndMakeVisible(controlPanelButton);
      controlPanelButton.setButtonText(L"设备控制面板");
      controlPanelButton.onClick = [this]() {
        if (auto *device = deviceManager.getCurrentAudioDevice()) {
          if (device->hasControlPanel() && device->showControlPanel()) {
            deviceManager.closeAudioDevice();
            deviceManager.restartLastAudioDevice();
          }
        }
        refreshControls(false);
      };

      addAndMakeVisible(testButton);
      testButton.setButtonText(L"播放测试音");
      testButton.onClick = [this]() { deviceManager.playTestSound(); };

      driverBox.onChange = [this]() {
        if (refreshing || deviceChangePending)
          return;
        const int index = driverBox.getSelectedItemIndex();
        const auto &types = deviceManager.getAvailableDeviceTypes();
        if (juce::isPositiveAndBelow(index, types.size())) {
          const auto typeName = types[index]->getTypeName();
          beginDeviceChange([this, typeName]() {
            deviceManager.setCurrentAudioDeviceType(typeName, true);
            if (deviceManager.getCurrentAudioDeviceType() != typeName)
              return juce::String(L"无法切换到所选音频驱动");
            return juce::String();
          }, true);
        }
      };

      deviceBox.onChange = [this]() {
        if (refreshing || deviceChangePending)
          return;
        const int index = deviceBox.getSelectedItemIndex();
        if (!juce::isPositiveAndBelow(index, outputDevices.size()))
          return;

        auto setup = deviceManager.getAudioDeviceSetup();
        setup.outputDeviceName = outputDevices[index];
        setup.useDefaultOutputChannels = true;
        if (auto *type = deviceManager.getCurrentDeviceTypeObject();
            type != nullptr && !type->hasSeparateInputsAndOutputs())
          setup.inputDeviceName = setup.outputDeviceName;
        applySetup(setup);
      };

      sampleRateBox.onChange = [this]() {
        if (refreshing || deviceChangePending)
          return;
        const int index = sampleRateBox.getSelectedItemIndex();
        if (!juce::isPositiveAndBelow(index, sampleRates.size()))
          return;
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.sampleRate = sampleRates.getUnchecked(index);
        applySetup(setup);
      };

      bufferSizeBox.onChange = [this]() {
        if (refreshing || deviceChangePending)
          return;
        const int index = bufferSizeBox.getSelectedItemIndex();
        if (!juce::isPositiveAndBelow(index, bufferSizes.size()))
          return;
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.bufferSize = bufferSizes.getUnchecked(index);
        applySetup(setup);
      };

      channelPairBox.onChange = [this]() {
        if (refreshing || deviceChangePending)
          return;
        const int index = channelPairBox.getSelectedItemIndex();
        if (!juce::isPositiveAndBelow(index, channelStarts.size()))
          return;
        auto setup = deviceManager.getAudioDeviceSetup();
        setup.useDefaultOutputChannels = false;
        setup.outputChannels = makeStereoOutputMask(
            outputChannelCount, channelStarts.getUnchecked(index));
        applySetup(setup);
      };

      deviceManager.addChangeListener(this);
      refreshControls(true);
    }

    ~AudioSettingsContent() override {
      stopTimer();
      deviceManager.removeChangeListener(this);
      engine.saveAudioDeviceSettings();
      setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics &g) override {
      FluentSettingsStyle::paintPanel(g, fluentLookAndFeel);
      FluentSettingsStyle::paintCard(g, fluentLookAndFeel,
                                     deviceCardBounds);
      FluentSettingsStyle::paintCard(g, fluentLookAndFeel,
                                     statusCardBounds);

      const auto &colors = fluentLookAndFeel.getColors();
      const auto phase = statusAnimation.getPhase();
      if (phase == AudioStatusAnimation::Phase::busy) {
        const auto indicator = statusIndicator.toFloat().expanded(2.0f);
        g.setColour(colors.accentPrimary.withAlpha(0.18f));
        g.drawEllipse(indicator, 2.0f);

        const float start =
            statusAnimation.getBusyRotation() *
            juce::MathConstants<float>::twoPi;
        juce::Path spinner;
        spinner.addCentredArc(
            indicator.getCentreX(), indicator.getCentreY(),
            indicator.getWidth() * 0.5f, indicator.getHeight() * 0.5f, 0.0f,
            start, start + juce::MathConstants<float>::pi * 1.35f, true);
        g.setColour(colors.accentPrimary);
        g.strokePath(spinner, juce::PathStrokeType(
                                  2.0f, juce::PathStrokeType::curved,
                                  juce::PathStrokeType::rounded));
      } else {
        const auto indicatorColour =
            lastError.isNotEmpty()
                ? juce::Colour(0xffd95555)
                : (deviceManager.getCurrentAudioDevice() != nullptr
                       ? juce::Colour(0xff61c454)
                       : colors.textDisabled);
        const float glow = statusAnimation.getSuccessGlow();
        if (glow > 0.0f) {
          g.setColour(indicatorColour.withAlpha(glow * 0.28f));
          g.fillEllipse(statusIndicator.toFloat().expanded(7.0f * glow));
        }
        g.setColour(indicatorColour);
        g.fillEllipse(statusIndicator.toFloat());
      }
    }

    void resized() override {
      auto area =
          getLocalBounds().reduced(FluentSettingsStyle::panelMargin);
      deviceCardBounds = area.removeFromTop(234);
      area.removeFromTop(12);
      statusCardBounds = area;

      auto content =
          deviceCardBounds.reduced(FluentSettingsStyle::cardPadding);
      sectionLabel.setBounds(content.removeFromTop(24));
      content.removeFromTop(12);

      layoutField(content.removeFromTop(34), driverLabel, driverBox);
      content.removeFromTop(10);
      layoutField(content.removeFromTop(34), deviceLabel, deviceBox);
      content.removeFromTop(10);

      auto timingRow = content.removeFromTop(34);
      auto sampleArea = timingRow.removeFromLeft(timingRow.getWidth() / 2 - 6);
      timingRow.removeFromLeft(12);
      layoutField(sampleArea, sampleRateLabel, sampleRateBox);
      layoutField(timingRow, bufferSizeLabel, bufferSizeBox);
      content.removeFromTop(10);
      layoutField(content.removeFromTop(34), channelPairLabel,
                  channelPairBox);

      auto status =
          statusCardBounds.reduced(FluentSettingsStyle::cardPadding);
      statusIndicator =
          juce::Rectangle<int>(status.getX(), status.getCentreY() - 5, 10, 10);

      auto buttons = status;
      auto testBounds = buttons.removeFromRight(112);
      testBounds = testBounds.withSizeKeepingCentre(112, 34);
      testButton.setBounds(testBounds);
      buttons.removeFromRight(8);
      if (controlPanelButton.isVisible()) {
        auto controlBounds = buttons.removeFromRight(126);
        controlBounds = controlBounds.withSizeKeepingCentre(126, 34);
        controlPanelButton.setBounds(controlBounds);
        buttons.removeFromRight(8);
      } else {
        controlPanelButton.setBounds({});
      }

      auto statusText = buttons;
      statusText.removeFromLeft(22);
      statusTitleLabel.setBounds(statusText.removeFromTop(24));
      statusDetailLabel.setBounds(statusText.removeFromTop(24));
    }

  private:
    void configureField(juce::Label &label, const juce::String &text,
                        juce::ComboBox &box) {
      addAndMakeVisible(label);
      label.setText(text, juce::dontSendNotification);
      FluentSettingsStyle::configureLabel(label, fluentLookAndFeel);

      addAndMakeVisible(box);
      box.setJustificationType(juce::Justification::centredLeft);
      box.setTextWhenNothingSelected(L"请选择");
      box.setTextWhenNoChoicesAvailable(L"无可用选项");
    }

    static void layoutField(juce::Rectangle<int> area, juce::Label &label,
                            juce::ComboBox &box, int labelWidth = 82) {
      label.setBounds(area.removeFromLeft(labelWidth));
      area.removeFromLeft(10);
      box.setBounds(area);
    }

    void applySetup(const juce::AudioDeviceManager::AudioDeviceSetup &setup) {
      beginDeviceChange(
          [this, setup]() {
            return deviceManager.setAudioDeviceSetup(setup, true);
          },
          false);
    }

    void changeListenerCallback(juce::ChangeBroadcaster *) override {
      refreshControls(false);
    }

    void beginDeviceChange(std::function<juce::String()> operation,
                           bool rescanDevices) {
      if (deviceChangePending)
        return;

      deviceChangePending = true;
      statusAnimation.beginBusy();
      statusTitleLabel.setText(L"正在切换音频设备",
                               juce::dontSendNotification);
      statusDetailLabel.setText(L"正在重新初始化音频引擎",
                                juce::dontSendNotification);
      updateControlEnabledState();
      startTimerHz(60);
      repaint(statusCardBounds);

      auto safeThis =
          juce::Component::SafePointer<AudioSettingsContent>(this);
      juce::Timer::callAfterDelay(
          45, [safeThis, operation = std::move(operation),
               rescanDevices]() mutable {
            if (safeThis == nullptr)
              return;

            const auto error = operation();
            safeThis->finishDeviceChange(error, rescanDevices);
          });
    }

    void finishDeviceChange(const juce::String &error,
                            bool rescanDevices) {
      lastError = error;
      deviceChangePending = false;
      if (lastError.isEmpty())
        engine.saveAudioDeviceSettings();
      refreshControls(rescanDevices);
      statusAnimation.complete(lastError.isEmpty());
      startTimerHz(60);
      repaint(statusCardBounds);
    }

    void timerCallback() override {
      const bool active = statusAnimation.tick();
      const auto transform = juce::AffineTransform::translation(
          statusAnimation.getShakeOffset(), 0.0f);
      statusTitleLabel.setTransform(transform);
      statusDetailLabel.setTransform(transform);
      repaint(statusCardBounds);

      if (!active) {
        statusTitleLabel.setTransform(juce::AffineTransform());
        statusDetailLabel.setTransform(juce::AffineTransform());
        stopTimer();
      }
    }

    void refreshControls(bool rescanDevices) {
      juce::ScopedValueSetter<bool> guard(refreshing, true);

      const auto &types = deviceManager.getAvailableDeviceTypes();
      driverBox.clear(juce::dontSendNotification);
      int selectedDriver = 0;
      for (int i = 0; i < types.size(); ++i) {
        driverBox.addItem(types[i]->getTypeName(), i + 1);
        if (types[i]->getTypeName() ==
            deviceManager.getCurrentAudioDeviceType())
          selectedDriver = i + 1;
      }
      driverBox.setSelectedId(selectedDriver,
                              juce::dontSendNotification);

      auto *type = deviceManager.getCurrentDeviceTypeObject();
      if (type != nullptr && rescanDevices)
        type->scanForDevices();

      outputDevices =
          type != nullptr ? type->getDeviceNames(false) : juce::StringArray{};
      deviceBox.clear(juce::dontSendNotification);
      for (int i = 0; i < outputDevices.size(); ++i)
        deviceBox.addItem(outputDevices[i], i + 1);

      const auto setup = deviceManager.getAudioDeviceSetup();
      const int selectedDevice =
          outputDevices.indexOf(setup.outputDeviceName);
      deviceBox.setSelectedId(selectedDevice >= 0 ? selectedDevice + 1 : 0,
                              juce::dontSendNotification);

      sampleRates.clear();
      bufferSizes.clear();
      channelStarts.clear();
      sampleRateBox.clear(juce::dontSendNotification);
      bufferSizeBox.clear(juce::dontSendNotification);
      channelPairBox.clear(juce::dontSendNotification);

      auto *device = deviceManager.getCurrentAudioDevice();
      outputChannelCount = 0;
      if (device != nullptr) {
        sampleRates = device->getAvailableSampleRates();
        for (int i = 0; i < sampleRates.size(); ++i)
          sampleRateBox.addItem(
              formatAudioSampleRate(sampleRates.getUnchecked(i)), i + 1);
        sampleRateBox.setText(
            formatAudioSampleRate(device->getCurrentSampleRate()),
            juce::dontSendNotification);

        bufferSizes = device->getAvailableBufferSizes();
        for (int i = 0; i < bufferSizes.size(); ++i)
          bufferSizeBox.addItem(
              formatAudioBufferSize(bufferSizes.getUnchecked(i),
                                    device->getCurrentSampleRate()),
              i + 1);
        bufferSizeBox.setText(
            formatAudioBufferSize(device->getCurrentBufferSizeSamples(),
                                  device->getCurrentSampleRate()),
            juce::dontSendNotification);

        const auto channelNames = device->getOutputChannelNames();
        outputChannelCount = channelNames.size();
        const auto activeChannels = device->getActiveOutputChannels();
        const int activeFirst = activeChannels.findNextSetBit(0);

        if (outputChannelCount == 1) {
          channelStarts.add(0);
          channelPairBox.addItem("1  " + channelNames[0], 1);
        } else {
          for (int first = 0; first + 1 < outputChannelCount; first += 2) {
            channelStarts.add(first);
            channelPairBox.addItem(
                juce::String(first + 1) + "-" + juce::String(first + 2) +
                    "  " + channelNames[first] + " / " +
                    channelNames[first + 1],
                channelStarts.size());
          }
        }

        int selectedPair = 0;
        for (int i = 0; i < channelStarts.size(); ++i) {
          const int first = channelStarts.getUnchecked(i);
          if (activeFirst == first ||
              (activeFirst >= first &&
               activeFirst <= first + (outputChannelCount > 1 ? 1 : 0))) {
            selectedPair = i + 1;
            break;
          }
        }
        channelPairBox.setSelectedId(selectedPair,
                                     juce::dontSendNotification);

        if (!deviceChangePending)
          statusTitleLabel.setText(L"设备已连接",
                                   juce::dontSendNotification);
        const auto latency =
            formatAudioBufferSize(device->getOutputLatencyInSamples(),
                                  device->getCurrentSampleRate());
        if (!deviceChangePending)
          statusDetailLabel.setText(
              juce::String(device->getCurrentBitDepth()) +
                  "-bit  |  输出延迟 " + latency,
              juce::dontSendNotification);
        statusDetailLabel.setTooltip(
            device->getName() + "  |  " +
            formatAudioSampleRate(device->getCurrentSampleRate()) +
            "  |  " + juce::String(device->getCurrentBitDepth()) +
            "-bit  |  输出延迟 " + latency);
      } else if (!deviceChangePending) {
        statusTitleLabel.setText(
            lastError.isNotEmpty() ? L"设备切换失败" : L"未连接输出设备",
            juce::dontSendNotification);
        statusDetailLabel.setText(
            lastError.isNotEmpty() ? lastError
                                   : L"请选择驱动类型和输出设备",
            juce::dontSendNotification);
      }

      updateControlEnabledState();
      controlPanelButton.setVisible(
          device != nullptr && device->hasControlPanel());
      repaint();
    }

    void updateControlEnabledState() {
      auto *type = deviceManager.getCurrentDeviceTypeObject();
      auto *device = deviceManager.getCurrentAudioDevice();
      const bool canInteract = !deviceChangePending;
      driverBox.setEnabled(canInteract);
      deviceBox.setEnabled(canInteract && type != nullptr &&
                           !outputDevices.isEmpty());
      sampleRateBox.setEnabled(canInteract && device != nullptr &&
                               !sampleRates.isEmpty());
      bufferSizeBox.setEnabled(canInteract && device != nullptr &&
                               !bufferSizes.isEmpty());
      channelPairBox.setEnabled(canInteract && device != nullptr &&
                                !channelStarts.isEmpty());
      testButton.setEnabled(canInteract && device != nullptr);
      controlPanelButton.setEnabled(canInteract && device != nullptr);
    }

    AudioEngine &engine;
    FluentLookAndFeel &fluentLookAndFeel;
    juce::AudioDeviceManager &deviceManager;

    juce::Label sectionLabel;
    juce::Label driverLabel, deviceLabel, sampleRateLabel, bufferSizeLabel;
    juce::Label channelPairLabel, statusTitleLabel, statusDetailLabel;
    juce::ComboBox driverBox, deviceBox, sampleRateBox, bufferSizeBox;
    juce::ComboBox channelPairBox;
    juce::TextButton controlPanelButton, testButton;

    juce::StringArray outputDevices;
    juce::Array<double> sampleRates;
    juce::Array<int> bufferSizes;
    juce::Array<int> channelStarts;
    juce::String lastError;
    int outputChannelCount = 0;
    bool refreshing = false;
    bool deviceChangePending = false;
    AudioStatusAnimation statusAnimation;

    juce::Rectangle<int> deviceCardBounds;
    juce::Rectangle<int> statusCardBounds;
    juce::Rectangle<int> statusIndicator;
  };

  struct FontSettingsContent : public juce::Component {
    std::function<void()> onSettingsChanged;

    FontSettingsContent(FluentLookAndFeel &laf) : fluentLookAndFeel(laf) {
      setLookAndFeel(&fluentLookAndFeel);
      setSize(500, 320);
      setOpaque(false);

      availableFonts = juce::Font::findAllTypefaceNames();

      // 尽量把保存的播放列表字体解析到当前系统已安装字体。
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
      FluentSettingsStyle::configureLabel(interfaceSectionLabel,
                                          fluentLookAndFeel, true);

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
          if (onSettingsChanged)
            onSettingsChanged();
        }
      };

      addAndMakeVisible(playlistSectionLabel);
      playlistSectionLabel.setText(L"播放列表", juce::dontSendNotification);
      FluentSettingsStyle::configureLabel(playlistSectionLabel,
                                          fluentLookAndFeel, true);

      addAndMakeVisible(playlistFontLabel);
      playlistFontLabel.setText(L"字体", juce::dontSendNotification);
      FluentSettingsStyle::configureLabel(playlistFontLabel,
                                          fluentLookAndFeel);

      addAndMakeVisible(playlistFontCombo);
      populateFontCombo(playlistFontCombo,
                        getAppSettings().getPlaylistFontName(), true);
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

      addAndMakeVisible(fontSizeLabel);
      fontSizeLabel.setText(L"字号", juce::dontSendNotification);
      FluentSettingsStyle::configureLabel(fontSizeLabel, fluentLookAndFeel);

      addAndMakeVisible(fontSizeSlider);
      fontSizeSlider.setRange(12.0, 36.0, 1.0);
      fontSizeSlider.setValue(getAppSettings().getPlaylistFontSize());
      fontSizeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
      fontSizeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
      fontSizeSlider.onValueChange = [this]() {
        float size = (float)fontSizeSlider.getValue();
        getAppSettings().setPlaylistFontSize(size);
        if (onSettingsChanged)
          onSettingsChanged();
      };

    }

    void paint(juce::Graphics &g) override {
      FluentSettingsStyle::paintPanel(g, fluentLookAndFeel);
      FluentSettingsStyle::paintCard(g, fluentLookAndFeel,
                                     interfaceCardBounds);
      FluentSettingsStyle::paintCard(g, fluentLookAndFeel,
                                     playlistCardBounds);
    }

    ~FontSettingsContent() override {
      getAppSettings().save();
      setLookAndFeel(nullptr);
    }

    void populateFontCombo(juce::ComboBox &combo,
                           const juce::String &currentFont,
                           bool isPlaylist = false) {
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
        if (!topFonts.contains(font) &&
            !topFonts.contains(getDisplayName(font))) {
          addItem(font, 0);
        }
      }
    }

    juce::String getDisplayName(const juce::String &name) { return name; }

    void resized() override {
      auto area =
          getLocalBounds().reduced(FluentSettingsStyle::panelMargin);
      interfaceCardBounds = area.removeFromTop(104);
      area.removeFromTop(12);
      playlistCardBounds = area.removeFromTop(172);

      constexpr int labelWidth = 72;
      auto interfaceContent =
          interfaceCardBounds.reduced(FluentSettingsStyle::cardPadding);
      interfaceSectionLabel.setBounds(interfaceContent.removeFromTop(22));
      interfaceContent.removeFromTop(8);
      auto uiRow = interfaceContent.removeFromTop(
          FluentSettingsStyle::controlHeight);
      uiFontLabel.setBounds(uiRow.removeFromLeft(labelWidth));
      uiFontCombo.setBounds(uiRow);

      auto playlistContent =
          playlistCardBounds.reduced(FluentSettingsStyle::cardPadding);
      playlistSectionLabel.setBounds(playlistContent.removeFromTop(22));
      playlistContent.removeFromTop(8);
      auto fontRow = playlistContent.removeFromTop(
          FluentSettingsStyle::controlHeight);
      playlistFontLabel.setBounds(fontRow.removeFromLeft(labelWidth));
      playlistFontCombo.setBounds(fontRow);
      playlistContent.removeFromTop(FluentSettingsStyle::rowGap);
      auto sizeRow = playlistContent.removeFromTop(
          FluentSettingsStyle::controlHeight);
      fontSizeLabel.setBounds(sizeRow.removeFromLeft(labelWidth));
      fontSizeSlider.setBounds(sizeRow);
    }

    FluentLookAndFeel &fluentLookAndFeel;
    juce::StringArray availableFonts;
    juce::StringArray fontRealNames_UI;
    juce::StringArray fontRealNames_Playlist;

    juce::Label interfaceSectionLabel, playlistSectionLabel, uiFontLabel,
        playlistFontLabel, fontSizeLabel;
    juce::ComboBox uiFontCombo, playlistFontCombo;
    juce::Slider fontSizeSlider;
    juce::Rectangle<int> interfaceCardBounds, playlistCardBounds;
  };

  midi::Core &core;
  midi::LegacyCoreAdapter legacy;
  AudioEngine &engine;
  FluentLookAndFeel &fluentLookAndFeel;

  BackgroundComponent background;
  NavigationSidebar navigation;

  juce::Label pageTitle;
  juce::ComboBox pluginSelector;
  TransparentButton loopModeBtn;
  SvgButton exportBtn{R"(<svg width="24" height="24" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg"><path d="M12 15V3M12 15L8 11M12 15L16 11" stroke="#000000" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/><path d="M20 16V18C20 19.1046 19.1046 20 18 20H6C4.89543 20 4 19.1046 4 18V16" stroke="#000000" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>)"};
  juce::Slider volumeSlider;
  TransparentButton scanBtn, unloadBtn, openPluginBtn;

  juce::Label contentLabel;
  PlaylistManager &playlist;
  PlaylistPanel playlistPanel;

  juce::Component transportBar;
  ScrollingLabel trackLabel;
  juce::Label timeLabel;
  juce::Slider progressSlider;
  ProgressTimeTooltip progressTimeTooltip;
  TransparentButton prevBtn, playBtn, nextBtn, stopBtn, volumeBtn;
  std::unique_ptr<juce::FileChooser> fileChooser;
  std::unique_ptr<juce::Drawable> sequentialIconDrawable;
  juce::Colour lastSequentialIconColor = juce::Colours::black;
  juce::String currentPage = "library";

  juce::Component::SafePointer<juce::DialogWindow> audioSettingsWindow;
  juce::Component::SafePointer<juce::DialogWindow> backgroundSettingsWindow;
  juce::Component::SafePointer<juce::DialogWindow> fontSettingsWindow;
  bool isUserDraggingProgress = false;
  bool isScanningPlugins = false;
  bool pluginLoadInProgress = false;
  bool lastPlayingState = false;
  bool isDragOver = false;
  bool isMuted = false;
  double volumeBeforeMute = 1.0;
  float playbackModeAnimationScale = 1.0f;
  ToastComponent modeToast;

  std::atomic<uint32_t> lastSeekRequestTime{0};
  bool playbackPausedByPluginSwitch = false;
  bool pluginWorkerCrashAlertShown = false;
  bool crashedWorkerTerminationQueued = false;
  bool pendingShellOpen = false;
  PluginWindowLifecycle pluginLifecycle;

  juce::File currentPlaylistFile;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainContentComponent)
};
