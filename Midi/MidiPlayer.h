#pragma once

#include "../Utils/DebugLogger.h"
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>

/**
    MidiPlayer 负责处理 MIDI 消息序列的步进逻辑。
    它基于音频块（Sample-accurate）提供高精度的定时触发。

    线程安全模型：
    - processBlock(): 由音频线程调用，负责实时生成 MIDI 消息。
    - setSequence(), setPlaying(), seekTo(): UI 线程调用，用于控制播放状态。
    - 内部锁 (juce::CriticalSection): 确保 UI
   线程修改序列或位置时，音频线程不会崩溃。
    - 原子变量: 播放状态 (isPlaying)、循环 (looping) 等标志使用 std::atomic
   实现无锁访问。

    维护细节：
    - Critical Section
   必须轻量：音频线程仅在检查状态和提取消息时持有锁，严禁在锁内进行耗时操作。
    - 所有的 Seek（跳转）都是异步处理的，通过 pendingSeekPosition
   标志触发，在下一个音频块开始时统一处理。
*/
class MidiPlayer {
public:
  enum class PlayMode { Sequential, ListLoop, SingleLoop, Shuffle };

  MidiPlayer() = default;

  /**
      Sets a new MIDI sequence. Thread-safe.
      @param newSequence The new sequence to play (takes ownership)
      @param newSampleRate The sample rate for timing calculations
  */
  void setSequence(std::shared_ptr<juce::MidiMessageSequence> newSequence,
                   double newSampleRate) {
    LOG_DEBUG("MidiPlayer::setSequence - Attempting lock...");

    // 使用 tryEnter 避免 UI 线程无限阻塞
    // 如果音频线程持有锁太久，最多等待 100ms 后强制获取
    bool acquired = lock.tryEnter();
    if (!acquired) {
      for (int i = 0; i < 100 && !acquired; ++i) {
        juce::Thread::sleep(1);
        acquired = lock.tryEnter();
      }
      if (!acquired) {
        LOG_DEBUG("MidiPlayer::setSequence - WARNING: Lock timeout after "
                  "100ms, forcing entry");
        lock.enter(); // 最后强制获取
      }
    }

    LOG_DEBUG("MidiPlayer::setSequence - Lock acquired");

    // 只在当前有序列时才发送 All Notes Off
    // 避免在已经停止/清空状态时重复发送，减少 VSL 插件阻塞风险
    if (sequence != nullptr) {
      pendingAllNotesOff.store(true);
    }

    sequence = std::move(newSequence);
    sequenceLoaded.store(sequence != nullptr);
    sampleRate = newSampleRate > 0 ? newSampleRate : 44100.0;
    cachedDurationSamples.store(sequence != nullptr ? sequence->getEndTime()
                                                    : 0.0);
    currentPositionInSamples.store(0.0);
    nextMessageIndex = 0;
    finishedFlag.store(false);

    lock.exit(); // 手动释放锁
  }

  /**
      Process a block of audio. Called from the audio thread.
      @param buffer The MIDI buffer to fill with events
      @param numSamples Number of samples in this block
  */
  void processBlock(juce::MidiBuffer &buffer, int numSamples) {
    // Note: Logging inside audio thread processBlock is dangerous,
    // but useful for tracking deadlocks. We keep it minimal.
    const juce::ScopedLock sl(lock);

    // 1. 处理异步跳转 (事件追溯)
    // 负载优化：SeekRequest 已在 UI 线程预先计算好追踪消息。
    // 这里只需执行原子指针交换，并更新索引，将 O(N) 降为 O(1)。
    if (auto *request = pendingSeekRequest.exchange(nullptr)) {
      // a. Apply pre-computed chase/reset buffer
      buffer.addEvents(request->chaseMessages, 0, numSamples, 0);

      // b. Update position and index
      currentPositionInSamples.store(request->position);
      nextMessageIndex = request->index;

      // Transfer to trash for UI thread to clean up (Avoid delete in real-time
      // thread)
      if (auto *oldTrash = trashRequest.exchange(request))
        delete oldTrash; // Should rarely happen, but safety first

      // Reset flags
      finishedFlag.store(false);
    }

    // Handle pending All Notes Off (if manually requested or stop)
    // 只发送通道 1 的 All Notes Off，避免 VSL
    // 等插件在收到多通道重置时阻塞消息线程 大多数钢琴 MIDI 只使用通道 1，与
    // restoreControllersState 保持一致
    if (pendingAllNotesOff.exchange(false)) {
      buffer.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    }

    if (sequence == nullptr || !isPlaying.load())
      return;

    double currentPos = currentPositionInSamples.load();
    double endPosition = currentPos + numSamples;
    int eventsProcessed = 0;
    const int maxEventsPerBlock =
        500; // Reduced from 2000 to prevent locking UI during heavy load

    while (nextMessageIndex < sequence->getNumEvents()) {
      auto *event = sequence->getEventPointer(nextMessageIndex);
      if (event == nullptr)
        break;

      double eventSample = event->message.getTimeStamp();

      if (eventSample >= endPosition)
        break;

      // Filter out invalid events for VST3
      // We only allow musical events
      if (event->message.isNoteOn() || event->message.isNoteOff() ||
          event->message.isController() || event->message.isPitchWheel() ||
          event->message.isProgramChange() ||
          event->message.isChannelPressure() || event->message.isAftertouch()) {

        int offset = juce::jmax(0, (int)(eventSample - currentPos));

        // Clamp offset to valid range
        offset = juce::jmin(offset, numSamples - 1);

        buffer.addEvent(event->message, offset);
      }

      nextMessageIndex++;
      eventsProcessed++;

      // If we hit the limit, stop processing for this block to free the lock
      if (eventsProcessed >= maxEventsPerBlock) {
        // Advance current position only to the last processed event's time
        // to ensure we pick up exactly where we left off
        currentPositionInSamples.store(eventSample);
        return;
      }
    }

    currentPositionInSamples.store(endPosition);

    // Check if sequence finished
    if (nextMessageIndex >= sequence->getNumEvents()) {
      if (looping.load()) {
        currentPositionInSamples.store(0.0);
        nextMessageIndex = 0;
        // Optional: Perform chasing for loop start if needed, or simple reset
      } else {
        isPlaying.store(false);
        finishedFlag.store(true);
      }
    }
  }

  void setPlaying(bool play) {
    if (!play && isPlaying.load()) {
      // Send All Notes Off when stopping
      pendingAllNotesOff.store(true);
    }

    isPlaying.store(play);
  }

  bool getPlaying() const { return isPlaying.load(); }

  void setLooping(bool loop) { looping.store(loop); }
  bool getLooping() const { return looping.load(); }

  /**
      Seek to a position in samples. Thread-safe and Lock-free for UI thread.
      This sets a flag that the audio thread will pick up in the next
     processBlock.
      @param positionInSamples The position to seek to
  */
  void seekTo(double positionInSamples) {
    auto currentSeq = sequence;
    if (currentSeq == nullptr)
      return;

    // 1. Clamp position
    double seekPos =
        juce::jlimit(0.0, cachedDurationSamples.load(), positionInSamples);

    auto *request = new SeekRequest();
    request->position = seekPos;

    // 2. Find new index (Binary Search) - NOW IN CALLER THREAD
    const int numEvents = currentSeq->getNumEvents();
    int low = 0;
    int high = numEvents;
    while (low < high) {
      int mid = low + (high - low) / 2;
      auto *event = currentSeq->getEventPointer(mid);
      if (event->message.getTimeStamp() < seekPos)
        low = mid + 1;
      else
        high = mid;
    }
    request->index = low;

    // 3. Pre-compute chase messages - NOW IN CALLER THREAD (OFF-AUDIO)
    // a. All Notes Off - 只发送通道1，减少 MIDI 消息量
    // 原因：发送 16 个通道的 All Notes Off 会触发 VSL 等插件在 GUI
    // 线程执行重操作 大多数钢琴 MIDI 只使用通道 1，这样可以大幅减少阻塞风险
    request->chaseMessages.addEvent(juce::MidiMessage::allNotesOff(1), 0);

    // b. Perform O(N) scan for controller states
    restoreControllersState(currentSeq.get(), seekPos, request->index,
                            request->chaseMessages);

    // 4. Send to audio thread
    if (auto *old = pendingSeekRequest.exchange(request))
      delete old; // Clean up any unhandled pending seek

    // 5. Clean up any trash from audio thread
    if (auto *trash = trashRequest.exchange(nullptr))
      delete trash;
  }

  /**
      Check if the sequence has finished playing. Thread-safe.
      This flag is automatically reset after being read.
      @return true if the sequence finished since last check
  */
  bool hasFinished() { return finishedFlag.exchange(false); }

  /**
      Get the current playback position in samples.
  */
  double getPositionInSamples() const {
    // No lock needed for atomic load - prevents UI thread from blocking
    return currentPositionInSamples.load();
  }

  /**
      Get the total duration in samples.
  */
  double getDurationInSamples() const {
    // Return cached value to avoid lock contention
    return cachedDurationSamples.load();
  }

  /**
      Check if a sequence is loaded.
  */
  bool hasSequence() const {
    // Return atomic flag to avoid lock contention
    return sequenceLoaded.load();
  }

private:
  struct SeekRequest {
    double position = 0;
    int index = 0;
    juce::MidiBuffer chaseMessages;
  };

  mutable juce::CriticalSection lock;
  std::shared_ptr<juce::MidiMessageSequence> sequence;

  std::atomic<bool> isPlaying{false};
  std::atomic<bool> looping{false};
  std::atomic<bool> finishedFlag{false};
  std::atomic<bool> sequenceLoaded{false};

  // Async seek request from UI (null means no seek pending)
  std::atomic<SeekRequest *> pendingSeekRequest{nullptr};
  std::atomic<SeekRequest *> trashRequest{nullptr};

  std::atomic<bool> pendingAllNotesOff{false};

  double sampleRate = 44100.0;
  std::atomic<double> currentPositionInSamples{0.0};
  std::atomic<double> cachedDurationSamples{0.0};
  int nextMessageIndex = 0;

  /**
      控制器状态恢复助手（事件追溯 Event Chasing）。
      现在在锁外执行，并且明确接收快照目标。
  */
  void restoreControllersState(const juce::MidiMessageSequence *seq,
                               double timeInSamples, int nextIndex,
                               juce::MidiBuffer &buffer) {
    if (seq == nullptr)
      return;

    // Track the last value for important controllers per channel
    // Structure: [channel 1-16] -> [CC# 0-127] -> value
    int ccValues[17][128];
    // Initialize with -1 (meaning not found yet)
    for (int ch = 1; ch <= 16; ++ch)
      for (int cc = 0; cc < 128; ++cc)
        ccValues[ch][cc] = -1;

    // Also track Program Changes: [channel 1-16] -> program
    int programValues[17];
    for (int ch = 1; ch <= 16; ++ch)
      programValues[ch] = -1;

    // nextIndex points to the first event >= timeInSamples (or end)
    int startIndex = nextIndex - 1;
    if (startIndex >= seq->getNumEvents())
      startIndex = seq->getNumEvents() - 1;

    int iterations = 0;
    const int maxIterations =
        2000; // stricter safety guard for massive sequences

    for (int i = startIndex; i >= 0; --i) {
      if (++iterations > maxIterations)
        break; // Safety break

      auto *event = seq->getEventPointer(i);
      // Double check time just in case, though index logic should suffice
      if (event->message.getTimeStamp() >= timeInSamples)
        continue;

      auto &m = event->message;
      int ch = m.getChannel();

      if (m.isController()) {
        int cc = m.getControllerNumber();
        // 追踪关键控制器：
        // CC64=延音踏板, CC66=持续音踏板, CC67=弱音踏板
        // CC7=音量, CC11=表情, CC10=声像, CC1=调制
        if ((cc == 64 || cc == 66 || cc == 67 || cc == 7 || cc == 11 ||
             cc == 10 || cc == 1) &&
            ccValues[ch][cc] == -1) {
          ccValues[ch][cc] = m.getControllerValue();
        }
      } else if (m.isProgramChange() && programValues[ch] == -1) {
        programValues[ch] = m.getProgramChangeNumber();
      }

      // Heuristic break: If we've gone back too far (e.g. 500 events) and found
      // most things? No, for correctness we must verify. But backwards search
      // is generally very fast because controllers are frequent.
    }

    // Now emit the restored states - 只恢复通道1，减少 MIDI 消息量
    // 钢琴通常只使用通道1，发送16个通道的控制器会触发 VSL 阻塞
    for (int ch = 1; ch <= 1; ++ch) {
      // 注意：不再发送 Program Change！
      // 原因：VSL 等采样器插件收到 PC 消息时会在 GUI 线程执行重操作
      // （如加载采样），导致整个 UI 冻结。对于钢琴等单音色插件，
      // PC 恢复是不必要的；对于多音色插件，用户应在插件内手动选择。
      // if (programValues[ch] != -1) {
      //   buffer.addEvent(juce::MidiMessage::programChange(ch,
      //   programValues[ch]), 0);
      // }

      // Restore Controllers
      for (int cc = 0; cc < 128; ++cc) {
        int val = ccValues[ch][cc];

        // 踏板特殊逻辑：
        // 如果没找到值（val == -1），说明曲子开头踏板是松开的（默认0）
        // 必须显式发送0来重置踏板状态
        if ((cc == 64 || cc == 66 || cc == 67) && val == -1) {
          val = 0;
        }

        if (val != -1) {
          // 只发送我们关心的控制器
          if (cc == 64 || cc == 66 || cc == 67 || cc == 7 || cc == 11 ||
              cc == 10 || cc == 1) {
            buffer.addEvent(juce::MidiMessage::controllerEvent(ch, cc, val), 0);
          }
        }
      }
    }
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiPlayer)
};
