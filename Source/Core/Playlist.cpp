#include "CoreImpl.h"

// Core/Playlist —— 播放列表保存/加载与未保存状态。曲目增删/重复检测仍由
// PlaylistManager 承载（经 LegacyCoreAdapter 给 Legacy 控件过渡使用）。

namespace midi {

bool Core::Impl::addToPlaylist(const juce::File &file) {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load())
    return false;
  const bool ok = playlist.addFile(file);
  if (ok)
    notify();
  return ok;
}

int Core::Impl::addFilesToPlaylist(const std::vector<juce::File> &files) {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load())
    return 0;
  int added = 0;
  for (const auto &file : files) {
    if (playlist.addFile(file))
      ++added;
  }
  if (added > 0)
    notify();
  return added;
}

bool Core::Impl::removeTrack(int index) {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load())
    return false;
  const bool ok = playlist.removeTrack(index);
  if (ok) {
    if (currentTrackIndex == index) {
      currentTrackIndex = -1;
      currentMidiFile = {};
      currentMidiName = {};
      engine.getMidiPlayer().setPlaying(false);
    } else if (currentTrackIndex > index) {
      --currentTrackIndex;
    }
    notify();
  }
  return ok;
}

bool Core::Impl::moveTrack(int fromIndex, int toIndex) {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load())
    return false;
  const bool ok = playlist.moveTrack(fromIndex, toIndex);
  if (ok) {
    if (currentTrackIndex == fromIndex) {
      currentTrackIndex = toIndex;
    } else if (fromIndex < currentTrackIndex && currentTrackIndex <= toIndex) {
      --currentTrackIndex;
    } else if (toIndex <= currentTrackIndex && currentTrackIndex < fromIndex) {
      ++currentTrackIndex;
    }
    notify();
  }
  return ok;
}

bool Core::Impl::refreshTrack(int index) {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load())
    return false;
  const bool ok = playlist.refreshTrack(index);
  if (ok)
    notify();
  return ok;
}

void Core::Impl::clearPlaylist() {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load())
    return;
  playlist.clear();
  currentTrackIndex = -1;
  currentMidiFile = {};
  currentMidiName = {};
  currentPlaylistFile = {};
  playlistErrorText.clear();
  getAppSettings().setLastPlaylistPath({});
  engine.getMidiPlayer().setPlaying(false);
  notify();
}

void Core::Impl::setPlayMode(int mode) {
  StateLock lock(stateMutex);
  if (mode < 1 || mode > 4)
    mode = 1;
  playlist.setPlaybackMode(static_cast<PlaylistManager::PlaybackMode>(mode));
  getAppSettings().setPlayMode(mode);
  notify();
}

juce::File Core::Impl::trackFileAt(int index) const {
  StateLock lock(stateMutex);
  if (const auto *track = playlist.getTrack(index))
    return track->file;
  return {};
}

bool Core::Impl::saveList(const juce::File &file) {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load())
    return false;

  const auto result = playlist.saveDetailed(file);
  playlistErrorText = result.getErrorMessage();
  if (result.wasOk()) {
    currentPlaylistFile = file;
    getAppSettings().setLastPlaylistPath(file.getFullPathName());
  }
  notify();
  return result.wasOk();
}

bool Core::Impl::loadList(const juce::File &file) {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load())
    return false;

  const auto result = playlist.loadDetailed(file);
  playlistErrorText = result.getErrorMessage();
  if (result.wasOk()) {
    currentPlaylistFile = file;
    getAppSettings().setLastPlaylistPath(file.getFullPathName());
    currentTrackIndex = -1;
  }
  notify();
  return result.wasOk();
}

} // namespace midi
