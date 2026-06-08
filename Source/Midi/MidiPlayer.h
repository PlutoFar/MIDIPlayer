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
  enum class PlayMode { Sequential, ListLoop, SingleLoop, Shuffle };

  MidiPlayer() = default;

  ~MidiPlayer() {
    publishedSlot.store(-1, std::memory_order_release);
    audioActiveSlot.store(-1, std::memory_order_release);
    for (auto &slot : sequenceSlots)
      slot.snapshot.reset();
  }

  // newSequence must contain timestamps in seconds. Snapshot allocation,
  // timestamp conversion and reclamation all remain on the message thread.
  void setSequence(std::unique_ptr<juce::MidiMessageSequence> newSequence,
                   double newSampleRate) {
    if (hasSequence()) {
      pendingAllNotesOff.store(true);
      // Trigger crossfade so AudioEngine mutes the buffer containing
      // the allSoundOff, eliminating track-switch pops.
      seekOccurred.store(true, std::memory_order_release);
    }

    const double rate = sanitiseSampleRate(newSampleRate);
    auto snapshot = createSnapshot(std::move(newSequence), rate, 0.0);
    publishSnapshot(std::move(snapshot), false);
  }

  // Called from the message thread when the audio device rate changes.
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
    applyPendingSeekRequest(buffer, numSamples);

    // Panic messages are only sent when audio is already silent
    // (triggered by AudioEngine after fade-out, or by setSequence
    // which pairs this with a crossfade mute).
    if (pendingAllNotesOff.exchange(false)) {
      addPanicMessages(buffer);
      cleanupOccurred.store(true, std::memory_order_release);
    }

    auto *currentSequence = getAudioThreadSnapshot();
    if (currentSequence == nullptr || !isPlaying.load() || numSamples <= 0)
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

      if (event->message.isNoteOn() || event->message.isNoteOff() ||
          event->message.isController() || event->message.isPitchWheel() ||
          event->message.isProgramChange() ||
          event->message.isChannelPressure() ||
          event->message.isAftertouch()) {
        int offset = juce::jmax(0, (int)(eventSample - currentPos));
        offset = juce::jmin(offset, numSamples - 1);
        buffer.addEvent(event->message, offset);
      }

      ++nextMessageIndex;
    }

    currentPositionInSamples.store(endPosition);

    if (nextMessageIndex >= events.getNumEvents()) {
      if (looping.load()) {
        currentPositionInSamples.store(0.0);
        nextMessageIndex = 0;
      } else {
        isPlaying.store(false);
        finishedFlag.store(true);
        // Don't send panic here — AudioEngine will trigger cleanup
        // after its fade-out reaches silence.
      }
    }
  }

  void setPlaying(bool play) {
    if (play && !hasSequence())
      return;
    // Pause: just stop advancing the sequence. No MIDI reset here —
    // AudioEngine's fade-out silences the output, then triggers
    // cleanup (allSoundOff) once audio reaches zero.
    // Resume: the caller (UI) should call seekTo(currentPos) first
    // to chase-restore all CC and note state.
    isPlaying.store(play);
  }

  bool getPlaying() const { return isPlaying.load(); }

  void setLooping(bool loop) { looping.store(loop); }
  bool getLooping() const { return looping.load(); }

  void seekTo(double positionInSamples) {
    auto *currentSequence = getMessageThreadSnapshot();
    if (currentSequence == nullptr)
      return;

    // Seek performs a full reset + state rebuild, so any pending
    // stop-cleanup is now redundant.
    pendingAllNotesOff.store(false);

    SeekRequest request;
    request.position =
        juce::jlimit(0.0, cachedDurationSamples.load(), positionInSamples);
    request.generation = currentSequence->generation;
    request.index =
        findEventIndex(currentSequence->sequenceSamples, request.position);

    addSeekResetMessages(request.chaseMessages);
    restoreControllersState(&currentSequence->sequenceSamples, request.position,
                            request.index, request.chaseMessages);
    restoreActiveNotes(&currentSequence->sequenceSamples, request.position,
                       request.index, request.chaseMessages);

    enqueueSeekRequest(std::move(request));
  }

  bool hasFinished() { return finishedFlag.exchange(false); }

  double getPositionInSamples() const {
    return currentPositionInSamples.load();
  }

  double getDurationInSamples() const {
    return cachedDurationSamples.load();
  }

  double getSequenceSampleRate() const { return cachedSampleRate.load(); }

  bool hasSequence() const { return sequenceLoaded.load(); }

  // Returns true exactly once after a seek/reset has been applied on the
  // audio thread.  AudioEngine uses this to trigger a crossfade.
  bool consumeSeekOccurred() {
    return seekOccurred.exchange(false, std::memory_order_acquire);
  }

  // Returns true exactly once after a stop-cleanup (allSoundOff) has been applied.
  // AudioEngine uses this to mute the exact block containing the cleanup click.
  bool consumeCleanupOccurred() {
    return cleanupOccurred.exchange(false, std::memory_order_acquire);
  }

  // Called by AudioEngine after its fade-out reaches silence.
  // Sends allSoundOff on the next processBlock to free VST voices.
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

  // The message thread alone creates and destroys snapshots. The audio thread
  // only transitions fixed slot states and reads the active immutable object.
  std::array<SequenceSlot, sequenceSlotCount> sequenceSlots;
  std::atomic<int> publishedSlot{-1};
  std::atomic<int> audioActiveSlot{-1};

  std::atomic<bool> isPlaying{false};
  std::atomic<bool> looping{false};
  std::atomic<bool> finishedFlag{false};
  std::atomic<bool> sequenceLoaded{false};
  std::atomic<bool> pendingAllNotesOff{false};
  std::atomic<bool> seekOccurred{false};
  std::atomic<bool> cleanupOccurred{false};

  std::array<SeekRequest, seekQueueCapacity> pendingSeekRequests;
  juce::AbstractFifo pendingSeekFifo{seekQueueCapacity};

  std::atomic<double> currentPositionInSamples{0.0};
  std::atomic<double> cachedDurationSamples{0.0};
  std::atomic<double> cachedSampleRate{44100.0};
  std::atomic<uint32_t> nextSequenceGeneration{1};
  int nextMessageIndex = 0;

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
  }

  static void addPanicMessages(juce::MidiBuffer &buffer) {
    for (int channel = 1; channel <= 16; ++channel) {
      // This only fires when audio output is already at zero (after
      // AudioEngine's fade-out, or inside a crossfade-muted buffer).
      // allSoundOff is safe here — no pop because nobody can hear it.
      buffer.addEvent(juce::MidiMessage::allSoundOff(channel), 0);
    }
  }

  static void addSeekResetMessages(juce::MidiBuffer &buffer) {
    for (int channel = 1; channel <= 16; ++channel) {
      // Use All Sound Off (CC#120) for an immediate hard-kill of every voice.
      // This avoids the problem of releasing pedals first (which triggers a
      // burst of release tails) and then sending All Notes Off.  The
      // resulting audio discontinuity is masked by the AudioEngine's muted
      // seek block and short fade-in at the audio output level.
      buffer.addEvent(juce::MidiMessage::allSoundOff(channel), 0);
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
    return false;
  }

  void applyPendingSeekRequest(juce::MidiBuffer &buffer, int numSamples) {
    const int ready = pendingSeekFifo.getNumReady();
    if (ready <= 0)
      return;

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
      buffer.addEvents(latestRequest->chaseMessages, 0, numSamples, 0);
      currentPositionInSamples.store(latestRequest->position);
      nextMessageIndex = latestRequest->index;
      finishedFlag.store(false);
      seekOccurred.store(true, std::memory_order_release);
    }

    pendingSeekFifo.finishedRead(size1 + size2);
  }

  // ---------- Forward-scan CC/Program/PitchWheel chase ----------
  // Walk from event 0 to the seek point and keep the last controller,
  // program-change, and pitch-wheel value seen for each channel. This rebuilds
  // the MIDI state that should be active at the target position.
  void restoreControllersState(const juce::MidiMessageSequence *seq,
                               double timeInSamples, int nextIndex,
                               juce::MidiBuffer &buffer) {
    if (seq == nullptr)
      return;

    // Per-channel state arrays.
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

    // Forward scan: walk every event from the start up to (but not
    // including) the seek point. The last value we see for each
    // controller/program/pitch-wheel is the definitive state.
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

    // Emit chased state in correct instrument-reset order:
    //   bank-select → program-change → all other CCs → pitch-wheel
    for (int channel = 1; channel <= 16; ++channel) {
      // 1. Bank Select (CC0, CC32) first
      for (int cc : {0, 32}) {
        if (ccValues[channel][cc] != -1) {
          buffer.addEvent(juce::MidiMessage::controllerEvent(
                              channel, cc, ccValues[channel][cc]),
                          0);
        }
      }

      // 2. Program Change
      if (programValues[channel] != -1) {
        buffer.addEvent(juce::MidiMessage::programChange(
                            channel, programValues[channel]),
                        0);
      }

      // 3. All remaining CCs (including pedals, modulation, volume, etc.)
      for (int cc = 0; cc < 128; ++cc) {
        if (cc == 0 || cc == 32)
          continue; // already emitted above

        int value = ccValues[channel][cc];
        if (value != -1) {
          buffer.addEvent(
              juce::MidiMessage::controllerEvent(channel, cc, value), 0);
        }
      }

      // 4. Pitch Wheel
      if (pitchWheelValues[channel] != -1) {
        buffer.addEvent(
            juce::MidiMessage::pitchWheel(channel, pitchWheelValues[channel]),
            0);
      }
    }
  }

  // ---------- Note chase ----------
  // Re-trigger every note whose [note-on .. note-off) interval spans the
  // seek point.  The original note-off that lives in the future part of the
  // sequence will fire at its correct time, preserving the intended duration.
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
      // Skip notes that have already ended strictly before the seek point.
      // A note-off exactly AT the seek time means the note is still
      // sounding at the instant we land, so we re-trigger it (the
      // upcoming note-off at offset 0 in processBlock will release it
      // immediately, which is the correct musical result).
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
