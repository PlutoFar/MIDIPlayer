#include "TestHarness.h"
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

  AudioEngine engine;
  engine.prepareToPlay(44100, 512);
  juce::PluginDescription instrument;
  instrument.name = "Device recovery fixture";
  instrument.pluginFormatName = "VST3";
  instrument.fileOrIdentifier = "device-recovery-fixture.vst3";
  setTestEnvironmentVariable(L"MIDI_PLAYER_TEST_REJECTED_SAMPLE_RATE", L"88200");
  const bool loaded = engine.loadPlugin(instrument);
  clearTestEnvironmentVariable(L"MIDI_PLAYER_TEST_REJECTED_SAMPLE_RATE");
  if (!loaded)
    throw std::runtime_error(engine.getLastPluginError().toStdString());
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
  return failures;
}

} // namespace miditest
