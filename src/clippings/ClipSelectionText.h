#pragma once
#include <cctype>
#include <string>

#include "ClipSelectionGesture.h"

namespace clipSelection {
// Bounded, whole-word capture. The endpoint advances only with committed text.
class Text {
 public:
  static constexpr size_t LIMIT = 4096;
  std::string value;
  Position end;
  uint16_t count = 0;
  Text() {
    value.reserve(LIMIT);
    word.reserve(64);
  }
  bool append(const char* raw, Position position, bool paragraph, bool attached, bool wrapped = false) {
    if (!raw) return true;
    word.clear();
    const auto* p = reinterpret_cast<const unsigned char*>(raw);
    if (p[0] == 0xe2 && p[1] == 0x80 && p[2] == 0x83) p += 3;
    while (*p) {
      if (*p <= ' ' || (*p == 0xc2 && p[1] == 0xa0)) {
        if (!word.empty() && word.back() != ' ') word.push_back(' ');
        p += *p == 0xc2 ? 2 : 1;
      } else {
        if (*p >= 0x80 && (*p < 0xc2 || *p > 0xf4)) return false;
        size_t n = *p < 0x80 ? 1 : *p <= 0xdf ? 2 : *p <= 0xef ? 3 : 4;
        for (size_t i = 1; i < n; ++i)
          if (!p[i] || (p[i] & 0xc0) != 0x80) return false;
        if ((n == 3 && ((*p == 0xe0 && p[1] < 0xa0) || (*p == 0xed && p[1] >= 0xa0))) ||
            (n == 4 && ((*p == 0xf0 && p[1] < 0x90) || (*p == 0xf4 && p[1] >= 0x90))))
          return false;
        if (word.size() + n > LIMIT) return false;
        word.append(reinterpret_cast<const char*>(p), n);
        p += n;
      }
      if (word.size() > LIMIT) return false;
    }
    while (!word.empty() && word.back() == ' ') word.pop_back();
    if (word.empty()) return true;
    const bool dehyphenate = wrapped && !paragraph && !value.empty() && value.back() == '-' &&
                             std::isalnum(static_cast<unsigned char>(word[0]));
    const bool separator = !value.empty() && !dehyphenate && (paragraph || !attached);
    if (value.size() - (dehyphenate ? 1 : 0) + (separator ? 1 : 0) + word.size() > LIMIT) return false;
    if (dehyphenate) value.pop_back();
    if (separator) value.push_back(paragraph ? '\n' : ' ');
    value += word;
    end = position;
    ++count;
    return true;
  }

 private:
  // Reuse normalization storage across words to avoid per-word allocation churn.
  std::string word;
};
}  // namespace clipSelection
