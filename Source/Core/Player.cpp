#include "CoreImpl.h"

// Core/Player —— 播放、暂停、停止、seek、切曲、曲目结束、命令行/Shell 打开。
// 从 MainContentComponent 抽出，保留原有代际守卫与延迟恢复语义；UI 相关的
// 标签/列表高亮改由前端读取 state() 同步，不在这里触碰。

namespace midi {

bool Core::Impl::canStartPlayback() {
  StateLock lock(stateMutex);
  return engine.getMidiPlayer().hasSequence() && engine.hasPluginLoaded() &&
         !engine.hasPluginWorkerCrashed() && !pluginChangesAudio.load() &&
         !exportActiveFlag.load();
}

bool Core::Impl::loadMidi(const juce::File &file) {
  StateLock lock(stateMutex);
  if (!file.existsAsFile())
    return false;

  juce::MidiFile mf;
  auto stream = file.createInputStream();
  if (stream == nullptr || !mf.readFrom(*stream))
    return false;

  const double sr = sampleRate();
  mf.convertTimestampTicksToSeconds();

  auto seq = std::make_unique<juce::MidiMessageSequence>();
  for (int i = 0; i < mf.getNumTracks(); ++i) {
    if (auto *t = mf.getTrack(i)) {
      for (int j = 0; j < t->getNumEvents(); ++j)
        seq->addEvent(t->getEventPointer(j)->message);
    }
  }
  seq->updateMatchedPairs();
  seq->sort();
  engine.getMidiPlayer().setSequence(std::move(seq), sr);
  currentMidiFile = file;
  currentMidiName = file.getFileNameWithoutExtension();
  return true;
}

void Core::Impl::play() {
  StateLock lock(stateMutex);
  if (!canStartPlayback())
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
    // 恢复播放前在当前位置追踪 CC、音符、踏板和弯音状态，避免暂停期间
    // allSoundOff 清理过的控制状态丢失。
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
  if (exportActiveFlag.load())
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
  if (exportActiveFlag.load() || pluginChangesAudio.load())
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
    if (loadMidi(track->file)) {
      currentTrackIndex = nextIndex;
      ++trackSwitchGeneration;
      const int gen = trackSwitchGeneration;
      scheduleAfter(100, [gen](Impl &self) {
        if (self.trackSwitchGeneration != gen || !self.canStartPlayback())
          return;
        self.engine.getMidiPlayer().setPlaying(true);
      });
    }
  }
}

void Core::Impl::prev() {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load() || pluginChangesAudio.load())
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
    if (loadMidi(track->file)) {
      currentTrackIndex = previousIndex;
      ++trackSwitchGeneration;
      const int gen = trackSwitchGeneration;
      scheduleAfter(100, [gen](Impl &self) {
        if (self.trackSwitchGeneration != gen || !self.canStartPlayback())
          return;
        self.engine.getMidiPlayer().setPlaying(true);
      });
    }
  }
}

void Core::Impl::handleTrackEnd() {
  StateLock lock(stateMutex);
  if (isHandlingTrackEnd)
    return;

  isHandlingTrackEnd = true;
  ++trackSwitchGeneration;
  const int myGeneration = trackSwitchGeneration;
  engine.getMidiPlayer().setPlaying(false);
  engine.getMidiPlayer().seekTo(0);

  scheduleAfter(0, [myGeneration](Impl &self) {
    if (self.trackSwitchGeneration != myGeneration ||
        !self.canStartPlayback()) {
      return;
    }

    const int nextIndex = self.playlist.getNextIndex(self.currentTrackIndex);
    if (nextIndex != -1) {
      if (const auto *track = self.playlist.getTrack(nextIndex)) {
        if (self.loadMidi(track->file)) {
          self.currentTrackIndex = nextIndex;
          self.scheduleAfter(100, [myGeneration](Impl &delayed) {
            if (delayed.trackSwitchGeneration != myGeneration ||
                !delayed.canStartPlayback())
              return;
            delayed.engine.getMidiPlayer().setPlaying(true);
            delayed.isHandlingTrackEnd = false;
          });
          return;
        }
      }
    }
    self.isHandlingTrackEnd = false;
  });
}

bool Core::Impl::openMidi(const juce::File &file, bool autoLoadPluginIfMissing,
                          std::function<void()> onPluginMissing) {
  std::unique_lock<std::recursive_mutex> lock(stateMutex);
  if (exportActiveFlag.load() || pluginChangesAudio.load())
    return false;
  if (!file.existsAsFile())
    return false;

  const auto ext = file.getFileExtension().toLowerCase();
  if (ext != ".mid" && ext != ".midi")
    return false;

  ++trackSwitchGeneration;
  isHandlingTrackEnd = false;
  if (!loadMidi(file))
    return false;

  const bool hasExistingPlaylist = (playlist.size() > 0);

  if (hasExistingPlaylist) {
    if (playlist.contains(file)) {
      const int idx = playlist.findTrackIndex(file);
      if (idx >= 0)
        currentTrackIndex = idx;
    } else if (playlist.addFile(file)) {
      currentTrackIndex = playlist.size() - 1;
    }
  } else {
    playlist.clear();
    currentPlaylistFile = juce::File();
    currentTrackIndex = -1;
    if (playlist.addFile(file))
      currentTrackIndex = 0;
  }

  const bool requestPlugin =
      !engine.hasPluginLoaded() && autoLoadPluginIfMissing;
  if (engine.hasPluginLoaded()) {
    const int generation = trackSwitchGeneration;
    scheduleAfter(150, [generation](Impl &self) {
      if (self.trackSwitchGeneration != generation || !self.canStartPlayback())
        return;
      self.engine.getMidiPlayer().setPlaying(true);
    });
  }

  getAppSettings().setLastMidiDirectory(
      file.getParentDirectory().getFullPathName());
  // UI callbacks may create modal windows; no core lock crosses that boundary.
  lock.unlock();
  if (requestPlugin && onPluginMissing)
    onPluginMissing();
  return true;
}

void Core::Impl::tick(bool uiSuppressTrackAdvance) {
  StateLock lock(stateMutex);
  auto &player = engine.getMidiPlayer();
  const double sr = sampleRate();
  const bool isExporting = exportActiveFlag.load();

  if (!isExporting && !pluginChangesAudio.load() && player.hasSequence() &&
      std::abs(player.getSequenceSampleRate() - sr) >= 0.01)
    player.setSampleRate(sr);

  // 曲目自然结束后推进到下一曲（用户拖动进度条期间抑制）。
  if (!isExporting && !pluginChangesAudio.load() && !uiSuppressTrackAdvance &&
      player.hasFinished())
    handleTrackEnd();

  // worker 崩溃后停止播放；崩溃提示由 UI 层依据 state() 呈现。
  if (engine.hasPluginWorkerCrashed())
    player.setPlaying(false);
}

} // namespace midi
