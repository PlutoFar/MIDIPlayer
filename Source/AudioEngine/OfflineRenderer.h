#pragma once
#include "AudioEngine.h"
#include "ExportSettings.h"

// Responsibilities: 将独占离线会话的音频写入临时文件，编码完成后替换目标文件。
// Preconditions: 会话已成功启动，编码参数已经校验。
// Concurrency: 进度及取消回调在调用线程执行，不得重入会话或抛出异常。
ExportOutcome renderOfflineExport(
    AudioEngine::OfflineExportSession &session, const juce::File &outputFile,
    const ExportSettings &settings, std::function<void(float)> progressCallback,
    std::function<bool()> shouldCancel);
