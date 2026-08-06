#include "CoreImpl.h"

// Core/Library —— 插件扫描、加载、卸载、editor、worker 崩溃清理。
// 从 MainContentComponent 抽出的与界面无关的插件库业务。

namespace midi {

bool Core::Impl::scan(std::function<bool()> shouldCancel) {
  std::lock_guard<std::mutex> scanLock(pluginScanMutex);
  if (pluginLoadActive.load() || exportActiveFlag.load())
    return false;

  pluginScanActive.store(true);
  struct ScanGuard {
    std::atomic<bool> &active;
    ~ScanGuard() { active.store(false); }
  } scanGuard{pluginScanActive};

  juce::KnownPluginList scanned;
  {
    StateLock lock(stateMutex);
    engine.copyPluginListTo(scanned);
  }

  if (!engine.scanPlugins(scanned, shouldCancel))
    return false;

  {
    StateLock lock(stateMutex);
    engine.replacePluginList(scanned);
  }
  notify();
  return true;
}

bool Core::Impl::findById(const PluginId &id, juce::PluginDescription &out) {
  StateLock lock(stateMutex);
  for (const auto &type : engine.getPluginList().getTypes()) {
    if (std::wstring(type.createIdentifierString().toWideCharPointer()) == id) {
      out = type;
      return true;
    }
  }
  return false;
}

bool Core::Impl::load(const PluginId &id) {
  if (exportActiveFlag.load() || pluginScanActive.load() ||
      pluginLoadActive.exchange(true))
    return false;

  struct LoadGuard {
    std::atomic<bool> &active;
    ~LoadGuard() { active.store(false); }
  } loadGuard{pluginLoadActive};

  {
    StateLock lock(stateMutex);
    ++trackSwitchGeneration;
    pendingResumePlayback = false;
    isHandlingTrackEnd = false;
    engine.getMidiPlayer().setPlaying(false);
  }

  juce::PluginDescription desc;
  if (findById(id, desc)) {
    const bool ok = engine.loadPlugin(desc);
    if (ok) {
      getAppSettings().setLastPluginId(desc.createIdentifierString());
      getAppSettings().save();
    }
    notify();
    return ok;
  }
  return false;
}

void Core::Impl::unload() {
  if (exportActiveFlag.load() || pluginLoadActive.load())
    return;
  {
    StateLock lock(stateMutex);
    ++trackSwitchGeneration;
    pendingResumePlayback = false;
    isHandlingTrackEnd = false;
    engine.getMidiPlayer().setPlaying(false);
  }
  engine.unloadPlugin();
  getAppSettings().setLastPluginId({});
  getAppSettings().save();
  notify();
}

bool Core::Impl::editor() {
  if (exportActiveFlag.load() || pluginLoadActive.load())
    return false;
  return engine.openPluginEditor();
}

void Core::Impl::terminateCrashedWorker() {
  if (engine.hasPluginWorkerCrashed())
    engine.terminateCrashedPluginWorker();
}

} // namespace midi
