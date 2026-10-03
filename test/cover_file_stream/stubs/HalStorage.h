#pragma once

#include <algorithm>
#include <cstring>
#include <string>

class HalFile {
 public:
  std::string data;
  size_t position = 0;
  unsigned reads = 0;
  unsigned availabilityChecks = 0;
  bool failReads = false;

  int available() {
    ++availabilityChecks;
    return static_cast<int>(data.size() - position);
  }
  int read() {
    ++reads;
    if (failReads) return -1;
    return position < data.size() ? static_cast<unsigned char>(data[position++]) : -1;
  }
  int read(void* buffer, size_t length) {
    ++reads;
    if (failReads) return -1;
    const size_t count = std::min(length, data.size() - position);
    std::memcpy(buffer, data.data() + position, count);
    position += count;
    return static_cast<int>(count);
  }
};
