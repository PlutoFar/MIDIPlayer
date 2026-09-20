#pragma once

#include <juce_core/juce_core.h>

// Contract: 界面与编码器共用的导出参数；写文件前必须经过 `validateExportFormatSettings`。
// Units: 采样率为 Hz，尾音长度为秒；质量索引属于对应编码器的选项列表。
struct ExportSettings {
  juce::String formatName = "WAV"; // 编码格式："WAV"、"FLAC" 或 "Ogg Vorbis"
  double sampleRate = 96000.0;     // 导出采样率，必须由目标编码器支持
  int bitDepth = 24;               // 目标位深，必须由对应 JUCE AudioFormat 支持
  bool useFloatingPoint = false;   // 32-bit WAV 和 Ogg Vorbis 使用浮点输入
  bool autoTail =
      true; // true 时按实际尾音电平结束，false 时使用 fixedTailSeconds
  double fixedTailSeconds = 3.0; // 固定尾音长度，仅在 autoTail 为 false 时生效

  juce::String title;   // 可选文件元数据标题
  int qualityIndex = 0; // FLAC/Ogg Vorbis 的质量或压缩等级索引
};
