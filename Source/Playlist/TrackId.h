#pragma once

#include <cstdint>

namespace midi {

// Identity within the current application session, independent of row order.
using TrackId = std::int64_t;

} // namespace midi
