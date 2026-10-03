#pragma once
#include <Epub/Page.h>

#include <functional>
#include <memory>

#include "activities/Activity.h"
#include "clippings/ClipSelectionGesture.h"

class ClipSelectionActivity final : public Activity {
 public:
  struct PageLoad {
    std::unique_ptr<Page> page;
    bool pending = false;
  };
  ClipSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Page> page,
                        uint16_t pageNumber, std::function<PageLoad(uint16_t)> loadPage, int marginLeft, int marginTop);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool handleHomeGesture() override;

 private:
  struct WordBox {
    int16_t x = 0, y = 0, width = 0, height = 0;
    uint16_t row = 0;
    uint16_t pageWordIndex = 0;
    const char* text = nullptr;
    EpdFontFamily::Style style = EpdFontFamily::REGULAR;
    bool paragraphStart = false;
  };
  bool extractWords();
  int closestInRow(uint16_t row, int centerX) const;
  int wordAt(int x, int y) const;
  void moveVertical(int direction);
  void selectIndex(int index);
  bool load(uint16_t target, bool allowEmpty = false);
  void moveToPage(int target);
  void confirmSelection();
  void cancel();
  void drawSelection() const;
  std::unique_ptr<Page> page;
  std::function<PageLoad(uint16_t)> loadPage;
  uint16_t pageNumber;
  const int marginLeft, marginTop;
  int fontId = 0, lineHeight = 0;
  std::unique_ptr<WordBox[]> words;
  size_t wordCount = 0;
  int selected = 0;
  clipSelection::Position anchor;
  bool hasAnchor = false;
  uint16_t rowCount = 0;
  bool touchDragSelecting = false, touchDragHasMoved = false;
  int touchDragStartX = 0, touchDragStartY = 0;
  clipSelection::EdgeHold edgeHold;
  int pendingPage = -1;
  bool pendingRelease = false, loadPending = false, loadEmpty = false;
  unsigned long lastHorizontalMoveTime = 0;
};
