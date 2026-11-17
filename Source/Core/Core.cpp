#include "CoreImpl.h"
#include <unordered_map>

namespace midi {

namespace {

std::wstring toW(const juce::String &value) {
  return std::wstring(value.toWideCharPointer());
}

juce::String fromW(const std::wstring &value) {
  return juce::String(value.c_str());
}

juce::File fileFromPath(const std::wstring &path) {
  return juce::File(fromW(path));
}

std::wstring pathToW(const juce::File &file) {
  return toW(file.getFullPathName());
}

PlaylistEntry toPlaylistEntry(const PlaylistManager::Track &track) {
  return {track.id, toW(track.name), pathToW(track.file), track.available};
}

} // namespace

// ---- shared helpers ----

AppState Core::Impl::buildState() {
  StateLock lock(stateMutex);
  AppState s;

  s.plugin.scanning = audioSession.isScanning();
  s.task.audioChangeInProgress = audioSession.isChangingAudio();
  s.task.commandActive = audioSession.hasControlTask();
  s.plugin.loaded = engine.hasPluginLoaded();
  s.plugin.loadedName = toW(engine.getLoadedPluginName());
  s.plugin.workerCrashed = workerCrashed();

  auto &mp = engine.getMidiPlayer();
  s.transport.playing = mp.getPlaying();
  s.transport.hasSequence = mp.hasSequence();
  const double sampleRate = mp.getSequenceSampleRate();
  const double duration = mp.getDurationInSamples();
  s.transport.positionSeconds = juce::jmin(mp.getPositionInSamples(), duration) / sampleRate;
  s.transport.durationSeconds = duration / sampleRate;
  s.transport.currentTrackIndex = currentTrackIndex();
  s.transport.currentMidiName = toW(loadedTrack.source.getFileNameWithoutExtension());
  s.transport.lastError = toW(midiErrorText);
  s.transport.skippedTracks = midiSkippedTracks;
  s.transport.skipRevision = midiSkipRevision;

  s.playlist = buildPlaylistSummary();

  s.audio.hasDevice = audio.hasDevice();
  s.audio.firstRunAudio = audio.isFirstRun();
  s.audio.deviceFallback = audio.wasRestoredWithFallback();
  s.audio.masterVolume = engine.getMasterVolume();
  s.audio.underrunCount = engine.getUnderrunCount();
  s.audio.renderLatencySamples = engine.getRenderLatencySamples();
  s.audio.lastInitError = toW(audio.lastError());
  s.audio.outputNeedsPreparation = engine.requiresPrepare();
  if (s.audio.outputNeedsPreparation)
    s.audio.preparationError = toW(audioSession.deviceError(engine.configurationRevision()));

  s.task.exportActive = audioSession.isExporting();

  return s;
}

PlaylistState Core::Impl::buildPlaylistState() {
  StateLock lock(stateMutex);
  PlaylistState result;
  static_cast<PlaylistSummary &>(result) = buildPlaylistSummary();
  result.currentTrackIndex = currentTrackIndex();
  result.tracks.reserve(static_cast<size_t>(playlist.size()));
  for (const auto &track : playlist.getTracks())
    result.tracks.push_back(toPlaylistEntry(track));
  return result;
}

PlaylistState Core::Impl::buildExportPlaylistState() {
  struct FileCheck {
    juce::File file;
    uint64_t observation;
    bool present = false;
  };
  std::unordered_map<TrackId, FileCheck> checks;
  {
    StateLock lock(stateMutex);
    for (const auto &track : playlist.getTracks())
      if (track.available)
        checks.emplace(track.id, FileCheck{track.file, track.availabilityObservation});
  }

  // Concurrency: 慢盘和网络路径的文件探测不持有核心锁。
  for (auto &entry : checks)
    entry.second.present = entry.second.file.existsAsFile();

  StateLock lock(stateMutex);
  PlaylistState result;
  result.tracks.reserve(checks.size());
  for (int index = 0; index < playlist.size(); ++index) {
    const auto &track = *playlist.getTrack(index);
    const auto checked = checks.find(track.id);
    if (checked == checks.end())
      continue;
    // Ordering: 保留探测期间完成的加载或刷新结果，即使可用性值相同。
    if (track.availabilityObservation == checked->second.observation)
      playlist.setTrackAvailable(index, checked->second.present);
    if (!track.available)
      continue;
    if (track.id == loadedTrack.id)
      result.currentTrackIndex = static_cast<int>(result.tracks.size());
    result.tracks.push_back(toPlaylistEntry(track));
  }
  static_cast<PlaylistSummary &>(result) = buildPlaylistSummary();
  return result;
}

PlaylistSummary Core::Impl::buildPlaylistSummary() const {
  return {playlist.hasChanges(), toW(playlist.getChangeSummary()),
          pathToW(currentPlaylistFile), toW(playlistErrorText),
          static_cast<int>(playlist.getPlaybackMode()),
          playlist.hasAvailableTracks(), playlist.getAvailabilityRevision()};
}

// ---- facade ----

Core::Core(UserSettings &settings) : impl(std::make_unique<Impl>(settings)) {}

Core::~Core() = default;

AppState Core::state() const { return impl->buildState(); }
PlaylistState Core::playlistState() const { return impl->buildPlaylistState(); }
PlaylistState Core::exportPlaylistState() const { return impl->buildExportPlaylistState(); }

void Core::setScrubbing(bool active) {
  impl->scrubbing = active;
}

// library
juce::Result Core::scan(std::function<bool()> shouldCancel) {
  return impl->scan(std::move(shouldCancel));
}
std::vector<PluginInfo> Core::plugins() const {
  Impl::StateLock lock(impl->stateMutex);
  std::vector<PluginInfo> result;
  for (const auto &type : impl->library.instruments())
    result.push_back({toW(type.createIdentifierString()), toW(type.name),
                      toW(type.manufacturerName), toW(type.pluginFormatName)});
  return result;
}
juce::Result Core::loadAsync(const PluginId &id, CommandCompletion completion) {
  return impl->loadAsync(id, std::move(completion));
}
juce::Result Core::unloadAsync(CommandCompletion completion) {
  return impl->unloadAsync(std::move(completion));
}
juce::Result Core::editorAsync(CommandCompletion completion) {
  return impl->editorAsync(std::move(completion));
}
void Core::closeEditor() { impl->closeEditor(); }
bool Core::hasPluginLoaded() const { return impl->engine.hasPluginLoaded(); }
std::wstring Core::loadedPluginName() const {
  return toW(impl->engine.getLoadedPluginName());
}
bool Core::workerCrashed() const {
  Impl::StateLock lock(impl->stateMutex);
  return impl->workerCrashed();
}
std::wstring Core::pluginError() const {
  Impl::StateLock lock(impl->stateMutex);
  const auto &failure = impl->audioSession.workerFailure();
  return toW(failure ? *failure : impl->engine.getPluginWorkerError());
}
void Core::cancelPendingPluginOperation() {
  impl->engine.cancelPendingPluginOperation();
}

// player
bool Core::openMidi(const std::wstring &path, std::function<void(bool)> completion) {
  Impl::StateLock lock(impl->stateMutex);
  return impl->beginMidiLoad({{fileFromPath(path)}}, std::move(completion));
}
bool Core::openMidiFromShell(const std::wstring &path,
                             std::function<void()> onPluginMissing,
                             std::function<void(bool)> completion) {
  Impl::StateLock lock(impl->stateMutex);
  return impl->beginMidiLoad({{fileFromPath(path)}},
                             std::move(completion), std::move(onPluginMissing));
}
void Core::play() { impl->play(); }
void Core::pause() { impl->pause(); }
void Core::togglePlay() { impl->togglePlay(); }
void Core::stop() { impl->stop(); }
void Core::next() { impl->next(); }
void Core::prev() { impl->prev(); }
void Core::playTrack(TrackId id) {
  Impl::StateLock lock(impl->stateMutex);
  if (!impl->audioSession.isIdle() ||
      !impl->audio.hasDevice() || !impl->engine.hasPluginLoaded() ||
      impl->engine.requiresPrepare())
    return;
  const int index = impl->playlist.findTrackIndex(id);
  if (const auto *track = impl->playlist.getTrack(index))
    impl->beginMidiLoad({{track->file, index}});
}
void Core::seek(double ratio) { impl->seek(ratio); }
void Core::volume(float value) { impl->volume(value); }
int Core::currentTrackIndex() const {
  Impl::StateLock lock(impl->stateMutex);
  return impl->currentTrackIndex();
}

// playlist
juce::Result Core::addToPlaylist(const std::wstring &path) {
  return impl->addToPlaylist(fileFromPath(path));
}
int Core::addFilesToPlaylist(const std::vector<std::wstring> &paths) {
  std::vector<juce::File> files;
  files.reserve(paths.size());
  for (const auto &path : paths)
    files.push_back(fileFromPath(path));
  return impl->addFilesToPlaylist(files);
}
bool Core::removeTrack(TrackId id) { return impl->removeTrack(id); }
bool Core::moveTrack(TrackId id, int toIndex) {
  return impl->moveTrack(id, toIndex);
}
juce::Result Core::refreshTrack(int index) { return impl->refreshTrack(index); }
int Core::findTrackIndex(const std::wstring &path) const {
  Impl::StateLock lock(impl->stateMutex);
  return impl->playlist.findTrackIndex(fileFromPath(path));
}
bool Core::clearPlaylist() { return impl->clearPlaylist(); }
void Core::setPlayMode(int mode) { impl->setPlayMode(mode); }
std::wstring Core::trackFile(TrackId id) const {
  Impl::StateLock lock(impl->stateMutex);
  if (const auto *track = impl->playlist.getTrack(impl->playlist.findTrackIndex(id)))
    return pathToW(track->file);
  return {};
}
bool Core::saveList(const std::wstring &path) {
  return impl->saveList(fileFromPath(path));
}
bool Core::loadList(const std::wstring &path) {
  return impl->loadList(fileFromPath(path));
}
std::wstring Core::lastPlaylistError() const {
  Impl::StateLock lock(impl->stateMutex);
  return toW(impl->playlistErrorText);
}

// audio device
bool Core::hasAudioDevice() const { return impl->audio.hasDevice(); }
bool Core::isFirstRunAudio() const { return impl->audio.isFirstRun(); }
bool Core::wasDeviceRestoredWithFallback() const {
  return impl->audio.wasRestoredWithFallback();
}

} // namespace midi
