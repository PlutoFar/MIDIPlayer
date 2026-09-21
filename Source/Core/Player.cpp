#include "CoreImpl.h"
#include "../Midi/MidiFileLoader.h"

// Responsibilities: MIDI 解析、播放位置和切曲编排；界面通过 `Core` 快照更新显示。
// Concurrency: 可变曲目状态和序列生产由 `stateMutex` 串行化；实时消费只读取已发布快照。
// Ordering: 显式播放控制递增代次，延迟回调必须同时匹配代次和当前可播放条件。

namespace midi {

bool Core::Impl::canStartPlayback() {
  StateLock lock(stateMutex);
  return engine.getMidiPlayer().hasSequence() && engine.hasPluginLoaded() &&
         !engine.hasPluginWorkerCrashed() && !commandChangesAudio.load() &&
         !exportActiveFlag.load();
}

bool Core::Impl::loadMidi(const juce::File &file) {
  std::unique_ptr<juce::MidiMessageSequence> seq;
  const auto result = readMidiSequence(file, seq);
  if (result.failed()) {
    StateLock lock(stateMutex);
    midiErrorText = result.getErrorMessage();
    return false;
  }
  StateLock lock(stateMutex);
  const double sr = sampleRate();
  engine.getMidiPlayer().setSequence(std::move(seq), sr);
  currentMidiFile = file;
  currentMidiName = file.getFileNameWithoutExtension();
  midiErrorText.clear();
  return true;
}

bool Core::Impl::beginMidiLoad(const juce::File &file, bool addToList, bool autoPlay,
                              std::function<void(bool)> completion,
                              std::function<void()> onPluginMissing) {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load() || commandTaskActive.load() || pluginScanActive.load()) {
    midiErrorText = L"当前操作尚未结束，无法加载 MIDI 文件。";
    return false;
  }
  const int generation = ++trackSwitchGeneration;
  midiErrorText.clear();
  const bool accepted = startCommandTask(
      [this, file, addToList] {
        if (!loadMidi(file)) {
          StateLock resultLock(stateMutex);
          commandTaskError = midiErrorText;
          return false;
        }
        StateLock resultLock(stateMutex);
        if (addToList)
          playlist.addFile(file);
        currentTrackIndex = playlist.findTrackIndex(file);
        return true;
      },
      [this, file, generation, autoPlay, completion = std::move(completion),
       onPluginMissing = std::move(onPluginMissing)](bool succeeded) {
        if (succeeded) {
          getAppSettings().setLastMidiDirectory(file.getParentDirectory().getFullPathName());
          StateLock resultLock(stateMutex);
          if (autoPlay && generation == trackSwitchGeneration && canStartPlayback()) {
            // Ordering: 新曲目从 0 开始，禁止使用旧渲染块尚未完成交接的位置缓存。
            engine.getMidiPlayer().seekTo(0.0, true);
            engine.getMidiPlayer().setPlaying(true);
          }
        } else {
          StateLock resultLock(stateMutex);
          midiErrorText = pluginErrorText;
        }
        if (completion)
          completion(succeeded);
        if (succeeded && !engine.hasPluginLoaded() && onPluginMissing)
          onPluginMissing();
      });
  if (accepted) {
    isHandlingTrackEnd = false;
    engine.getMidiPlayer().setPlaying(false);
  } else {
    midiErrorText = pluginErrorText;
  }
  return accepted;
}

void Core::Impl::play() {
  StateLock lock(stateMutex);
  if (!canStartPlayback() || engine.getMidiPlayer().getPlaying())
    return;
  ++trackSwitchGeneration;
  isHandlingTrackEnd = false;
  auto &mp = engine.getMidiPlayer();
  mp.seekTo(mp.getPositionInSamples(), true);
  mp.setPlaying(true);
}

void Core::Impl::pause() {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load())
    return;
  ++trackSwitchGeneration;
  isHandlingTrackEnd = false;
  engine.getMidiPlayer().setPlaying(false);
}

void Core::Impl::togglePlay() {
  StateLock lock(stateMutex);
  if (!canStartPlayback())
    return;
  ++trackSwitchGeneration;
  isHandlingTrackEnd = false;
  auto &mp = engine.getMidiPlayer();
  if (mp.getPlaying()) {
    mp.setPlaying(false);
  } else {
    // 恢复当前位置的控制器、踏板和弯音；已播放音符不重新触发。
    const double pos = mp.getPositionInSamples();
    mp.seekTo(pos, true);
    mp.setPlaying(true);
  }
}

void Core::Impl::stop() {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load())
    return;
  ++trackSwitchGeneration;
  isHandlingTrackEnd = false;
  engine.getMidiPlayer().setPlaying(false);
  engine.getMidiPlayer().seekTo(0);
}

void Core::Impl::seek(double ratio) {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load() || commandChangesAudio.load())
    return;
  auto &mp = engine.getMidiPlayer();
  const double dur = mp.getDurationInSamples();
  if (dur > 0)
    mp.seekTo(juce::jlimit(0.0, 1.0, ratio) * dur);
}

void Core::Impl::volume(float value) {
  StateLock lock(stateMutex);
  if (!exportActiveFlag.load())
    engine.setMasterVolume(value);
}

void Core::Impl::next() {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load() || commandTaskActive.load() || pluginScanActive.load() ||
      audioConfigurationActive.load())
    return;
  if (playlist.isEmpty() || !engine.hasPluginLoaded())
    return;

  isHandlingTrackEnd = false;
  ++trackSwitchGeneration;
  engine.getMidiPlayer().setPlaying(false);

  const int nextIndex = playlist.getNextIndex(currentTrackIndex);
  if (nextIndex == -1) {
    stop();
    return;
  }

  if (const auto *track = playlist.getTrack(nextIndex)) {
    beginMidiLoad(track->file, false, true);
  }
}

void Core::Impl::prev() {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load() || commandTaskActive.load() || pluginScanActive.load() ||
      audioConfigurationActive.load())
    return;
  if (playlist.isEmpty() || !engine.hasPluginLoaded())
    return;

  isHandlingTrackEnd = false;
  ++trackSwitchGeneration;
  engine.getMidiPlayer().setPlaying(false);

  const int previousIndex = playlist.getPreviousIndex(currentTrackIndex);
  if (previousIndex == -1) {
    stop();
    return;
  }
  if (const auto *track = playlist.getTrack(previousIndex)) {
    beginMidiLoad(track->file, false, true);
  }
}

void Core::Impl::handleTrackEnd() {
  StateLock lock(stateMutex);
  if (isHandlingTrackEnd)
    return;

  isHandlingTrackEnd = true;
  ++trackSwitchGeneration;
  engine.getMidiPlayer().setPlaying(false);
  engine.getMidiPlayer().seekTo(0);
  const int nextIndex = playlist.getNextIndex(currentTrackIndex);
  if (const auto *track = playlist.getTrack(nextIndex))
    beginMidiLoad(track->file, false, true);
  isHandlingTrackEnd = false;
}

void Core::Impl::tick(bool uiSuppressTrackAdvance) {
  StateLock lock(stateMutex);
  auto &player = engine.getMidiPlayer();
  const bool isExporting = exportActiveFlag.load();

  // 曲目自然结束后推进到下一曲（用户拖动进度条期间抑制）。
  if (!isExporting && !commandTaskActive.load() && !pluginScanActive.load() &&
      !audioConfigurationActive.load() && !uiSuppressTrackAdvance &&
      player.hasFinished())
    handleTrackEnd();

  // worker 崩溃后停止播放；崩溃提示由 UI 层依据 state() 呈现。
  if (engine.hasPluginWorkerCrashed())
    player.setPlaying(false);
}

} // namespace midi
