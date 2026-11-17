#pragma once

#include "../Utils/DebugLogger.h"
#include "MusicalTimeline.h"
#include "MidiPedalChase.h"
#include "MidiControllerChase.h"
#include <array>
#include <atomic>
#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <memory>

// Responsibilities: 发布不可变 MIDI 序列，按采样位置生成事件并重建 seek 后的控制器状态。
// Concurrency: `setSequence`、`setSampleRate`、`seekTo` 只有一个串行生产方；
// 消息线程、命令线程与独占导出线程的生产权由 `Core` 协调。`processBlock` 同时只能有一个消费方。
// Ownership: 序列和 seek 消息归本对象持有；析构前必须停止生产方与实时/离线消费方。
class MidiPlayer {
public:
  enum class ProcessMode { realtime, offline };
  MidiPlayer() = default;

  ~MidiPlayer() {
    publishedSlot.store(-1, std::memory_order_release);
    audioActiveSlot.store(-1, std::memory_order_release);
    for (auto &slot : sequenceSlots)
      slot.snapshot.reset();
  }

  // Preconditions: `newSequence` 事件时间戳为秒；空指针表示清空序列。
  // Postconditions: 生产方创建采样时间快照并发布，后续消费块切换到新序列；播放状态归为暂停。
  // Failures: 槽位不可用时记录日志并保留旧快照；此 void 接口不报告发布失败。
  void setSequence(std::unique_ptr<juce::MidiMessageSequence> newSequence,
                   double newSampleRate) {
    const double rate = sanitiseSampleRate(newSampleRate);
    auto snapshot = createSnapshot(std::move(newSequence), rate, 0.0);
    publishSnapshot(std::move(snapshot), false);
  }

  // Preconditions: 与其他序列生产操作串行；`newSampleRate` 单位为 Hz。
  // Postconditions: 有序列且采样率变化时按时间位置重新发布快照，保留播放状态。
  void setSampleRate(double newSampleRate) {
    const double rate = sanitiseSampleRate(newSampleRate);
    const double oldRate = cachedSampleRate.load();
    if (!hasSequence() || std::abs(rate - oldRate) < 0.01)
      return;

    auto *current = getMessageThreadSnapshot();
    if (current == nullptr)
      return;

    auto seconds = std::make_unique<juce::MidiMessageSequence>(
        current->sequenceSeconds);
    const double positionSeconds = getPositionInSamples() / oldRate;
    auto snapshot =
        createSnapshot(std::move(seconds), rate, positionSeconds * rate);
    publishSnapshot(std::move(snapshot), true);
  }

  // Preconditions: 唯一消费方调用；`buffer` 由调用方持有，本方法追加事件且不负责预先清空。
  // Postconditions: 追加本块事件并推进采样位置；先消费快照和 seek，再处理普通序列事件。
  void processBlock(juce::MidiBuffer &buffer, int numSamples,
                    ProcessMode mode = ProcessMode::realtime) {
    if (numSamples <= 0)
      return;
    // Ordering: 先取得定位请求，再消费序列；请求发布前的序列写入对本消费块可见。
    const bool hasSeekRequest = takeLatestSeekRequest();
    applyPublishedSequence();

    // Ordering: 停止清理由 `AudioEngine` 淡出后触发；seek 清理块由引擎静音并接续淡入。
    // Ordering: 过期的暂停清理不能覆盖已恢复的踏板；切曲清理由消费方交接序列时产生。
    const bool stopCleanup = pendingStopCleanup.exchange(false) && !getPlaying();
    if (pendingAllNotesOff.exchange(false) || stopCleanup) {
      recoveryMessages.clear();
      recoveryIndex = 0;
      recoveryIsSeek = false;
      addStateResetMessages(recoveryMessages, resetControllersOnCleanup);
      resetControllersOnCleanup = false;
    }

    const bool appliedSeek = hasSeekRequest && applyPendingSeekRequest();
    renderPosition = playbackPosition;
    renderCommand = transportCommand.load(std::memory_order_acquire);
    renderPlaying = isPlaying(renderCommand) &&
                    renderCommand.positionRevision == playbackPositionRevision;
    const bool recovered = writeRecoveryMessages(buffer, numSamples, mode);
    // Ordering: 实时定位留出清理块；离线定位在同块开始正常序列，不写入准备空块。
    if ((appliedSeek || recovered) && mode == ProcessMode::realtime)
      return;

    auto *currentSequence = getAudioThreadSnapshot();
    if (currentSequence == nullptr || !renderPlaying)
      return;

    const double endPosition = renderPosition + numSamples;
    const auto &events = currentSequence->sequenceSamples;

    while (nextMessageIndex < events.getNumEvents()) {
      const auto &message = events.getEventPointer(nextMessageIndex)->message;
      const double eventSample = message.getTimeStamp();
      if (eventSample >= endPosition)
        break;

      if (!message.isMetaEvent())
        addTrackedEvent(buffer, message,
                        static_cast<int>(juce::jmax(0.0, eventSample - renderPosition)));

      ++nextMessageIndex;
    }

    if (!sequenceEnded && nextMessageIndex == events.getNumEvents()) {
      // Ordering: 释放比末个事件晚至少 1 个采样，避免同采样的 Note Off 抢在 Note On 之前生效。
      const double releaseOffset = juce::jmax(
          0.0, std::floor(currentSequence->durationSamples - renderPosition) + 1.0);
      if (releaseOffset < numSamples) {
        releaseHeldState(buffer, static_cast<int>(releaseOffset));
        sequenceEnded = true;
      }
    }

    playbackPosition = endPosition;
    renderedPosition.store(endPosition, std::memory_order_release);
  }

  // Postconditions: 原子改变播放标记；无序列时拒绝开始，停止不重置采样位置。
  void setPlaying(bool play) {
    if (play && !hasSequence())
      return;
    auto previous = transportCommand.load();
    auto next = previous;
    do {
      next = previous;
      next.playback = nextPlayback(previous, play ? Playback::playing : Playback::paused);
    } while (!transportCommand.compare_exchange_weak(previous, next));
  }

  bool getPlaying() const { return isPlaying(transportCommand.load()); }

  // Preconditions: 序列生产方调用，位置单位为当前序列采样；与快照替换串行。
  // Postconditions: 发布限定在序列范围内的位置；新请求替换尚未消费的旧请求，没有容量拒绝。
  // Failures: 无序列时无操作；实际定位在消费方后续块执行，旧序列代次的请求失效。
  void seekTo(double positionInSamples, bool forceChaseWhilePaused = false) {
    auto *currentSequence = getMessageThreadSnapshot();
    if (currentSequence == nullptr)
      return;

    SeekRequest request;
    request.position =
        juce::jlimit(0.0, cachedDurationSamples.load(), positionInSamples);
    request.generation = currentSequence->generation;
    request.index =
        findEventIndex(currentSequence->sequenceSamples, request.position);
    request.emitChase = getPlaying() || forceChaseWhilePaused;

    if (request.emitChase) {
      appendChasedControllers(currentSequence->sequenceSamples, request.index,
                              request.chaseMessages);
      // Ordering: 音色/控制器恢复完成后再恢复踏板，避免 VST3 的复位参数覆盖踏板值。
      juce::MidiBuffer pedals;
      appendChasedPedals(currentSequence->sequenceSamples, request.index, pedals);
      request.chaseMessages.addEvents(pedals, 0, -1, 1);
    }

    request.positionRevision = publishPositionIntent(request.position, true);
    pendingSeekRequests[(size_t)producerSeekSlot] = std::move(request);
    producerSeekSlot = publishedSeekSlot.exchange(
                           producerSeekSlot | seekRequestReady,
                           std::memory_order_acq_rel) & seekSlotMask;
  }

  // Postconditions: `hasFinished` 一次性消费当前播放命令的完成标记。
  bool hasFinished() {
    auto command = transportCommand.load();
    return playbackOf(command) == Playback::finished &&
           transportCommand.compare_exchange_strong(
               command, {command.positionRevision, nextPlayback(command, Playback::paused)});
  }

  // Preconditions: 由渲染消费方在处理当前音频块后查询。
  bool isWaitingForTail() const {
    return sequenceEnded && renderPlaying && transportCommand.load() == renderCommand;
  }

  // Preconditions: 仅消费方在尾音达到结束条件后调用；将结束状态转为待核心消费的完成事件。
  bool finishTail() {
    auto command = renderCommand;
    return sequenceEnded && renderPlaying &&
           transportCommand.compare_exchange_strong(
               command, {command.positionRevision, nextPlayback(command, Playback::finished)});
  }

  // Postconditions: 定位命令尚未被渲染消费时返回已请求的位置。
  double getPositionInSamples() const {
    const auto revision = transportCommand.load().positionRevision;
    return renderedPositionRevision.load(std::memory_order_acquire) == revision
               ? renderedPosition.load(std::memory_order_acquire)
               : requestedPosition.load(std::memory_order_acquire);
  }

  double getDurationInSamples() const {
    return cachedDurationSamples.load();
  }

  double getSequenceSampleRate() const { return cachedSampleRate.load(); }

  bool hasSequence() const { return sequenceLoaded.load(); }

  // Preconditions: 唯一消费方在 `processBlock` 后查询，偏移属于刚生成的块。
  MusicalPosition getMusicalPosition(int offset = 0) const {
    const auto *snapshot = getAudioThreadSnapshot();
    return snapshot != nullptr
               ? snapshot->timeline.at(renderPosition + (renderPlaying ? offset : 0),
                                       snapshot->sampleRate, renderPlaying)
               : MusicalPosition{};
  }

  // Preconditions: 实时消费已停止，由独占离线会话调用；下一块从文件开头重建 MIDI。
  void resetForOfflineRender(double rate) {
    setPlaying(false);
    setSampleRate(rate);
    seekTo(0.0);
    pendingAllNotesOff.store(true);
    pendingStopCleanup.store(false);
    activeNotes = {};
    activePedals = {};
    recoveryMessages.clear();
    recoveryIndex = 0;
    resetControllersOnCleanup = true;
    seekOccurred.store(false);
    cleanupOccurred.store(false);
  }

  // Ordering: seek 实际应用后返回一次 `true`；只由引擎消费，用于清理块静音及后续淡入。
  bool consumeSeekOccurred() {
    return seekOccurred.exchange(false, std::memory_order_acquire);
  }

  // Ordering: 清理事件已写入当前块后返回一次 `true`；引擎据此静音当前块。
  bool consumeCleanupOccurred() {
    return cleanupOccurred.exchange(false, std::memory_order_acquire);
  }

  // Preconditions: 引擎已完成停止淡出；下一次 `processBlock` 写入音符和踏板释放事件。
  void triggerStopCleanup() {
    pendingStopCleanup.store(true);
  }

private:
  struct SequenceSnapshot {
    MusicalTimeline timeline;
    juce::MidiMessageSequence sequenceSeconds;
    juce::MidiMessageSequence sequenceSamples;
    double sampleRate = 44100.0;
    double durationSamples = 0.0;
    double initialPositionSamples = 0.0;
    int initialMessageIndex = 0;
    uint32_t generation = 0;
  };

  struct SeekRequest {
    double position = 0.0;
    int index = 0;
    uint32_t generation = 0;
    uint32_t positionRevision = 0;
    bool emitChase = false;
    juce::MidiBuffer chaseMessages;
  };

  static constexpr int seekRequestReady = 4;
  static constexpr int seekSlotMask = 3;
  static constexpr int sequenceSlotCount = 4;

  enum class SlotState : uint8_t {
    Free,
    Writing,
    Ready,
    Active,
    Retired
  };

  struct SequenceSlot {
    std::unique_ptr<SequenceSnapshot> snapshot;
    uint32_t positionRevision = 0;
    std::atomic<SlotState> state{SlotState::Free};
  };

  // Concurrency: 生产方独占创建/回收 `SequenceSnapshot`，消费方只切换槽位并读取快照。
  // Invariant: 只有 `Retired` 槽位可回收；消费阶段不创建或销毁序列快照。
  std::array<SequenceSlot, sequenceSlotCount> sequenceSlots;
  std::atomic<int> publishedSlot{-1};
  std::atomic<int> audioActiveSlot{-1};
  // Ownership: 生产方记录最新序列，避免消费方交接槽位期间读到上一代序列。
  int latestSequenceSlot = -1;

  enum class Playback : uint32_t { paused, playing, finished };
  struct TransportCommand {
    uint32_t positionRevision = 0;
    uint32_t playback = 0;

    bool operator==(const TransportCommand &other) const {
      return positionRevision == other.positionRevision && playback == other.playback;
    }
  };

  static constexpr uint32_t playbackMask = 3;
  static Playback playbackOf(TransportCommand command) {
    return static_cast<Playback>(command.playback & playbackMask);
  }
  static bool isPlaying(TransportCommand command) {
    return playbackOf(command) == Playback::playing;
  }
  static uint32_t nextPlayback(TransportCommand command, Playback playback) {
    return ((command.playback & ~playbackMask) + playbackMask + 1) |
           static_cast<uint32_t>(playback);
  }

  // Invariant: 尾音完成只提交到渲染块对应的播放命令；旧块不能覆盖新定位或播放意图。
  std::atomic<TransportCommand> transportCommand{};
  std::atomic<bool> sequenceLoaded{false};
  std::atomic<bool> pendingAllNotesOff{false};
  std::atomic<bool> pendingStopCleanup{false};
  std::atomic<bool> seekOccurred{false};
  std::atomic<bool> cleanupOccurred{false};

  // Concurrency: 三个槽位分别归生产方、消费方和原子交换位置所有；双方只写自己的槽位。
  // Ordering: exchange 交接槽位所有权；覆盖旧请求及回收 MIDI 消息只发生在生产方。
  // Invariant: 最新请求替换尚未消费的请求；消费方只应用当前序列的 generation。
  std::array<SeekRequest, 3> pendingSeekRequests;
  int producerSeekSlot = 0;
  int consumerSeekSlot = 1;
  std::atomic<int> publishedSeekSlot{2};

  // Ownership: 生产方持有请求位置，消费方持有渲染游标。仅在渲染确认同一代次后读取其位置。
  std::atomic<double> requestedPosition{0.0};
  uint32_t nextPositionRevision = 0;
  std::atomic<double> renderedPosition{0.0};
  std::atomic<uint32_t> renderedPositionRevision{0};
  std::atomic<double> cachedDurationSamples{0.0};
  std::atomic<double> cachedSampleRate{44100.0};
  std::atomic<uint32_t> nextSequenceGeneration{1};
  int nextMessageIndex = 0;
  double playbackPosition = 0.0;
  uint32_t playbackPositionRevision = 0;
  double renderPosition = 0.0;
  TransportCommand renderCommand;
  bool renderPlaying = false;
  bool sequenceEnded = false;
  bool resetControllersOnCleanup = false;
  juce::MidiBuffer recoveryMessages;
  int recoveryIndex = 0;
  bool recoveryIsSeek = false;
  std::array<std::array<bool, 128>, 16> activeNotes{};
  static constexpr std::array<int, 3> pedalControllers{64, 66, 67};
  std::array<std::array<bool, pedalControllers.size()>, 16> activePedals{};

  uint32_t publishPositionIntent(double position, bool preservePlayback) {
    requestedPosition.store(position, std::memory_order_release);
    const auto revision = ++nextPositionRevision;
    auto previous = transportCommand.load();
    auto next = previous;
    do {
      const auto playback = preservePlayback && isPlaying(previous)
                                ? Playback::playing : Playback::paused;
      next = {revision, nextPlayback(previous, playback)};
    } while (!transportCommand.compare_exchange_weak(previous, next));
    return revision;
  }

  void setRenderCursor(double position, uint32_t revision) {
    playbackPosition = position;
    playbackPositionRevision = revision;
    renderedPosition.store(position, std::memory_order_relaxed);
    renderedPositionRevision.store(revision, std::memory_order_release);
    sequenceEnded = false;
  }

  static double sanitiseSampleRate(double rate) {
    return rate > 0.0 ? rate : 44100.0;
  }

  static int findEventIndex(const juce::MidiMessageSequence &sequence,
                            double position) {
    int low = 0;
    int high = sequence.getNumEvents();
    while (low < high) {
      const int mid = low + (high - low) / 2;
      auto *event = sequence.getEventPointer(mid);
      if (event->message.getTimeStamp() < position)
        low = mid + 1;
      else
        high = mid;
    }
    return low;
  }

  std::unique_ptr<SequenceSnapshot>
  createSnapshot(std::unique_ptr<juce::MidiMessageSequence> seconds,
                 double rate, double initialPositionSamples) {
    if (seconds == nullptr)
      return {};

    auto snapshot = std::make_unique<SequenceSnapshot>();
    snapshot->sequenceSeconds = std::move(*seconds);
    snapshot->sequenceSeconds.sort();
    snapshot->sequenceSeconds.updateMatchedPairs();
    snapshot->timeline.build(snapshot->sequenceSeconds);
    snapshot->sequenceSamples = snapshot->sequenceSeconds;

    for (int i = 0; i < snapshot->sequenceSamples.getNumEvents(); ++i) {
      auto *event = snapshot->sequenceSamples.getEventPointer(i);
      event->message.setTimeStamp(event->message.getTimeStamp() * rate);
    }

    snapshot->sampleRate = rate;
    snapshot->durationSamples = snapshot->sequenceSamples.getEndTime();
    snapshot->initialPositionSamples =
        juce::jlimit(0.0, snapshot->durationSamples, initialPositionSamples);
    snapshot->initialMessageIndex = findEventIndex(
        snapshot->sequenceSamples, snapshot->initialPositionSamples);
    snapshot->generation = nextSequenceGeneration.fetch_add(1);
    return snapshot;
  }

  void publishSnapshot(std::unique_ptr<SequenceSnapshot> snapshot,
                       bool preservePlayingState) {
    reclaimRetiredSequences();

    int targetSlot = -1;
    for (int i = 0; i < sequenceSlotCount; ++i) {
      auto expected = SlotState::Free;
      if (sequenceSlots[(size_t)i].state.compare_exchange_strong(
              expected, SlotState::Writing, std::memory_order_acq_rel)) {
        targetSlot = i;
        break;
      }
    }

    if (targetSlot < 0) {
      LOG_DEBUG("MidiPlayer::publishSnapshot - no free sequence slot");
      return;
    }

    const auto *raw = snapshot.get();
    auto &slot = sequenceSlots[(size_t)targetSlot];
    slot.positionRevision = publishPositionIntent(
        raw != nullptr ? raw->initialPositionSamples : 0.0, preservePlayingState);
    slot.snapshot = std::move(snapshot);
    sequenceLoaded.store(raw != nullptr);
    cachedDurationSamples.store(raw != nullptr ? raw->durationSamples : 0.0);
    cachedSampleRate.store(raw != nullptr ? raw->sampleRate : 44100.0);
    slot.state.store(SlotState::Ready, std::memory_order_release);

    const int replacedPending =
        publishedSlot.exchange(targetSlot, std::memory_order_acq_rel);
    latestSequenceSlot = targetSlot;
    if (replacedPending >= 0 && replacedPending != targetSlot) {
      auto &replaced = sequenceSlots[(size_t)replacedPending];
      auto expected = SlotState::Ready;
      if (replaced.state.compare_exchange_strong(
              expected, SlotState::Writing, std::memory_order_acq_rel)) {
        replaced.snapshot.reset();
        replaced.state.store(SlotState::Free, std::memory_order_release);
      }
    }
  }

  void reclaimRetiredSequences() {
    for (auto &slot : sequenceSlots) {
      auto expected = SlotState::Retired;
      if (slot.state.compare_exchange_strong(
              expected, SlotState::Writing, std::memory_order_acq_rel)) {
        slot.snapshot.reset();
        slot.state.store(SlotState::Free, std::memory_order_release);
      }
    }
  }

  SequenceSnapshot *getMessageThreadSnapshot() const {
    return latestSequenceSlot >= 0
               ? sequenceSlots[(size_t)latestSequenceSlot].snapshot.get()
               : nullptr;
  }

  SequenceSnapshot *getAudioThreadSnapshot() const {
    const int slotIndex = audioActiveSlot.load(std::memory_order_relaxed);
    return slotIndex >= 0
               ? sequenceSlots[(size_t)slotIndex].snapshot.get()
               : nullptr;
  }

  void applyPublishedSequence() {
    // Ordering: 消费方将 `Ready` 改为 `Active` 并替换活动槽位，再将旧槽位置为 `Retired`。
    const int nextSlot = publishedSlot.exchange(-1, std::memory_order_acq_rel);
    if (nextSlot < 0)
      return;

    auto &next = sequenceSlots[(size_t)nextSlot];
    auto expected = SlotState::Ready;
    if (!next.state.compare_exchange_strong(
            expected, SlotState::Active, std::memory_order_acq_rel))
      return;

    const int oldSlot =
        audioActiveSlot.exchange(nextSlot, std::memory_order_acq_rel);
    if (oldSlot >= 0 && oldSlot != nextSlot)
      sequenceSlots[(size_t)oldSlot].state.store(SlotState::Retired,
                                                 std::memory_order_release);

    // Ordering: 清理与新序列在同一消费块交接，禁止旧序列的延迟清理进入新曲目。
    if (oldSlot >= 0) {
      pendingAllNotesOff.store(true);
      seekOccurred.store(true, std::memory_order_release);
    }

    auto *active = next.snapshot.get();
    nextMessageIndex = active != nullptr ? active->initialMessageIndex : 0;
    setRenderCursor(active != nullptr ? active->initialPositionSamples : 0.0,
                    next.positionRevision);
  }

  void addTrackedEvent(juce::MidiBuffer &buffer,
                       const juce::MidiMessage &message, int sampleOffset) {
    buffer.addEvent(message, sampleOffset);

    const int channel = message.getChannel() - 1;
    if (channel < 0)
      return;

    auto &notes = activeNotes[static_cast<size_t>(channel)];
    if (message.isNoteOnOrOff()) {
      notes[static_cast<size_t>(message.getNoteNumber())] = message.isNoteOn();
    } else if (message.isController()) {
      const int controller = message.getControllerNumber();
      auto &pedals = activePedals[static_cast<size_t>(channel)];
      if (controller == 121)
        pedals.fill(false);
      else if (controller == 120 || controller >= 123)
        notes.fill(false);
      else {
        for (size_t index = 0; index < pedalControllers.size(); ++index)
          if (pedalControllers[index] == controller)
            pedals[index] = message.getControllerValue() != 0;
      }
    }
  }

  void releaseHeldState(juce::MidiBuffer &buffer, int offset) {
    for (size_t channel = 0; channel < activeNotes.size(); ++channel) {
      const int midiChannel = static_cast<int>(channel) + 1;
      const auto &notes = activeNotes[channel];
      for (size_t note = 0; note < notes.size(); ++note)
        if (notes[note])
          addTrackedEvent(buffer, juce::MidiMessage::noteOff(midiChannel, static_cast<int>(note)),
                          offset);

      const auto &pedals = activePedals[channel];
      for (size_t index = 0; index < pedals.size(); ++index)
        if (pedals[index])
          addTrackedEvent(buffer, juce::MidiMessage::controllerEvent(
                                      midiChannel, pedalControllers[index], 0), offset);
    }
  }

  void addStateResetMessages(juce::MidiBuffer &buffer,
                             bool resetControllers) {
    for (int channel = 1; channel <= 16; ++channel) {
      for (int controller : pedalControllers)
        buffer.addEvent(
            juce::MidiMessage::controllerEvent(channel, controller, 0), 0);

      auto &channelNotes = activeNotes[(size_t)(channel - 1)];
      for (int note = 0; note < 128; ++note) {
        if (!channelNotes[(size_t)note])
          continue;

        buffer.addEvent(juce::MidiMessage::noteOff(channel, note), 0);
        // Ownership: 按键状态由 addTrackedEvent 在实际发送时更新，分包取消不能提前遗失状态。
      }

      // AudioEngine 会静音包含这些释放事件的音频块，避免声部切断瞬态。
      buffer.addEvent(juce::MidiMessage::allSoundOff(channel), 0);
      if (resetControllers)
        buffer.addEvent(juce::MidiMessage::allControllersOff(channel), 0);
    }
  }

  bool takeLatestSeekRequest() {
    if ((publishedSeekSlot.load(std::memory_order_acquire) & seekRequestReady) == 0)
      return false;
    consumerSeekSlot = publishedSeekSlot.exchange(
                          consumerSeekSlot, std::memory_order_acq_rel) &
                      seekSlotMask;
    return true;
  }

  // Preconditions: 本消费块已取得定位槽位，并消费了此前发布的序列。
  bool applyPendingSeekRequest() {
    auto *active = getAudioThreadSnapshot();
    const auto &request = pendingSeekRequests[(size_t)consumerSeekSlot];
    if (active == nullptr || request.generation != active->generation)
      return false;

    setRenderCursor(request.position, request.positionRevision);
    nextMessageIndex = request.index;

    if (request.emitChase) {
      recoveryMessages.clear();
      recoveryIndex = 0;
      recoveryIsSeek = true;
      addStateResetMessages(recoveryMessages, true);
      for (const auto metadata : request.chaseMessages)
        recoveryMessages.addEvent(metadata.data, metadata.numBytes,
                                  metadata.samplePosition + 1);
    } else if (recoveryIsSeek) {
      // Ordering: 暂停定位取消尚未发送的恢复消息，暂停清理仍由引擎淡出触发。
      recoveryMessages.clear();
      recoveryIndex = 0;
      recoveryIsSeek = false;
    }
    return true;
  }

  // Ordering: 实时恢复的清理、控制器和踏板分别占用处理块，块内从采样 0 生效。
  // Reason: VST3 将各 CC 转为独立参数队列，同块内的复位与踏板不能依赖 MIDI 排序。
  bool writeRecoveryMessages(juce::MidiBuffer &buffer, int numSamples, ProcessMode mode) {
    int index = 0;
    int phase = -1;
    bool emitted = false, complete = true;
    for (const auto metadata : recoveryMessages) {
      if (index++ < recoveryIndex)
        continue;
      if (mode == ProcessMode::realtime && phase >= 0 && metadata.samplePosition != phase) {
        complete = false;
        break;
      }
      phase = metadata.samplePosition;
      addTrackedEvent(buffer, metadata.getMessage(),
                      mode == ProcessMode::realtime ? 0
                          : juce::jmin(metadata.samplePosition, numSamples - 1));
      ++recoveryIndex;
      emitted = true;
    }
    if (complete) {
      recoveryMessages.clear();
      recoveryIndex = 0;
    }
    if (emitted) {
      if (recoveryIsSeek)
        seekOccurred.store(true, std::memory_order_release);
      else
        cleanupOccurred.store(true, std::memory_order_release);
    }
    return emitted;
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiPlayer)
};
