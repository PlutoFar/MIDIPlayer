#include "PluginLibrary.h"
#include "../PluginBridge/PluginListSupport.h"
#include "../Utils/UserSettings.h"

PluginLibrary::PluginLibrary(juce::File cacheFile) : file(std::move(cacheFile)) {
  loadKnownPluginList();
}

void PluginLibrary::copyPluginListTo(juce::KnownPluginList &destination) const {
  if (auto xml = pluginList.createXml())
    destination.recreateFromXml(*xml);
}

juce::Result PluginLibrary::savePluginList(const juce::XmlElement &xml) const {
  juce::TemporaryFile temporary(file);
  if (!xml.writeTo(temporary.getFile()) ||
      !temporary.overwriteTargetFileWithTemporary())
    return juce::Result::fail(L"无法保存插件目录缓存: " + file.getFullPathName());
  return juce::Result::ok();
}

void PluginLibrary::publishPluginList(const juce::XmlElement &xml) {
  pluginList.recreateFromXml(xml);
}

juce::Array<juce::PluginDescription> PluginLibrary::instruments() const {
  auto types = pluginList.getTypes();
  types.removeIf([](const auto &type) { return !isVst3Instrument(type); });
  return types;
}

std::optional<juce::PluginDescription> PluginLibrary::findInstrument(
    const juce::String &id) const {
  for (const auto &type : instruments())
    if (type.createIdentifierString() == id)
      return type;
  return std::nullopt;
}

void PluginLibrary::loadKnownPluginList() {
  if (!file.existsAsFile())
    return;
  const auto tree = juce::XmlDocument::parse(file);
  if (tree != nullptr && tree->hasTagName("KNOWNPLUGINS")) {
    publishPluginList(*tree);
    return;
  }
  juce::Logger::writeToLog(L"插件目录缓存格式无效: " + file.getFullPathName());
  quarantineCorruptPluginCache(file);
}

void PluginLibrary::quarantineCorruptPluginCache(const juce::File &file) {
  // Ownership: 隔离文件保留原始内容；每次隔离采用独立文件名，不覆盖已存在的恢复文件。
  const auto result = UserSettings::quarantineCorruptSettingsFile(file);
  if (result.failed())
    juce::Logger::writeToLog(result.getErrorMessage());
}
