#include "BackgroundRenderer.h"
#include "BackgroundEffectSupport.h"

#include <algorithm>
#include <array>
#include <random>

namespace midi::background {
namespace {

using MaterialType = Material;

struct ImageEffects {
    static juce::Image applyMaterial(const juce::Image &source, MaterialType type,
                                     int radius, const std::function<bool()> &cancelled) {
      if (source.isNull() || type == MaterialType::None)
        return source;

      // 材质在软件位图上处理预乘 ARGB，统一 RGB、ARGB 和仅含透明度的输入。
      juce::Image argb(juce::Image::ARGB, source.getWidth(), source.getHeight(),
                       false, juce::SoftwareImageType{});
      {
        const juce::Image::BitmapData input(source, juce::Image::BitmapData::readOnly);
        juce::Image::BitmapData output(argb, juce::Image::BitmapData::writeOnly);
        output.convertFrom(input);
      }

      switch (type) {
      case MaterialType::GaussianBlur:
        return applyGaussianBlurStatic(argb, radius, cancelled);
      case MaterialType::Aero:
        return applyAeroEffectStatic(argb, radius, cancelled);
      case MaterialType::Acrylic:
        return applyAcrylicEffectStatic(argb, radius, cancelled);
      case MaterialType::None:
        break;
      }
      return {};
    }
    static juce::Image
    applyGaussianBlurStatic(const juce::Image &source, int radius,
                            std::function<bool()> shouldCancel) {
      if (radius < 1)
        return source;

      // 根据半径动态缩放，避免低半径模糊出现块状感
      int scale = 1;
      if (radius > 16)
        scale = 2;
      if (radius > 32)
        scale = 4;

      int smallW = source.getWidth() / scale;
      int smallH = source.getHeight() / scale;

      if (smallW < 2 || smallH < 2)
        return source;

      auto small =
          source.rescaled(smallW, smallH, juce::Graphics::lowResamplingQuality);

      // 多次 box blur 近似 Gaussian；小半径保持 scale=1，避免过度模糊
      int effectiveRadius = radius / scale;
      if (effectiveRadius < 1)
        effectiveRadius = 1;

      juce::Image horizontal(juce::Image::ARGB, smallW, smallH, false,
                             juce::SoftwareImageType{});
      juce::Image filtered(juce::Image::ARGB, smallW, smallH, false,
                           juce::SoftwareImageType{});
      for (int pass = 0; pass < 3; ++pass) {
        if (!blurAxis(small, horizontal, effectiveRadius + 1, false, shouldCancel) ||
            !blurAxis(horizontal, filtered, effectiveRadius + 1, true, shouldCancel))
          return {};
        small = filtered;
      }

      if (shouldCancel())
        return juce::Image();

      return small.rescaled(source.getWidth(), source.getHeight(),
                            juce::Graphics::mediumResamplingQuality);
    }

    static bool blurAxis(const juce::Image &source, juce::Image &destination,
                         int radius, bool vertical,
                         const std::function<bool()> &shouldCancel) {
      const juce::Image::BitmapData input(source, juce::Image::BitmapData::readOnly);
      const juce::Image::BitmapData output(destination, juce::Image::BitmapData::writeOnly);
      const int lineCount = vertical ? input.width : input.height;
      const int lineLength = vertical ? input.height : input.width;
      const int inputLineStride = vertical ? input.pixelStride : input.lineStride;
      const int outputLineStride = vertical ? output.pixelStride : output.lineStride;
      const int inputStep = vertical ? input.lineStride : input.pixelStride;
      const int outputStep = vertical ? output.lineStride : output.pixelStride;

      for (int line = 0; line < lineCount; ++line) {
        if (shouldCancel())
          return false;

        const auto *inputLine = input.data + line * inputLineStride;
        auto *outputLine = output.data + line * outputLineStride;
        std::array<int, sizeof(juce::PixelARGB)> totals{};
        int sampleCount = 0;
        const auto updateWindow = [&](int index, int weight) {
          const auto *pixel = inputLine + index * inputStep;
          for (size_t channel = 0; channel < totals.size(); ++channel)
            totals[channel] += weight * pixel[channel];
          sampleCount += weight;
        };

        for (int index = 0; index < std::min(radius + 1, lineLength); ++index)
          updateWindow(index, 1);

        for (int index = 0; index < lineLength; ++index) {
          auto *pixel = outputLine + index * outputStep;
          // 颜色和透明度使用同一窗口，透明邻居不会污染剩余颜色。
          for (size_t channel = 0; channel < totals.size(); ++channel)
            pixel[channel] = static_cast<uint8_t>(totals[channel] / sampleCount);
          if (index + radius + 1 < lineLength)
            updateWindow(index + radius + 1, 1);
          if (index >= radius)
            updateWindow(index - radius, -1);
        }
      }
      return true;
    }

    static juce::Image
    applyAeroEffectStatic(const juce::Image &source, int radius,
                          std::function<bool()> shouldCancel) {
      // Aero: 中等模糊和更明显的玻璃高光，区别于普通模糊
      auto blurred = applyGaussianBlurStatic(source, radius, shouldCancel);
      if (shouldCancel())
        return {};

      juce::Image result(juce::Image::ARGB, blurred.getWidth(),
                         blurred.getHeight(), true, juce::SoftwareImageType{});
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

      auto result = applyGaussianBlurStatic(source, radius, shouldCancel);

      if (shouldCancel())
        return {};

      uint32_t seed = 123456;
      auto rand = [&seed]() {
        seed = seed * 1103515245 + 12345;
        return (seed / 65536) % 32768;
      };

      using Pixel = juce::PixelARGB;
      {
        const juce::Image::BitmapData data(result, juce::Image::BitmapData::readWrite);
        for (int y = 0; y < result.getHeight(); ++y) {
          if (shouldCancel())
            return {};

          for (int x = 0; x < result.getWidth(); ++x) {
            auto *pixel = data.getPixelPointer(x, y);
            const int alpha = pixel[Pixel::indexA];
            const int grey = (pixel[Pixel::indexR] + pixel[Pixel::indexG] +
                              pixel[Pixel::indexB]) / 3;
            // 噪点以透明度缩放，颜色分量始终处于预乘后的 [0, alpha] 范围。
            const int noise = (static_cast<int>(rand() % 12) - 6) * alpha / 255;
            for (const int channel : {Pixel::indexR, Pixel::indexG, Pixel::indexB}) {
              const int saturated = grey + static_cast<int>((pixel[channel] - grey) * 1.3f);
              const int boosted = juce::jlimit(0, alpha, saturated);
              pixel[channel] = static_cast<uint8_t>(juce::jlimit(0, alpha, boosted + noise));
            }
          }
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
