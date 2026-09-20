#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>
#include <vector>

// Responsibilities: 秒级 MIDI 速度图与拍号转换为插件宿主音乐时间。
// Ownership: 序列快照持有时间点；构造发生在生产方，渲染方只执行无分配查询。
struct MusicalPosition {
  double samplePosition = 0.0;
  double ppqPosition = 0.0;
  double barStartPpq = 0.0;
  double bpm = 120.0;
  int numerator = 4;
  int denominator = 4;
  bool playing = false;
};

class MusicalTimeline {
public:
  void build(const juce::MidiMessageSequence &sequence) {
    points.assign(1, Point{});
    for (int i = 0; i < sequence.getNumEvents(); ++i) {
      const auto &message = sequence.getEventPointer(i)->message;
      if (!message.isTempoMetaEvent() && !message.isTimeSignatureMetaEvent())
        continue;
      auto point = points.back();
      const double time = message.getTimeStamp();
      point.ppq += (time - point.seconds) * point.bpm / 60.0;
      point.seconds = time;
      if (message.isTempoMetaEvent()) {
        const double secondsPerQuarter = message.getTempoSecondsPerQuarterNote();
        if (secondsPerQuarter > 0.0)
          point.bpm = 60.0 / secondsPerQuarter;
      } else {
        message.getTimeSignatureInfo(point.numerator, point.denominator);
        point.barOrigin = point.ppq;
      }
      points.push_back(point);
    }
  }

  // Preconditions: `sampleRate` 为有效采样率；定位和播放位置均使用当前序列采样。
  MusicalPosition at(double samples, double sampleRate, bool playing) const {
    const double seconds = samples / sampleRate;
    const auto upper = std::upper_bound(
        points.begin(), points.end(), seconds,
        [](double time, const Point &point) { return time < point.seconds; });
    const auto &point = upper == points.begin() ? points.front() : *std::prev(upper);
    const double ppq = point.ppq + (seconds - point.seconds) * point.bpm / 60.0;
    const double barLength = point.numerator * 4.0 / point.denominator;
    return {samples, ppq,
            point.barOrigin + std::floor((ppq - point.barOrigin) / barLength) * barLength,
            point.bpm, point.numerator, point.denominator, playing};
  }

private:
  struct Point {
    double seconds = 0.0, ppq = 0.0, barOrigin = 0.0, bpm = 120.0;
    int numerator = 4, denominator = 4;
  };
  std::vector<Point> points{Point{}};
};
