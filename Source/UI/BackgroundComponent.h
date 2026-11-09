#pragma once

#include "../Utils/DebugLogger.h"
#include "../Utils/PaletteSelectionSupport.h"
#include "../Utils/UserSettings.h"
#include "BackgroundEffectSupport.h"
#include "BackgroundRenderer.h"
#include <juce_gui_basics/juce_gui_basics.h>

/**
    Responsibilities: 背景图像、材质和强调色过渡；文件解码及图像计算交给后台任务。
    Ownership: Renderer 随组件销毁，并在析构中取消未交付结果。
    Concurrency: 图像发布、绘制和过渡均在消息线程执行。
    Failures: 图像加载或处理可返回空图；组件根据可用图像状态决定绘制，不承诺所有请求都完成。
*/
class BackgroundComponent : public juce::Component,
                            public juce::ChangeBroadcaster,
                            private juce::Timer {
public:
  using MaterialType = midi::background::Material;

  explicit BackgroundComponent(UserSettings &preferences) : settings(preferences) {
    setInterceptsMouseClicks(false, false);

    addAndMakeVisible(loadingLabel);
    loadingLabel.setText(L"应用图片效果中...", juce::dontSendNotification);
    loadingLabel.setFont(juce::Font(juce::FontOptions(16.0f)));
    loadingLabel.setJustificationType(juce::Justification::centred);
    loadingLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    loadingLabel.setColour(juce::Label::backgroundColourId,
                           juce::Colours::black.withAlpha(0.6f));
    loadingLabel.setVisible(false);

    loadSettings();
    lastLoadedPath = settings.getBackgroundImagePath();
    if (lastLoadedPath.isNotEmpty()) {
      ++imageRevision;
      if (materialType == MaterialType::None ||
          !loadStartupBackgroundSynchronously())
        submitRender(L"加载图片中...");
    }
  }

  void loadAsync(bool forceExtraction = false) {
    auto path = settings.getBackgroundImagePath();

    if (!forceExtraction && path == lastLoadedPath) {
      return;
    }

    lastLoadedPath = path;
    ++imageRevision;
    submitRender(L"加载图片中...");
  }

  void setMaterialType(MaterialType type) {
    if (materialType != type) {
      materialType = type;
      settings.setBackgroundBlurMode(static_cast<int>(type));
      submitRender(L"应用图片效果中...");
    }
  }

  MaterialType getMaterialType() const { return materialType; }

  void setBlurRadius(int radius) {
    radius = juce::jlimit(1, 50, radius);
    if (blurRadius != radius) {
      blurRadius = radius;
      settings.setBackgroundBlurRadius(radius);
      submitRender(L"应用图片效果中...");
    }
  }

  int getBlurRadius() const { return blurRadius; }

  void setOverlayOpacity(float opacity) {
    overlayOpacity = juce::jlimit(0.0f, 1.0f, opacity);
    settings.setBackgroundOverlay(overlayOpacity);
    repaint();
  }

  float getOverlayOpacity() const { return overlayOpacity; }

  void setTintColor(juce::Colour color) {
    tintColor = color;
    repaint();
  }

  void paint(juce::Graphics &g) override {
    SCOPED_TIMER_SLOW("BackgroundComponent::paint", 15);
    auto bounds = getLocalBounds();

    // 无图片时使用稳定的纯色基底，避免低对比渐变在大窗口中产生色带。
    g.fillAll(juce::Colour(0xFF0D0D0F));

    const BackgroundImageState imageState{processedImage.isValid(),
                                          previousImage.isValid(),
                                          originalImage.isValid(),
                                          isTransitioningImage};
    const bool effectRequiresProcessedImage =
        materialType != MaterialType::None;
    const bool hasDrawableImage = shouldDrawVisibleBackgroundImage(
        imageState, effectRequiresProcessedImage);

    auto drawImageWithAlpha = [&](const juce::Image &img, float alpha) {
      if (!img.isNull()) {
        g.setOpacity(alpha);
        // UI 繁忙时用低质量重采样绘制大图，降低绘制开销
        g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
        g.drawImage(img, bounds.toFloat(),
                    juce::RectanglePlacement::fillDestination);
      }
    };

    if (isTransitioningImage) {
      drawImageWithAlpha(previousImage, 1.0f - transitionAlpha);
      drawImageWithAlpha(processedImage, transitionAlpha);
    } else {
      if (processedImage.isValid()) {
        drawImageWithAlpha(processedImage, 1.0f);
      } else if (shouldDrawOriginalBackgroundImage(
                     imageState, effectRequiresProcessedImage)) {
        drawImageWithAlpha(originalImage, 1.0f);
      }
    }

    // 遮罩属于图片效果链；默认纯色背景不应用，避免不同透明层形成色块。
    if (hasDrawableImage) {
      g.setOpacity(1.0f);
      g.setColour(tintColor.withAlpha(overlayOpacity));
      g.fillRect(bounds);
    }

  }

  void timerCallback() override {
    if (isTransitioningImage) {
      transitionAlpha += 0.08f; // Units: 每次定时回调增加的透明度。
      if (transitionAlpha >= 1.0f) {
        transitionAlpha = 1.0f;
        isTransitioningImage = false;
        previousImage = {};
        checkTimerState();
        repaint();
      } else {
        repaint();
      }
    }

    if (isTransitioningColor) {
      auto c1 = lastExtractedColor;
      auto c2 = targetAccentColor;

      if (c1 == c2) {
        isTransitioningColor = false;
        checkTimerState();
        return;
      }

      auto stepParams = [](uint8_t current, uint8_t target) -> uint8_t {
        int diff = (int)target - (int)current;
        if (std::abs(diff) <= 2)
          return target;
        return (uint8_t)(current + diff * 0.15f);
      };

      juce::Colour next(stepParams(c1.getRed(), c2.getRed()),
                        stepParams(c1.getGreen(), c2.getGreen()),
                        stepParams(c1.getBlue(), c2.getBlue()));

      lastExtractedColor = next;
      if (onAccentColorChanged)
        onAccentColorChanged(next);
      sendChangeMessage();

      if (lastExtractedColor == targetAccentColor) {
        isTransitioningColor = false;
        checkTimerState();
        sendChangeMessage();
      }
      if (isTimerRunning())
        repaint();
    }
  }

  void setCurrentAccentColor(juce::Colour color) {
    if (targetAccentColor != color) {
      targetAccentColor = color;
      startColorTransition();
    }
  }

  void startColorTransition() {
    isTransitioningColor = true;
    startTimerHz(60);
  }

  void onColorExtracted(const std::vector<juce::Colour> &palette) {
    if (palette.empty())
      return;

    currentPalette = palette;
    const auto savedColor =
        juce::Colour::fromString(settings.getThemeAccentColor());
    const auto bestColor = midi::selectPaletteAccent(palette, savedColor);

    targetAccentColor = bestColor;

    startColorTransition();

    if (onAccentColorChanged)
      onAccentColorChanged(
          lastExtractedColor);

    sendChangeMessage();
  }

  std::function<void(juce::Colour)> onAccentColorChanged;
  juce::Colour getLastExtractedColor() const { return lastExtractedColor; }
  juce::Colour getTargetAccentColor() const { return targetAccentColor; }
  const std::vector<juce::Colour> &getPalette() const { return currentPalette; }

  bool hasBackgroundImage() const {
    return originalImage.isValid();
  }

private:
  void checkTimerState() {
    if (!isTransitioningImage && !isTransitioningColor) {
      stopTimer();
    }
  }

  bool loadStartupBackgroundSynchronously() {
    auto result = midi::background::Renderer::renderInitial(renderRequest());
    if (result.processed.isNull())
      return false;

    if (!result.palette.empty()) {
      currentPalette = std::move(result.palette);
      const auto saved = juce::Colour::fromString(settings.getThemeAccentColor());
      const auto selected = midi::selectPaletteAccent(currentPalette, saved);
      lastExtractedColor = selected;
      targetAccentColor = selected;
    }

    originalImage = std::move(result.original);
    processedImage = std::move(result.processed);
    appliedImageRevision = imageRevision;
    isFirstLoad = false;

    loadingLabel.setVisible(false);
    repaint();
    return true;
  }

  void loadSettings() {
    int mode = settings.getBackgroundBlurMode();
    // Trust Boundary: 配置编号映射为 `MaterialType`；范围外值归一为 None=1 并回写设置。
    if (mode < 1 || mode > 4) {
      mode = static_cast<int>(MaterialType::None);
      settings.setBackgroundBlurMode(mode);
    }
    materialType = static_cast<MaterialType>(mode);
    blurRadius = settings.getBackgroundBlurRadius();
    overlayOpacity = settings.getBackgroundOverlay();

    auto savedColor =
        juce::Colour::fromString(settings.getThemeAccentColor());
    lastExtractedColor = savedColor;
    targetAccentColor = savedColor;
  }

  void submitRender(const juce::String &status) {
    loadingLabel.setVisible(lastLoadedPath.isNotEmpty());
    loadingLabel.setText(status, juce::dontSendNotification);
    if (!renderer.submit(renderRequest(), [this](midi::background::RenderResult result) {
          applyRender(std::move(result));
        }))
      loadingLabel.setText(L"无法启动背景处理", juce::dontSendNotification);
  }

  midi::background::RenderRequest renderRequest() const {
    return {imageRevision, juce::File(lastLoadedPath),
            appliedImageRevision == imageRevision ? originalImage : juce::Image{},
            materialType, blurRadius, settings.getMonetEnabled()};
  }

  void applyRender(midi::background::RenderResult result) {
    previousImage = std::move(processedImage);
    originalImage = std::move(result.original);
    processedImage = std::move(result.processed);
    isTransitioningImage = !isFirstLoad &&
                           (previousImage.isValid() || processedImage.isValid());
    transitionAlpha = isTransitioningImage ? 0.0f : 1.0f;
    isFirstLoad = false;
    if (appliedImageRevision != imageRevision) {
      appliedImageRevision = imageRevision;
      if (originalImage.isNull())
        onColorExtracted({juce::Colour(0xFF0078D4)});
      else if (!result.palette.empty())
        onColorExtracted(result.palette);
    }
    loadingLabel.setVisible(false);
    if (isTransitioningImage || isTransitioningColor)
      startTimerHz(60);
    repaint();
  }

private:
  UserSettings &settings;
  juce::Image originalImage;
  juce::Image processedImage;
  juce::Image previousImage;
  float transitionAlpha = 1.0f;

  MaterialType materialType = MaterialType::None;
  int blurRadius = 20;
  float overlayOpacity = 0.5f;
  juce::Colour tintColor = juce::Colours::black;
  juce::Colour lastExtractedColor = juce::Colour(0xFF0078D4);
  juce::Colour targetAccentColor = juce::Colour(0xFF0078D4);
  std::vector<juce::Colour> currentPalette;
  juce::String lastLoadedPath;
  uint64_t imageRevision = 0;
  uint64_t appliedImageRevision = 0;
  juce::Label loadingLabel;

  bool isTransitioningImage = false;
  bool isTransitioningColor = false;
  bool isFirstLoad = true;

  midi::background::Renderer renderer;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BackgroundComponent)
};
