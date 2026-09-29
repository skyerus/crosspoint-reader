#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
// Deterministic host digest stand-in: tests exercise transaction equality and
// recovery, not the ESP32 MD5 implementation.
class MD5Builder {
  uint64_t value = 1469598103934665603ULL;

 public:
  void begin() { value = 1469598103934665603ULL; }
  void add(const char* s) { add(reinterpret_cast<const uint8_t*>(s), std::strlen(s)); }
  void add(const uint8_t* p, size_t n) {
    while (n--) {
      value ^= *p++;
      value *= 1099511628211ULL;
    }
  }
  void calculate() {}
  std::string toString() const {
    char out[33];
    std::snprintf(out, sizeof(out), "%016llx%016llx", (unsigned long long)value, (unsigned long long)value);
    return out;
  }
};
