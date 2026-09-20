#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

// Ownership: 预分配立体声音频存储；渲染线程为唯一生产方，设备回调为唯一消费方。
// Preconditions: `prepare` 仅在双方停止后调用；读写期间容量固定。
class RealtimeAudioBuffer {
public:
  void prepare(int capacity) {
    audio.setSize(2, capacity + 1);
    fifo.setTotalSize(capacity + 1);
    fifo.reset();
  }
  int freeSamples() const { return fifo.getFreeSpace(); }
  void write(const juce::AudioBuffer<float> &source) {
    const auto region = fifo.write(source.getNumSamples());
    for (int ch = 0; ch < 2; ++ch) {
      audio.copyFrom(ch, region.startIndex1, source, ch, 0, region.blockSize1);
      audio.copyFrom(ch, region.startIndex2, source, ch, region.blockSize1, region.blockSize2);
    }
  }
  int read(juce::AudioBuffer<float> &destination) {
    destination.clear();
    const auto region = fifo.read(destination.getNumSamples());
    for (int ch = 0; ch < juce::jmin(2, destination.getNumChannels()); ++ch) {
      destination.copyFrom(ch, 0, audio, ch, region.startIndex1, region.blockSize1);
      destination.copyFrom(ch, region.blockSize1, audio, ch, region.startIndex2, region.blockSize2);
    }
    return region.blockSize1 + region.blockSize2;
  }
private:
  juce::AbstractFifo fifo{1};
  juce::AudioBuffer<float> audio;
};
