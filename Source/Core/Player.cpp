#include "CoreImpl.h"

// Core/Player —— 播放、暂停、停止、seek、切曲、曲目结束、命令行/Shell 打开。
// 从 MainContentComponent 抽出，保留原有代际守卫与延迟恢复语义；UI 相关的
// 标签/列表高亮改由前端读取 state() 同步，不在这里触碰。

namespace midi {

bool Core::Impl::canStartPlayback() {
  StateLock lock(stateMutex);
  return engine.getMidiPlayer().hasSequence() && engine.hasPluginLoaded() &&
         !engine.hasPluginWorkerCrashed() && !pluginLoadActive.load() &&
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
  notify();
  return true;
}

void Core::Impl::play() {
  StateLock lock(stateMutex);
  if (!canStartPlayback())
    return;
  pendingResumePlayback = false;
  auto &mp = engine.getMidiPlayer();
  mp.seekTo(mp.getPositionInSamples(), true);
  mp.setPlaying(true);
}

void Core::Impl::pause() {
  StateLock lock(stateMutex);
  if (!exportActiveFlag.load())
    engine.getMidiPlayer().setPlaying(false);
}

void Core::Impl::togglePlay() {
  StateLock lock(stateMutex);
  if (!canStartPlayback())
    return;
  pendingResumePlayback = false;
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
  pendingResumePlayback = false;
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
  lastSeekRequestMs = juce::Time::getMillisecondCounter();
}

void Core::Impl::volume(float value) {
  StateLock lock(stateMutex);
  if (!exportActiveFlag.load()) {
    engine.setMasterVolume(value);
    getAppSettings().setMasterVolume(value);
  }
}

void Core::Impl::next() {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load() || pluginLoadActive.load())
    return;
  if (playlist.isEmpty() || !engine.hasPluginLoaded())
    return;

  isHandlingTrackEnd = false;
  engine.getMidiPlayer().setPlaying(false);

  currentTrackIndex = playlist.getNextIndex(currentTrackIndex);
  if (currentTrackIndex == -1) {
    stop();
    return;
  }

  if (const auto *track = playlist.getTrack(currentTrackIndex)) {
    if (loadMidi(track->file)) {
      ++trackSwitchGeneration;
      const int gen = trackSwitchGeneration;
      scheduleAfter(100, [gen](Impl &self) {
        if (self.trackSwitchGeneration != gen)
          return;
        self.engine.getMidiPlayer().setPlaying(true);
      });
    }
  }
}

void Core::Impl::prev() {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load() || pluginLoadActive.load())
    return;
  if (playlist.isEmpty() || !engine.hasPluginLoaded())
    return;

  isHandlingTrackEnd = false;
  engine.getMidiPlayer().setPlaying(false);

  currentTrackIndex = playlist.getPreviousIndex(currentTrackIndex);
  if (currentTrackIndex == -1) {
    stop();
    return;
  }
  if (const auto *track = playlist.getTrack(currentTrackIndex)) {
    if (loadMidi(track->file)) {
      ++trackSwitchGeneration;
      const int gen = trackSwitchGeneration;
      scheduleAfter(100, [gen](Impl &self) {
        if (self.trackSwitchGeneration != gen)
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
    if (self.trackSwitchGeneration != myGeneration) {
      self.isHandlingTrackEnd = false;
      return;
    }

    const int nextIndex = self.playlist.getNextIndex(self.currentTrackIndex);
    if (nextIndex != -1) {
      self.currentTrackIndex = nextIndex;
      if (const auto *track = self.playlist.getTrack(self.currentTrackIndex)) {
        if (self.loadMidi(track->file)) {
          self.scheduleAfter(100, [myGeneration](Impl &delayed) {
            if (delayed.trackSwitchGeneration != myGeneration)
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
  StateLock lock(stateMutex);
  if (exportActiveFlag.load() || pluginLoadActive.load())
    return false;
  if (!file.existsAsFile())
    return false;

  const auto ext = file.getFileExtension().toLowerCase();
  if (ext != ".mid" && ext != ".midi")
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

  bool loaded = false;
  if (loadMidi(file)) {
    loaded = true;
    if (engine.hasPluginLoaded()) {
      scheduleAfter(150, [](Impl &self) {
        self.engine.getMidiPlayer().setPlaying(true);
      });
    } else if (autoLoadPluginIfMissing && onPluginMissing) {
      onPluginMissing();
    }
  }

  getAppSettings().setLastMidiDirectory(
      file.getParentDirectory().getFullPathName());
  notify();
  return loaded;
}

void Core::Impl::tick(bool uiSuppressTrackAdvance) {
  StateLock lock(stateMutex);
  auto &player = engine.getMidiPlayer();
  const double sr = sampleRate();
  const bool isExporting = exportActiveFlag.load();

  if (!isExporting && player.hasSequence() &&
      std::abs(player.getSequenceSampleRate() - sr) >= 0.01)
    player.setSampleRate(sr);

  // 曲目自然结束后推进到下一曲（用户拖动进度条期间抑制）。
  if (!isExporting && !uiSuppressTrackAdvance && player.hasFinished())
    handleTrackEnd();

  // worker 崩溃后停止播放；崩溃提示由 UI 层依据 state() 呈现。
  if (engine.hasPluginWorkerCrashed())
    player.setPlaying(false);

  // 异步 seek 后短暂延迟恢复播放（当前 seek 流程不置位，此分支多为休眠）。
  if (!engine.hasPluginLoaded()) {
    pendingResumePlayback = false;
  } else if (!isExporting && pendingResumePlayback) {
    if (juce::Time::getMillisecondCounter() - lastSeekRequestMs > 50) {
      player.setPlaying(true);
      pendingResumePlayback = false;
    }
  }
}

} // namespace midi
