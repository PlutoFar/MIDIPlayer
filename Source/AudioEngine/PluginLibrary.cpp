#include "PluginLibrary.h"
#include "../PluginBridge/PluginScanProcess.h"
#include "../Utils/UserSettings.h"

PluginLibrary::PluginLibrary() { loadKnownPluginList(); }

void PluginLibrary::copyPluginListTo(juce::KnownPluginList &destination) const {
  if (auto xml = pluginList.createXml())
    destination.recreateFromXml(*xml);
}

bool PluginLibrary::scanPlugins(juce::KnownPluginList &destination,
                                const std::function<bool()> &shouldCancel) {
  juce::String diagnostic;
  const bool succeeded = PluginBridge::scanPluginListInChildProcess(
      destination, destination, shouldCancel, diagnostic);
  setError(diagnostic);
  return succeeded;
}

bool PluginLibrary::replacePluginList(const juce::KnownPluginList &source) {
  const auto xml = source.createXml();
  if (xml == nullptr) {
    setError(L"无法生成插件目录缓存");
    return false;
  }
  const auto file =
      UserSettings::getSettingsDirectory().getChildFile("Plugins.xml");
  juce::TemporaryFile temporary(file);
  if (!xml->writeTo(temporary.getFile()) ||
      !temporary.overwriteTargetFileWithTemporary()) {
    setError(L"无法保存插件目录缓存: " + file.getFullPathName());
    return false;
  }
  // Publish only after the cache replacement succeeds, keeping disk and UI
  // consistent.
  pluginList.recreateFromXml(*xml);
  setError({});
  return true;
}

void PluginLibrary::loadKnownPluginList() {
  const auto file =
      UserSettings::getSettingsDirectory().getChildFile("Plugins.xml");
  if (!file.existsAsFile())
    return;
  const auto tree = juce::XmlDocument::parse(file);
  if (tree != nullptr && tree->hasTagName("KNOWNPLUGINS")) {
    pluginList.recreateFromXml(*tree);
    return;
  }
  setError(L"插件目录缓存格式无效: " + file.getFullPathName());
  quarantineCorruptPluginCache(file);
}

void PluginLibrary::quarantineCorruptPluginCache(const juce::File &file) {
  // Preserve every corrupt cache rather than replacing an earlier recovery
  // file.
  const auto result = UserSettings::quarantineCorruptSettingsFile(file);
  if (result.failed())
    juce::Logger::writeToLog(result.getErrorMessage());
}
