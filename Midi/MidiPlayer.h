#pragma once

#include "../Utils/DebugLogger.h"
#include <array>
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <memory>

class MidiPlayer {
public:
  enum class PlayMode { Sequential, ListLoop, SingleLoop, Shuffle };

  MidiPlayer() = default;

  void setSequence(std::shared_ptr<juce::MidiMessageSequence> newSequence,
                   double newSampleRate) {
    if (hasSequence())
      pendingAllNotesOff.store(true);

    auto update = std::make_shared<SequenceUpdate>();
    update->sequence = std::move(newSequence);
    update->sampleRate = newSampleRate > 0 ? newSampleRate : 44100.0;
    update->durationSamples =
        update->sequence != nullptr ? update->sequence->getEndTime() : 0.0;
    update->generation = nextSequenceGeneration.fetch_add(1);

    isPlaying.store(false);
    finishedFlag.store(false);
    sequenceLoaded.store(update->sequence != nullptr);
    cachedDurationSamples.store(update->durationSamples);
    currentPositionInSamples.store(0.0);

    std::atomic_store(&pendingSequenceUpdate, std::move(update));
  }

  void processBlock(juce::MidiBuffer &buffer, int numSamples) {
    applyPendingSequenceUpdate();
    applyPendingSeekRequest(buffer, numSamples);

    if (pendingAllNotesOff.exchange(false))
      addAllNotesOffForAllChannels(buffer, 0);

    auto currentSequence = std::atomic_load(&sequence);
    if (currentSequence == nullptr || !isPlaying.load() || numSamples <= 0)
      return;

    const double currentPos = currentPositionInSamples.load();
    const double endPosition = currentPos + numSamples;

    while (nextMessageIndex < currentSequence->getNumEvents()) {
      auto *event = currentSequence->getEventPointer(nextMessageIndex);
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
        int offset = juce::jmax(0, (int) (eventSample - currentPos));
        offset = juce::jmin(offset, numSamples - 1);
        buffer.addEvent(event->message, offset);
      }

      ++nextMessageIndex;
    }

    currentPositionInSamples.store(endPosition);

    if (nextMessageIndex >= currentSequence->getNumEvents()) {
      if (looping.load()) {
        currentPositionInSamples.store(0.0);
        nextMessageIndex = 0;
      } else {
        isPlaying.store(false);
        finishedFlag.store(true);
        pendingAllNotesOff.store(true);
      }
    }
  }

  void setPlaying(bool play) {
    if (play && !hasSequence())
      return;

    if (!play && isPlaying.load())
      pendingAllNotesOff.store(true);

    isPlaying.store(play);
  }

  bool getPlaying() const { return isPlaying.load(); }

  void setLooping(bool loop) { looping.store(loop); }
  bool getLooping() const { return looping.load(); }

  void seekTo(double positionInSamples) {
    uint32_t generation = 0;
    auto currentSequence = getSeekSequenceSnapshot(generation);
    if (currentSequence == nullptr)
      return;

    SeekRequest request;
    request.position =
        juce::jlimit(0.0, cachedDurationSamples.load(), positionInSamples);
    request.generation = generation;

    const int numEvents = currentSequence->getNumEvents();
    int low = 0;
    int high = numEvents;
    while (low < high) {
      const int mid = low + (high - low) / 2;
      auto *event = currentSequence->getEventPointer(mid);
      if (event->message.getTimeStamp() < request.position)
        low = mid + 1;
      else
        high = mid;
    }
    request.index = low;

    request.chaseMessages.clear();
    addAllNotesOffForAllChannels(request.chaseMessages, 0);
    restoreControllersState(currentSequence.get(), request.position,
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

  bool hasSequence() const { return sequenceLoaded.load(); }

private:
  struct SequenceUpdate {
    std::shared_ptr<juce::MidiMessageSequence> sequence;
    double sampleRate = 44100.0;
    double durationSamples = 0.0;
    uint32_t generation = 0;
  };

  struct SeekRequest {
    double position = 0.0;
    int index = 0;
    uint32_t generation = 0;
    juce::MidiBuffer chaseMessages;
  };

  static constexpr int seekQueueCapacity = 8;

  static void addAllNotesOffForAllChannels(juce::MidiBuffer &buffer,
                                           int sampleOffset) {
    for (int ch = 1; ch <= 16; ++ch)
      buffer.addEvent(juce::MidiMessage::allNotesOff(ch), sampleOffset);
  }

  std::shared_ptr<juce::MidiMessageSequence> sequence;
  std::shared_ptr<SequenceUpdate> pendingSequenceUpdate;

  std::atomic<bool> isPlaying{false};
  std::atomic<bool> looping{false};
  std::atomic<bool> finishedFlag{false};
  std::atomic<bool> sequenceLoaded{false};
  std::atomic<bool> pendingAllNotesOff{false};

  std::array<SeekRequest, seekQueueCapacity> pendingSeekRequests;
  juce::AbstractFifo pendingSeekFifo{seekQueueCapacity};

  double sampleRate = 44100.0;
  std::atomic<double> currentPositionInSamples{0.0};
  std::atomic<double> cachedDurationSamples{0.0};
  std::atomic<uint32_t> activeSequenceGeneration{0};
  std::atomic<uint32_t> nextSequenceGeneration{1};
  int nextMessageIndex = 0;

  void applyPendingSequenceUpdate() {
    auto update =
        std::atomic_exchange(&pendingSequenceUpdate,
                             std::shared_ptr<SequenceUpdate>{});
    if (update == nullptr)
      return;

    std::atomic_store(&sequence, update->sequence);
    activeSequenceGeneration.store(update->generation);
    sampleRate = update->sampleRate;
    cachedDurationSamples.store(update->durationSamples);
    currentPositionInSamples.store(0.0);
    nextMessageIndex = 0;
    finishedFlag.store(false);
  }

  std::shared_ptr<juce::MidiMessageSequence>
  getSeekSequenceSnapshot(uint32_t &generation) const {
    if (auto pendingUpdate = std::atomic_load(&pendingSequenceUpdate)) {
      generation = pendingUpdate->generation;
      return pendingUpdate->sequence;
    }

    generation = activeSequenceGeneration.load();
    return std::atomic_load(&sequence);
  }

  bool enqueueSeekRequest(SeekRequest request) {
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    pendingSeekFifo.prepareToWrite(1, start1, size1, start2, size2);

    if (size1 > 0) {
      pendingSeekRequests[(size_t) start1] = std::move(request);
      pendingSeekFifo.finishedWrite(1);
      return true;
    }

    if (size2 > 0) {
      pendingSeekRequests[(size_t) start2] = std::move(request);
      pendingSeekFifo.finishedWrite(1);
      return true;
    }

    return false;
  }

  void applyPendingSeekRequest(juce::MidiBuffer &buffer, int numSamples) {
    const int ready = pendingSeekFifo.getNumReady();
    if (ready <= 0)
      return;

    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    pendingSeekFifo.prepareToRead(ready, start1, size1, start2, size2);

    const uint32_t generation = activeSequenceGeneration.load();
    const SeekRequest *latestRequest = nullptr;

    auto findLatestMatchingRequest = [&](int start, int size) {
      for (int i = 0; i < size; ++i) {
        auto &candidate = pendingSeekRequests[(size_t) (start + i)];
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
    }

    pendingSeekFifo.finishedRead(size1 + size2);
  }

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
    for (int ch = 1; ch <= 16; ++ch)
      programValues[ch] = -1;

    int startIndex = nextIndex - 1;
    if (startIndex >= seq->getNumEvents())
      startIndex = seq->getNumEvents() - 1;

    int iterations = 0;
    const int maxIterations = 2000;

    for (int i = startIndex; i >= 0; --i) {
      if (++iterations > maxIterations)
        break;

      auto *event = seq->getEventPointer(i);
      if (event->message.getTimeStamp() >= timeInSamples)
        continue;

      auto &m = event->message;
      const int ch = m.getChannel();

      if (m.isController()) {
        const int cc = m.getControllerNumber();
        if ((cc == 64 || cc == 66 || cc == 67 || cc == 7 || cc == 11 ||
             cc == 10 || cc == 1) &&
            ccValues[ch][cc] == -1) {
          ccValues[ch][cc] = m.getControllerValue();
        }
      } else if (m.isProgramChange() && programValues[ch] == -1) {
        programValues[ch] = m.getProgramChangeNumber();
      }
    }

    for (int ch = 1; ch <= 16; ++ch) {
      if (programValues[ch] != -1) {
        buffer.addEvent(
            juce::MidiMessage::programChange(ch, programValues[ch]), 0);
      }

      for (int cc = 0; cc < 128; ++cc) {
        int value = ccValues[ch][cc];

        if ((cc == 64 || cc == 66 || cc == 67) && value == -1)
          value = 0;

        if (value != -1 &&
            (cc == 64 || cc == 66 || cc == 67 || cc == 7 || cc == 11 ||
             cc == 10 || cc == 1)) {
          buffer.addEvent(juce::MidiMessage::controllerEvent(ch, cc, value), 0);
        }
      }
    }
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiPlayer)
};
