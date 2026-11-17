#include "CoreImpl.h"
#include "../PluginBridge/PluginScanProcess.h"

// 插件目录持久化和插件命令由 Core 统一受理。

namespace midi {

juce::Result Core::Impl::scan(std::function<bool()> shouldCancel) {
  {
    StateLock lock(stateMutex);
    if (!audioSession.begin(Operation::scanPlugins))
      return juce::Result::fail(L"当前操作尚未结束");
  }
  SessionOperation scanning{*this};
  juce::KnownPluginList scanned;
  {
    StateLock lock(stateMutex);
    library.copyPluginListTo(scanned);
  }
  const auto scannedResult =
      PluginBridge::scanPluginListInChildProcess(scanned, scanned, shouldCancel);
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

juce::Result Core::Impl::loadAsync(const PluginId &id, CommandCompletion completion) {
  StateLock lock(stateMutex);
  if (!audio.hasDevice())
    return juce::Result::fail(L"没有可用的音频输出设备");
  const auto description = library.findInstrument(juce::String(id.c_str()));
  if (!description)
    return juce::Result::fail(L"未找到所选插件，请重新扫描");
  const auto accepted = startCommandTask(Operation::changePlugin,
      [this, description = *description] {
        const bool loaded = engine.loadPlugin(description, [this](double rate) {
          StateLock lock(stateMutex);
          engine.getMidiPlayer().setSampleRate(rate);
        });
        return loaded ? juce::Result::ok()
                      : juce::Result::fail(engine.getLastPluginError());
      },
      [this, completion = std::move(completion)](juce::Result result) {
        if (result.wasOk()) {
          StateLock lock(stateMutex);
          audioSession.preparationCompleted(engine.configurationRevision(), result);
        }
        if (completion)
          completion(std::move(result));
      });
  if (accepted.wasOk()) {
    audioSession.clearWorkerFailure();
    ++trackSwitchGeneration;
    engine.getMidiPlayer().setPlaying(false);
  }
  return accepted;
}

juce::Result Core::Impl::unloadAsync(CommandCompletion completion) {
  StateLock lock(stateMutex);
  const auto accepted = startCommandTask(Operation::changePlugin,
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
      Operation::pluginEditor,
      [this] {
        return engine.openPluginEditor()
                   ? juce::Result::ok()
                   : juce::Result::fail(engine.getLastPluginError());
      },
      std::move(completion));
}

void Core::Impl::closeEditor() {
  StateLock lock(stateMutex);
  audioSession.requestEditorClose();
  if (audioSession.isIdle())
    closeEditorWhenIdle();
}

void Core::Impl::closeEditorWhenIdle() {
  if (!audioSession.takeEditorCloseRequest())
    return;
  if (engine.hasPluginLoaded())
    startCommandTask(Operation::pluginEditor,
        [this] {
          engine.closePluginEditor();
          return engine.hasPluginWorkerCrashed()
                     ? juce::Result::fail(engine.getPluginWorkerError())
                     : juce::Result::ok();
        });
}

bool Core::Impl::workerCrashed() const {
  return audioSession.workerFailure().has_value() || engine.hasPluginWorkerCrashed();
}

} // namespace midi
