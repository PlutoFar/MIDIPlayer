#pragma once

#include "../Utils/DebugLogger.h"
#include <array>
#include <atomic>
#include <cmath>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <memory>

class MidiPlayer {
public:
  MidiPlayer() = default;

  ~MidiPlayer() {
    publishedSlot.store(-1, std::memory_order_release);
    audioActiveSlot.store(-1, std::memory_order_release);
    for (auto &slot : sequenceSlots)
      slot.snapshot.reset();
  }

  // newSequence 的时间戳必须是秒。快照分配、时间戳换算和旧快照回收都留在
  // 消息线程完成，音频线程只读取已发布的不可变快照。
  void setSequence(std::unique_ptr<juce::MidiMessageSequence> newSequence,
                   double newSampleRate) {
    if (hasSequence()) {
      pendingAllNotesOff.store(true);
      seekOccurred.store(true, std::memory_order_release);
    }

    const double rate = sanitiseSampleRate(newSampleRate);
    auto snapshot = createSnapshot(std::move(newSequence), rate, 0.0);
    publishSnapshot(std::move(snapshot), false);
  }

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
    const double positionSeconds =
        oldRate > 0.0 ? currentPositionInSamples.load() / oldRate : 0.0;
    auto snapshot =
        createSnapshot(std::move(seconds), rate, positionSeconds * rate);
    publishSnapshot(std::move(snapshot), true);
  }

  void processBlock(juce::MidiBuffer &buffer, int numSamples) {
    applyPublishedSequence();

    // 清理消息只在音频已静音时发送：停止播放由 AudioEngine 淡出后触发，
    // 切曲由 setSequence 搭配交叉淡入静音块触发。
    if (pendingAllNotesOff.exchange(false)) {
      addStateResetMessages(buffer, false);
      cleanupOccurred.store(true, std::memory_order_release);
    }

    if (applyPendingSeekRequest(buffer))
      return;

    auto *currentSequence = getAudioThreadSnapshot();
    if (currentSequence == nullptr || !isPlaying.load() || numSamples <= 0)
      return;
    if (sequenceEnded.load(std::memory_order_acquire))
      return;

    const double currentPos = currentPositionInSamples.load();
    const double endPosition = currentPos + numSamples;
    const auto &events = currentSequence->sequenceSamples;

    while (nextMessageIndex < events.getNumEvents()) {
      auto *event = events.getEventPointer(nextMessageIndex);
      if (event == nullptr)
        break;

      const double eventSample = event->message.getTimeStamp();
      if (eventSample >= endPosition)
        break;

      if (!event->message.isMetaEvent()) {
        int offset = juce::jmax(0, (int)(eventSample - currentPos));
        offset = juce::jmin(offset, numSamples - 1);
        addTrackedEvent(buffer, event->message, offset);
      }

      ++nextMessageIndex;
    }

    currentPositionInSamples.store(endPosition);

    if (nextMessageIndex >= events.getNumEvents())
      sequenceEnded.store(true, std::memory_order_release);
  }

  void setPlaying(bool play) {
    if (play && !hasSequence())
      return;
    // 暂停只停止序列推进，不重置 MIDI；AudioEngine 淡出输出后触发 MIDI 状态清理。
    // 恢复播放前 UI 应调用 seekTo(currentPos, true)，
    // 用 MIDI chase 重建 CC、Program、PitchWheel 和活跃音符状态。
    if (!play)
      sequenceEnded.store(false, std::memory_order_release);
    else
      finishedFlag.store(false, std::memory_order_release);
    isPlaying.store(play);
  }

  bool getPlaying() const { return isPlaying.load(); }

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
    request.emitChase =
        isPlaying.load(std::memory_order_acquire) || forceChaseWhilePaused;

    if (request.emitChase) {
      pendingAllNotesOff.store(false);
      restoreControllersState(&currentSequence->sequenceSamples,
                              request.position, request.index,
                              request.chaseMessages);
      restoreActiveNotes(&currentSequence->sequenceSamples, request.position,
                         request.index, request.chaseMessages);
    }

    enqueueSeekRequest(std::move(request));
  }

  bool hasFinished() { return finishedFlag.exchange(false); }

  bool isWaitingForTail() const {
    return sequenceEnded.load(std::memory_order_acquire);
  }

  void finishTail() {
    if (!sequenceEnded.exchange(false, std::memory_order_acq_rel))
      return;
    isPlaying.store(false, std::memory_order_release);
    finishedFlag.store(true, std::memory_order_release);
  }

  double getPositionInSamples() const {
    return currentPositionInSamples.load();
  }

  double getDurationInSamples() const {
    return cachedDurationSamples.load();
  }

  double getSequenceSampleRate() const { return cachedSampleRate.load(); }

  bool hasSequence() const { return sequenceLoaded.load(); }

  // seek/reset 在音频线程真正应用后只返回一次 true，供 AudioEngine 触发交叉淡入。
  bool consumeSeekOccurred() {
    return seekOccurred.exchange(false, std::memory_order_acquire);
  }

  // 停止清理应用后只返回一次 true，供 AudioEngine 精确静音清理块。
  bool consumeCleanupOccurred() {
    return cleanupOccurred.exchange(false, std::memory_order_acquire);
  }

  // AudioEngine 淡出到静音后调用；下一次 processBlock 释放 VST3 按键和踏板状态。
  void triggerStopCleanup() {
    pendingAllNotesOff.store(true);
  }

private:
  struct SequenceSnapshot {
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
    bool emitChase = false;
    juce::MidiBuffer chaseMessages;
  };

  static constexpr int seekQueueCapacity = 8;
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
    std::atomic<SlotState> state{SlotState::Free};
  };

  // 实时线程快照模型：消息线程独占创建/销毁 SequenceSnapshot，并通过固定槽位发布；
  // 音频线程只切换槽位状态和读取当前活动的不可变对象，避免在 processBlock 分配或加锁。
  std::array<SequenceSlot, sequenceSlotCount> sequenceSlots;
  std::atomic<int> publishedSlot{-1};
  std::atomic<int> audioActiveSlot{-1};

  std::atomic<bool> isPlaying{false};
  std::atomic<bool> finishedFlag{false};
  std::atomic<bool> sequenceLoaded{false};
  std::atomic<bool> sequenceEnded{false};
  std::atomic<bool> pendingAllNotesOff{false};
  std::atomic<bool> seekOccurred{false};
  std::atomic<bool> cleanupOccurred{false};

  // seek FIFO 是消息线程到音频线程的有界队列。UI 连续拖动时可能写入多个请求，
  // 音频线程每块只取同 generation 标记的最新请求，丢弃过期 seek，保持实时路径有界。
  std::array<SeekRequest, seekQueueCapacity> pendingSeekRequests;
  juce::AbstractFifo pendingSeekFifo{seekQueueCapacity};

  std::atomic<double> currentPositionInSamples{0.0};
  std::atomic<double> cachedDurationSamples{0.0};
  std::atomic<double> cachedSampleRate{44100.0};
  std::atomic<uint32_t> nextSequenceGeneration{1};
  int nextMessageIndex = 0;
  std::array<std::array<bool, 128>, 16> activeNotes{};

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

    auto *raw = snapshot.get();
    sequenceSlots[(size_t)targetSlot].snapshot = std::move(snapshot);
    sequenceSlots[(size_t)targetSlot].state.store(SlotState::Ready,
                                                  std::memory_order_release);

    const int replacedPending =
        publishedSlot.exchange(targetSlot, std::memory_order_acq_rel);
    if (replacedPending >= 0 && replacedPending != targetSlot) {
      auto &replaced = sequenceSlots[(size_t)replacedPending];
      auto expected = SlotState::Ready;
      if (replaced.state.compare_exchange_strong(
              expected, SlotState::Writing, std::memory_order_acq_rel)) {
        replaced.snapshot.reset();
        replaced.state.store(SlotState::Free, std::memory_order_release);
      }
    }

    if (!preservePlayingState)
      isPlaying.store(false);

    finishedFlag.store(false);
    sequenceEnded.store(false, std::memory_order_release);
    sequenceLoaded.store(raw != nullptr);
    cachedDurationSamples.store(raw != nullptr ? raw->durationSamples : 0.0);
    cachedSampleRate.store(raw != nullptr ? raw->sampleRate : 44100.0);
    if (!preservePlayingState) {
      currentPositionInSamples.store(
          raw != nullptr ? raw->initialPositionSamples : 0.0);
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
    int slotIndex = publishedSlot.load(std::memory_order_acquire);
    if (slotIndex < 0)
      slotIndex = audioActiveSlot.load(std::memory_order_acquire);

    return slotIndex >= 0
               ? sequenceSlots[(size_t)slotIndex].snapshot.get()
               : nullptr;
  }

  SequenceSnapshot *getAudioThreadSnapshot() const {
    const int slotIndex = audioActiveSlot.load(std::memory_order_relaxed);
    return slotIndex >= 0
               ? sequenceSlots[(size_t)slotIndex].snapshot.get()
               : nullptr;
  }

  void applyPublishedSequence() {
    // 槽位状态转换只能在音频线程消费，消息线程只负责发布 Ready 槽位。
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

    auto *active = next.snapshot.get();
    nextMessageIndex = active != nullptr ? active->initialMessageIndex : 0;
    currentPositionInSamples.store(
        active != nullptr ? active->initialPositionSamples : 0.0);
    finishedFlag.store(false);
    sequenceEnded.store(false, std::memory_order_release);
  }

  void addTrackedEvent(juce::MidiBuffer &buffer,
                       const juce::MidiMessage &message, int sampleOffset) {
    buffer.addEvent(message, sampleOffset);

    if (!message.isNoteOnOrOff())
      return;

    activeNotes[(size_t)(message.getChannel() - 1)]
               [(size_t)message.getNoteNumber()] = message.isNoteOn();
  }

  void addStateResetMessages(juce::MidiBuffer &buffer,
                             bool resetControllers) {
    for (int channel = 1; channel <= 16; ++channel) {
      for (int controller : {64, 66, 67})
        buffer.addEvent(
            juce::MidiMessage::controllerEvent(channel, controller, 0), 0);

      auto &channelNotes = activeNotes[(size_t)(channel - 1)];
      for (int note = 0; note < 128; ++note) {
        if (!channelNotes[(size_t)note])
          continue;

        buffer.addEvent(juce::MidiMessage::noteOff(channel, note), 0);
        channelNotes[(size_t)note] = false;
      }

      // AudioEngine 会静音包含这些释放事件的音频块，避免声部切断瞬态。
      buffer.addEvent(juce::MidiMessage::allSoundOff(channel), 0);
      if (resetControllers)
        buffer.addEvent(juce::MidiMessage::allControllersOff(channel), 0);
    }
  }

  bool enqueueSeekRequest(SeekRequest request) {
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    pendingSeekFifo.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 > 0) {
      pendingSeekRequests[(size_t)start1] = std::move(request);
      pendingSeekFifo.finishedWrite(1);
      return true;
    }

    if (size2 > 0) {
      pendingSeekRequests[(size_t)start2] = std::move(request);
      pendingSeekFifo.finishedWrite(1);
      return true;
    }

    LOG_DEBUG("MidiPlayer::seekTo - dropping seek request because the queue is "
              "full");
    // 拖动 seek 时允许丢弃过密请求；实时路径保持有界比完整保留每次拖动更重要。
    return false;
  }

  bool applyPendingSeekRequest(juce::MidiBuffer &buffer) {
    const int ready = pendingSeekFifo.getNumReady();
    if (ready <= 0)
      return false;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    pendingSeekFifo.prepareToRead(ready, start1, size1, start2, size2);

    auto *active = getAudioThreadSnapshot();
    const uint32_t generation = active != nullptr ? active->generation : 0;
    const SeekRequest *latestRequest = nullptr;

    auto findLatestMatchingRequest = [&](int start, int size) {
      for (int i = 0; i < size; ++i) {
        auto &candidate = pendingSeekRequests[(size_t)(start + i)];
        if (candidate.generation == generation)
          latestRequest = &candidate;
      }
    };

    findLatestMatchingRequest(start1, size1);
    findLatestMatchingRequest(start2, size2);

    if (latestRequest != nullptr) {
      currentPositionInSamples.store(latestRequest->position);
      nextMessageIndex = latestRequest->index;
      finishedFlag.store(false);
      sequenceEnded.store(false, std::memory_order_release);

      if (latestRequest->emitChase) {
        addStateResetMessages(buffer, true);
        for (const auto metadata : latestRequest->chaseMessages)
          addTrackedEvent(buffer, metadata.getMessage(), 1);
        seekOccurred.store(true, std::memory_order_release);
      }
    }

    pendingSeekFifo.finishedRead(size1 + size2);
    return latestRequest != nullptr;
  }

  // CC/Program/PitchWheel chase
  // 从第 0 个事件扫描到 seek 点，记录每个通道最后出现的 controller、
  // program-change 和 pitch-wheel 值，用于重建目标位置应当生效的 MIDI 状态。
  void restoreControllersState(const juce::MidiMessageSequence *seq,
                               double timeInSamples, int nextIndex,
                               juce::MidiBuffer &buffer) {
    if (seq == nullptr)
      return;

    int ccValues[17][128];
    for (int ch = 1; ch <= 16; ++ch)
      for (int cc = 0; cc < 128; ++cc)
        ccValues[ch][cc] = -1;

    int programValues[17];
    int pitchWheelValues[17];
    for (int ch = 1; ch <= 16; ++ch) {
      programValues[ch] = -1;
      pitchWheelValues[ch] = -1;
    }

    const int limit = juce::jmin(nextIndex, seq->getNumEvents());
    for (int i = 0; i < limit; ++i) {
      auto *event = seq->getEventPointer(i);
      if (event == nullptr)
        break;
      if (event->message.getTimeStamp() >= timeInSamples)
        break;

      const auto &msg = event->message;
      const int channel = msg.getChannel();
      if (channel < 1 || channel > 16)
        continue;

      if (msg.isController()) {
        ccValues[channel][msg.getControllerNumber()] =
            msg.getControllerValue();
      } else if (msg.isProgramChange()) {
        programValues[channel] = msg.getProgramChangeNumber();
      } else if (msg.isPitchWheel()) {
        pitchWheelValues[channel] = msg.getPitchWheelValue();
      }
    }

    // 按乐器重置顺序发出 chase 状态：
    //   bank-select -> program-change -> 其他 CC -> pitch-wheel
    for (int channel = 1; channel <= 16; ++channel) {
      for (int cc : {0, 32}) {
        if (ccValues[channel][cc] != -1) {
          buffer.addEvent(juce::MidiMessage::controllerEvent(
                              channel, cc, ccValues[channel][cc]),
                          0);
        }
      }

      if (programValues[channel] != -1) {
        buffer.addEvent(juce::MidiMessage::programChange(
                            channel, programValues[channel]),
                        0);
      }

      for (int cc = 0; cc < 128; ++cc) {
        if (cc == 0 || cc == 32)
          continue; // 已在前面发送

        int value = ccValues[channel][cc];
        if (value != -1) {
          buffer.addEvent(
              juce::MidiMessage::controllerEvent(channel, cc, value), 0);
        }
      }

      if (pitchWheelValues[channel] != -1) {
        buffer.addEvent(
            juce::MidiMessage::pitchWheel(channel, pitchWheelValues[channel]),
            0);
      }
    }
  }

  // Note chase
  // 重新触发所有跨过 seek 点的 [note-on .. note-off) 音符。原始 note-off 仍留在
  // 未来序列中按原时间发送，从而保持目标位置之后的实际音符长度。
  static void restoreActiveNotes(const juce::MidiMessageSequence *seq,
                                 double timeInSamples, int nextIndex,
                                 juce::MidiBuffer &buffer) {
    if (seq == nullptr)
      return;

    bool noteSeen[17][128] = {false};

    int startIndex = juce::jmin(nextIndex - 1, seq->getNumEvents() - 1);
    for (int i = startIndex; i >= 0; --i) {
      auto *event = seq->getEventPointer(i);
      if (event == nullptr)
        continue;

      const auto &message = event->message;
      if (!message.isNoteOn())
        continue;

      const int channel = message.getChannel();
      const int noteNum = message.getNoteNumber();
      if (channel < 1 || channel > 16 || noteNum < 0 || noteNum > 127)
        continue;

      if (noteSeen[channel][noteNum])
        continue;
      noteSeen[channel][noteNum] = true;

      auto *noteOff = event->noteOffObject;
      // 严格早于 seek 点结束的音符跳过。note-off 正好等于 seek 时间时，
      // 落点瞬间仍视为发声，因此重触发；下一音频块中 offset 0 的 note-off
      // 会立即释放它，不会产生可听见的延长。
      if (noteOff == nullptr ||
          noteOff->message.getTimeStamp() < timeInSamples)
        continue;

      auto chasedNote = message;
      chasedNote.setTimeStamp(0.0);
      buffer.addEvent(chasedNote, 0);
    }
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiPlayer)
};
