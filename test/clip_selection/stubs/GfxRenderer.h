#pragma once
#include <cstdint>
#include <cstring>

#include "FontCacheManager.h"
struct EpdFontFamily {
  enum Style { REGULAR = 0, BOLD = 1, ITALIC = 2, UNDERLINE = 4 };
};
enum class Color { LightGray };
class GfxRenderer {
 public:
  int width = 480;
  int height = 800;
  int renderedPage = -1;
  int getScreenWidth() const { return width; }
  int getScreenHeight() const { return height; }
  int getLineHeight(int) const { return 20; }
  int getFontAscenderSize(int) const { return 16; }
  int getTextAdvanceX(int, const char* text, EpdFontFamily::Style) const {
    return static_cast<int>(std::strlen(text)) * 5;
  }
  bool isSdCardFont(int) const { return false; }
  void ensureSdCardFontReady(int, const char*, uint8_t) {}
  void fillRectDither(int, int, int, int, Color) {}
  void drawText(int, int, int, const char*, bool, EpdFontFamily::Style) {}
  void drawRect(int, int, int, int, bool) {}
  void clearScreen() {}
  void displayBuffer() {}
  FontCacheManager* getFontCacheManager() { return &cache; }

 private:
  FontCacheManager cache;
};
