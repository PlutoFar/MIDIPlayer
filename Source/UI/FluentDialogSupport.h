#pragma once

#include "../Core/BackgroundEffects.h"
#include "../Utils/WindowMaterial.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

struct FluentDialogWindowPolicy {
  bool useSystemDropShadow = false;
  bool useDwmRoundedCorners = false;
  bool configureBeforeEnteringModal = true;
  bool centreOnOwner = true;
  bool useNativeOwner = true;
  bool allowAlwaysOnTop = false;
  bool allowDragging = false;
};

constexpr float fluentDialogCornerRadius = 8.0f;
constexpr float fluentTransientDialogSurfaceAlphaBoost = 0.18f;
constexpr int fluentDialogMaterialSourcePadding =
    WindowMaterial::maximumSoftwareBlurRadius * 2;
constexpr juce::uint32 fluentDialogBackdropBaseArgb = 0xFF0D0D0F;

inline juce::Path createFluentDialogSurfacePath(
    juce::Rectangle<float> bounds, float cornerRadius,
    bool roundTopCorners = true, bool roundBottomCorners = true) {
  juce::Path path;
  path.addRoundedRectangle(
      bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
      cornerRadius, cornerRadius, roundTopCorners, roundTopCorners,
      roundBottomCorners, roundBottomCorners);
  return path;
}

struct FluentDialogMaterialSource {
  juce::Image image;
  juce::Rectangle<int> outputBounds;

  bool isValid() const {
    return !image.isNull() && !outputBounds.isEmpty() &&
           image.getBounds().contains(outputBounds);
  }
};

inline FluentDialogMaterialSource captureFluentDialogMaterialSource(
    juce::Component *window, juce::Component *owner) {
  if (window == nullptr || owner == nullptr || window->getWidth() <= 0 ||
      window->getHeight() <= 0 || owner->getWidth() <= 0 ||
      owner->getHeight() <= 0)
    return {};

  constexpr int padding = fluentDialogMaterialSourcePadding;
  const auto sourceOffset =
      window->getScreenPosition() - owner->getScreenPosition();
  const juce::Rectangle<int> sampleArea(
      sourceOffset.x - padding, sourceOffset.y - padding,
      window->getWidth() + padding * 2,
      window->getHeight() + padding * 2);
  auto snapshot = owner->createComponentSnapshot(sampleArea, false, 1.0f);
  if (snapshot.isNull())
    return {};
  return {snapshot.convertedToFormat(juce::Image::ARGB),
          {padding, padding, window->getWidth(), window->getHeight()}};
}

inline juce::Image createFluentDialogBaseLayer(juce::Rectangle<int> bounds) {
  if (bounds.isEmpty())
    return {};
  juce::Image base(juce::Image::ARGB, bounds.getWidth(), bounds.getHeight(),
                   true);
  {
    juce::Graphics graphics(base);
    graphics.fillAll(juce::Colour(fluentDialogBackdropBaseArgb));
  }
  return base;
}

inline juce::Image renderFluentDialogMaterialBackdrop(
    const FluentDialogMaterialSource &source,
    WindowMaterial::Config config) {
  if (!source.isValid())
    return {};

  config = WindowMaterial::normalise(config);
  if (config.type == WindowMaterial::Type::Transparent)
    return {};

  const int blurRadius = WindowMaterial::softwareBlurRadius(config);
  juce::Image processed;
  switch (config.type) {
  case WindowMaterial::Type::GaussianBlur:
    processed = midi::bgfx::gaussianBlur(source.image, blurRadius);
    break;
  case WindowMaterial::Type::Aero:
    processed = midi::bgfx::aeroBackdrop(source.image, blurRadius);
    break;
  case WindowMaterial::Type::Acrylic:
    processed = midi::bgfx::acrylicBackdrop(source.image, blurRadius);
    break;
  case WindowMaterial::Type::Transparent:
    return {};
  }

  if (processed.isNull())
    return {};

  juce::Image opaqueBackdrop(juce::Image::ARGB, processed.getWidth(),
                             processed.getHeight(), true);
  {
    juce::Graphics graphics(opaqueBackdrop);
    graphics.fillAll(juce::Colour(fluentDialogBackdropBaseArgb));
    graphics.drawImageAt(processed, 0, 0);
  }

  return opaqueBackdrop
      .getClippedImage(source.outputBounds)
      .createCopy();
}

class FluentDialogMaterialRenderJob final : public juce::ThreadPoolJob {
public:
  using Completion = std::function<void(juce::Image)>;

  FluentDialogMaterialRenderJob(FluentDialogMaterialSource materialSource,
                                WindowMaterial::Config materialConfig,
                                Completion completion)
      : juce::ThreadPoolJob("FluentDialogMaterial"),
        source(std::move(materialSource)), config(materialConfig),
        completion(std::move(completion)) {}

  JobStatus runJob() override {
    if (shouldExit())
      return jobHasFinished;
    auto result = renderFluentDialogMaterialBackdrop(source, config);
    if (shouldExit())
      return jobHasFinished;
    juce::MessageManager::callAsync(
        [result, callback = std::move(completion)]() mutable {
          if (callback)
            callback(std::move(result));
        });
    return jobHasFinished;
  }

private:
  FluentDialogMaterialSource source;
  WindowMaterial::Config config;
  Completion completion;
};

inline juce::ThreadPool &fluentDialogMaterialThreadPool() {
  static juce::ThreadPool pool(2);
  return pool;
}

inline void renderFluentDialogMaterialBackdropAsync(
    FluentDialogMaterialSource source, WindowMaterial::Config config,
    FluentDialogMaterialRenderJob::Completion completion) {
  fluentDialogMaterialThreadPool().addJob(
      new FluentDialogMaterialRenderJob(std::move(source), config,
                                        std::move(completion)),
      true);
}

inline void paintFluentDialogMaterialBackdrop(
    juce::Graphics &graphics, const juce::Image &backdrop,
    juce::Rectangle<int> bounds, float cornerRadius) {
  if (backdrop.isNull())
    return;

  juce::Graphics::ScopedSaveState saveState(graphics);
  graphics.reduceClipRegion(createFluentDialogSurfacePath(
      bounds.toFloat(), cornerRadius));
  graphics.drawImage(backdrop, bounds.toFloat(),
                     juce::RectanglePlacement::stretchToFit);
}

constexpr FluentDialogWindowPolicy
getFluentDialogWindowPolicy(bool) {
  return {false, false, true, true, true, false, false};
}

inline juce::Component::WindowControlKind getNonDraggableDialogControlKind(
    juce::Component::WindowControlKind control) {
  using Kind = juce::Component::WindowControlKind;
  return control == Kind::close ? Kind::close : Kind::client;
}

constexpr float dialogTransitionAlphaFloor = 0.32f;

inline float getDialogEnterAlpha(float progress) {
  progress = juce::jlimit(0.0f, 1.0f, progress);
  const float remaining = 1.0f - progress;
  const float eased = 1.0f - remaining * remaining * remaining;
  return dialogTransitionAlphaFloor +
         (1.0f - dialogTransitionAlphaFloor) * eased;
}

inline float getDialogExitAlpha(float progress) {
  progress = juce::jlimit(0.0f, 1.0f, progress);
  const float eased = 1.0f - progress * progress * progress;
  return dialogTransitionAlphaFloor +
         (1.0f - dialogTransitionAlphaFloor) * eased;
}

inline float getDialogExitAlpha(float initialAlpha, float progress) {
  initialAlpha =
      juce::jlimit(dialogTransitionAlphaFloor, 1.0f, initialAlpha);
  const float normalised =
      (getDialogExitAlpha(progress) - dialogTransitionAlphaFloor) /
      (1.0f - dialogTransitionAlphaFloor);
  return dialogTransitionAlphaFloor +
         (initialAlpha - dialogTransitionAlphaFloor) * normalised;
}
