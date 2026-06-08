#pragma once

// 文件关联只需要 Registry/Shell 的窄接口声明。
// 不在此处包含 <windows.h>：它会与 Win11Helpers.h 的 HWND 声明冲突，
// 还会把 min/max/BYTE 等宏带进来，影响 BackgroundComponent.h。
// Win11Helpers.h 已声明 HWND、HRESULT、DWORD、BOOL。
#if JUCE_WINDOWS
extern "C" {
// Registry API 所需类型。
// DWORD 也会在 Win11Helpers.h 中声明；MSVC 允许相同 typedef 重复出现。
typedef unsigned long DWORD;
typedef void *HKEY;
typedef unsigned char BYTE;
typedef BYTE *LPBYTE;

// Registry 句柄常量。
#ifndef HKEY_CURRENT_USER
#define HKEY_CURRENT_USER ((HKEY)(unsigned long long)0x80000001)
#endif

// Registry 访问权限。
#ifndef KEY_READ
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#endif

// Registry 选项与类型。
#ifndef REG_SZ
#define REG_OPTION_NON_VOLATILE 0x00000000
#define REG_SZ 1
#endif

// WinAPI 成功返回码。
#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS 0L
#endif

// Registry 函数，来自 advapi32.dll。
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

// Shell 通知函数，来自 shell32.dll。
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
        // 离线导出在工作线程中驱动 AudioEngine：上报进度并把取消请求转交给渲染循环。
        // UI 线程只负责模态进度窗口和结果提示，避免长时间阻塞消息循环。
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
      - 超过 5 秒记录卡顿日志，超过 30 秒触发恢复流程。
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

    // 插件加载成功前禁止卸载操作。
    unloadBtn.setEnabled(false);

    addChildComponent(scanSpinner);

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

    // 嵌入式 Tooltip（最后添加，确保在最上层）
    addAndMakeVisible(embeddedTooltip);
    // 使用 EmbeddedTooltip 自身的 MouseListener 逻辑，递归监听所有子组件
    addMouseListener(&embeddedTooltip, true);

    addAndMakeVisible(trackLabel);
    trackLabel.setFont(fluentLookAndFeel.getDefaultFont(14.0f, true));
    trackLabel.setColour(juce::Label::textColourId,
                         fluentLookAndFeel.getColors().textPrimary);
    trackLabel.setInterceptsMouseClicks(true, false);

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
    progressSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    progressSlider.setVelocityBasedMode(false);
    progressSlider.setScrollWheelEnabled(false);
    progressSlider.textFromValueFunction = [this](double v) {
      double dur = engine.getMidiPlayer().getDurationInSamples();
      double sr = engine.getSampleRate() > 0 ? engine.getSampleRate() : 44100.0;
      return formatTime((int)(v * dur / sr));
    };

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
    // 启动时同步保存的主音量到 UI 与 AudioEngine。
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

    // 背景点击用于取消播放列表选择。
    background.addMouseListener(this, false);

    // 曲目加载前进度条不可操作。
    progressSlider.setEnabled(false);

    startTimerHz(30);
    updatePluginList();

    auto savedColor =
        juce::Colour::fromString(getAppSettings().getThemeAccentColor());
    fluentLookAndFeel.updateAccentColor(savedColor);
    fluentLookAndFeel.updateAccentColor(savedColor);

    // 应用 Windows 11 窗口样式，并统一调度启动期音频设备提示。
    runLater(150, [](MainContentComponent &self) {
      if (auto *topLevel = self.getTopLevelComponent())
        Win11Helpers::applyWin11Style(topLevel);

      // 如果正在通过文件关联打开 MIDI 文件，跳过通用音频设备警告，
      // 由 openMidiFileFromShell 流程统一处理错误提示，避免对话框堆叠。
      if (self.pendingShellOpen)
        return;

      if (self.engine.isFirstRunAudio()) {
        // 首次运行且没有音频设置时，引导用户配置输出设备。
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
        // 非首次运行但当前没有可用音频设备。
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, L"音频设备不可用",
            L"未检测到可用的音频输出设备，请检查您的音频设备是否正常工作。"
            L"\n\n您可以在侧栏「设置」中手动配置音频输出。");
      } else if (self.engine.wasDeviceRestoredWithFallback()) {
        // 保存的设备缺失时已回退到系统默认设备。
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
    const bool isExporting = engine.isOfflineExportActive();
    if (!isExporting && player.hasSequence() &&
        std::abs(player.getSequenceSampleRate() - currentSampleRate) >= 0.01)
      player.setSampleRate(currentSampleRate);

    auto currentTime = juce::Time::getMillisecondCounter();
    bool isPlaying = engine.getMidiPlayer().getPlaying();

    bool hasTrack = player.hasSequence();
    if (progressSlider.isEnabled() != hasTrack) {
      progressSlider.setEnabled(hasTrack);
    }

    // 拖动或刚完成 seek 时暂缓刷新，避免进度条被计时器回拉。
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

    if (isPlaying != lastPlayingState) {
      lastPlayingState = isPlaying;
      repaint();
    }

    // 检查播放结束标志（仅在用户未拖动进度条时）。
    // 延迟检查防止在用户快速跳转时触发“下一曲”，避免状态冲突和播放中断。
    if (!isExporting && !isUserDraggingProgress && player.hasFinished()) {
      LOG_DEBUG("[FREEZE_DIAG] TC: calling handleTrackEnd");
      handleTrackEnd();
      LOG_DEBUG("[FREEZE_DIAG] TC: handleTrackEnd returned");
    }

    // 只在 timerCallback 执行时间异常时输出完成日志，避免每次都输出导致日志爆炸
    auto tcEndTime = juce::Time::getMillisecondCounter();
    auto tcDuration = tcEndTime - now;
    if (tcDuration > 20) { // 超过 20ms 视为慢速执行。
      LOG_DEBUG("[FREEZE_DIAG] TC completed (slow: " +
                juce::String(tcDuration) + "ms)");
    }

    // 根据插件加载状态启用/禁用播放控制按钮。
    bool hasPlugin = engine.getVst3Instance() != nullptr;
    if (playBtn.isEnabled() != hasPlugin) {
      playBtn.setEnabled(hasPlugin);
      stopBtn.setEnabled(hasPlugin);
      prevBtn.setEnabled(hasPlugin);
      nextBtn.setEnabled(hasPlugin);
      // progressSlider 只有在有插件且有曲目时才启用。
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

    // seekTo 已改为异步，这里只保留很短的恢复播放缓冲，避免 UI 抖动。
    if (!isExporting && pendingResumePlayback) {
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

  void handleFreezeRecovery() {
    LOG_DEBUG("!!! UI WATCHDOG TRIGGERED RECOVERY !!!");
    // 防止恢复弹窗重复堆叠。
    if (isWatchdogDialogActive.load())
      return;

    isWatchdogDialogActive.store(true);

    // 紧急降载 BackgroundComponent，优先恢复消息线程响应。
    background.emergencyReset();

    if (lastPlayingState || engine.getMidiPlayer().getPlaying()) {
      engine.getMidiPlayer().setPlaying(true);
    }

    repaint();

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
    bool isPlaying = engine.getMidiPlayer().getPlaying();

    drawIconButton(g, scanBtn, L"\uE9A1");       // 搜索/扫描
    drawIconButton(g, unloadBtn, L"\uE74D");     // 删除
    drawIconButton(g, openPluginBtn, L"\uE8A7"); // 打开窗格

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
    switch (playlist.getPlaybackMode()) {
    case PlaylistManager::PlaybackMode::Sequential:
      isSequential = true;
      break;
    case PlaylistManager::PlaybackMode::LoopList:
      loopIcon = L"\uE8EE"; // 全部循环
      break;
    case PlaylistManager::PlaybackMode::LoopSingle:
      loopIcon = L"\uE8ED"; // 单曲循环
      break;
    case PlaylistManager::PlaybackMode::Shuffle:
      loopIcon = L"\uE8B1"; // 随机播放
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
    if (scanSpinner.isVisible()) {
      scanSpinner.setBounds(header.removeFromRight(btnSize));
    }
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

  // === 导航 ===
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

  // === 按钮 ===
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
    auto next = static_cast<PlaylistManager::PlaybackMode>(
        (static_cast<int>(current) % 4) + 1);
    playlist.setPlaybackMode(next);
    getAppSettings().setPlayMode((int)next);

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
    embeddedTooltip.hideTooltip();

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

  // === 滑块 ===
  void sliderValueChanged(juce::Slider *s) override {
    if (s == &volumeSlider) {
      float vol = (float)s->getValue();
      engine.setMasterVolume(vol);
      getAppSettings().setMasterVolume(vol);
      repaint();
    } else if (s == &progressSlider) {
      // 点击跳转时记录 seek 时间，避免 timerCallback 立即回写旧位置。
      if (!isUserDraggingProgress)
        lastSeekRequestTime.store(juce::Time::getMillisecondCounter());

      // 拖动中只更新显示时间，真正的 seek 在拖动结束后异步执行。
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

      // 使用 AsyncUpdater 把 seek 移出拖动事件栈。
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

  // === 播放列表 ===
  void playlistTrackSelected(int index) override {
    // 选择逻辑由 PlaylistPanel 内部处理。
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

    // 中断正在进行的自动切歌流程。
    isHandlingTrackEnd = false;
    engine.getMidiPlayer().setPlaying(false);

    currentTrackIndex = index;
    if (const auto *track = playlist.getTrack(index)) {
      if (loadMidiFile(track->file)) {
        playlistPanel.setCurrentTrackIndex(index);
        // 延迟播放给 VSL 等插件处理重置消息；generation 防止旧回调过期后误播放。
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

    // 保存当前播放曲目的文件对象，用于添加文件后重新定位。
    const auto *currentlyPlaying = (currentTrackIndex >= 0)
                                       ? playlist.getTrack(currentTrackIndex)
                                       : nullptr;
    juce::File currentPlayingFile;
    if (currentlyPlaying)
      currentPlayingFile = currentlyPlaying->file;

    // 分离新文件与重复文件。
    for (auto &f : files) {
      juce::File file(f);
      if (playlist.contains(file)) {
        duplicateFiles.add(f);
      } else {
        newFiles.add(f);
      }
    }

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
        return;
      if (result == 1)
        action = DupAction::AddNewOnly;
      else if (result == 2)
        action = DupAction::OverwriteExisting;
    }

    // 两种导入模式都会先添加新文件。
    bool anythingChanged = false;
    for (const auto &f : newFiles) {
      if (playlist.addFile(juce::File(f), false))
        anythingChanged = true;
    }

    // 覆盖模式下刷新重复条目的缓存元数据。
    std::vector<int> overwrittenRows;
    if (action == DupAction::OverwriteExisting) {
      for (const auto &f : duplicateFiles) {
        int idx = playlist.findTrackIndex(juce::File(f));
        if (idx >= 0) {
          playlist.refreshTrack(idx);
          overwrittenRows.push_back(idx);
          anythingChanged = true;
        }
      }
    }

    if (anythingChanged) {
      // 如果有曲目正在播放，重新查找其在列表中的索引。
      if (currentlyPlaying && currentPlayingFile.existsAsFile()) {
        int newIndex = playlist.findTrackIndex(currentPlayingFile);
        if (newIndex != -1 && newIndex != currentTrackIndex) {
          currentTrackIndex = newIndex;
          playlistPanel.setCurrentTrackIndex(newIndex);
        }
      }

      playlistPanel.refresh();

      // 对被覆盖的条目播放闪烁动画提示。
      if (!overwrittenRows.empty()) {
        playlistPanel.startDropAnimation(overwrittenRows, false);
      }
    }
  }

  // === 文件拖放 ===
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

  // === 背景 ===
  void backgroundSettingsChanged(bool reapplyEffects) override {
    if (reapplyEffects)
      background.loadAsync();

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

  void onAccentColorChanged(juce::Colour newColor) {
    getAppSettings().setThemeAccentColor(newColor.toString());
    fluentLookAndFeel.updateAccentColor(newColor);

    // 先重绘视觉负载较高的区域，避免主题色切换残留。
    navigation.repaint();
    playlistPanel.repaint();
    repaint();

    // 仅在 BackgroundComponent 过渡到目标色后广播，避免频繁刷新 LookAndFeel。
    if (newColor == background.getTargetAccentColor()) {
      sendLookAndFeelChange();
    }
  }

  // === 键盘快捷键 ===
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
      // Escape 关闭已打开的插件窗口。
      if (pluginWindow && pluginWindow->isVisible()) {
        pluginWindow->setVisible(false);
        return true;
      }
    }
    return false;
  }

  // === 拖放视觉反馈 ===
  void fileDragEnter(const juce::StringArray &, int, int) override {
    isDragOver = true;
    repaint();
  }

  void fileDragExit(const juce::StringArray &) override {
    isDragOver = false;
    repaint();
  }

  void mouseDown(const juce::MouseEvent &e) override {
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
                      const bool needsTrackSwap = (selectedTrackIdx != currentTrackIndex);
                      auto originalState = captureExportPlaybackState();

                      if (needsTrackSwap) {
                          if (auto* targetTrack = playlist.getTrack(selectedTrackIdx)) {
                              if (!loadMidiFile(targetTrack->file)) {
                                  restoreExportPlaybackState(originalState);
                                  juce::AlertWindow::showMessageBoxAsync(
                                      juce::AlertWindow::WarningIcon,
                                      L"导出失败",
                                      L"无法加载待导出的 MIDI 文件。");
                                  return;
                              }
                          } else {
                              restoreExportPlaybackState(originalState);
                              juce::AlertWindow::showMessageBoxAsync(
                                  juce::AlertWindow::WarningIcon,
                                  L"导出失败",
                                  L"未找到待导出的曲目。");
                              return;
                          }
                      }

                      std::unique_ptr<OfflineExportThread> thread;
                      {
                          AudioEngine::OfflineExportSession exportSession(engine, s);
                          thread = std::make_unique<OfflineExportThread>(engine, result, s);
                          thread->runThread(); // 阻塞当前模态导出流程。
                      }
                      restoreExportPlaybackState(originalState);

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

  struct ExportPlaybackState {
      int trackIndex = -1;
      juce::File file;
      double position = 0.0;
      bool wasPlaying = false;
      bool hadPendingResume = false;
      bool wasHandlingTrackEnd = false;
  };

  ExportPlaybackState captureExportPlaybackState() {
      // 导出可能临时切换到其他曲目；进入导出前保存播放现场并终止待恢复播放。
      // trackSwitchGeneration 同步递增，防止旧的延迟播放回调在导出期间误触发。
      ExportPlaybackState state;
      state.trackIndex = currentTrackIndex;
      if (currentTrackIndex >= 0) {
          if (auto* track = playlist.getTrack(currentTrackIndex))
              state.file = track->file;
      }
      state.position = engine.getMidiPlayer().getPositionInSamples();
      state.wasPlaying = engine.getMidiPlayer().getPlaying();
      state.hadPendingResume = pendingResumePlayback;
      state.wasHandlingTrackEnd = isHandlingTrackEnd;
      ++trackSwitchGeneration;

      pendingResumePlayback = false;
      isHandlingTrackEnd = false;
      engine.getMidiPlayer().setPlaying(false);
      return state;
  }

  void restoreExportPlaybackState(const ExportPlaybackState& state) {
      // 导出结束后恢复用户原本的曲目、位置和播放状态。
      // 再次递增 trackSwitchGeneration，让导出过程中排队的旧回调全部过期。
      ++trackSwitchGeneration;
      currentTrackIndex = state.trackIndex;
      pendingResumePlayback = state.hadPendingResume;
      isHandlingTrackEnd = state.wasHandlingTrackEnd;

      if (state.file.existsAsFile())
          loadMidiFile(state.file);

      engine.getMidiPlayer().seekTo(state.position);
      engine.getMidiPlayer().setPlaying(state.wasPlaying);

      if (currentTrackIndex >= 0)
          playlistPanel.setCurrentTrackIndex(currentTrackIndex);
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
      ScanThread(AudioEngine &e)
          : juce::ThreadWithProgressWindow(L"扫描 VST3 插件...", true, true),
            engine(e) {}

      void run() override {
        setProgress(-1.0);
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
  // === 持久化 ===
  bool hasUnsavedChanges() const { return playlist.hasChanges(); }

  juce::String getPlaylistChangeSummary() const {
    return playlist.getChangeSummary();
  }

  // 保存当前播放列表；没有文件路径时先询问保存位置。
  // 用户取消或写入失败时返回 false。
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

    // 关闭窗口时需要立即得到结果，因此这里保留同步桌面文件选择器。
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
    unloadBtn.setEnabled(false);
    contentLabel.setText(L"选择一个 VST3 乐器插件开始演奏",
                         juce::dontSendNotification);

    // 卸载插件后重置播放 UI。
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

    // 用户关闭窗口时只隐藏编辑器窗口。Ivory 等插件在处理器仍存活时，
    // 反复销毁和重建原生编辑器不稳定。
    if (pluginWindow != nullptr) {
      pluginWindow->setVisible(true);
      pluginWindow->toFront(true);
      LOG_DEBUG("Reusing existing plugin window for " + instance->getName());
      return;
    }

    // 重型采样插件在处理器创建后可能继续初始化，延后一轮消息循环再打开编辑器。
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
      } catch ([[maybe_unused]] const std::exception &e) {
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
    pendingResumePlayback = false;
    if (engine.getMidiPlayer().getPlaying()) {
      engine.getMidiPlayer().setPlaying(false);
    } else {
      // 恢复播放前在当前位置追踪 CC、音符、踏板和弯音状态，
      // 避免暂停期间 allSoundOff 清理过的控制状态丢失。
      double pos = engine.getMidiPlayer().getPositionInSamples();
      engine.getMidiPlayer().seekTo(pos);
      engine.getMidiPlayer().setPlaying(true);
    }
  }

  void stopPlayback() {
    pendingResumePlayback = false;
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

    // 顺序播放到列表末尾时停止。
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

      int next = self.playlist.getNextIndex(self.currentTrackIndex);
      if (next != -1) {
        self.currentTrackIndex = next;
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
      - 播放列表为空：新建列表并加载 MIDI。
      - 播放列表已有内容：添加到列表末尾并加载该 MIDI。
      - 已有插件时会延迟开始播放。
      - 没有插件时只尝试加载上次插件并打开插件窗口，等待用户确认音色后手动播放。
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
      加载成功后只打开插件窗口，不自动开始播放；很多乐器插件需要用户先加载音色。
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

          safeThis->applyConfiguredFonts();
          safeThis->playlistPanel.refresh();
          safeThis->playlistPanel.repaint();
          safeThis->repaint();
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

  void applyConfiguredFonts() {
    pageTitle.setFont(fluentLookAndFeel.getDefaultFont(26.0f, true));
    contentLabel.setFont(fluentLookAndFeel.getDefaultFont(16.0f));
    trackLabel.setFont(fluentLookAndFeel.getDefaultFont(14.0f, true));
    timeLabel.setFont(fluentLookAndFeel.getDefaultFont(12.0f));
  }

  void closeSettingsWindows() {
    audioSettingsWindow.deleteAndZero();
    backgroundSettingsWindow.deleteAndZero();
    fontSettingsWindow.deleteAndZero();
  }

  void updatePluginList() {
    // 清空列表前保存当前插件选择，避免扫描刷新后丢失选择状态。
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

    // 将保存的字体设置同步到 LookAndFeel。
    fluentLookAndFeel.setUIFont(settings.getUIFontName());
    fluentLookAndFeel.setPlaylistFont(settings.getPlaylistFontName());

    // 刷新依赖字体的组件。
    playlistPanel.refresh();
  }

  void saveSettings() {
    getAppSettings().setMasterVolume((float)volumeSlider.getValue());
    getAppSettings().setPlayMode(static_cast<int>(playlist.getPlaybackMode()));
    getAppSettings().save();
  }

  // 音频设置面板。
  struct AudioSettingsContent : public juce::Component {
    AudioSettingsContent(AudioEngine &e)
        : engine(e),
          selector(e.getDeviceManager(), 0, 2, 0, 2, true, true, true, false) {
      setSize(520, 500);
      setOpaque(false);

      // AudioDeviceSelectorComponent 使用独立颜色表，需要显式同步深色主题。
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

  // 字体设置面板。
  struct FontSettingsContent : public juce::Component {
    std::function<void()> onSettingsChanged;

    FontSettingsContent(FluentLookAndFeel &laf) : fluentLookAndFeel(laf) {
      setSize(420, 360);
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

      addAndMakeVisible(uiFontLabel);
      uiFontLabel.setText(L"界面字体:", juce::dontSendNotification);
      uiFontLabel.setColour(juce::Label::textColourId, juce::Colours::white);

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

      addAndMakeVisible(playlistFontLabel);
      playlistFontLabel.setText(L"列表字体:", juce::dontSendNotification);
      playlistFontLabel.setColour(juce::Label::textColourId,
                                  juce::Colours::white);

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
      fontSizeLabel.setText(L"列表字号:", juce::dontSendNotification);
      fontSizeLabel.setColour(juce::Label::textColourId, juce::Colours::white);

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
      auto area = getLocalBounds().reduced(20);
      int labelWidth = 80;
      int rowHeight = 40;
      int gap = 10;

      auto row1 = area.removeFromTop(rowHeight);
      uiFontLabel.setBounds(row1.removeFromLeft(labelWidth));
      uiFontCombo.setBounds(row1.reduced(0, 5));
      area.removeFromTop(gap);

      auto row2 = area.removeFromTop(rowHeight);
      playlistFontLabel.setBounds(row2.removeFromLeft(labelWidth));
      playlistFontCombo.setBounds(row2.reduced(0, 5));
      area.removeFromTop(gap);

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
        g.drawText(text, getLocalBounds(), juce::Justification::centredLeft,
                   true);
      } else {
        float x = -scrollOffset;
        g.drawText(text, (int)x, 0, (int)textWidth + 20, getHeight(),
                   juce::Justification::centredLeft, false);
      }
    }

    void mouseDown(const juce::MouseEvent &e) override {
      // 转发点击，让播放列表空白区仍可触发取消选择。
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

      if (scrollOffset >= maxScroll + 50.0f) {
        scrollOffset = -50.0f;
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

        if (diff > 5000) {
          static uint32_t lastWarnTime = 0;
          if (currentTime - lastWarnTime > 5000) {
            LOG_DEBUG("CRITICAL: UI HEARTBEAT LAG: " + juce::String(diff) +
                      " ms");
            lastWarnTime = currentTime;
          }
        }

        if (diff > 30000) {
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

  // 插件扫描进度指示器，仅在可见时运行定时器。
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
        angle = 0.0f;
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

  // === 成员 ===
  AudioEngine &engine;
  FluentLookAndFeel fluentLookAndFeel;
  // 嵌入式 Tooltip，直接在父窗口中绘制，确保完美圆角
  EmbeddedTooltip embeddedTooltip;

  BackgroundComponent background;
  NavigationSidebar navigation;

  juce::Label pageTitle;
  juce::ComboBox pluginSelector;
  TransparentButton loopModeBtn;
  SvgButton exportBtn{R"(<svg width="24" height="24" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg"><path d="M12 15V3M12 15L8 11M12 15L16 11" stroke="#000000" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/><path d="M20 16V18C20 19.1046 19.1046 20 18 20H6C4.89543 20 4 19.1046 4 18V16" stroke="#000000" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"/></svg>)"};
  juce::Slider volumeSlider;
  TransparentButton scanBtn, unloadBtn, openPluginBtn;
  SpinnerComponent scanSpinner;

  juce::Label contentLabel;
  PlaylistManager playlist;
  PlaylistPanel playlistPanel;

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
