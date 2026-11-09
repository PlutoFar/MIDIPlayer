#include "CoreImpl.h"

// Responsibilities: 列表变更、当前曲目索引及文件路径同步；磁盘格式由 `PlaylistManager` 处理。
// Invariant: 删除当前项或清空列表同时清空 MIDI 序列，并使旧延迟播放请求失效。

namespace midi {

bool Core::Impl::addToPlaylist(const juce::File &file) {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load() || commandChangesAudio.load())
    return false;
  const bool ok = playlist.addFile(file);
  return ok;
}

int Core::Impl::addFilesToPlaylist(const std::vector<juce::File> &files) {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load() || commandChangesAudio.load())
    return 0;
  int added = 0;
  for (const auto &file : files) {
    if (playlist.addFile(file))
      ++added;
  }
  return added;
}

bool Core::Impl::removeTrack(TrackId id) {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load() || commandChangesAudio.load())
    return false;
  const int index = playlist.findTrackIndex(id);
  const bool ok = playlist.removeTrack(index);
  if (ok) {
    if (currentTrackIndex == index) {
      ++trackSwitchGeneration;
      currentTrackIndex = -1;
      currentMidiFile = {};
      currentMidiName = {};
      engine.getMidiPlayer().setPlaying(false);
      engine.getMidiPlayer().setSequence(nullptr, engine.liveSampleRate());
    } else if (currentTrackIndex > index) {
      --currentTrackIndex;
    }
  }
  return ok;
}

bool Core::Impl::moveTrack(TrackId id, int toIndex) {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load() || commandChangesAudio.load())
    return false;
  const int fromIndex = playlist.findTrackIndex(id);
  const bool ok = playlist.moveTrack(fromIndex, toIndex);
  if (ok) {
    if (currentTrackIndex == fromIndex) {
      currentTrackIndex = toIndex;
    } else if (fromIndex < currentTrackIndex && currentTrackIndex <= toIndex) {
      --currentTrackIndex;
    } else if (toIndex <= currentTrackIndex && currentTrackIndex < fromIndex) {
      ++currentTrackIndex;
    }
  }
  return ok;
}

bool Core::Impl::refreshTrack(int index) {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load() || commandChangesAudio.load())
    return false;
  const bool ok = playlist.refreshTrack(index);
  return ok;
}

bool Core::Impl::clearPlaylist() {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load() || commandChangesAudio.load())
    return false;
  ++trackSwitchGeneration;
  playlist.clear();
  currentTrackIndex = -1;
  currentMidiFile = {};
  currentMidiName = {};
  currentPlaylistFile = {};
  playlistErrorText.clear();
  settings.setLastPlaylistPath({});
  engine.getMidiPlayer().setPlaying(false);
  engine.getMidiPlayer().setSequence(nullptr, engine.liveSampleRate());
  return true;
}

void Core::Impl::setPlayMode(int mode) {
  StateLock lock(stateMutex);
  if (mode < 1 || mode > 4)
    mode = 1;
  playlist.setPlaybackMode(static_cast<midi::PlaybackMode>(mode));
  settings.setPlayMode(mode);
}

bool Core::Impl::saveList(const juce::File &file) {
  PlaylistManager::Tracks snapshot;
  uint64_t revision;
  {
    StateLock lock(stateMutex);
    if (exportActiveFlag.load() || commandChangesAudio.load()) {
      playlistErrorText = L"当前操作尚未结束，无法保存播放列表。";
      return false;
    }
    snapshot = playlist.getTracks();
    revision = playlist.getRevision();
  }

  const auto result = PlaylistManager::write(file, snapshot);
  {
    StateLock lock(stateMutex);
    playlistErrorText = result.getErrorMessage();
    if (result.wasOk()) {
      playlist.markSaved(revision);
      currentPlaylistFile = file;
    }
  }
  if (result.wasOk())
    settings.setLastPlaylistPath(file.getFullPathName());
  return result.wasOk();
}

bool Core::Impl::loadList(const juce::File &file) {
  PlaylistManager::Tracks loadedTracks;
  const auto result = PlaylistManager::read(file, loadedTracks);
  {
    StateLock lock(stateMutex);
    if (exportActiveFlag.load() || commandChangesAudio.load()) {
      playlistErrorText = L"当前操作尚未结束，无法加载播放列表。";
      return false;
    }
    playlistErrorText = result.getErrorMessage();
    if (result.failed())
      return false;
    playlist.replace(std::move(loadedTracks));
    currentPlaylistFile = file;
    currentTrackIndex = playlist.findTrackIndex(currentMidiFile);
    ++trackSwitchGeneration;
  }
  settings.setLastPlaylistPath(file.getFullPathName());
  return true;
}

} // namespace midi
