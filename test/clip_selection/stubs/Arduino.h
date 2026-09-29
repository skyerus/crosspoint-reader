#pragma once
#include <cstdint>
inline uint32_t clipTestNow = 0;
inline uint32_t millis() { return clipTestNow; }
