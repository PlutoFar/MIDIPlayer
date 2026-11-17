#include "CoreImpl.h"
#include "../Midi/MidiFileLoader.h"
#include <set>

// Responsibilities: MIDI 解析、播放位置和切曲编排；界面通过 `Core` 快照更新显示。
// Concurrency: 可变曲目状态和序列生产由 `stateMutex` 串行化；实时消费只读取已发布快照。
// Ordering: 显式播放控制递增代次，延迟回调必须同时匹配代次和当前可播放条件。

namespace midi {

bool Core::Impl::canStartPlayback() {
  return audio.hasDevice() && engine.getMidiPlayer().hasSequence() && engine.hasPluginLoaded() &&
         !engine.requiresPrepare() && audioSession.allowsSequenceChange();
}

bool Core::Impl::beginMidiLoad(std::vector<MidiLoadTarget> targets,
                              std::function<void(bool)> completion,
                              std::function<void()> onPluginMissing) {
  const int generation = trackSwitchGeneration + 1;
  const bool navigation = targets.size() > 1;
  midiErrorText.clear();
  const auto accepted = startCommandTask(Operation::loadMidi,
      [this, targets = std::move(targets), navigation] {
        std::unique_ptr<juce::MidiMessageSequence> sequence;
        std::set<juce::File> failedFiles;
        juce::String firstError;
        size_t loadedTarget = 0;
        for (; loadedTarget < targets.size(); ++loadedTarget) {
          const auto &target = targets[loadedTarget];
          if (failedFiles.count(target.file) > 0)
            continue;
          const auto parsed = readMidiSequence(target.file, sequence);
          if (parsed.wasOk())
            break;
          if (firstError.isEmpty())
            firstError = parsed.getErrorMessage();
          failedFiles.insert(target.file);
        }

        StateLock resultLock(stateMutex);
        // Operation::loadMidi 禁止列表结构变更，候选索引在命令结束前有效。
        const auto availabilityBefore = playlist.getAvailabilityRevision();
        for (size_t index = 0; index < loadedTarget; ++index)
          if (const int row = targets[index].playlistIndex; row >= 0)
            playlist.setTrackAvailable(row, false);
        const bool discoveredUnavailable = availabilityBefore != playlist.getAvailabilityRevision();

        if (sequence == nullptr) {
          return juce::Result::fail(navigation
              ? L"未找到可播放的曲目。\n" + firstError : firstError);
        }

        const auto &[file, row] = targets[loadedTarget];
        engine.getMidiPlayer().setSequence(std::move(sequence), engine.liveSampleRate());
        loadedTrack = {file, 0};
        if (row >= 0) {
          playlist.setTrackAvailable(row, true);
          loadedTrack.id = playlist.getTrack(row)->id;
        } else {
          playlist.addFile(file);
          const int existingRow = playlist.findTrackIndex(file);
          if (const auto *entry = playlist.getTrack(existingRow)) {
            playlist.setTrackAvailable(existingRow, true);
            loadedTrack.id = entry->id;
          }
        }
        if (discoveredUnavailable) {
          midiSkippedTracks = static_cast<int>(loadedTarget);
          ++midiSkipRevision;
        }
        return juce::Result::ok();
      },
      [this, generation, completion = std::move(completion),
       onPluginMissing = std::move(onPluginMissing)](juce::Result result) {
        if (result.wasOk()) {
          StateLock resultLock(stateMutex);
          settings.setLastMidiDirectory(loadedTrack.source.getParentDirectory().getFullPathName());
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
  if (audioSession.isExporting())
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
  if (audioSession.isExporting())
    return;
  resetPlayback();
}

void Core::Impl::resetPlayback() {
  ++trackSwitchGeneration;
  engine.getMidiPlayer().setPlaying(false);
  engine.getMidiPlayer().seekTo(0);
}

void Core::Impl::clearLoadedTrack() {
  ++trackSwitchGeneration;
  loadedTrack = {};
  auto &player = engine.getMidiPlayer();
  player.setPlaying(false);
  player.setSequence(nullptr, engine.liveSampleRate());
}

int Core::Impl::currentTrackIndex() const {
  return loadedTrack.id == 0 ? -1 : playlist.findTrackIndex(loadedTrack.id);
}

void Core::Impl::seek(double ratio) {
  StateLock lock(stateMutex);
  if (!audioSession.allowsSequenceChange())
    return;
  auto &mp = engine.getMidiPlayer();
  const double dur = mp.getDurationInSamples();
  if (dur > 0)
    mp.seekTo(juce::jlimit(0.0, 1.0, ratio) * dur);
}

void Core::Impl::volume(float value) {
  StateLock lock(stateMutex);
  if (!audioSession.isExporting())
    engine.setMasterVolume(value);
}

void Core::Impl::next() {
  StateLock lock(stateMutex);
  loadAdjacentTrack(PlaylistManager::Advance::next);
}

void Core::Impl::prev() {
  StateLock lock(stateMutex);
  loadAdjacentTrack(PlaylistManager::Advance::previous);
}

void Core::Impl::loadAdjacentTrack(PlaylistManager::Advance advance) {
  if (!audioSession.isIdle() || playlist.isEmpty() ||
      !audio.hasDevice() || !engine.hasPluginLoaded() || engine.requiresPrepare())
    return;

  const auto candidates = playlist.playbackCandidates(currentTrackIndex(), advance);
  if (candidates.empty()) {
    resetPlayback();
    return;
  }

  std::vector<MidiLoadTarget> targets;
  targets.reserve(candidates.size());
  for (const int index : candidates)
    targets.push_back({playlist.getTrack(index)->file, index});
  beginMidiLoad(std::move(targets));
}

void Core::Impl::handleTrackEnd() {
  resetPlayback();
  loadAdjacentTrack(PlaylistManager::Advance::natural);
}

} // namespace midi
