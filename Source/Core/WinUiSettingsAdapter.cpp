#include "WinUiSettingsAdapter.h"

#include "../Utils/UserSettings.h"
#include "../Utils/PaletteSelectionSupport.h"
#include "BackgroundEffects.h"

#include <algorithm>
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

namespace midi {

namespace {

juce::String fromW(const std::wstring &s) { return juce::String(s.c_str()); }

juce::File backgroundsDir() {
  auto dir = UserSettings::getSettingsDirectory().getChildFile("Backgrounds");
  dir.createDirectory();
  return dir;
}

} // namespace

WinUiSettingsAdapter::WinUiSettingsAdapter()
    : bgWorker(&WinUiSettingsAdapter::runBackgroundWorker, this) {}

WinUiSettingsAdapter::~WinUiSettingsAdapter() {
  {
    std::lock_guard<std::mutex> lock(bgReqMutex);
    bgStop = true;
  }
  bgCv.notify_all();
  if (bgWorker.joinable())
    bgWorker.join();
}

WinUiSettingsState WinUiSettingsAdapter::state() {
  WinUiSettingsState state;
  {
    std::lock_guard<std::mutex> lock(bgMutex);
    state.bgImagePath = bgProcessedPath;
    state.bgHasImage = bgHasImage;
    state.bgAccent = bgAccent;
    state.bgPalette = bgPalette;
  }
  state.bgOverlay = 0xFF000000;
  state.bgOverlayOpacity = getAppSettings().getBackgroundOverlay();
  return state;
}

void WinUiSettingsAdapter::applyBackground() {
  const auto path = std::wstring(
      getAppSettings().getBackgroundImagePath().toWideCharPointer());
  const int mode = getAppSettings().getBackgroundBlurMode();
  const int radius = getAppSettings().getBackgroundBlurRadius();
  const bool monet = getAppSettings().getMonetEnabled();
  const auto accent =
      juce::Colour::fromString(getAppSettings().getThemeAccentColor()).getARGB();
  {
    std::lock_guard<std::mutex> lock(bgReqMutex);
    if (bgLastRequestValid && bgReqPath == path && bgReqMode == mode &&
        bgReqRadius == radius && bgReqMonet == monet &&
        bgReqAccent == accent)
      return;
    bgReqPath = path;
    bgReqMode = mode;
    bgReqRadius = radius;
    bgReqMonet = monet;
    bgReqAccent = accent;
    bgLastRequestValid = true;
    bgHasReq = true;
  }
  bgCv.notify_one();
}

void WinUiSettingsAdapter::runBackgroundWorker() {
  for (;;) {
    std::wstring path;
    int mode = 1;
    int radius = 20;
    bool monet = false;
    WinArgb savedAccent = 0xFF0078D4;
    {
      std::unique_lock<std::mutex> lock(bgReqMutex);
      bgCv.wait(lock, [this]() { return bgHasReq || bgStop; });
      if (bgStop)
        return;
      bgHasReq = false;
      path = bgReqPath;
      mode = bgReqMode;
      radius = bgReqRadius;
      monet = bgReqMonet;
      savedAccent = bgReqAccent;
    }

    std::wstring outPath;
    bool hasImage = false;
    WinArgb accent = savedAccent;
    std::vector<WinArgb> palette;

    juce::File src(fromW(path));
    juce::Image image = (!path.empty() && src.existsAsFile())
                            ? juce::ImageFileFormat::loadFrom(src)
                            : juce::Image();
    if (!image.isNull()) {
      image = midi::bgfx::prepareLoaded(image);
      auto processed = midi::bgfx::applyMaterial(image, mode, radius);
      if (!processed.isNull()) {
        for (auto &file : backgroundsDir().findChildFiles(
                 juce::File::findFiles, false, "winui_bg_*.png")) {
          file.deleteFile();
        }
        auto out = backgroundsDir().getChildFile(
            "winui_bg_" + juce::String(juce::Time::currentTimeMillis()) +
            ".png");
        juce::PNGImageFormat png;
        juce::FileOutputStream stream(out);
        if (stream.openedOk()) {
          png.writeImageToStream(processed, stream);
          stream.flush();
          outPath = std::wstring(out.getFullPathName().toWideCharPointer());
          hasImage = true;
        }
      }

      if (monet) {
        auto colours = midi::bgfx::extractPaletteKMeans(image, 6);
        if (!colours.empty()) {
          accent = midi::selectPaletteAccent(
                       colours, juce::Colour(static_cast<juce::uint32>(savedAccent)))
                       .getARGB();
        }
        for (auto &colour : colours)
          palette.push_back(colour.getARGB());
      }
    }

    {
      std::lock_guard<std::mutex> lock(bgMutex);
      bgProcessedPath = outPath;
      bgHasImage = hasImage;
      bgAccent = accent;
      bgPalette = palette;
    }
  }
}

void WinUiSettingsAdapter::setBackgroundImage(const std::wstring &srcPath) {
  juce::File src(fromW(srcPath));
  if (!src.existsAsFile())
    return;

  auto dest = backgroundsDir().getChildFile(
      "bg_" + juce::String(juce::Time::currentTimeMillis()) + ".png");
  auto image = juce::ImageFileFormat::loadFrom(src);
  if (image.isNull())
    return;

  image = midi::bgfx::prepareLoaded(image);
  juce::PNGImageFormat png;
  juce::FileOutputStream stream(dest);
  if (stream.openedOk()) {
    png.writeImageToStream(image, stream);
    stream.flush();
  }

  getAppSettings().setBackgroundImagePath(dest.getFullPathName());
  getAppSettings().save();
  applyBackground();
}

void WinUiSettingsAdapter::clearBackground() {
  getAppSettings().setBackgroundImagePath("");
  getAppSettings().setMonetEnabled(false);
  getAppSettings().save();
  applyBackground();
}

void WinUiSettingsAdapter::setBlurMode(int mode) {
  getAppSettings().setBackgroundBlurMode(mode);
  getAppSettings().save();
  applyBackground();
}

int WinUiSettingsAdapter::blurMode() {
  return getAppSettings().getBackgroundBlurMode();
}

void WinUiSettingsAdapter::setBlurRadius(int radius) {
  getAppSettings().setBackgroundBlurRadius(radius);
  getAppSettings().save();
  applyBackground();
}

int WinUiSettingsAdapter::blurRadius() {
  return getAppSettings().getBackgroundBlurRadius();
}

void WinUiSettingsAdapter::setOverlayOpacity(float value) {
  getAppSettings().setBackgroundOverlay(value);
  getAppSettings().save();
}

void WinUiSettingsAdapter::setMonetEnabled(bool enabled) {
  getAppSettings().setMonetEnabled(enabled);
  getAppSettings().save();
  applyBackground();
}

bool WinUiSettingsAdapter::monetEnabled() {
  return getAppSettings().getMonetEnabled();
}

void WinUiSettingsAdapter::setAccentColor(WinArgb argb) {
  getAppSettings().setThemeAccentColor(juce::Colour(argb).toString());
  getAppSettings().save();
  std::lock_guard<std::mutex> lock(bgMutex);
  bgAccent = argb;
}

std::vector<std::wstring> WinUiSettingsAdapter::recentBackgrounds() {
  auto files = backgroundsDir().findChildFiles(juce::File::findFiles, false,
                                               "bg_*.png");
  std::sort(files.begin(), files.end(),
            [](const juce::File &a, const juce::File &b) {
              return a.getLastModificationTime() > b.getLastModificationTime();
            });
  std::vector<std::wstring> out;
  for (auto &file : files)
    out.push_back(std::wstring(file.getFullPathName().toWideCharPointer()));
  return out;
}

bool WinUiSettingsAdapter::sequentialIconListStyle() {
  return getAppSettings().getSequentialIconListStyle();
}

void WinUiSettingsAdapter::setSequentialIconListStyle(bool value) {
  getAppSettings().setSequentialIconListStyle(value);
  getAppSettings().save();
}

bool WinUiSettingsAdapter::rememberWindowBounds() {
  return getAppSettings().getRememberWindowBounds();
}

void WinUiSettingsAdapter::setRememberWindowBounds(bool value) {
  getAppSettings().setRememberWindowBounds(value);
  getAppSettings().save();
}

WinWindowState WinUiSettingsAdapter::windowState() {
  WinWindowState state;
  const auto bounds = getAppSettings().getWindowBounds();
  state.x = bounds.getX();
  state.y = bounds.getY();
  state.width = bounds.getWidth();
  state.height = bounds.getHeight();
  state.remember = getAppSettings().getRememberWindowBounds();
  state.maximized = getAppSettings().getWindowMaximized();
  state.alwaysOnTop = getAppSettings().getAlwaysOnTop();
  return state;
}

void WinUiSettingsAdapter::saveWindowState(const WinWindowState &state) {
  getAppSettings().setWindowBounds(
      {state.x, state.y, state.width, state.height});
  getAppSettings().setWindowMaximized(state.maximized);
  getAppSettings().setAlwaysOnTop(state.alwaysOnTop);
  getAppSettings().save();
}

void WinUiSettingsAdapter::setAlwaysOnTop(bool value) {
  getAppSettings().setAlwaysOnTop(value);
  getAppSettings().save();
}

std::vector<std::wstring> WinUiSettingsAdapter::systemFonts() {
  std::vector<std::wstring> out;
  for (auto &name : juce::Font::findAllTypefaceNames())
    out.push_back(std::wstring(name.toWideCharPointer()));
  return out;
}

std::wstring WinUiSettingsAdapter::uiFontName() {
  return std::wstring(getAppSettings().getUIFontName().toWideCharPointer());
}

void WinUiSettingsAdapter::setUiFontName(const std::wstring &name) {
  getAppSettings().setUIFontName(fromW(name));
  getAppSettings().save();
}

float WinUiSettingsAdapter::uiFontSize() {
  return getAppSettings().getUIFontSize();
}

void WinUiSettingsAdapter::setUiFontSize(float size) {
  getAppSettings().setUIFontSize(size);
  getAppSettings().save();
}

std::wstring WinUiSettingsAdapter::playlistFontName() {
  return std::wstring(getAppSettings().getPlaylistFontName().toWideCharPointer());
}

void WinUiSettingsAdapter::setPlaylistFontName(const std::wstring &name) {
  getAppSettings().setPlaylistFontName(fromW(name));
  getAppSettings().save();
}

float WinUiSettingsAdapter::playlistFontSize() {
  return getAppSettings().getPlaylistFontSize();
}

void WinUiSettingsAdapter::setPlaylistFontSize(float size) {
  getAppSettings().setPlaylistFontSize(size);
  getAppSettings().save();
}

} // namespace midi
