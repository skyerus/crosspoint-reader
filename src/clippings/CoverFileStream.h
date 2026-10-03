#pragma once

#include <HalStorage.h>
#include <Stream.h>

#include <atomic>

// HTTPClient takes Stream, while HalFile is Print. Reads use HalStorage's
// mutex without buffering cover bytes in heap.
class CoverFileStream final : public Stream {
 public:
  CoverFileStream(HalFile& file, const std::atomic<bool>& cancelled) : file(file), cancelled(cancelled) {}
  int available() override {
    if (cancelled.load() || failed) return -1;
    const int remaining = file.available();
    // HTTPClient's upload loop treats zero as waiting and a negative value as EOF.
    return remaining > 0 ? remaining : -1;
  }
  int read() override {
    if (cancelled.load() || failed) return -1;
    const int value = file.read();
    if (value < 0) failed = true;
    return value;
  }
  int peek() override { return -1; }
  size_t readBytes(char* buffer, size_t length) override {
    if (cancelled.load() || failed || length == 0) return 0;
    const int read = file.read(buffer, length);
    if (read <= 0) failed = true;
    return read > 0 ? static_cast<size_t>(read) : 0;
  }
  void flush() override {}
  size_t write(uint8_t) override { return 0; }

 private:
  HalFile& file;
  const std::atomic<bool>& cancelled;
  bool failed = false;
};
