#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

class UserSettings {
public:
  UserSettings() {
    settingsFile = getSettingsDirectory().getChildFile("Settings.xml");
    load();
  }

  explicit UserSettings(juce::File file) : settingsFile(std::move(file)) {
    load();
  }

  ~UserSettings() {
    if (automaticSaveEnabled)
      save();
  }

  static juce::File getSettingsDirectory() {
    auto exeDir =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getParentDirectory();
    bool isPortable = exeDir.getChildFile("portable.dat").existsAsFile() ||
                      exeDir.getChildFile("portable_debug.dat").existsAsFile();

    juce::File dir;
    if (isPortable) {
      dir = exeDir.getChildFile("Settings");
    } else {
      dir = juce::File::getSpecialLocation(
                juce::File::userApplicationDataDirectory)
                .getChildFile("ModernMidiPlayer");
    }

    dir.createDirectory();
    return dir;
  }

  static juce::File getPortableVst3Directory() {
    auto exeDir =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getParentDirectory();
    auto vst3Dir = exeDir.getChildFile("VST3");
    vst3Dir.createDirectory();
    return vst3Dir;
  }

  static bool isPortableMode() {
    auto exeDir =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getParentDirectory();
    return exeDir.getChildFile("portable.dat").existsAsFile() ||
           exeDir.getChildFile("portable_debug.dat").existsAsFile();
  }

  float getMasterVolume() const {
    return (float)settings.getDoubleValue("masterVolume", 0.8);
  }

  void setMasterVolume(float volume) {
    settings.setValue("masterVolume", juce::jlimit(0.0f, 1.0f, volume));
  }

  int getPlayMode() const { return settings.getIntValue("playMode", 1); }

  void setPlayMode(int mode) {
    settings.setValue("playMode", juce::jlimit(1, 4, mode));
  }

  bool getSequentialIconListStyle() const {
    return settings.getBoolValue("sequentialIconListStyle", false);
  }

  void setSequentialIconListStyle(bool useListStyle) {
    settings.setValue("sequentialIconListStyle", useListStyle);
  }

  bool getRememberWindowBounds() const {
    return settings.getBoolValue("rememberWindowBounds", false);
  }
  void setRememberWindowBounds(bool remember) {
    settings.setValue("rememberWindowBounds", remember);
  }

  juce::Rectangle<int> getWindowBounds() const {
    return juce::Rectangle<int>(settings.getIntValue("windowX", 100),
                                settings.getIntValue("windowY", 100),
                                settings.getIntValue("windowWidth", 1100),
                                settings.getIntValue("windowHeight", 750));
  }

  void setWindowBounds(const juce::Rectangle<int> &bounds) {
    settings.setValue("windowX", bounds.getX());
    settings.setValue("windowY", bounds.getY());
    settings.setValue("windowWidth", bounds.getWidth());
    settings.setValue("windowHeight", bounds.getHeight());
  }

  bool getWindowMaximized() const {
    return settings.getBoolValue("windowMaximized", false);
  }

  void setWindowMaximized(bool maximized) {
    settings.setValue("windowMaximized", maximized);
  }

  juce::String getLastPluginId() const {
    return settings.getValue("lastPluginId", "");
  }

  void setLastPluginId(const juce::String &id) {
    settings.setValue("lastPluginId", id);
  }

  juce::String getLastMidiDirectory() const {
    return settings.getValue("lastMidiDir", juce::File::getSpecialLocation(
                                                juce::File::userMusicDirectory)
                                                .getFullPathName());
  }

  void setLastMidiDirectory(const juce::String &path) {
    settings.setValue("lastMidiDir", path);
  }

  int getThemeId() const { return settings.getIntValue("themeId", 1); }
  void setThemeId(int id) { settings.setValue("themeId", id); }

  juce::String getThemeAccentColor() const {
    return settings.getValue("themeAccentColor",
                             "FF0078D4");
  }
  void setThemeAccentColor(const juce::String &colorCode) {
    settings.setValue("themeAccentColor", colorCode);
  }

  bool getMonetEnabled() const {
    return settings.getBoolValue("monetEnabled", true);
  }
  void setMonetEnabled(bool enabled) {
    settings.setValue("monetEnabled", enabled);
  }

  juce::String getUIFontName() const {
    return settings.getValue("uiFontName", "Source Han Sans SC");
  }
  void setUIFontName(const juce::String &fontName) {
    settings.setValue("uiFontName", fontName);
  }

  float getUIFontSize() const {
    return juce::jlimit(
        12.0f, 20.0f,
        (float)settings.getDoubleValue("uiFontSize", 14.0));
  }
  void setUIFontSize(float size) {
    settings.setValue("uiFontSize", juce::jlimit(12.0f, 20.0f, size));
  }

  float getLegacyUIFontSize() const {
    const float migratedSize = juce::jmax(16.0f, getUIFontSize());
    return juce::jlimit(
        14.0f, 22.0f,
        (float)settings.getDoubleValue("legacyUiFontSize", migratedSize));
  }

  void setLegacyUIFontSize(float size) {
    settings.setValue("legacyUiFontSize",
                      juce::jlimit(14.0f, 22.0f, size));
  }

  juce::String getPlaylistFontName() const {
    return settings.getValue("playlistFontName", "Microsoft YaHei UI");
  }
  void setPlaylistFontName(const juce::String &fontName) {
    settings.setValue("playlistFontName", fontName);
  }

  float getPlaylistFontSize() const {
    return (float)settings.getDoubleValue("playlistFontSize", 16.0);
  }
  void setPlaylistFontSize(float size) {
    settings.setValue("playlistFontSize", juce::jlimit(12.0f, 36.0f, size));
  }

  juce::StringArray getRecentFonts() const {
    juce::StringArray fonts;
    fonts.addTokens(settings.getValue("recentFonts", ""), ";", "");
    return fonts;
  }

  void addRecentFont(const juce::String &fontName) {
    auto fonts = getRecentFonts();
    fonts.removeString(fontName);
    fonts.insert(0, fontName);

    while (fonts.size() > 3)
      fonts.remove(3);

    settings.setValue("recentFonts", fonts.joinIntoString(";"));
  }

  juce::String getBackgroundImagePath() const {
    return settings.getValue("backgroundImagePath", "");
  }
  void setBackgroundImagePath(const juce::String &path) {
    settings.setValue("backgroundImagePath", path);
  }

  int getBackgroundBlurMode() const {
    return settings.getIntValue(
        "backgroundBlurMode",
        0); // 0 表示旧版本未设置；BackgroundComponent 会回退到 None=1。
  }
  void setBackgroundBlurMode(int mode) {
    settings.setValue("backgroundBlurMode", juce::jlimit(0, 4, mode));
  }

  int getBackgroundBlurRadius() const {
    return settings.getIntValue("backgroundBlurRadius", 20);
  }
  void setBackgroundBlurRadius(int radius) {
    settings.setValue("backgroundBlurRadius", juce::jlimit(1, 50, radius));
  }

  float getBackgroundOverlay() const {
    return (float)settings.getDoubleValue("backgroundOverlay", 0.5);
  }
  void setBackgroundOverlay(float opacity) {
    settings.setValue("backgroundOverlay", juce::jlimit(0.0f, 1.0f, opacity));
  }

  juce::String getLastPlaylistPath() const {
    return settings.getValue("lastPlaylistPath", "");
  }
  void setLastPlaylistPath(const juce::String &path) {
    settings.setValue("lastPlaylistPath", path);
  }

  bool getSidebarCollapsed() const {
    return settings.getBoolValue("sidebarCollapsed", false);
  }
  void setSidebarCollapsed(bool collapsed) {
    settings.setValue("sidebarCollapsed", collapsed);
  }

  bool getFileAssociated() const {
    return settings.getBoolValue("fileAssociated", false);
  }
  void setFileAssociated(bool associated) {
    settings.setValue("fileAssociated", associated);
  }

  bool getDontShowFileAssocPrompt() const {
    return settings.getBoolValue("dontShowFileAssocPrompt", false);
  }
  void setDontShowFileAssocPrompt(bool dontShow) {
    settings.setValue("dontShowFileAssocPrompt", dontShow);
  }

  bool getAlwaysOnTop() const {
    return settings.getBoolValue("alwaysOnTop", false);
  }
  void setAlwaysOnTop(bool pinned) { settings.setValue("alwaysOnTop", pinned); }

  float getUiScale() const {
    return (float)settings.getDoubleValue("uiScale", 1.0);
  }

  void setUiScale(float scale) {
    settings.setValue("uiScale", juce::jlimit(0.75f, 2.0f, scale));
  }

  static juce::Result writeSettingsAtomically(const juce::PropertySet &source,
                                              const juce::File &targetFile) {
    auto xml = source.createXml("ModernMidiPlayerSettings");
    if (xml == nullptr)
      return juce::Result::fail("Unable to create settings XML");

    auto tempFile =
        targetFile.getSiblingFile(targetFile.getFileName() + ".tmp");
    if (tempFile.exists() && !tempFile.deleteFile())
      return juce::Result::fail("Unable to delete stale settings temp file: " +
                                tempFile.getFullPathName());

    auto stream = tempFile.createOutputStream();
    if (stream == nullptr)
      return juce::Result::fail("Unable to open settings file for writing: " +
                                tempFile.getFullPathName());

    xml->writeTo(*stream, {});
    stream->flush();
    if (stream->getStatus().failed()) {
      const auto error = stream->getStatus().getErrorMessage();
      stream.reset();
      tempFile.deleteFile();
      return juce::Result::fail("Unable to write settings XML: " + error);
    }
    stream.reset();

    if (!tempFile.replaceFileIn(targetFile)) {
      tempFile.deleteFile();
      return juce::Result::fail("Unable to replace settings file: " +
                                targetFile.getFullPathName());
    }

    return juce::Result::ok();
  }

  juce::Result saveDetailed() {
    lastSaveError.clear();
    auto result = writeSettingsAtomically(settings, settingsFile);
    if (result.failed())
      lastSaveError = result.getErrorMessage();
    return result;
  }

  bool save() { return saveDetailed().wasOk(); }

  juce::String getLastSaveError() const { return lastSaveError; }

  juce::String getLastLoadError() const { return lastLoadError; }

  static juce::Result quarantineCorruptSettingsFile(
      const juce::File &file, juce::File *quarantinedFile = nullptr) {
    if (!file.existsAsFile())
      return juce::Result::fail("Settings file does not exist: " +
                                file.getFullPathName());

    const auto timestamp =
        juce::Time::getCurrentTime().formatted("%Y%m%d-%H%M%S");
    auto destination = file.getSiblingFile(file.getFileName() + ".corrupt-" +
                                           timestamp);
    if (destination.exists())
      destination = file.getSiblingFile(file.getFileName() + ".corrupt-" +
                                        timestamp + "-" +
                                        juce::Uuid().toString());

    if (!file.moveFileTo(destination))
      return juce::Result::fail("Unable to quarantine corrupt settings: " +
                                file.getFullPathName());

    if (quarantinedFile != nullptr)
      *quarantinedFile = destination;
    return juce::Result::ok();
  }

  void load() {
    lastLoadError.clear();
    automaticSaveEnabled = true;
    if (!settingsFile.existsAsFile())
      return;

    auto xml = juce::XmlDocument::parse(settingsFile);
    if (xml != nullptr && xml->hasTagName("ModernMidiPlayerSettings")) {
      settings = juce::PropertySet();
      settings.restoreFromXml(*xml);
      return;
    }

    juce::File quarantinedFile;
    const auto result =
        quarantineCorruptSettingsFile(settingsFile, &quarantinedFile);
    if (result.wasOk()) {
      settings = juce::PropertySet();
      lastLoadError = "Corrupt settings quarantined to: " +
                      quarantinedFile.getFullPathName();
      return;
    }

    // 隔离失败时禁止析构自动保存，避免默认值覆盖仍可人工恢复的原文件。
    automaticSaveEnabled = false;
    lastLoadError = result.getErrorMessage();
  }

  void resetToDefaults() {
    settings = juce::PropertySet();
    save();
  }

private:
  juce::PropertySet settings;
  juce::File settingsFile;
  juce::String lastSaveError;
  juce::String lastLoadError;
  bool automaticSaveEnabled = true;
};

inline UserSettings &getAppSettings() {
  static UserSettings instance;
  return instance;
}
