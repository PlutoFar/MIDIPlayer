#include "TestHarness.h"

#include "Playlist/PlaylistManager.h"
#include "Utils/UserSettings.h"

namespace miditest {

int runPersistenceTests() {
  int failures = 0;
  const auto tempDir =
      juce::File::getSpecialLocation(juce::File::tempDirectory)
          .getChildFile("midi-player-persistence-tests-" +
                        juce::Uuid().toString());
  expect(failures, tempDir.createDirectory(),
         "persistence test directory should exist");

  const auto first = tempDir.getChildFile("first.mid");
  const auto missing = tempDir.getChildFile("offline-network-track.midi");
  first.replaceWithText("MThd");
  missing.replaceWithText("MThd");

  PlaylistManager source;
  expect(failures, source.addFile(first),
         "playlist should accept an available MIDI file");
  expect(failures, source.addFile(missing),
         "playlist should accept a second available MIDI file");
  const auto playlistFile = tempDir.getChildFile("library.json");
  expect(failures, source.saveDetailed(playlistFile).wasOk(),
         "playlist JSON should save atomically");
  expect(failures, missing.deleteFile(),
         "test should make one persisted track unavailable");

  PlaylistManager restored;
  expect(failures, restored.loadDetailed(playlistFile).wasOk(),
         "playlist JSON should load when a track is unavailable");
  expect(failures, restored.size() == 2,
         "playlist load should preserve unavailable track entries");
  const auto *offlineTrack = restored.getTrack(1);
  expect(failures,
         offlineTrack != nullptr && offlineTrack->file == missing &&
             !offlineTrack->available,
         "playlist should preserve the original path and availability state");
  expect(failures, !restored.hasChanges(),
         "loaded playlist should start without unsaved changes");

  missing.replaceWithText("MThd");
  expect(failures, restored.refreshTrack(1),
         "refresh should recover a track after its path becomes available");
  expect(failures,
         restored.getTrack(1) != nullptr && restored.getTrack(1)->available,
         "refresh should publish recovered availability");

  const auto malformed = tempDir.getChildFile("malformed.json");
  malformed.replaceWithText("{\"version\":1,\"tracks\":[42]}");
  expect(failures, restored.loadDetailed(malformed).failed(),
         "playlist should reject non-string track entries");
  expect(failures, restored.size() == 2,
         "failed playlist load should preserve the existing list");

  const auto settingsFile = tempDir.getChildFile("Settings.xml");
  settingsFile.replaceWithText("<broken");
  {
    UserSettings settings(settingsFile);
    expect(failures, !settingsFile.existsAsFile(),
           "corrupt settings should be moved before defaults are used");
    expect(failures, settings.getLastLoadError().contains("quarantined"),
           "settings should expose the corrupt-file quarantine result");
    expect(failures, settings.getMasterVolume() == 0.8f,
           "settings should use defaults after corrupt input is quarantined");
  }

  juce::Array<juce::File> quarantined;
  tempDir.findChildFiles(quarantined, juce::File::findFiles, false,
                         "Settings.xml.corrupt-*");
  expect(failures, quarantined.size() == 1,
         "settings quarantine should retain exactly one recoverable source file");
  expect(failures, settingsFile.existsAsFile(),
         "settings destructor should save fresh defaults after quarantine");

  {
    UserSettings settings(settingsFile);
    expect(failures, settings.getUIFontSize() == 14.0f,
           "UI font size should default to 14");
    expect(failures, settings.getLegacyUIFontSize() == 16.0f,
           "Legacy UI font size should migrate the old default to 16");
    expect(failures,
           settings.getDialogMaterialType() == WindowMaterial::Type::Acrylic &&
               settings.getDialogMaterialOpacity() == 0.78f &&
               settings.getDialogMaterialStrength() == 24,
           "dialog materials should default to Acrylic with balanced values");
    settings.setUIFontSize(30.0f);
    expect(failures, settings.getUIFontSize() == 20.0f,
           "UI font size should clamp to the supported maximum");
    settings.setUIFontSize(18.0f);
    settings.setDialogMaterialType(WindowMaterial::Type::FrostedGlass);
    settings.setDialogMaterialOpacity(0.1f);
    settings.setDialogMaterialStrength(80);
    expect(failures, settings.save(), "UI font size should save");
  }
  {
    UserSettings settings(settingsFile);
    expect(failures, settings.getUIFontSize() == 18.0f,
           "UI font size should persist");
    expect(failures, settings.getLegacyUIFontSize() == 18.0f,
           "Legacy UI font size should preserve a larger migrated value");
    expect(failures,
           settings.getDialogMaterialType() ==
                   WindowMaterial::Type::FrostedGlass &&
               settings.getDialogMaterialOpacity() == 0.25f &&
               settings.getDialogMaterialStrength() == 50,
           "dialog material settings should persist with range clamping");
    settings.setLegacyUIFontSize(30.0f);
    expect(failures, settings.getLegacyUIFontSize() == 22.0f,
           "Legacy UI font size should clamp to the supported maximum");
    settings.setUIFontSize(8.0f);
    expect(failures, settings.getUIFontSize() == 12.0f,
           "UI font size should clamp to the supported minimum");
    expect(failures, settings.save(), "Legacy UI font size should save");
  }
  {
    UserSettings settings(settingsFile);
    expect(failures, settings.getLegacyUIFontSize() == 22.0f,
           "Legacy UI font size should persist independently");
    settings.setLegacyUIFontSize(8.0f);
    expect(failures, settings.getLegacyUIFontSize() == 14.0f,
           "Legacy UI font size should clamp to the supported minimum");
  }

  tempDir.deleteRecursively();
  return failures;
}

} // namespace miditest
