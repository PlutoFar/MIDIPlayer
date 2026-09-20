#include "CoreImpl.h"

namespace midi {

Core::Impl::~Impl() {
  // Joining holds no state/message lock. IPC work never waits for UI callbacks.
  life.reset();
  cancelPendingUpdate();
  engine.cancelPendingPluginOperation();
  if (pluginTask.joinable())
    pluginTask.join();
  cancelPendingUpdate();
}

bool Core::Impl::startPluginTask(std::function<bool()> operation,
                                 std::function<void(bool)> completion,
                                 bool changesAudio) {
  jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
  {
    StateLock lock(stateMutex);
    if (pluginTaskActive.load() || pluginScanActive.load() ||
        exportActiveFlag.load() || audioConfigurationActive.load()) {
      pluginErrorText = L"当前操作尚未结束";
      return false;
    }
    pluginTaskActive.store(true);
    pluginChangesAudio.store(changesAudio);
    pluginErrorText.clear();
  }
  engine.resetPluginCancellation();
  pluginCompletion = std::move(completion);
  pluginTask = std::thread([this, operation = std::move(operation)] {
    pluginTaskSucceeded = operation();
    triggerAsyncUpdate();
  });
  return true;
}

void Core::Impl::handleAsyncUpdate() {
  pluginTask.join();
  auto completion = std::move(pluginCompletion);
  const bool succeeded = pluginTaskSucceeded;
  {
    StateLock lock(stateMutex);
    if (!succeeded)
      pluginErrorText = engine.getLastPluginError();
    pluginTaskActive.store(false);
    pluginChangesAudio.store(false);
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
    if (pluginScanActive.load() || pluginTaskActive.load() ||
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
  const bool accepted = startPluginTask(
      [this, description] { return engine.loadPlugin(description); },
      [id, completion = std::move(completion)](bool succeeded) {
        if (succeeded) {
          getAppSettings().setLastPluginId(juce::String(id.c_str()));
          getAppSettings().save();
        }
        if (completion)
          completion(succeeded);
      });
  if (accepted) {
    StateLock lock(stateMutex);
    ++trackSwitchGeneration;
    isHandlingTrackEnd = false;
    engine.getMidiPlayer().setPlaying(false);
  }
  return accepted;
}

bool Core::Impl::unloadAsync(std::function<void(bool)> completion) {
  const bool accepted = startPluginTask(
      [this] {
        engine.unloadPlugin();
        return !engine.hasPluginWorkerCrashed();
      },
      [completion = std::move(completion)](bool succeeded) {
        getAppSettings().setLastPluginId({});
        getAppSettings().save();
        if (completion)
          completion(succeeded);
      });
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
  return startPluginTask([this] { return engine.openPluginEditor(); },
                         std::move(completion), false);
}

void Core::Impl::closeEditor() {
  if (pluginTaskActive.load()) {
    closeEditorWhenIdle = true;
    return;
  }
  if (engine.hasPluginLoaded())
    startPluginTask(
        [this] {
          engine.closePluginEditor();
          return !engine.hasPluginWorkerCrashed();
        },
        {}, false);
}

void Core::Impl::terminateCrashedWorker() {
  if (!engine.hasPluginWorkerCrashed())
    return;
  startPluginTask(
      [this] {
        engine.terminateCrashedPluginWorker();
        return true;
      },
      {});
}

} // namespace midi
