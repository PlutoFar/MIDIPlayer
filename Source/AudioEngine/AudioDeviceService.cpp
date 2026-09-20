#include "AudioDeviceService.h"
#include "../Utils/UserSettings.h"

AudioDeviceService::AudioDeviceService(juce::AudioProcessor &processor) {
  restore();
  manager.addChangeListener(this);
  player.setProcessor(&processor);
  manager.addAudioCallback(&player);
}

AudioDeviceService::~AudioDeviceService() {
  manager.removeChangeListener(this);
  manager.removeAudioCallback(&player);
  player.setProcessor(nullptr);
}

void AudioDeviceService::publishStatus(const juce::String &diagnostic) {
  deviceAvailable.store(manager.getCurrentAudioDevice() != nullptr);
  const juce::ScopedLock lock(statusLock);
  error = diagnostic;
}

AudioDeviceState AudioDeviceService::state(bool rescan) {
  AudioDeviceState result;
  for (auto *type : manager.getAvailableDeviceTypes())
    result.driverTypes.add(type->getTypeName());
  result.currentDriver = manager.getCurrentAudioDeviceType();
  result.setup = manager.getAudioDeviceSetup();
  if (auto *type = manager.getCurrentDeviceTypeObject()) {
    result.hasDriver = true;
    if (rescan)
      type->scanForDevices();
    result.outputDevices = type->getDeviceNames(false);
    result.separateInputsAndOutputs = type->hasSeparateInputsAndOutputs();
  }
  if (auto *device = manager.getCurrentAudioDevice()) {
    result.hasDevice = true;
    result.deviceName = device->getName();
    result.sampleRates = device->getAvailableSampleRates();
    result.bufferSizes = device->getAvailableBufferSizes();
    result.channelNames = device->getOutputChannelNames();
    result.activeChannels = device->getActiveOutputChannels();
    result.sampleRate = device->getCurrentSampleRate();
    result.bufferSize = device->getCurrentBufferSizeSamples();
    result.bitDepth = device->getCurrentBitDepth();
    result.outputLatency = device->getOutputLatencyInSamples();
    result.hasControlPanel = device->hasControlPanel();
  }
  return result;
}

juce::String AudioDeviceService::save() {
  auto xml = manager.createStateXml();
  if (xml == nullptr) {
    auto *device = manager.getCurrentAudioDevice();
    if (device == nullptr)
      return {};
    const auto setup = manager.getAudioDeviceSetup();
    xml = std::make_unique<juce::XmlElement>("DEVICESETUP");
    xml->setAttribute("deviceType", device->getTypeName());
    xml->setAttribute("audioOutputDeviceName", setup.outputDeviceName);
    xml->setAttribute("audioInputDeviceName", setup.inputDeviceName);
    xml->setAttribute("audioDeviceRate", device->getCurrentSampleRate());
    xml->setAttribute("audioDeviceBufferSize",
                      device->getCurrentBufferSizeSamples());
    if (!setup.useDefaultInputChannels)
      xml->setAttribute("audioDeviceInChans", setup.inputChannels.toString(2));
    if (!setup.useDefaultOutputChannels)
      xml->setAttribute("audioDeviceOutChans",
                        setup.outputChannels.toString(2));
  }
  const auto file =
      UserSettings::getSettingsDirectory().getChildFile("AudioDevice.xml");
  juce::TemporaryFile temporary(file);
  if (!xml->writeTo(temporary.getFile()) ||
      !temporary.overwriteTargetFileWithTemporary())
    return L"无法保存音频设备设置: " + file.getFullPathName();
  return {};
}

void AudioDeviceService::restore() {
  const auto file =
      UserSettings::getSettingsDirectory().getChildFile("AudioDevice.xml");
  firstRun = !file.existsAsFile();
  if (!firstRun) {
    if (auto xml = juce::XmlDocument::parse(file)) {
      const auto diagnostic = manager.initialise(0, 2, xml.get(), true);
      publishStatus(diagnostic);
      if (diagnostic.isEmpty() && hasDevice())
        return;
    }
  }
  auto diagnostic = manager.initialiseWithDefaultDevices(0, 2);
  publishStatus(diagnostic);
  if (diagnostic.isEmpty() && hasDevice()) {
    restoredWithFallback = !firstRun;
    diagnostic = save();
  } else if (diagnostic.isEmpty()) {
    diagnostic = L"没有可用的音频输出设备";
  }
  publishStatus(diagnostic);
}

juce::String AudioDeviceService::setDriver(const juce::String &name) {
  manager.setCurrentAudioDeviceType(name, true);
  const auto diagnostic = manager.getCurrentAudioDeviceType() == name &&
                                  manager.getCurrentAudioDevice() != nullptr
                              ? save()
                              : juce::String(L"无法切换到所选音频驱动");
  publishStatus(diagnostic);
  return diagnostic;
}

juce::String AudioDeviceService::apply(
    const juce::AudioDeviceManager::AudioDeviceSetup &setup) {
  auto diagnostic = manager.setAudioDeviceSetup(setup, true);
  if (diagnostic.isEmpty())
    diagnostic = save();
  publishStatus(diagnostic);
  return diagnostic;
}

juce::String AudioDeviceService::showControlPanel() {
  if (auto *device = manager.getCurrentAudioDevice()) {
    if (device->hasControlPanel() && device->showControlPanel()) {
      manager.closeAudioDevice();
      manager.restartLastAudioDevice();
      const auto diagnostic = manager.getCurrentAudioDevice() != nullptr
                                  ? save()
                                  : juce::String(L"无法重新打开音频设备");
      publishStatus(diagnostic);
      return diagnostic;
    }
  }
  return {};
}

void AudioDeviceService::playTestSound() { manager.playTestSound(); }

void AudioDeviceService::changeListenerCallback(juce::ChangeBroadcaster *) {
  if (recovering)
    return;
  juce::String diagnostic;
  if (manager.getCurrentAudioDevice() != nullptr) {
    diagnostic = save();
  } else if (UserSettings::getSettingsDirectory()
                 .getChildFile("AudioDevice.xml")
                 .existsAsFile()) {
    juce::ScopedValueSetter<bool> recovery(recovering, true);
    diagnostic = manager.initialiseWithDefaultDevices(0, 2);
    if (diagnostic.isEmpty() && manager.getCurrentAudioDevice() != nullptr)
      diagnostic = save();
  }
  publishStatus(diagnostic);
  sendChangeMessage();
}
