#include "TestHarness.h"
#include "AudioEngine/OfflineRenderer.h"
#include "Core/Core.h"
#include "Utils/UserSettings.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace miditest {
namespace {

std::unique_ptr<juce::MidiMessageSequence> sequence(double onset, double end) {
  auto result = std::make_unique<juce::MidiMessageSequence>();
  auto noteOn = juce::MidiMessage::noteOn(1, 60, (juce::uint8)100);
  noteOn.setTimeStamp(onset);
  auto noteOff = juce::MidiMessage::noteOff(1, 60);
  noteOff.setTimeStamp(end);
  result->addEvent(noteOn);
  result->addEvent(noteOff);
  result->updateMatchedPairs();
  return result;
}

} // namespace

int runExportTaskTests() {
  int failures = 0;
  const TestDirectory directory;
  const auto output = directory.path.getChildFile(juce::String(L"导出.wav"));
  ExportSettings settings;
  settings.sampleRate = 48000;
  settings.bitDepth = 32;
  settings.useFloatingPoint = true;
  settings.autoTail = false;
  settings.fixedTailSeconds = 0;

  {
    UserSettings preferences(directory.path.getChildFile("Settings.xml"));
    preferences.finishPersistence();
    midi::Core core(preferences);
    midi::Core::ExportRequest request{
        directory.path.getChildFile("source.mid").getFullPathName().toWideCharPointer(),
        output.getFullPathName().toWideCharPointer(), settings};
    bool progressCalled = false;
    const auto rejected = core.runExport(request, [&](float) { progressCalled = true; });
    expect(failures, rejected.status == ExportStatus::failed &&
                         !rejected.targetWritten && rejected.error.isNotEmpty() &&
                         !progressCalled && !core.state().task.exportActive,
           "a rejected export should report its own error without starting");
  }

  AudioEngine engine;
  auto &live = engine.getMidiPlayer();
  live.setSequence(sequence(0.1, 2.0), 44100.0);
  juce::PluginDescription testInstrument;
  testInstrument.name = "Impulse test instrument";
  testInstrument.pluginFormatName = "VST3";
  testInstrument.fileOrIdentifier = "test-instrument.vst3";
  if (!engine.loadPlugin(testInstrument))
    throw std::runtime_error(engine.getLastPluginError().toStdString());

  {
    AudioEngine::OfflineExportSession session(
        engine, sequence(0.05, 0.125), settings.sampleRate);
    if (const auto result = session.preparationResult(); result.failed())
      throw std::runtime_error(result.getErrorMessage().toStdString());
    bool cancelled = false;
    const auto written = renderOfflineExport(
        session, output, settings,
        [&](float progress) { cancelled = progress == 1.0f; },
        [&] { return cancelled; });
    expect(failures, cancelled && written.status == ExportStatus::succeeded &&
                         written.targetWritten && output.existsAsFile(),
           "cancellation after file commit should preserve success");
    expect(failures, live.getSequenceSampleRate() == 44100.0 &&
                         live.getDurationInSamples() == 88200.0 && !live.getPlaying(),
           "offline playback must leave the live sequence and cursor mode intact");
    expect(failures, session.finish().wasOk(),
           "a completed export should restore the realtime plugin session");
  }

  juce::AudioFormatManager formats;
  formats.registerBasicFormats();
  std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(output));
  expect(failures, reader != nullptr && reader->sampleRate == 48000 &&
                       reader->lengthInSamples == 6000,
         "export should encode the selected sequence at its own rate and duration");
  if (reader != nullptr) {
    juce::AudioBuffer<float> samples(2, 6000);
    expect(failures, reader->read(&samples, 0, 6000, 0, true, true) &&
                         samples.getMagnitude(0, 2400) == 0.0f &&
                         std::abs(samples.getSample(0, 2400) - 0.4f) < 0.0001f,
           "the MIDI note should appear at the exact sample in the exported WAV");
  }
  reader.reset();

  const auto prior = output.getSiblingFile("prior.wav");
  if (!output.copyFileTo(prior))
    throw std::runtime_error("Could not preserve the first export for comparison");
  {
    AudioEngine::OfflineExportSession session(
        engine, sequence(0.02, 0.25), settings.sampleRate);
    if (const auto result = session.preparationResult(); result.failed())
      throw std::runtime_error(result.getErrorMessage().toStdString());
    const auto cancelled = renderOfflineExport(session, output, settings, {}, [] {
      return true;
    });
    expect(failures, cancelled.status == ExportStatus::cancelled &&
                         !cancelled.targetWritten && output.hasIdenticalContentTo(prior),
           "cancellation before commit should preserve an existing target");
    expect(failures, session.finish().wasOk(),
           "cancellation should restore the realtime plugin session");
  }
  return failures;
}

} // namespace miditest
