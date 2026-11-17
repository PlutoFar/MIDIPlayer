#include "TestHarness.h"
#include "TestMidiEvents.h"
#include "Midi/MidiPlayer.h"

namespace {

void addEvent(juce::MidiMessageSequence &sequence, juce::MidiMessage message,
              double timeSeconds) {
  message.setTimeStamp(timeSeconds);
  sequence.addEvent(message);
}

bool containsNoteOff(const juce::MidiBuffer &buffer, int channel, int note) {
  for (const auto metadata : buffer) {
    const auto message = metadata.getMessage();
    if (message.isNoteOff() && message.getChannel() == channel &&
        message.getNoteNumber() == note)
      return true;
  }
  return false;
}

int findNoteEventIndex(const juce::MidiBuffer &buffer, bool noteOn,
                       int channel, int note) {
  int index = 0;
  for (const auto metadata : buffer) {
    const auto message = metadata.getMessage();
    const bool matchesType = noteOn ? message.isNoteOn() : message.isNoteOff();
    if (matchesType && message.getChannel() == channel &&
        message.getNoteNumber() == note)
      return index;
    ++index;
  }
  return -1;
}

bool containsController(const juce::MidiBuffer &buffer, int channel,
                        int controller, int value) {
  for (const auto metadata : buffer) {
    const auto message = metadata.getMessage();
    if (message.isController() && message.getChannel() == channel &&
        message.getControllerNumber() == controller &&
        message.getControllerValue() == value)
      return true;
  }
  return false;
}

int findControllerIndex(const juce::MidiBuffer &buffer, int channel,
                        int controller, int value) {
  int index = 0;
  for (const auto metadata : buffer) {
    const auto message = metadata.getMessage();
    if (message.isController() && message.getChannel() == channel &&
        message.getControllerNumber() == controller &&
        message.getControllerValue() == value)
      return index;
    ++index;
  }
  return -1;
}

} // namespace

namespace miditest {

int runMidiPlaybackTests() {
  int failures = 0;
  const auto expect = [&](bool condition, const char *message) {
    miditest::expect(failures, condition, message);
  };

  MidiPlayer midiPlayer;
  auto playingSequence = std::make_unique<juce::MidiMessageSequence>();
  addEvent(*playingSequence, juce::MidiMessage::noteOn(1, 60, (juce::uint8)100),
           0.0);
  addEvent(*playingSequence, juce::MidiMessage::controllerEvent(1, 64, 127),
           0.0);
  addEvent(*playingSequence, juce::MidiMessage::noteOff(1, 60), 1.0);
  midiPlayer.setSequence(std::move(playingSequence), 1000.0);
  midiPlayer.setPlaying(true);

  juce::MidiBuffer playingEvents;
  midiPlayer.processBlock(playingEvents, 128);

  midiPlayer.setSequence(std::make_unique<juce::MidiMessageSequence>(),
                         1000.0);
  juce::MidiBuffer trackSwitchEvents;
  midiPlayer.processBlock(trackSwitchEvents, 128);
  expect(containsNoteOff(trackSwitchEvents, 1, 60),
         "switching tracks should directly release every active note");
  expect(containsController(trackSwitchEvents, 1, 64, 0),
         "switching tracks should explicitly release the sustain pedal");
  expect(containsController(trackSwitchEvents, 1, 66, 0),
         "switching tracks should explicitly release the sostenuto pedal");
  expect(containsController(trackSwitchEvents, 1, 67, 0),
         "switching tracks should explicitly release the soft pedal");

  MidiPlayer silentSeekPlayer;
  auto silentSeekSequence = std::make_unique<juce::MidiMessageSequence>();
  addEvent(*silentSeekSequence,
           juce::MidiMessage::noteOn(1, 61, (juce::uint8)100), 0.0);
  addEvent(*silentSeekSequence,
           juce::MidiMessage::controllerEvent(1, 64, 127), 0.0);
  addEvent(*silentSeekSequence, juce::MidiMessage::noteOff(1, 61), 0.2);
  addEvent(*silentSeekSequence,
           juce::MidiMessage::controllerEvent(1, 64, 0), 0.2);
  addEvent(*silentSeekSequence,
           juce::MidiMessage::controllerEvent(1, 1, 0), 1.0);
  silentSeekPlayer.setSequence(std::move(silentSeekSequence), 1000.0);
  silentSeekPlayer.setPlaying(true);
  juce::MidiBuffer silentSeekStart;
  silentSeekPlayer.processBlock(silentSeekStart, 128);
  silentSeekPlayer.seekTo(500.0);
  juce::MidiBuffer silentSeekEvents;
  silentSeekPlayer.processBlock(silentSeekEvents, 128);
  expect(containsNoteOff(silentSeekEvents, 1, 61),
         "seeking past a note should directly release its previous key state");

  MidiPlayer activeSeekPlayer;
  auto activeSeekSequence = std::make_unique<juce::MidiMessageSequence>();
  addEvent(*activeSeekSequence,
           juce::MidiMessage::noteOn(1, 62, (juce::uint8)100), 0.0);
  addEvent(*activeSeekSequence,
           juce::MidiMessage::controllerEvent(1, 64, 127), 0.0);
  addEvent(*activeSeekSequence, juce::MidiMessage::noteOff(1, 62), 1.0);
  addEvent(*activeSeekSequence,
           juce::MidiMessage::controllerEvent(1, 64, 0), 1.0);
  activeSeekPlayer.setSequence(std::move(activeSeekSequence), 1000.0);
  activeSeekPlayer.setPlaying(true);
  juce::MidiBuffer activeSeekStart;
  activeSeekPlayer.processBlock(activeSeekStart, 128);
  activeSeekPlayer.seekTo(500.0);
  juce::MidiBuffer activeSeekEvents;
  activeSeekPlayer.processBlock(activeSeekEvents, 128);
  const int oldNoteOffIndex = findNoteEventIndex(activeSeekEvents, false, 1, 62);
  expect(oldNoteOffIndex >= 0 && findNoteEventIndex(activeSeekEvents, true, 1, 62) < 0,
         "seek should release the old key without retriggering it");
  const int pedalOffIndex = findControllerIndex(activeSeekEvents, 1, 64, 0);
  expect(pedalOffIndex >= 0 && findControllerIndex(activeSeekEvents, 1, 64, 127) < 0,
         "seek should finish the reset block before restoring the pedal");

  expect(findControllerSamplePosition(activeSeekEvents, 1, 64, 0) == 0,
         "seek should reset sustain at sample 0");
  juce::MidiBuffer activeSeekPedals;
  activeSeekPlayer.processBlock(activeSeekPedals, 128);
  expect(findControllerSamplePosition(activeSeekPedals, 1, 64, 127) == 0,
         "seek should restore sustain at the start of a separate block");
  expect(activeSeekPedals.getNumEvents() == 1,
         "pedal restore should contain only the pedal used by the MIDI file");
  expect(findNoteSamplePosition(activeSeekPedals, true, 1, 62) < 0,
         "seek should leave previously played notes released");
  expect(activeSeekPlayer.getPositionInSamples() == 500.0,
         "the silent seek reconstruction block must not advance playback");

  juce::MidiBuffer resumedPlayback;
  activeSeekPlayer.processBlock(resumedPlayback, 128);
  expect(activeSeekPlayer.getPositionInSamples() == 628.0,
         "playback should advance from the seek target on the next block");

  activeSeekPlayer.setPlaying(false);
  activeSeekPlayer.triggerStopCleanup();
  juce::MidiBuffer chasedNoteCleanup;
  activeSeekPlayer.processBlock(chasedNoteCleanup, 128);
  expect(!containsNoteOff(chasedNoteCleanup, 1, 62) &&
             containsController(chasedNoteCleanup, 1, 64, 0),
         "stop cleanup should release the restored pedal without repeating old key releases");

  MidiPlayer pauseResumePlayer;
  auto pauseResumeSequence = std::make_unique<juce::MidiMessageSequence>();
  addEvent(*pauseResumeSequence,
           juce::MidiMessage::noteOn(1, 65, (juce::uint8)100), 0.0);
  addEvent(*pauseResumeSequence,
           juce::MidiMessage::controllerEvent(1, 64, 127), 0.0);
  addEvent(*pauseResumeSequence, juce::MidiMessage::noteOff(1, 65), 1.0);
  addEvent(*pauseResumeSequence,
           juce::MidiMessage::controllerEvent(1, 64, 0), 1.0);
  pauseResumePlayer.setSequence(std::move(pauseResumeSequence), 1000.0);
  pauseResumePlayer.setPlaying(true);
  juce::MidiBuffer pauseResumeStart;
  pauseResumePlayer.processBlock(pauseResumeStart, 128);

  pauseResumePlayer.setPlaying(false);
  pauseResumePlayer.triggerStopCleanup();
  juce::MidiBuffer pauseCleanup;
  pauseResumePlayer.processBlock(pauseCleanup, 128);
  expect(containsController(pauseCleanup, 1, 64, 0),
         "pause cleanup should release sustain while output is silent");

  const double draggedPausedPosition = 500.0;
  pauseResumePlayer.seekTo(draggedPausedPosition);
  juce::MidiBuffer pausedSeekEvents;
  pauseResumePlayer.processBlock(pausedSeekEvents, 128);
  expect(pausedSeekEvents.isEmpty(),
         "seeking while paused should only update position and emit no MIDI");
  expect(pauseResumePlayer.getPositionInSamples() == draggedPausedPosition,
         "paused seek should update the stored playback position");

  pauseResumePlayer.seekTo(draggedPausedPosition, true);
  pauseResumePlayer.setPlaying(true);
  juce::MidiBuffer pauseResumeChase;
  pauseResumePlayer.processBlock(pauseResumeChase, 128);
  expect(findControllerIndex(pauseResumeChase, 1, 64, 127) < 0,
         "resume should keep pedal restore out of the reset block");
  juce::MidiBuffer pauseResumePedals;
  pauseResumePlayer.processBlock(pauseResumePedals, 128);
  expect(findControllerSamplePosition(pauseResumePedals, 1, 64, 127) == 0,
         "resume should restore the sustained CC64 state before playback continues");
  expect(findNoteSamplePosition(pauseResumePedals, true, 1, 65) < 0,
         "resume should not retrigger the old note");
  expect(pauseResumePlayer.getPositionInSamples() == draggedPausedPosition,
         "resume reconstruction should preserve the seek target");

  pauseResumePlayer.setPlaying(false);
  pauseResumePlayer.seekTo(750.0);
  expect(pauseResumePlayer.getPositionInSamples() == 750.0,
         "a pending paused seek must take precedence over the last rendered cursor");
  pauseResumePlayer.seekTo(pauseResumePlayer.getPositionInSamples(), true);
  pauseResumePlayer.setPlaying(true);
  juce::MidiBuffer pendingResumeChase;
  pauseResumePlayer.processBlock(pendingResumeChase, 128);
  expect(pauseResumePlayer.getPositionInSamples() == 750.0,
         "resume before the next render block must keep the pending seek target");

  pauseResumePlayer.setPlaying(false);
  pauseResumePlayer.seekTo(750.0);
  pauseResumePlayer.setSampleRate(2000.0);
  expect(pauseResumePlayer.getPositionInSamples() == 1500.0,
         "a sample rate change must rescale the pending seek rather than the old cursor");
  pauseResumePlayer.seekTo(0.0);
  pauseResumePlayer.seekTo(pauseResumePlayer.getPositionInSamples(), true);
  pauseResumePlayer.setPlaying(true);
  juce::MidiBuffer restartCleanup, restartedPlayback;
  pauseResumePlayer.processBlock(restartCleanup, 128);
  pauseResumePlayer.processBlock(restartedPlayback, 128);
  expect(findNoteSamplePosition(restartedPlayback, true, 1, 65) == 0 &&
             pauseResumePlayer.getPositionInSamples() == 128.0,
         "stop and play in the same render interval must restart at sample zero");

  MidiPlayer releasedNotePlayer;
  auto releasedNoteSequence = std::make_unique<juce::MidiMessageSequence>();
  addEvent(*releasedNoteSequence,
           juce::MidiMessage::noteOn(1, 63, (juce::uint8)100), 0.0);
  addEvent(*releasedNoteSequence, juce::MidiMessage::noteOff(1, 63), 0.05);
  addEvent(*releasedNoteSequence,
           juce::MidiMessage::controllerEvent(1, 1, 0), 1.0);
  releasedNotePlayer.setSequence(std::move(releasedNoteSequence), 1000.0);
  releasedNotePlayer.setPlaying(true);
  juce::MidiBuffer releasedNotePlayback;
  releasedNotePlayer.processBlock(releasedNotePlayback, 128);
  releasedNotePlayer.setPlaying(false);
  releasedNotePlayer.triggerStopCleanup();
  juce::MidiBuffer releasedNoteCleanup;
  releasedNotePlayer.processBlock(releasedNoteCleanup, 128);
  expect(!containsNoteOff(releasedNoteCleanup, 1, 63),
         "cleanup should not repeat a note-off already sent during playback");

  for (const auto mode : {MidiPlayer::ProcessMode::realtime, MidiPlayer::ProcessMode::offline}) {
    MidiPlayer tailPlayer;
    auto tailSequence = std::make_unique<juce::MidiMessageSequence>();
    addEvent(*tailSequence, juce::MidiMessage::noteOn(1, 63, (juce::uint8)100), 0.0);
    addEvent(*tailSequence, juce::MidiMessage::controllerEvent(1, 64, 127), 0.0);
    addEvent(*tailSequence, juce::MidiMessage::controllerEvent(2, 66, 127), 0.001);
    addEvent(*tailSequence, juce::MidiMessage::controllerEvent(2, 67, 36), 0.002);
    addEvent(*tailSequence, juce::MidiMessage::controllerEvent(3, 64, 127), 0.0);
    addEvent(*tailSequence, juce::MidiMessage::controllerEvent(3, 121, 0), 0.002);
    addEvent(*tailSequence, juce::MidiMessage::noteOff(1, 63), 0.05);
    addEvent(*tailSequence, juce::MidiMessage::noteOn(1, 64, (juce::uint8)100), 0.128);
    tailPlayer.setSequence(std::move(tailSequence), 1000.0);
    tailPlayer.setPlaying(true);
    juce::MidiBuffer playback, lastEvent, release;
    tailPlayer.processBlock(playback, 128, mode);
    expect(!tailPlayer.isWaitingForTail(),
           "a final event on a render block boundary must remain pending");
    tailPlayer.processBlock(lastEvent, 1, mode);
    expect(findNoteSamplePosition(lastEvent, true, 1, 64) == 0 && !tailPlayer.isWaitingForTail(),
           "tail release must follow the note-on at the end of the sequence");
    tailPlayer.processBlock(release, 128, mode);
    expect(findNoteSamplePosition(release, false, 1, 64) == 0 &&
               containsController(release, 1, 64, 0) &&
               containsController(release, 2, 66, 0) &&
               containsController(release, 2, 67, 0) &&
               release.getNumEvents() == 4 && !tailPlayer.consumeCleanupOccurred(),
           "natural end should release only held keys and pedals without cutting the tail");
    expect(tailPlayer.isWaitingForTail() && !tailPlayer.hasFinished(),
           "track completion should wait for the measured plugin tail");
    if (mode == MidiPlayer::ProcessMode::realtime) {
      tailPlayer.seekTo(50.0);
      expect(!tailPlayer.finishTail() && tailPlayer.getPlaying(),
             "a stale tail completion must not cancel a newer seek");
    } else {
      expect(tailPlayer.finishTail() && tailPlayer.hasFinished() && !tailPlayer.hasFinished(),
             "tail completion should publish the natural track end once");
    }
  }

  MidiPlayer sysExPlayer;
  auto sysExSequence = std::make_unique<juce::MidiMessageSequence>();
  const juce::uint8 sysExData[] = {0x41, 0x10, 0x42, 0x12, 0x00, 0x00, 0x7f};
  addEvent(*sysExSequence,
           juce::MidiMessage::createSysExMessage(sysExData,
                                                 sizeof(sysExData)),
           0.0);
  addEvent(*sysExSequence, juce::MidiMessage::noteOn(1, 60, (juce::uint8)100),
           0.1);
  sysExPlayer.setSequence(std::move(sysExSequence), 1000.0);
  sysExPlayer.setPlaying(true);
  juce::MidiBuffer sysExOutput;
  sysExPlayer.processBlock(sysExOutput, 128);
  bool foundSysEx = false;
  for (const auto metadata : sysExOutput)
    foundSysEx = foundSysEx || metadata.getMessage().isSysEx();
  expect(foundSysEx,
         "MIDI playback should preserve GS/XG system-exclusive messages");

  return failures;
}

} // namespace miditest
