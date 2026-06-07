#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

/**
    UserSettings: 用户设置管理器。

    负责管理软件的所有持久化偏好，如音量、播放模式、窗口位置、自定义 VST
   路径等。

    特殊设计：
    - 便携模式检测：程序目录下存在 "portable.dat" 或 "portable_debug.dat"
   文件时，将配置保存在程序目录而非 AppData。
    - 调试模式：将 portable.dat 重命名为 portable_debug.dat
   可同时启用便携模式和调试日志。
    - 自动保存：在析构函数中自动调用 save()，确保设置不丢失。
*/
class UserSettings {
public:
  UserSettings() {
    // 设置文件路径
    settingsFile = getSettingsDirectory().getChildFile("Settings.xml");
    load();
  }

  ~UserSettings() { save(); }

  static juce::File getSettingsDirectory() {
    // 检查是否为便携模式（程序目录下有 portable.txt）
    auto exeDir =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getParentDirectory();
    // 同时识别 portable.dat 和 portable_debug.dat 作为便携模式标记
    bool isPortable = exeDir.getChildFile("portable.dat").existsAsFile() ||
                      exeDir.getChildFile("portable_debug.dat").existsAsFile();

    juce::File dir;
    if (isPortable) {
      // 便携模式：设置保存在程序目录下的 Settings 文件夹
      dir = exeDir.getChildFile("Settings");
    } else {
      // 安装模式：设置保存在用户 AppData 目录
      dir = juce::File::getSpecialLocation(
                juce::File::userApplicationDataDirectory)
                .getChildFile("ModernMidiPlayer");
    }

    dir.createDirectory();
    return dir;
  }

  // 获取便携模式的 VST3 目录
  static juce::File getPortableVst3Directory() {
    auto exeDir =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getParentDirectory();
    auto vst3Dir = exeDir.getChildFile("VST3");
    vst3Dir.createDirectory();
    return vst3Dir;
  }

  // 检查是否为便携模式
  static bool isPortableMode() {
    auto exeDir =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getParentDirectory();
    // 同时识别 portable.dat 和 portable_debug.dat
    return exeDir.getChildFile("portable.dat").existsAsFile() ||
           exeDir.getChildFile("portable_debug.dat").existsAsFile();
  }

  // --- 音量设置 ---
  float getMasterVolume() const {
    return (float)settings.getDoubleValue("masterVolume", 0.8);
  }

  void setMasterVolume(float volume) {
    settings.setValue("masterVolume", juce::jlimit(0.0f, 1.0f, volume));
  }

  // --- 播放模式 ---
  int getPlayMode() const { return settings.getIntValue("playMode", 1); }

  void setPlayMode(int mode) {
    settings.setValue("playMode", juce::jlimit(1, 4, mode));
  }

  // --- 播放图标设置 ---
  bool getSequentialIconListStyle() const {
    return settings.getBoolValue("sequentialIconListStyle", false);
  }

  void setSequentialIconListStyle(bool useListStyle) {
    settings.setValue("sequentialIconListStyle", useListStyle);
  }

  // --- Keep Window Position ---
  bool getRememberWindowBounds() const {
    return settings.getBoolValue("rememberWindowBounds", false);
  }
  void setRememberWindowBounds(bool remember) {
    settings.setValue("rememberWindowBounds", remember);
  }

  // --- 窗口设置 ---
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

  // --- 上次使用的插件 ---
  juce::String getLastPluginId() const {
    return settings.getValue("lastPluginId", "");
  }

  void setLastPluginId(const juce::String &id) {
    settings.setValue("lastPluginId", id);
  }

  // --- 上次打开的文件夹 ---
  juce::String getLastMidiDirectory() const {
    return settings.getValue("lastMidiDir", juce::File::getSpecialLocation(
                                                juce::File::userMusicDirectory)
                                                .getFullPathName());
  }

  void setLastMidiDirectory(const juce::String &path) {
    settings.setValue("lastMidiDir", path);
  }

  // --- 播放列表设置 ---
  bool getPlaylistVisible() const {
    return settings.getBoolValue("playlistVisible", true);
  }

  void setPlaylistVisible(bool visible) {
    settings.setValue("playlistVisible", visible);
  }

  int getPlaylistWidth() const {
    return settings.getIntValue("playlistWidth", 250);
  }

  void setPlaylistWidth(int width) {
    settings.setValue("playlistWidth", juce::jlimit(150, 400, width));
  }

  // --- VST3 扫描路径 ---
  juce::StringArray getCustomVst3Paths() const {
    juce::StringArray paths;
    auto pathsStr = settings.getValue("customVst3Paths", "");
    if (pathsStr.isNotEmpty())
      paths.addTokens(pathsStr, ";", "");
    return paths;
  }

  void setCustomVst3Paths(const juce::StringArray &paths) {
    settings.setValue("customVst3Paths", paths.joinIntoString(";"));
  }

  void addCustomVst3Path(const juce::String &path) {
    auto paths = getCustomVst3Paths();
    if (!paths.contains(path)) {
      paths.add(path);
      setCustomVst3Paths(paths);
    }
  }

  // --- 主题设置 ---
  int getThemeId() const { return settings.getIntValue("themeId", 1); }
  void setThemeId(int id) { settings.setValue("themeId", id); }

  // --- 主题色设置 (Monet) ---
  // Monet 引擎通过分析背景图片获取色调，并自动应用到 UI 组件中。

  juce::String getThemeAccentColor() const {
    return settings.getValue("themeAccentColor",
                             "FF0078D4"); // Default Windows Blue
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

  // --- 字体设置 ---
  juce::String getUIFontName() const {
    return settings.getValue("uiFontName", "Source Han Sans SC");
  }
  void setUIFontName(const juce::String &fontName) {
    settings.setValue("uiFontName", fontName);
  }

  // Playlist Font settings
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

  // Recent Fonts (stored as semicolon delimited string)
  juce::StringArray getRecentFonts() const {
    juce::StringArray fonts;
    fonts.addTokens(settings.getValue("recentFonts", ""), ";", "");
    return fonts;
  }

  void addRecentFont(const juce::String &fontName) {
    auto fonts = getRecentFonts();
    fonts.removeString(fontName); // Remove if exists (to move to top)
    fonts.insert(0, fontName);    // Add to top

    // Keep only top 3
    while (fonts.size() > 3)
      fonts.remove(3);

    settings.setValue("recentFonts", fonts.joinIntoString(";"));
  }

  // --- 背景设置 ---
  juce::String getBackgroundImagePath() const {
    return settings.getValue("backgroundImagePath", "");
  }
  void setBackgroundImagePath(const juce::String &path) {
    settings.setValue("backgroundImagePath", path);
  }

  int getBackgroundBlurMode() const {
    return settings.getIntValue(
        "backgroundBlurMode",
        0); // MaterialType: 1=None, 2=GaussianBlur, 3=Aero, 4=Acrylic
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

  // --- 播放列表文件设置 ---
  juce::String getLastPlaylistPath() const {
    return settings.getValue("lastPlaylistPath", "");
  }
  void setLastPlaylistPath(const juce::String &path) {
    settings.setValue("lastPlaylistPath", path);
  }

  // --- 侧边栏设置 ---
  bool getSidebarCollapsed() const {
    return settings.getBoolValue("sidebarCollapsed", false);
  }
  void setSidebarCollapsed(bool collapsed) {
    settings.setValue("sidebarCollapsed", collapsed);
  }

  // --- 文件关联 ---
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

  // --- 窗口置顶 ---
  bool getAlwaysOnTop() const {
    return settings.getBoolValue("alwaysOnTop", false);
  }
  void setAlwaysOnTop(bool pinned) { settings.setValue("alwaysOnTop", pinned); }

  // --- VST 编辑器设置 ---
  bool getVstEditorVisible() const {
    return settings.getBoolValue("vstEditorVisible", true);
  }
  void setVstEditorVisible(bool visible) {
    settings.setValue("vstEditorVisible", visible);
  }

  // --- UI 缩放 ---
  float getUiScale() const {
    return (float)settings.getDoubleValue("uiScale", 1.0);
  }

  void setUiScale(float scale) {
    settings.setValue("uiScale", juce::jlimit(0.75f, 2.0f, scale));
  }

  // --- 持久化 ---
  bool save() {
    if (auto xml = settings.createXml("ModernMidiPlayerSettings")) {
      auto tempFile =
          settingsFile.getSiblingFile(settingsFile.getFileName() + ".tmp");
      if (tempFile.exists() && !tempFile.deleteFile())
        return false;

      auto stream = tempFile.createOutputStream();
      if (stream == nullptr)
        return false;

      xml->writeTo(*stream, {});
      stream->flush();
      if (stream->getStatus().failed()) {
        stream.reset();
        tempFile.deleteFile();
        return false;
      }
      stream.reset();

      if (!tempFile.replaceFileIn(settingsFile)) {
        tempFile.deleteFile();
        return false;
      }

      return true;
    }
    return false;
  }

  void load() {
    if (settingsFile.existsAsFile()) {
      if (auto xml = juce::XmlDocument::parse(settingsFile)) {
        settings = juce::PropertySet();
        settings.restoreFromXml(*xml);
      }
    }
  }

  // --- 重置为默认 ---
  void resetToDefaults() {
    settings = juce::PropertySet();
    save();
  }

private:
  juce::PropertySet settings;
  juce::File settingsFile;
};

// 全局设置单例
inline UserSettings &getAppSettings() {
  static UserSettings instance;
  return instance;
}
