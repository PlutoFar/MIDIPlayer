#pragma once
#include "ExportSettings.h"
class AudioEngine;

// Responsibilities: 将独占离线会话的音频写入临时文件，编码完成后替换目标文件。
// Ownership: 借用 `AudioEngine`；调用方负责会话、曲目恢复和引擎寿命。
class OfflineRenderer {
public:
  explicit OfflineRenderer(AudioEngine &owner) : engine(owner) {}
  // Preconditions: 核心已校验设置及正时长序列，`OfflineExportSession::isActive()` 为真；调用线程独占渲染。
  // Concurrency: 进度/取消回调在调用线程执行，不得重入引擎或抛出异常。
  // Postconditions: 编码器创建、写入及目标替换均成功才返回 `true`。
  // Failures: `false` 的原因及取消状态由引擎查询；临时文件由 RAII 回收。
  bool runOfflineExport(const juce::File &outputFile,
                        const ExportSettings &settings,
                        std::function<void(float)> progressCallback,
                        std::function<bool()> shouldCancel);

private:
  AudioEngine &engine;
};
