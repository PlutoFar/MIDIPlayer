#pragma once

#include "Core.h"

#include "../AudioEngine/AudioDeviceService.h"
#include "../AudioEngine/AudioEngine.h"
#include "../AudioEngine/OfflineRenderer.h"
#include "../AudioEngine/PluginLibrary.h"
#include "../Playlist/PlaylistManager.h"
#include "../Utils/UserSettings.h"

#include <atomic>
#include <functional>
#include <juce_events/juce_events.h>
#include <memory>
#include <mutex>
#include <thread>

namespace midi {

// Shared private state for midi::Core. The Core facade forwards to this; the
// per-module implementations (Library/Player/Playlist/ExportTask/Startup) are
// methods on Impl, split across their own .cpp files. Impl owns the single
// AudioEngine and PlaylistManager instances for the whole application.
struct Core::Impl : private juce::AsyncUpdater {
  using StateLock = std::lock_guard<std::recursive_mutex>;

  AudioEngine engine;
  AudioDeviceService audio{engine};
  PluginLibrary library;
  OfflineRenderer renderer{engine};
  PlaylistManager playlist;
  mutable std::recursive_mutex stateMutex;
  std::mutex exportMutex;
  std::atomic<bool> pluginScanActive{false};
  std::atomic<bool> pluginTaskActive{false};
  std::atomic<bool> pluginChangesAudio{false};
  std::atomic<bool> audioConfigurationActive{false};
  juce::String pluginErrorText;
  // Liveness token for delayed (juce::Timer) callbacks: expires on destruction
  // so stale scheduled lambdas become no-ops, mirroring the Component
  // SafePointer guard the Legacy UI used.
  std::shared_ptr<int> life{std::make_shared<int>(0)};

  // Playback / track-switch business state (moved out of MainContentComponent).
  int currentTrackIndex = -1;
  juce::File currentMidiFile;
  juce::String currentMidiName;
  juce::File currentPlaylistFile;
  juce::String playlistErrorText;
  int trackSwitchGeneration = 0;
  bool isHandlingTrackEnd = false;

  // Export task state (Core/ExportTask).
  std::atomic<bool> exportActiveFlag{false};
  std::atomic<float> exportProgressValue{0.0f};
  bool exportCancelledFlag = false;
  juce::String exportErrorText;

  ~Impl();

  // ---- shared helpers (Core.cpp) ----
  AppState buildState();
  PlaylistState buildPlaylistState();
  bool startPluginTask(std::function<bool()> operation,
                       std::function<void(bool)> completion,
                       bool changesAudio = true);
  void handleAsyncUpdate() override;
  std::thread pluginTask;
  std::function<void(bool)> pluginCompletion;
  bool pluginTaskSucceeded = false;
  bool closeEditorWhenIdle = false;
  juce::String configureAudio(std::function<juce::String()> operation);
  void scheduleAfter(int ms, std::function<void(Impl &)> fn);
  double sampleRate() const;

  // ---- Library (Library.cpp) ----
  bool scan(std::function<bool()> shouldCancel);
  bool findById(const PluginId &id, juce::PluginDescription &out);
  bool loadAsync(const PluginId &id, std::function<void(bool)> completion);
  bool unloadAsync(std::function<void(bool)> completion);
  bool editorAsync(std::function<void(bool)> completion);
  void closeEditor();
  void terminateCrashedWorker();

  // ---- Player (Player.cpp) ----
  bool canStartPlayback();
  bool loadMidi(const juce::File &file);
  void play();
  void pause();
  void togglePlay();
  void stop();
  void seek(double ratio);
  void volume(float value);
  bool openMidi(const juce::File &file, bool autoLoadPluginIfMissing,
                std::function<void()> onPluginMissing);
  void next();
  void prev();
  void handleTrackEnd();
  void tick(bool uiSuppressTrackAdvance);

  // ---- Playlist (Playlist.cpp) ----
  bool addToPlaylist(const juce::File &file);
  int addFilesToPlaylist(const std::vector<juce::File> &files);
  bool removeTrack(int index);
  bool moveTrack(int fromIndex, int toIndex);
  bool refreshTrack(int index);
  bool clearPlaylist();
  void setPlayMode(int mode);
  juce::File trackFileAt(int index) const;
  bool saveList(const juce::File &file);
  bool loadList(const juce::File &file);

  // ---- ExportTask (ExportTask.cpp) ----
  // Runs the offline export synchronously on the calling (worker) thread,
  // driving AudioEngine and reporting progress / honouring cancel. The modal
  // progress window stays in the UI layer, which supplies the callbacks. The
  // playback state capture/restore around the render is handled here.
  struct ExportPlaybackState {
    int trackIndex = -1;
    juce::File file;
    double positionSeconds = 0.0;
    bool wasPlaying = false;
  };
  ExportPlaybackState captureExportPlaybackState();
  juce::Result restoreExportPlaybackState(const ExportPlaybackState &state);
  Core::ExportResult runExport(int trackIndex, const ExportSettings &settings,
                               const juce::File &targetFile,
                               std::function<void(float)> onProgress,
                               std::function<bool()> shouldCancel);
};

} // namespace midi
