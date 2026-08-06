#pragma once

#include "Core.h"

#include "../AudioEngine/AudioEngine.h"
#include "../Playlist/PlaylistManager.h"
#include "../Utils/UserSettings.h"

#include <atomic>
#include <functional>
#include <juce_events/juce_events.h>
#include <memory>
#include <mutex>

namespace midi {

// Shared private state for midi::Core. The Core facade forwards to this; the
// per-module implementations (Library/Player/Playlist/ExportTask/Startup) are
// methods on Impl, split across their own .cpp files. Impl owns the single
// AudioEngine and PlaylistManager instances for the whole application.
struct Core::Impl {
  using StateLock = std::lock_guard<std::recursive_mutex>;

  AudioEngine engine;
  PlaylistManager playlist;
  mutable std::recursive_mutex stateMutex;
  std::mutex pluginScanMutex;
  std::mutex exportMutex;
  std::atomic<bool> pluginScanActive{false};
  std::atomic<bool> pluginLoadActive{false};
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
  bool pendingResumePlayback = false;
  juce::uint32 lastSeekRequestMs = 0;

  // Export task state (Core/ExportTask).
  std::atomic<bool> exportActiveFlag{false};
  std::atomic<float> exportProgressValue{0.0f};
  bool exportCancelledFlag = false;
  juce::String exportErrorText;

  ~Impl() = default;

  // ---- shared helpers (Core.cpp) ----
  AppState buildState();
  void notify();
  void scheduleAfter(int ms, std::function<void(Impl &)> fn);
  double sampleRate() const;

  // ---- Library (Library.cpp) ----
  bool scan(std::function<bool()> shouldCancel);
  bool findById(const PluginId &id, juce::PluginDescription &out);
  bool load(const PluginId &id);
  void unload();
  bool editor();
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
  void clearPlaylist();
  void setPlayMode(int mode);
  juce::File trackFileAt(int index) const;
  bool saveList(const juce::File &file);
  bool loadList(const juce::File &file);

  // ---- Audio device (Core.cpp) ----
  std::vector<juce::String> audioOutputDevices();
  juce::String currentAudioDevice();
  bool setAudioDevice(const juce::String &name);
  std::vector<int> sampleRates();
  int currentSampleRate();
  bool setSampleRate(int sampleRate);
  std::vector<int> bufferSizes();
  int currentBufferSize();
  bool setBufferSize(int bufferSize);
  void playTestSound();
  juce::String audioStatus();

  // ---- ExportTask (ExportTask.cpp) ----
  // Runs the offline export synchronously on the calling (worker) thread,
  // driving AudioEngine and reporting progress / honouring cancel. The modal
  // progress window stays in the UI layer, which supplies the callbacks. The
  // playback state capture/restore around the render is handled here so both
  // front-ends get identical behaviour.
  struct ExportPlaybackState {
    int trackIndex = -1;
    juce::File file;
    double position = 0.0;
    bool wasPlaying = false;
    bool hadPendingResume = false;
    bool wasHandlingTrackEnd = false;
  };
  ExportPlaybackState captureExportPlaybackState();
  juce::Result restoreExportPlaybackState(const ExportPlaybackState &state);
  Core::ExportResult runExport(int trackIndex, const ExportSettings &settings,
                               const juce::File &targetFile,
                               std::function<void(float)> onProgress,
                               std::function<bool()> shouldCancel);
};

} // namespace midi
