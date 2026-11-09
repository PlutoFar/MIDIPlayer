#include "MainContentComponent.h"
#include "SettingsPersistence.h"

namespace {

constexpr int pluginEditorOpenDelayMs = 300;

bool shouldAutoOpenPluginEditor(const juce::String &name) {
  return !name.containsIgnoreCase("Ivory");
}

} // namespace

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
      result = core.scan([this] { return threadShouldExit(); });
    }

    midi::Core &core;
    juce::Result result = juce::Result::fail(L"无法启动扫描任务。");
  };

  isScanningPlugins = true;
  pluginSelector.setEnabled(false);

  auto scanner = std::make_unique<ScanThread>(core, this);
  const bool completed = scanner->runThread();
  if (scanner->result.wasOk()) {
    updatePluginList();
  } else if (completed) {
    showOperationError(L"插件扫描失败", std::wstring(scanner->result.getErrorMessage().toWideCharPointer()));
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
  const auto accepted = core.unloadAsync([safeThis](juce::Result result) {
        if (safeThis == nullptr)
          return;
        safeThis->pluginSelector.setEnabled(true);
        safeThis->pluginSelector.setSelectedId(0, juce::dontSendNotification);
        safeThis->openPluginBtn.setEnabled(false);
        safeThis->unloadBtn.setEnabled(false);
        safeThis->contentLabel.setText(L"选择一个 VST3 乐器插件开始演奏",
                                       juce::dontSendNotification);
        if (result.failed()) {
          if (safeThis->core.workerCrashed())
            safeThis->handlePluginWorkerCrash();
          else
            safeThis->showOperationError(L"无法卸载插件", result.getErrorMessage().toWideCharPointer());
        }
        getAppSettings().setLastPluginId({});
        saveAppSettingsWithFeedback();
      });
  if (accepted.failed()) {
    showOperationError(L"无法卸载插件", accepted.getErrorMessage().toWideCharPointer());
    return;
  }
  ++pluginGeneration;
  pluginSelector.setEnabled(false);
  playbackPausedByPluginSwitch = false;
  pluginWorkerCrashAlertShown = false;
  pluginWorkerRecoveryHandled = false;
}

void MainContentComponent::finishPluginLoadUi(const midi::PluginInfo &plugin) {
  const juce::String pluginName(plugin.name.c_str());
  pluginWorkerCrashAlertShown = false;
  pluginWorkerRecoveryHandled = false;
  openPluginBtn.setEnabled(true);
  unloadBtn.setEnabled(true);
  auto label = L"已加载: " + pluginName;
  if (playbackPausedByPluginSwitch)
    label += L"（播放已暂停，请手动恢复）";
  contentLabel.setText(label, juce::dontSendNotification);
  playbackPausedByPluginSwitch = false;
  getAppSettings().setLastPluginId(juce::String(plugin.id.c_str()));
  saveAppSettingsWithFeedback();
}

void MainContentComponent::showPluginLoadSuccessToast(
    const juce::String &pluginName) {
  const auto title = pluginName.isEmpty() ? juce::String(L"插件加载成功")
                                         : L"插件加载成功: " + pluginName;
  modeToast.showTopCenter(title, getLocalBounds(), 3000);
}

void MainContentComponent::loadPluginInfo(const midi::PluginInfo &plugin,
                                          bool openEditorAfterLoad) {
  if (pluginLoadInProgress)
    return;
  pluginLoadInProgress = true;
  pluginSelector.setEnabled(false);
  const int generation = pluginGeneration + 1;
  const bool wasPlaying = core.state().transport.playing;
  pluginLoadingWindow = FluentSettingsStyle::showMessageDialogAsync(
      L"正在加载乐器", L"正在加载插件: " + juce::String(plugin.name.c_str()),
      this, fluentLookAndFeel, {}, false);
  auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
  const auto accepted = core.loadAsync(plugin.id, [safeThis, plugin,
                                                   openEditorAfterLoad,
                                                   generation](juce::Result result) {
    if (safeThis == nullptr)
      return;
    safeThis->pluginLoadingWindow.deleteAndZero();
    safeThis->pluginLoadInProgress = false;
    safeThis->pluginSelector.setEnabled(!safeThis->isScanningPlugins);
    if (safeThis->pluginGeneration != generation)
      return;
    if (result.wasOk()) {
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
                                     result.getErrorMessage().toWideCharPointer());
    }
  });
  if (accepted.failed()) {
    pluginLoadingWindow.deleteAndZero();
    pluginLoadInProgress = false;
    pluginSelector.setEnabled(!isScanningPlugins);
    showOperationError(L"无法加载插件", accepted.getErrorMessage().toWideCharPointer());
  } else {
    pluginGeneration = generation;
    playbackPausedByPluginSwitch = wasPlaying;
  }
}

void MainContentComponent::loadSelectedPlugin() {
  if (isScanningPlugins)
    return;

  int idx = pluginSelector.getSelectedItemIndex();
  const auto plugins = core.plugins();
  if (idx >= 0 && idx < static_cast<int>(plugins.size())) {
    const auto &plugin = plugins[static_cast<size_t>(idx)];
    loadPluginInfo(plugin, shouldAutoOpenPluginEditor(
                               juce::String(plugin.name.c_str())));
  }
}

void MainContentComponent::openPluginWindow() {
  if (!core.hasPluginLoaded())
    return;

  const int requestGeneration = pluginGeneration;
  auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
  const auto pluginName = juce::String(core.loadedPluginName().c_str());
  LOG_DEBUG("Scheduling plugin editor for " + pluginName + " in " +
            juce::String(pluginEditorOpenDelayMs) + " ms");

  juce::Timer::callAfterDelay(pluginEditorOpenDelayMs, [safeThis, requestGeneration]() {
    if (safeThis == nullptr)
      return;

    if (safeThis->pluginGeneration != requestGeneration ||
        !safeThis->core.hasPluginLoaded())
      return;

    const auto accepted = safeThis->core.editorAsync([safeThis](juce::Result result) {
          if (safeThis == nullptr || result.wasOk())
            return;
          if (safeThis->core.workerCrashed())
            safeThis->handlePluginWorkerCrash();
          else
            safeThis->showOperationError(L"插件窗口打开失败",
                                         result.getErrorMessage().toWideCharPointer());
        });
    if (accepted.failed())
      safeThis->showOperationError(L"插件窗口打开失败", accepted.getErrorMessage().toWideCharPointer());
  });
}

void MainContentComponent::closePluginWindow() {
  core.closeEditor();
}

void MainContentComponent::handlePluginWorkerCrash() {
  const auto error = juce::String(core.pluginError().c_str());
  core.pause();
  if (!pluginWorkerRecoveryHandled) {
    pluginWorkerRecoveryHandled = true;
    ++pluginGeneration;
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
  loadPluginInfo(plugin, shouldAutoOpenPluginEditor(
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
