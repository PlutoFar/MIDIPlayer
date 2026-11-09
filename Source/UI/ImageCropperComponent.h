#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Contract: 消息线程维护裁剪交互；`cropRegion` 为原图归一化坐标，外部设置须提供有效区域。
// Ownership: 保存 JUCE 图像引用，裁剪结果可能共享源图像存储；空源图返回空结果。
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
