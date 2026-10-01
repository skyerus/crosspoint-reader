#pragma once
#include <ctime>
class HalClock {
 public:
  time_t now = 0;
  bool utcTime(time_t& out) const {
    out = now;
    return now != 0;
  }
};
inline HalClock halClock;
