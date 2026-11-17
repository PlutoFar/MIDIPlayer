#include "TestHarness.h"
#include "UI/BackgroundComponent.h"
#include "UI/BackgroundHistory.h"
#include <algorithm>

namespace miditest {
namespace {

juce::Image swatch(juce::Colour colour) {
  juce::Image image(juce::Image::RGB, 64, 64, false);
  image.clear(image.getBounds(), colour);
  return image;
}

bool waitForBackground(const std::function<bool()> &ready) {
  const auto start = juce::Time::getMillisecondCounter();
  while (!ready() && juce::Time::getMillisecondCounter() - start < 5000)
    juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
  return ready();
}

} // namespace

int runBackgroundTests() {
  int failures = 0;
  const TestDirectory directory;
  BackgroundHistory history(directory.path.getChildFile("Backgrounds"));
  juce::File redFile, blueFile, duplicate;
  const auto red = swatch(juce::Colours::red);
  if (!history.store(red, redFile).wasOk() ||
      !history.store(swatch(juce::Colours::blue), blueFile).wasOk())
    throw std::runtime_error("Could not create background fixtures");
  expect(failures, history.store(red, duplicate).wasOk() &&
                       duplicate == redFile && history.recentImages().size() == 2,
         "importing an identical image should reuse the existing history entry");
  expect(failures, history.remove(blueFile, blueFile).failed() && blueFile.existsAsFile(),
         "history should protect the active image");

  using midi::background::Material;
  using midi::background::RenderRequest;
  using midi::background::RenderResult;
  using midi::background::Renderer;

  const juce::Colour swatchColour(37, 112, 221);
  juce::Image opaque(juce::Image::RGB, 23, 9, false, juce::SoftwareImageType{});
  opaque.clear(opaque.getBounds(), swatchColour);
  const auto opaqueBlur = Renderer::renderInitial({1, {}, opaque, Material::GaussianBlur, 1, false}).processed;
  expect(failures, opaqueBlur.isValid() && opaqueBlur.getPixelAt(0, 0) == swatchColour &&
                       opaqueBlur.getPixelAt(22, 8) == swatchColour,
         "Gaussian blur should preserve opaque RGB pixels and their colour at padded row edges");

  juce::Image translucent(juce::Image::ARGB, 23, 9, true, juce::SoftwareImageType{});
  translucent.clear({0, 0, 11, 9}, juce::Colours::red);
  const auto translucentBlur = Renderer::renderInitial({1, {}, translucent, Material::GaussianBlur, 1, false}).processed;
  const auto edgeColour = translucentBlur.getPixelAt(11, 4);
  expect(failures, edgeColour.getAlpha() > 0 && edgeColour.getAlpha() < 255 &&
                       edgeColour.getRed() == 255 && edgeColour.getGreen() == 0 &&
                       edgeColour.getBlue() == 0 && translucentBlur.getPixelAt(22, 4).isTransparent(),
         "Gaussian blur should average premultiplied colour and alpha together");

  const auto acrylic = Renderer::renderInitial({1, {}, translucent, Material::Acrylic, 1, false}).processed;
  bool premultipliedEdge = false;
  if (acrylic.isValid()) {
    const juce::Image::BitmapData pixels(acrylic, juce::Image::BitmapData::readOnly);
    using Pixel = juce::PixelARGB;
    const auto *edge = pixels.getPixelPointer(11, 4);
    const auto alpha = edge[Pixel::indexA];
    premultipliedEdge = alpha == edgeColour.getAlpha() && edge[Pixel::indexR] <= alpha &&
                        edge[Pixel::indexG] <= alpha && edge[Pixel::indexB] <= alpha;
  }
  expect(failures, premultipliedEdge && acrylic.getPixelAt(22, 4).isTransparent(),
         "Acrylic saturation and noise should preserve the premultiplied alpha bounds");

  juce::Image alphaMask(juce::Image::SingleChannel, 23, 9, true, juce::SoftwareImageType{});
  alphaMask.clear(alphaMask.getBounds(), juce::Colours::white.withAlpha(uint8_t{128}));
  const auto maskBlur = Renderer::renderInitial({1, {}, alphaMask, Material::GaussianBlur, 1, false}).processed;
  expect(failures, maskBlur.isValid() && maskBlur.getPixelAt(22, 8).getAlpha() == 128,
         "Gaussian blur should preserve constant alpha in an alpha-only source");

  bool obsoleteDelivered = false;
  std::optional<RenderResult> latest;
  {
    Renderer renderer;
    expect(failures,
           renderer.submit({1, redFile, {}, Material::Acrylic, 30, true},
                           [&](RenderResult) { obsoleteDelivered = true; }) &&
           renderer.submit({2, blueFile, {}, Material::GaussianBlur, 5, true},
                           [&](RenderResult result) { latest = std::move(result); }),
           "the renderer should accept independent image and material requests");
    expect(failures, waitForBackground([&] { return latest.has_value(); }) &&
                         !obsoleteDelivered && latest->processed.isValid() &&
                         !latest->palette.empty() &&
                         latest->palette.front().getBlue() > latest->palette.front().getRed(),
           "the renderer should deliver only the latest image and palette");
  }
  {
    Renderer renderer;
    expect(failures, renderer.submit(
        RenderRequest{3, redFile, {}, Material::None, 5, false},
        [&](RenderResult) { obsoleteDelivered = true; }),
        "the renderer should accept a request before its owner is destroyed");
  }
  juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
  expect(failures, !obsoleteDelivered,
         "destroying the renderer should cancel pending callbacks");

  UserSettings settings(directory.path.getChildFile("Settings.xml"));
  settings.finishPersistence();
  settings.setMonetEnabled(true);
  const juce::Colour initialAccent(100, 100, 100);
  const auto targetAccent = juce::Colour::fromRGBA(106, 96, 100, 252);
  settings.setThemeAccentColor(initialAccent.toString());
  BackgroundComponent background(settings);
  std::vector<juce::Colour> accentChanges;
  background.onAccentColorChanged = [&](juce::Colour colour) {
    accentChanges.push_back(colour);
  };
  background.setCurrentAccentColor(targetAccent);
  expect(failures, waitForBackground([&] {
    return background.getLastExtractedColor() == targetAccent;
  }), "small RGB and alpha changes should converge to the precise target accent");
  const auto completedChanges = accentChanges.size();
  background.setCurrentAccentColor(targetAccent);
  juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
  expect(failures, completedChanges > 0 && accentChanges.size() == completedChanges &&
                       accentChanges.back() == targetAccent &&
                       std::adjacent_find(accentChanges.begin(), accentChanges.end()) == accentChanges.end(),
         "accent changes should be published once per colour and stop at the target");
  background.onAccentColorChanged = {};
  settings.setBackgroundImagePath(redFile.getFullPathName());
  background.loadAsync();
  settings.setBackgroundImagePath({});
  background.loadAsync();
  expect(failures, waitForBackground([&] {
    return !background.hasBackgroundImage() && !background.getPalette().empty() &&
           background.getPalette().front() == juce::Colour(0xFF0078D4);
  }), "clearing during the first load should commit an empty background");

  settings.setBackgroundImagePath(redFile.getFullPathName());
  background.loadAsync();
  settings.setBackgroundImagePath(blueFile.getFullPathName());
  background.loadAsync();
  background.setMaterialType(BackgroundComponent::MaterialType::GaussianBlur);
  expect(failures, waitForBackground([&] {
    const auto &palette = background.getPalette();
    return background.hasBackgroundImage() && !palette.empty() &&
           palette.front().getBlue() > palette.front().getRed();
  }), "a material change during loading should keep the latest selected image");

  settings.setBackgroundImagePath(redFile.getFullPathName());
  background.loadAsync();
  settings.setBackgroundImagePath({});
  background.loadAsync();
  expect(failures, waitForBackground([&] { return !background.hasBackgroundImage(); }),
         "clearing the background should supersede an in-flight image load");
  expect(failures, history.remove(redFile, blueFile).wasOk() &&
                       !redFile.existsAsFile() && history.recentImages().size() == 1,
         "deleting an inactive entry should update the managed history");
  return failures;
}

} // namespace miditest
