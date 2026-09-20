#include "CoreImpl.h"

// Playlist commands own index updates, persistence and delayed-play
// invalidation.

namespace midi {

bool Core::Impl::addToPlaylist(const juce::File &file) {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load())
    return false;
  const bool ok = playlist.addFile(file);
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
  return added;
}

bool Core::Impl::removeTrack(int index) {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load())
    return false;
  const bool ok = playlist.removeTrack(index);
  if (ok) {
    if (currentTrackIndex == index) {
      ++trackSwitchGeneration;
      isHandlingTrackEnd = false;
      currentTrackIndex = -1;
      currentMidiFile = {};
      currentMidiName = {};
      engine.getMidiPlayer().setPlaying(false);
      engine.getMidiPlayer().setSequence(nullptr, sampleRate());
    } else if (currentTrackIndex > index) {
      --currentTrackIndex;
    }
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
  }
  return ok;
}

bool Core::Impl::refreshTrack(int index) {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load())
    return false;
  const bool ok = playlist.refreshTrack(index);
  return ok;
}

bool Core::Impl::clearPlaylist() {
  StateLock lock(stateMutex);
  if (exportActiveFlag.load())
    return false;
  ++trackSwitchGeneration;
  isHandlingTrackEnd = false;
  playlist.clear();
  currentTrackIndex = -1;
  currentMidiFile = {};
  currentMidiName = {};
  currentPlaylistFile = {};
  playlistErrorText.clear();
  getAppSettings().setLastPlaylistPath({});
  engine.getMidiPlayer().setPlaying(false);
  engine.getMidiPlayer().setSequence(nullptr, sampleRate());
  return true;
}

void Core::Impl::setPlayMode(int mode) {
  StateLock lock(stateMutex);
  if (mode < 1 || mode > 4)
    mode = 1;
  playlist.setPlaybackMode(static_cast<midi::PlaybackMode>(mode));
  getAppSettings().setPlayMode(mode);
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
    currentTrackIndex = playlist.findTrackIndex(currentMidiFile);
    ++trackSwitchGeneration;
    isHandlingTrackEnd = false;
  }
  return result.wasOk();
}

} // namespace midi
