#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "GfxRenderer.h"
constexpr int TAG_PageLine = 1;
struct TestBlock {
  struct Style {
    bool isRtl = false;
  } style;
  std::vector<std::string> words;
  bool valid() const { return true; }
  const Style& getBlockStyle() const { return style; }
  int getRubyShift(int) const { return 0; }
  uint16_t wordCount() const { return static_cast<uint16_t>(words.size()); }
  const char* wordText(uint16_t index) const { return words.at(index).c_str(); }
  uint8_t wordStyle(uint16_t) const { return 0; }
  int wordXpos(uint16_t index) const { return index * 40; }
};
struct TestElement {
  virtual ~TestElement() = default;
  virtual int getTag() const { return TAG_PageLine; }
};
struct PageLine : TestElement {
  int xPos = 0;
  int yPos = 0;
  std::shared_ptr<TestBlock> block;
  const std::shared_ptr<TestBlock>& getBlock() const { return block; }
};
struct Page {
  int number = 0;
  std::vector<std::unique_ptr<TestElement>> elements;
  void render(GfxRenderer& renderer, int, int, int) const { renderer.renderedPage = number; }
};
