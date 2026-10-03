#pragma once

#include <cstddef>
#include <string_view>

namespace koReaderSyncMemory {
// Leave room for TLS records when the server does not negotiate smaller ones.
inline constexpr size_t MIN_FREE_FOR_TLS = 35000;
inline constexpr size_t MIN_BLOCK_FOR_TLS = 20000;

inline bool needsTls(const std::string_view url) {
  constexpr std::string_view scheme = "https://";
  if (url.size() < scheme.size()) return false;
  for (size_t i = 0; i < scheme.size(); ++i) {
    const char c = url[i] >= 'A' && url[i] <= 'Z' ? url[i] + ('a' - 'A') : url[i];
    if (c != scheme[i]) return false;
  }
  return true;
}

inline bool insufficientHeap(const std::string_view url, const size_t freeBytes, const size_t largestBlockBytes) {
  return needsTls(url) && (freeBytes < MIN_FREE_FOR_TLS || largestBlockBytes < MIN_BLOCK_FOR_TLS);
}
}  // namespace koReaderSyncMemory
