#pragma once

#include "../Utils/DebugLogger.h"
#include "../Utils/UserSettings.h"
#include "BackgroundEffectSupport.h"
#include "BackgroundHistorySupport.h"
#include "BackgroundLoadSupport.h"
#include "../Utils/PaletteSelectionSupport.h"
#include "BackgroundScrollMotion.h"
#include "BackgroundThumbnailSupport.h"
#include "CustomControls.h"
#include "CustomLookAndFeel.h"
#include "FluentSettingsStyle.h"
#include <algorithm>
#include <atomic>
#include <functional>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <mutex>
#include <random>

/**
    BackgroundComponent: 全窗口背景组件，负责背景图片、Monet 取色和
    GaussianBlur/Aero/Acrylic 等背景材质效果。

    图像加载、缩放、效果计算和主题色提取交给 BackgroundWorkerThread，
    避免大图处理阻塞 UI 线程。
*/
class BackgroundComponent : public juce::Component,
                            public juce::ChangeBroadcaster,
                            private juce::Timer {
public:
  enum class MaterialType { None = 1, GaussianBlur = 2, Aero = 3, Acrylic = 4 };

  BackgroundComponent() {
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
    loadAsync();
  }

  ~BackgroundComponent() override {
    cancelPendingWork();
  }

  void loadAsync(bool forceExtraction = false) {
    auto path = getAppSettings().getBackgroundImagePath();

    if (!forceExtraction && path == lastLoadedPath) {
      return;
    }

    lastLoadedPath = path;
    LOG_DEBUG("BackgroundComponent::loadAsync - Path: " + path);

    if (shouldPrepareStartupBackgroundSynchronously(
            isFirstLoad, path.isNotEmpty(), static_cast<int>(materialType)) &&
        loadStartupBackgroundSynchronously(path)) {
      return;
    }

    if (shouldUseAsyncBackgroundLoad(isFirstLoad, path.isNotEmpty())) {
      startImageLoadJob(path, forceExtraction);
    } else {
      isFirstLoad = false;
    }
  }

  void startImageLoadJob(const juce::String &path,
                         bool forceExtraction = false) {
    if (path.isNotEmpty()) {
      loadingLabel.setVisible(true);
      loadingLabel.setText(L"加载图片中...", juce::dontSendNotification);
    }
    ensureWorkerThread();
    BackgroundWorkerThread::LoadSettingsSnapshot settingsSnapshot;
    settingsSnapshot.monetEnabled = getAppSettings().getMonetEnabled();
    workerThread->setLoadTask(path, settingsSnapshot, forceExtraction);
    workerThread->notify();
  }

  void setMaterialType(MaterialType type) {
    if (materialType != type) {
      materialType = type;
      getAppSettings().setBackgroundBlurMode(static_cast<int>(type));
      applyEffects();
    }
  }

  MaterialType getMaterialType() const { return materialType; }

  void setBlurRadius(int radius) {
    radius = juce::jlimit(1, 50, radius);
    if (blurRadius != radius) {
      blurRadius = radius;
      getAppSettings().setBackgroundBlurRadius(radius);
      applyEffects();
    }
  }

  int getBlurRadius() const { return blurRadius; }

  void setOverlayOpacity(float opacity) {
    overlayOpacity = juce::jlimit(0.0f, 1.0f, opacity);
    getAppSettings().setBackgroundOverlay(overlayOpacity);
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

    // paint 路径使用非阻塞锁，避免 UI 线程等待后台图像处理。
    juce::Image currentImage;
    juce::Image previousImageCopy;
    juce::Image originalImageCopy;
    float imageTransitionAlpha = 1.0f;
    bool transitioningImage = false;

    if (imageLock.tryEnter()) {
      currentImage = processedImage;
      previousImageCopy = previousImage;
      originalImageCopy = originalImage;
      imageTransitionAlpha = transitionAlpha;
      transitioningImage = isTransitioningImage;
      imageLock.exit();
    }

    // 无图片时使用稳定的纯色基底，避免低对比渐变在大窗口中产生色带。
    g.fillAll(juce::Colour(0xFF0D0D0F));

    const BackgroundImageState imageState{!currentImage.isNull(),
                                          !previousImageCopy.isNull(),
                                          !originalImageCopy.isNull(),
                                          transitioningImage};
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

    if (transitioningImage) {
      drawImageWithAlpha(previousImageCopy, 1.0f - imageTransitionAlpha);
      drawImageWithAlpha(currentImage, imageTransitionAlpha);
    } else {
      if (!currentImage.isNull()) {
        drawImageWithAlpha(currentImage, 1.0f);
      } else if (shouldDrawOriginalBackgroundImage(
                     imageState, effectRequiresProcessedImage)) {
        drawImageWithAlpha(originalImageCopy, 1.0f);
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
      transitionAlpha += 0.08f; // 60fps 下约 300ms
      if (transitionAlpha >= 1.0f) {
        transitionAlpha = 1.0f;
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
        juce::Colour::fromString(getAppSettings().getThemeAccentColor());
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
    const juce::ScopedLock sl(imageLock);
    return !originalImage.isNull();
  }

private:
  static juce::Image prepareLoadedBackgroundImage(juce::Image img) {
    int maxW = 2560;
    int maxH = 1440;
    if ((img.getWidth() > maxW || img.getHeight() > maxH) &&
        img.getWidth() > 0 && img.getHeight() > 0) {
      float scale = std::min((float)maxW / img.getWidth(),
                             (float)maxH / img.getHeight());
      int newW = std::max(1, (int)(img.getWidth() * scale));
      int newH = std::max(1, (int)(img.getHeight() * scale));
      img = img.rescaled(newW, newH, juce::Graphics::highResamplingQuality);
    }

    return img;
  }

  void checkTimerState() {
    if (!isTransitioningImage && !isTransitioningColor) {
      stopTimer();
    }
  }

private:
  /**
      后台工作线程，用于图像加载、缩放、效果计算和主题色提取。

      处理策略：
      1. 原图超过 2560x1440 时降采样，减少后续模糊计算量。
      2. K-Means 在 150x150 缩略图上运行；内部使用 8 个聚类中心，
         最终返回调用方请求的颜色数量。
      3. abortCurrentTask 是协作取消标记；耗时循环定期检查它，发现新任务后
         尽快放弃当前结果并处理最新请求。
  */
  class BackgroundWorkerThread : public juce::Thread {
  public:
    struct LoadSettingsSnapshot {
      bool monetEnabled = false;
    };

    BackgroundWorkerThread(BackgroundComponent &owner)
        : juce::Thread("BackgroundWorker"), component(owner) {}

    void setLoadTask(const juce::String &path,
                     const LoadSettingsSnapshot &settingsSnapshot,
                     bool forceExtraction = false) {
      const juce::ScopedLock sl(taskLock);
      pendingLoadPath = path;
      pendingLoadSettings = settingsSnapshot;
      hasLoadTask = true;
      hasEffectTask = false;
      abortCurrentTask = true;
      if (forceExtraction)
        lastExtractedPath = "";
    }

    void setEffectTask(const juce::Image &img, MaterialType type, int radius) {
      const juce::ScopedLock sl(taskLock);
      pendingImage = img;
      pendingType = type;
      pendingRadius = radius;
      hasEffectTask = true;
      hasLoadTask = false;
      abortCurrentTask = true;
    }

    void run() override {
      while (!threadShouldExit()) {
        juce::String loadPath;
        LoadSettingsSnapshot loadSettings;
        juce::Image imgToProcess;
        MaterialType type = MaterialType::None;
        int radius = 20;
        bool doLoad = false;
        bool doEffect = false;

        {
          const juce::ScopedLock sl(taskLock);
          if (hasLoadTask) {
            loadPath = pendingLoadPath;
            loadSettings = pendingLoadSettings;
            doLoad = true;
            hasLoadTask = false;
            abortCurrentTask = false;
          } else if (hasEffectTask) {
            imgToProcess = pendingImage;
            type = pendingType;
            radius = pendingRadius;
            doEffect = true;
            hasEffectTask = false;
            abortCurrentTask = false;
          }
        }

        if (doLoad) {
          LOG_DEBUG("BackgroundWorkerThread: Starting load task: " + loadPath);
          processLoadTask(loadPath, loadSettings);
          LOG_DEBUG("BackgroundWorkerThread: Finished load task");
        } else if (doEffect) {
          LOG_DEBUG("BackgroundWorkerThread: Starting effect task, type: " +
                    juce::String((int)type));
          processEffectTask(imgToProcess, type, radius);
          LOG_DEBUG("BackgroundWorkerThread: Finished effect task");
        } else {
          wait(100);
        }
      }
    }

  private:
    juce::String
        lastExtractedPath; // 记录上次取色路径，用于跳过重复取色

    void processLoadTask(const juce::String &path,
                         const LoadSettingsSnapshot &settingsSnapshot) {
      auto file = juce::File(path);
      auto safeComponent =
          juce::Component::SafePointer<BackgroundComponent>(&component);

      if (path.isEmpty()) {
        lastExtractedPath = "";
        juce::MessageManager::callAsync([safeComponent]() {
          if (safeComponent == nullptr)
            return;

          safeComponent->onImageLoaded(juce::Image());
          safeComponent->onColorExtracted(
              {juce::Colour(0xFF0078D4)});
        });
        return;
      }

      const bool shouldExtractColor = path != lastExtractedPath;
      if (shouldExtractColor)
        lastExtractedPath = path;

      if (file.existsAsFile() && !threadShouldExit()) {
        auto img = juce::ImageFileFormat::loadFrom(file);
        if (!threadShouldExit() && !img.isNull()) {
          if (settingsSnapshot.monetEnabled && shouldExtractColor) {
            juce::MessageManager::callAsync([safeComponent]() {
              if (safeComponent != nullptr)
                safeComponent->loadingLabel.setText(L"分析主题色...",
                                               juce::dontSendNotification);
            });

            // 请求返回 6 个代表色；K-Means 内部使用 8 个聚类中心
            auto palette = extractPaletteKMeans(img, 6, [this]() {
              return threadShouldExit() || abortCurrentTask;
            });

            if (!abortCurrentTask && !threadShouldExit() && !palette.empty()) {
              juce::MessageManager::callAsync([safeComponent, palette]() {
                if (safeComponent == nullptr)
                  return;

                safeComponent->onColorExtracted(palette);
              });
            }
          }

          img = prepareLoadedBackgroundImage(img);

          if (threadShouldExit() || abortCurrentTask)
            return;

          juce::MessageManager::callAsync([safeComponent, img]() {
            if (safeComponent == nullptr)
              return;

            safeComponent->onImageLoaded(img);
          });
        }
      } else {
        juce::MessageManager::callAsync([safeComponent]() {
          if (safeComponent == nullptr)
            return;

          safeComponent->onImageLoaded(juce::Image());
          safeComponent->onColorExtracted(
              {juce::Colour(0xFF0078D4)});
        });
      }
    }

    void processEffectTask(const juce::Image &source, MaterialType type,
                           int radius) {
      if (threadShouldExit() || source.isNull())
        return;

      auto safeComponent =
          juce::Component::SafePointer<BackgroundComponent>(&component);
      juce::Image result;

      auto cancelCheck = [this]() {
        return threadShouldExit() || abortCurrentTask;
      };

      switch (type) {
      case MaterialType::GaussianBlur:
        result = applyGaussianBlurStatic(source, radius, cancelCheck);
        break;
      case MaterialType::Aero:
        result = applyAeroEffectStatic(source, radius, cancelCheck);
        break;
      case MaterialType::Acrylic:
        result = applyAcrylicEffectStatic(source, radius, cancelCheck);
        break;
      case MaterialType::None:
      default:
        result = source.createCopy();
        break;
      }

      if (!threadShouldExit() && !result.isNull()) {
        juce::MessageManager::callAsync([safeComponent, result]() {
          if (safeComponent == nullptr)
            return;

          safeComponent->onEffectApplied(result);
        });
      }
    }

  public:
    static juce::Image
    applyGaussianBlurStatic(const juce::Image &source, int radius,
                            std::function<bool()> shouldCancel) {
      if (source.isNull() || radius < 1)
        return source.createCopy();

      // 根据半径动态缩放，避免低半径模糊出现块状感
      int scale = 1;
      if (radius > 16)
        scale = 2;
      if (radius > 32)
        scale = 4;

      int smallW = source.getWidth() / scale;
      int smallH = source.getHeight() / scale;

      if (smallW < 2 || smallH < 2)
        return source.createCopy();

      auto small =
          source.rescaled(smallW, smallH, juce::Graphics::lowResamplingQuality);

      // 多次 box blur 近似 Gaussian；小半径保持 scale=1，避免过度模糊
      int effectiveRadius = radius / scale;
      if (effectiveRadius < 1)
        effectiveRadius = 1;

      for (int pass = 0; pass < 3; ++pass) {
        if (shouldCancel())
          return juce::Image();
        small = boxBlurStatic(small, effectiveRadius + 1, shouldCancel);
        if (small.isNull())
          return juce::Image();
      }

      if (shouldCancel())
        return juce::Image();

      return small.rescaled(source.getWidth(), source.getHeight(),
                            juce::Graphics::mediumResamplingQuality);
    }

    static juce::Image boxBlurStatic(const juce::Image &source, int radius,
                                     std::function<bool()> shouldCancel) {
      if (radius < 1)
        return source.createCopy();

      int w = source.getWidth();
      int h = source.getHeight();
      auto result = juce::Image(juce::Image::ARGB, w, h, true);

      juce::Image::BitmapData srcData(source,
                                      juce::Image::BitmapData::readOnly);
      juce::Image::BitmapData dstData(result,
                                      juce::Image::BitmapData::writeOnly);

      auto temp = juce::Image(juce::Image::ARGB, w, h, true);
      juce::Image::BitmapData tempData(temp,
                                       juce::Image::BitmapData::writeOnly);

      for (int y = 0; y < h; ++y) {
        if (shouldCancel())
          return juce::Image();

        int r = 0, g = 0, b = 0, a = 0, count = 0;

        for (int x = 0; x <= radius && x < w; ++x) {
          auto *p = srcData.getPixelPointer(x, y);
          b += p[0];
          g += p[1];
          r += p[2];
          a += p[3];
          ++count;
        }

        for (int x = 0; x < w; ++x) {
          auto *dst = tempData.getPixelPointer(x, y);
          dst[0] = (uint8_t)(b / count);
          dst[1] = (uint8_t)(g / count);
          dst[2] = (uint8_t)(r / count);
          dst[3] = (uint8_t)(a / count);

          int addX = x + radius + 1;
          int remX = x - radius;

          if (addX < w) {
            auto *p = srcData.getPixelPointer(addX, y);
            b += p[0];
            g += p[1];
            r += p[2];
            a += p[3];
            ++count;
          }
          if (remX >= 0) {
            auto *p = srcData.getPixelPointer(remX, y);
            b -= p[0];
            g -= p[1];
            r -= p[2];
            a -= p[3];
            --count;
          }
        }
      }

      juce::Image::BitmapData tempReadData(temp,
                                           juce::Image::BitmapData::readOnly);

      for (int x = 0; x < w; ++x) {
        if (shouldCancel())
          return juce::Image();

        int r = 0, g = 0, b = 0, a = 0, count = 0;

        for (int y = 0; y <= radius && y < h; ++y) {
          auto *p = tempReadData.getPixelPointer(x, y);
          b += p[0];
          g += p[1];
          r += p[2];
          a += p[3];
          ++count;
        }

        for (int y = 0; y < h; ++y) {
          auto *dst = dstData.getPixelPointer(x, y);
          dst[0] = (uint8_t)(b / count);
          dst[1] = (uint8_t)(g / count);
          dst[2] = (uint8_t)(r / count);
          dst[3] = (uint8_t)(a / count);

          int addY = y + radius + 1;
          int remY = y - radius;

          if (addY < h) {
            auto *p = tempReadData.getPixelPointer(x, addY);
            b += p[0];
            g += p[1];
            r += p[2];
            a += p[3];
            ++count;
          }
          if (remY >= 0) {
            auto *p = tempReadData.getPixelPointer(x, remY);
            b -= p[0];
            g -= p[1];
            r -= p[2];
            a -= p[3];
            --count;
          }
        }
      }

      return result;
    }

    static juce::Image
    applyAeroEffectStatic(const juce::Image &source, int radius,
                          std::function<bool()> shouldCancel) {
      if (source.isNull())
        return source.createCopy();

      // Aero: 中等模糊和更明显的玻璃高光，区别于普通模糊
      auto blurred = applyGaussianBlurStatic(source, radius, shouldCancel);
      if (shouldCancel())
        return {};

      juce::Image result(juce::Image::ARGB, blurred.getWidth(),
                         blurred.getHeight(), true);
      {
        juce::Graphics g(result);
        g.drawImageAt(blurred, 0, 0);

        // 玻璃高光渐变，提高起始透明度以增强可见性
        juce::ColourGradient shine(juce::Colours::white.withAlpha(0.25f), 0, 0,
                                   juce::Colours::white.withAlpha(0.05f), 0,
                                   (float)result.getHeight() * 0.6f, false);
        g.setGradientFill(shine);
        g.fillRect(result.getBounds());

        // 顶部高光线模拟玻璃边缘
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.fillRect(0, 0, result.getWidth(), 1);

        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.drawRect(result.getBounds(), 1);
      }
      return result;
    }

    static juce::Image
    applyAcrylicEffectStatic(const juce::Image &source, int radius,
                             std::function<bool()> shouldCancel) {
      // Acrylic = 模糊 + 饱和度增强 + 亮度噪点

      auto blurred = applyGaussianBlurStatic(source, radius, shouldCancel);

      if (shouldCancel())
        return {};

      juce::Image result = blurred.createCopy();
      juce::Image::BitmapData data(result, 0, 0, result.getWidth(),
                                   result.getHeight(),
                                   juce::Image::BitmapData::readWrite);

      // 简单伪随机生成器
      uint32_t seed = 123456;
      auto rand = [&seed]() {
        seed = seed * 1103515245 + 12345;
        return (seed / 65536) % 32768;
      };

      for (int y = 0; y < result.getHeight(); ++y) {
        if (shouldCancel())
          return {};

        uint8_t *p = data.getLinePointer(y);
        for (int x = 0; x < result.getWidth(); ++x) {
          // A. 增强饱和度：让颜色远离灰阶
          if (data.pixelStride >= 3) {
            int b = p[0];
            int g = p[1];
            int r = p[2];
            int grey = (r + g + b) / 3;

            // RGB 分量相对灰阶扩大 30%
            r = grey + (int)((r - grey) * 1.3f);
            g = grey + (int)((g - grey) * 1.3f);
            b = grey + (int)((b - grey) * 1.3f);

            p[2] = (uint8_t)juce::jlimit(0, 255, r);
            p[1] = (uint8_t)juce::jlimit(0, 255, g);
            p[0] = (uint8_t)juce::jlimit(0, 255, b);
          }

          // B. 添加单色亮度噪点，避免彩色噪声破坏质感
          int noise = (rand() % 12) - 6; // +/- 6 的细微颗粒

          const int colourChannels = juce::jmin(3, data.pixelStride);
          for (int c = 0; c < colourChannels; ++c) {
            p[c] = (uint8_t)juce::jlimit(0, 255, (int)p[c] + noise);
          }
          p += data.pixelStride;
        }
      }

      return result;
    }

    static std::vector<juce::Colour>
    extractPaletteKMeans(const juce::Image &image, int k,
                         std::function<bool()> shouldCancel) {
      if (image.isNull())
        return {juce::Colour(0xFF0078D4)};

      // 降采样到 150x150，足够做主色分析并显著降低计算量。
      auto workImg =
          image.rescaled(150, 150, juce::Graphics::lowResamplingQuality);
      int w = workImg.getWidth();
      int h = workImg.getHeight();

      std::vector<juce::Colour> pixels;
      pixels.reserve(w * h);
      juce::Image::BitmapData data(workImg, juce::Image::BitmapData::readOnly);

      for (int y = 0; y < h; ++y) {
        if (shouldCancel())
          return {};
        auto *p = data.getLinePointer(y);
        for (int x = 0; x < w; ++x) {
          int pixelStride = data.pixelStride;
          juce::Colour c(p[pixelStride == 4 ? 2 : 0],  // R
                         p[pixelStride == 4 ? 1 : 1],  // G
                         p[pixelStride == 4 ? 0 : 2]); // B
          pixels.push_back(c);
          p += pixelStride;
        }
      }

      if (pixels.empty())
        return {juce::Colour(0xFF0078D4)};

      struct Centroid {
        float r = 0, g = 0, b = 0;
        int count = 0;
        void reset() {
          r = 0;
          g = 0;
          b = 0;
          count = 0;
        }
        void add(juce::Colour c) {
          r += c.getFloatRed();
          g += c.getFloatGreen();
          b += c.getFloatBlue();
          count++;
        }
        juce::Colour getAverage() const {
          if (count == 0)
            return juce::Colours::black;
          return juce::Colour::fromFloatRGBA(r / count, g / count, b / count,
                                             1.0f);
        }
      };

      int numClusters = 8; // 内部聚类中心数，提升代表色采样质量
      std::vector<juce::Colour> centers;
      std::mt19937 rng(42);
      std::uniform_int_distribution<int> dist(0, (int)pixels.size() - 1);

      for (int i = 0; i < numClusters; ++i)
        centers.push_back(pixels[dist(rng)]);

      for (int iter = 0; iter < 5; ++iter) {
        if (shouldCancel())
          return {};
        std::vector<Centroid> newCentroids(numClusters);

        for (const auto &p : pixels) {
          int best = 0;
          float minDist = 1e9f;
          float pr = p.getFloatRed(), pg = p.getFloatGreen(),
                pb = p.getFloatBlue();

          for (int i = 0; i < numClusters; ++i) {
            float dr = pr - centers[i].getFloatRed();
            float dg = pg - centers[i].getFloatGreen();
            float db = pb - centers[i].getFloatBlue();
            float d = dr * dr + dg * dg + db * db;
            if (d < minDist) {
              minDist = d;
              best = i;
            }
          }
          newCentroids[best].add(p);
        }

        for (int i = 0; i < numClusters; ++i) {
          if (newCentroids[i].count > 0)
            centers[i] = newCentroids[i].getAverage();
          else
            centers[i] = pixels[dist(rng)];
        }
      }

      struct ScoredColor {
        juce::Colour c;
        float score;
      };
      std::vector<ScoredColor> ranked;
      for (const auto &c : centers) {
        float sat = c.getSaturation();
        float bri = c.getBrightness();
        float score = sat * 2.0f +
                      (bri > 0.5f ? 0.5f : 0.0f); // 偏好高饱和且较亮的颜色
        if (bri < 0.15f || bri > 0.95f)
          score *= 0.1f; // 降低极暗或极亮颜色的权重
        ranked.push_back({c, score});
      }

      std::sort(ranked.begin(), ranked.end(),
                [](const auto &a, const auto &b) { return a.score > b.score; });

      std::vector<juce::Colour> result;
      for (const auto &rc : ranked) {
        bool unique = true;
        for (const auto &ex : result) {
          float dh = std::abs(rc.c.getHue() - ex.getHue());
          if (dh > 0.5f)
            dh = 1.0f - dh;
          if (dh < 0.08f) { // 允许少量渐变相近色
            unique = false;
            break;
          }
          int dr = rc.c.getRed() - ex.getRed();
          int dg = rc.c.getGreen() - ex.getGreen();
          int db = rc.c.getBlue() - ex.getBlue();
          if (dr * dr + dg * dg + db * db <
              2500) { // 较高阈值减少近似色
            unique = false;
            break;
          }
        }
        if (unique)
          result.push_back(rc.c);
        if (result.size() >= k)
          break;
      }

      // 代表色不足时生成补充变化色。
      while (result.size() < k) {
        if (!result.empty()) {
          auto base = result.back();
          result.push_back(base.withRotatedHue(0.08f * (float)result.size())
                               .withMultipliedBrightness(0.9f)
                               .withMultipliedSaturation(1.1f));
        } else {
          result.push_back(juce::Colour(0xFF0078D4));
        }
      }
      return result;
    }

    BackgroundComponent &component;
    juce::CriticalSection taskLock;
    juce::String pendingLoadPath;
    LoadSettingsSnapshot pendingLoadSettings;
    juce::Image pendingImage;
    MaterialType pendingType = MaterialType::None;
    int pendingRadius = 20;
    bool hasLoadTask = false;
    bool hasEffectTask = false;
    std::atomic<bool> abortCurrentTask{false};
  };

  static juce::Image
  applyMaterialEffectSynchronously(const juce::Image &source, MaterialType type,
                                   int radius) {
    auto neverCancel = []() { return false; };

    switch (type) {
    case MaterialType::GaussianBlur:
      return BackgroundWorkerThread::applyGaussianBlurStatic(source, radius,
                                                             neverCancel);
    case MaterialType::Aero:
      return BackgroundWorkerThread::applyAeroEffectStatic(source, radius,
                                                           neverCancel);
    case MaterialType::Acrylic:
      return BackgroundWorkerThread::applyAcrylicEffectStatic(source, radius,
                                                              neverCancel);
    case MaterialType::None:
    default:
      return source.createCopy();
    }
  }

  bool loadStartupBackgroundSynchronously(const juce::String &path) {
    auto file = juce::File(path);
    if (!file.existsAsFile())
      return false;

    auto img = juce::ImageFileFormat::loadFrom(file);
    if (img.isNull())
      return false;

    img = prepareLoadedBackgroundImage(img);

    auto processed =
        applyMaterialEffectSynchronously(img, materialType, blurRadius);
    if (processed.isNull())
      return false;

    if (getAppSettings().getMonetEnabled()) {
      auto palette = BackgroundWorkerThread::extractPaletteKMeans(
          img, 6, []() { return false; });
      if (!palette.empty()) {
        currentPalette = palette;
        const auto saved = juce::Colour::fromString(
            getAppSettings().getThemeAccentColor());
        const auto selected = midi::selectPaletteAccent(palette, saved);
        lastExtractedColor = selected;
        targetAccentColor = selected;
      }
    }

    {
      const juce::ScopedLock sl(imageLock);
      originalImage = img;
      processedImage = processed;
      previousImage = juce::Image();
      transitionAlpha = 1.0f;
      isTransitioningImage = false;
      isFirstLoad = false;
    }

    loadingLabel.setVisible(false);
    repaint();
    return true;
  }

  void loadSettings() {
    int mode = getAppSettings().getBackgroundBlurMode();
    // MaterialType 编号会持久化到设置：None=1、GaussianBlur=2、Aero=3、Acrylic=4
    // 非法编号统一回退到 None，避免读取旧配置或损坏配置后产生未定义模式
    if (mode < 1 || mode > 4) {
      mode = static_cast<int>(MaterialType::None);
      getAppSettings().setBackgroundBlurMode(mode);
    }
    materialType = static_cast<MaterialType>(mode);
    blurRadius = getAppSettings().getBackgroundBlurRadius();
    overlayOpacity = getAppSettings().getBackgroundOverlay();

    auto savedColor =
        juce::Colour::fromString(getAppSettings().getThemeAccentColor());
    lastExtractedColor = savedColor;
    targetAccentColor = savedColor;
  }

  void applyEffects() {
    if (materialType == MaterialType::None) {
      loadingLabel.setVisible(false);
      juce::Image img;
      {
        const juce::ScopedLock sl(imageLock);
        if (originalImage.isNull()) {
          processedImage = juce::Image();
          repaint();
          return;
        }
        img = originalImage.createCopy();
      }
      onEffectApplied(img);
      return;
    }

    loadingLabel.setVisible(true);
    loadingLabel.setText(L"应用图片效果中...", juce::dontSendNotification);

    juce::Image imgCopy;
    {
      const juce::ScopedLock sl(imageLock);
      if (originalImage.isNull()) {
        processedImage = juce::Image();
        loadingLabel.setVisible(false);
        repaint();
        return;
      }
      imgCopy = originalImage.createCopy();
    }

    ensureWorkerThread();
    workerThread->setEffectTask(imgCopy, materialType, blurRadius);
    workerThread->notify();
  }

  void ensureWorkerThread() {
    if (workerThread == nullptr) {
      workerThread = std::make_unique<BackgroundWorkerThread>(*this);
      workerThread->startThread();
    }
  }

  void cancelPendingWork() {
    if (workerThread != nullptr) {
      LOG_DEBUG("[FREEZE_DIAG] cancelPendingWork - signaling thread to exit");
      if (!workerThread->stopThread(getBackgroundWorkerStopTimeoutMs())) {
        LOG_DEBUG("[FREEZE_DIAG] cancelPendingWork - worker did not stop before timeout");
      }

      workerThread.reset();
      LOG_DEBUG("[FREEZE_DIAG] cancelPendingWork - done");
    }
  }

  // 后台线程回调在消息线程执行。
  void onImageLoaded(const juce::Image &img) {
    LOG_DEBUG("[FREEZE_DIAG] onImageLoaded - start");
    {
      const juce::ScopedLock sl(imageLock);
      LOG_DEBUG("[FREEZE_DIAG] onImageLoaded - lock acquired");
      originalImage = img;
    }
    LOG_DEBUG("[FREEZE_DIAG] onImageLoaded - lock released");
    if (materialType == MaterialType::None)
      loadingLabel.setVisible(false);
    applyEffects();
    repaint();
    LOG_DEBUG("[FREEZE_DIAG] onImageLoaded - end");
  }

  void onEffectApplied(const juce::Image &img) {
    LOG_DEBUG("[FREEZE_DIAG] onEffectApplied - start");
    {
      const juce::ScopedLock sl(imageLock);
      LOG_DEBUG("[FREEZE_DIAG] onEffectApplied - lock acquired");
      previousImage = processedImage.isNull() ? originalImage : processedImage;
      processedImage = img;

      if (isFirstLoad) {
        transitionAlpha = 1.0f;
        isTransitioningImage = false;
        isFirstLoad = false;
      } else {
        transitionAlpha = 0.0f;
        isTransitioningImage = true;
      }
    }
    LOG_DEBUG("[FREEZE_DIAG] onEffectApplied - lock released");

    loadingLabel.setVisible(false);
    if (isTransitioningImage || isTransitioningColor)
      startTimerHz(60);
    repaint();
    LOG_DEBUG("[FREEZE_DIAG] onEffectApplied - end");
  }

private:
  juce::CriticalSection imageLock;
  juce::Image originalImage;
  juce::Image processedImage;
  juce::Image previousImage;
  float transitionAlpha = 1.0f;

  std::unique_ptr<BackgroundWorkerThread> workerThread;

  MaterialType materialType = MaterialType::None;
  int blurRadius = 20;
  float overlayOpacity = 0.5f;
  juce::Colour tintColor = juce::Colours::black;
  juce::Colour lastExtractedColor = juce::Colour(0xFF0078D4);
  juce::Colour targetAccentColor = juce::Colour(0xFF0078D4);
  std::vector<juce::Colour> currentPalette;
  juce::String lastLoadedPath;
  juce::Label loadingLabel;

  bool isTransitioningImage = false;
  bool isTransitioningColor = false;
  bool isFirstLoad = true;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BackgroundComponent)
};

class ImageCropperComponent : public juce::Component {
public:
  ImageCropperComponent() { setInterceptsMouseClicks(true, true); }

  void setImage(const juce::Image &img) {
    originalImage = img;
    if (!img.isNull()) {
      cropRegion = juce::Rectangle<float>(0.0f, 0.0f, 1.0f, 1.0f);
    }
    repaint();
  }

  void setCropRegion(juce::Rectangle<float> region) {
    cropRegion = region;
    repaint();
  }

  juce::Rectangle<float> getCropRegion() const { return cropRegion; }

  juce::Image getCroppedImage() const {
    if (originalImage.isNull())
      return juce::Image();

    int x = juce::roundToInt(cropRegion.getX() * originalImage.getWidth());
    int y = juce::roundToInt(cropRegion.getY() * originalImage.getHeight());
    int w = juce::roundToInt(cropRegion.getWidth() * originalImage.getWidth());
    int h =
        juce::roundToInt(cropRegion.getHeight() * originalImage.getHeight());

    return originalImage.getClippedImage(
        {x, y, juce::jmax(1, w), juce::jmax(1, h)});
  }

  void paint(juce::Graphics &g) override {
    auto bounds = getLocalBounds().toFloat();

    if (originalImage.isNull()) {
      g.setColour(juce::Colours::grey.withAlpha(0.3f));
      g.drawRect(bounds, 1.0f);
      g.setColour(juce::Colours::white.withAlpha(0.5f));
      g.setFont(14.0f);
      g.drawText(L"无图片", bounds, juce::Justification::centred);
      return;
    }

    g.setOpacity(0.4f);
    g.drawImage(originalImage, bounds, juce::RectanglePlacement::centred);

    float imgAspect =
        (float)originalImage.getWidth() / originalImage.getHeight();
    float boundsAspect = bounds.getWidth() / bounds.getHeight();

    juce::Rectangle<float> imageDisplayRect;
    if (imgAspect > boundsAspect) {
      float displayHeight = bounds.getWidth() / imgAspect;
      imageDisplayRect =
          bounds.withHeight(displayHeight).withCentre(bounds.getCentre());
    } else {
      float displayWidth = bounds.getHeight() * imgAspect;
      imageDisplayRect =
          bounds.withWidth(displayWidth).withCentre(bounds.getCentre());
    }
    displayRect = imageDisplayRect;

    auto cropDisplayRect = juce::Rectangle<float>(
        imageDisplayRect.getX() +
            cropRegion.getX() * imageDisplayRect.getWidth(),
        imageDisplayRect.getY() +
            cropRegion.getY() * imageDisplayRect.getHeight(),
        cropRegion.getWidth() * imageDisplayRect.getWidth(),
        cropRegion.getHeight() * imageDisplayRect.getHeight());

    g.setOpacity(1.0f);
    g.saveState();
    g.reduceClipRegion(cropDisplayRect.toNearestInt());
    g.drawImage(originalImage, imageDisplayRect,
                juce::RectanglePlacement::centred);
    g.restoreState();

    g.setColour(juce::Colour(0xFFFF8C00));
    g.drawRect(cropDisplayRect, 2.0f);

    float handleSize = 10.0f;
    g.setColour(juce::Colours::white);
    g.fillRect(cropDisplayRect.getX() - handleSize / 2,
               cropDisplayRect.getY() - handleSize / 2, handleSize, handleSize);
    g.fillRect(cropDisplayRect.getRight() - handleSize / 2,
               cropDisplayRect.getY() - handleSize / 2, handleSize, handleSize);
    g.fillRect(cropDisplayRect.getX() - handleSize / 2,
               cropDisplayRect.getBottom() - handleSize / 2, handleSize,
               handleSize);
    g.fillRect(cropDisplayRect.getRight() - handleSize / 2,
               cropDisplayRect.getBottom() - handleSize / 2, handleSize,
               handleSize);
  }

  void mouseDown(const juce::MouseEvent &e) override {
    if (originalImage.isNull())
      return;

    auto cropDisplayRect = getCropDisplayRect();
    float handleSize = 12.0f;
    auto pos = e.position;

    if (juce::Rectangle<float>(cropDisplayRect.getX() - handleSize,
                               cropDisplayRect.getY() - handleSize,
                               handleSize * 2, handleSize * 2)
            .contains(pos))
      dragMode = DragMode::TopLeft;
    else if (juce::Rectangle<float>(cropDisplayRect.getRight() - handleSize,
                                    cropDisplayRect.getY() - handleSize,
                                    handleSize * 2, handleSize * 2)
                 .contains(pos))
      dragMode = DragMode::TopRight;
    else if (juce::Rectangle<float>(cropDisplayRect.getX() - handleSize,
                                    cropDisplayRect.getBottom() - handleSize,
                                    handleSize * 2, handleSize * 2)
                 .contains(pos))
      dragMode = DragMode::BottomLeft;
    else if (juce::Rectangle<float>(cropDisplayRect.getRight() - handleSize,
                                    cropDisplayRect.getBottom() - handleSize,
                                    handleSize * 2, handleSize * 2)
                 .contains(pos))
      dragMode = DragMode::BottomRight;
    else if (cropDisplayRect.contains(pos))
      dragMode = DragMode::Move;
    else
      dragMode = DragMode::None;

    dragStart = pos;
    cropStart = cropRegion;
  }

  void mouseDrag(const juce::MouseEvent &e) override {
    if (originalImage.isNull() || dragMode == DragMode::None)
      return;

    auto delta = e.position - dragStart;
    float dx = delta.x / displayRect.getWidth();
    float dy = delta.y / displayRect.getHeight();

    auto newCrop = cropStart;

    switch (dragMode) {
    case DragMode::Move:
      newCrop = newCrop.translated(dx, dy);
      break;
    case DragMode::TopLeft:
      newCrop = juce::Rectangle<float>(
          cropStart.getX() + dx, cropStart.getY() + dy,
          cropStart.getWidth() - dx, cropStart.getHeight() - dy);
      break;
    case DragMode::TopRight:
      newCrop = juce::Rectangle<float>(cropStart.getX(), cropStart.getY() + dy,
                                       cropStart.getWidth() + dx,
                                       cropStart.getHeight() - dy);
      break;
    case DragMode::BottomLeft:
      newCrop = juce::Rectangle<float>(cropStart.getX() + dx, cropStart.getY(),
                                       cropStart.getWidth() - dx,
                                       cropStart.getHeight() + dy);
      break;
    case DragMode::BottomRight:
      newCrop = juce::Rectangle<float>(cropStart.getX(), cropStart.getY(),
                                       cropStart.getWidth() + dx,
                                       cropStart.getHeight() + dy);
      break;
    default:
      break;
    }

    newCrop = newCrop.constrainedWithin(juce::Rectangle<float>(0, 0, 1, 1));
    if (newCrop.getWidth() > 0.05f && newCrop.getHeight() > 0.05f) {
      cropRegion = newCrop;
      repaint();
    }
  }

  void mouseUp(const juce::MouseEvent &) override { dragMode = DragMode::None; }

private:
  juce::Rectangle<float> getCropDisplayRect() const {
    return juce::Rectangle<float>(
        displayRect.getX() + cropRegion.getX() * displayRect.getWidth(),
        displayRect.getY() + cropRegion.getY() * displayRect.getHeight(),
        cropRegion.getWidth() * displayRect.getWidth(),
        cropRegion.getHeight() * displayRect.getHeight());
  }

  juce::Image originalImage;
  juce::Rectangle<float> cropRegion{0, 0, 1, 1};
  juce::Rectangle<float> displayRect;

  enum class DragMode {
    None,
    Move,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
  };
  DragMode dragMode = DragMode::None;
  juce::Point<float> dragStart;
  juce::Rectangle<float> cropStart;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImageCropperComponent)
};

class ImagePreviewButton : public juce::Button, private juce::Timer {
public:
  std::function<void()> onRemoveRequested;

  ImagePreviewButton(const juce::File &f)
      : juce::Button(f.getFileName()), file(f) {
    setTooltip(f.getFileName());
    safeThis = this;
    sharedThumbnailPool().addJob(new ThumbnailJob(file, safeThis), true);
  }

  ~ImagePreviewButton() override {
    stopTimer();
  }

  void mouseDown(const juce::MouseEvent &event) override {
    if (!event.mods.isPopupMenu())
      juce::Button::mouseDown(event);
  }

  void mouseUp(const juce::MouseEvent &event) override {
    if (event.mods.isPopupMenu()) {
      if (onRemoveRequested)
        onRemoveRequested();
      return;
    }
    juce::Button::mouseUp(event);
  }

  void paintButton(juce::Graphics &g, bool isMouseOver,
                   bool isButtonDown) override {
    auto bounds = getLocalBounds().toFloat();

    auto *laf = dynamic_cast<FluentLookAndFeel *>(&getLookAndFeel());
    const auto backgroundColour =
        laf != nullptr ? laf->getColors().controlBackground
                       : juce::Colours::black.withAlpha(0.3f);
    const auto borderColour =
        laf != nullptr ? laf->getColors().controlBorder
                       : juce::Colours::white.withAlpha(0.1f);
    const auto accentColour =
        laf != nullptr ? laf->getColors().accentPrimary
                       : juce::Colours::white.withAlpha(0.5f);

    g.setColour(backgroundColour);
    g.fillRoundedRectangle(bounds, 4.0f);

    if (thumbnail.isValid()) {
      const float eased =
          1.0f - std::pow(1.0f - thumbnailEntrance, 3.0f);
      const float inset = (1.0f - eased) * 5.0f;
      const auto imageBounds = bounds.reduced(inset);
      g.setOpacity(0.35f + eased * 0.65f);
      g.drawImage(thumbnail, imageBounds,
                  juce::RectanglePlacement::fillDestination);

      g.setOpacity(eased);
      g.setColour(borderColour);
      g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    } else {
      g.setColour(borderColour);
      g.drawText(L"\u22EF", getLocalBounds(), juce::Justification::centred,
                 false);
    }

    if (isEnabled() && (isMouseOver || isButtonDown)) {
      g.setColour(isButtonDown ? accentColour.darker(0.15f) : accentColour);
      g.drawRoundedRectangle(bounds, 4.0f, isButtonDown ? 2.0f : 1.5f);
    }
  }

private:
  void timerCallback() override {
    thumbnailEntrance = juce::jmin(1.0f, thumbnailEntrance + 0.12f);
    repaint();
    if (thumbnailEntrance >= 1.0f)
      stopTimer();
  }

public:
  juce::Component::SafePointer<ImagePreviewButton> safeThis;
  juce::File file;
  juce::Image thumbnail;
  float thumbnailEntrance = 1.0f;

private:
  class ThumbnailJob final : public juce::ThreadPoolJob {
  public:
    ThumbnailJob(juce::File imageFile,
                 juce::Component::SafePointer<ImagePreviewButton> target)
        : juce::ThreadPoolJob("BackgroundThumbnailJob"),
          file(std::move(imageFile)), safeButton(target) {}

    JobStatus runJob() override {
      if (shouldExit())
        return jobHasFinished;

      auto img = juce::ImageFileFormat::loadFrom(file);
      if (shouldExit() || img.isNull() || img.getWidth() <= 0 ||
          img.getHeight() <= 0)
        return jobHasFinished;

      auto thumb =
          img.rescaled(320, 180, juce::Graphics::mediumResamplingQuality);
      if (shouldExit())
        return jobHasFinished;

      juce::MessageManager::callAsync([safeBtn = safeButton, thumb]() {
        if (safeBtn != nullptr) {
          safeBtn->thumbnail = thumb;
          safeBtn->thumbnailEntrance = 0.0f;
          safeBtn->startTimerHz(60);
          safeBtn->repaint();
        }
      });

      return jobHasFinished;
    }

  private:
    juce::File file;
    juce::Component::SafePointer<ImagePreviewButton> safeButton;
  };

  static juce::ThreadPool &sharedThumbnailPool() {
    static juce::ThreadPool pool(getBackgroundThumbnailWorkerThreadCount());
    return pool;
  }
};

class PaletteSelector : public juce::Component {
public:
  std::function<void(juce::Colour)> onColorSelected;
  int selectedIndex = -1;

  void setPalette(const std::vector<juce::Colour> &colors) {
    if (palette == colors)
      return;
    palette = colors;
    if (palette.empty())
      palette = {juce::Colour(0xFF0078D4)};

    selectedIndex = -1;
    repaint();
  }

  void setSelectedIndex(int index) {
    if (selectedIndex != index) {
      selectedIndex = index;
      repaint();
    }
  }

  void setSelectedColor(juce::Colour c) {
    if (palette.empty())
      return;
    for (size_t i = 0; i < palette.size(); ++i) {
      // 允许过渡动画中的轻微舍入误差
      auto p = palette[i];
      if (std::abs(p.getRed() - c.getRed()) <= 1 &&
          std::abs(p.getGreen() - c.getGreen()) <= 1 &&
          std::abs(p.getBlue() - c.getBlue()) <= 1) {
        setSelectedIndex((int)i);
        return;
      }
    }
    // 动画中间色找不到匹配项时保持当前选择，避免闪烁
  }

  void paint(juce::Graphics &g) override {
    if (palette.empty())
      return;

    auto area = getLocalBounds().toFloat();
    int count = std::min((int)palette.size(), 6);
    float gap = 8.0f;
    float maxSwatchWidth = 48.0f;
    float maxSlots = 6.0f;
    float totalGapSpace = (maxSlots - 1.0f) * gap;
    float swatchWidth = juce::jmax(
        1.0f, juce::jmin(maxSwatchWidth,
                         (area.getWidth() - totalGapSpace) / maxSlots));

    for (int i = 0; i < count; ++i) {
      juce::Rectangle<float> r(i * (swatchWidth + gap), 0, swatchWidth,
                               area.getHeight());

      g.setColour(palette[i]);
      g.fillRoundedRectangle(r, 4.0f);

      if (i == selectedIndex) {
        float lux = (palette[i].getFloatRed() * 0.2126f +
                     palette[i].getFloatGreen() * 0.7152f +
                     palette[i].getFloatBlue() * 0.0722f);
        bool isBright = lux > 0.65f;

        g.setColour(isBright ? juce::Colours::black.withAlpha(0.6f)
                             : juce::Colours::white.withAlpha(0.85f));
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 3.5f);

        if (auto *laf = dynamic_cast<FluentLookAndFeel *>(&getLookAndFeel())) {
          g.setColour(isBright ? juce::Colours::black.withAlpha(0.85f)
                               : juce::Colours::white.withAlpha(0.95f));
          laf->drawIconGlyph(g, L"\uE73E", r.toFloat(),
                             LegacyDesignTokens::Icon::small);
        }
      }
    }
  }

  void mouseDown(const juce::MouseEvent &e) override {
    if (palette.empty() || !onColorSelected)
      return;

    int count = std::min((int)palette.size(), 6);
    float gap = 8.0f;
    float maxSlots = 6.0f;
    float totalGapSpace = (maxSlots - 1.0f) * gap;
    float swatchWidth =
        juce::jmax(1.0f, juce::jmin(48.0f, ((float)getWidth() - totalGapSpace) /
                                               maxSlots));

    for (int i = 0; i < count; ++i) {
      float startX = i * (swatchWidth + gap);
      if (e.position.x >= startX && e.position.x <= startX + swatchWidth) {
        setSelectedIndex(i);
        onColorSelected(palette[i]);
        break;
      }
    }
  }

private:
  std::vector<juce::Colour> palette;
};

class BackgroundSettingsDialog : public juce::Component,
                                 public juce::ChangeListener {
public:
  class VectorIconButton : public juce::Button {
  public:
    VectorIconButton(const juce::String &name, bool isLeft)
        : juce::Button(name), left(isLeft) {}
    void paintButton(juce::Graphics &g, bool isMouseOver,
                     bool isButtonDown) override {
      auto bounds = getLocalBounds().toFloat();
      auto *laf = dynamic_cast<FluentLookAndFeel *>(&getLookAndFeel());
      const auto hoverColour =
          laf != nullptr ? laf->getColors().controlHover
                         : juce::Colours::white.withAlpha(0.05f);
      const auto iconColour =
          laf != nullptr ? laf->getColors().textPrimary
                         : juce::Colours::white;
      if (isMouseOver || isButtonDown) {
        g.setColour(isButtonDown ? hoverColour.brighter(0.1f) : hoverColour);
        g.fillRoundedRectangle(bounds, 6.0f);
      }

      g.setColour(isEnabled() ? iconColour : iconColour.withAlpha(0.3f));
      if (laf != nullptr)
        laf->drawSystemIconGlyph(g, left ? L"\uE76B" : L"\uE76C", bounds,
                                 LegacyDesignTokens::Icon::small);

      if (hasKeyboardFocus(false)) {
        g.setColour(iconColour);
        g.drawRoundedRectangle(bounds.reduced(1.5f), 6.0f, 1.5f);
      }
    }
    bool left;
  };

  /**
      HorizontalViewport: 内容超出宽度时，将纵向滚轮转换为横向滚动。
  */
  class HorizontalViewport : public juce::Viewport,
                             private juce::Timer {
  public:
    std::function<void()> onVisibleAreaChanged;

    HorizontalViewport() {
      setScrollBarsShown(false, false);
    }

    ~HorizontalViewport() override {
      stopTimer();
    }

    void setScrollBounds(int maximumPosition) {
      motion.setBounds(0.0f, (float)juce::jmax(0, maximumPosition));
      if (!isTimerRunning())
        motion.setPosition((float)juce::jlimit(
            0, juce::jmax(0, maximumPosition), getViewPositionX()));
    }

    void jumpTo(int position) {
      stopTimer();
      motion.setPosition((float)position);
      setViewPosition(juce::roundToInt(motion.getPosition()), 0);
    }

    void animateTo(int position) {
      motion.setPosition((float)getViewPositionX());
      motion.animateTo((float)position);
      startTimerHz(60);
    }

    void visibleAreaChanged(const juce::Rectangle<int> &) override {
      if (onVisibleAreaChanged)
        onVisibleAreaChanged();
    }

    void mouseWheelMove(const juce::MouseEvent &e,
                        const juce::MouseWheelDetails &wheel) override {
      if (auto *viewedComp = getViewedComponent()) {
        const int maximum = juce::jmax(0, viewedComp->getWidth() - getWidth());
        if (maximum > 0) {
          motion.setBounds(0.0f, (float)maximum);
          if (!isTimerRunning())
            motion.setPosition((float)getViewPositionX());
          const float wheelDelta =
              std::abs(wheel.deltaX) > 0.001f ? -wheel.deltaX
                                               : -wheel.deltaY;
          motion.addImpulse(wheelDelta * (wheel.isSmooth ? 72.0f : 110.0f));
          startTimerHz(60);
          return;
        }
      }
      juce::Viewport::mouseWheelMove(e, wheel);
    }

  private:
    void timerCallback() override {
      const bool moving = motion.tick();
      setViewPosition(juce::roundToInt(motion.getPosition()), 0);

      if (!moving) {
        stopTimer();
      }

      if (onVisibleAreaChanged)
        onVisibleAreaChanged();
    }

    BackgroundScrollMotion motion;
  };

  class Listener {
  public:
    virtual ~Listener() = default;
    virtual void backgroundSettingsChanged(bool reapplyEffects) = 0;
    virtual void dialogMaterialChanged(bool backdropChanged) = 0;
    virtual void backgroundSettingsClosed() = 0;
  };

  BackgroundSettingsDialog(BackgroundComponent &bg, Listener *l,
                           FluentLookAndFeel &laf)
      : background(bg), listener(l), fluentLookAndFeel(laf),
        tooltipOverlay(laf) {
    background.addChangeListener(this);
    setLookAndFeel(&fluentLookAndFeel);
    setOpaque(false);

    addAndMakeVisible(titleLabel);
    titleLabel.setText(L"背景图片", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(titleLabel, fluentLookAndFeel, true);

    addAndMakeVisible(effectsSectionLabel);
    effectsSectionLabel.setText(L"图像效果", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(effectsSectionLabel,
                                        fluentLookAndFeel, true);

    addAndMakeVisible(behaviorSectionLabel);
    behaviorSectionLabel.setText(L"窗口与界面", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(behaviorSectionLabel,
                                        fluentLookAndFeel, true);

    addAndMakeVisible(dialogMaterialLabel);
    dialogMaterialLabel.setText(L"窗口材质", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(dialogMaterialLabel,
                                        fluentLookAndFeel);

    addAndMakeVisible(dialogMaterialCombo);
    dialogMaterialCombo.addItem(L"纯透明", 1);
    dialogMaterialCombo.addItem(L"高斯模糊", 2);
    dialogMaterialCombo.addItem(L"Aero (毛玻璃)", 3);
    dialogMaterialCombo.addItem(L"Acrylic (亚克力)", 4);
    dialogMaterialCombo.setSelectedId(
        static_cast<int>(getAppSettings().getDialogMaterialType()),
        juce::dontSendNotification);
    dialogMaterialCombo.onChange = [this]() {
      getAppSettings().setDialogMaterialType(
          static_cast<WindowMaterial::Type>(dialogMaterialCombo.getSelectedId()));
      updateDialogMaterialControlState();
      applyDialogMaterialSettings(true);
      getAppSettings().save();
    };

    addAndMakeVisible(dialogOpacityLabel);
    dialogOpacityLabel.setText(L"背景不透明度", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(dialogOpacityLabel,
                                        fluentLookAndFeel);

    addAndMakeVisible(dialogOpacitySlider);
    dialogOpacitySlider.setRange(25.0, 98.0, 1.0);
    dialogOpacitySlider.setValue(
        getAppSettings().getDialogMaterialOpacity() * 100.0,
        juce::dontSendNotification);
    dialogOpacitySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58,
                                        24);
    dialogOpacitySlider.setTextValueSuffix("%");
    dialogOpacitySlider.onValueChange = [this]() {
      getAppSettings().setDialogMaterialOpacity(
          (float)dialogOpacitySlider.getValue() / 100.0f);
      applyDialogMaterialSettings(false);
    };
    dialogOpacitySlider.onDragEnd = []() { getAppSettings().save(); };

    addAndMakeVisible(dialogStrengthLabel);
    dialogStrengthLabel.setText(L"效果强度", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(dialogStrengthLabel,
                                        fluentLookAndFeel);

    addAndMakeVisible(dialogStrengthSlider);
    dialogStrengthSlider.setRange(1.0, 50.0, 1.0);
    dialogStrengthSlider.setValue(
        getAppSettings().getDialogMaterialStrength(),
        juce::dontSendNotification);
    dialogStrengthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58,
                                         24);
    dialogStrengthSlider.onValueChange = [this]() {
      getAppSettings().setDialogMaterialStrength(
          juce::roundToInt(dialogStrengthSlider.getValue()));
      applyDialogMaterialSettings(false);
    };
    dialogStrengthSlider.onDragEnd = [this]() {
      getAppSettings().save();
      applyDialogMaterialSettings(true);
    };

    addAndMakeVisible(imageLabel);
    imageLabel.setText(L"图片", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(imageLabel, fluentLookAndFeel);

    addAndMakeVisible(selectImageBtn);
    selectImageBtn.setButtonText(L"选择新图片");
    selectImageBtn.setColour(juce::TextButton::buttonColourId,
                             fluentLookAndFeel.getColors().controlBackground);
    selectImageBtn.onClick = [this]() { selectImage(); };

    addAndMakeVisible(clearImageBtn);
    clearImageBtn.setButtonText(L"清除");
    clearImageBtn.setColour(juce::TextButton::buttonColourId,
                            fluentLookAndFeel.getColors().controlBackground);
    clearImageBtn.onClick = [this]() { clearImage(); };

    addAndMakeVisible(recentImagesLabel);
    recentImagesLabel.setText(L"最近使用", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(recentImagesLabel,
                                        fluentLookAndFeel);

    addAndMakeVisible(historyViewport);
    historyViewport.setViewedComponent(&recentImagesContainer);
    historyViewport.setScrollBarsShown(false, false);
    historyViewport.onVisibleAreaChanged =
        [this]() { updateHistoryNavigationState(); };

    addAndMakeVisible(vectorLeftBtn);
    vectorLeftBtn.onClick = [this] {
      historyViewport.animateTo(
          historyLayout.getPreviousPagePosition(
              historyViewport.getViewPositionX()));
    };

    addAndMakeVisible(vectorRightBtn);
    vectorRightBtn.onClick = [this] {
      historyViewport.animateTo(
          historyLayout.getNextPagePosition(
              historyViewport.getViewPositionX()));
    };

    refreshRecentImages();

    addAndMakeVisible(blurModeLabel);
    blurModeLabel.setText(L"效果", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(blurModeLabel, fluentLookAndFeel);

    addAndMakeVisible(blurModeCombo);
    blurModeCombo.addItem(L"无", 1);
    blurModeCombo.addItem(L"高斯模糊", 2);
    blurModeCombo.addItem(L"Aero (毛玻璃)", 3);
    blurModeCombo.addItem(L"Acrylic (亚克力)", 4);

    // MaterialType 编号与 ComboBox 选项 ID 保持一致
    int selectedId = 1;
    switch (background.getMaterialType()) {
    case BackgroundComponent::MaterialType::GaussianBlur:
      selectedId = 2;
      break;
    case BackgroundComponent::MaterialType::Aero:
      selectedId = 3;
      break;
    case BackgroundComponent::MaterialType::Acrylic:
      selectedId = 4;
      break;
    default:
      selectedId = 1;
      break;
    }
    blurModeCombo.setSelectedId(selectedId, juce::dontSendNotification);

    blurModeCombo.onChange = [this]() {
      int id = blurModeCombo.getSelectedId();
      BackgroundComponent::MaterialType type =
          BackgroundComponent::MaterialType::None;
      switch (id) {
      case 2:
        type = BackgroundComponent::MaterialType::GaussianBlur;
        break;
      case 3:
        type = BackgroundComponent::MaterialType::Aero;
        break;
      case 4:
        type = BackgroundComponent::MaterialType::Acrylic;
        break;
      default:
        type = BackgroundComponent::MaterialType::None;
        break;
      }
      background.setMaterialType(type);
      updateBlurRadiusVisibility();
      if (listener)
        listener->backgroundSettingsChanged(false);
    };

    addAndMakeVisible(blurRadiusLabel);
    blurRadiusLabel.setText(L"强度", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(blurRadiusLabel,
                                        fluentLookAndFeel);

    addAndMakeVisible(blurRadiusSlider);
    blurRadiusSlider.setRange(1, 50, 1);
    blurRadiusSlider.setValue(background.getBlurRadius(),
                              juce::dontSendNotification);
    blurRadiusSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 24);

    blurRadiusSlider.onValueChange = nullptr;
    blurRadiusSlider.onDragEnd = [this]() {
      background.setBlurRadius((int)blurRadiusSlider.getValue());
      if (listener)
        listener->backgroundSettingsChanged(false);
    };

    addAndMakeVisible(overlayLabel);
    overlayLabel.setText(L"遮罩", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(overlayLabel, fluentLookAndFeel);

    addAndMakeVisible(overlaySlider);
    overlaySlider.setRange(0.0, 1.0, 0.05);
    overlaySlider.setValue(background.getOverlayOpacity(),
                           juce::dontSendNotification);
    overlaySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 24);

    overlaySlider.onValueChange = [this]() {
      background.setOverlayOpacity((float)overlaySlider.getValue());
      if (listener)
        listener->backgroundSettingsChanged(false);
    };
    overlaySlider.onDragEnd = nullptr;

    addAndMakeVisible(monetToggle);
    monetToggle.setButtonText(L"自动提取主题色");

    // 背景可能仍在加载，因此同时检查已加载图像和设置中的路径
    bool hasBG = hasConfiguredBackground();
    monetToggle.setEnabled(hasBG);

    bool shouldBeOn = getAppSettings().getMonetEnabled() && hasBG;
    monetToggle.setToggleState(shouldBeOn, juce::dontSendNotification);

    monetToggle.onClick = [this]() {
      bool enabled = monetToggle.getToggleState();
      getAppSettings().setMonetEnabled(enabled);
      getAppSettings().save();
      paletteSelector.setVisible(enabled);

      if (enabled) {
        background.loadAsync(true);
      } else {
        background.setCurrentAccentColor(juce::Colour(0xFF0078D4));
      }

      if (listener)
        listener->backgroundSettingsChanged(false);
    };

    addAndMakeVisible(paletteSelector);
    paletteSelector.setPalette(background.getPalette());
    // 使用目标色同步选中标记，避免过渡中间色导致不匹配
    paletteSelector.setSelectedColor(background.getTargetAccentColor());
    paletteSelector.setVisible(getAppSettings().getMonetEnabled());
    paletteSelector.onColorSelected = [this](juce::Colour c) {
      background.setCurrentAccentColor(c);
      paletteSelector.setSelectedColor(c);
    };

    addAndMakeVisible(sequentialIconToggle);
    sequentialIconToggle.setButtonText(L"更换原生图标样式");
    sequentialIconToggle.setToggleState(
        getAppSettings().getSequentialIconListStyle(),
        juce::dontSendNotification);
    sequentialIconToggle.onClick = [this]() {
      getAppSettings().setSequentialIconListStyle(
          sequentialIconToggle.getToggleState());
      if (listener)
        listener->backgroundSettingsChanged(false);
    };

    addAndMakeVisible(rememberWindowToggle);
    rememberWindowToggle.setButtonText(L"记住窗口位置和大小");
    rememberWindowToggle.setToggleState(
        getAppSettings().getRememberWindowBounds(), juce::dontSendNotification);
    rememberWindowToggle.onClick = [this]() {
      getAppSettings().setRememberWindowBounds(
          rememberWindowToggle.getToggleState());
      getAppSettings().save();
    };

    addAndMakeVisible(currentPathLabel);
    FluentSettingsStyle::configureLabel(currentPathLabel, fluentLookAndFeel,
                                        false, true);
    currentPathLabel.setJustificationType(juce::Justification::centredLeft);
    updateCurrentPath();

    updateBlurRadiusVisibility();
    updateImageDependentControls();
    updateDialogMaterialControlState();

    // ToggleButton widths depend on their final text, so perform the initial
    // layout only after every control has been configured.
    setSize(640, getPreferredHeight());

    addChildComponent(tooltipOverlay);
    addMouseListener(&tooltipOverlay, true);
  }

  void paint(juce::Graphics &g) override {
    FluentSettingsStyle::paintPanel(g, fluentLookAndFeel, getLocalBounds());
    FluentSettingsStyle::paintCard(g, fluentLookAndFeel, sourceCardBounds);
    FluentSettingsStyle::paintCard(g, fluentLookAndFeel, effectsCardBounds);
    FluentSettingsStyle::paintCard(g, fluentLookAndFeel, behaviorCardBounds);
  }

  void resized() override {
    auto area =
        getLocalBounds().reduced(FluentSettingsStyle::panelMargin);
    auto topCards = area.removeFromTop(getTopCardsHeight());
    const int columnWidth = (topCards.getWidth() - 12) / 2;
    sourceCardBounds = topCards.removeFromLeft(columnWidth);
    topCards.removeFromLeft(12);
    effectsCardBounds = topCards;
    area.removeFromTop(12);
    behaviorCardBounds = area;

    auto source =
        sourceCardBounds.reduced(FluentSettingsStyle::cardPadding);
    titleLabel.setBounds(source.removeFromTop(22));
    source.removeFromTop(8);
    auto imageRow =
        source.removeFromTop(
            FluentSettingsStyle::controlHeight(fluentLookAndFeel));
    imageLabel.setBounds(imageRow.removeFromLeft(64));
    clearImageBtn.setBounds(imageRow.removeFromRight(64));
    imageRow.removeFromRight(8);
    selectImageBtn.setBounds(imageRow);
    source.removeFromTop(12);
    auto historyHeader = source.removeFromTop(24);
    auto navigation = historyHeader.removeFromRight(60);
    vectorRightBtn.setBounds(navigation.removeFromRight(28));
    navigation.removeFromRight(4);
    vectorLeftBtn.setBounds(navigation.removeFromRight(28));
    recentImagesLabel.setBounds(historyHeader);
    source.removeFromTop(6);
    currentPathLabel.setBounds(source.removeFromBottom(24));
    source.removeFromBottom(8);
    historyViewport.setBounds(source);
    layoutRecentImageButtons();

    auto effects =
        effectsCardBounds.reduced(FluentSettingsStyle::cardPadding);
    effectsSectionLabel.setBounds(effects.removeFromTop(22));
    effects.removeFromTop(8);
    constexpr int effectLabelWidth = 64;
    auto blurRow =
        effects.removeFromTop(
            FluentSettingsStyle::controlHeight(fluentLookAndFeel));
    blurModeLabel.setBounds(blurRow.removeFromLeft(effectLabelWidth));
    blurModeCombo.setBounds(blurRow);
    effects.removeFromTop(FluentSettingsStyle::rowGap);
    auto radiusRow =
        effects.removeFromTop(
            FluentSettingsStyle::controlHeight(fluentLookAndFeel));
    blurRadiusLabel.setBounds(radiusRow.removeFromLeft(effectLabelWidth));
    blurRadiusSlider.setBounds(radiusRow);
    effects.removeFromTop(FluentSettingsStyle::rowGap);
    auto overlayRow =
        effects.removeFromTop(
            FluentSettingsStyle::controlHeight(fluentLookAndFeel));
    overlayLabel.setBounds(overlayRow.removeFromLeft(effectLabelWidth));
    overlaySlider.setBounds(overlayRow);
    effects.removeFromTop(12);
    auto monetRow = effects.removeFromTop(
        FluentSettingsStyle::controlHeight(fluentLookAndFeel));
    monetToggle.setBounds(monetRow.removeFromLeft(juce::jmin(
        monetRow.getWidth(),
        fluentLookAndFeel.getToggleButtonPreferredWidth(monetToggle))));
    effects.removeFromTop(8);
    paletteSelector.setBounds(effects.removeFromTop(40));

    auto behavior =
        behaviorCardBounds.reduced(FluentSettingsStyle::cardPadding);
    behaviorSectionLabel.setBounds(behavior.removeFromTop(22));
    behavior.removeFromTop(8);
    auto toggleRow =
        behavior.removeFromTop(
            FluentSettingsStyle::controlHeight(fluentLookAndFeel));
    const int toggleWidth = (toggleRow.getWidth() - 16) / 2;
    auto sequentialArea = toggleRow.removeFromLeft(toggleWidth);
    sequentialIconToggle.setBounds(sequentialArea.removeFromLeft(juce::jmin(
        sequentialArea.getWidth(), fluentLookAndFeel
                                       .getToggleButtonPreferredWidth(
                                           sequentialIconToggle))));
    toggleRow.removeFromLeft(16);
    rememberWindowToggle.setBounds(toggleRow.removeFromLeft(juce::jmin(
        toggleRow.getWidth(), fluentLookAndFeel.getToggleButtonPreferredWidth(
                                  rememberWindowToggle))));

    constexpr int materialLabelWidth = 112;
    behavior.removeFromTop(FluentSettingsStyle::rowGap);
    auto materialRow = behavior.removeFromTop(
        FluentSettingsStyle::controlHeight(fluentLookAndFeel));
    dialogMaterialLabel.setBounds(
        materialRow.removeFromLeft(materialLabelWidth));
    dialogMaterialCombo.setBounds(materialRow);

    behavior.removeFromTop(FluentSettingsStyle::rowGap);
    auto opacityRow = behavior.removeFromTop(
        FluentSettingsStyle::controlHeight(fluentLookAndFeel));
    dialogOpacityLabel.setBounds(
        opacityRow.removeFromLeft(materialLabelWidth));
    dialogOpacitySlider.setBounds(opacityRow);

    behavior.removeFromTop(FluentSettingsStyle::rowGap);
    auto strengthRow = behavior.removeFromTop(
        FluentSettingsStyle::controlHeight(fluentLookAndFeel));
    dialogStrengthLabel.setBounds(
        strengthRow.removeFromLeft(materialLabelWidth));
    dialogStrengthSlider.setBounds(strengthRow);
  }

  ~BackgroundSettingsDialog() override {
    removeMouseListener(&tooltipOverlay);
    tooltipOverlay.hideTooltip();
    background.removeChangeListener(this);
    if (listener)
      listener->backgroundSettingsClosed();
    setLookAndFeel(nullptr);
  }

  void changeListenerCallback(juce::ChangeBroadcaster *) override {
    bool hasBG = hasConfiguredBackground();

    if (getAppSettings().getMonetEnabled() && !hasBG) {
      getAppSettings().setMonetEnabled(false);
      monetToggle.setToggleState(false, juce::dontSendNotification);
      paletteSelector.setVisible(false);
    } else {
      paletteSelector.setVisible(getAppSettings().getMonetEnabled());
      paletteSelector.setPalette(background.getPalette());
      // setPalette() 会刷新列表，之后恢复当前目标色选中状态
      paletteSelector.setSelectedColor(background.getTargetAccentColor());
    }

    updateImageDependentControls();
    repaint();
  }

  int getTopCardsHeight() const {
    const int rowHeight =
        FluentSettingsStyle::controlHeight(fluentLookAndFeel);
    return FluentSettingsStyle::cardPadding * 2 + 22 + 8 +
           rowHeight * 4 + FluentSettingsStyle::rowGap * 2 + 12 + 8 + 40;
  }

  int getBehaviorCardHeight() const {
    return FluentSettingsStyle::cardPadding * 2 + 22 + 8 +
           FluentSettingsStyle::controlHeight(fluentLookAndFeel) * 4 +
           FluentSettingsStyle::rowGap * 3;
  }

  int getPreferredHeight() const {
    return FluentSettingsStyle::panelMargin * 2 + getTopCardsHeight() + 12 +
           getBehaviorCardHeight();
  }

private:
  void updateDialogMaterialControlState() {
    const bool supportsStrength = WindowMaterial::supportsStrength(
        static_cast<WindowMaterial::Type>(dialogMaterialCombo.getSelectedId()));
    dialogStrengthLabel.setEnabled(supportsStrength);
    dialogStrengthSlider.setEnabled(supportsStrength);
    dialogStrengthLabel.setTooltip({});
    dialogStrengthSlider.setTooltip({});
  }

  void applyDialogMaterialSettings(bool backdropChanged) {
    if (listener) {
      listener->dialogMaterialChanged(backdropChanged);
    } else if (auto *window = findParentComponentOfClass<juce::DialogWindow>()) {
      if (backdropChanged)
        FluentSettingsStyle::refreshDialogMaterial(window);
      else
        FluentSettingsStyle::refreshDialogSurface(window);
    }
  }

  void selectImage() {
    fileChooser = std::make_unique<juce::FileChooser>(
        L"选择背景图片",
        juce::File::getSpecialLocation(juce::File::userPicturesDirectory),
        "*.png;*.jpg;*.jpeg;*.bmp;*.gif");

    auto safeThis =
        juce::Component::SafePointer<BackgroundSettingsDialog>(this);
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode,
        [safeThis](const juce::FileChooser &fc) {
          if (safeThis == nullptr)
            return;
          auto result = fc.getResult();
          if (result.existsAsFile())
            safeThis->showCropDialog(result);
        });
  }

  class CropDialog : public juce::Component {
  public:
    CropDialog(const juce::Image &img,
               FluentLookAndFeel &lookAndFeel,
               std::function<void(const juce::Image &)> onComplete)
        : callback(onComplete) {
      setSize(600, 500);
      addAndMakeVisible(cropper);
      cropper.setImage(img);
      addAndMakeVisible(applyBtn);
      applyBtn.setButtonText(L"应用");
      applyBtn.onClick = [this]() {
        if (callback)
          callback(cropper.getCroppedImage());
        if (auto *dw = findParentComponentOfClass<juce::DialogWindow>())
          dw->closeButtonPressed();
      };
      addAndMakeVisible(cancelBtn);
      cancelBtn.setButtonText(L"取消");
      cancelBtn.onClick = [this]() {
        if (auto *dw = findParentComponentOfClass<juce::DialogWindow>())
          dw->closeButtonPressed();
      };
      addAndMakeVisible(resetBtn);
      resetBtn.setButtonText(L"重置");
      resetBtn.onClick = [this]() { cropper.setCropRegion({0, 0, 1, 1}); };
      addAndMakeVisible(instructionLabel);
      instructionLabel.setText(L"拖动边框调整裁剪区域，或拖动中央移动选区",
                               juce::dontSendNotification);
      FluentSettingsStyle::configureLabel(instructionLabel, lookAndFeel,
                                          false, true);
      instructionLabel.setJustificationType(juce::Justification::centred);
    }

    void resized() override {
      auto area = getLocalBounds().reduced(16);
      instructionLabel.setBounds(area.removeFromTop(24));
      area.removeFromTop(8);
      auto buttonRow = area.removeFromBottom(36);
      int btnWidth = 80;
      int gap = 12;
      auto centered =
          buttonRow.withSizeKeepingCentre(btnWidth * 3 + gap * 2, 36);
      cancelBtn.setBounds(centered.removeFromLeft(btnWidth));
      centered.removeFromLeft(gap);
      resetBtn.setBounds(centered.removeFromLeft(btnWidth));
      centered.removeFromLeft(gap);
      applyBtn.setBounds(centered.removeFromLeft(btnWidth));
      area.removeFromBottom(12);
      cropper.setBounds(area);
    }

  private:
    ImageCropperComponent cropper;
    juce::TextButton applyBtn, cancelBtn, resetBtn;
    juce::Label instructionLabel;
    std::function<void(const juce::Image &)> callback;
  };

  void showCropDialog(const juce::File &imageFile) {
    auto img = juce::ImageFileFormat::loadFrom(imageFile);
    if (img.isNull())
      return;
    auto safeThis =
        juce::Component::SafePointer<BackgroundSettingsDialog>(this);
    auto *dialogContent = new CropDialog(
        img, fluentLookAndFeel,
        [safeThis](const juce::Image &croppedImage) {
          if (safeThis != nullptr && !croppedImage.isNull())
            safeThis->applyCroppedImage(croppedImage);
        });
    dialogContent->setLookAndFeel(&fluentLookAndFeel);
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = L"裁剪图片";
    options.dialogBackgroundColour =
        fluentLookAndFeel.getColors().cardBackground;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = true;
    options.componentToCentreAround = this;
    options.content.setOwned(dialogContent);
    FluentSettingsStyle::launchDialogAsync(options);
  }

  void applyCroppedImage(const juce::Image &croppedImage) {
    auto settingsDir =
        UserSettings::getSettingsDirectory().getChildFile("Backgrounds");
    settingsDir.createDirectory();
    auto uniqueName =
        "bg_" + juce::String(juce::Time::currentTimeMillis()) + ".png";
    auto uniqueFile = settingsDir.getChildFile(uniqueName);

    // 保存前缩放超大图片，避免 PNG 过大和加载变慢
    juce::Image imageToSave = croppedImage;
    int maxW = 2560;
    int maxH = 1440;
    if (imageToSave.isValid() &&
        (imageToSave.getWidth() > maxW || imageToSave.getHeight() > maxH)) {
      if (imageToSave.getWidth() > 0 && imageToSave.getHeight() > 0) {
        float scale = std::min((float)maxW / imageToSave.getWidth(),
                               (float)maxH / imageToSave.getHeight());
        imageToSave =
            imageToSave.rescaled((int)(imageToSave.getWidth() * scale),
                                 (int)(imageToSave.getHeight() * scale),
                                 juce::Graphics::highResamplingQuality);
      }
    }

    juce::PNGImageFormat pngFormat;
    {
      juce::FileOutputStream stream(uniqueFile);
      if (stream.openedOk()) {
        pngFormat.writeImageToStream(imageToSave, stream);
        stream.flush();
      } else
        return;
    }
    uniqueFile = reuseExistingBackgroundIfDuplicate(uniqueFile);
    cleanupOldBackgrounds();

    getAppSettings().setBackgroundImagePath(uniqueFile.getFullPathName());
    getAppSettings().save();
    updateImageDependentControls();

    background.startImageLoadJob(uniqueFile.getFullPathName(), true);

    updateCurrentPath();
    refreshRecentImages();
    if (listener)
      listener->backgroundSettingsChanged(true);
  }

  void cleanupOldBackgrounds() {
    auto settingsDir =
        UserSettings::getSettingsDirectory().getChildFile("Backgrounds");
    auto files =
        settingsDir.findChildFiles(juce::File::findFiles, false, "bg_*.png");
    std::sort(files.begin(), files.end(),
              [](const juce::File &a, const juce::File &b) {
                return a.getLastModificationTime() >
                       b.getLastModificationTime();
              });

    juce::Array<juce::File> retainedFiles;
    for (const auto &file : files) {
      bool isDuplicate = false;
      for (const auto &retained : retainedFiles) {
        if (file.getSize() == retained.getSize() &&
            file.hasIdenticalContentTo(retained)) {
          isDuplicate = true;
          break;
        }
      }

      if (isDuplicate || retainedFiles.size() >= 5)
        file.deleteFile();
      else
        retainedFiles.add(file);
    }
  }

  juce::File reuseExistingBackgroundIfDuplicate(
      const juce::File &candidate) {
    auto files = candidate.getParentDirectory().findChildFiles(
        juce::File::findFiles, false, "bg_*.png");
    for (const auto &existing : files) {
      if (existing == candidate)
        continue;
      if (candidate.getSize() == existing.getSize() &&
          candidate.hasIdenticalContentTo(existing)) {
        candidate.deleteFile();
        existing.setLastModificationTime(juce::Time::getCurrentTime());
        return existing;
      }
    }
    return candidate;
  }

  bool isCurrentBackgroundFile(const juce::File &file) const {
    const auto currentPath = getAppSettings().getBackgroundImagePath();
    return currentPath.isNotEmpty() &&
           juce::File(currentPath).getFullPathName().equalsIgnoreCase(
               file.getFullPathName());
  }

  void showRecentImageContextMenu(
      const juce::File &file,
      juce::Component::SafePointer<ImagePreviewButton> target) {
    if (target == nullptr)
      return;

    const bool isCurrent = isCurrentBackgroundFile(file);
    juce::PopupMenu menu;
    menu.addItem(1,
                 isCurrent ? L"正在使用，无法移除"
                           : L"从最近使用中移除",
                 !isCurrent);

    auto safeThis =
        juce::Component::SafePointer<BackgroundSettingsDialog>(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(target.getComponent()),
        [safeThis, file](int result) {
          if (result == 1 && safeThis != nullptr)
            safeThis->removeRecentImageCache(file);
        });
  }

  void removeRecentImageCache(const juce::File &file) {
    const auto cacheDirectory =
        UserSettings::getSettingsDirectory().getChildFile("Backgrounds");
    const auto currentFile =
        juce::File(getAppSettings().getBackgroundImagePath());
    if (!canRemoveRecentBackgroundCache(cacheDirectory, currentFile, file))
      return;

    if (file.existsAsFile() && !file.deleteFile()) {
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::WarningIcon, L"无法移除背景",
          L"背景缓存文件正在使用或没有删除权限。", L"确定", this);
      return;
    }

    refreshRecentImages();
  }

  void refreshRecentImages() {
    recentImagesContainer.removeAllChildren();
    recentButtons.clear();

    auto settingsDir =
        UserSettings::getSettingsDirectory().getChildFile("Backgrounds");
    if (!settingsDir.exists())
      settingsDir.createDirectory();

    auto files =
        settingsDir.findChildFiles(juce::File::findFiles, false, "bg_*.png");

    std::sort(files.begin(), files.end(),
              [](const juce::File &a, const juce::File &b) {
                return a.getLastModificationTime() >
                       b.getLastModificationTime();
              });

    files = getUniqueRecentBackgroundFiles(files, 10);
    int limit = files.size();

    for (int i = 0; i < limit; ++i) {
      auto f = files[i];
      auto *btn = new ImagePreviewButton(f);
      recentButtons.add(btn);
      btn->onClick = [this, f] {
        getAppSettings().setBackgroundImagePath(f.getFullPathName());
        getAppSettings().save();
        updateImageDependentControls();
        background.startImageLoadJob(f.getFullPathName(),
                                     true);
        updateCurrentPath();
        if (listener)
          listener->backgroundSettingsChanged(true);
      };
      btn->onRemoveRequested = [this, f, safeButton =
                                    juce::Component::SafePointer<
                                        ImagePreviewButton>(btn)] {
        showRecentImageContextMenu(f, safeButton);
      };
      recentImagesContainer.addAndMakeVisible(btn);
    }

    // 图片数量决定箭头是否占位以及 Viewport 的实际宽度。刷新后必须重新
    // 执行整块布局，否则从 1 张变为多张时箭头仍保持空 bounds。
    historyViewport.jumpTo(0);
    resized();
    repaint();
  }

  void layoutRecentImageButtons() {
    const int count = recentButtons.size();
    const int viewportWidth = juce::jmax(1, historyViewport.getWidth());
    const int height = juce::jmax(1, historyViewport.getHeight());
    constexpr int gap = 8;
    historyLayout =
        makeBackgroundHistoryLayout(count, viewportWidth, gap);
    historyViewport.setScrollBounds(historyLayout.maxScrollX);

    if (count == 0) {
      recentImagesContainer.setSize(viewportWidth, height);
      updateHistoryNavigationState();
      return;
    }

    recentImagesContainer.setSize(historyLayout.totalWidth, height);

    for (int index = 0; index < count; ++index)
      recentButtons[index]->setBounds(
          index * (historyLayout.tileWidth + gap), 0,
          historyLayout.tileWidth, height);

    historyViewport.jumpTo(juce::jmin(historyViewport.getViewPositionX(),
                                      historyLayout.maxScrollX));
    updateHistoryNavigationState();
  }

  void updateHistoryNavigationState() {
    const bool hasOverflow = historyLayout.maxScrollX > 0;
    const int position = historyViewport.getViewPositionX();
    const bool canGoLeft =
        hasOverflow && historyLayout.canScrollLeft(position);
    const bool canGoRight =
        hasOverflow && historyLayout.canScrollRight(position);

    vectorLeftBtn.setVisible(hasOverflow);
    vectorRightBtn.setVisible(hasOverflow);
    vectorLeftBtn.setEnabled(canGoLeft);
    vectorRightBtn.setEnabled(canGoRight);
    vectorLeftBtn.setTooltip(canGoLeft ? L"查看较新的背景"
                                       : L"已经是第一组");
    vectorRightBtn.setTooltip(canGoRight ? L"查看更多背景"
                                         : L"已经是最后一组");
  }

  void clearImage() {
    getAppSettings().setBackgroundImagePath("");
    getAppSettings().setMonetEnabled(false);
    getAppSettings().save();
    monetToggle.setToggleState(false, juce::dontSendNotification);
    paletteSelector.setVisible(false);

    background.loadAsync(true);
    updateCurrentPath();
    updateImageDependentControls();

    if (listener)
      listener->backgroundSettingsChanged(
          false);
  }

  void updateCurrentPath() {
    auto path = getAppSettings().getBackgroundImagePath();
    if (path.isEmpty())
      currentPathLabel.setText(L"未选择图片", juce::dontSendNotification);
    else
      currentPathLabel.setText(juce::File(path).getFileName(),
                               juce::dontSendNotification);
  }

  void updateBlurRadiusVisibility() {
    const bool canEdit = isBackgroundEffectStrengthEnabled(
        blurModeCombo.getSelectedId(), hasConfiguredBackground());
    // 保留强度行的布局和语义。无效果时沿用窗口材质“纯透明”的禁用状态。
    blurRadiusLabel.setVisible(true);
    blurRadiusSlider.setVisible(true);
    blurRadiusSlider.setEnabled(canEdit);
    blurRadiusLabel.setEnabled(canEdit);
  }

  bool hasConfiguredBackground() const {
    const auto path = getAppSettings().getBackgroundImagePath();
    return path.isNotEmpty() && juce::File(path).existsAsFile();
  }

  void updateImageDependentControls() {
    const bool hasBackground = hasConfiguredBackground();
    blurModeCombo.setEnabled(hasBackground);
    blurModeLabel.setEnabled(hasBackground);
    overlaySlider.setEnabled(hasBackground);
    overlayLabel.setEnabled(hasBackground);
    monetToggle.setEnabled(hasBackground);
    clearImageBtn.setEnabled(hasBackground);

    const auto unavailableTip =
        hasBackground ? juce::String() : juce::String(L"选择背景图片后可用");
    blurModeCombo.setTooltip(unavailableTip);
    blurRadiusSlider.setTooltip(unavailableTip);
    overlaySlider.setTooltip(unavailableTip);
    monetToggle.setTooltip(unavailableTip);
    updateBlurRadiusVisibility();
  }

private:
  std::unique_ptr<juce::FileChooser> fileChooser;

  BackgroundComponent &background;
  Listener *listener;
  FluentLookAndFeel &fluentLookAndFeel;
  juce::Label titleLabel, effectsSectionLabel, behaviorSectionLabel, imageLabel,
      recentImagesLabel, blurModeLabel, blurRadiusLabel, overlayLabel,
      currentPathLabel, dialogMaterialLabel, dialogOpacityLabel,
      dialogStrengthLabel;
  juce::TextButton selectImageBtn, clearImageBtn;
  VectorIconButton vectorLeftBtn{"<", true}, vectorRightBtn{">", false};
  HorizontalViewport historyViewport;
  juce::Component recentImagesContainer;
  juce::OwnedArray<ImagePreviewButton> recentButtons;
  juce::ComboBox blurModeCombo;
  juce::ComboBox dialogMaterialCombo;
  juce::Slider blurRadiusSlider, overlaySlider, dialogOpacitySlider,
      dialogStrengthSlider;
  juce::ToggleButton monetToggle;
  PaletteSelector paletteSelector;
  juce::ToggleButton sequentialIconToggle;
  juce::ToggleButton rememberWindowToggle;
  juce::Rectangle<int> sourceCardBounds, effectsCardBounds,
      behaviorCardBounds;
  BackgroundHistoryLayout historyLayout;
  EmbeddedTooltip tooltipOverlay;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BackgroundSettingsDialog)
};
