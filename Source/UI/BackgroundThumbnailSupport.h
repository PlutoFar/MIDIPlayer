#pragma once

// Contract: 缩略图调度使用本模块的线程数和最大任务数；任务所有权与取消由使用方管理。

inline int getBackgroundThumbnailWorkerThreadCount() { return 2; }

inline int getBackgroundThumbnailMaxJobCount() { return 16; }
