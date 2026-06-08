#pragma once

#include "../Midi/MidiPlayer.h"
#include <juce_audio_processors/juce_audio_processors.h>

/**
    将 MidiPlayer 包装成 JUCE AudioProcessor 节点，使其可以放入 AudioProcessorGraph。
    该节点只产生 MIDI，不处理音频。
*/
class MidiPlayerProcessor : public juce::AudioProcessor {
public:
  MidiPlayerProcessor(MidiPlayer &p) : player(p) {}

  void prepareToPlay(double sampleRate, int) override {
    player.setSampleRate(sampleRate);
  }
  
  void releaseResources() override {}

  void processBlock(juce::AudioBuffer<float> &buffer,
                    juce::MidiBuffer &midiMessages) override {
    midiMessages.clear();
    player.processBlock(midiMessages, buffer.getNumSamples());
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
