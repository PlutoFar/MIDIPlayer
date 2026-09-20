#pragma once
#include <juce_audio_devices/juce_audio_devices.h>

// Value snapshot: callers cannot mutate the device manager through this type.
struct AudioDeviceState {
  juce::StringArray driverTypes;
  juce::String currentDriver;
  juce::StringArray outputDevices;
  juce::AudioDeviceManager::AudioDeviceSetup setup;
  juce::Array<double> sampleRates;
  juce::Array<int> bufferSizes;
  juce::StringArray channelNames;
  juce::BigInteger activeChannels;
  juce::String deviceName;
  double sampleRate = 0.0;
  int bufferSize = 0;
  int bitDepth = 0;
  int outputLatency = 0;
  bool hasDevice = false;
  bool hasDriver = false;
  bool hasControlPanel = false;
  bool separateInputsAndOutputs = true;
};
