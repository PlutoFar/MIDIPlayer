#pragma once

// 纯图像处理函数：高斯/Aero/亚克力模糊 + Monet K-Means 取色 + 大图降采样。
// 从 Source/UI/BackgroundComponent.h 的 BackgroundWorkerThread 静态实现抽出为
// 自由函数，供 WinUI、对话框软件材质和其他后台图像任务复用。Legacy 背景
// 保留支持协作取消的实现。仅依赖 juce 图像；不含任何 UI / WinRT 类型。

#include <algorithm>
#include <cmath>
#include <functional>
#include <random>
#include <vector>

#include <juce_graphics/juce_graphics.h>

namespace midi::bgfx {

inline juce::Image prepareLoaded(juce::Image img) {
  const int maxW = 2560, maxH = 1440;
  if ((img.getWidth() > maxW || img.getHeight() > maxH) && img.getWidth() > 0 &&
      img.getHeight() > 0) {
    float scale = std::min((float)maxW / img.getWidth(),
                           (float)maxH / img.getHeight());
    img = img.rescaled(std::max(1, (int)(img.getWidth() * scale)),
                       std::max(1, (int)(img.getHeight() * scale)),
                       juce::Graphics::highResamplingQuality);
  }
  // 关键：统一转 ARGB（4 字节/像素）。否则 RGB 源（JPG）在 box blur 里按
  // 4 字节读 p[3] 会越界错位，导致整图洗白/损坏。
  return img.convertedToFormat(juce::Image::ARGB);
}

inline juce::Image boxBlur(const juce::Image &source, int radius) {
  if (radius < 1)
    return source.createCopy();
  int w = source.getWidth(), h = source.getHeight();
  auto result = juce::Image(juce::Image::ARGB, w, h, true);
  juce::Image::BitmapData srcData(source, juce::Image::BitmapData::readOnly);
  juce::Image::BitmapData dstData(result, juce::Image::BitmapData::writeOnly);
  auto temp = juce::Image(juce::Image::ARGB, w, h, true);
  juce::Image::BitmapData tempData(temp, juce::Image::BitmapData::writeOnly);

  for (int y = 0; y < h; ++y) {
    int r = 0, g = 0, b = 0, a = 0, count = 0;
    for (int x = 0; x <= radius && x < w; ++x) {
      auto *p = srcData.getPixelPointer(x, y);
      b += p[0]; g += p[1]; r += p[2]; a += p[3]; ++count;
    }
    for (int x = 0; x < w; ++x) {
      auto *dst = tempData.getPixelPointer(x, y);
      dst[0] = (uint8_t)(b / count); dst[1] = (uint8_t)(g / count);
      dst[2] = (uint8_t)(r / count); dst[3] = (uint8_t)(a / count);
      int addX = x + radius + 1, remX = x - radius;
      if (addX < w) { auto *p = srcData.getPixelPointer(addX, y); b += p[0]; g += p[1]; r += p[2]; a += p[3]; ++count; }
      if (remX >= 0) { auto *p = srcData.getPixelPointer(remX, y); b -= p[0]; g -= p[1]; r -= p[2]; a -= p[3]; --count; }
    }
  }
  juce::Image::BitmapData tempReadData(temp, juce::Image::BitmapData::readOnly);
  for (int x = 0; x < w; ++x) {
    int r = 0, g = 0, b = 0, a = 0, count = 0;
    for (int y = 0; y <= radius && y < h; ++y) {
      auto *p = tempReadData.getPixelPointer(x, y);
      b += p[0]; g += p[1]; r += p[2]; a += p[3]; ++count;
    }
    for (int y = 0; y < h; ++y) {
      auto *dst = dstData.getPixelPointer(x, y);
      dst[0] = (uint8_t)(b / count); dst[1] = (uint8_t)(g / count);
      dst[2] = (uint8_t)(r / count); dst[3] = (uint8_t)(a / count);
      int addY = y + radius + 1, remY = y - radius;
      if (addY < h) { auto *p = tempReadData.getPixelPointer(x, addY); b += p[0]; g += p[1]; r += p[2]; a += p[3]; ++count; }
      if (remY >= 0) { auto *p = tempReadData.getPixelPointer(x, remY); b -= p[0]; g -= p[1]; r -= p[2]; a -= p[3]; --count; }
    }
  }
  return result;
}

inline juce::Image gaussianBlur(const juce::Image &source, int radius) {
  if (source.isNull() || radius < 1)
    return source.createCopy();
  // 高半径时先降采样，降低卷积开销。
  int scale = 1;
  if (radius > 16) scale = 2;
  if (radius > 32) scale = 4;
  int smallW = std::max(1, source.getWidth() / scale);
  int smallH = std::max(1, source.getHeight() / scale);
  juce::Image small =
      (scale > 1)
          ? source.rescaled(smallW, smallH, juce::Graphics::mediumResamplingQuality)
          : source.createCopy();

  int er = std::max(1, radius / scale);
  // 用 JUCE 内置高斯卷积核（正确、不会洗白）。
  juce::ImageConvolutionKernel kernel(er * 2 + 1);
  kernel.createGaussianBlur((float)er);
  juce::Image blurred(juce::Image::ARGB, small.getWidth(), small.getHeight(),
                      true);
  kernel.applyToImage(blurred, small, small.getBounds());

  if (scale > 1)
    return blurred.rescaled(source.getWidth(), source.getHeight(),
                            juce::Graphics::mediumResamplingQuality);
  return blurred;
}

inline juce::Image aeroBackdrop(const juce::Image &source, int radius) {
  if (source.isNull())
    return source.createCopy();
  auto blurred = gaussianBlur(source, radius);
  if (blurred.isNull())
    return {};
  juce::Image result(juce::Image::ARGB, blurred.getWidth(), blurred.getHeight(), true);
  {
    juce::Graphics g(result);
    g.drawImageAt(blurred, 0, 0);
    juce::ColourGradient shine(juce::Colours::white.withAlpha(0.25f), 0, 0,
                               juce::Colours::white.withAlpha(0.05f), 0,
                               (float)result.getHeight() * 0.6f, false);
    g.setGradientFill(shine);
    g.fillRect(result.getBounds());
  }
  return result;
}

inline juce::Image aero(const juce::Image &source, int radius) {
  auto result = aeroBackdrop(source, radius);
  if (result.isNull())
    return {};
  {
    juce::Graphics g(result);
    g.setColour(juce::Colours::white.withAlpha(0.4f));
    g.fillRect(0, 0, result.getWidth(), 1);
    g.setColour(juce::Colours::white.withAlpha(0.15f));
    g.drawRect(result.getBounds(), 1);
  }
  return result;
}

inline juce::Image acrylicBackdrop(const juce::Image &source, int radius) {
  auto blurred = gaussianBlur(source, radius);
  if (blurred.isNull())
    return {};
  juce::Image result = blurred.createCopy();
  juce::Image::BitmapData data(result, 0, 0, result.getWidth(), result.getHeight(),
                               juce::Image::BitmapData::readWrite);
  for (int y = 0; y < result.getHeight(); ++y) {
    uint8_t *p = data.getLinePointer(y);
    for (int x = 0; x < result.getWidth(); ++x) {
      if (data.pixelStride >= 3) {
        int b = p[0], g = p[1], r = p[2];
        int grey = (r + g + b) / 3;
        r = grey + (int)((r - grey) * 1.3f);
        g = grey + (int)((g - grey) * 1.3f);
        b = grey + (int)((b - grey) * 1.3f);
        p[2] = (uint8_t)juce::jlimit(0, 255, r);
        p[1] = (uint8_t)juce::jlimit(0, 255, g);
        p[0] = (uint8_t)juce::jlimit(0, 255, b);
      }
      p += data.pixelStride;
    }
  }
  return result;
}

inline juce::Image acrylic(const juce::Image &source, int radius) {
  auto result = acrylicBackdrop(source, radius);
  if (result.isNull())
    return {};
  juce::Image::BitmapData data(result, 0, 0, result.getWidth(), result.getHeight(),
                               juce::Image::BitmapData::readWrite);
  uint32_t seed = 123456;
  auto rand = [&seed]() { seed = seed * 1103515245 + 12345; return (seed / 65536) % 32768; };
  for (int y = 0; y < result.getHeight(); ++y) {
    uint8_t *p = data.getLinePointer(y);
    for (int x = 0; x < result.getWidth(); ++x) {
      int noise = (rand() % 12) - 6;
      const int chans = juce::jmin(3, data.pixelStride);
      for (int c = 0; c < chans; ++c)
        p[c] = (uint8_t)juce::jlimit(0, 255, p[c] + noise);
      p += data.pixelStride;
    }
  }
  return result;
}

// type: 1=None 2=GaussianBlur 3=Aero 4=Acrylic
inline juce::Image applyMaterial(const juce::Image &source, int type, int radius) {
  switch (type) {
  case 2: return gaussianBlur(source, radius);
  case 3: return aero(source, radius);
  case 4: return acrylic(source, radius);
  default: return source.createCopy();
  }
}

inline std::vector<juce::Colour> extractPaletteKMeans(const juce::Image &image, int k) {
  if (image.isNull())
    return {juce::Colour(0xFF0078D4)};
  auto workImg = image.rescaled(150, 150, juce::Graphics::lowResamplingQuality);
  int w = workImg.getWidth(), h = workImg.getHeight();
  std::vector<juce::Colour> pixels;
  pixels.reserve(w * h);
  juce::Image::BitmapData data(workImg, juce::Image::BitmapData::readOnly);
  for (int y = 0; y < h; ++y) {
    auto *p = data.getLinePointer(y);
    for (int x = 0; x < w; ++x) {
      int ps = data.pixelStride;
      pixels.push_back(juce::Colour(p[ps == 4 ? 2 : 0], p[1], p[ps == 4 ? 0 : 2]));
      p += ps;
    }
  }
  if (pixels.empty())
    return {juce::Colour(0xFF0078D4)};

  struct Centroid {
    float r = 0, g = 0, b = 0; int count = 0;
    void add(juce::Colour c) { r += c.getFloatRed(); g += c.getFloatGreen(); b += c.getFloatBlue(); count++; }
    juce::Colour avg() const { return count == 0 ? juce::Colours::black : juce::Colour::fromFloatRGBA(r / count, g / count, b / count, 1.0f); }
  };
  int numClusters = 8;
  std::vector<juce::Colour> centers;
  std::mt19937 rng(42);
  std::uniform_int_distribution<int> dist(0, (int)pixels.size() - 1);
  for (int i = 0; i < numClusters; ++i) centers.push_back(pixels[dist(rng)]);
  for (int iter = 0; iter < 5; ++iter) {
    std::vector<Centroid> nc(numClusters);
    for (const auto &p : pixels) {
      int best = 0; float minD = 1e9f;
      float pr = p.getFloatRed(), pg = p.getFloatGreen(), pb = p.getFloatBlue();
      for (int i = 0; i < numClusters; ++i) {
        float dr = pr - centers[i].getFloatRed(), dg = pg - centers[i].getFloatGreen(), db = pb - centers[i].getFloatBlue();
        float d = dr * dr + dg * dg + db * db;
        if (d < minD) { minD = d; best = i; }
      }
      nc[best].add(p);
    }
    for (int i = 0; i < numClusters; ++i)
      centers[i] = nc[i].count > 0 ? nc[i].avg() : pixels[dist(rng)];
  }
  struct Scored { juce::Colour c; float score; };
  std::vector<Scored> ranked;
  for (const auto &c : centers) {
    float sat = c.getSaturation(), bri = c.getBrightness();
    float score = sat * 2.0f + (bri > 0.5f ? 0.5f : 0.0f);
    if (bri < 0.15f || bri > 0.95f) score *= 0.1f;
    ranked.push_back({c, score});
  }
  std::sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.score > b.score; });
  std::vector<juce::Colour> result;
  for (const auto &rc : ranked) {
    bool unique = true;
    for (const auto &ex : result) {
      float dh = std::abs(rc.c.getHue() - ex.getHue());
      if (dh > 0.5f) dh = 1.0f - dh;
      if (dh < 0.08f) { unique = false; break; }
      int dr = rc.c.getRed() - ex.getRed(), dg = rc.c.getGreen() - ex.getGreen(), db = rc.c.getBlue() - ex.getBlue();
      if (dr * dr + dg * dg + db * db < 2500) { unique = false; break; }
    }
    if (unique) result.push_back(rc.c);
    if ((int)result.size() >= k) break;
  }
  while ((int)result.size() < k) {
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

} // namespace midi::bgfx
