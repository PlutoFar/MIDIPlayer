#include "BackgroundRenderer.h"
#include "BackgroundEffectSupport.h"

#include <algorithm>
#include <random>

namespace midi::background {
namespace {

using MaterialType = Material;

struct ImageEffects {
    static juce::Image applyMaterial(const juce::Image &source, MaterialType type,
                                     int radius, const std::function<bool()> &cancelled) {
      switch (type) {
      case MaterialType::GaussianBlur:
        return applyGaussianBlurStatic(source, radius, cancelled);
      case MaterialType::Aero:
        return applyAeroEffectStatic(source, radius, cancelled);
      case MaterialType::Acrylic:
        return applyAcrylicEffectStatic(source, radius, cancelled);
      case MaterialType::None:
        return source;
      }
      return {};
    }
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
          // Reason: 各 RGB 分量相对灰阶扩大差值，以增强材质饱和度。
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
          int noise = (rand() % 12) - 6; // Units: [-6,5] 的单色亮度扰动。

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
        // JUCE 的 RGB 像素在 Windows 上按 BGR 排列，由 BitmapData 统一解码。
        for (int x = 0; x < w; ++x)
          pixels.push_back(data.getPixelColour(x, y));
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
};

} // namespace

Renderer::Renderer() : juce::Thread("BackgroundWorker") {}

Renderer::~Renderer() {
  stopThread(-1);
  cancelPendingUpdate();
}

bool Renderer::submit(RenderRequest request, Completion completion) {
  if (!isThreadRunning() && !startThread())
    return false;
  {
    const juce::ScopedLock lock(queueLock);
    pending.emplace(Job{++latestRevision, std::move(request), std::move(completion)});
    delivery.reset();
  }
  notify();
  return true;
}

RenderResult Renderer::renderInitial(const RenderRequest &request) {
  ImageCache cache;
  return render(request, cache, [] { return false; });
}

RenderResult Renderer::render(const RenderRequest &request, ImageCache &cache,
                              const std::function<bool()> &cancelled) {
  if (cache.revision != request.imageRevision) {
    cache.original = request.loadedImage;
    if (cache.original.isNull() && request.file != juce::File{})
      cache.original = prepareBackgroundImage(juce::ImageFileFormat::loadFrom(request.file));
    cache.revision = request.imageRevision;
    cache.palette.clear();
  }
  if (cancelled())
    return {};

  if (request.extractPalette && cache.original.isValid() && cache.palette.empty()) {
    auto palette = ImageEffects::extractPaletteKMeans(cache.original, 6, cancelled);
    if (cancelled())
      return {};
    cache.palette = std::move(palette);
  }
  auto processed = ImageEffects::applyMaterial(cache.original, request.material,
                                               request.radius, cancelled);
  return {cache.original, std::move(processed),
          request.extractPalette ? cache.palette : std::vector<juce::Colour>{}};
}

void Renderer::run() {
  ImageCache cache;
  while (!threadShouldExit()) {
    std::optional<Job> job;
    {
      const juce::ScopedLock lock(queueLock);
      job = std::exchange(pending, std::nullopt);
    }
    if (!job) {
      wait(-1);
      continue;
    }

    const auto cancelled = [this, revision = job->revision] {
      return threadShouldExit() || latestRevision.load() != revision;
    };
    auto result = render(job->request, cache, cancelled);
    {
      const juce::ScopedLock lock(queueLock);
      if (cancelled())
        continue;
      delivery.emplace(Delivery{std::move(result), std::move(job->completion)});
    }
    triggerAsyncUpdate();
  }
}

void Renderer::handleAsyncUpdate() {
  std::optional<Delivery> completed;
  {
    const juce::ScopedLock lock(queueLock);
    completed = std::exchange(delivery, std::nullopt);
  }
  if (completed)
    completed->completion(std::move(completed->result));
}

} // namespace midi::background
