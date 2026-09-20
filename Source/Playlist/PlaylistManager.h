#pragma once

#include "PlaybackMode.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Responsibilities: 曲目顺序、可用性、未保存变更及 JSON 持久化；不控制音频播放。
// Ownership: 持有曲目记录；返回的指针/引用只在下一次列表修改前有效。
// Concurrency: 调用方串行化所有访问；应用运行时由 `Core::stateMutex` 提供保护。
class PlaylistManager {
public:
  struct Track {
    juce::File file;
    juce::String name;
    double durationSeconds = 0.0;
    bool available = false;
  };

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
  // Postconditions: 接受后追加记录并更新变更计数；拒绝文件或重复项返回 `false`。
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

  // Postconditions: 接受的删除/移动更新变更摘要；无效索引或原位移动返回 `false`。
  // 播放索引和 MIDI 清理由 `Core` 同步处理，本模型不持有这些状态。
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

  // Ownership: 查询返回借用列表或元素指针，禁止跨列表修改保存它们；无效元素索引返回 nullptr。
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

  // Side effect: 重新检查文件存在性并更新记录；返回 `false` 仍可能已将条目标记为不可用。
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

  // Preconditions: 调用方独占列表，目标目录可创建/写入；路径中的中文使用 JUCE 文件接口处理。
  // Ordering: JSON 完整写入并关闭临时文件后替换目标；成功后才清除变更摘要。
  // Failures: `saveDetailed` 返回具体诊断，`save` 只返回布尔结果。
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

      changeLog.reset();
      return juce::Result::ok();
    } catch (...) {
      return juce::Result::fail("Unexpected exception while saving playlist: " +
                                file.getFullPathName());
    }
  }

  bool save(const juce::File &file) const { return saveDetailed(file).wasOk(); }

  // Trust Boundary: 验证 JSON 根结构、曲目数组及全部条目类型后才替换内存列表；当前未校验版本字段。
  // Postconditions: 缺失文件保留原始路径/顺序并标记不可用；加载失败保留原列表。
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
      return juce::Result::fail(
          "Unexpected exception while loading playlist: " +
          file.getFullPathName());
    }
  }

  bool load(const juce::File &file) { return loadDetailed(file).wasOk(); }

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
  juce::Array<Track> tracks;
  midi::PlaybackMode currentMode = midi::PlaybackMode::Sequential;
  mutable ChangeLog changeLog; // Side effect: const 保存操作成功后也会清零变更摘要。

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
