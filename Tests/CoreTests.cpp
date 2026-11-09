#include "TestHarness.h"
#include "Core/Core.h"
#include "Utils/UserSettings.h"
#include <optional>

namespace miditest {
namespace {

std::wstring pathOf(const juce::File &file) {
  return file.getFullPathName().toWideCharPointer();
}

bool writeMidi(const juce::File &file) {
  juce::MidiMessageSequence track;
  track.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
  track.addEvent(juce::MidiMessage::noteOff(1, 60), 480.0);
  juce::MidiFile midi;
  midi.setTicksPerQuarterNote(480);
  midi.addTrack(track);
  auto stream = file.createOutputStream();
  return stream != nullptr && midi.writeTo(*stream);
}

std::optional<bool> openAndWait(midi::Core &core, const juce::File &file) {
  auto result = std::make_shared<std::optional<bool>>();
  if (!core.openMidi(pathOf(file), [result](bool loaded) { *result = loaded; }))
    return std::nullopt;
  const auto started = juce::Time::getMillisecondCounter();
  while (!result->has_value() &&
         juce::Time::getMillisecondCounter() - started < 5000)
    juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
  return *result;
}

} // namespace

int runCoreTests() {
  int failures = 0;
  const TestDirectory directory;
  UserSettings settings(directory.path.getChildFile("Settings.xml"));
  settings.finishPersistence();
  midi::Core core(settings);
  const auto first = directory.path.getChildFile(juce::String(L"曲目一.mid"));
  const auto second = directory.path.getChildFile(juce::String(L"曲目二.mid"));
  const auto third = directory.path.getChildFile(juce::String(L"曲目三.mid"));
  if (!writeMidi(first) || !writeMidi(second) || !writeMidi(third))
    throw std::runtime_error("Could not create MIDI fixtures");

  expect(failures, core.addFilesToPlaylist({pathOf(first), pathOf(second),
                                          pathOf(third), pathOf(second)}) == 3,
         "playlist should reject duplicate additions");
  const auto initial = core.playlistState();
  if (initial.tracks.size() != 3)
    return failures + 1;
  const auto firstId = initial.tracks[0].id;
  const auto secondId = initial.tracks[1].id;
  expect(failures, openAndWait(core, second) == true,
         "a valid MIDI should publish through the background command");
  expect(failures, core.state().transport.hasSequence &&
                       core.playlistState().currentTrackIndex == 1,
         "loading should publish sequence and playlist selection");
  core.play();
  expect(failures, !core.state().transport.playing,
         "playback should require an instrument");

  expect(failures, core.moveTrack(firstId, 2) && core.removeTrack(firstId),
         "a delete issued with the original identity should survive reorder");
  auto snapshot = core.playlistState();
  expect(failures, snapshot.tracks.size() == 2 &&
                       snapshot.tracks[0].id == secondId &&
                       snapshot.currentTrackIndex == 0,
         "reorder and deletion should preserve the current track");

  const auto invalid = directory.path.getChildFile("invalid.mid");
  if (!invalid.replaceWithText("broken MIDI"))
    throw std::runtime_error("Could not create invalid MIDI fixture");
  expect(failures, openAndWait(core, invalid) == false,
         "an invalid MIDI should complete with failure");
  const auto afterFailure = core.state();
  expect(failures, afterFailure.transport.hasSequence &&
                       afterFailure.transport.currentTrackIndex == 0 &&
                       !afterFailure.transport.lastError.empty() && core.pluginError().empty(),
         "MIDI errors should preserve the loaded selection and stay in transport");

  const auto playlistFile = directory.path.getChildFile("playlist.json");
  expect(failures, core.saveList(pathOf(playlistFile)) &&
                       !core.playlistState().hasUnsavedChanges,
         "successful save should publish the saved playlist state");
  expect(failures, core.loadList(pathOf(playlistFile)) &&
                       !core.removeTrack(secondId),
         "an identity from the previous list should never delete a reloaded row");
  snapshot = core.playlistState();
  if (snapshot.tracks.empty())
    return failures + 1;
  expect(failures, core.removeTrack(snapshot.tracks[0].id) &&
                       !core.state().transport.hasSequence,
         "removing the current track should clear its playback sequence");
  return failures;
}

} // namespace miditest
