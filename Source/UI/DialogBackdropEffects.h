#pragma once

#include <juce_graphics/juce_graphics.h>

namespace midi::bgfx {

// 对话框使用 ARGB 快照。大半径模糊先缩小快照，卷积完成后恢复原尺寸。
inline juce::Image gaussianBlur(const juce::Image &source, int radius) {
  if (source.isNull() || radius < 1)
    return source.createCopy();

  const int scale = radius > 32 ? 4 : (radius > 16 ? 2 : 1);
  const int width = std::max(1, source.getWidth() / scale);
  const int height = std::max(1, source.getHeight() / scale);
  const auto input = scale > 1
                         ? source.rescaled(width, height, juce::Graphics::mediumResamplingQuality)
                         : source.createCopy();
  const int kernelRadius = std::max(1, radius / scale);
  juce::ImageConvolutionKernel kernel(kernelRadius * 2 + 1);
  kernel.createGaussianBlur(static_cast<float>(kernelRadius));
  juce::Image blurred(juce::Image::ARGB, width, height, true);
  kernel.applyToImage(blurred, input, input.getBounds());

  return scale > 1
             ? blurred.rescaled(source.getWidth(), source.getHeight(),
                                juce::Graphics::mediumResamplingQuality)
             : blurred;
}

inline juce::Image aeroBackdrop(const juce::Image &source, int radius) {
  auto blurred = gaussianBlur(source, radius);
  if (blurred.isNull())
    return {};
  juce::Image result(juce::Image::ARGB, blurred.getWidth(), blurred.getHeight(), true);
  juce::Graphics graphics(result);
  graphics.drawImageAt(blurred, 0, 0);
  juce::ColourGradient shine(juce::Colours::white.withAlpha(0.25f), 0, 0,
                             juce::Colours::white.withAlpha(0.05f), 0,
                             static_cast<float>(result.getHeight()) * 0.6f, false);
  graphics.setGradientFill(shine);
  graphics.fillRect(result.getBounds());
  return result;
}

inline juce::Image acrylicBackdrop(const juce::Image &source, int radius) {
  auto result = gaussianBlur(source, radius);
  if (result.isNull())
    return {};
  juce::Image::BitmapData data(result, juce::Image::BitmapData::readWrite);
  for (int y = 0; y < result.getHeight(); ++y) {
    for (int x = 0; x < result.getWidth(); ++x) {
      auto *pixel = data.getPixelPointer(x, y);
      const int grey = (pixel[juce::PixelARGB::indexR] +
                        pixel[juce::PixelARGB::indexG] +
                        pixel[juce::PixelARGB::indexB]) / 3;
      for (const int channel : {juce::PixelARGB::indexR, juce::PixelARGB::indexG,
                                juce::PixelARGB::indexB}) {
        const int saturated = grey + static_cast<int>((pixel[channel] - grey) * 1.3f);
        pixel[channel] = static_cast<uint8_t>(juce::jlimit(0, 255, saturated));
      }
    }
  }
  return result;
}

} // namespace midi::bgfx
