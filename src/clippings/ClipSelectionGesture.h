#pragma once
#include <cstdint>

namespace clipSelection {
struct Position {
  uint16_t page = 0;
  uint16_t word = 0;
  bool operator<(const Position& other) const { return page < other.page || (page == other.page && word < other.word); }
  bool operator==(const Position& other) const { return page == other.page && word == other.word; }
};

// Coordinates are logical screen coordinates, after MappedInputManager rotation.
class EdgeHold {
 public:
  void reset() {
    direction = 0;
    latched = false;
  }
  bool suppressHit() const { return latched; }
  int update(int x, int y, int width, int height, uint32_t now, bool moved) {
    const int edge = y < 24 ? -1 : y >= height - 24 ? 1 : x < 24 ? -1 : x >= width - 24 ? 1 : 0;
    if (!edge) {
      reset();
      return 0;
    }
    if (latched || !moved) return 0;
    if (edge != direction) {
      direction = edge;
      since = now;
      return 0;
    }
    if (static_cast<uint32_t>(now - since) < 1000) return 0;
    latched = true;
    return edge;
  }

 private:
  int direction = 0;
  uint32_t since = 0;
  bool latched = false;
};
}  // namespace clipSelection
