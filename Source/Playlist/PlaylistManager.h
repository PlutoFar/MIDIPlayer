#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

/**
    播放列表管理器，负责管理 MIDI
   文件列表、播放顺序（顺序、循环、随机）以及持久化存储。
*/
class PlaylistManager {
public:
  struct Track {
    juce::File file;
    juce::String name;
    double durationSeconds = 0.0;
  };

  struct ChangeLog {
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

  /**
      Add a MIDI file to the playlist.
      @param file The MIDI file to add
      @return true if file was added successfully
  */
  /**
      Check if the file is already in the playlist.
  */
  bool contains(const juce::File &file) const {
    for (const auto &t : tracks) {
      if (t.file == file)
        return true;
    }
    return false;
  }

  /**
      Add a MIDI file to the playlist.
      @param file The MIDI file to add
      @param allowDuplicates If true, adds even if already present
      @return true if file was added successfully
  */
  bool addFile(const juce::File &file, bool allowDuplicates = false) {
    if (!file.existsAsFile())
      return false;

    if (!file.hasFileExtension(".mid;.midi"))
      return false;

    // Check for duplicates
    if (!allowDuplicates && contains(file)) {
      return false;
    }

    tracks.add({file, file.getFileNameWithoutExtension(), 0.0});
    changeLog.added++;
    return true;
  }

  /**
      Remove a track by index.
      @param index The index to remove
      @return true if removal was successful
  */
  bool removeTrack(int index) {
    if (index < 0 || index >= tracks.size())
      return false;

    tracks.remove(index);
    changeLog.removed++;
    return true;
  }

  /**
      Move a track from one position to another.
      @param fromIndex The source index
      @param toIndex The destination index
      @return true if move was successful
  */
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

  /**
      Get a track safely by index.
      @param index The track index
      @return Pointer to track, or nullptr if index is invalid
  */
  const Track *getTrack(int index) const {
    if (index < 0 || index >= tracks.size())
      return nullptr;
    return &tracks.getReference(index);
  }

  /**
      查找文件在播放列表中的索引。
      @return 索引，-1 表示不存在
  */
  int findTrackIndex(const juce::File &file) const {
    for (int i = 0; i < tracks.size(); ++i)
      if (tracks[i].file == file)
        return i;
    return -1;
  }

  /**
      刷新指定索引的曲目元数据（重新读取文件名）。
      用于"覆盖"已有条目时更新缓存。
  */
  bool refreshTrack(int index) {
    if (index < 0 || index >= tracks.size())
      return false;
    auto &t = tracks.getReference(index);
    if (!t.file.existsAsFile())
      return false;
    t.name = t.file.getFileNameWithoutExtension();
    t.durationSeconds = 0.0;
    return true;
  }

  // --- JSON Persistence ---

  /**
      将当前播放列表保存为 JSON 文件。

      实现说明：
      - 使用 JUCE DynamicObject 构建 JSON 树，保存所有文件的绝对路径。
      - 先写临时文件，再替换目标文件，避免半写入文件被当成有效播放列表。
  */
  bool save(const juce::File &file) const {
    try {
      juce::DynamicObject::Ptr root = new juce::DynamicObject();
      juce::Array<juce::var> trackList;

      for (const auto &t : tracks)
        trackList.add(t.file.getFullPathName());

      root->setProperty("version", 1);
      root->setProperty("tracks", trackList);

      auto tempFile = file.getSiblingFile(file.getFileName() + ".tmp");
      if (tempFile.exists() && !tempFile.deleteFile())
        return false;

      auto stream = tempFile.createOutputStream();
      if (stream == nullptr)
        return false;

      if (!stream->setPosition(0) || stream->truncate().failed()) {
        stream.reset();
        tempFile.deleteFile();
        return false;
      }

      juce::JSON::writeToStream(*stream, juce::var(root.get()), true);
      stream->flush();
      if (stream->getStatus().failed()) {
        stream.reset();
        tempFile.deleteFile();
        return false;
      }
      stream.reset();

      if (!tempFile.replaceFileIn(file)) {
        tempFile.deleteFile();
        return false;
      }

      changeLog.reset(); // Changes saved
      return true;
    } catch (...) {
      return false;
    }
  }

  /**
      Load a playlist from a JSON file.
      @param file The file to load from
      @return true if load was successful
  */
  bool load(const juce::File &file) {
    if (!file.existsAsFile())
      return false;

    try {
      auto json = juce::JSON::parse(file);

      if (json.isVoid())
        return false;

      auto *obj = json.getDynamicObject();
      if (obj == nullptr)
        return false;

      auto *trackArray = obj->getProperty("tracks").getArray();
      if (trackArray == nullptr)
        return false;

      tracks.clear();

      for (const auto &t : *trackArray) {
        juce::File trackFile(t.toString());
        if (trackFile.existsAsFile())
          addFile(trackFile);
      }

      changeLog.reset(); // Don't count loaded tracks as "new"
      return true;
    } catch (...) {
      return false;
    }
  }

  enum class PlaybackMode {
    Sequential = 1,
    LoopList = 2,
    LoopSingle = 3,
    Shuffle = 4
  };

  void setPlaybackMode(PlaybackMode mode) { currentMode = mode; }
  PlaybackMode getPlaybackMode() const { return currentMode; }

  /**
      根据当前的播放模式计算下一个曲目的索引。

      逻辑要点：
      - 单曲循环: 保持当前索引不变。
      - 随机播放:
     尝试获取一个与当前不同的索引（最多尝试5次，防止单曲不断重复导致的视觉死板）。
      - 列表循环: 索引递增并对总数取模。
      - 顺序播放: 到达末尾返回 -1 以表示停止。
  */
  int getNextIndex(int currentIndex) const {
    if (tracks.isEmpty())
      return -1;

    switch (currentMode) {
    case PlaybackMode::LoopSingle:
      return currentIndex; // Repeat current

    case PlaybackMode::Shuffle: {
      if (tracks.size() == 1)
        return 0;
      // Avoid playing the same track twice in a row if possible
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
        return -1; // Stop at end
      return currentIndex + 1;
    }
  }

  /**
      Get the previous track index.
      @param currentIndex Current track index
      @return Previous track index, or -1
  */
  int getPreviousIndex(int currentIndex) const {
    if (tracks.isEmpty())
      return -1;

    // Previous is usually just index - 1, but handle wrapping for loops
    if (currentMode == PlaybackMode::LoopList ||
        currentMode == PlaybackMode::LoopSingle) {
      // Even in LoopSingle, pressing "Prev" usually goes to previous track in
      // list (or restarts current, but let's say prev track for now)
      int prev = currentIndex - 1;
      if (prev < 0)
        prev = tracks.size() - 1;
      return prev;
    }

    // Sequential / Shuffle (History? For now just prev in list)
    int prev = currentIndex - 1;
    if (prev < 0)
      return -1; // Stop/Don't wrap
    return prev;
  }

private:
  juce::Array<Track> tracks;
  PlaybackMode currentMode = PlaybackMode::Sequential;
  mutable ChangeLog changeLog;

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
