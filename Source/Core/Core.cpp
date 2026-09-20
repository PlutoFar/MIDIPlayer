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
  s.plugin.loadInProgress = pluginLoadActive.load();
  s.plugin.loaded = engine.hasPluginLoaded();
  s.plugin.loadedName = toW(engine.getLoadedPluginName());
  s.plugin.workerCrashed = engine.hasPluginWorkerCrashed();
  s.plugin.lastError = toW(engine.getLastPluginError());
  for (const auto &type : engine.getPluginList().getTypes())
    s.plugin.available.push_back(
        {toW(type.createIdentifierString()), toW(type.name),
         toW(type.manufacturerName), toW(type.pluginFormatName)});

  auto &mp = engine.getMidiPlayer();
  s.transport.playing = mp.getPlaying();
  s.transport.hasSequence = mp.hasSequence();
  s.transport.positionSamples = mp.getPositionInSamples();
  s.transport.durationSamples = mp.getDurationInSamples();
  s.transport.currentTrackIndex = currentTrackIndex;
  s.transport.currentMidiName = toW(currentMidiName);

  for (const auto &track : playlist.getTracks()) {
    s.playlist.trackNames.push_back(toW(track.name));
    s.playlist.trackAvailable.push_back(track.available);
  }
  s.playlist.hasUnsavedChanges = playlist.hasChanges();
  s.playlist.changeSummary = toW(playlist.getChangeSummary());
  s.playlist.currentListPath = pathToW(currentPlaylistFile);
  s.playlist.lastError = toW(playlistErrorText);
  s.playlist.playMode = static_cast<int>(playlist.getPlaybackMode());

  s.audio.hasDevice = engine.hasAudioDevice();
  s.audio.firstRunAudio = engine.isFirstRunAudio();
  s.audio.deviceFallback = engine.wasDeviceRestoredWithFallback();
  s.audio.masterVolume = engine.getMasterVolume();
  s.audio.lastInitError = toW(engine.getLastInitError());

  s.task.exportActive = exportActiveFlag.load();
  s.task.exportProgress = exportProgressValue.load();
  s.task.exportCancelled = exportCancelledFlag;
  s.task.exportError = toW(exportErrorText);

  return s;
}

void Core::Impl::notify() {
  // JUCE 界面按帧读取状态快照。
}

void Core::Impl::scheduleAfter(int ms, std::function<void(Impl &)> fn) {
  std::weak_ptr<int> token = life;
  juce::Timer::callAfterDelay(ms, [this, token, fn = std::move(fn)]() mutable {
    if (token.expired())
      return;
    StateLock lock(stateMutex);
    fn(*this);
  });
}

double Core::Impl::sampleRate() const {
  StateLock lock(stateMutex);
  const double sr = engine.getSampleRate();
  return sr > 0.0 ? sr : 44100.0;
}

// ---- facade ----

Core::Core() : impl(std::make_unique<Impl>()) {}

Core::~Core() { shutdown(); }

bool Core::init() {
  return true;
}

void Core::shutdown() {}

AppState Core::state() const { return impl->buildState(); }

void Core::tick(bool uiSuppressTrackAdvance) {
  impl->tick(uiSuppressTrackAdvance);
}

// library
bool Core::scan(std::function<bool()> shouldCancel) {
  return impl->scan(std::move(shouldCancel));
}
std::vector<PluginInfo> Core::plugins() const { return state().plugin.available; }
bool Core::load(const PluginId &id) { return impl->load(id); }
void Core::unload() { impl->unload(); }
bool Core::editor() { return impl->editor(); }
void Core::closeEditor() {
  if (!impl->exportActiveFlag.load() && !impl->pluginLoadActive.load())
    impl->engine.closePluginEditor();
}
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
std::wstring Core::lastPluginError() const {
  return toW(impl->engine.getLastPluginError());
}
void Core::terminateCrashedWorker() { impl->terminateCrashedWorker(); }
void Core::cancelPendingPluginOperation() {
  impl->engine.cancelPendingPluginOperation();
}

// player
bool Core::openMidi(const std::wstring &path) {
  return impl->openMidi(fileFromPath(path), false, {});
}
bool Core::openMidiFromShell(const std::wstring &path,
                            std::function<void()> onPluginMissing) {
  return impl->openMidi(fileFromPath(path), true, std::move(onPluginMissing));
}
bool Core::loadMidiFile(const std::wstring &path) {
  Impl::StateLock lock(impl->stateMutex);
  if (impl->exportActiveFlag.load() || impl->pluginLoadActive.load())
    return false;
  return impl->loadMidi(fileFromPath(path));
}
void Core::play() { impl->play(); }
void Core::pause() { impl->pause(); }
void Core::togglePlay() { impl->togglePlay(); }
void Core::stop() { impl->stop(); }
void Core::next() { impl->next(); }
void Core::prev() { impl->prev(); }
void Core::playTrackAt(int index) {
  Impl::StateLock lock(impl->stateMutex);
  if (impl->exportActiveFlag.load() || impl->pluginLoadActive.load() ||
      !impl->engine.hasPluginLoaded() ||
      impl->engine.hasPluginWorkerCrashed())
    return;
  impl->currentTrackIndex = index;
  if (const auto *track = impl->playlist.getTrack(index)) {
    if (impl->loadMidi(track->file)) {
      ++impl->trackSwitchGeneration;
      const int gen = impl->trackSwitchGeneration;
      impl->scheduleAfter(100, [gen](Impl &self) {
        if (self.trackSwitchGeneration != gen)
          return;
        self.engine.getMidiPlayer().setPlaying(true);
      });
    }
  }
}
void Core::seek(double ratio) { impl->seek(ratio); }
void Core::volume(float value) { impl->volume(value); }
int Core::currentTrackIndex() const {
  Impl::StateLock lock(impl->stateMutex);
  return impl->currentTrackIndex;
}
double Core::sampleRate() const { return impl->sampleRate(); }

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
bool Core::removeTrack(int index) { return impl->removeTrack(index); }
bool Core::moveTrack(int fromIndex, int toIndex) {
  return impl->moveTrack(fromIndex, toIndex);
}
bool Core::refreshTrack(int index) { return impl->refreshTrack(index); }
int Core::findTrackIndex(const std::wstring &path) const {
  Impl::StateLock lock(impl->stateMutex);
  return impl->playlist.findTrackIndex(fileFromPath(path));
}
void Core::clearPlaylist() { impl->clearPlaylist(); }
void Core::setPlayMode(int mode) { impl->setPlayMode(mode); }
std::wstring Core::trackFileAt(int index) const {
  return pathToW(impl->trackFileAt(index));
}
bool Core::saveList(const std::wstring &path) {
  return impl->saveList(fileFromPath(path));
}
bool Core::loadList(const std::wstring &path) {
  return impl->loadList(fileFromPath(path));
}
void Core::setCurrentPlaylistFile(const std::wstring &path) {
  Impl::StateLock lock(impl->stateMutex);
  if (!impl->exportActiveFlag.load())
    impl->currentPlaylistFile = fileFromPath(path);
}
std::wstring Core::currentPlaylistFile() const {
  Impl::StateLock lock(impl->stateMutex);
  return pathToW(impl->currentPlaylistFile);
}
std::wstring Core::lastPlaylistError() const {
  Impl::StateLock lock(impl->stateMutex);
  return toW(impl->playlistErrorText);
}

// audio device
bool Core::hasAudioDevice() const { return impl->engine.hasAudioDevice(); }
bool Core::isFirstRunAudio() const { return impl->engine.isFirstRunAudio(); }
bool Core::wasDeviceRestoredWithFallback() const {
  return impl->engine.wasDeviceRestoredWithFallback();
}

// export
bool Core::isExportActive() const { return impl->exportActiveFlag.load(); }
std::wstring Core::lastExportError() const {
  Impl::StateLock lock(impl->stateMutex);
  return toW(impl->exportErrorText);
}

Core::ExportResult Core::runExport(const ExportRequest &request,
                                   std::function<void(float)> onProgress,
                                   std::function<bool()> shouldCancel) {
  ExportSettings settings;
  settings.formatName = fromW(request.formatName);
  settings.sampleRate = request.sampleRate;
  settings.bitDepth = request.bitDepth;
  settings.useFloatingPoint = request.useFloatingPoint;
  settings.autoTail = request.autoTail;
  settings.fixedTailSeconds = request.fixedTailSeconds;
  settings.title = fromW(request.title);
  settings.qualityIndex = request.qualityIndex;

  return impl->runExport(request.trackIndex, settings,
                         fileFromPath(request.targetPath),
                         std::move(onProgress), std::move(shouldCancel));
}

} // namespace midi
