#include "CoreImpl.h"

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

} // namespace

// ---- shared helpers ----

AppState Core::Impl::buildState() {
  StateLock lock(stateMutex);
  AppState s;

  s.plugin.scanning = pluginScanActive.load();
  s.task.audioChangeInProgress = commandChangesAudio.load() || audioConfigurationActive.load();
  s.task.commandActive = commandTaskActive.load() || pluginScanActive.load() ||
                         audioConfigurationActive.load();
  s.plugin.loaded = engine.hasPluginLoaded();
  s.plugin.loadedName = toW(engine.getLoadedPluginName());
  s.plugin.workerCrashed = engine.hasPluginWorkerCrashed();

  auto &mp = engine.getMidiPlayer();
  s.transport.playing = mp.getPlaying();
  s.transport.hasSequence = mp.hasSequence();
  const double sampleRate = mp.getSequenceSampleRate();
  const double duration = mp.getDurationInSamples();
  s.transport.positionSeconds = juce::jmin(mp.getPositionInSamples(), duration) / sampleRate;
  s.transport.durationSeconds = duration / sampleRate;
  s.transport.currentTrackIndex = currentTrackIndex;
  s.transport.currentMidiName = toW(currentMidiName);
  s.transport.lastError = toW(midiErrorText);

  s.playlist = buildPlaylistSummary();

  s.audio.hasDevice = audio.hasDevice();
  s.audio.firstRunAudio = audio.isFirstRun();
  s.audio.deviceFallback = audio.wasRestoredWithFallback();
  s.audio.masterVolume = engine.getMasterVolume();
  s.audio.underrunCount = engine.getUnderrunCount();
  s.audio.renderLatencySamples = engine.getRenderLatencySamples();
  s.audio.lastInitError = toW(audio.lastError());

  s.task.exportActive = exportActiveFlag.load();

  return s;
}

PlaylistState Core::Impl::buildPlaylistState() {
  StateLock lock(stateMutex);
  PlaylistState result;
  static_cast<PlaylistSummary &>(result) = buildPlaylistSummary();
  result.currentTrackIndex = currentTrackIndex;
  result.tracks.reserve(static_cast<size_t>(playlist.size()));
  for (const auto &track : playlist.getTracks())
    result.tracks.push_back({track.id, toW(track.name), pathToW(track.file),
                             track.available});
  return result;
}

PlaylistSummary Core::Impl::buildPlaylistSummary() const {
  return {playlist.hasChanges(), toW(playlist.getChangeSummary()),
          pathToW(currentPlaylistFile), toW(playlistErrorText),
          static_cast<int>(playlist.getPlaybackMode())};
}

// ---- facade ----

Core::Core(UserSettings &settings) : impl(std::make_unique<Impl>(settings)) {}

Core::~Core() = default;

AppState Core::state() const { return impl->buildState(); }
PlaylistState Core::playlistState() const { return impl->buildPlaylistState(); }

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
  for (const auto &type : impl->library.plugins().getTypes())
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
  return impl->engine.hasPluginWorkerCrashed();
}
std::wstring Core::pluginError() const {
  return toW(impl->engine.getPluginWorkerError());
}
void Core::terminateCrashedWorker() { impl->terminateCrashedWorker(); }
void Core::cancelPendingPluginOperation() {
  impl->engine.cancelPendingPluginOperation();
}

// player
bool Core::openMidi(const std::wstring &path, std::function<void(bool)> completion) {
  Impl::StateLock lock(impl->stateMutex);
  return impl->beginMidiLoad(fileFromPath(path), 0, std::move(completion));
}
bool Core::openMidiFromShell(const std::wstring &path,
                             std::function<void()> onPluginMissing,
                             std::function<void(bool)> completion) {
  Impl::StateLock lock(impl->stateMutex);
  return impl->beginMidiLoad(fileFromPath(path), 0,
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
  if (impl->exportActiveFlag.load() || impl->commandChangesAudio.load() ||
      !impl->engine.hasPluginLoaded() || impl->engine.hasPluginWorkerCrashed())
    return;
  if (const auto *track = impl->playlist.getTrack(impl->playlist.findTrackIndex(id)))
    impl->beginMidiLoad(track->file, track->id);
}
void Core::seek(double ratio) { impl->seek(ratio); }
void Core::volume(float value) { impl->volume(value); }
int Core::currentTrackIndex() const {
  Impl::StateLock lock(impl->stateMutex);
  return impl->currentTrackIndex;
}

// playlist
bool Core::addToPlaylist(const std::wstring &path) {
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
bool Core::refreshTrack(int index) { return impl->refreshTrack(index); }
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
