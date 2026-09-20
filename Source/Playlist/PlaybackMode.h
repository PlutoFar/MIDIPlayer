#pragma once

namespace midi {
enum class PlaybackMode {
  Sequential = 1,
  LoopList = 2,
  LoopSingle = 3,
  Shuffle = 4
};
}
