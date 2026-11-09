#include "TestHarness.h"
#include "UI/BackgroundComponent.h"
#include "UI/BackgroundHistory.h"

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
  BackgroundComponent background(settings);
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
