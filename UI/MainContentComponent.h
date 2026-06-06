#pragma once

#if defined(_WIN32)
extern "C" {
typedef unsigned long DWORD;
typedef void *HKEY;
typedef unsigned char BYTE;
typedef BYTE *LPBYTE;

#ifndef HKEY_CURRENT_USER
#define HKEY_CURRENT_USER ((HKEY)(unsigned long long)0x80000001)
#endif

#ifndef KEY_READ
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#endif

#ifndef REG_SZ
#define REG_OPTION_NON_VOLATILE 0x00000000
#define REG_SZ 1
#endif

#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0L
#endif

__declspec(dllimport) long __stdcall
RegOpenKeyExW(HKEY hKey, const wchar_t *lpSubKey, DWORD ulOptions,
              DWORD samDesired, HKEY *phkResult);
__declspec(dllimport) long __stdcall
RegCreateKeyExW(HKEY hKey, const wchar_t *lpSubKey, DWORD Reserved,
                wchar_t *lpClass, DWORD dwOptions, DWORD samDesired,
                void *lpSecurityAttributes, HKEY *phkResult,
                DWORD *lpdwDisposition);
__declspec(dllimport) long __stdcall
RegSetValueExW(HKEY hKey, const wchar_t *lpValueName, DWORD Reserved,
               DWORD dwType, const BYTE *lpData, DWORD cbData);
__declspec(dllimport) long __stdcall
RegQueryValueExW(HKEY hKey, const wchar_t *lpValueName, DWORD *lpReserved,
                 DWORD *lpType, LPBYTE lpData, DWORD *lpcbData);
__declspec(dllimport) long __stdcall RegCloseKey(HKEY hKey);
__declspec(dllimport) long __stdcall
RegDeleteValueW(HKEY hKey, const wchar_t *lpValueName);
__declspec(dllimport) long __stdcall RegDeleteTreeW(HKEY hKey,
                                                    const wchar_t *lpSubKey);

#ifndef SHCNE_ASSOCCHANGED
#define SHCNE_ASSOCCHANGED 0x08000000L
#define SHCNF_IDLIST 0x0000
#endif
__declspec(dllimport) void __stdcall SHChangeNotify(long wEventId,
                                                    unsigned int uFlags,
                                                    const void *dwItem1,
                                                    const void *dwItem2);
}
#endif

#include "../AudioEngine/AudioEngine.h"
#include "../Playlist/PlaylistManager.h"
#include "../Utils/UserSettings.h"
#include "../Utils/Win11Helpers.h"
#include "BackgroundComponent.h"
#include "CustomControls.h"
#include "CustomLookAndFeel.h"
#include "NavigationSidebar.h"
#include "PlaylistPanel.h"

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

class MainContentComponent : public juce::Component,
                             public juce::Button::Listener,
                             public juce::ChangeListener,
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
  MainContentComponent(AudioEngine &e)
      : engine(e), navigation(fluentLookAndFeel), playlistPanel(playlist) {
    setLookAndFeel(&fluentLookAndFeel);
    engine.addChangeListener(this);
    setWantsKeyboardFocus(true);
    loadSettings();

    // Background (covers entire window)
    addAndMakeVisible(background);
    background.toBack();
    background.onAccentColorChanged =
        [safeThis = juce::Component::SafePointer<MainContentComponent>(this)](
            juce::Colour c) {
          if (safeThis != nullptr)
            safeThis->onAccentColorChanged(c);
        };

    // Navigation sidebar (collapsible)
    addAndMakeVisible(navigation);
    navigation.setListener(this);

    // Page title - uses dedicated title font
    addAndMakeVisible(pageTitle);
    pageTitle.setText(L"乐器库", juce::dontSendNotification);
    pageTitle.setFont(fluentLookAndFeel.getDefaultFont(26.0f, true));
    pageTitle.setColour(juce::Label::textColourId,
                        fluentLookAndFeel.getColors().textPrimary);

    // Plugin controls with tooltips
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

    // Start disabled until plugin loads
    unloadBtn.setEnabled(false);

    // Scan progress indicator
    addChildComponent(scanSpinner);

    // Content area label
    addAndMakeVisible(contentLabel);
    contentLabel.setFont(fluentLookAndFeel.getDefaultFont(16.0f));
    contentLabel.setColour(juce::Label::textColourId,
                           fluentLookAndFeel.getColors().textSecondary);
    contentLabel.setInterceptsMouseClicks(false, false); // Let clicks through
    contentLabel.setJustificationType(juce::Justification::centred);
    contentLabel.setText(L"选择一个 VST3 乐器插件开始演奏",
                         juce::dontSendNotification);

    // Playlist
    addChildComponent(playlistPanel);
    playlistPanel.setListener(this);

    // === Transport Bar ===
    addAndMakeVisible(transportBar);
    transportBar.setInterceptsMouseClicks(
        false, true); // Allow clicks to pass through empty areas if needed

    // 嵌入式 Tooltip（最后添加，确保在最上层）
    addAndMakeVisible(embeddedTooltip);
    // 使用 EmbeddedTooltip 自身的 MouseListener 逻辑，递归监听所有子组件
    addMouseListener(&embeddedTooltip, true);

    addAndMakeVisible(trackLabel);
    trackLabel.setFont(fluentLookAndFeel.getDefaultFont(14.0f, true));
    trackLabel.setColour(juce::Label::textColourId,
                         fluentLookAndFeel.getColors().textPrimary);
    trackLabel.setInterceptsMouseClicks(
        true, false); // Allow clicks for hover, forward later

    addAndMakeVisible(timeLabel);
    timeLabel.setText("0:00 / 0:00", juce::dontSendNotification);
    timeLabel.setFont(fluentLookAndFeel.getDefaultFont(12.0f));
    timeLabel.setColour(juce::Label::textColourId,
                        fluentLookAndFeel.getColors().textSecondary);

    addAndMakeVisible(progressSlider);
    progressSlider.setRange(0.0, 1.0);
    progressSlider.addListener(this);
    progressSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    progressSlider.setPopupDisplayEnabled(true, true, this);
    // Allow clicking on track to jump
    progressSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    progressSlider.setVelocityBasedMode(false);
    progressSlider.setScrollWheelEnabled(false);
    progressSlider.textFromValueFunction = [this](double v) {
      double dur = engine.getMidiPlayer().getDurationInSamples();
      double sr = engine.getSampleRate() > 0 ? engine.getSampleRate() : 44100.0;
      return formatTime((int)(v * dur / sr));
    };

    // Transport buttons (icon buttons)
    setupIconButton(prevBtn, L"\uE892", L"上一首 (←)");
    setupIconButton(playBtn, L"\uE768", L"播放/暂停 (空格)");
    setupIconButton(nextBtn, L"\uE893", L"下一首 (→)");
    setupIconButton(stopBtn, L"\uE71A", L"停止");

    // Loop Mode
    addAndMakeVisible(loopModeBtn);
    loopModeBtn.addListener(this);
    loopModeBtn.setTooltip(L"播放模式: 连续播放"); // Initial tooltip

    // Volume
    addAndMakeVisible(volumeBtn);
    volumeBtn.addListener(this);
    volumeBtn.setTooltip(L"点击切换静音");

    addAndMakeVisible(volumeSlider);

    volumeSlider.setRange(0.0, 1.0);
    // Restore volume from settings
    float savedVol = getAppSettings().getMasterVolume();
    volumeSlider.setValue(savedVol, juce::dontSendNotification);
    engine.setMasterVolume(savedVol);
    volumeSlider.addListener(this);
    volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    volumeSlider.setPopupDisplayEnabled(true, true, this);
    volumeSlider.setNumDecimalPlacesToDisplay(0);
    volumeSlider.textFromValueFunction = [](double v) {
      return juce::String(juce::roundToInt(v * 100)) + "%";
    };

    updateLoopButtonTooltip();

    // Background mouse listener for deselecting
    background.addMouseListener(this, false);

    // Progress slider starts disabled until a track is loaded
    progressSlider.setEnabled(false);

    startTimerHz(30);
    updatePluginList();

    // Apply saved accent color if any
    auto savedColor =
        juce::Colour::fromString(getAppSettings().getThemeAccentColor());
    fluentLookAndFeel.updateAccentColor(savedColor);

    // Apply Windows 11 style and handle audio initialization alerts
    runLater(150, [](MainContentComponent &self) {
      if (auto *topLevel = self.getTopLevelComponent())
        Win11Helpers::applyWin11Style(topLevel);

      // 如果正在通过文件关联打开 MIDI 文件，跳过通用音频设备警告，
      // 由 openMidiFileFromShell 流程统一处理错误提示，避免对话框堆叠。
      if (self.pendingShellOpen)
        return;

      if (self.engine.isFirstRunAudio()) {
        // First run - no audio settings found. Guide user to settings.
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
      } else if (!self.engine.hasAudioDevice()) {
        // Not first run, but no audio device available at all
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, L"音频设备不可用",
            L"未检测到可用的音频输出设备，请检查您的音频设备是否正常工作。"
            L"\n\n您可以在侧栏「设置」中手动配置音频输出。");
      } else if (self.engine.wasDeviceRestoredWithFallback()) {
        // Saved device was missing at startup, fell back to default
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::InfoIcon, L"音频设备已重置",
            L"由于上次使用的音频设备未找到，已自动切换到系统的默认播放设备。");
      }
    });

    addAndMakeVisible(modeToast);

    // 启动时自动加载上次使用的播放列表文件
    runLater(200, [](MainContentComponent &self) {
      self.playlistPanel.autoLoadLastPlaylist();
    });

    // 文件关联提示（排在音频设置提示之后）
    runLater(500, [](MainContentComponent &self) {
      self.showFileAssociationPrompt();
    });
  }

  ~MainContentComponent() override;

  void timerCallback() override {
    static uint32_t lastCallTime = 0;
    auto now = juce::Time::getMillisecondCounter();

    if (lastCallTime != 0) {
      auto interval = now - lastCallTime;
      (void)interval;
    }
    lastCallTime = now;

    auto &player = engine.getMidiPlayer();
    player.collectRetiredResources();
    auto currentTime = juce::Time::getMillisecondCounter();
    bool isPlaying = engine.getMidiPlayer().getPlaying();

    // Enable/disable progress slider based on track state
    bool hasTrack = player.hasSequence();
    if (progressSlider.isEnabled() != hasTrack) {
      progressSlider.setEnabled(hasTrack);
    }

    // 1. Update Progress (only if not dragging and NOT in "Confirm Seek" grace
    // period)
    if (!isUserDraggingProgress &&
        (currentTime - lastSeekRequestTime.load() > 250)) {
      double pos = engine.getMidiPlayer().getPositionInSamples();
      double dur = engine.getMidiPlayer().getDurationInSamples();
      if (dur > 0) {
        progressSlider.setValue(pos / dur, juce::dontSendNotification);
        double sr =
            engine.getSampleRate() > 0 ? engine.getSampleRate() : 44100.0;
        timeLabel.setText(formatTime((int)(pos / sr)) + " / " +
                              formatTime((int)(dur / sr)),
                          juce::dontSendNotification);
      } else {
        progressSlider.setValue(0.0, juce::dontSendNotification);
        timeLabel.setText("0:00 / 0:00", juce::dontSendNotification);
      }
    }

    // Update play button icon based on state
    if (isPlaying != lastPlayingState) {
      lastPlayingState = isPlaying;
      repaint();
    }

    // 检查播放结束标志（仅在用户未拖动进度条时）。
    // 延迟检查防止在用户快速跳转时触发“下一曲”，避免状态冲突和播放中断。
    if (!isUserDraggingProgress && player.hasFinished())
      handleTrackEnd();

    // 根据插件加载状态禁用/启用交通控制按钮
    bool hasPlugin = engine.getVst3Instance() != nullptr;
    if (playBtn.isEnabled() != hasPlugin) {
      playBtn.setEnabled(hasPlugin);
      stopBtn.setEnabled(hasPlugin);
      prevBtn.setEnabled(hasPlugin);
      nextBtn.setEnabled(hasPlugin);
      // progressSlider 只有在有插件且有曲目时才启用
      progressSlider.setEnabled(hasPlugin && hasTrack);
    }

    // 如果没有插件，强制进度归零
    if (!hasPlugin) {
      progressSlider.setValue(0.0, juce::dontSendNotification);
      timeLabel.setText("0:00 / 0:00", juce::dontSendNotification);
    }

    // Handle playback resumption with debouncing
    if (pendingResumePlayback) {
      // We don't need a heavy debounce here anymore because seekTo is now async
      // and doesn't block. We can resume pretty much immediately or keep a
      // small buffer. Keeping a small delay is still good for UI
      // feel/smoothing.
      if (juce::Time::getMillisecondCounter() - lastSeekRequestTime > 50) {
        engine.getMidiPlayer().setPlaying(true);
        pendingResumePlayback = false;
      }
    }

    if (playbackModeAnimationScale < 1.0f) {
      playbackModeAnimationScale += 0.05f;
      if (playbackModeAnimationScale >= 1.0f)
        playbackModeAnimationScale = 1.0f;
      repaint();
    }

  }

  void paint(juce::Graphics &g) override {
    auto &colors = fluentLookAndFeel.getColors();

    // Transport bar background
    g.setColour(colors.transportBackground);
    g.fillRect(transportBar.getBounds());

    // Transport bar top border
    g.setColour(colors.cardBorder);
    g.drawHorizontalLine(transportBar.getY(), 0.0f, (float)getWidth());

    // Drag-over highlight
    if (isDragOver) {
      g.setColour(colors.accentPrimary.withAlpha(0.15f));
      g.fillAll();
      g.setColour(colors.accentPrimary);
      g.drawRect(getLocalBounds(), 3);
    }
  }

  void paintOverChildren(juce::Graphics &g) override {
    bool isPlaying = engine.getMidiPlayer().getPlaying();

    // Draw toolbar button icons
    drawIconButton(g, scanBtn, L"\uE9A1");       // Search/Scan
    drawIconButton(g, unloadBtn, L"\uE74D");     // Delete
    drawIconButton(g, openPluginBtn, L"\uE8A7"); // OpenPane

    // Transport buttons
    drawIconButton(g, prevBtn, L"\uE892");
    drawPlayButton(g, playBtn, isPlaying);
    drawIconButton(g, nextBtn, L"\uE893");
    drawIconButton(g, stopBtn, L"\uE71A");

    // Volume icon
    float vol = (float)volumeSlider.getValue();
    juce::String volIcon =
        vol > 0.5f ? L"\uE995" : (vol > 0 ? L"\uE994" : L"\uE992");
    drawIconButton(g, volumeBtn, volIcon);

    // Loop mode icon
    juce::String loopIcon;
    bool isSequential = false;
    switch (playlist.getPlaybackMode()) {
    case PlaylistManager::PlaybackMode::Sequential:
      isSequential = true;
      break;
    case PlaylistManager::PlaybackMode::LoopList:
      loopIcon = L"\uE8EE"; // RepeatAll
      break;
    case PlaylistManager::PlaybackMode::LoopSingle:
      loopIcon = L"\uE8ED"; // RepeatOne
      break;
    case PlaylistManager::PlaybackMode::Shuffle:
      loopIcon = L"\uE8B1"; // Shuffle
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

    // Draw toasts always on top
    modeToast.toFront(false);
  }

  void resized() override {
    triggerAsyncUpdate(); // Throttled layout
  }

  void handleAsyncUpdate() override {
    auto area = getLocalBounds();

    // Background covers everything
    background.setBounds(area);

    // Sidebar
    int navWidth = navigation.getPreferredWidth();
    navigation.setBounds(area.removeFromLeft(navWidth));

    // Transport bar
    int transportHeight = 80;
    auto transportArea = area.removeFromBottom(transportHeight);
    transportBar.setBounds(transportArea);
    layoutTransportBar(transportArea);

    // Content area
    int padding = 24;
    auto content = area.reduced(padding, 16);

    // Header row
    auto header = content.removeFromTop(48);
    pageTitle.setBounds(header.removeFromLeft(180));

    // Toolbar with icon buttons
    int btnSize = 36;
    int comboWidth = 200;

    openPluginBtn.setBounds(header.removeFromRight(btnSize).reduced(2));
    unloadBtn.setBounds(header.removeFromRight(btnSize).reduced(2));
    scanBtn.setBounds(header.removeFromRight(btnSize).reduced(2));
    if (scanSpinner.isVisible()) {
      scanSpinner.setBounds(header.removeFromRight(btnSize));
    }
    pluginSelector.setBounds(header.removeFromRight(comboWidth).reduced(2, 6));

    content.removeFromTop(12);

    // Main content
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

  void layoutTransportBar(juce::Rectangle<int> area);

  // === Navigation ===
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
    // "pin" is handled by navigationPinToggled
  }

  void navigationPinToggled(bool isPinned) override {
    if (auto *tlw = getTopLevelComponent())
      tlw->setAlwaysOnTop(isPinned);
  }

  void navigationBackgroundClicked() override {
    playlistPanel.deselectAllRows();
  }

  // === Buttons ===
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
  }

  void toggleLoopMode();

  void toggleMute();

  void updateLoopButtonTooltip();

  // === Sliders ===
  void sliderValueChanged(juce::Slider *s) override {
    if (s == &volumeSlider) {
      float vol = (float)s->getValue();
      engine.setMasterVolume(vol);
      getAppSettings().setMasterVolume(vol); // Persist setting
      repaint();                             // Update volume icon
    } else if (s == &progressSlider) {
      // If we clicked (not dragging), update the seek timestamp to prevent
      // timer jump
      if (!isUserDraggingProgress)
        lastSeekRequestTime.store(juce::Time::getMillisecondCounter());

      // During drag, only update the time label
      double dur = engine.getMidiPlayer().getDurationInSamples();
      if (dur > 0) {
        double currentVal = s->getValue();
        double sr =
            engine.getSampleRate() > 0 ? engine.getSampleRate() : 44100.0;
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

      // Use AsyncUpdater to perform the seek off-drag-event
      triggerSeekUpdate(s->getValue());
    }
  }

  void triggerSeekUpdate(double normalizedPos);

  // === ComboBox ===
  void comboBoxChanged(juce::ComboBox *c) override {
    if (c == &pluginSelector)
      loadSelectedPlugin();
  }

  // === Playlist ===
  void playlistSaveRequested() override { savePlaylist({}); }

  void playlistLoaded(const juce::File &file) override {
    setCurrentTrackIndex(-1);
    currentPlaylistFile = file;
  }

  // 拖拽排序后同步播放索引，确保切歌逻辑使用正确的位置
  void playlistTrackReordered(int newCurrentIndex) override {
    setCurrentTrackIndex(newCurrentIndex);
  }

  void playlistTrackDoubleClicked(int index);

  void playlistFilesDropped(const juce::StringArray &files) override;

  // === FileDragAndDrop ===
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

  // === Background ===
  void backgroundSettingsChanged(bool reapplyEffects) override {
    // If we are just refreshing settings, reload
    if (reapplyEffects)
      background.loadAsync();

    // If this was triggered by Monet toggle restore or normal update
    auto accentColor =
        juce::Colour::fromString(getAppSettings().getThemeAccentColor());
    fluentLookAndFeel.updateAccentColor(accentColor);
    repaint();
  }

  void backgroundSettingsClosed() override {}

  void changeListenerCallback(juce::ChangeBroadcaster *source) override {
    // 使用 SafePointer 防止组件销毁后访问悬空指针
    auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
    juce::MessageManager::callAsync([safeThis]() {
      if (safeThis == nullptr)
        return;
      safeThis->updatePluginList();
      safeThis->scanSpinner.setVisible(false);
      safeThis->resized();
    });
  }

  // Callback when extracting color
  void onAccentColorChanged(juce::Colour newColor);

  // === Keyboard shortcuts ===
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
      // Escape closes plugin window if open
      if (pluginWindow && pluginWindow->isVisible()) {
        pluginWindow->setVisible(false);
        return true;
      }
    }
    return false;
  }

  // === Drag-drop visual feedback ===
  void fileDragEnter(const juce::StringArray &, int, int) override {
    isDragOver = true;
    repaint();
  }

  void fileDragExit(const juce::StringArray &) override {
    isDragOver = false;
    repaint();
  }

  // Handle global mouse clicks to deselect playlist
  void mouseDown(const juce::MouseEvent &e) override {
    // If we clicked background or this component directly
    playlistPanel.deselectAllRows();
  }

private:
  void setCurrentTrackIndex(int index);
  void applyDroppedPlaylistFiles(const juce::StringArray &newFiles,
                                 const juce::StringArray &duplicateFiles,
                                 const juce::File &currentPlayingFile,
                                 bool overwriteDuplicates);

  void runLater(int delayMs, std::function<void(MainContentComponent &)> fn);

  void runAsync(std::function<void(MainContentComponent &)> fn);

  void setupIconButton(juce::Button &btn, const juce::String &,
                       const juce::String &tooltip);

  void drawIconButton(juce::Graphics &g, juce::Button &btn,
                      const juce::String &icon);

  void drawIconButtonCombined(juce::Graphics &g, juce::Button &btn,
                              const juce::String &mainIcon,
                              const juce::String &subIcon);

  void drawSequentialIcon(juce::Graphics &g, juce::Button &btn);

  void drawPlayButton(juce::Graphics &g, juce::Button &btn, bool isPlaying);

  void showPage(const juce::String &pageId, const juce::String &title);

  void startPluginScan();

  void confirmUnloadPlugin();

public:
  // === Persistence ===
  bool hasUnsavedChanges() const { return playlist.hasChanges(); }

  juce::String getPlaylistChangeSummary() const {
    return playlist.getChangeSummary();
  }

  void savePlaylist(std::function<void(bool)> completion);

  void savePlaylistAs(std::function<void(bool)> completion);

  void unloadPlugin();

  void loadSelectedPlugin();

  void openPluginWindow();

  void closePluginWindow();

  class PluginWindow : public juce::DocumentWindow {
  public:
    PluginWindow(const juce::String &name, juce::AudioProcessorEditor *editor,
                 int w, int h)
        : juce::DocumentWindow(name, juce::Colour(0xFF1a1a1a),
                               juce::DocumentWindow::allButtons, true) {
      setUsingNativeTitleBar(true);

      if (editor != nullptr) {
        // 让窗口遵循插件本身的缩放规则。
        bool canResize = editor->isResizable();
        setResizable(canResize, canResize);

        if (canResize)
          setResizeLimits(160, 120, 8192, 8192);
      }

      setContentOwned(editor, true);

      // 仅对插件窗口应用深色模式，避免应用 Mica 特效，
      // 因为部分 VST3 插件的绘图引擎会与 Windows 11 的亚克力/Mica
      // 产生冲突。使用 SafePointer 防止窗口在延迟期间被销毁。
      auto safeWindow =
          juce::Component::SafePointer<PluginWindow>(this);
      juce::Timer::callAfterDelay(100, [safeWindow]() {
        if (safeWindow != nullptr)
          Win11Helpers::updateDarkMode(safeWindow, true);
      });

      centreWithSize(w, h);
      setVisible(true);
      toFront(true);
    }

    void closeButtonPressed() override { setVisible(false); }
  };

  void togglePlayPause();

  void stopPlayback();

  void playNextTrack();

  void playPreviousTrack();

  void handleTrackEnd();

  bool loadMidiFile(const juce::File &file);

  void showOpenFileDialog();

#if JUCE_WINDOWS
  bool isFileAssociatedToSelf();
  bool registerFileAssociation();
  void removeFileAssociation();
#endif // JUCE_WINDOWS

  std::unique_ptr<juce::ToggleButton> createDontShowAgainToggle();
  void showFileAssociationPrompt();
  void openMidiFileFromShell(const juce::File &file);
  void setPendingShellOpen(bool pending);
  void tryLoadLastPluginWithDialog();

  void showAudioSettings();

  void showBackgroundSettings();

  void showFontSettings();

  void updatePluginList();

  void tryLoadLastPlugin();

  juce::String formatTime(int seconds);

  void loadSettings();

  void saveSettings();

  struct AudioSettingsContent;

  struct FontSettingsContent;

  class PluginScanThread;
  struct PluginScanThreadDeleter {
    void operator()(PluginScanThread *thread) const;
  };

  class SeekUpdater : public juce::AsyncUpdater {
  public:
    SeekUpdater(MainContentComponent &o) : owner(o) {}
    void handleAsyncUpdate() override {
      double dur = owner.engine.getMidiPlayer().getDurationInSamples();
      if (dur > 0) {
        double val = owner.pendingSeekValue.load();
        owner.engine.getMidiPlayer().seekTo(val * dur);
      }
    }

  private:
    MainContentComponent &owner;
  };

  SeekUpdater seekUpdater{*this};
  std::atomic<double> pendingSeekValue{0.0};

  // === Members ===
  AudioEngine &engine;
  FluentLookAndFeel fluentLookAndFeel;
  // 嵌入式 Tooltip，直接在父窗口中绘制，确保完美圆角
  EmbeddedTooltip embeddedTooltip;

  BackgroundComponent background;
  NavigationSidebar navigation;

  juce::Label pageTitle;
  juce::ComboBox pluginSelector;
  TransparentButton scanBtn, unloadBtn, openPluginBtn;
  SpinnerComponent scanSpinner;

  juce::Label contentLabel;
  PlaylistManager playlist;
  PlaylistPanel playlistPanel;

  std::unique_ptr<PluginWindow> pluginWindow;
  std::unique_ptr<PluginScanThread, PluginScanThreadDeleter> pluginScanThread;

  // Transport
  juce::Component transportBar;
  juce::Slider progressSlider;
  juce::Label timeLabel;
  ScrollingLabel trackLabel;
  TransparentButton prevBtn, playBtn, nextBtn, stopBtn;
  TransparentButton volumeBtn;
  TransparentButton loopModeBtn;
  juce::Slider volumeSlider;

  std::unique_ptr<juce::FileChooser> fileChooser;

  juce::String currentPage = "library";
  int currentTrackIndex = -1;
  bool isUserDraggingProgress = false;
  bool isScanningPlugins = false;
  bool lastPlayingState = false;
  bool isDragOver = false;
  bool isMuted = false;
  double volumeBeforeMute = 1.0;
  float playbackModeAnimationScale = 1.0f;
  ToastComponent modeToast;

  std::atomic<uint32_t> lastSeekRequestTime{0};
  bool pendingResumePlayback = false;
  bool isHandlingTrackEnd = false;
  int trackSwitchGeneration = 0;
  bool pendingShellOpen = false;

  juce::File currentPlaylistFile;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainContentComponent)
};
