#pragma once
#include "GfxRenderer.h"
struct TestTheme {
  void drawButtonHints(GfxRenderer&, const char*, const char*, const char*, const char*) {}
};
inline TestTheme clipTestTheme;
#define GUI clipTestTheme
