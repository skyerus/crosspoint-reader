#pragma once

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

namespace coverSyncProtocol {
inline unsigned retryDelay(const unsigned previous) {
  return previous >= 150 ? 300 : previous == 0 ? 30 : previous * 2;
}
inline bool retryDue(const time_t now, const time_t next) {
  // A corrected clock must not strand a durable offline queue.
  return next <= now || next - now > 300;
}

inline std::string percentEncode(const std::string& value) {
  std::string out;
  out.reserve(value.size() * 3);
  for (const unsigned char ch : value) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' ||
        ch == '.' || ch == '~') {
      out += static_cast<char>(ch);
    } else {
      char escaped[4];
      snprintf(escaped, sizeof(escaped), "%%%02X", ch);
      out += escaped;
    }
  }
  return out;
}

inline bool validStoredAck(const char* status, const char* sha) {
  if (!status || strcmp(status, "stored") != 0 || !sha || strlen(sha) != 64) return false;
  for (size_t i = 0; i < 64; ++i)
    if (!((sha[i] >= '0' && sha[i] <= '9') || (sha[i] >= 'a' && sha[i] <= 'f'))) return false;
  return true;
}
}  // namespace coverSyncProtocol
