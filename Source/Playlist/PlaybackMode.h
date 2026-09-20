#pragma once

// Invariant: 枚举数值作为配置持久化编号；调整编号必须同步读取、写入及界面选项。

namespace midi {
enum class PlaybackMode {
  Sequential = 1,
  LoopList = 2,
  LoopSingle = 3,
  Shuffle = 4
};
}
