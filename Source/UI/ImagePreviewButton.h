#pragma once

#include "BackgroundThumbnailSupport.h"
#include "CustomLookAndFeel.h"

// Contract: 消息线程维护缩略图按钮及 hover 动画；图像和动画状态只影响预览，不修改背景配置。
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
