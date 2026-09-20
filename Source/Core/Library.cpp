#include "CoreImpl.h"

// Responsibilities: 后台命令受理、插件操作及消息线程结果交付。
// Invariant: 同时最多一个内部命令任务；线程结束通过 `join` 与结果读取建立同步。

namespace midi {

Core::Impl::~Impl() {
  // Ordering: 等待任务前撤销完成回调；`join` 期间不持有核心锁或消息管理器锁。
  // Concurrency: 工作线程只能等待 IPC，禁止依赖主程序消息线程完成操作。
  cancelPendingUpdate();
  engine.cancelPendingPluginOperation();
  if (commandTask.joinable())
    commandTask.join();
  cancelPendingUpdate();
}

bool Core::Impl::startCommandTask(std::function<bool()> operation,
                                 std::function<void(bool)> completion,
                                 bool changesAudio) {
  jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
  {
    StateLock lock(stateMutex);
    if (commandTaskActive.load() || pluginScanActive.load() ||
        exportActiveFlag.load() || audioConfigurationActive.load()) {
      pluginErrorText = L"当前操作尚未结束";
      return false;
    }
    commandTaskActive.store(true);
    commandChangesAudio.store(changesAudio);
    pluginErrorText.clear();
  }
  engine.resetPluginCancellation();
  commandCompletion = std::move(completion);
  commandTaskError.clear();
  try {
    commandTask = std::thread([this, operation = std::move(operation)] {
      try {
        commandTaskSucceeded = operation();
      } catch (const std::exception &error) {
        commandTaskSucceeded = false;
        commandTaskError = juce::String(L"后台操作失败: ") + error.what();
      } catch (...) {
        commandTaskSucceeded = false;
        commandTaskError = L"后台操作发生未知异常。";
      }
      triggerAsyncUpdate();
    });
  } catch (const std::exception &error) {
    StateLock lock(stateMutex);
    commandCompletion = {};
    commandTaskActive.store(false);
    commandChangesAudio.store(false);
    pluginErrorText = juce::String(L"无法启动后台操作: ") + error.what();
    return false;
  }
  return true;
}

void Core::Impl::handleAsyncUpdate() {
  // Ordering: 先等待线程退出，再读取结果、清除任务标记并交付回调。
  commandTask.join();
  auto completion = std::move(commandCompletion);
  const bool succeeded = commandTaskSucceeded;
  {
    StateLock lock(stateMutex);
    if (!succeeded)
      pluginErrorText = commandTaskError.isNotEmpty() ? commandTaskError
                                                    : engine.getLastPluginError();
    commandTaskActive.store(false);
    commandChangesAudio.store(false);
  }
  if (closeEditorWhenIdle) {
    closeEditorWhenIdle = false;
    closeEditor();
  }
  if (completion)
    completion(succeeded);
}

bool Core::Impl::scan(std::function<bool()> shouldCancel) {
  {
    StateLock lock(stateMutex);
    if (pluginScanActive.load() || commandTaskActive.load() ||
        exportActiveFlag.load() || audioConfigurationActive.load()) {
      pluginErrorText = L"当前操作尚未结束";
      return false;
    }
    pluginScanActive.store(true);
    pluginErrorText.clear();
  }
  struct ScanGuard {
    std::atomic<bool> &active;
    ~ScanGuard() { active.store(false); }
  } scanGuard{pluginScanActive};
  juce::KnownPluginList scanned;
  {
    StateLock lock(stateMutex);
    library.copyPluginListTo(scanned);
  }
  if (!library.scanPlugins(scanned, shouldCancel)) {
    StateLock lock(stateMutex);
    pluginErrorText = library.lastError();
    return false;
  }
  {
    StateLock lock(stateMutex);
    if (!library.replacePluginList(scanned)) {
      pluginErrorText = library.lastError();
      return false;
    }
  }
  return true;
}

bool Core::Impl::findById(const PluginId &id, juce::PluginDescription &out) {
  StateLock lock(stateMutex);
  for (const auto &type : library.plugins().getTypes()) {
    if (std::wstring(type.createIdentifierString().toWideCharPointer()) == id) {
      out = type;
      return true;
    }
  }
  return false;
}

bool Core::Impl::loadAsync(const PluginId &id,
                           std::function<void(bool)> completion) {
  juce::PluginDescription description;
  if (!audio.hasDevice() || !findById(id, description)) {
    StateLock lock(stateMutex);
    pluginErrorText = audio.hasDevice() ? L"未找到所选插件，请重新扫描"
                                        : L"没有可用的音频输出设备";
    return false;
  }
  const bool accepted = startCommandTask(
      [this, description] {
        return engine.loadPlugin(description, [this](double rate) {
          StateLock lock(stateMutex);
          engine.getMidiPlayer().setSampleRate(rate);
        });
      },
      std::move(completion));
  if (accepted) {
    StateLock lock(stateMutex);
    ++trackSwitchGeneration;
    isHandlingTrackEnd = false;
    engine.getMidiPlayer().setPlaying(false);
  }
  return accepted;
}

bool Core::Impl::unloadAsync(std::function<void(bool)> completion) {
  const bool accepted = startCommandTask(
      [this] {
        engine.unloadPlugin();
        return !engine.hasPluginWorkerCrashed();
      },
      std::move(completion));
  if (accepted) {
    StateLock lock(stateMutex);
    ++trackSwitchGeneration;
    engine.getMidiPlayer().setPlaying(false);
  }
  return accepted;
}

bool Core::Impl::editorAsync(std::function<void(bool)> completion) {
  if (!engine.hasPluginLoaded())
    return false;
  return startCommandTask([this] { return engine.openPluginEditor(); },
                         std::move(completion), false);
}

void Core::Impl::closeEditor() {
  if (commandTaskActive.load()) {
    closeEditorWhenIdle = true;
    return;
  }
  if (engine.hasPluginLoaded())
    startCommandTask(
        [this] {
          engine.closePluginEditor();
          return !engine.hasPluginWorkerCrashed();
        },
        {}, false);
}

void Core::Impl::terminateCrashedWorker() {
  if (!engine.hasPluginWorkerCrashed())
    return;
  startCommandTask(
      [this] {
        engine.terminateCrashedPluginWorker();
        return true;
      },
      {});
}

} // namespace midi
