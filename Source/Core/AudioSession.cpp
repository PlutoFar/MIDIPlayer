#include "CoreImpl.h"

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
    Operation operation, std::function<juce::Result()> work,
    CommandCompletion completion) {
  jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());
  if (!audioSession.begin(operation))
    return juce::Result::fail(L"当前操作尚未结束");
  engine.resetPluginCancellation();
  commandCompletion = std::move(completion);
  try {
    commandTask = std::thread([this, work = std::move(work)] {
      try {
        commandResult = work();
      } catch (const std::exception &error) {
        commandResult = juce::Result::fail(juce::String(L"后台操作失败: ") + error.what());
      } catch (...) {
        commandResult = juce::Result::fail(L"后台操作发生未知异常。");
      }
      triggerAsyncUpdate();
    });
  } catch (const std::exception &error) {
    commandCompletion = {};
    audioSession.finish();
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
    audioSession.finish();
  }
  if (completion)
    completion(std::move(result));
}

void Core::Impl::timerCallback() {
  std::unique_lock lock(stateMutex);
  auto &player = engine.getMidiPlayer();
  if (engine.takeRealtimeMidiEventRejection())
    midiErrorText = PluginBridge::midiEventTooLargeError();
  if (engine.hasPluginWorkerCrashed()) {
    player.setPlaying(false);
    audioSession.noteWorkerFailure(engine.getPluginWorkerError());
    if (audioSession.isIdle())
      startCommandTask(Operation::recoverWorker, [this] {
        engine.terminateCrashedPluginWorker();
        return juce::Result::ok();
      });
    return;
  }
  if (!audioSession.isIdle())
    return;

  if (audio.needsRecovery()) {
    lock.unlock();
    configureAudio([this] { return audio.recoverDefaultDevice(); });
    return;
  }

  closeEditorWhenIdle();
  if (!audioSession.isIdle())
    return;

  if (!audio.hasDevice()) {
    player.setPlaying(false);
    return;
  }

  if (engine.requiresPrepare()) {
    const auto configuration = engine.deviceConfiguration();
    if (!audioSession.canPrepare(configuration.revision))
      return;
    const auto accepted = startCommandTask(Operation::prepareDevice,
        [this, configuration] {
          const bool prepared = engine.preparePlugin(configuration, [this](double rate) {
            StateLock lock(stateMutex);
            auto &player = engine.getMidiPlayer();
            player.setSampleRate(rate);
            player.seekTo(player.getPositionInSamples(), player.getPlaying());
          });
          return prepared ? juce::Result::ok()
                          : juce::Result::fail(engine.getLastPluginError());
        },
        [this, revision = configuration.revision](juce::Result result) {
          StateLock lock(stateMutex);
          audioSession.preparationCompleted(revision, result);
          if (result.failed())
            engine.getMidiPlayer().setPlaying(false);
        });
    if (accepted.failed()) {
      audioSession.preparationCompleted(configuration.revision, accepted);
      player.setPlaying(false);
    }
    return;
  }
  if (!scrubbing && player.hasFinished())
    handleTrackEnd();
}

} // namespace midi
