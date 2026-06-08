#pragma once

// Forward-declare Registry & Shell APIs needed for file association.
// Do NOT include <windows.h> here - it conflicts with Win11Helpers.h
// (HWND redefinition) and pollutes the namespace with min/max/BYTE macros
// that break BackgroundComponent.h.
// Win11Helpers.h already declares: HWND, HRESULT, DWORD, BOOL.
#if JUCE_WINDOWS
extern "C" {
// Types needed for Registry APIs
// (DWORD will also be declared in Win11Helpers.h - MSVC allows identical
// redefs)
typedef unsigned long DWORD;
typedef void *HKEY;
typedef unsigned char BYTE;
typedef BYTE *LPBYTE;

// Registry handle constants
#ifndef HKEY_CURRENT_USER
#define HKEY_CURRENT_USER ((HKEY)(unsigned long long)0x80000001)
#endif

// Registry access rights
#ifndef KEY_READ
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#endif

// Registry options & types
#ifndef REG_SZ
#define REG_OPTION_NON_VOLATILE 0x00000000
#define REG_SZ 1
#endif

// Error code
#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0L
#endif

// Registry functions (from advapi32.dll)
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

// Shell notification (from shell32.dll)
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
#pragma once

// Forward-declare Registry & Shell APIs needed for file association.
// Do NOT include <windows.h> here - it conflicts with Win11Helpers.h
// (HWND redefinition) and pollutes the namespace with min/max/BYTE macros
// that break BackgroundComponent.h.
// Win11Helpers.h already declares: HWND, HRESULT, DWORD, BOOL.
#if JUCE_WINDOWS
extern "C" {
// Types needed for Registry APIs
// (DWORD will also be declared in Win11Helpers.h - MSVC allows identical
// redefs)
typedef unsigned long DWORD;
typedef void *HKEY;
typedef unsigned char BYTE;
typedef BYTE *LPBYTE;

// Registry handle constants
#ifndef HKEY_CURRENT_USER
#define HKEY_CURRENT_USER ((HKEY)(unsigned long long)0x80000001)
#endif

// Registry access rights
#ifndef KEY_READ
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#endif

// Registry options & types
#ifndef REG_SZ
#define REG_OPTION_NON_VOLATILE 0x00000000
#define REG_SZ 1
#endif

// Error code
#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0L
#endif

// Registry functions (from advapi32.dll)
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

// Shell notification (from shell32.dll)
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
#include "ExportDialog.h"

class OfflineExportThread : public juce::ThreadWithProgressWindow {
public:
    OfflineExportThread(AudioEngine& e, const juce::File& f, const ExportSettings& s)
        : ThreadWithProgressWindow(L"正在导出高保真音频...", true, true), engine(e), file(f), settings(s) {}

    void run() override {
        bool success = engine.runOfflineExport(
            file, settings,
            [this](float p) { setProgress(p); },
            [this]() -> bool { return threadShouldExit(); }
        );

        if (!success && !threadShouldExit()) {
            exportFailed = true;
        }
    }
    
    bool exportFailed = false;

private:
    AudioEngine& engine;
    juce::File file;
    ExportSettings settings;
};

#include <cmath>
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
  /**
      UI 看门狗线程。

      维护要点：
      - 该线程独立于消息线程运行。
      - 它定期检查 lastHeartbeatTime（由计时器回调在主线程更新）。
      - 如果发现 3 秒钟内没有心跳，判定 UI 冻结。
      - 恢复机制：紧急停止 BackgroundComponent
     的高负载操作，并向消息队列发送异步恢复信号。
  */
  MainContentComponent(AudioEngine &e)
      : engine(e), navigation(fluentLookAndFeel), playlistPanel(playlist),
        watchdog(*this) {
    lastHeartbeatTime.store(juce::Time::getMillisecondCounter());
    isWatchdogDialogActive.store(false);
    watchdog.startThread();
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

    addAndMakeVisible(exportBtn);
    exportBtn.addListener(this);
    exportBtn.setTooltip(L"离线渲染导出高保真音频");

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

  ~MainContentComponent() override {
    stopTimer();
    cancelPendingUpdate();
    seekUpdater.cancelPendingUpdate();
    watchdog.stopThread(1000);
    closeSettingsWindows();
    fileChooser.reset();
    saveSettings();
    engine.removeChangeListener(this);
    closePluginWindow();
    setLookAndFeel(nullptr);
  }

  void timerCallback() override {
    static uint32_t lastCallTime = 0;
    static uint32_t freezeDiagCounter = 0;
    auto now = juce::Time::getMillisecondCounter();

    // 每500次调用输出一次心跳日志（约16秒一次），减少日志量
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
    lastHeartbeatTime.store(juce::Time::getMillisecondCounter());

    auto &player = engine.getMidiPlayer();
    const double currentSampleRate =
        engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 44100.0;
    if (player.hasSequence() &&
        std::abs(player.getSequenceSampleRate() - currentSampleRate) >= 0.01)
      player.setSampleRate(currentSampleRate);

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
    if (!isUserDraggingProgress && player.hasFinished()) {
      LOG_DEBUG("[FREEZE_DIAG] TC: calling handleTrackEnd");
      handleTrackEnd();
      LOG_DEBUG("[FREEZE_DIAG] TC: handleTrackEnd returned");
    }

    // 只在 timerCallback 执行时间异常时输出完成日志，避免每次都输出导致日志爆炸
    auto tcEndTime = juce::Time::getMillisecondCounter();
    auto tcDuration = tcEndTime - now;
    if (tcDuration > 20) { // 超过20ms视为慢速执行
      LOG_DEBUG("[FREEZE_DIAG] TC completed (slow: " +
                juce::String(tcDuration) + "ms)");
    }

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

    if (exportBtn.isEnabled() != (hasPlugin && hasTrack)) {
      exportBtn.setEnabled(hasPlugin && hasTrack);
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

    // Update watchdog heartbeat here (in Message Thread) instead of paint()
    // This ensures detection works even if window is minimized or covered.
    // lastHeartbeatTime.store(juce::Time::getMillisecondCounter()); // MOVED
    // to top
  }

  void handleFreezeRecovery() {
    LOG_DEBUG("!!! UI WATCHDOG TRIGGERED RECOVERY !!!");
    // Prevent multiple dialogs stacking up
    if (isWatchdogDialogActive.load())
      return;

    isWatchdogDialogActive.store(true);

    // 1. Emergency Reset Background
    background.emergencyReset();

    // 2. Resume playback if it was supposed to be playing
    if (lastPlayingState || engine.getMidiPlayer().getPlaying()) {
      engine.getMidiPlayer().setPlaying(true);
    }

    // 3. Force Repaint
    repaint();

    // 4. Notify User
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon, L"界面响应恢复",
        L"检测到界面长时间未响应，已自动尝试恢复。", L"OK", nullptr,
        juce::ModalCallbackFunction::create(
            [safeThis = juce::Component::SafePointer<MainContentComponent>(
                 this)](int) {
              if (safeThis != nullptr)
                safeThis->isWatchdogDialogActive.store(false);
        }));
  }

  void paint(juce::Graphics &g) override {
    SCOPED_TIMER_SLOW("MainContentComponent::paint", 10);
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
    // auto &colors = lookAndFeel.getColors(); // Removed unused variable
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
    SCOPED_TIMER_SLOW("MainContentComponent::resized", 10);
    triggerAsyncUpdate(); // Throttled layout
  }

  void handleAsyncUpdate() override {
    SCOPED_TIMER_ALWAYS("MainContentComponent::performLayout");
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
    exportBtn.setBounds(header.removeFromRight(btnSize).reduced(2));
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

  void layoutTransportBar(juce::Rectangle<int> area) {
    area = area.reduced(20, 8);

    // Progress bar at top (needs enough height for thumb)
    progressSlider.setBounds(area.removeFromTop(24));
    area.removeFromTop(4);

    // Control row
    auto controlRow = area;
    int btnSize = 36;
    int playBtnSize = 44; // Keep it square

    // Volume right (reserve this space first)
    auto volumeArea = controlRow.removeFromRight(180);
    // Loop button and Volume button layout
    loopModeBtn.setBounds(volumeArea.removeFromLeft(btnSize).reduced(2));
    volumeBtn.setBounds(volumeArea.removeFromLeft(btnSize).reduced(2));

    volumeSlider.setBounds(
        volumeArea.reduced(4, 4)); // Less vertical padding for taller slider

    // Transport buttons center (calculate center space needed)
    int gap = 8;
    int controlsWidth = btnSize * 3 + playBtnSize + gap * 3;

    // Calculate available space for track info
    // Leave space for centered controls (controlsWidth + some padding)
    int minTrackWidth = 200;
    int centerPadding = 40; // Padding around center controls
    int availableForTrack =
        (controlRow.getWidth() - controlsWidth) / 2 - centerPadding;
    int trackInfoWidth = juce::jmax(minTrackWidth, availableForTrack);

    // Track info left - responsive width
    auto leftInfo = controlRow.removeFromLeft(trackInfoWidth);
    trackLabel.setBounds(leftInfo.removeFromTop(22));
    timeLabel.setBounds(leftInfo);

    // Transport buttons center - use already calculated values
    int controlsHeight = playBtnSize; // Use play button size for the row height
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
    else if (b == &exportBtn)
      showExportDialog();
  }

  void toggleLoopMode() {
    auto current = playlist.getPlaybackMode();
    // Correctly cycle through 1, 2, 3, 4
    auto next = static_cast<PlaylistManager::PlaybackMode>(
        (static_cast<int>(current) % 4) + 1);
    playlist.setPlaybackMode(next);
    getAppSettings().setPlayMode((int)next); // Persist setting

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

    // Hide hover tooltip so it doesn't overlap with the toast
    embeddedTooltip.hideTooltip();

    // Trigger animations and toast
    modeToast.show(toastText, loopModeBtn.getBounds());
    playbackModeAnimationScale = 0.8f; // Start pulse

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
    switch (playlist.getPlaybackMode()) {
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

  void triggerSeekUpdate(double normalizedPos) {
    pendingSeekValue.store(normalizedPos);
    seekUpdater.triggerAsyncUpdate();
  }

  // === ComboBox ===
  void comboBoxChanged(juce::ComboBox *c) override {
    if (c == &pluginSelector)
      loadSelectedPlugin();
  }

  // === Playlist ===
  void playlistTrackSelected(int index) override {
    // Logic handled by PlaylistPanel internally
  }

  // 当播放列表被加载或清空时调用，重置播放索引
  void playlistLoaded() override { currentTrackIndex = -1; }

  // 拖拽排序后同步播放索引，确保切歌逻辑使用正确的位置
  void playlistTrackReordered(int newCurrentIndex) override {
    currentTrackIndex = newCurrentIndex;
  }

  void playlistTrackDoubleClicked(int index) {
    LOG_DEBUG("Playlist double-clicked index: " + juce::String(index));
    if (engine.getVst3Instance() == nullptr) {
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::WarningIcon, L"无法播放",
          L"请先加载一个乐器插件以开始播放。");
      return;
    }

    // 中断任何正在进行的 handleTrackEnd 流程
    isHandlingTrackEnd = false;
    engine.getMidiPlayer().setPlaying(false);

    currentTrackIndex = index;
    if (const auto *track = playlist.getTrack(index)) {
      if (loadMidiFile(track->file)) {
        playlistPanel.setCurrentTrackIndex(index);
        // 延迟播放，给 VSL 插件时间处理重置消息
        ++trackSwitchGeneration;
        int gen = trackSwitchGeneration;
        runLater(100, [gen](MainContentComponent &self) {
          if (self.trackSwitchGeneration != gen)
            return;
          self.engine.getMidiPlayer().setPlaying(true);
        });
      }
    }
  }

  void playlistFilesDropped(const juce::StringArray &files) override {
    juce::StringArray newFiles;
    juce::StringArray duplicateFiles;

    // 保存当前播放曲目的文件对象,用于在添加文件后重新定位
    const auto *currentlyPlaying = (currentTrackIndex >= 0)
                                       ? playlist.getTrack(currentTrackIndex)
                                       : nullptr;
    juce::File currentPlayingFile;
    if (currentlyPlaying)
      currentPlayingFile = currentlyPlaying->file;

    // 1. 分离新文件与重复文件
    for (auto &f : files) {
      juce::File file(f);
      if (playlist.contains(file)) {
        duplicateFiles.add(f);
      } else {
        newFiles.add(f);
      }
    }

    // 2. 根据是否有重复文件决定流程
    enum class DupAction { AddNewOnly, OverwriteExisting, Cancel };
    DupAction action = DupAction::AddNewOnly; // 默认：仅添加新文件

    if (!duplicateFiles.isEmpty()) {
      int result = juce::AlertWindow::showYesNoCancelBox(
          juce::AlertWindow::QuestionIcon, L"发现重复文件",
          L"检测到 " + juce::String(duplicateFiles.size()) +
              L" 个文件已在列表中。\n\n"
              L"「仅保存新的」= 跳过重复，只添加新文件\n"
              L"「保存并覆盖」= 添加新文件，并刷新已有条目",
          L"仅保存新的", L"保存并覆盖", L"取消");

      if (result == 0)
        return; // 取消
      if (result == 1)
        action = DupAction::AddNewOnly;
      else if (result == 2)
        action = DupAction::OverwriteExisting;
    }

    // 3. 添加新文件（两种模式都会执行）
    bool anythingChanged = false;
    for (const auto &f : newFiles) {
      if (playlist.addFile(juce::File(f), false))
        anythingChanged = true;
    }

    // 4. 处理重复文件
    std::vector<int> overwrittenRows;
    if (action == DupAction::OverwriteExisting) {
      for (const auto &f : duplicateFiles) {
        int idx = playlist.findTrackIndex(juce::File(f));
        if (idx >= 0) {
          playlist.refreshTrack(idx); // 刷新缓存元数据
          overwrittenRows.push_back(idx);
          anythingChanged = true;
        }
      }
    }

    if (anythingChanged) {
      // 如果有曲目正在播放,重新查找其在列表中的索引
      if (currentlyPlaying && currentPlayingFile.existsAsFile()) {
        int newIndex = playlist.findTrackIndex(currentPlayingFile);
        if (newIndex != -1 && newIndex != currentTrackIndex) {
          currentTrackIndex = newIndex;
          playlistPanel.setCurrentTrackIndex(newIndex);
        }
      }

      playlistPanel.refresh();

      // 对被覆盖的条目播放闪烁动画提示
      if (!overwrittenRows.empty()) {
        playlistPanel.startDropAnimation(overwrittenRows, false);
      }
    }
  }

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
  void onAccentColorChanged(juce::Colour newColor) {
    getAppSettings().setThemeAccentColor(newColor.toString());
    fluentLookAndFeel.updateAccentColor(newColor);

    // Repaint heavy areas immediately
    navigation.repaint();
    playlistPanel.repaint();
    repaint();

    // Throttled notification: Only broadcast when transition is complete
    // background sends change message when target == current
    if (newColor == background.getTargetAccentColor()) {
      sendLookAndFeelChange();
    }
  }

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
  void showExportDialog() {
      juce::StringArray trackNames;
      for (int i = 0; i < playlist.size(); ++i) {
          if (auto* track = playlist.getTrack(i)) {
              trackNames.add(track->name);
          }
      }
      
      int initialIndex = currentTrackIndex;

      auto* dlg = new ExportDialog(trackNames, initialIndex, [this](int selectedTrackIdx, const ExportSettings& s) {
          auto exportDir = UserSettings::getSettingsDirectory().getChildFile("ExportedAudio");
          exportDir.createDirectory();
          const auto extension = getExportExtension(s);
          fileChooser = std::make_unique<juce::FileChooser>(
              L"保存音频文件",
              exportDir.getChildFile(s.title.isNotEmpty() ? s.title + extension
                                                           : "export" + extension),
              "*" + extension);

          fileChooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
              [this, selectedTrackIdx, s](const juce::FileChooser& chooser) {
                  auto result = chooser.getResult();
                  if (result != juce::File{}) {
                      result = ensureExportExtension(result, s);
                      bool needsTrackSwap = (selectedTrackIdx != currentTrackIndex);
                      juce::File originalFile;
                      double originalPosition = 0.0;
                      bool originalPlaying = false;

                      auto restoreOriginalTrack = [&]() {
                          if (needsTrackSwap && originalFile.existsAsFile()) {
                              loadMidiFile(originalFile);
                              engine.getMidiPlayer().seekTo(originalPosition);
                              if (originalPlaying)
                                  engine.getMidiPlayer().setPlaying(true);
                          }
                      };

                      if (needsTrackSwap) {
                          if (currentTrackIndex >= 0) {
                              if (auto* currTrack = playlist.getTrack(currentTrackIndex)) {
                                  originalFile = currTrack->file;
                              }
                          }
                          originalPosition = engine.getMidiPlayer().getPositionInSamples();
                          originalPlaying = engine.getMidiPlayer().getPlaying();

                          engine.getMidiPlayer().setPlaying(false);
                          if (auto* targetTrack = playlist.getTrack(selectedTrackIdx)) {
                              if (!loadMidiFile(targetTrack->file)) {
                                  juce::AlertWindow::showMessageBoxAsync(
                                      juce::AlertWindow::WarningIcon,
                                      L"导出失败",
                                      L"无法加载待导出的 MIDI 文件。");
                                  restoreOriginalTrack();
                                  return;
                              }
                          } else {
                              juce::AlertWindow::showMessageBoxAsync(
                                  juce::AlertWindow::WarningIcon,
                                  L"导出失败",
                                  L"未找到待导出的曲目。");
                              restoreOriginalTrack();
                              return;
                          }
                      }

                      engine.prepareForOfflineExport(s);
                      auto thread = std::make_unique<OfflineExportThread>(engine, result, s);
                      thread->runThread(); // blocks modal
                      engine.restoreFromOfflineExport();

                      if (needsTrackSwap && originalFile.existsAsFile()) {
                          restoreOriginalTrack();
                      }

                      if (thread->exportFailed) {
                          auto error = engine.getLastExportError();
                          if (error.isEmpty())
                              error = L"无法创建文件或渲染引擎出现错误。";
                          juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, L"导出失败", error);
                      }
                  }
              });
      });
      
      juce::DialogWindow::LaunchOptions options;
      options.content.setOwned(dlg);
      options.dialogTitle = L"高保真离线导出";
      options.dialogBackgroundColour = fluentLookAndFeel.getColors().background;
      options.escapeKeyTriggersCloseButton = true;
      options.useNativeTitleBar = false;
      options.resizable = false;
      options.launchAsync();
  }

  static juce::String getExportExtension(const ExportSettings& settings) {
      if (settings.formatName == "FLAC")
          return ".flac";
      if (settings.formatName.containsIgnoreCase("Ogg"))
          return ".ogg";
      return ".wav";
  }

  static juce::File ensureExportExtension(const juce::File& file,
                                          const ExportSettings& settings) {
      const auto extension = getExportExtension(settings);
      if (file.getFileExtension().equalsIgnoreCase(extension))
          return file;
      return file.withFileExtension(extension);
  }

  void runLater(int delayMs, std::function<void(MainContentComponent &)> fn) {
    auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
    juce::Timer::callAfterDelay(
        delayMs, [safeThis, fn = std::move(fn)]() mutable {
          if (safeThis != nullptr)
            fn(*safeThis);
        });
  }

  void runAsync(std::function<void(MainContentComponent &)> fn) {
    auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
    juce::MessageManager::callAsync([safeThis, fn = std::move(fn)]() mutable {
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

    // Background on hover
    if (btn.isEnabled() && (btn.isMouseOver() || btn.isMouseButtonDown())) {
      g.setColour(btn.isMouseButtonDown() ? colors.controlPressed
                                          : colors.controlHover);
      g.fillRoundedRectangle(bounds.reduced(2.0f), 6.0f);
    }

    // Icon
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

    // Background on hover
    if (btn.isEnabled() && (btn.isMouseOver() || btn.isMouseButtonDown())) {
      g.setColour(btn.isMouseButtonDown() ? colors.controlPressed
                                          : colors.controlHover);
      g.fillRoundedRectangle(bounds.reduced(2.0f), 6.0f);
    }

    auto iconColor = btn.isEnabled() ? colors.textPrimary
                                     : colors.textSecondary.withAlpha(0.5f);
    g.setColour(iconColor);

    // Main Icon (shifted slightly left and up)
    g.setFont(fluentLookAndFeel.getIconFont(16.0f));
    auto mainArea = btn.getBounds().translated(-2, -1);
    g.drawText(mainIcon, mainArea, juce::Justification::centred, false);

    // Sub Icon (smaller and at bottom right)
    g.setFont(fluentLookAndFeel.getIconFont(10.0f));
    auto subArea = btn.getBounds().translated(6, 6);
    g.drawText(subIcon, subArea, juce::Justification::centred, false);
  }

  void drawSequentialIcon(juce::Graphics &g, juce::Button &btn) {
    if (!btn.isVisible())
      return;

    auto bounds = btn.getBounds().toFloat();
    auto &colors = fluentLookAndFeel.getColors();

    // Background on hover
    if (btn.isEnabled() && (btn.isMouseOver() || btn.isMouseButtonDown())) {
      g.setColour(btn.isMouseButtonDown() ? colors.controlPressed
                                          : colors.controlHover);
      g.fillRoundedRectangle(bounds.reduced(2.0f), 6.0f);
    }

    auto iconColor = btn.isEnabled() ? colors.textPrimary
                                     : colors.textSecondary.withAlpha(0.5f);
    g.setColour(iconColor);

    // Check user preference for icon style
    if (getAppSettings().getSequentialIconListStyle()) {
      // Draw List Icon (\uEA42)
      g.setFont(fluentLookAndFeel.getIconFont(16.0f));
      g.drawText(L"\uEA42", btn.getBounds(), juce::Justification::centred,
                 false);
    } else {
      // Draw two horizontal parallel arrows (\uEBE7)
      g.setFont(fluentLookAndFeel.getIconFont(9.0f));
      auto b = btn.getBounds();
      g.drawText(L"\uEBE7", b.translated(0, -8), juce::Justification::centred,
                 false);
      g.drawText(L"\uEBE7", b.translated(0, 8), juce::Justification::centred,
                 false);
    }
  }

  void drawPlayButton(juce::Graphics &g, juce::Button &btn, bool isPlaying) {
    auto bounds = btn.getBounds().toFloat().reduced(4.0f);
    auto &colors = fluentLookAndFeel.getColors();
    bool isEnabled = btn.isEnabled();

    // Circular gradient border
    juce::ColourGradient gradient(colors.accentLight, bounds.getTopLeft(),
                                  colors.accentPrimary, bounds.getBottomRight(),
                                  false);
    g.setGradientFill(gradient);
    g.drawEllipse(bounds, 2.5f);

    // Fill on hover (only if enabled)
    if (isEnabled && (btn.isMouseOver() || btn.isMouseButtonDown())) {
      g.setColour(colors.accentPrimary.withAlpha(
          btn.isMouseButtonDown() ? 0.25f : 0.15f));
      g.fillEllipse(bounds.reduced(3.0f));
    }

    // Icon
    g.setFont(fluentLookAndFeel.getIconFont(18.0f));
    g.setColour(isEnabled ? colors.textPrimary
                          : colors.textSecondary.withAlpha(0.4f));
    g.drawText(isPlaying ? L"\uE769" : L"\uE768", btn.getBounds(),
               juce::Justification::centred, false);

    // Fade the border if disabled
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

    // Use ThreadWithProgressWindow for proper progress display
    class ScanThread : public juce::ThreadWithProgressWindow {
    public:
      ScanThread(AudioEngine &e)
          : juce::ThreadWithProgressWindow(L"扫描 VST3 插件...", true, true),
            engine(e) {}

      void run() override {
        setProgress(-1.0); // Indeterminate
        engine.scanPlugins();
      }

      AudioEngine &engine;
    };

    isScanningPlugins = true;
    pluginSelector.setEnabled(false);

    auto scanner = std::make_unique<ScanThread>(engine);
    if (scanner->runThread()) {
      updatePluginList();
    }

    isScanningPlugins = false;
    pluginSelector.setEnabled(true);
  }

  void confirmUnloadPlugin() {
    if (engine.getVst3Instance() == nullptr)
      return;

    // 使用异步对话框避免阻塞消息循环
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
  // === Persistence ===
  bool hasUnsavedChanges() const { return playlist.hasChanges(); }

  juce::String getPlaylistChangeSummary() const {
    return playlist.getChangeSummary();
  }

  // Save current playlist (Proposes Save As if new)
  // Returns true if saved (or user accepted SaveAs), false if cancelled/failed
  bool savePlaylist() {
    if (currentPlaylistFile.existsAsFile()) {
      return playlist.save(currentPlaylistFile);
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

    // Note: Since we are likely called from closeButtonPressed, we need
    // synchronous behavior or careful async.
    // However, closeButtonPressed in Main.cpp usually expects a return value
    // immediately or handles async quit.
    // For now, we'll use a modal loop or assume this is triggered by a button
    // (Manual Save).
    // If triggered by Exit Prompt, the Exit Prompt itself (AlertWindow) is
    // handling the flow. Use browseForFileToSave for synchronous if needed, OR
    // launchAsync.
    // Given Juce 7/8 trends, async is preferred. But for 'closeButtonPressed'
    // it's tricky.
    // We will use synchronous browse for simplicity in this specific "Save As"
    // flow if allowed, otherwise we might need a workaround for exit.
    // Let's use launchAsync with a callback that updates the file.
    // BUT: If this is called from Exit Prompt, we need to know the result to
    // proceed closing.
    // Refactoring: The Exit Prompt in Main.cpp will be the one driving this.
    // If "Save" is clicked, Main.cpp calls savePlaylist().
    // If savePlaylist returns false (e.g. cancelled SaveAs), we shouldn't quit?
    // User requirement: "options to save, discard, or cancel".
    // If Save is chosen -> Save.
    // If SaveAs is needed -> Show dialog.
    // If user cancels SaveAs -> Cancel exit?

    // Ideally use browseForFileToSave (modal) for simplicity on Desktop.
    if (fileChooser->browseForFileToSave(true)) {
      currentPlaylistFile = fileChooser->getResult();
      return playlist.save(currentPlaylistFile);
    }
    return false;
  }

  void unloadPlugin() {
    closePluginWindow();
    engine.unloadPlugin();
    pluginSelector.setSelectedId(0, juce::dontSendNotification);
    openPluginBtn.setEnabled(false);
    unloadBtn.setEnabled(false); // Disable unload button
    contentLabel.setText(L"选择一个 VST3 乐器插件开始演奏",
                         juce::dontSendNotification);

    // Reset playback UI
    progressSlider.setValue(0.0, juce::dontSendNotification);
    progressSlider.setEnabled(false);
    timeLabel.setText("0:00 / 0:00", juce::dontSendNotification);
  }

  void loadSelectedPlugin() {
    if (isScanningPlugins)
      return;

    int idx = pluginSelector.getSelectedItemIndex();
    if (idx >= 0 && idx < engine.getPluginList().getNumTypes()) {
      auto desc = engine.getPluginList().getTypes()[idx];

      // 显示加载提示框，让用户知道正在加载插件
      auto *loadingWindow = new juce::AlertWindow(
          L"正在加载乐器", L"正在加载插件: " + desc.name + L"\n请稍候...",
          juce::MessageBoxIconType::InfoIcon);
      auto safeLoadingWindow =
          juce::Component::SafePointer<juce::AlertWindow>(loadingWindow);
      loadingWindow->enterModalState(false, nullptr, true);

      // 延迟执行加载，让提示框先显示出来
      auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
      juce::Timer::callAfterDelay(
          10, [safeThis, desc, safeLoadingWindow]() {
        if (safeThis == nullptr) {
          if (safeLoadingWindow != nullptr)
            safeLoadingWindow->exitModalState(0);
          return;
        }

        if (safeThis->engine.loadPlugin(desc)) {
          safeThis->openPluginBtn.setEnabled(true);
          safeThis->unloadBtn.setEnabled(true);
          safeThis->contentLabel.setText(L"已加载: " + desc.name,
                                        juce::dontSendNotification);
          getAppSettings().setLastPluginId(
              desc.createIdentifierString());
          safeThis->openPluginWindow();
        } else {
          juce::AlertWindow::showMessageBoxAsync(
              juce::AlertWindow::WarningIcon, L"插件加载失败",
              safeThis->engine.getLastPluginError());
        }

        // 关闭加载提示
        if (safeLoadingWindow != nullptr)
          safeLoadingWindow->exitModalState(0);
      });
    }
  }

  void openPluginWindow() {
    auto *instance = engine.getVst3Instance();
    if (instance == nullptr)
      return;

    const auto pluginName = instance->getName();

    // Keep the plugin editor alive when the user closes its window. Some
    // instruments, including Ivory, are not safe to repeatedly destroy and
    // recreate their native editor while the processor remains active.
    if (pluginWindow != nullptr) {
      pluginWindow->setVisible(true);
      pluginWindow->toFront(true);
      LOG_DEBUG("Reusing existing plugin window for " + instance->getName());
      return;
    }

    // Heavy sample instruments may continue initialization after the processor
    // instance is created. Give them a full message-loop turn before opening
    // their native editor.
    auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
    const int editorDelayMs =
        pluginName.containsIgnoreCase("Ivory") ? 1500 : 300;
    LOG_DEBUG("Scheduling plugin editor for " + pluginName + " in " +
              juce::String(editorDelayMs) + " ms");

    juce::Timer::callAfterDelay(editorDelayMs, [safeThis, pluginName]() {
      if (safeThis == nullptr)
        return;

      auto *inst = safeThis->engine.getVst3Instance();
      if (inst == nullptr)
        return;

      const bool isIvory = pluginName.containsIgnoreCase("Ivory");
      if (isIvory) {
        LOG_DEBUG("Suspending audio graph before Ivory editor creation");
        safeThis->engine.suspendProcessing(true);
      }

      try {
        LOG_DEBUG("Creating plugin editor for " + pluginName);
        auto *editor = inst->createEditorIfNeeded();
        if (editor != nullptr) {
          LOG_DEBUG("Plugin editor created for " + pluginName);
          auto editorBounds = editor->getBounds();
          int w = editorBounds.getWidth();
          int h = editorBounds.getHeight();
          if (w < 100)
            w = 800;
          if (h < 100)
            h = 600;

          safeThis->pluginWindow = std::make_unique<PluginWindow>(
              inst->getName(), editor, w, h);
          LOG_DEBUG("Plugin window shown for " + pluginName);
        }
      } catch (const std::exception &e) {
        DBG("Plugin editor creation failed: " + juce::String(e.what()));
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, L"插件窗口打开失败",
            L"无法创建插件编辑器窗口，请尝试重新加载插件。");
      } catch (...) {
        DBG("Plugin editor creation failed with unknown exception");
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, L"插件窗口打开失败",
            L"无法创建插件编辑器窗口，请尝试重新加载插件。");
      }

      if (isIvory) {
        safeThis->engine.suspendProcessing(false);
        LOG_DEBUG("Resumed audio graph after Ivory editor creation");
      }
    });
  }

  void closePluginWindow() { pluginWindow.reset(); }

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

      centreWithSize(w, h);
      setVisible(true);
      toFront(true);
    }

    void closeButtonPressed() override { setVisible(false); }
  };

  void togglePlayPause() {
    if (!engine.getMidiPlayer().hasSequence() ||
        engine.getVst3Instance() == nullptr)
      return;
    pendingResumePlayback = false; // Cancel any pending seek resumption
    if (engine.getMidiPlayer().getPlaying()) {
      engine.getMidiPlayer().setPlaying(false);
    } else {
      // Chase-restore all CC/note state at the current position before
      // resuming.  This guarantees pedals, volume, pitch wheel etc.
      // are correct even after a long pause (where allSoundOff ran).
      double pos = engine.getMidiPlayer().getPositionInSamples();
      engine.getMidiPlayer().seekTo(pos);
      engine.getMidiPlayer().setPlaying(true);
    }
  }

  void stopPlayback() {
    pendingResumePlayback = false; // Cancel any pending seek resumption
    engine.getMidiPlayer().setPlaying(false);
    engine.getMidiPlayer().seekTo(0);
  }

  void playNextTrack() {
    SCOPED_TIMER_ALWAYS("MainContentComponent::playNextTrack");
    if (playlist.isEmpty() || engine.getVst3Instance() == nullptr)
      return;

    // 中断任何正在进行的 handleTrackEnd 流程
    isHandlingTrackEnd = false;

    // 先停止当前播放，防止 finishedFlag 在新曲目加载后被误读
    engine.getMidiPlayer().setPlaying(false);

    currentTrackIndex =
        playlist.getNextIndex(currentTrackIndex);

    // If -1 (end of list in sequential), we stop.
    if (currentTrackIndex == -1) {
      stopPlayback();
      return;
    }

    if (const auto *track = playlist.getTrack(currentTrackIndex)) {
      if (loadMidiFile(track->file)) {
        playlistPanel.setCurrentTrackIndex(currentTrackIndex);
        // 延迟播放，给 VSL 插件时间处理重置消息
        ++trackSwitchGeneration;
        int gen = trackSwitchGeneration;
        runLater(100, [gen](MainContentComponent &self) {
          if (self.trackSwitchGeneration != gen)
            return; // 被更新的操作取代
          self.engine.getMidiPlayer().setPlaying(true);
        });
      }
    }
  }

  void playPreviousTrack() {
    SCOPED_TIMER_ALWAYS("MainContentComponent::playPreviousTrack");
    if (playlist.isEmpty() || engine.getVst3Instance() == nullptr)
      return;

    // 中断任何正在进行的 handleTrackEnd 流程
    isHandlingTrackEnd = false;

    // 先停止当前播放
    engine.getMidiPlayer().setPlaying(false);

    currentTrackIndex = playlist.getPreviousIndex(currentTrackIndex);
    if (currentTrackIndex == -1) {
      stopPlayback();
      return;
    }
    if (const auto *track = playlist.getTrack(currentTrackIndex)) {
      if (loadMidiFile(track->file)) {
        playlistPanel.setCurrentTrackIndex(currentTrackIndex);
        // 延迟播放，给 VSL 插件时间处理重置消息
        ++trackSwitchGeneration;
        int gen = trackSwitchGeneration;
        runLater(100, [gen](MainContentComponent &self) {
          if (self.trackSwitchGeneration != gen)
            return; // 被更新的操作取代
          self.engine.getMidiPlayer().setPlaying(true);
        });
      }
    }
  }

  void handleTrackEnd() {
    if (isHandlingTrackEnd)
      return;

    isHandlingTrackEnd = true;
    ++trackSwitchGeneration;
    int myGeneration = trackSwitchGeneration;
    LOG_DEBUG("MainContentComponent::handleTrackEnd - Starting switch");
    SCOPED_TIMER_ALWAYS("MainContentComponent::handleTrackEnd");
    engine.getMidiPlayer().setPlaying(false);
    engine.getMidiPlayer().seekTo(0);

    runAsync([myGeneration](MainContentComponent &self) {
      // 如果代数不匹配，说明用户已经手动切歌，放弃此次自动切歌
      if (self.trackSwitchGeneration != myGeneration) {
        self.isHandlingTrackEnd = false;
        return;
      }

      // Use PlaylistManager's Mode logic
      int next = self.playlist.getNextIndex(self.currentTrackIndex);
      if (next != -1) {
        self.currentTrackIndex = next;
        // Load and play
        if (const auto *track = self.playlist.getTrack(self.currentTrackIndex)) {
          if (self.loadMidiFile(track->file)) {
            self.playlistPanel.setCurrentTrackIndex(self.currentTrackIndex);
            // 延迟启动播放，给 VSL 插件时间处理重置消息
            self.runLater(100, [myGeneration](MainContentComponent &delayedSelf) {
              if (delayedSelf.trackSwitchGeneration != myGeneration)
                return; // 已被其他操作取代
              delayedSelf.engine.getMidiPlayer().setPlaying(true);
              delayedSelf.isHandlingTrackEnd = false;
            });
            return;
          }
        }
      }
      // 没有下一曲或加载失败时重置标志
      self.isHandlingTrackEnd = false;
    });
  }

  bool loadMidiFile(const juce::File &file) {
    if (!file.existsAsFile())
      return false;

    juce::MidiFile mf;
    auto stream = file.createInputStream();
    if (stream == nullptr || !mf.readFrom(*stream))
      return false;

    double sr = engine.getSampleRate() > 0 ? engine.getSampleRate() : 44100.0;
    mf.convertTimestampTicksToSeconds();

    auto seq = std::make_unique<juce::MidiMessageSequence>();
    for (int i = 0; i < mf.getNumTracks(); ++i) {
      if (auto *t = mf.getTrack(i)) {
        for (int j = 0; j < t->getNumEvents(); ++j) {
          auto m = t->getEventPointer(j)->message;
          seq->addEvent(m);
        }
      }
    }
    seq->updateMatchedPairs();
    seq->sort();
    engine.getMidiPlayer().setSequence(std::move(seq), sr);
    trackLabel.setText(file.getFileNameWithoutExtension(),
                       juce::dontSendNotification);
    return true;
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
            if (safeThis->loadMidiFile(result)) {
              safeThis->engine.getMidiPlayer().setPlaying(true);
            }
          }
        });
  }

  // ============================================================
  //  文件关联功能 (Windows Only)
  // ============================================================

#if JUCE_WINDOWS
  /** 检查 .mid / .midi 是否已关联到当前程序的 exe 路径。 */
  bool isFileAssociatedToSelf() {
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\.mid", 0,
                      KEY_READ, &hKey) != ERROR_SUCCESS)
      return false;

    wchar_t value[256] = {};
    DWORD size = sizeof(value);
    DWORD type = 0;
    bool result = false;

    if (RegQueryValueExW(hKey, nullptr, nullptr, &type, (LPBYTE)value, &size) ==
        ERROR_SUCCESS) {
      if (juce::String(value) == "ModernMidiPlayer.MIDIFile") {
        RegCloseKey(hKey);
        hKey = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                          L"Software\\Classes\\ModernMidiPlayer.MIDIFile"
                          L"\\shell\\open\\command",
                          0, KEY_READ, &hKey) == ERROR_SUCCESS) {
          wchar_t cmdValue[1024] = {};
          DWORD cmdSize = sizeof(cmdValue);
          if (RegQueryValueExW(hKey, nullptr, nullptr, &type, (LPBYTE)cmdValue,
                               &cmdSize) == ERROR_SUCCESS) {
            juce::String cmd(cmdValue);
            auto exePath = juce::File::getSpecialLocation(
                               juce::File::currentExecutableFile)
                               .getFullPathName();
            result = cmd.containsIgnoreCase(exePath);
          }
        }
      }
    }

    if (hKey)
      RegCloseKey(hKey);
    return result;
  }

  /** 将 .mid / .midi 文件关联到本应用。写入 HKCU 不需要管理员权限。 */
  bool registerFileAssociation() {
    auto exePath =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getFullPathName();
    juce::String command = "\"" + exePath + "\" \"%1\"";

    bool ok = true;

    auto setRegKey = [&](const wchar_t *subKey,
                         const juce::String &value) -> bool {
      HKEY hKey = nullptr;
      DWORD disposition = 0;
      if (RegCreateKeyExW(HKEY_CURRENT_USER, subKey, 0, nullptr,
                          REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey,
                          &disposition) != ERROR_SUCCESS)
        return false;

      auto wideValue = value.toWideCharPointer();
      auto byteLen = (DWORD)((wcslen(wideValue) + 1) * sizeof(wchar_t));
      bool success =
          RegSetValueExW(hKey, nullptr, 0, REG_SZ, (const BYTE *)wideValue,
                         byteLen) == ERROR_SUCCESS;
      RegCloseKey(hKey);
      return success;
    };

    // .mid 和 .midi 扩展名
    ok &= setRegKey(L"Software\\Classes\\.mid", "ModernMidiPlayer.MIDIFile");
    ok &= setRegKey(L"Software\\Classes\\.midi", "ModernMidiPlayer.MIDIFile");

    // ProgId 描述
    ok &= setRegKey(L"Software\\Classes\\ModernMidiPlayer.MIDIFile",
                    L"MIDI \u97F3\u4E50\u6587\u4EF6");

    // open 命令
    ok &= setRegKey(L"Software\\Classes\\ModernMidiPlayer.MIDIFile"
                    L"\\shell\\open\\command",
                    command);

    // 设置图标为 exe 自身图标
    ok &= setRegKey(L"Software\\Classes\\ModernMidiPlayer.MIDIFile"
                    L"\\DefaultIcon",
                    "\"" + exePath + "\",0");

    // 通知 Shell 刷新
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    return ok;
  }

  /** 移除文件关联（清理注册表条目）。 */
  void removeFileAssociation() {
    auto deleteRegValue = [](const wchar_t *subKey) {
      HKEY hKey = nullptr;
      if (RegOpenKeyExW(HKEY_CURRENT_USER, subKey, 0, KEY_WRITE, &hKey) ==
          ERROR_SUCCESS) {
        RegDeleteValueW(hKey, nullptr);
        RegCloseKey(hKey);
      }
    };

    deleteRegValue(L"Software\\Classes\\.mid");
    deleteRegValue(L"Software\\Classes\\.midi");

    RegDeleteTreeW(HKEY_CURRENT_USER,
                   L"Software\\Classes\\ModernMidiPlayer.MIDIFile");

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  }
#endif // JUCE_WINDOWS

  /** 创建"不再提示"复选框组件（用于 AlertWindow） */
  std::unique_ptr<juce::ToggleButton> createDontShowAgainToggle() {
    auto toggle =
        std::make_unique<juce::ToggleButton>(L"\u4e0d\u518d\u63d0\u793a");
    toggle->setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    toggle->setSize(200, 24);
    return toggle;
  }

  /**
      显示文件关联提示对话框。
      条件：未关联到当前 exe 且用户未选择“不再提示”。
  */
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
            // 用户选择“关联”
            if (safeThis->registerFileAssociation()) {
              getAppSettings().setFileAssociated(true);
              getAppSettings().setDontShowFileAssocPrompt(true);
              getAppSettings().save();
            }
          } else {
            // 用户选择“不关联”
            if (dontShowAgain) {
              getAppSettings().setDontShowFileAssocPrompt(true);
              getAppSettings().save();
            }
          }
        }),
        true);
#endif
  }

  /**
      从 Shell 打开 MIDI 文件（双击文件、命令行传入等场景）。

      行为逻辑：
      - 播放列表为空（冷启动）：新建播放列表，自动加载上次使用的插件并播放。
      - 播放列表已有内容（程序已运行）：添加到现有列表末尾并自动播放。
      - 未加载且无法自动加载插件时，提示用户先手动加载乐器插件。
  */
  void openMidiFileFromShell(const juce::File &file) {
    if (!file.existsAsFile())
      return;

    auto ext = file.getFileExtension().toLowerCase();
    if (ext != ".mid" && ext != ".midi")
      return;

    // 切换到播放列表页面
    showPage("playlist", L"\u97F3\u4E50\u5217\u8868");

    bool hasExistingPlaylist = (playlist.size() > 0);

    if (hasExistingPlaylist) {
      // === 场景B：程序已运行，播放列表已有内容 ===
      if (playlist.contains(file)) {
        // 文件已在列表中，直接定位到它
        int idx = playlist.findTrackIndex(file);
        if (idx >= 0) {
          currentTrackIndex = idx;
          playlistPanel.setCurrentTrackIndex(idx);
        }
      } else {
        // 添加到列表末尾
        playlist.addFile(file);
        playlistPanel.refresh();
        currentTrackIndex = playlist.size() - 1;
        playlistPanel.setCurrentTrackIndex(currentTrackIndex);
      }

      if (loadMidiFile(file)) {
        if (engine.getVst3Instance() != nullptr) {
          // 已有插件，加载并播放
          runLater(150, [](MainContentComponent &self) {
            self.engine.getMidiPlayer().setPlaying(true);
          });
        } else {
          // 没有加载乐器插件 → 尝试自动加载上次使用的插件
          tryLoadLastPluginWithDialog();
        }
      }
    } else {
      // === 场景A：冷启动，播放列表为空 ===
      playlist.clear();
      currentPlaylistFile = juce::File();
      currentTrackIndex = -1;

      playlist.addFile(file);
      playlistPanel.refresh();
      currentTrackIndex = 0;
      playlistPanel.setCurrentTrackIndex(0);

      if (loadMidiFile(file)) {
        if (engine.getVst3Instance() != nullptr) {
          // 已有插件，直接播放
          runLater(150, [](MainContentComponent &self) {
            self.engine.getMidiPlayer().setPlaying(true);
          });
        } else {
          // 尝试自动加载上次使用的插件
          tryLoadLastPluginWithDialog();
        }
      }
    }

    // 清除 shell-open 标记
    pendingShellOpen = false;

    getAppSettings().setLastMidiDirectory(
        file.getParentDirectory().getFullPathName());
  }

  /** 标记有文件关联打开挂起，抑制通用音频设备警告弹窗。 */
  void setPendingShellOpen(bool pending) { pendingShellOpen = pending; }

  /**
      尝试自动加载上次使用的插件，并显示加载提示。
      如果没有上次使用的插件记录，则提示用户手动加载。
      加载成功后自动播放当前已加载的 MIDI 文件。
  */
  void tryLoadLastPluginWithDialog() {
    // 前置检查：无音频设备时不尝试加载插件
    if (!engine.hasAudioDevice()) {
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

    // 从未加载过插件 → 提示用户手动加载
    if (lastPluginId.isEmpty()) {
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::WarningIcon, L"无法播放",
          L"请先加载一个乐器插件以开始播放。");
      return;
    }

    // 在插件列表中查找上次使用的插件
    auto &list = engine.getPluginList();
    int pluginIndex = -1;
    for (int i = 0; i < list.getNumTypes(); ++i) {
      if (list.getTypes()[i].createIdentifierString() == lastPluginId) {
        pluginIndex = i;
        break;
      }
    }

    if (pluginIndex < 0) {
      // 插件列表中找不到上次使用的插件
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::WarningIcon, L"无法播放",
          L"上次使用的乐器插件未找到，请手动加载一个乐器插件。");
      return;
    }

    auto pluginName = list.getTypes()[pluginIndex].name;

    // 显示加载提示
    auto *loadingWindow = new juce::AlertWindow(
        L"正在加载乐器", L"正在加载插件: " + pluginName + L"\n请稍候...",
        juce::MessageBoxIconType::InfoIcon);
    auto safeLoadingWindow =
        juce::Component::SafePointer<juce::AlertWindow>(loadingWindow);
    loadingWindow->enterModalState(false, nullptr, true);

    // 延迟执行加载（让提示框先显示出来）
    auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
    juce::Timer::callAfterDelay(
        100, [safeThis, pluginIndex, safeLoadingWindow]() {
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

      auto desc = pluginList.getTypes()[pluginIndex];
      safeThis->pluginSelector.setSelectedId(pluginIndex + 1,
                                             juce::dontSendNotification);

      if (safeThis->engine.loadPlugin(desc)) {
        safeThis->openPluginBtn.setEnabled(true);
        safeThis->unloadBtn.setEnabled(true);
        safeThis->contentLabel.setText(L"已加载: " + desc.name,
                                       juce::dontSendNotification);

        // 加载成功 → 打开插件窗口（不自动播放）
        safeThis->openPluginWindow();
      }

      // 关闭加载提示
      if (safeLoadingWindow != nullptr)
        safeLoadingWindow->exitModalState(0);
    });
  }

  void showAudioSettings() {
    audioSettingsWindow.deleteAndZero();
    auto *content = new AudioSettingsContent(engine);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(content);
    options.dialogTitle = L"音频设置";
    options.dialogBackgroundColour =
        fluentLookAndFeel.getColors().cardBackground;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    audioSettingsWindow = options.launchAsync();
  }

  void showBackgroundSettings() {
    backgroundSettingsWindow.deleteAndZero();
    auto *content = new BackgroundSettingsDialog(background, this);
    content->setLookAndFeel(&fluentLookAndFeel);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(content);
    options.dialogTitle = L"背景设置";
    options.dialogBackgroundColour = juce::Colours::transparentBlack;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = false;
    backgroundSettingsWindow = options.launchAsync();
  }

  void showFontSettings() {
    fontSettingsWindow.deleteAndZero();
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
    options.dialogTitle = L"字体设置";
    options.dialogBackgroundColour = juce::Colours::transparentBlack;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = false;
    fontSettingsWindow = options.launchAsync();
  }

  void closeSettingsWindows() {
    audioSettingsWindow.deleteAndZero();
    backgroundSettingsWindow.deleteAndZero();
    fontSettingsWindow.deleteAndZero();
  }

  void updatePluginList() {
    // Capture current state before clearing
    juce::String idToRestore;
    if (engine.getVst3Instance() != nullptr) {
      idToRestore = getAppSettings().getLastPluginId();
    }

    pluginSelector.clear(juce::dontSendNotification);
    auto &list = engine.getPluginList();

    int idToSelect = 0;
    for (int i = 0; i < list.getNumTypes(); ++i) {
      auto desc = list.getTypes()[i];
      pluginSelector.addItem(desc.name, i + 1);

      if (idToRestore.isNotEmpty() &&
          desc.createIdentifierString() == idToRestore) {
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

    auto &list = engine.getPluginList();
    for (int i = 0; i < list.getNumTypes(); ++i) {
      if (list.getTypes()[i].createIdentifierString() == lastPluginId) {
        pluginSelector.setSelectedId(i + 1, juce::dontSendNotification);
        if (engine.loadPlugin(list.getTypes()[i])) {
          openPluginBtn.setEnabled(true);
          contentLabel.setText(L"已加载: " + list.getTypes()[i].name,
                               juce::dontSendNotification);
          DBG("Auto-loaded last plugin: " + list.getTypes()[i].name);
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
    engine.setMasterVolume(settings.getMasterVolume());

    // 恢复播放模式
    int savedMode = settings.getPlayMode();
    playlist.setPlaybackMode(
        static_cast<PlaylistManager::PlaybackMode>(savedMode));

    // Apply saved font settings to LookAndFeel
    fluentLookAndFeel.setUIFont(settings.getUIFontName());
    fluentLookAndFeel.setPlaylistFont(settings.getPlaylistFontName());

    // Refresh components that depend on fonts
    playlistPanel.refresh();
  }

  void saveSettings() {
    getAppSettings().setMasterVolume((float)volumeSlider.getValue());
    getAppSettings().setPlayMode(static_cast<int>(playlist.getPlaybackMode()));
    getAppSettings().save();
  }

  // Audio Settings content - using LookAndFeel colors
  struct AudioSettingsContent : public juce::Component {
    AudioSettingsContent(AudioEngine &e)
        : engine(e),
          selector(e.getDeviceManager(), 0, 2, 0, 2, true, true, true, false) {
      setSize(520, 500);
      setOpaque(false);

      // Apply dark theme colors to the selector
      juce::Colour darkBg(0xFF2D2D2D);
      juce::Colour textColour(0xFFE0E0E0);

      selector.setColour(juce::ComboBox::backgroundColourId, darkBg);
      selector.setColour(juce::ComboBox::textColourId, textColour);
      selector.setColour(juce::ComboBox::outlineColourId,
                         juce::Colour(0xFF4A4A4A));
      selector.setColour(juce::TextButton::buttonColourId, darkBg);
      selector.setColour(juce::TextButton::textColourOnId, textColour);
      selector.setColour(juce::TextButton::textColourOffId, textColour);
      selector.setColour(juce::Label::textColourId, textColour);
      selector.setColour(juce::ListBox::backgroundColourId, darkBg);
      selector.setColour(juce::ListBox::textColourId, textColour);

      addAndMakeVisible(selector);
    }

    ~AudioSettingsContent() override { engine.saveAudioDeviceSettings(); }

    void paint(juce::Graphics &g) override {
      if (auto *laf = dynamic_cast<FluentLookAndFeel *>(&getLookAndFeel())) {
        g.fillAll(laf->getColors().cardBackground);
      } else {
        g.fillAll(juce::Colour(0xFF1F1F1F));
      }
    }

    void resized() override {
      selector.setBounds(getLocalBounds().reduced(10));
    }

    AudioEngine &engine;
    juce::AudioDeviceSelectorComponent selector;
  };

  // Font Settings content
  struct FontSettingsContent : public juce::Component {
    std::function<void()> onSettingsChanged;

    FontSettingsContent(FluentLookAndFeel &laf) : fluentLookAndFeel(laf) {
      setSize(420, 360);
      setOpaque(false);

      // Get system fonts
      availableFonts = juce::Font::findAllTypefaceNames();

      // Implement Fallback Logic for Playlist Font
      juce::String currentPlaylistFont = getAppSettings().getPlaylistFontName();
      bool fontExists = false;
      if (availableFonts.contains(currentPlaylistFont)) {
        fontExists = true;
      } else {
        // Check mapped name
        // (Simple check: iterate mappings or easier: just check if
        // availableFonts contains the display name if that was stored? No, we
        // store real names. but maybe system has changed or mapped name logic
        // needed) For now, simpler check:
        juce::String displayName =
            getDisplayName(currentPlaylistFont); // This might return Chinese
        if (availableFonts.contains(displayName))
          fontExists = true;
      }

      if (!fontExists) {
        // Fallback to Microsoft YaHei UI
        juce::String fallback = "Microsoft YaHei UI";
        if (availableFonts.contains(fallback)) {
          getAppSettings().setPlaylistFontName(fallback);
          fluentLookAndFeel.setPlaylistFont(fallback);
        } else if (availableFonts.contains("Microsoft YaHei")) {
          getAppSettings().setPlaylistFontName("Microsoft YaHei");
          fluentLookAndFeel.setPlaylistFont("Microsoft YaHei");
        }
      }

      // UI Font label
      addAndMakeVisible(uiFontLabel);
      uiFontLabel.setText(L"界面字体:", juce::dontSendNotification);
      uiFontLabel.setColour(juce::Label::textColourId, juce::Colours::white);

      // UI Font ComboBox
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

      // Playlist Font label
      addAndMakeVisible(playlistFontLabel);
      playlistFontLabel.setText(L"列表字体:", juce::dontSendNotification);
      playlistFontLabel.setColour(juce::Label::textColourId,
                                  juce::Colours::white);

      // Playlist Font ComboBox
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

          // Refresh list to show updated recent
          // populateFontCombo(playlistFontCombo, fontName, true);
          // Note: Re-populating immediately might reset scroll/focus, so maybe
          // skip for now
        }
      };

      // Playlist Font Size Label
      addAndMakeVisible(fontSizeLabel);
      fontSizeLabel.setText(L"列表字号:", juce::dontSendNotification);
      fontSizeLabel.setColour(juce::Label::textColourId, juce::Colours::white);

      // Playlist Font Size Slider
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

      // Info label
      addAndMakeVisible(infoLabel);
      infoLabel.setText(L"更改字体后可能需要重启应用才能完全生效",
                        juce::dontSendNotification);
      infoLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
      infoLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    }

    void paint(juce::Graphics &g) override {
      if (auto *laf = dynamic_cast<FluentLookAndFeel *>(&getLookAndFeel())) {
        g.fillAll(laf->getColors().cardBackground);
      } else {
        g.fillAll(juce::Colour(0xFF202020));
      }
    }

    ~FontSettingsContent() override { getAppSettings().save(); }

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

      // 1. Top Section: Pinned + Recent (Playlist only)
      if (isPlaylist) {
        combo.addSectionHeading(L"常用 & 最近");

        // Pinned fonts (Always show if available)
        juce::StringArray pinned = {"Microsoft YaHei UI", "SimHei", "SimSun"};

        // Helper to add if available
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

        // Recent fonts
        auto recent = getAppSettings().getRecentFonts();
        for (const auto &f : recent)
          tryAdd(f);

        combo.addSeparator();
      }

      // 2. All Others
      for (const auto &font : availableFonts) {
        if (!topFonts.contains(font) &&
            !topFonts.contains(getDisplayName(font))) {
          addItem(font, 0);
        }
      }
    }

    juce::String getDisplayName(const juce::String &name) { return name; }

    void resized() override {
      auto area = getLocalBounds().reduced(20);
      int labelWidth = 80;
      int rowHeight = 40;
      int gap = 10;

      // UI Font
      auto row1 = area.removeFromTop(rowHeight);
      uiFontLabel.setBounds(row1.removeFromLeft(labelWidth));
      uiFontCombo.setBounds(row1.reduced(0, 5));
      area.removeFromTop(gap);

      // Playlist Font
      auto row2 = area.removeFromTop(rowHeight);
      playlistFontLabel.setBounds(row2.removeFromLeft(labelWidth));
      playlistFontCombo.setBounds(row2.reduced(0, 5));
      area.removeFromTop(gap);

      // Playlist Size
      auto row3 = area.removeFromTop(rowHeight);
      fontSizeLabel.setBounds(row3.removeFromLeft(labelWidth));
      fontSizeSlider.setBounds(row3.reduced(0, 5));

      area.removeFromTop(gap * 2);
      infoLabel.setBounds(area.removeFromTop(30));
    }

    FluentLookAndFeel &fluentLookAndFeel;
    juce::StringArray availableFonts;
    juce::StringArray fontRealNames_UI;
    juce::StringArray fontRealNames_Playlist;

    juce::Label uiFontLabel, playlistFontLabel, fontSizeLabel, infoLabel;
    juce::ComboBox uiFontCombo, playlistFontCombo;
    juce::Slider fontSizeSlider;
  };

  // ToastComponent and TransparentButton move to CustomControls.h

  class ScrollingLabel : public juce::Component, public juce::Timer {
  public:
    ScrollingLabel() = default;
    ~ScrollingLabel() override { stopTimer(); }

    void setText(const juce::String &newText, juce::NotificationType) {
      if (text != newText) {
        text = newText;
        scrollOffset = 0.0f;
        repaint();
      }
    }

    void setFont(const juce::Font &newFont) {
      font = newFont;
      repaint();
    }

    void setColour(int colourId, juce::Colour colour) {
      if (colourId == juce::Label::textColourId)
        textColour = colour;
    }

    void paint(juce::Graphics &g) override {
      g.setFont(font);
      g.setColour(textColour);

      float textWidth = getTextWidth(text);
      float availableWidth = (float)getWidth();

      if (textWidth <= availableWidth || !isHovered) {
        // Text fits or not hovered - draw normally (left aligned)
        g.drawText(text, getLocalBounds(), juce::Justification::centredLeft,
                   true);
      } else {
        // Scrolling mode - draw text with offset
        float x = -scrollOffset;
        g.drawText(text, (int)x, 0, (int)textWidth + 20, getHeight(),
                   juce::Justification::centredLeft, false);
      }
    }

    void mouseDown(const juce::MouseEvent &e) override {
      // Forward click to parent to allow "deselect all" behavior
      if (auto *parent = getParentComponent())
        parent->mouseDown(e.getEventRelativeTo(parent));
    }

    void mouseEnter(const juce::MouseEvent &) override {
      isHovered = true;
      float textWidth = getTextWidth(text);
      if (textWidth > (float)getWidth()) {
        scrollOffset = 0.0f;
        scrollDirection = 1.0f;
        startTimerHz(30);
      }
    }

    void mouseExit(const juce::MouseEvent &) override {
      isHovered = false;
      stopTimer();
      scrollOffset = 0.0f;
      repaint();
    }

    void timerCallback() override {
      float textWidth = getTextWidth(text);
      float maxScroll = textWidth - (float)getWidth() + 20.0f;

      scrollOffset += 1.5f;

      // Loop back to start when reaching end
      if (scrollOffset >= maxScroll + 50.0f) {
        scrollOffset = -50.0f; // Start from slightly before visible area
      }

      repaint();
    }

    float getTextWidth(const juce::String &s) const {
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
      return (float)font.getStringWidth(s);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    }

  private:
    juce::String text;
    juce::Font font{juce::FontOptions(14.0f)};
    juce::Colour textColour{juce::Colours::white};
    float scrollOffset = 0.0f;
    float scrollDirection = 1.0f;
    bool isHovered = false;
  };

  class SeekUpdater : public juce::AsyncUpdater {
  public:
    SeekUpdater(MainContentComponent &o) : owner(o) {}
    void handleAsyncUpdate() override {
      LOG_DEBUG("[FREEZE_DIAG] SeekUpdater::handleAsyncUpdate - start");
      SCOPED_TIMER_ALWAYS("MainContentComponent::performSeek");
      double dur = owner.engine.getMidiPlayer().getDurationInSamples();
      if (dur > 0) {
        double val = owner.pendingSeekValue.load();
        LOG_DEBUG("[FREEZE_DIAG] SeekUpdater: calling seekTo(" +
                  juce::String(val * dur) + ")");
        owner.engine.getMidiPlayer().seekTo(val * dur);
        LOG_DEBUG("[FREEZE_DIAG] SeekUpdater: seekTo returned");
      }
      LOG_DEBUG("[FREEZE_DIAG] SeekUpdater::handleAsyncUpdate - end");
    }

  private:
    MainContentComponent &owner;
  };

  SeekUpdater seekUpdater{*this};
  std::atomic<double> pendingSeekValue{0.0};

  class UIWatchdog : public juce::Thread {
  public:
    UIWatchdog(MainContentComponent &o)
        : juce::Thread("UIWatchdog"), owner(o) {}

    void run() override {
      while (!threadShouldExit()) {
        auto currentTime = juce::Time::getMillisecondCounter();
        auto lastHeartbeat = owner.lastHeartbeatTime.load();
        auto diff = currentTime - lastHeartbeat;

        if (diff > 5000) { // Log warning at 5s
          static uint32_t lastWarnTime = 0;
          if (currentTime - lastWarnTime > 5000) {
            LOG_DEBUG("CRITICAL: UI HEARTBEAT LAG: " + juce::String(diff) +
                      " ms");
            lastWarnTime = currentTime;
          }
        }

        if (diff > 30000) { // 30 seconds timeout
          if (!owner.isWatchdogDialogActive.load()) {
            LOG_DEBUG("!!! UI WATCHDOG: MT FREEZE DETECTED !!!");
            // 使用 SafePointer 防止组件销毁后访问悬空指针
            auto safeOwner =
                juce::Component::SafePointer<MainContentComponent>(&owner);
            juce::MessageManager::callAsync([safeOwner]() {
              if (safeOwner != nullptr)
                safeOwner->handleFreezeRecovery();
            });
          }
        }
        wait(500);
      }
    }

  private:
    MainContentComponent &owner;
  };

  // Spinner for scan progress - optimized to only run when visible
  class SpinnerComponent : public juce::Component, public juce::Timer {
  public:
    SpinnerComponent() = default;
    ~SpinnerComponent() override { stopTimer(); }

    void visibilityChanged() override {
      if (isVisible() && isShowing())
        startTimerHz(30);
      else
        stopTimer();
    }

    void timerCallback() override {
      angle += 0.15f;
      if (angle > juce::MathConstants<float>::twoPi * 100.0f)
        angle = 0.0f; // Prevent overflow
      repaint();
    }

    void paint(juce::Graphics &g) override {
      auto bounds = getLocalBounds().toFloat().reduced(8.0f);
      g.setColour(juce::Colour(0xFFFF8C00));
      juce::Path arc;
      arc.addArc(bounds.getX(), bounds.getY(), bounds.getWidth(),
                 bounds.getHeight(), angle, angle + 4.0f, true);
      g.strokePath(arc, juce::PathStrokeType(2.5f));
    }

  private:
    float angle = 0.0f;
  };

  // === Members ===
  AudioEngine &engine;
  FluentLookAndFeel fluentLookAndFeel;
  // 嵌入式 Tooltip，直接在父窗口中绘制，确保完美圆角
  EmbeddedTooltip embeddedTooltip;

  BackgroundComponent background;
  NavigationSidebar navigation;

  juce::Label pageTitle;
  juce::ComboBox pluginSelector;
  TransparentButton loopModeBtn; // Added LoopMode button
  SvgButton exportBtn{R"(<svg width="24" height="24" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg"><path d="M12 15V3M12 15L8 11M12 15L16 11" stroke="#000000" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/><path d="M20 16V18C20 19.1046 19.1046 20 18 20H6C4.89543 20 4 19.1046 4 18V16" stroke="#000000" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>)"};
  juce::Slider volumeSlider;
  TransparentButton scanBtn, unloadBtn, openPluginBtn;
  SpinnerComponent scanSpinner;

  juce::Label contentLabel;
  PlaylistManager playlist;
  PlaylistPanel playlistPanel;

  // Re-added missing transport and state members
  juce::Component transportBar;
  ScrollingLabel trackLabel;
  juce::Label timeLabel;
  juce::Slider progressSlider;
  TransparentButton prevBtn, playBtn, nextBtn, stopBtn, volumeBtn;
  std::unique_ptr<juce::FileChooser> fileChooser;
  juce::String currentPage = "library";
  int currentTrackIndex = -1;

  std::unique_ptr<PluginWindow> pluginWindow;
  juce::Component::SafePointer<juce::DialogWindow> audioSettingsWindow;
  juce::Component::SafePointer<juce::DialogWindow> backgroundSettingsWindow;
  juce::Component::SafePointer<juce::DialogWindow> fontSettingsWindow;
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

  std::atomic<int64_t> lastHeartbeatTime{0};
  std::atomic<bool> isWatchdogDialogActive{false};
  UIWatchdog watchdog;

  juce::File currentPlaylistFile;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainContentComponent)
};
