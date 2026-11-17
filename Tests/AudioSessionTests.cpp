#include "TestHarness.h"
#include "AudioEngine/AudioDeviceService.h"
#include "AudioEngine/AudioEngine.h"
#include "Core/AudioSession.h"

namespace miditest {

int runAudioSessionTests() {
  using Operation = midi::AudioSession::Operation;
  int failures = 0;
  midi::AudioSession session;
  expect(failures, session.begin(Operation::scanPlugins) &&
                       !session.begin(Operation::configureDevice) && session.isScanning(),
         "scanning should block audio configuration in either admission order");
  session.finish();
  expect(failures, session.begin(Operation::configureDevice) &&
                       !session.begin(Operation::scanPlugins) && session.isChangingAudio(),
         "configuration should retain exclusive admission until it finishes");
  session.finish();

  expect(failures, session.begin(Operation::exportAudio) &&
                       !session.allowsSequenceChange() &&
                       !session.begin(Operation::pluginEditor),
         "export should exclude playback mutation and editor requests");
  session.requestEditorClose();
  session.finish();
  expect(failures, session.takeEditorCloseRequest() && !session.takeEditorCloseRequest(),
         "the deferred editor close should be consumed once when the session is idle");

  constexpr unsigned failedRevision = 4;
  constexpr unsigned replacementRevision = 5;
  expect(failures, session.canPrepare(failedRevision) && session.begin(Operation::prepareDevice),
         "a new device revision should be admitted for preparation");
  session.preparationCompleted(failedRevision, juce::Result::fail("prepare rejected"));
  session.finish();
  expect(failures, !session.canPrepare(failedRevision) &&
                       session.deviceError(failedRevision) == "prepare rejected" &&
                       session.canPrepare(replacementRevision) &&
                       session.deviceError(replacementRevision).isEmpty(),
         "a failed revision should remain diagnosable without retrying until the device changes");
  session.preparationCompleted(replacementRevision, juce::Result::ok());
  expect(failures, session.deviceError(failedRevision).isEmpty(),
         "successful preparation should clear the previous diagnostic");

  {
    const TestDirectory directory;
    const auto savedDevice = directory.path.getChildFile("AudioDevice.xml");
    const juce::String missingOutput = "MIDI Player intentionally absent output";
    juce::XmlElement oldSettings("DEVICESETUP");
    oldSettings.setAttribute("deviceType", "Windows Audio");
    oldSettings.setAttribute("audioOutputDeviceName", missingOutput);
    expect(failures, oldSettings.writeTo(savedDevice),
           "the missing-device fixture should be written");
    AudioEngine deviceEngine;
    AudioDeviceService audio(deviceEngine, savedDevice);
    expect(failures, !audio.isFirstRun() &&
                         audio.wasRestoredWithFallback() == audio.hasDevice(),
           "saved-device failure should report fallback only when an output was opened");
    const auto persisted = juce::XmlDocument::parse(savedDevice);
    if (audio.hasDevice()) {
      const auto actual = audio.state(false);
      expect(failures, persisted != nullptr &&
                           actual.setup.outputDeviceName != missingOutput &&
                           persisted->getStringAttribute("audioOutputDeviceName") ==
                               actual.setup.outputDeviceName &&
                           persisted->getStringAttribute("deviceType") == actual.currentDriver,
             "fallback should persist the opened default output and driver");
    } else {
      expect(failures, audio.lastError().contains(missingOutput) &&
                           persisted != nullptr &&
                           persisted->getStringAttribute("audioOutputDeviceName") == missingOutput,
             "failed fallback should retain saved settings and report the unavailable output");
    }
  }

  AudioEngine engine;
  engine.prepareToPlay(44100, 512);
  juce::PluginDescription instrument;
  instrument.name = "Device recovery fixture";
  instrument.pluginFormatName = "VST3";
  instrument.isInstrument = true;
  instrument.fileOrIdentifier = "device-recovery-fixture.vst3";
  setTestEnvironmentVariable(L"MIDI_PLAYER_TEST_REJECTED_SAMPLE_RATE", L"88200");
  const bool loaded = engine.loadPlugin(instrument);
  clearTestEnvironmentVariable(L"MIDI_PLAYER_TEST_REJECTED_SAMPLE_RATE");
  if (!loaded)
    throw std::runtime_error(engine.getLastPluginError().toStdString());
  auto effect = instrument;
  effect.name = "EQ fixture";
  effect.category = "Fx|EQ";
  effect.isInstrument = false;
  expect(failures, !engine.loadPlugin(effect) && engine.hasPluginLoaded() &&
                       engine.getLoadedPluginName() == instrument.name,
         "rejecting a known effect should preserve the active instrument");
  engine.prepareToPlay(88200, 256);
  const auto rejected = engine.deviceConfiguration();
  expect(failures, !engine.preparePlugin(rejected) && engine.requiresPrepare() &&
                       engine.hasPluginLoaded(),
         "a rejected device configuration should preserve the plugin but leave the output unprepared");
  session.preparationCompleted(rejected.revision,
                               juce::Result::fail(engine.getLastPluginError()));
  expect(failures, !session.canPrepare(rejected.revision) &&
                       session.deviceError(rejected.revision).contains("preparation rejected"),
         "the real prepare command failure should block automatic retries for its revision");

  engine.prepareToPlay(48000, 256);
  const auto replacement = engine.deviceConfiguration();
  expect(failures, session.canPrepare(replacement.revision) &&
                       engine.preparePlugin(replacement) && !engine.requiresPrepare(),
         "a replacement device configuration should recover the realtime renderer");

  juce::MemoryBlock sysEx(PluginBridge::MidiPacketLimits::maxEventBytes - 1, true);
  auto oversized = juce::MidiMessage::createSysExMessage(
      sysEx.getData(), static_cast<int>(sysEx.getSize()));
  oversized.setTimeStamp(0.01);
  auto sequence = std::make_unique<juce::MidiMessageSequence>();
  sequence->addEvent(oversized);
  engine.getMidiPlayer().setSequence(std::move(sequence), 48000);
  engine.getMidiPlayer().setPlaying(true);
  juce::AudioBuffer<float> audio(2, 256);
  juce::MidiBuffer midi;
  bool rejectedEvent = false;
  const auto started = juce::Time::getMillisecondCounter();
  while (!rejectedEvent && juce::Time::getMillisecondCounter() - started < 3000) {
    engine.processBlock(audio, midi);
    juce::Thread::sleep(5);
    rejectedEvent = engine.takeRealtimeMidiEventRejection();
  }
  expect(failures, rejectedEvent && !engine.getMidiPlayer().getPlaying() &&
                       engine.hasPluginLoaded() && !engine.hasPluginWorkerCrashed(),
         "an oversized realtime MIDI event should stop playback and report a track error");
  expect(failures, !engine.takeRealtimeMidiEventRejection(),
         "the realtime MIDI error should be consumed once");
  return failures;
}

} // namespace miditest
