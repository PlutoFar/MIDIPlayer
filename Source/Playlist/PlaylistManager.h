#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

/**
    播放列表管理器，负责 MIDI 文件列表、播放模式和 JSON 持久化。
*/
class PlaylistManager {
public:
  struct Track {
    juce::File file;
    juce::String name;
    double durationSeconds = 0.0;
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

  /**
      检查文件是否已存在于播放列表。
  */
  bool contains(const juce::File &file) const {
    for (const auto &t : tracks) {
      if (t.file == file)
        return true;
    }
    return false;
  }

  /**
      添加 MIDI 文件。allowDuplicates 为 true 时允许重复路径。
  */
  bool addFile(const juce::File &file, bool allowDuplicates = false) {
    if (!file.existsAsFile())
      return false;

    if (!file.hasFileExtension(".mid;.midi"))
      return false;

    if (!allowDuplicates && contains(file)) {
      return false;
    }

    tracks.add({file, file.getFileNameWithoutExtension(), 0.0});
    changeLog.added++;
    return true;
  }

  /**
      按索引移除曲目。
  */
  bool removeTrack(int index) {
    if (index < 0 || index >= tracks.size())
      return false;

    tracks.remove(index);
    changeLog.removed++;
    return true;
  }

  /**
      移动曲目位置。
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
      按索引安全获取曲目；索引无效时返回 nullptr。
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

  // --- JSON 持久化 ---

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

      changeLog.reset();
      return true;
    } catch (...) {
      return false;
    }
  }

  /**
      从 JSON 文件加载播放列表。

      不存在的曲目会被跳过；加载完成后清空 ChangeLog，
      避免把文件内容恢复过程计为新增曲目。
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

      changeLog.reset();
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
      - 单曲循环：保持当前索引不变。
      - 随机播放：尽量避免连续两次选中同一首。
      - 列表循环：索引递增并对总数取模。
      - 顺序播放：到达末尾返回 -1 表示停止。
  */
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

  /**
      获取上一曲索引。

      上一曲不维护随机历史；Shuffle 与 Sequential 都按列表前一项处理。
  */
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
