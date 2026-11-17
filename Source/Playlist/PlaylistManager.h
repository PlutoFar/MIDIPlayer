#pragma once

#include "PlaybackMode.h"
#include "TrackId.h"
#include "../Utils/AtomicTextFile.h"

#include <juce_data_structures/juce_data_structures.h>
#include <optional>
#include <vector>

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
    uint64_t availabilityObservation = 0;
  };
  using Tracks = juce::Array<Track>;
  enum class Advance { next, previous, natural };

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
    ++availableTrackCount;
    changeLog.added++;
    ++revision;
    return juce::Result::ok();
  }

  // Postconditions: 接受的删除/移动更新变更摘要；无效索引或原位移动返回 `false`。
  // 播放索引和 MIDI 清理由 `Core` 同步处理，本模型不持有这些状态。
  bool removeTrack(int index) {
    if (index < 0 || index >= tracks.size())
      return false;

    availableTrackCount -= tracks.getReference(index).available;
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
      availableTrackCount = 0;
      ++revision;
    }
  }

  // Ownership: 查询返回借用列表或元素指针，禁止跨列表修改保存它们；无效元素索引返回 nullptr。
  const Tracks &getTracks() const { return tracks; }

  int size() const { return tracks.size(); }
  bool isEmpty() const { return tracks.isEmpty(); }
  bool hasAvailableTracks() const { return availableTrackCount > 0; }
  uint64_t getAvailabilityRevision() const { return availabilityRevision; }

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
    setTrackAvailable(index, t.file.existsAsFile());
    if (!t.available)
      return juce::Result::fail(L"文件不存在或不可用");
    t.name = t.file.getFileNameWithoutExtension();
    return juce::Result::ok();
  }

  void setTrackAvailable(int index, bool available) {
    auto &track = tracks.getReference(index);
    ++track.availabilityObservation;
    if (track.available == available)
      return;
    track.available = available;
    availableTrackCount += available ? 1 : -1;
    ++availabilityRevision;
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

      return midi::writeTextFileAtomically(
          file, juce::JSON::toString(juce::var(root.get()), true));
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
    availableTrackCount = 0;
    for (auto &track : loadedTracks) {
      track.id = ++nextTrackId;
      availableTrackCount += track.available;
    }
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

  // Contract: 每个候选索引只出现一次。真实文件可用性由加载方判断。
  std::vector<int> playbackCandidates(int currentIndex, Advance advance) const {
    using midi::PlaybackMode;
    const int count = tracks.size();
    const bool hasCurrent = juce::isPositiveAndBelow(currentIndex, count);
    if (currentMode == PlaybackMode::LoopSingle && advance == Advance::natural && hasCurrent)
      return {currentIndex};

    const bool backwards = advance == Advance::previous;
    std::vector<int> candidates;
    candidates.reserve(static_cast<size_t>(count));
    if (currentMode == PlaybackMode::Shuffle && !backwards) {
      for (int index = 0; index < count; ++index)
        if (index != currentIndex)
          candidates.push_back(index);
      auto &random = juce::Random::getSystemRandom();
      for (int remaining = static_cast<int>(candidates.size()); remaining > 1; --remaining)
        std::swap(candidates[remaining - 1], candidates[random.nextInt(remaining)]);
      if (hasCurrent)
        candidates.push_back(currentIndex);
      return candidates;
    }

    // 随机模式的上一首仍按列表顺序。单曲循环的下一首先尝试重播当前曲目。
    const bool wrap = currentMode == PlaybackMode::LoopList ||
                      currentMode == PlaybackMode::LoopSingle;
    const int step = backwards ? -1 : 1;
    int index = hasCurrent ? currentIndex + step : (backwards ? -1 : 0);
    if (currentMode == PlaybackMode::LoopSingle && !backwards && hasCurrent)
      index = currentIndex;
    for (int visited = 0; visited < count; ++visited) {
      if (!juce::isPositiveAndBelow(index, count)) {
        if (!wrap)
          break;
        index = backwards ? count - 1 : 0;
      }
      candidates.push_back(index);
      index += step;
    }
    return candidates;
  }

private:
  Tracks tracks;
  midi::PlaybackMode currentMode = midi::PlaybackMode::Sequential;
  ChangeLog changeLog;
  midi::TrackId nextTrackId = 0;
  uint64_t revision = 0;
  uint64_t availabilityRevision = 0;
  int availableTrackCount = 0;

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
