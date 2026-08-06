#include "WinBridge.h"

#include "Core.h"
#include "../Utils/UserSettings.h"
#include "../Utils/WindowsFileAssociation.h"

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace midi {

namespace {
std::wstring toW(const juce::String &s) {
  return std::wstring(s.toWideCharPointer());
}
juce::String fromW(const std::wstring &s) { return juce::String(s.c_str()); }
} // namespace

// 持有 JUCE 的 MessageManager/GUI 运行时。必须在构造 midi::Core（进而
// AudioEngine）之前、在将要泵送消息的 UI 线程上初始化；WinUI 的消息循环
// 会泵送 JUCE 的隐藏消息窗口，从而驱动 juce::Timer 与变更通知。
struct JuceRuntime {
  juce::ScopedJuceInitialiser_GUI initialiser;
};

WinCore::WinCore() = default;

WinCore::~WinCore() {
  scanCancel = true;
  if (scanThread.joinable())
    scanThread.join();
  if (loading.load() && core)
    core->cancelPendingPluginOperation();
  if (loadThread.joinable())
    loadThread.join();
  exportCancel = true;
  if (exportThread.joinable())
    exportThread.join();
  getAppSettings().save();
}

bool WinCore::init(bool restoreSession) {
  juceRuntime = std::make_unique<JuceRuntime>();
  core = std::make_unique<Core>();
  bool ok = core->init();
  if (!restoreSession)
    return ok;
  core->volume(getAppSettings().getMasterVolume());
  core->setPlayMode(getAppSettings().getPlayMode());

  const auto playlistPath = getAppSettings().getLastPlaylistPath();
  if (playlistPath.isNotEmpty())
    core->loadList(std::wstring(playlistPath.toWideCharPointer()));

  const auto pluginId = getAppSettings().getLastPluginId();
  if (pluginId.isNotEmpty())
    loadAsync(std::wstring(pluginId.toWideCharPointer()));
  return ok;
}

WinState WinCore::state() {
  const auto s = core->state();
  WinState w;
  w.pluginLoaded = s.plugin.loaded;
  w.loadInProgress = loading.load() || s.plugin.loadInProgress;
  w.editorOpen = s.plugin.editorOpen;
  w.workerCrashed = s.plugin.workerCrashed;
  w.scanning = scanning.load();
  {
    std::lock_guard<std::mutex> lk(scanMutex);
    w.scanFinished = scanFinished;
    w.scanSucceeded = scanSucceeded;
    w.scanSessionId = scanSessionId;
    w.scanMessage = scanMessage;
  }
  w.pluginName = s.plugin.loadedName;
  w.pluginError = s.plugin.lastError;
  {
    std::lock_guard<std::mutex> lk(loadMutex);
    w.loadFinished = loadFinished;
    w.loadSucceeded = loadSucceeded;
    w.loadSessionId = loadSessionId;
    w.loadMessage = loadMessage;
  }
  for (const auto &p : s.plugin.available)
    w.plugins.push_back({p.id, p.name, p.manufacturer, p.formatName});

  w.playing = s.transport.playing;
  w.hasSequence = s.transport.hasSequence;
  w.positionSamples = s.transport.positionSamples;
  w.durationSamples = s.transport.durationSamples;
  w.trackIndex = s.transport.currentTrackIndex;
  w.midiName = s.transport.currentMidiName;
  for (const auto &t : s.playlist.trackNames)
    w.tracks.push_back(t);
  w.trackAvailable = s.playlist.trackAvailable;

  w.playMode = s.playlist.playMode;
  w.hasUnsavedChanges = s.playlist.hasUnsavedChanges;
  w.playlistError = s.playlist.lastError;

  w.hasAudioDevice = s.audio.hasDevice;
  w.firstRunAudio = s.audio.firstRunAudio;
  w.deviceFallback = s.audio.deviceFallback;
  w.volume = s.audio.masterVolume;
  w.muted = muted;

  w.exportActive = s.task.exportActive;
  w.exportProgress = s.task.exportProgress;
  {
    std::lock_guard<std::mutex> lk(exportMutex);
    w.exportFinished = exportFinished;
    w.exportSucceeded = exportSucceeded;
    w.exportSessionId = exportSessionId;
    w.exportMessage = exportMessage;
  }

  return w;
}

void WinCore::scan() { core->scan(); }

void WinCore::scanAsync() {
  if (scanning.load() || loading.load())
    return;
  if (scanThread.joinable())
    scanThread.join();
  {
    std::lock_guard<std::mutex> lk(scanMutex);
    scanFinished = false;
    scanSucceeded = false;
    ++scanSessionId;
    scanMessage.clear();
  }
  scanCancel = false;
  scanning = true;
  scanThread = std::thread([this]() {
    const bool succeeded = core->scan([this]() { return scanCancel.load(); });
    const auto error = core->lastPluginError();
    {
      std::lock_guard<std::mutex> lk(scanMutex);
      scanFinished = true;
      scanSucceeded = succeeded;
      scanMessage = succeeded
                        ? std::wstring(L"插件扫描完成。")
                        : (!error.empty() ? error
                                          : std::wstring(L"插件扫描失败。"));
    }
    scanning = false;
  });
}

bool WinCore::load(const std::wstring &id) {
  if (scanning.load() || loading.exchange(true))
    return false;
  struct LoadingGuard {
    std::atomic<bool> &flag;
    ~LoadingGuard() { flag.store(false); }
  } guard{loading};
  return core->load(id);
}

bool WinCore::loadAsync(const std::wstring &id) {
  if (id.empty() || scanning.load() || loading.exchange(true))
    return false;
  if (loadThread.joinable())
    loadThread.join();

  {
    std::lock_guard<std::mutex> lk(loadMutex);
    loadFinished = false;
    loadSucceeded = false;
    ++loadSessionId;
    loadMessage.clear();
  }

  loadThread = std::thread([this, id]() {
    const bool succeeded = core->load(id);
    const auto state = core->state();
    {
      std::lock_guard<std::mutex> lk(loadMutex);
      loadSucceeded = succeeded;
      loadFinished = true;
      loadMessage = succeeded
                        ? state.plugin.loadedName
                        : (!state.plugin.lastError.empty()
                               ? state.plugin.lastError
                               : std::wstring(L"无法加载所选插件。"));
    }
    loading = false;
  });
  return true;
}
void WinCore::unload() { core->unload(); }
bool WinCore::openEditor() { return core->editor(); }
void WinCore::terminateCrashedWorker() { core->terminateCrashedWorker(); }

bool WinCore::openMidi(const std::wstring &path) {
  return core->openMidi(path);
}

bool WinCore::addToPlaylist(const std::wstring &path) {
  return core->addToPlaylist(path);
}

int WinCore::addFiles(const std::vector<std::wstring> &paths) {
  return core->addFilesToPlaylist(paths);
}

void WinCore::play() { core->play(); }
void WinCore::pause() { core->pause(); }
void WinCore::togglePlay() { core->togglePlay(); }
void WinCore::stop() { core->stop(); }
void WinCore::next() { core->next(); }
void WinCore::prev() { core->prev(); }
void WinCore::playTrackAt(int index) { core->playTrackAt(index); }
void WinCore::seek(double ratio) { core->seek(ratio); }

void WinCore::volume(float value) {
  // 手动调节音量即视为解除静音。
  muted = false;
  core->volume(value);
}

void WinCore::toggleMute() { setMuted(!muted); }

void WinCore::setMuted(bool shouldMute) {
  if (shouldMute == muted)
    return;
  if (shouldMute) {
    volumeBeforeMute = core->state().audio.masterVolume;
    muted = true;
    core->volume(0.0f);
  } else {
    muted = false;
    core->volume(volumeBeforeMute);
  }
}

void WinCore::setPlayMode(int mode) {
  core->setPlayMode(mode);
}

void WinCore::tick(bool suppressTrackAdvance) {
  core->tick(suppressTrackAdvance);
}

bool WinCore::removeTrack(int index) {
  return core->removeTrack(index);
}

bool WinCore::moveTrack(int fromIndex, int toIndex) {
  return core->moveTrack(fromIndex, toIndex);
}

void WinCore::clearPlaylist() { core->clearPlaylist(); }

void WinCore::revealTrack(int index) {
  const auto file = core->trackFileAt(index);
  const juce::File juceFile(fromW(file));
  if (juceFile.existsAsFile())
    juceFile.revealToUser();
}

bool WinCore::saveList(const std::wstring &path) {
  return core->saveList(path);
}
bool WinCore::loadList(const std::wstring &path) {
  return core->loadList(path);
}
double WinCore::sampleRate() { return core->sampleRate(); }

bool WinCore::isMidiFileAssociated() { return isMidiFileAssociatedToSelf(); }

bool WinCore::setMidiFileAssociated(bool associated) {
  if (associated)
    return registerMidiFileAssociation();
  removeMidiFileAssociation();
  return !isMidiFileAssociatedToSelf();
}

bool WinCore::exportAudio(const WinExportRequest &request) {
  if (request.targetPath.empty() || core->isExportActive())
    return false;
  if (exportThread.joinable())
    exportThread.join();

  {
    std::lock_guard<std::mutex> lk(exportMutex);
    exportFinished = false;
    exportSucceeded = false;
    ++exportSessionId;
    exportMessage.clear();
  }
  exportCancel = false;

  exportThread = std::thread([this, request]() {
    Core::ExportRequest coreRequest;
    coreRequest.trackIndex = request.trackIndex;
    coreRequest.targetPath = request.targetPath;
    coreRequest.formatName = request.formatName;
    coreRequest.sampleRate = request.sampleRate;
    coreRequest.bitDepth = request.bitDepth;
    coreRequest.useFloatingPoint = request.useFloatingPoint;
    coreRequest.autoTail = request.autoTail;
    coreRequest.fixedTailSeconds = request.fixedTailSeconds;
    coreRequest.title = request.title;
    coreRequest.qualityIndex = request.qualityIndex;

    const auto result =
        core->runExport(coreRequest, {}, [this]() { return exportCancel.load(); });

    std::lock_guard<std::mutex> lk(exportMutex);
    exportFinished = true;
    exportSucceeded = result == Core::ExportResult::Succeeded;
    if (result == Core::ExportResult::Succeeded) {
      exportMessage = L"音频文件已导出。";
    } else if (result == Core::ExportResult::Cancelled) {
      exportMessage = L"导出已取消。";
    } else {
      const auto error = core->lastExportError();
      exportMessage = !error.empty() ? error : std::wstring(L"导出失败。");
    }
  });
  return true;
}

void WinCore::cancelExport() { exportCancel = true; }

// ---- 音频设备 ----

std::vector<std::wstring> WinCore::audioOutputDevices() {
  return core->audioOutputDevices();
}

std::wstring WinCore::currentAudioDevice() {
  return core->currentAudioDevice();
}

void WinCore::setAudioDevice(const std::wstring &name) {
  core->setAudioDevice(name);
}

std::vector<int> WinCore::sampleRates() {
  return core->sampleRates();
}
int WinCore::currentSampleRate() {
  return core->currentSampleRate();
}
void WinCore::setSampleRate(int sr) {
  core->setSampleRate(sr);
}

std::vector<int> WinCore::bufferSizes() {
  return core->bufferSizes();
}
int WinCore::currentBufferSize() {
  return core->currentBufferSize();
}
void WinCore::setBufferSize(int bs) {
  core->setBufferSize(bs);
}

void WinCore::playTestSound() { core->playTestSound(); }

std::wstring WinCore::audioStatus() {
  return core->audioStatus();
}

} // namespace midi
