#pragma once
#include <algorithm>
#include <ctime>

namespace highlightRetry {
constexpr unsigned delaySeconds(unsigned failures) {
  return failures == 0 ? 5 : std::min(60u << (std::min(failures, 5u) - 1), 900u);
}
inline time_t repairClock(time_t now, time_t deadline) { return deadline > now + 900 ? now + 60 : deadline; }
constexpr bool claimRadio(bool radioOff, bool stationOnly, bool connected) {
  return radioOff || (stationOnly && !connected);
}
}  // namespace highlightRetry
