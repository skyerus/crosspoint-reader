#pragma once

#include <cstddef>
#include <cstdint>

class Stream {
 public:
  virtual ~Stream() = default;
  virtual int available() = 0;
  virtual int read() = 0;
  virtual int peek() = 0;
  virtual size_t readBytes(char* buffer, size_t length) = 0;
  size_t readBytes(uint8_t* buffer, size_t length) { return readBytes(reinterpret_cast<char*>(buffer), length); }
  virtual void flush() = 0;
  virtual size_t write(uint8_t) = 0;
};
