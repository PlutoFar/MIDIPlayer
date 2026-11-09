#include "CoreImpl.h"

// Responsibilities: 插件操作和后台命令结果交付。
// Invariant: 同时最多一个内部命令任务；线程结束通过 `join` 与结果读取建立同步。

namespace midi {

Core::Impl::Impl(UserSettings &userSettings)
    : settings(userSettings),
      audio(engine, settings.directory().getChildFile("AudioDevice.xml")),
      library(settings.directory().getChildFile("Plugins.xml")) {
  const int mode = settings.getPlayMode();
  if (mode >= 1 && mode <= 4)
    playlist.setPlaybackMode(static_cast<PlaybackMode>(mode));
  startTimerHz(30);
}

Core::Impl::~Impl() {
  stopTimer();
  // Ordering: 等待任务前撤销完成回调；`join` 期间不持有核心锁或消息管理器锁。
  // Concurrency: 工作线程只能等待 IPC，禁止依赖主程序消息线程完成操作。
  cancelPendingUpdate();
  engine.cancelPendingPluginOperation();
  if (commandTask.joinable())
    commandTask.join();
  cancelPendingUpdate();
}

juce::Result Core::Impl::startCommandTask(
    std::function<juce::Result()> operation, CommandCompletion completion,
    bool changesAudio) {
  jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
  if (commandTaskActive.load() || pluginScanActive.load() ||
      exportActiveFlag.load() || audioConfigurationActive.load())
    return juce::Result::fail(L"当前操作尚未结束");
  commandTaskActive.store(true);
  commandChangesAudio.store(changesAudio);
  engine.resetPluginCancellation();
  commandCompletion = std::move(completion);
  try {
    commandTask = std::thread([this, operation = std::move(operation)] {
      try {
        commandResult = operation();
      } catch (const std::exception &error) {
        commandResult = juce::Result::fail(juce::String(L"后台操作失败: ") + error.what());
      } catch (...) {
        commandResult = juce::Result::fail(L"后台操作发生未知异常。");
      }
      triggerAsyncUpdate();
    });
  } catch (const std::exception &error) {
    commandCompletion = {};
    commandTaskActive.store(false);
    commandChangesAudio.store(false);
    return juce::Result::fail(juce::String(L"无法启动后台操作: ") + error.what());
  }
  return juce::Result::ok();
}

void Core::Impl::handleAsyncUpdate() {
  // Ordering: 先等待线程退出，再读取结果、清除任务标记并交付回调。
  commandTask.join();
  auto completion = std::move(commandCompletion);
  auto result = std::move(commandResult);
  {
    StateLock lock(stateMutex);
    commandTaskActive.store(false);
    commandChangesAudio.store(false);
  }
  if (closeEditorWhenIdle) {
    closeEditorWhenIdle = false;
    closeEditor();
  }
  if (completion)
    completion(std::move(result));
}

juce::Result Core::Impl::scan(std::function<bool()> shouldCancel) {
  {
    StateLock lock(stateMutex);
    if (pluginScanActive.load() || commandTaskActive.load() ||
        exportActiveFlag.load() || audioConfigurationActive.load())
      return juce::Result::fail(L"当前操作尚未结束");
    pluginScanActive.store(true);
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
  const auto scannedResult = PluginLibrary::scanPlugins(scanned, shouldCancel);
  if (scannedResult.failed())
    return scannedResult;
  const auto xml = scanned.createXml();
  if (xml == nullptr)
    return juce::Result::fail(L"无法生成插件目录缓存");
  if (const auto saved = library.savePluginList(*xml); saved.failed())
    return saved;
  {
    StateLock lock(stateMutex);
    library.publishPluginList(*xml);
  }
  return juce::Result::ok();
}

bool Core::Impl::findById(const PluginId &id, juce::PluginDescription &out) {
  for (const auto &type : library.plugins().getTypes()) {
    if (std::wstring(type.createIdentifierString().toWideCharPointer()) == id) {
      out = type;
      return true;
    }
  }
  return false;
}

juce::Result Core::Impl::loadAsync(const PluginId &id, CommandCompletion completion) {
  StateLock lock(stateMutex);
  juce::PluginDescription description;
  if (!audio.hasDevice())
    return juce::Result::fail(L"没有可用的音频输出设备");
  if (!findById(id, description))
    return juce::Result::fail(L"未找到所选插件，请重新扫描");
  const auto accepted = startCommandTask(
      [this, description] {
        const bool loaded = engine.loadPlugin(description, [this](double rate) {
          StateLock lock(stateMutex);
          engine.getMidiPlayer().setSampleRate(rate);
        });
        return loaded ? juce::Result::ok()
                      : juce::Result::fail(engine.getLastPluginError());
      },
      std::move(completion));
  if (accepted.wasOk()) {
    ++trackSwitchGeneration;
    engine.getMidiPlayer().setPlaying(false);
  }
  return accepted;
}

juce::Result Core::Impl::unloadAsync(CommandCompletion completion) {
  StateLock lock(stateMutex);
  const auto accepted = startCommandTask(
      [this] {
        engine.unloadPlugin();
        return engine.hasPluginWorkerCrashed()
                   ? juce::Result::fail(engine.getPluginWorkerError())
                   : juce::Result::ok();
      },
      std::move(completion));
  if (accepted.wasOk()) {
    ++trackSwitchGeneration;
    engine.getMidiPlayer().setPlaying(false);
  }
  return accepted;
}

juce::Result Core::Impl::editorAsync(CommandCompletion completion) {
  StateLock lock(stateMutex);
  if (!engine.hasPluginLoaded())
    return juce::Result::fail(L"请先加载乐器插件。");
  return startCommandTask(
      [this] {
        return engine.openPluginEditor()
                   ? juce::Result::ok()
                   : juce::Result::fail(engine.getLastPluginError());
      },
      std::move(completion), false);
}

void Core::Impl::closeEditor() {
  StateLock lock(stateMutex);
  if (commandTaskActive.load()) {
    closeEditorWhenIdle = true;
    return;
  }
  if (engine.hasPluginLoaded())
    startCommandTask(
        [this] {
          engine.closePluginEditor();
          return engine.hasPluginWorkerCrashed()
                     ? juce::Result::fail(engine.getPluginWorkerError())
                     : juce::Result::ok();
        },
        {}, false);
}

void Core::Impl::terminateCrashedWorker() {
  StateLock lock(stateMutex);
  if (!engine.hasPluginWorkerCrashed())
    return;
  startCommandTask(
      [this] {
        engine.terminateCrashedPluginWorker();
        return juce::Result::ok();
      },
      {});
}

} // namespace midi
