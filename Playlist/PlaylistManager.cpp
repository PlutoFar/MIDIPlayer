#include "PlaylistManager.h"

void PlaylistManager::ChangeLog::reset() {
  added = 0;
  removed = 0;
  reordered = false;
}

bool PlaylistManager::ChangeLog::hasChanges() const {
  return added > 0 || removed > 0 || reordered;
}

bool PlaylistManager::contains(const juce::File &file) const {
  return findTrackIndex(file) >= 0;
}

bool PlaylistManager::addFile(const juce::File &file, bool allowDuplicates) {
  if (!file.hasFileExtension(".mid;.midi"))
    return false;

  if (!allowDuplicates && contains(file))
    return false;

  tracks.add(
      {file, file.getFileNameWithoutExtension(), 0.0, !file.existsAsFile()});
  ++changeLog.added;
  return true;
}

bool PlaylistManager::removeTrack(int index) {
  if (!juce::isPositiveAndBelow(index, tracks.size()))
    return false;

  tracks.remove(index);
  ++changeLog.removed;
  return true;
}

bool PlaylistManager::moveTrack(int fromIndex, int toIndex) {
  if (!juce::isPositiveAndBelow(fromIndex, tracks.size()) ||
      !juce::isPositiveAndBelow(toIndex, tracks.size()) ||
      fromIndex == toIndex) {
    return false;
  }

  tracks.move(fromIndex, toIndex);
  changeLog.reordered = true;
  return true;
}

void PlaylistManager::clear() {
  if (tracks.isEmpty())
    return;

  changeLog.removed += tracks.size();
  tracks.clear();
}

const juce::Array<PlaylistManager::Track> &
PlaylistManager::getTracks() const {
  return tracks;
}

const PlaylistManager::Track *PlaylistManager::getTrack(int index) const {
  return juce::isPositiveAndBelow(index, tracks.size())
             ? &tracks.getReference(index)
             : nullptr;
}

int PlaylistManager::findTrackIndex(const juce::File &file) const {
  for (int i = 0; i < tracks.size(); ++i)
    if (tracks.getReference(i).file == file)
      return i;
  return -1;
}

bool PlaylistManager::refreshTrack(int index) {
  if (!juce::isPositiveAndBelow(index, tracks.size()))
    return false;

  auto &track = tracks.getReference(index);
  track.missing = !track.file.existsAsFile();
  if (track.missing)
    return false;

  track.name = track.file.getFileNameWithoutExtension();
  track.durationSeconds = 0.0;
  return true;
}

int PlaylistManager::size() const { return tracks.size(); }

bool PlaylistManager::isEmpty() const { return tracks.isEmpty(); }

bool PlaylistManager::save(const juce::File &file) const {
  auto stream = file.createOutputStream();
  if (stream == nullptr)
    return false;

  juce::DynamicObject::Ptr root = new juce::DynamicObject();
  juce::Array<juce::var> trackList;
  const auto playlistDirectory = file.getParentDirectory();

  for (const auto &track : tracks) {
    juce::DynamicObject::Ptr entry = new juce::DynamicObject();
    const bool useRelativePath = track.file.isAChildOf(playlistDirectory);
    entry->setProperty(
        "path", useRelativePath
                    ? track.file.getRelativePathFrom(playlistDirectory)
                    : track.file.getFullPathName());
    entry->setProperty("relative", useRelativePath);
    trackList.add(juce::var(entry.get()));
  }

  root->setProperty("version", 2);
  root->setProperty("tracks", trackList);

  stream->setPosition(0);
  stream->truncate();
  juce::JSON::writeToStream(*stream, juce::var(root.get()), true);
  stream->flush();
  if (stream->getStatus().failed())
    return false;

  changeLog.reset();
  return true;
}

bool PlaylistManager::load(const juce::File &file) {
  if (!file.existsAsFile())
    return false;

  auto json = juce::JSON::parse(file);
  auto *root = json.getDynamicObject();
  if (root == nullptr)
    return false;

  auto *trackArray = root->getProperty("tracks").getArray();
  if (trackArray == nullptr)
    return false;

  juce::Array<Track> loadedTracks;
  const auto playlistDirectory = file.getParentDirectory();

  for (const auto &value : *trackArray) {
    juce::String path;
    bool relative = false;

    if (auto *entry = value.getDynamicObject()) {
      path = entry->getProperty("path").toString();
      relative = static_cast<bool>(entry->getProperty("relative"));
    } else {
      path = value.toString();
    }

    if (path.isEmpty())
      continue;

    auto trackFile =
        relative ? playlistDirectory.getChildFile(path) : juce::File(path);
    if (!trackFile.hasFileExtension(".mid;.midi"))
      continue;

    loadedTracks.add({trackFile, trackFile.getFileNameWithoutExtension(), 0.0,
                      !trackFile.existsAsFile()});
  }

  tracks = std::move(loadedTracks);
  changeLog.reset();
  return true;
}

void PlaylistManager::setPlaybackMode(PlaybackMode mode) {
  currentMode = mode;
}

PlaylistManager::PlaybackMode PlaylistManager::getPlaybackMode() const {
  return currentMode;
}

int PlaylistManager::getNextIndex(int currentIndex) const {
  if (tracks.isEmpty())
    return -1;

  if (currentIndex < 0)
    return 0;
  if (currentIndex >= tracks.size())
    currentIndex = tracks.size() - 1;

  switch (currentMode) {
  case PlaybackMode::LoopSingle:
    return currentIndex;
  case PlaybackMode::Shuffle: {
    if (tracks.size() == 1)
      return 0;

    int next = currentIndex;
    while (next == currentIndex)
      next = juce::Random::getSystemRandom().nextInt(tracks.size());
    return next;
  }
  case PlaybackMode::LoopList:
    return (currentIndex + 1) % tracks.size();
  case PlaybackMode::Sequential:
  default:
    return currentIndex < tracks.size() - 1 ? currentIndex + 1 : -1;
  }
}

int PlaylistManager::getPreviousIndex(int currentIndex) const {
  if (tracks.isEmpty())
    return -1;

  if (currentIndex >= tracks.size())
    currentIndex = tracks.size() - 1;
  if (currentIndex < 0)
    return currentMode == PlaybackMode::LoopList ? tracks.size() - 1 : -1;

  if (currentMode == PlaybackMode::LoopList ||
      currentMode == PlaybackMode::LoopSingle) {
    return currentIndex > 0 ? currentIndex - 1 : tracks.size() - 1;
  }

  return currentIndex > 0 ? currentIndex - 1 : -1;
}

const PlaylistManager::ChangeLog &PlaylistManager::getChangeLog() const {
  return changeLog;
}

bool PlaylistManager::hasChanges() const { return changeLog.hasChanges(); }

bool PlaylistManager::hasMissingFiles() const {
  for (const auto &track : tracks)
    if (track.missing)
      return true;
  return false;
}

juce::String PlaylistManager::getChangeSummary() const {
  juce::StringArray details;
  if (changeLog.added > 0)
    details.add(L"新增 " + juce::String(changeLog.added) + L" 首");
  if (changeLog.removed > 0)
    details.add(L"移除 " + juce::String(changeLog.removed) + L" 首");
  if (changeLog.reordered)
    details.add(L"列表排序已变更");
  return details.joinIntoString("\n - ");
}
