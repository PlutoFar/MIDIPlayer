#pragma once
#include "ExportSettings.h"
class AudioEngine;

// Writes encoded audio from the engine's exclusive offline session.
class OfflineRenderer {
public:
  explicit OfflineRenderer(AudioEngine &owner) : engine(owner) {}
  bool runOfflineExport(const juce::File &outputFile,
                        const ExportSettings &settings,
                        std::function<void(float)> progressCallback,
                        std::function<bool()> shouldCancel);

private:
  AudioEngine &engine;
};
