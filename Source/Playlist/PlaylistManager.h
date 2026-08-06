#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

class PlaylistManager {
public:
  struct Track {
    juce::File file;
    juce::String name;
    double durationSeconds = 0.0;
    bool available = false;
  };

  struct ChangeLog {
    // 只记录本次会话尚未保存的增删/排序状态，不作为完整审计日志。
    int added = 0;
    int removed = 0;
    bool reordered = false;
    void reset() {
      added = 0;
      removed = 0;
      reordered = false;
    }
    bool hasChanges() const { return added > 0 || removed > 0 || reordered; }
  };

  PlaylistManager() = default;

  bool contains(const juce::File &file) const {
    for (const auto &t : tracks) {
      if (t.file == file)
        return true;
    }
    return false;
  }

  bool addFile(const juce::File &file, bool allowDuplicates = false) {
    if (!file.existsAsFile())
      return false;

    if (!file.hasFileExtension(".mid;.midi"))
      return false;

    if (!allowDuplicates && contains(file)) {
      return false;
    }

    tracks.add({file, file.getFileNameWithoutExtension(), 0.0, true});
    changeLog.added++;
    return true;
  }

  bool removeTrack(int index) {
    if (index < 0 || index >= tracks.size())
      return false;

    tracks.remove(index);
    changeLog.removed++;
    return true;
  }

  bool moveTrack(int fromIndex, int toIndex) {
    if (fromIndex < 0 || fromIndex >= tracks.size())
      return false;
    if (toIndex < 0 || toIndex >= tracks.size())
      return false;
    if (fromIndex == toIndex)
      return false;

    tracks.move(fromIndex, toIndex);
    changeLog.reordered = true;
    return true;
  }

  void clear() {
    if (!tracks.isEmpty()) {
      changeLog.removed += tracks.size();
      tracks.clear();
    }
  }

  const juce::Array<Track> &getTracks() const { return tracks; }

  int size() const { return tracks.size(); }
  bool isEmpty() const { return tracks.isEmpty(); }

  const Track *getTrack(int index) const {
    if (index < 0 || index >= tracks.size())
      return nullptr;
    return &tracks.getReference(index);
  }

  int findTrackIndex(const juce::File &file) const {
    for (int i = 0; i < tracks.size(); ++i)
      if (tracks[i].file == file)
        return i;
    return -1;
  }

  bool refreshTrack(int index) {
    if (index < 0 || index >= tracks.size())
      return false;
    auto &t = tracks.getReference(index);
    t.available = t.file.existsAsFile();
    if (!t.available)
      return false;
    t.name = t.file.getFileNameWithoutExtension();
    t.durationSeconds = 0.0;
    return true;
  }

  // JSON 写入使用临时文件替换目标文件，避免半写入文件被加载。
  juce::Result saveDetailed(const juce::File &file) const {
    try {
      juce::DynamicObject::Ptr root = new juce::DynamicObject();
      juce::Array<juce::var> trackList;

      for (const auto &t : tracks)
        trackList.add(t.file.getFullPathName());

      root->setProperty("version", 1);
      root->setProperty("tracks", trackList);

      auto tempFile = file.getSiblingFile(file.getFileName() + ".tmp");
      if (tempFile.exists() && !tempFile.deleteFile())
        return juce::Result::fail("Unable to delete stale playlist temp file: " +
                                  tempFile.getFullPathName());

      auto stream = tempFile.createOutputStream();
      if (stream == nullptr)
        return juce::Result::fail("Unable to open playlist for writing: " +
                                  tempFile.getFullPathName());

      if (!stream->setPosition(0) || stream->truncate().failed()) {
        const auto error = stream->getStatus().getErrorMessage();
        stream.reset();
        tempFile.deleteFile();
        return juce::Result::fail("Unable to reset playlist temp file: " +
                                  (error.isNotEmpty() ? error
                                                      : tempFile.getFullPathName()));
      }

      juce::JSON::writeToStream(*stream, juce::var(root.get()), true);
      stream->flush();
      if (stream->getStatus().failed()) {
        const auto error = stream->getStatus().getErrorMessage();
        stream.reset();
        tempFile.deleteFile();
        return juce::Result::fail("Unable to write playlist JSON: " + error);
      }
      stream.reset();

      if (!tempFile.replaceFileIn(file)) {
        tempFile.deleteFile();
        return juce::Result::fail("Unable to replace playlist file: " +
                                  file.getFullPathName());
      }

      changeLog.reset();
      return juce::Result::ok();
    } catch (...) {
      return juce::Result::fail("Unexpected exception while saving playlist: " +
                                file.getFullPathName());
    }
  }

  bool save(const juce::File &file) const { return saveDetailed(file).wasOk(); }

  // 缺失曲目仍保留原始路径和顺序；全部条目验证通过后一次性替换列表。
  juce::Result loadDetailed(const juce::File &file) {
    if (!file.existsAsFile())
      return juce::Result::fail("Playlist file does not exist: " +
                                file.getFullPathName());

    try {
      auto json = juce::JSON::parse(file);

      if (json.isVoid())
        return juce::Result::fail("Playlist JSON is empty or invalid: " +
                                  file.getFullPathName());

      auto *obj = json.getDynamicObject();
      if (obj == nullptr)
        return juce::Result::fail("Playlist JSON root is not an object: " +
                                  file.getFullPathName());

      auto *trackArray = obj->getProperty("tracks").getArray();
      if (trackArray == nullptr)
        return juce::Result::fail("Playlist JSON has no tracks array: " +
                                  file.getFullPathName());

      juce::Array<Track> loadedTracks;

      for (const auto &t : *trackArray) {
        if (!t.isString())
          return juce::Result::fail(
              "Playlist track entry is not a path string: " +
              file.getFullPathName());

        juce::File trackFile(t.toString());
        if (!trackFile.hasFileExtension(".mid;.midi"))
          return juce::Result::fail(
              "Playlist track entry is not a MIDI file: " +
              trackFile.getFullPathName());

        loadedTracks.add({trackFile, trackFile.getFileNameWithoutExtension(),
                          0.0, trackFile.existsAsFile()});
      }

      tracks.swapWith(loadedTracks);
      changeLog.reset();
      return juce::Result::ok();
    } catch (...) {
      return juce::Result::fail("Unexpected exception while loading playlist: " +
                                file.getFullPathName());
    }
  }

  bool load(const juce::File &file) { return loadDetailed(file).wasOk(); }

  enum class PlaybackMode {
    Sequential = 1,
    LoopList = 2,
    LoopSingle = 3,
    Shuffle = 4
  };

  void setPlaybackMode(PlaybackMode mode) { currentMode = mode; }
  PlaybackMode getPlaybackMode() const { return currentMode; }

  int getNextIndex(int currentIndex) const {
    if (tracks.isEmpty())
      return -1;

    switch (currentMode) {
    case PlaybackMode::LoopSingle:
      return currentIndex;

    case PlaybackMode::Shuffle: {
      if (tracks.size() == 1)
        return 0;
      int next;
      int attempts = 0;
      do {
        next = juce::Random::getSystemRandom().nextInt(tracks.size());
        attempts++;
      } while (next == currentIndex && attempts < 5);
      return next;
    }

    case PlaybackMode::LoopList:
      return (currentIndex + 1) % tracks.size();

    case PlaybackMode::Sequential:
    default:
      if (currentIndex >= tracks.size() - 1)
        return -1;
      return currentIndex + 1;
    }
  }

  // 上一曲不维护随机历史；Shuffle 与 Sequential 都按列表前一项处理。
  int getPreviousIndex(int currentIndex) const {
    if (tracks.isEmpty())
      return -1;

    if (currentMode == PlaybackMode::LoopList ||
        currentMode == PlaybackMode::LoopSingle) {
      int prev = currentIndex - 1;
      if (prev < 0)
        prev = tracks.size() - 1;
      return prev;
    }

    int prev = currentIndex - 1;
    if (prev < 0)
      return -1;
    return prev;
  }

private:
  juce::Array<Track> tracks;
  PlaybackMode currentMode = PlaybackMode::Sequential;
  mutable ChangeLog changeLog; // save() 为 const，保存成功后仍需清零变更记录。

public:
  const ChangeLog &getChangeLog() const { return changeLog; }
  bool hasChanges() const { return changeLog.hasChanges(); }

  juce::String getChangeSummary() const {
    juce::StringArray details;
    if (changeLog.added > 0)
      details.add(L"新增 " + juce::String(changeLog.added) + L" 首");
    if (changeLog.removed > 0)
      details.add(L"移除 " + juce::String(changeLog.removed) + L" 首");
    if (changeLog.reordered)
      details.add(L"列表排序已变更");

    return details.joinIntoString("\n - ");
  }
};
