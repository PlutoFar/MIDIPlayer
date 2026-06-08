#pragma once

#include "../Utils/DebugLogger.h"
#include "../Utils/UserSettings.h"
#include "CustomLookAndFeel.h"
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
    // 取消待处理任务并等待线程结束
    cancelPendingWork();
  }

  void loadAsync(bool forceExtraction = false) {
    auto path = getAppSettings().getBackgroundImagePath();

    // 冗余检查：如果路径没变且非强制更新，则跳过
    if (!forceExtraction && path == lastLoadedPath) {
      return;
    }

    lastLoadedPath = path;
    LOG_DEBUG("BackgroundComponent::loadAsync - Path: " + path);

    if (isFirstLoad && path.isNotEmpty()) {
      // 首次启动同步加载已保存背景，避免异步刷新前出现空白
      loadSynchronously(path, forceExtraction);
    } else {
      // 启动后台加载任务
      startImageLoadJob(path, forceExtraction);
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
    workerThread->notify(); // 唤醒后台线程
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

    // 尝试非阻塞获取锁，避免在paint中永久等待
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

    // 1. 绘制基础背景
    juce::ColourGradient gradient(juce::Colour(0xFF0D0D0F), 0.0f, 0.0f,
                                  juce::Colour(0xFF1A1A22), 0.0f,
                                  (float)getHeight(), false);
    g.setGradientFill(gradient);
    g.fillRect(bounds);

    // 2. 交叉淡入淡出绘制图片
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
      // 旧图片淡出
      drawImageWithAlpha(previousImageCopy, 1.0f - imageTransitionAlpha);
      // 新图片淡入
      drawImageWithAlpha(currentImage, imageTransitionAlpha);
    } else {
      // 稳定状态直接绘制当前图
      if (!currentImage.isNull()) {
        drawImageWithAlpha(currentImage, 1.0f);
      } else if (!originalImageCopy.isNull()) {
        drawImageWithAlpha(originalImageCopy, 1.0f);
      }
    }

    // 3. 绘制遮罩色
    g.setOpacity(1.0f);
    g.setColour(tintColor.withAlpha(overlayOpacity));
    g.fillRect(bounds);

  }

  // Timer 回调用于平滑过渡
  void timerCallback() override {
    // Watchdog 请求停止时立即退出过渡
    if (shouldStopNow.load()) {
      stopTimer();
      isTransitioningImage = false;
      isTransitioningColor = false;
      return;
    }

    // 1. 图片淡入淡出过渡
    if (isTransitioningImage) {
      transitionAlpha += 0.08f; // 60fps 下约 300ms
      if (transitionAlpha >= 1.0f) {
        transitionAlpha = 1.0f;
        checkTimerState(); // 无其他过渡时停止 Timer
        repaint();
      } else {
        repaint();
      }
    }

    // 2. 主题色过渡
    if (isTransitioningColor) {
      auto c1 = lastExtractedColor;
      auto c2 = targetAccentColor;

      if (c1 == c2) {
        isTransitioningColor = false;
        checkTimerState();
        return;
      }

      // 单步混合
      auto stepParams = [](uint8_t current, uint8_t target) -> uint8_t {
        int diff = (int)target - (int)current;
        if (std::abs(diff) <= 2)
          return target; // 接近目标时直接吸附
        return (uint8_t)(current + diff * 0.15f);
      };

      juce::Colour next(stepParams(c1.getRed(), c2.getRed()),
                        stepParams(c1.getGreen(), c2.getGreen()),
                        stepParams(c1.getBlue(), c2.getBlue()));

      lastExtractedColor = next;
      if (onAccentColorChanged)
        onAccentColorChanged(next);
      sendChangeMessage(); // 通知 PaletteSelector 等监听者

      if (lastExtractedColor == targetAccentColor) {
        isTransitioningColor = false;
        checkTimerState();
        sendChangeMessage(); // 通知过渡完成
      }
      // checkTimerState 可能已停止 Timer，这里重新确认
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
    startTimerHz(60); // 确保 Timer 正在运行
  }

  // Watchdog 紧急重置
  void emergencyReset() {
    LOG_DEBUG("[FREEZE_DIAG] BackgroundComponent::emergencyReset - start");
    cancelPendingWork();
    LOG_DEBUG("[FREEZE_DIAG] BackgroundComponent::emergencyReset - "
              "cancelPendingWork done");

    stopTimer();
    isTransitioningImage = false;
    isTransitioningColor = false;
    transitionAlpha = 1.0f;
    shouldStopNow.store(false);

    // 使用 tryEnter 避免在紧急重置时死锁
    if (!imageLock.tryEnter()) {
      LOG_DEBUG("[FREEZE_DIAG] BackgroundComponent::emergencyReset - imageLock "
                "busy, skipping lock");
      // 不等待锁，直接重置状态
    } else {
      previousImage = juce::Image();
      imageLock.exit();
    }

    // 清理加载状态，重资源由取消流程处理
    loadingLabel.setVisible(false);
    LOG_DEBUG("[FREEZE_DIAG] BackgroundComponent::emergencyReset - end");
  }

  // Watchdog 发出的线程安全停止请求
  void requestEmergencyStop() { shouldStopNow.store(true); }

  void onColorExtracted(const std::vector<juce::Colour> &palette) {
    if (palette.empty())
      return;

    currentPalette = palette;
    // 默认选择评分最高的第一个颜色
    auto bestColor = palette[0];

    // 立即更新目标色
    targetAccentColor = bestColor;

    // 启动颜色过渡
    startColorTransition();

    // 立即通知一次，保证调色板列表响应及时
    if (onAccentColorChanged)
      onAccentColorChanged(
          lastExtractedColor); // 先发送当前色，后续由 Timer 推进

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
  void checkTimerState() {
    if (!isTransitioningImage && !isTransitioningColor) {
      stopTimer();
    }
  }

private:
  /**
      后台工作线程，用于图像加载、缩放、效果计算和主题色提取。

      处理策略：
      1. 原图超过 2560x1440 时先降采样，减少后续模糊计算量。
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
      abortCurrentTask = true; // 请求当前任务协作取消
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
      abortCurrentTask = true; // 请求当前任务协作取消
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

        // 取出待处理任务
        {
          const juce::ScopedLock sl(taskLock);
          if (hasLoadTask) {
            loadPath = pendingLoadPath;
            loadSettings = pendingLoadSettings;
            doLoad = true;
            hasLoadTask = false;
            abortCurrentTask = false; // 新任务开始前清除取消标记
          } else if (hasEffectTask) {
            imgToProcess = pendingImage;
            type = pendingType;
            radius = pendingRadius;
            doEffect = true;
            hasEffectTask = false;
            abortCurrentTask = false; // 新任务开始前清除取消标记
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
          // 无任务时等待唤醒
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
        lastExtractedPath = ""; // 清空取色缓存
        // 清空图片并恢复默认主题色
        juce::MessageManager::callAsync([safeComponent]() {
          if (safeComponent == nullptr)
            return;

          safeComponent->onImageLoaded(juce::Image());
          safeComponent->onColorExtracted(
              {juce::Colour(0xFF0078D4)}); // 恢复默认色
        });
        return;
      }

      // 路径变化时更新取色缓存；重复加载同一路径时沿用已有结果
      if (path == lastExtractedPath) {
      } else {
        lastExtractedPath = path;
      }

      if (file.existsAsFile() && !threadShouldExit()) {
        auto img = juce::ImageFileFormat::loadFrom(file);
        if (!threadShouldExit() && !img.isNull()) {
          // Monet 调色板提取
          if (settingsSnapshot.monetEnabled) {
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

          // 大图先缩放到 2560x1440 内，避免 4K/8K 背景拖慢模糊效果
          int maxW = 2560;
          int maxH = 1440;
          if ((img.getWidth() > maxW || img.getHeight() > maxH) &&
              img.getWidth() > 0 && img.getHeight() > 0) {
            float scale = std::min((float)maxW / img.getWidth(),
                                   (float)maxH / img.getHeight());
            int newW = std::max(1, (int)(img.getWidth() * scale));
            int newH = std::max(1, (int)(img.getHeight() * scale));
            img =
                img.rescaled(newW, newH, juce::Graphics::highResamplingQuality);
          }

          // 加载或缩放后再次检查取消状态
          if (threadShouldExit() || abortCurrentTask)
            return;

          // 保存原图并触发效果处理
          juce::MessageManager::callAsync([safeComponent, img]() {
            if (safeComponent == nullptr)
              return;

            safeComponent->onImageLoaded(img);
          });
        }
      } else {
        // 文件不存在时按清空背景处理
        juce::MessageManager::callAsync([safeComponent]() {
          if (safeComponent == nullptr)
            return;

          safeComponent->onImageLoaded(juce::Image());
          safeComponent->onColorExtracted(
              {juce::Colour(0xFF0078D4)}); // 恢复默认色
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
    // 带取消检查的静态效果函数
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

      // 缩放回原尺寸
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

      // 横向模糊
      auto temp = juce::Image(juce::Image::ARGB, w, h, true);
      juce::Image::BitmapData tempData(temp,
                                       juce::Image::BitmapData::writeOnly);

      for (int y = 0; y < h; ++y) {
        if (shouldCancel())
          return juce::Image();

        int r = 0, g = 0, b = 0, a = 0, count = 0;

        // 初始化滑动窗口
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

          // 移动滑动窗口
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

      // 纵向模糊
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
    applyMicaEffectStatic(const juce::Image &source, int radius,
                          std::function<bool()> shouldCancel) {
      if (source.isNull())
        return source.createCopy();

      // Mica: 重模糊、低饱和度和浅色蒙层
      int micaRadius = std::max(60, radius * 2);
      auto blurred = applyGaussianBlurStatic(source, micaRadius, shouldCancel);

      if (shouldCancel())
        return {};

      juce::Image result(juce::Image::ARGB, blurred.getWidth(),
                         blurred.getHeight(), true);
      {
        juce::Graphics g(result);
        g.drawImageAt(blurred, 0, 0);

        // Mica 通常使用较轻的去饱和浅色蒙层
        g.setColour(juce::Colour(0xFFF3F3F3).withAlpha(0.65f));
        g.fillRect(result.getBounds());
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

        // 玻璃边框
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.drawRect(result.getBounds(), 1);
      }
      return result;
    }

    static juce::Image
    applyAcrylicEffectStatic(const juce::Image &source, int radius,
                             std::function<bool()> shouldCancel) {
      // Acrylic = 模糊 + 饱和度增强 + 亮度噪点

      // 1. 模糊
      auto blurred = applyGaussianBlurStatic(source, radius, shouldCancel);

      if (shouldCancel())
        return {};

      // 2. 准备像素处理
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

          for (int c = 0; c < 3; ++c) { // R、G、B
            int val = p[data.pixelStride == 4 ? c + 1 : c] + noise;
            p[data.pixelStride == 4 ? c + 1 : c] =
                (uint8_t)juce::jlimit(0, 255, val);
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

      // 1. 降采样到 150x150，足够做主色分析并显著降低计算量
      auto workImg =
          image.rescaled(150, 150, juce::Graphics::lowResamplingQuality);
      int w = workImg.getWidth();
      int h = workImg.getHeight();

      // 2. 收集像素
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

      // 3. 简化 K-Means
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

      // 4. 为聚类结果评分
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

      // 5. 贪心选择差异明显的颜色
      std::vector<juce::Colour> result;
      for (const auto &rc : ranked) {
        bool unique = true;
        for (const auto &ex : result) {
          // 色相距离检查
          float dh = std::abs(rc.c.getHue() - ex.getHue());
          if (dh > 0.5f)
            dh = 1.0f - dh;
          if (dh < 0.08f) { // 允许少量渐变相近色
            unique = false;
            break;
          }
          // RGB 距离保证视觉差异
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

      // 代表色不足时生成变化色兜底
      while (result.size() < k) {
        if (!result.empty()) {
          // 基于最后一个颜色调整色相、亮度和饱和度
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

    // 恢复持久化主题色
    auto savedColor =
        juce::Colour::fromString(getAppSettings().getThemeAccentColor());
    lastExtractedColor = savedColor;
    targetAccentColor = savedColor;
  }

  void applyEffects() {
    // 无效果时快速处理，但仍走统一的淡入淡出流程
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

  // 启动阶段同步加载，保证首帧显示已保存背景
  void loadSynchronously(const juce::String &path, bool forceExtraction) {
    auto file = juce::File(path);
    if (!file.existsAsFile())
      return;

    auto img = juce::ImageFileFormat::loadFrom(file);
    if (img.isNull())
      return;

    // 1. 同步 Monet 取色
    if (getAppSettings().getMonetEnabled()) {
      auto palette = BackgroundWorkerThread::extractPaletteKMeans(
          img, 6, []() { return false; });
      if (!palette.empty()) {
        currentPalette = palette;
        targetAccentColor = palette[0];
        lastExtractedColor = targetAccentColor; // 首次加载直接使用目标色
        if (onAccentColorChanged)
          onAccentColorChanged(targetAccentColor);
      }
    }

    // 2. 为效果处理降采样
    int maxW = 2560;
    int maxH = 1440;
    if ((img.getWidth() > maxW || img.getHeight() > maxH) &&
        img.getWidth() > 0 && img.getHeight() > 0) {
      float scale =
          std::min((float)maxW / img.getWidth(), (float)maxH / img.getHeight());
      int newW = std::max(1, (int)(img.getWidth() * scale));
      int newH = std::max(1, (int)(img.getHeight() * scale));
      img = img.rescaled(newW, newH, juce::Graphics::highResamplingQuality);
    }

    // 3. 同步应用效果
    juce::Image result;
    auto noCancel = []() { return false; };

    switch (materialType) {
    case MaterialType::GaussianBlur:
      result = BackgroundWorkerThread::applyGaussianBlurStatic(img, blurRadius,
                                                               noCancel);
      break;
    case MaterialType::Aero:
      result = BackgroundWorkerThread::applyAeroEffectStatic(img, blurRadius,
                                                             noCancel);
      break;
    case MaterialType::Acrylic:
      result = BackgroundWorkerThread::applyAcrylicEffectStatic(img, blurRadius,
                                                                noCancel);
      break;
    default:
      result = img.createCopy();
      break;
    }

    // 4. 直接写入显示状态
    {
      const juce::ScopedLock sl(imageLock);
      originalImage = img;
      processedImage = result;
      transitionAlpha = 1.0f;
      isTransitioningImage = false;
      isFirstLoad = false;
    }
    repaint();
  }

  void cancelPendingWork() {
    if (workerThread != nullptr) {
      LOG_DEBUG("[FREEZE_DIAG] cancelPendingWork - signaling thread to exit");
      workerThread->stopThread(2000);

      // 短暂等待后台线程响应停止请求，再释放线程对象
      workerThread.reset();
      LOG_DEBUG("[FREEZE_DIAG] cancelPendingWork - done");
    }
  }

  // 后台线程回调，实际在消息线程执行
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
      // previousImage 保存当前画面，用于切换过渡
      previousImage = processedImage.isNull() ? originalImage : processedImage;
      processedImage = img;

      if (isFirstLoad) {
        transitionAlpha = 1.0f;
        isTransitioningImage = false;
        isFirstLoad = false;
      } else {
        transitionAlpha = 0.0f;
        isTransitioningImage = true; // 交给 Timer 推进过渡
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
  std::atomic<bool> shouldStopNow{false};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BackgroundComponent)
};

/**
    ImageCropperComponent: 图片裁剪区域选择组件。
*/
class ImageCropperComponent : public juce::Component {
public:
  ImageCropperComponent() { setInterceptsMouseClicks(true, true); }

  void setImage(const juce::Image &img) {
    originalImage = img;
    if (!img.isNull()) {
      // 默认选中整张图片
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

    // 绘制变暗的完整图片
    g.setOpacity(0.4f);
    g.drawImage(originalImage, bounds, juce::RectanglePlacement::centred);

    // 计算图片实际显示区域
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

    // 绘制高亮裁剪区域
    auto cropDisplayRect = juce::Rectangle<float>(
        imageDisplayRect.getX() +
            cropRegion.getX() * imageDisplayRect.getWidth(),
        imageDisplayRect.getY() +
            cropRegion.getY() * imageDisplayRect.getHeight(),
        cropRegion.getWidth() * imageDisplayRect.getWidth(),
        cropRegion.getHeight() * imageDisplayRect.getHeight());

    // 裁剪区域保持完整亮度
    g.setOpacity(1.0f);
    g.saveState();
    g.reduceClipRegion(cropDisplayRect.toNearestInt());
    g.drawImage(originalImage, imageDisplayRect,
                juce::RectanglePlacement::centred);
    g.restoreState();

    // 裁剪边框
    g.setColour(juce::Colour(0xFFFF8C00));
    g.drawRect(cropDisplayRect, 2.0f);

    // 四角拖拽手柄
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

    // 检查四角拖拽手柄
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

    // 限制在有效区域内
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

/**
    BackgroundSettingsDialog 辅助组件。
*/
class ImagePreviewButton : public juce::Button, private juce::Thread {
public:
  ImagePreviewButton(const juce::File &f)
      : juce::Button(f.getFileName()), juce::Thread("ThumbLoader"), file(f) {
    setTooltip(f.getFileName());
    // 使用 SafePointer 避免异步回调访问已销毁按钮
    safeThis = this;
    startThread();
  }

  ~ImagePreviewButton() override { stopThread(1000); }

  void run() override {
    auto img = juce::ImageFileFormat::loadFrom(file);
    if (!img.isNull() && img.getWidth() > 0 && img.getHeight() > 0 &&
        !threadShouldExit()) {
      auto thumb =
          img.rescaled(140, 80, juce::Graphics::mediumResamplingQuality);
      if (!threadShouldExit()) {
        juce::MessageManager::callAsync([safeBtn = safeThis, thumb]() {
          if (safeBtn != nullptr) {
            safeBtn->thumbnail = thumb;
            safeBtn->repaint();
          }
        });
      }
    }
  }

  void paintButton(juce::Graphics &g, bool isMouseOver,
                   bool isButtonDown) override {
    auto bounds = getLocalBounds().toFloat();

    // 深色占位背景
    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.fillRoundedRectangle(bounds, 4.0f);

    if (thumbnail.isValid()) {
      // 直接按按钮边界填充缩略图，避免裁剪区域和按钮坐标不一致导致错位
      g.setOpacity(1.0f);
      g.drawImage(thumbnail, bounds, juce::RectanglePlacement::fillDestination);

      // 轻微边框
      g.setColour(juce::Colours::white.withAlpha(0.1f));
      g.drawRoundedRectangle(bounds, 4.0f, 1.0f);
    } else {
      // 加载指示
      g.setColour(juce::Colours::white.withAlpha(0.2f));
      g.drawText(L"\u22EF", getLocalBounds(), juce::Justification::centred,
                 false);
    }

    // 鼠标交互外框反馈
    if (isEnabled() && (isMouseOver || isButtonDown)) {
      g.setColour(juce::Colours::white.withAlpha(isButtonDown ? 0.9f : 0.5f));
      g.drawRoundedRectangle(bounds, 4.0f, isButtonDown ? 2.0f : 1.5f);
    }
  }

  juce::Component::SafePointer<ImagePreviewButton> safeThis;
  juce::File file;
  juce::Image thumbnail;
};

class PaletteSelector : public juce::Component {
public:
  std::function<void(juce::Colour)> onColorSelected;
  int selectedIndex = -1; // 用索引跟踪当前选中色块

  void setPalette(const std::vector<juce::Colour> &colors) {
    if (palette == colors)
      return; // 调色板未变化时跳过重绘
    palette = colors;
    if (palette.empty())
      palette = {juce::Colour(0xFF0078D4)};

    // 新调色板需要重新匹配选中色
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
    // 根据颜色查找对应索引
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

    // 按固定间隔排列色块
    for (int i = 0; i < count; ++i) {
      juce::Rectangle<float> r(i * (swatchWidth + gap), 0, swatchWidth,
                               area.getHeight());

      // 色块背景
      g.setColour(palette[i]);
      g.fillRoundedRectangle(r, 4.0f);

      // 严格按索引判断选中状态
      if (i == selectedIndex) {
        // 根据亮度选择高对比选中标记
        float lux = (palette[i].getFloatRed() * 0.2126f +
                     palette[i].getFloatGreen() * 0.7152f +
                     palette[i].getFloatBlue() * 0.0722f);
        bool isBright = lux > 0.65f;

        g.setColour(isBright ? juce::Colours::black.withAlpha(0.6f)
                             : juce::Colours::white.withAlpha(0.85f));
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 3.5f);

        if (auto *laf = dynamic_cast<FluentLookAndFeel *>(&getLookAndFeel())) {
          g.setFont(laf->getIconFont(14.0f));
          g.setColour(isBright ? juce::Colours::black.withAlpha(0.85f)
                               : juce::Colours::white.withAlpha(0.95f));
          g.drawText(L"\uE73E", r, juce::Justification::centred, false);
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
        setSelectedIndex(i); // 固定当前选中索引
        onColorSelected(palette[i]);
        break;
      }
    }
  }

private:
  std::vector<juce::Colour> palette;
};

/**
    BackgroundSettingsDialog: 背景设置弹窗。
*/
class BackgroundSettingsDialog : public juce::Component,
                                 public juce::ChangeListener {
public:
  // 矢量箭头按钮
  class VectorIconButton : public juce::Button {
  public:
    VectorIconButton(const juce::String &name, bool isLeft)
        : juce::Button(name), left(isLeft) {}
    void paintButton(juce::Graphics &g, bool isMouseOver,
                     bool isButtonDown) override {
      auto bounds = getLocalBounds().toFloat();
      if (isMouseOver || isButtonDown) {
        g.setColour(
            juce::Colours::white.withAlpha(isButtonDown ? 0.1f : 0.05f));
        g.fillRoundedRectangle(bounds, 4.0f);
      }

      g.setColour(isEnabled() ? juce::Colours::white
                              : juce::Colours::white.withAlpha(0.3f));
      juce::Path p;
      float s = std::min(bounds.getWidth(), bounds.getHeight()) * 0.35f;
      float cx = bounds.getCentreX();
      float cy = bounds.getCentreY();

      if (left) {
        p.startNewSubPath(cx + s / 2, cy - s / 2);
        p.lineTo(cx - s / 2, cy);
        p.lineTo(cx + s / 2, cy + s / 2);
      } else {
        p.startNewSubPath(cx - s / 2, cy - s / 2);
        p.lineTo(cx + s / 2, cy);
        p.lineTo(cx - s / 2, cy + s / 2);
      }
      g.strokePath(p, juce::PathStrokeType(1.5f));
    }
    bool left;
  };

  /**
      HorizontalViewport: 内容超出宽度时，将纵向滚轮转换为横向滚动。
  */
  class HorizontalViewport : public juce::Viewport {
  public:
    HorizontalViewport() {
      setScrollBarsShown(false, false); // 隐藏滚动条
    }

    void mouseWheelMove(const juce::MouseEvent &e,
                        const juce::MouseWheelDetails &wheel) override {
      if (auto *viewedComp = getViewedComponent()) {
        // 仅在内容宽于视口时接管滚轮
        if (viewedComp->getWidth() > getWidth()) {
          auto newWheel = wheel;
          // 没有横向滚动量时，把纵向滚轮映射到横向
          if (std::abs(newWheel.deltaX) < 0.001f &&
              std::abs(newWheel.deltaY) > 0.001f) {
            newWheel.deltaX = newWheel.deltaY;
            newWheel.deltaY = 0.0f;
          }
          juce::Viewport::mouseWheelMove(e, newWheel);
          return;
        }
      }
      // 其他情况沿用 Viewport 默认行为
      juce::Viewport::mouseWheelMove(e, wheel);
    }
  };

  class Listener {
  public:
    virtual ~Listener() = default;
    virtual void backgroundSettingsChanged(bool reapplyEffects) = 0;
    virtual void backgroundSettingsClosed() = 0;
  };

  BackgroundSettingsDialog(BackgroundComponent &bg, Listener *l)
      : background(bg), listener(l) {
    background.addChangeListener(this);
    setSize(400, 600); // 标准弹窗尺寸
    setOpaque(false);

    addAndMakeVisible(forceExitBtn);
    forceExitBtn.setButtonText(L"强制退出并关闭程序");
    // 使用 Windows 11 深色模式下的关键系统色
    forceExitBtn.setColour(juce::TextButton::buttonColourId,
                           juce::Colour(0xFFC42B1C));
    forceExitBtn.setColour(juce::TextButton::textColourOffId,
                            juce::Colours::white);
    forceExitBtn.onClick = [this]() {
      // 强制退出路径：绕过常规关闭流程和未保存提示
      std::exit(0);
    };

    addAndMakeVisible(titleLabel);
    titleLabel.setText(L"背景设置", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(18.0f).withStyle("Bold")));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);

    addAndMakeVisible(imageLabel);
    imageLabel.setText(L"背景图片:", juce::dontSendNotification);
    imageLabel.setColour(juce::Label::textColourId,
                         juce::Colours::white.withAlpha(0.8f));

    addAndMakeVisible(selectImageBtn);
    selectImageBtn.setButtonText(L"选择新图片");
    // 与 ComboBox 空闲背景保持一致
    selectImageBtn.setColour(juce::TextButton::buttonColourId,
                             juce::Colours::black.withAlpha(0.15f));
    selectImageBtn.onClick = [this]() { selectImage(); };

    addAndMakeVisible(clearImageBtn);
    clearImageBtn.setButtonText(L"清除");
    clearImageBtn.setColour(juce::TextButton::buttonColourId,
                            juce::Colours::black.withAlpha(0.15f));
    clearImageBtn.onClick = [this]() { clearImage(); };

    addAndMakeVisible(recentImagesLabel);
    recentImagesLabel.setText(L"历史记录:", juce::dontSendNotification);
    recentImagesLabel.setColour(juce::Label::textColourId,
                                juce::Colours::white.withAlpha(0.8f));
    recentImagesLabel.setFont(juce::Font(juce::FontOptions(13.0f)));

    addAndMakeVisible(historyViewport);
    historyViewport.setViewedComponent(&recentImagesContainer);
    historyViewport.setScrollBarsShown(false, false);

    addAndMakeVisible(vectorLeftBtn);
    vectorLeftBtn.onClick = [this] {
      historyViewport.setViewPosition(
          juce::jmax(0, historyViewport.getViewPositionX() - 80), 0);
    };

    addAndMakeVisible(vectorRightBtn);
    vectorRightBtn.onClick = [this] {
      historyViewport.setViewPosition(historyViewport.getViewPositionX() + 80,
                                      0);
    };

    refreshRecentImages();

    addAndMakeVisible(blurModeLabel);
    blurModeLabel.setText(L"图片效果:", juce::dontSendNotification);
    blurModeLabel.setColour(juce::Label::textColourId,
                            juce::Colours::white.withAlpha(0.8f));

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
    addAndMakeVisible(blurRadiusLabel);
    blurRadiusLabel.setText(L"效果强度:", juce::dontSendNotification);
    blurRadiusLabel.setColour(juce::Label::textColourId,
                              juce::Colours::white.withAlpha(0.8f));

    addAndMakeVisible(blurRadiusSlider);
    blurRadiusSlider.setRange(1, 50, 1);
    blurRadiusSlider.setValue(background.getBlurRadius(),
                              juce::dontSendNotification);
    blurRadiusSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 24);

    // 模糊处理较重，只在拖动结束后应用
    blurRadiusSlider.onValueChange = nullptr; // 清除可能存在的即时回调
    blurRadiusSlider.onDragEnd = [this]() {
      background.setBlurRadius((int)blurRadiusSlider.getValue());
      if (listener)
        listener->backgroundSettingsChanged(false); // 刷新 UI，不重新加载图片
    };

    addAndMakeVisible(overlayLabel);
    overlayLabel.setText(L"遮罩不透明度:", juce::dontSendNotification);
    overlayLabel.setColour(juce::Label::textColourId,
                           juce::Colours::white.withAlpha(0.8f));

    addAndMakeVisible(overlaySlider);
    overlaySlider.setRange(0.0, 1.0, 0.05);
    overlaySlider.setValue(background.getOverlayOpacity(),
                           juce::dontSendNotification);
    overlaySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 24);

    // 遮罩透明度只触发重绘，可即时响应
    overlaySlider.onValueChange = [this]() {
      background.setOverlayOpacity((float)overlaySlider.getValue());
      if (listener)
        listener->backgroundSettingsChanged(false); // 只重绘，不重新加载
    };
    overlaySlider.onDragEnd = nullptr;

    addAndMakeVisible(monetToggle);
    monetToggle.setButtonText(L"莫奈取色 (自动主题色)");
    monetToggle.setColour(juce::ToggleButton::textColourId,
                          juce::Colours::white);

    // 背景可能仍在加载，因此同时检查已加载图像和设置中的路径
    bool hasBG = background.hasBackgroundImage() ||
                 getAppSettings().getBackgroundImagePath().isNotEmpty();
    monetToggle.setEnabled(hasBG);

    bool shouldBeOn = getAppSettings().getMonetEnabled() && hasBG;
    monetToggle.setToggleState(shouldBeOn, juce::dontSendNotification);

    monetToggle.onClick = [this]() {
      bool enabled = monetToggle.getToggleState();
      getAppSettings().setMonetEnabled(enabled);
      getAppSettings().save(); // 立即持久化 Monet 开关
      paletteSelector.setVisible(enabled);

      if (enabled) {
        background.loadAsync(true); // 强制重新提取 Monet 调色板
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
      paletteSelector.setSelectedColor(c); // 更新选中标记
    };

    addAndMakeVisible(sequentialIconToggle);
    sequentialIconToggle.setButtonText(L"更换原生图标样式");
    sequentialIconToggle.setColour(juce::ToggleButton::textColourId,
                                   juce::Colours::white);
    sequentialIconToggle.setToggleState(
        getAppSettings().getSequentialIconListStyle(),
        juce::dontSendNotification);
    sequentialIconToggle.onClick = [this]() {
      getAppSettings().setSequentialIconListStyle(
          sequentialIconToggle.getToggleState());
      // 只需通知重绘，不需要重新应用背景效果
      if (listener)
        listener->backgroundSettingsChanged(false);
    };

    addAndMakeVisible(rememberWindowToggle);
    rememberWindowToggle.setButtonText(L"记住窗口位置和大小");
    rememberWindowToggle.setColour(juce::ToggleButton::textColourId,
                                   juce::Colours::white);
    rememberWindowToggle.setToggleState(
        getAppSettings().getRememberWindowBounds(), juce::dontSendNotification);
    rememberWindowToggle.onClick = [this]() {
      getAppSettings().setRememberWindowBounds(
          rememberWindowToggle.getToggleState());
      getAppSettings().save();
    };

    addAndMakeVisible(currentPathLabel);
    currentPathLabel.setColour(juce::Label::textColourId,
                               juce::Colours::white.withAlpha(0.5f));
    currentPathLabel.setFont(juce::Font(juce::FontOptions(11.0f)));
    currentPathLabel.setJustificationType(juce::Justification::centred);
    updateCurrentPath();

    updateBlurRadiusVisibility();
  }

  void paint(juce::Graphics &g) override {
    if (auto *laf = dynamic_cast<FluentLookAndFeel *>(&getLookAndFeel())) {
      auto &colors = laf->getColors();
      g.fillAll(colors.cardBackground);
    } else {
      g.fillAll(juce::Colours::black.withAlpha(0.7f));
    }
  }

  void resized() override {
    auto area = getLocalBounds().reduced(20);
    int rowHeight = 36;
    int labelWidth = 110;
    int gap = 10;

    titleLabel.setBounds(area.removeFromTop(40));
    area.removeFromTop(gap);

    auto imageRow = area.removeFromTop(rowHeight);
    imageLabel.setBounds(imageRow.removeFromLeft(labelWidth));
    clearImageBtn.setBounds(imageRow.removeFromRight(70));
    imageRow.removeFromRight(gap);
    selectImageBtn.setBounds(imageRow);
    area.removeFromTop(gap);

    recentImagesLabel.setBounds(area.removeFromTop(20));
    auto historyRow = area.removeFromTop(44);
    vectorLeftBtn.setBounds(historyRow.removeFromLeft(44));
    vectorRightBtn.setBounds(historyRow.removeFromRight(44));
    historyViewport.setBounds(historyRow.reduced(2, 0));
    area.removeFromTop(gap);

    currentPathLabel.setBounds(area.removeFromTop(20));
    area.removeFromTop(gap);

    auto blurRow = area.removeFromTop(rowHeight);
    blurModeLabel.setBounds(blurRow.removeFromLeft(labelWidth));
    blurModeCombo.setBounds(blurRow);
    area.removeFromTop(gap);

    auto radiusRow = area.removeFromTop(rowHeight);
    blurRadiusLabel.setBounds(radiusRow.removeFromLeft(labelWidth));
    blurRadiusSlider.setBounds(radiusRow);
    area.removeFromTop(gap);

    auto overlayRow = area.removeFromTop(rowHeight);
    overlayLabel.setBounds(overlayRow.removeFromLeft(labelWidth));
    overlaySlider.setBounds(overlayRow);
    area.removeFromTop(gap);

    monetToggle.setBounds(area.removeFromTop(24).reduced(20, 0));
    area.removeFromTop(16);
    paletteSelector.setBounds(area.removeFromTop(44).reduced(20, 0));
    area.removeFromTop(10);

    sequentialIconToggle.setBounds(
        area.removeFromTop(24).reduced(labelWidth, 0));

    rememberWindowToggle.setBounds(
        area.removeFromTop(24).reduced(labelWidth, 0));

    area.removeFromTop(gap * 2);
    forceExitBtn.setBounds(area.removeFromTop(36).reduced(40, 0));
  }

  ~BackgroundSettingsDialog() override {
    background.removeChangeListener(this);
    if (listener)
      listener->backgroundSettingsClosed();
  }

  void changeListenerCallback(juce::ChangeBroadcaster *) override {
    bool hasBG = background.hasBackgroundImage();
    monetToggle.setEnabled(hasBG);

    // 背景清空后自动关闭 Monet
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

    repaint();
  }

private:
  void selectImage() {
    fileChooser = std::make_unique<juce::FileChooser>(
        L"选择背景图片",
        juce::File::getSpecialLocation(juce::File::userPicturesDirectory),
        "*.png;*.jpg;*.jpeg;*.bmp;*.gif");

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode,
                             [this](const juce::FileChooser &fc) {
                               auto result = fc.getResult();
                               if (result.existsAsFile()) {
                                 pendingImagePath = result.getFullPathName();
                                 showCropDialog(result);
                               }
                             });
  }

  class CropDialog : public juce::Component {
  public:
    CropDialog(const juce::Image &img,
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
      instructionLabel.setColour(juce::Label::textColourId,
                                 juce::Colours::white.withAlpha(0.7f));
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
    cropDialog = std::make_unique<CropDialog>(
        img, [this](const juce::Image &croppedImage) {
          if (!croppedImage.isNull())
            applyCroppedImage(croppedImage);
          cropDialog.reset();
        });
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = L"裁剪图片";
    options.dialogBackgroundColour = juce::Colour(0xFF202020);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = true;
    options.content.setOwned(cropDialog.release());
    options.launchAsync();
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
    cleanupOldBackgrounds();

    // 保存新背景路径到设置
    getAppSettings().setBackgroundImagePath(uniqueFile.getFullPathName());
    getAppSettings().save();

    // 直接开始加载任务，不重复调用 loadAsync
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
    for (int i = 5; i < files.size(); ++i)
      files[i].deleteFile();
  }

  void refreshRecentImages() {
    recentImagesContainer.removeAllChildren();
    recentButtons.clear();

    // 确保背景缓存目录存在
    auto settingsDir =
        UserSettings::getSettingsDirectory().getChildFile("Backgrounds");
    if (!settingsDir.exists())
      settingsDir.createDirectory();

    auto files =
        settingsDir.findChildFiles(juce::File::findFiles, false, "bg_*.png");

    // 按修改时间倒序排列
    std::sort(files.begin(), files.end(),
              [](const juce::File &a, const juce::File &b) {
                return a.getLastModificationTime() >
                       b.getLastModificationTime();
              });

    int limit = juce::jmin((int)files.size(), 10); // 最多显示 10 张
    int w = 70, h = 40, gap = 8;

    int totalWidth = limit * (w + gap);
    recentImagesContainer.setSize(totalWidth, h);

    for (int i = 0; i < limit; ++i) {
      auto f = files[i];
      auto *btn = new ImagePreviewButton(f);
      recentButtons.add(btn);
      btn->setBounds(i * (w + gap), 0, w, h);
      btn->onClick = [this, f] {
        getAppSettings().setBackgroundImagePath(f.getFullPathName());
        getAppSettings().save();
        background.startImageLoadJob(f.getFullPathName(),
                                     true); // 重新提取 Monet 调色板
        updateCurrentPath();
        if (listener)
          listener->backgroundSettingsChanged(true);
      };
      recentImagesContainer.addAndMakeVisible(btn);
    }

    // 只要存在历史图片就显示滚动按钮
    vectorLeftBtn.setVisible(limit > 0);
    vectorRightBtn.setVisible(limit > 0);
  }

  void clearImage() {
    getAppSettings().setBackgroundImagePath("");
    getAppSettings().setMonetEnabled(false);
    getAppSettings().save();
    monetToggle.setToggleState(false, juce::dontSendNotification);
    paletteSelector.setVisible(false);

    // 仅通过监听器或直接调用触发一次加载
    background.loadAsync(true); // 强制刷新以清除图片
    updateCurrentPath();

    if (listener)
      listener->backgroundSettingsChanged(
          false); // 不需要再次 reapplyEffects，因为已经调过 loadAsync
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
    bool showRadius = blurModeCombo.getSelectedId() > 1;
    blurRadiusLabel.setVisible(showRadius);
    blurRadiusSlider.setVisible(showRadius);
  }

private:
  std::unique_ptr<CropDialog> cropDialog;
  std::unique_ptr<juce::FileChooser> fileChooser;

  BackgroundComponent &background;
  Listener *listener;
  juce::Label titleLabel, imageLabel, recentImagesLabel, blurModeLabel,
      blurRadiusLabel, overlayLabel, currentPathLabel;
  juce::TextButton selectImageBtn, clearImageBtn, forceExitBtn;
  VectorIconButton vectorLeftBtn{"<", true}, vectorRightBtn{">", false};
  HorizontalViewport historyViewport;
  juce::Component recentImagesContainer;
  juce::OwnedArray<ImagePreviewButton> recentButtons;
  juce::ComboBox blurModeCombo;
  juce::Slider blurRadiusSlider, overlaySlider;
  juce::ToggleButton monetToggle;
  PaletteSelector paletteSelector;
  juce::String pendingImagePath;
  juce::ToggleButton sequentialIconToggle;
  juce::ToggleButton rememberWindowToggle;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BackgroundSettingsDialog)
};
