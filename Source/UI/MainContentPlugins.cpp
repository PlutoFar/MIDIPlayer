#include "MainContentComponent.h"

void MainContentComponent::startPluginScan() {
  if (isScanningPlugins)
    return;

  // Contract: 扫描接口不提供逐插件进度；模态进度窗口使用不确定进度和协作取消。
  class ScanThread : public juce::ThreadWithProgressWindow {
  public:
    ScanThread(midi::Core &c, juce::Component *ownerComponent)
        : juce::ThreadWithProgressWindow(L"扫描 VST3 插件...", true, true, -1,
                                         L"取消", ownerComponent),
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

  auto scanner = std::make_unique<ScanThread>(core, this);
  const bool completed = scanner->runThread();
  if (completed && scanner->scanSucceeded) {
    updatePluginList();
  } else if (completed) {
    showOperationError(L"插件扫描失败", core.lastPluginError());
  }

  isScanningPlugins = false;
  pluginSelector.setEnabled(true);
}

void MainContentComponent::confirmUnloadPlugin() {
  if (!core.hasPluginLoaded())
    return;

  juce::AlertWindow::showOkCancelBox(
      juce::AlertWindow::QuestionIcon, L"确认卸载", L"确定要卸载当前插件吗？",
      L"卸载", L"取消", this,
      juce::ModalCallbackFunction::create(
          [safeThis = juce::Component::SafePointer<MainContentComponent>(this)](
              int result) {
            if (result != 0 && safeThis != nullptr)
              safeThis->unloadPlugin();
          }));
}

void MainContentComponent::unloadPlugin() {
  const auto safeThis =
      juce::Component::SafePointer<MainContentComponent>(this);
  if (!core.unloadAsync([safeThis](bool succeeded) {
        if (safeThis == nullptr)
          return;
        safeThis->pluginSelector.setEnabled(true);
        safeThis->pluginSelector.setSelectedId(0, juce::dontSendNotification);
        safeThis->openPluginBtn.setEnabled(false);
        safeThis->unloadBtn.setEnabled(false);
        safeThis->contentLabel.setText(L"选择一个 VST3 乐器插件开始演奏",
                                       juce::dontSendNotification);
        if (!succeeded)
          safeThis->handlePluginWorkerCrash();
      })) {
    showOperationError(L"无法卸载插件", L"当前操作尚未结束");
    return;
  }
  beginPluginSwitch();
  pluginSelector.setEnabled(false);
  playbackPausedByPluginSwitch = false;
  pluginWorkerCrashAlertShown = false;
  pluginWorkerRecoveryHandled = false;
}

int MainContentComponent::beginPluginSwitch() {
  const bool wasPlayingBeforeSwitch = core.state().transport.playing;
  playbackPausedByPluginSwitch =
      shouldShowPluginSwitchPausedNotice(wasPlayingBeforeSwitch, false);
  core.pause();

  return pluginLifecycle.beginSwitch();
}

void MainContentComponent::finishPluginLoadUi(const midi::PluginInfo &plugin) {
  const juce::String pluginName(plugin.name.c_str());
  pluginWorkerCrashAlertShown = false;
  pluginWorkerRecoveryHandled = false;
  openPluginBtn.setEnabled(true);
  unloadBtn.setEnabled(true);
  contentLabel.setText(
      makeLoadedPluginLabel(pluginName, playbackPausedByPluginSwitch),
      juce::dontSendNotification);
  playbackPausedByPluginSwitch = false;
  getAppSettings().setLastPluginId(juce::String(plugin.id.c_str()));
}

void MainContentComponent::showPluginLoadSuccessToast(
    const juce::String &pluginName) {
  const auto title = midi::makePluginLoadSuccessToastTitle(
      std::wstring(pluginName.toWideCharPointer()));
  modeToast.showTopCenter(juce::String(title.c_str()), getLocalBounds(),
                          midi::pluginLoadSuccessToastDurationMs);
}

void MainContentComponent::loadPluginInfo(const midi::PluginInfo &plugin,
                                          bool openEditorAfterLoad) {
  if (pluginLoadInProgress)
    return;
  pluginLoadInProgress = true;
  pluginSelector.setEnabled(false);
  const int generation = beginPluginSwitch();
  pluginLoadingWindow = FluentSettingsStyle::showMessageDialogAsync(
      L"正在加载乐器", L"正在加载插件: " + juce::String(plugin.name.c_str()),
      this, fluentLookAndFeel, {}, false);
  auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
  const bool accepted = core.loadAsync(plugin.id, [safeThis, plugin,
                                                   openEditorAfterLoad,
                                                   generation](bool succeeded) {
    if (safeThis == nullptr)
      return;
    safeThis->pluginLoadingWindow.deleteAndZero();
    safeThis->pluginLoadInProgress = false;
    safeThis->pluginSelector.setEnabled(!safeThis->isScanningPlugins);
    if (!safeThis->pluginLifecycle.isGenerationCurrent(generation))
      return;
    if (succeeded) {
      safeThis->finishPluginLoadUi(plugin);
      safeThis->showPluginLoadSuccessToast(juce::String(plugin.name.c_str()));
      if (openEditorAfterLoad)
        safeThis->openPluginWindow();
    } else {
      safeThis->playbackPausedByPluginSwitch = false;
      if (!safeThis->core.hasPluginLoaded()) {
        safeThis->pluginSelector.setSelectedId(0, juce::dontSendNotification);
        safeThis->contentLabel.setText(L"选择一个 VST3 乐器插件开始演奏",
                                       juce::dontSendNotification);
      }
      if (safeThis->core.workerCrashed())
        safeThis->handlePluginWorkerCrash();
      else
        safeThis->showOperationError(L"插件加载失败",
                                     safeThis->core.lastPluginError());
    }
  });
  if (!accepted) {
    pluginLoadingWindow.deleteAndZero();
    pluginLoadInProgress = false;
    pluginSelector.setEnabled(!isScanningPlugins);
    showOperationError(L"无法加载插件", core.lastPluginError());
  }
}

void MainContentComponent::loadSelectedPlugin() {
  if (isScanningPlugins)
    return;

  int idx = pluginSelector.getSelectedItemIndex();
  const auto plugins = core.plugins();
  if (idx >= 0 && idx < static_cast<int>(plugins.size())) {
    const auto &plugin = plugins[static_cast<size_t>(idx)];
    loadPluginInfo(plugin, shouldAutoOpenPluginEditorAfterLoad(
                               juce::String(plugin.name.c_str())));
  }
}

void MainContentComponent::openPluginWindow() {
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

    if (!safeThis->core.editorAsync([safeThis](bool succeeded) {
          if (safeThis == nullptr || succeeded)
            return;
          if (safeThis->core.workerCrashed())
            safeThis->handlePluginWorkerCrash();
          else
            safeThis->showOperationError(L"插件窗口打开失败",
                                         safeThis->core.lastPluginError());
        }))
      safeThis->showOperationError(L"插件窗口打开失败", L"当前操作尚未结束");
  });
}

void MainContentComponent::closePluginWindow() {
  core.closeEditor();
  pluginLifecycle.closeWindow();
}

void MainContentComponent::handlePluginWorkerCrash() {
  const auto error = juce::String(core.pluginError().c_str());
  core.pause();
  if (!pluginWorkerRecoveryHandled) {
    pluginWorkerRecoveryHandled = true;
    pluginLifecycle.beginSwitch();
    pluginLoadInProgress = false;
    pluginSelector.setEnabled(!isScanningPlugins);
    pluginSelector.setSelectedId(0, juce::dontSendNotification);
    openPluginBtn.setEnabled(false);
    unloadBtn.setEnabled(false);
    contentLabel.setText(L"插件进程已停止，请重新选择插件",
                         juce::dontSendNotification);
    core.terminateCrashedWorker();
  }

  if (pluginWorkerCrashAlertShown)
    return;

  pluginWorkerCrashAlertShown = true;
  showPluginMessage(L"插件进程已停止",
                    error.isNotEmpty() ? error : L"插件工作进程无响应。");
}

void MainContentComponent::tryLoadLastPluginWithDialog() {
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
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                           L"无法播放",
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
  loadPluginInfo(plugin, shouldAutoOpenPluginEditorAfterLoad(
                             juce::String(plugin.name.c_str())));
}

void MainContentComponent::updatePluginList() {
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

void MainContentComponent::tryLoadLastPlugin() {
  auto lastPluginId = getAppSettings().getLastPluginId();
  if (lastPluginId.isEmpty())
    return;

  const auto plugins = core.plugins();
  for (int i = 0; i < static_cast<int>(plugins.size()); ++i) {
    const auto &plugin = plugins[static_cast<size_t>(i)];
    if (plugin.id == std::wstring(lastPluginId.toWideCharPointer())) {
      pluginSelector.setSelectedId(i + 1, juce::dontSendNotification);
      loadPluginInfo(plugin, false);
      return;
    }
  }
}
