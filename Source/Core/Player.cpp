#include "CoreImpl.h"
#include "../Midi/MidiFileLoader.h"

// Responsibilities: MIDI 解析、播放位置和切曲编排；界面通过 `Core` 快照更新显示。
// Concurrency: 可变曲目状态和序列生产由 `stateMutex` 串行化；实时消费只读取已发布快照。
// Ordering: 显式播放控制递增代次，延迟回调必须同时匹配代次和当前可播放条件。

namespace midi {

bool Core::Impl::canStartPlayback() {
  return engine.getMidiPlayer().hasSequence() && engine.hasPluginLoaded() &&
         !engine.hasPluginWorkerCrashed() && !commandChangesAudio.load() &&
         !exportActiveFlag.load();
}

bool Core::Impl::beginMidiLoad(const juce::File &file, TrackId trackId,
                              std::function<void(bool)> completion,
                              std::function<void()> onPluginMissing) {
  const int generation = trackSwitchGeneration + 1;
  midiErrorText.clear();
  const auto accepted = startCommandTask(
      [this, file, trackId] {
        std::unique_ptr<juce::MidiMessageSequence> sequence;
        const auto parsed = readMidiSequence(file, sequence);
        if (parsed.failed())
          return parsed;

        StateLock resultLock(stateMutex);
        engine.getMidiPlayer().setSequence(std::move(sequence), engine.liveSampleRate());
        currentMidiFile = file;
        currentMidiName = file.getFileNameWithoutExtension();
        if (trackId == 0) {
          playlist.addFile(file);
          currentTrackIndex = playlist.findTrackIndex(file);
        } else {
          currentTrackIndex = playlist.findTrackIndex(trackId);
        }
        return juce::Result::ok();
      },
      [this, file, generation, completion = std::move(completion),
       onPluginMissing = std::move(onPluginMissing)](juce::Result result) {
        if (result.wasOk()) {
          settings.setLastMidiDirectory(file.getParentDirectory().getFullPathName());
          StateLock resultLock(stateMutex);
          if (generation == trackSwitchGeneration && canStartPlayback()) {
            // Ordering: 新曲目从 0 开始，禁止使用旧渲染块尚未完成交接的位置缓存。
            engine.getMidiPlayer().seekTo(0.0, true);
            engine.getMidiPlayer().setPlaying(true);
          }
        } else {
          StateLock resultLock(stateMutex);
          midiErrorText = result.getErrorMessage();
        }
        const bool pluginMissing = result.wasOk() && !engine.hasPluginLoaded();
        if (completion)
          completion(result.wasOk());
        if (pluginMissing && onPluginMissing)
          onPluginMissing();
      });
  if (accepted.wasOk()) {
    trackSwitchGeneration = generation;
    engine.getMidiPlayer().setPlaying(false);
  } else {
    midiErrorText = accepted.getErrorMessage();
  }
  return accepted.wasOk();
}

void Core::Impl::play() {
  StateLock lock(stateMutex);
  if (!canStartPlayback() || engine.getMidiPlayer().getPlaying())
    return;
  ++trackSwitchGeneration;
  auto &mp = engine.getMidiPlayer();
  mp.seekTo(mp.getPositionInSamples(), true);
  mp.setPlaying(true);
}

void Core::Impl::pause() {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load())
    return;
  ++trackSwitchGeneration;
  engine.getMidiPlayer().setPlaying(false);
}

void Core::Impl::togglePlay() {
  StateLock lock(stateMutex);
  if (!canStartPlayback())
    return;
  ++trackSwitchGeneration;
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
  resetPlayback();
}

void Core::Impl::resetPlayback() {
  ++trackSwitchGeneration;
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

  const int nextIndex = playlist.getNextIndex(currentTrackIndex);
  if (nextIndex == -1) {
    resetPlayback();
    return;
  }

  if (const auto *track = playlist.getTrack(nextIndex)) {
    beginMidiLoad(track->file, track->id);
  }
}

void Core::Impl::prev() {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load() || commandTaskActive.load() || pluginScanActive.load() ||
      audioConfigurationActive.load())
    return;
  if (playlist.isEmpty() || !engine.hasPluginLoaded())
    return;

  const int previousIndex = playlist.getPreviousIndex(currentTrackIndex);
  if (previousIndex == -1) {
    resetPlayback();
    return;
  }
  if (const auto *track = playlist.getTrack(previousIndex)) {
    beginMidiLoad(track->file, track->id);
  }
}

void Core::Impl::handleTrackEnd() {
  resetPlayback();
  const int nextIndex = playlist.getNextIndex(currentTrackIndex);
  if (const auto *track = playlist.getTrack(nextIndex))
    beginMidiLoad(track->file, track->id);
}

void Core::Impl::timerCallback() {
  StateLock lock(stateMutex);
  auto &player = engine.getMidiPlayer();
  if (engine.hasPluginWorkerCrashed())
    player.setPlaying(false);
  if (exportActiveFlag.load() || commandTaskActive.load() ||
      pluginScanActive.load() || audioConfigurationActive.load())
    return;

  if (!scrubbing && player.hasFinished())
    handleTrackEnd();
  if (!commandTaskActive.load() && engine.requiresPrepare())
    startCommandTask(
        [this] {
          const bool prepared = engine.preparePlugin([this](double rate) {
            StateLock lock(stateMutex);
            auto &player = engine.getMidiPlayer();
            player.setSampleRate(rate);
            player.seekTo(player.getPositionInSamples(), player.getPlaying());
          });
          return prepared ? juce::Result::ok()
                          : juce::Result::fail(engine.getLastPluginError());
        },
        {});
}

} // namespace midi
