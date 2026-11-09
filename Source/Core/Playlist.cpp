#include "CoreImpl.h"

// Responsibilities: 列表变更、当前曲目索引及文件路径同步；磁盘格式由 `PlaylistManager` 处理。
// Invariant: 删除当前项或清空列表同时清空 MIDI 序列，并使旧延迟播放请求失效。

namespace midi {

bool Core::Impl::addToPlaylist(const juce::File &file) {
  StateLock lock(stateMutex);
  if (!audioSession.allowsSequenceChange())
    return false;
  const bool ok = playlist.addFile(file);
  return ok;
}

int Core::Impl::addFilesToPlaylist(const std::vector<juce::File> &files) {
  StateLock lock(stateMutex);
  if (!audioSession.allowsSequenceChange())
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
  if (!audioSession.allowsSequenceChange())
    return false;
  if (!playlist.removeTrack(playlist.findTrackIndex(id)))
    return false;
  if (loadedTrack.id == id)
    clearLoadedTrack();
  return true;
}

bool Core::Impl::moveTrack(TrackId id, int toIndex) {
  StateLock lock(stateMutex);
  if (!audioSession.allowsSequenceChange())
    return false;
  return playlist.moveTrack(playlist.findTrackIndex(id), toIndex);
}

bool Core::Impl::refreshTrack(int index) {
  StateLock lock(stateMutex);
  if (!audioSession.allowsSequenceChange())
    return false;
  const bool ok = playlist.refreshTrack(index);
  return ok;
}

bool Core::Impl::clearPlaylist() {
  StateLock lock(stateMutex);
  if (!audioSession.allowsSequenceChange())
    return false;
  playlist.clear();
  clearLoadedTrack();
  currentPlaylistFile = {};
  playlistErrorText.clear();
  settings.setLastPlaylistPath({});
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
    if (!audioSession.allowsSequenceChange()) {
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
    if (!audioSession.allowsSequenceChange()) {
      playlistErrorText = L"当前操作尚未结束，无法加载播放列表。";
      return false;
    }
    playlistErrorText = result.getErrorMessage();
    if (result.failed())
      return false;
    const auto occurrence = playlist.occurrenceOf(loadedTrack.id);
    playlist.replace(std::move(loadedTracks));
    currentPlaylistFile = file;
    // JSON 不保存会话 ID。保留同一路径的条目序号，条目缺失时解除绑定。
    loadedTrack.id = playlist.bindFile(loadedTrack.source, occurrence);
    ++trackSwitchGeneration;
  }
  settings.setLastPlaylistPath(file.getFullPathName());
  return true;
}

} // namespace midi
