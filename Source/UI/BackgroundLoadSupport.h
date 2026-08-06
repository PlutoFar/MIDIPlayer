#pragma once

inline bool shouldUseAsyncBackgroundLoad(bool isFirstLoad, bool hasPath) {
  return hasPath || !isFirstLoad;
}

inline bool shouldPrepareStartupBackgroundSynchronously(bool isFirstLoad,
                                                        bool hasPath,
                                                        int blurMode) {
  return isFirstLoad && hasPath && blurMode >= 2 && blurMode <= 4;
}

inline int getBackgroundWorkerStopTimeoutMs() { return 1000; }
