#pragma once

#include "PlaybackMode.h"
#include "TrackId.h"

#include <juce_data_structures/juce_data_structures.h>
#include <optional>

// Responsibilities: 曲目顺序、可用性、未保存变更及 JSON 持久化；不控制音频播放。
// Ownership: 持有曲目记录；返回的指针/引用只在下一次列表修改前有效。
// Concurrency: 调用方串行化所有访问；应用运行时由 `Core::stateMutex` 提供保护。
class PlaylistManager {
public:
  struct Track {
    juce::File file;
    juce::String name;
    bool available = false;
    midi::TrackId id = 0;
  };
  using Tracks = juce::Array<Track>;

  struct ChangeLog {
    // Contract: 只表示尚未保存的增删/排序摘要，成功保存或加载后归零。
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

  // Postconditions: 按文件路径身份查询；不验证 MIDI 内容，未命中的索引查询返回 -1。
  bool contains(const juce::File &file) const {
    for (const auto &t : tracks) {
      if (t.file == file)
        return true;
    }
    return false;
  }

  // Preconditions: 输入本机文件；只校验存在性和 .mid/.midi 扩展名，内容解析由播放器执行。
  // Postconditions: 接受后追加记录并更新变更计数；拒绝时返回原因。
  juce::Result addFile(const juce::File &file, bool allowDuplicates = false) {
    if (!file.existsAsFile())
      return juce::Result::fail(L"文件不存在或不可用");

    if (!file.hasFileExtension(".mid;.midi"))
      return juce::Result::fail(L"仅支持 .mid 和 .midi 文件");

    if (!allowDuplicates && contains(file))
      return juce::Result::fail(L"文件已在播放列表中");

    tracks.add({file, file.getFileNameWithoutExtension(), true, ++nextTrackId});
    changeLog.added++;
    ++revision;
    return juce::Result::ok();
  }

  // Postconditions: 接受的删除/移动更新变更摘要；无效索引或原位移动返回 `false`。
  // 播放索引和 MIDI 清理由 `Core` 同步处理，本模型不持有这些状态。
  bool removeTrack(int index) {
    if (index < 0 || index >= tracks.size())
      return false;

    tracks.remove(index);
    changeLog.removed++;
    ++revision;
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
    ++revision;
    return true;
  }

  void clear() {
    if (!tracks.isEmpty()) {
      changeLog.removed += tracks.size();
      tracks.clear();
      ++revision;
    }
  }

  // Ownership: 查询返回借用列表或元素指针，禁止跨列表修改保存它们；无效元素索引返回 nullptr。
  const Tracks &getTracks() const { return tracks; }

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

  int findTrackIndex(midi::TrackId id) const {
    for (int index = 0; index < tracks.size(); ++index)
      if (tracks[index].id == id)
        return index;
    return -1;
  }

  std::optional<int> occurrenceOf(midi::TrackId id) const {
    const int row = findTrackIndex(id);
    if (row < 0)
      return std::nullopt;
    int occurrence = 0;
    for (int index = 0; index < row; ++index)
      if (tracks[index].file == tracks[row].file)
        ++occurrence;
    return occurrence;
  }

  // 没有旧绑定时只匹配唯一条目，避免将正在播放的文件绑定到任意重复行。
  midi::TrackId bindFile(const juce::File &file,
                         std::optional<int> occurrence) const {
    midi::TrackId match = 0;
    int index = 0;
    for (const auto &track : tracks) {
      if (track.file != file)
        continue;
      if (occurrence) {
        if (index++ == *occurrence)
          return track.id;
      } else {
        if (match != 0)
          return 0;
        match = track.id;
      }
    }
    return match;
  }

  // Side effect: 文件不可用时更新条目状态，并返回失败原因。
  juce::Result refreshTrack(int index) {
    if (index < 0 || index >= tracks.size())
      return juce::Result::fail(L"曲目已不在播放列表中");
    auto &t = tracks.getReference(index);
    t.available = t.file.existsAsFile();
    if (!t.available)
      return juce::Result::fail(L"文件不存在或不可用");
    t.name = t.file.getFileNameWithoutExtension();
    return juce::Result::ok();
  }

  // Ordering: JSON 完整写入并关闭临时文件后替换目标。
  static juce::Result write(const juce::File &file, const Tracks &snapshot) {
    try {
      juce::DynamicObject::Ptr root = new juce::DynamicObject();
      juce::Array<juce::var> trackList;

      for (const auto &t : snapshot)
        trackList.add(t.file.getFullPathName());

      root->setProperty("version", 1);
      root->setProperty("tracks", trackList);

      auto tempFile = file.getSiblingFile(file.getFileName() + ".tmp");
      if (tempFile.exists() && !tempFile.deleteFile())
        return juce::Result::fail(
            "Unable to delete stale playlist temp file: " +
            tempFile.getFullPathName());

      auto stream = tempFile.createOutputStream();
      if (stream == nullptr)
        return juce::Result::fail("Unable to open playlist for writing: " +
                                  tempFile.getFullPathName());

      if (!stream->setPosition(0) || stream->truncate().failed()) {
        const auto error = stream->getStatus().getErrorMessage();
        stream.reset();
        tempFile.deleteFile();
        return juce::Result::fail(
            "Unable to reset playlist temp file: " +
            (error.isNotEmpty() ? error : tempFile.getFullPathName()));
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

      return juce::Result::ok();
    } catch (...) {
      return juce::Result::fail("Unexpected exception while saving playlist: " +
                                file.getFullPathName());
    }
  }

  // Postconditions: 保留缺失文件和重复路径，解析完成后才交付曲目。
  static juce::Result read(const juce::File &file, Tracks &result) {
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

      Tracks loadedTracks;

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
                          trackFile.existsAsFile()});
      }

      result.swapWith(loadedTracks);
      return juce::Result::ok();
    } catch (...) {
      return juce::Result::fail(
          "Unexpected exception while loading playlist: " +
          file.getFullPathName());
    }
  }

  void replace(Tracks loadedTracks) {
    for (auto &track : loadedTracks)
      track.id = ++nextTrackId;
    tracks.swapWith(loadedTracks);
    ++revision;
    changeLog.reset();
  }

  uint64_t getRevision() const { return revision; }
  void markSaved(uint64_t savedRevision) {
    if (revision == savedRevision)
      changeLog.reset();
  }

  // Preconditions: `mode` 已由核心入口约束为有效持久化枚举；本模型不负责归一化或保存设置。
  void setPlaybackMode(midi::PlaybackMode mode) { currentMode = mode; }
  midi::PlaybackMode getPlaybackMode() const { return currentMode; }

  // Postconditions: 空列表或连续播放到末尾返回 -1；随机模式在多曲目时排除当前有效索引。
  int getNextIndex(int currentIndex) const {
    if (tracks.isEmpty())
      return -1;

    switch (currentMode) {
    case midi::PlaybackMode::LoopSingle:
      return juce::isPositiveAndBelow(currentIndex, tracks.size())
                 ? currentIndex
                 : 0;

    case midi::PlaybackMode::Shuffle: {
      if (tracks.size() == 1)
        return 0;
      if (!juce::isPositiveAndBelow(currentIndex, tracks.size()))
        return juce::Random::getSystemRandom().nextInt(tracks.size());
      const int next =
          juce::Random::getSystemRandom().nextInt(tracks.size() - 1);
      return next >= currentIndex ? next + 1 : next;
    }

    case midi::PlaybackMode::LoopList:
      return (currentIndex + 1) % tracks.size();

    case midi::PlaybackMode::Sequential:
    default:
      if (currentIndex >= tracks.size() - 1)
        return -1;
      return currentIndex + 1;
    }
  }

  // Contract: 上一曲不维护随机历史；`Shuffle` 与 `Sequential` 按列表前一项处理。
  int getPreviousIndex(int currentIndex) const {
    if (tracks.isEmpty())
      return -1;

    if (currentMode == midi::PlaybackMode::LoopList ||
        currentMode == midi::PlaybackMode::LoopSingle) {
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
  Tracks tracks;
  midi::PlaybackMode currentMode = midi::PlaybackMode::Sequential;
  ChangeLog changeLog;
  midi::TrackId nextTrackId = 0;
  uint64_t revision = 0;

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

    return details.joinIntoString("\n");
  }
};
