#pragma once

#include "../Midi/MidiPlayer.h"
#include <juce_audio_processors/juce_audio_processors.h>

/**
    A processor bridge that allows the MidiPlayer to live inside the
   AudioProcessorGraph.
*/
class MidiPlayerProcessor : public juce::AudioProcessor {
public:
  MidiPlayerProcessor(MidiPlayer &p) : player(p) {}

  void prepareToPlay(double, int) override {}
  void releaseResources() override {}

  void processBlock(juce::AudioBuffer<float> &,
                    juce::MidiBuffer &midiMessages) override {
    player.processBlock(midiMessages, getBlockSize());
  }

  const juce::String getName() const override { return "MidiPlayerNode"; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return true; }
  double getTailLengthSeconds() const override { return 0.0; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return "None"; }
  void changeProgramName(int, const juce::String &) override {}
  bool hasEditor() const override { return false; }
  juce::AudioProcessorEditor *createEditor() override { return nullptr; }
  void getStateInformation(juce::MemoryBlock &) override {}
  void setStateInformation(const void *, int) override {}

private:
  MidiPlayer &player;
};
