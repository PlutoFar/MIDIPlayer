#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace midi {

// One palette swatch / accent colour, packed as 0xAARRGGBB.
using WinArgb = unsigned int;

struct WinUiSettingsState {
  std::wstring bgImagePath;
  bool bgHasImage = false;
  WinArgb bgAccent = 0xFF0078D4;
  WinArgb bgOverlay = 0xFF000000;
  float bgOverlayOpacity = 0.5f;
  std::vector<WinArgb> bgPalette;
};

struct WinWindowState {
  int x = 100;
  int y = 100;
  int width = 1100;
  int height = 750;
  bool remember = false;
  bool maximized = false;
  bool alwaysOnTop = false;
};

class WinUiSettingsAdapter {
public:
  WinUiSettingsAdapter();
  ~WinUiSettingsAdapter();

  WinUiSettingsAdapter(const WinUiSettingsAdapter &) = delete;
  WinUiSettingsAdapter &operator=(const WinUiSettingsAdapter &) = delete;

  WinUiSettingsState state();

  void setBackgroundImage(const std::wstring &srcPath);
  void clearBackground();
  void applyBackground();
  void setBlurMode(int mode);
  int blurMode();
  void setBlurRadius(int radius);
  int blurRadius();
  void setOverlayOpacity(float value);
  void setMonetEnabled(bool enabled);
  bool monetEnabled();
  void setAccentColor(WinArgb argb);
  std::vector<std::wstring> recentBackgrounds();
  bool sequentialIconListStyle();
  void setSequentialIconListStyle(bool value);
  bool rememberWindowBounds();
  void setRememberWindowBounds(bool value);
  WinWindowState windowState();
  void saveWindowState(const WinWindowState &state);
  void setAlwaysOnTop(bool value);

  std::vector<std::wstring> systemFonts();
  std::wstring uiFontName();
  void setUiFontName(const std::wstring &name);
  float uiFontSize();
  void setUiFontSize(float size);
  std::wstring playlistFontName();
  void setPlaylistFontName(const std::wstring &name);
  float playlistFontSize();
  void setPlaylistFontSize(float size);

private:
  void runBackgroundWorker();

  std::thread bgWorker;
  std::mutex bgReqMutex;
  std::condition_variable bgCv;
  bool bgStop = false;
  bool bgHasReq = false;
  std::wstring bgReqPath;
  int bgReqMode = 1;
  int bgReqRadius = 20;
  bool bgReqMonet = false;
  WinArgb bgReqAccent = 0xFF0078D4;
  bool bgLastRequestValid = false;

  std::mutex bgMutex;
  std::wstring bgProcessedPath;
  bool bgHasImage = false;
  WinArgb bgAccent = 0xFF0078D4;
  std::vector<WinArgb> bgPalette;
};

} // namespace midi
