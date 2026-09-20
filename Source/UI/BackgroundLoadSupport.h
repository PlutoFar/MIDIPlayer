#pragma once

// Contract: 启动标记、路径存在标记和材质编号来自同一加载请求；本模块只决定调度策略。
// Units: 线程停止预算为毫秒；返回预算不保证后台线程已经退出。

inline bool shouldUseAsyncBackgroundLoad(bool isFirstLoad, bool hasPath) {
  return hasPath || !isFirstLoad;
}

inline bool shouldPrepareStartupBackgroundSynchronously(bool isFirstLoad,
                                                        bool hasPath,
                                                        int blurMode) {
  return isFirstLoad && hasPath && blurMode >= 2 && blurMode <= 4;
}

inline int getBackgroundWorkerStopTimeoutMs() { return 1000; }
