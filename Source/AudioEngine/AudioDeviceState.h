#pragma once

// Contract: 消息线程从设备服务取得的值快照；列表、通道掩码和配置属于同次查询结果。
// Ownership: 快照不持有设备句柄；设备可能在下一次操作前变化，配置结果仍须检查。
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
