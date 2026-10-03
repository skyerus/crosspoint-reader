#pragma once
class MappedInputManager {
 public:
  enum class Button { Back, Confirm, ScreenLeft, ScreenRight, ScreenUp, ScreenDown };
  enum class SwipeDir { None, Left, Right, Up, Down };
  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };
  bool down = false;
  bool held = false;
  bool released = false;
  bool confirm = false;
  int x = 0;
  int y = 0;
  bool wasScreenTouchDown(int&, int&) const { return false; }
  bool wasScreenRawTouchDown(int& outX, int& outY) const {
    outX = x;
    outY = y;
    return down;
  }
  bool isScreenTouchHeld(int& outX, int& outY) const {
    outX = x;
    outY = y;
    return held;
  }
  bool wasScreenTouchReleased() const { return released; }
  bool wasReleased(Button button) const { return confirm && button == Button::Confirm; }
  bool wasPressed(Button) const { return false; }
  bool isPressed(Button) const { return false; }
  bool wasScreenTapped(int&, int&) const { return false; }
  SwipeDir wasSwipe() const { return SwipeDir::None; }
  unsigned long getHeldTime() const { return 0; }
  Labels mapLabels(const char* a, const char* b, const char* c, const char* d) const { return {a, b, c, d}; }
};
