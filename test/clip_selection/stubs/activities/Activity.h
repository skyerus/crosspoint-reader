#pragma once
#include <string>
#include <utility>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
struct RenderLock {
  RenderLock() = default;
  template <typename T>
  explicit RenderLock(T&) {}
};
class Activity {
 protected:
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;

 public:
  inline static ActivityResult lastResult;
  inline static bool finished = false;
  Activity(std::string, GfxRenderer& renderer, MappedInputManager& input) : renderer(renderer), mappedInput(input) {}
  virtual ~Activity() = default;
  virtual void onEnter() {
    finished = false;
    lastResult = ActivityResult{};
  }
  virtual void loop() {}
  virtual void render(RenderLock&&) {}
  virtual bool isReaderActivity() const { return false; }
  virtual bool handleHomeGesture() { return false; }
  void requestUpdate(bool = false) {}
  void setResult(ActivityResult&& result) { lastResult = std::move(result); }
  static void finish() { finished = true; }
};
