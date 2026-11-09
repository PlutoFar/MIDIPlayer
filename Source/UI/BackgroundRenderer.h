#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>
#include <juce_events/juce_events.h>
#include <juce_graphics/juce_graphics.h>

namespace midi::background {

enum class Material { None = 1, GaussianBlur = 2, Aero = 3, Acrylic = 4 };

struct RenderRequest {
  uint64_t imageRevision;
  juce::File file;
  juce::Image loadedImage;
  Material material;
  int radius;
  bool extractPalette;
};

struct RenderResult {
  juce::Image original;
  juce::Image processed;
  std::vector<juce::Colour> palette;
};

// 提交、结果回调及析构在消息线程执行。工作线程独占解码与取色缓存。
class Renderer final : private juce::Thread, private juce::AsyncUpdater {
public:
  using Completion = std::function<void(RenderResult)>;

  Renderer();
  ~Renderer() override;
  bool submit(RenderRequest request, Completion completion);
  static RenderResult renderInitial(const RenderRequest &request);

private:
  struct Job {
    uint64_t revision;
    RenderRequest request;
    Completion completion;
  };

  struct Delivery {
    RenderResult result;
    Completion completion;
  };

  struct ImageCache {
    std::optional<uint64_t> revision;
    juce::Image original;
    std::vector<juce::Colour> palette;
  };

  static RenderResult render(const RenderRequest &request, ImageCache &cache,
                             const std::function<bool()> &cancelled);
  void run() override;
  void handleAsyncUpdate() override;

  juce::CriticalSection queueLock;
  std::optional<Job> pending;
  std::optional<Delivery> delivery;
  std::atomic<uint64_t> latestRevision{0};
};

} // namespace midi::background
