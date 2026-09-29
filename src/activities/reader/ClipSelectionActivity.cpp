#include "ClipSelectionActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdlib>

#include "ClippingStore.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/ActivityResult.h"
#include "clippings/ClipSelectionText.h"
#include "components/UITheme.h"

namespace {

constexpr size_t FONT_PREWARM_TEXT_MAX = 2048;
constexpr unsigned long WORD_REPEAT_START_MS = 500;
constexpr unsigned long WORD_REPEAT_INTERVAL_MS = 500;
constexpr int TOUCH_DRAG_MOVEMENT_PX = 4;

bool hasVisibleText(const char* text) {
  if (!text) return false;
  for (const auto* p = reinterpret_cast<const uint8_t*>(text); *p != 0; ++p) {
    if (*p > ' ') return true;
  }
  return false;
}

bool hasEmSpacePrefix(const char* text) {
  return text && static_cast<uint8_t>(text[0]) == 0xE2 && static_cast<uint8_t>(text[1]) == 0x80 &&
         static_cast<uint8_t>(text[2]) == 0x83;
}

}  // namespace

ClipSelectionActivity::ClipSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             std::unique_ptr<Page> page, uint16_t pageNumber,
                                             std::function<PageLoad(uint16_t)> loadPage, int marginLeft, int marginTop)
    : Activity("ClipSelection", renderer, mappedInput),
      page(std::move(page)),
      loadPage(std::move(loadPage)),
      pageNumber(pageNumber),
      marginLeft(marginLeft),
      marginTop(marginTop) {}

void ClipSelectionActivity::onEnter() {
  Activity::onEnter();
  fontId = SETTINGS.getReaderFontId();
  lineHeight = renderer.getLineHeight(fontId);
  if (!extractWords() || wordCount == 0) {
    if (wordCount == 0) LOG_ERR("CLIP", "No selectable words on current page");
    cancel();
    return;
  }
  const int middle = closestInRow(rowCount / 2, renderer.getScreenWidth() / 2);
  if (middle >= 0) selected = middle;
  requestUpdate();
}

bool ClipSelectionActivity::extractWords() {
  wordCount = 0;
  // One exact-sized page index, never a global word cap that truncates dense pages.
  size_t capacity = 0;
  for (const auto& element : page->elements) {
    if (element->getTag() != TAG_PageLine) continue;
    const auto& block = static_cast<const PageLine&>(*element).getBlock();
    if (block && block->valid()) capacity += block->wordCount();
  }
  if (capacity > UINT16_MAX) return false;
  words = makeUniqueNoThrow<WordBox[]>(capacity);
  if (!words && capacity) return false;
  rowCount = 0;
  uint16_t pageWordIndex = 0;
  const bool needsFontPrewarm = renderer.isSdCardFont(fontId);
  auto pageText = needsFontPrewarm ? makeUniqueNoThrow<char[]>(FONT_PREWARM_TEXT_MAX) : nullptr;
  size_t pageTextLength = 0;
  if (needsFontPrewarm && !pageText) LOG_DBG("CLIP", "Skipping SD font prewarm: OOM");
  uint8_t styleMask = 0;

  {
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageLine) continue;
      const auto& line = static_cast<const PageLine&>(*element);
      const auto& block = line.getBlock();
      if (!block || !block->valid()) continue;

      const size_t lineStart = wordCount;
      const bool isRtl = block->getBlockStyle().isRtl;
      const int rubyShift = block->getRubyShift(renderer.getFontAscenderSize(fontId));
      for (uint16_t i = 0; i < block->wordCount(); ++i) {
        const char* text = block->wordText(i);
        if (!hasVisibleText(text)) continue;

        const auto style = static_cast<EpdFontFamily::Style>(block->wordStyle(i) & ~EpdFontFamily::UNDERLINE);
        int width = renderer.getTextAdvanceX(fontId, text, style);
        if (width <= 0) continue;
        if (i + 1 < block->wordCount() && block->wordXpos(i + 1) > block->wordXpos(i)) {
          width = std::min(width, static_cast<int>(block->wordXpos(i + 1) - block->wordXpos(i)));
        }

        WordBox& word = words[wordCount++];
        word.x = static_cast<int16_t>(marginLeft + line.xPos + block->wordXpos(i));
        word.y = static_cast<int16_t>(marginTop + line.yPos + rubyShift);
        word.width = static_cast<int16_t>(width);
        word.height = static_cast<int16_t>(lineHeight);
        word.row = rowCount;
        word.pageWordIndex = pageWordIndex++;
        word.text = text;
        word.style = style;
        word.paragraphStart = hasEmSpacePrefix(text);
        if (pageText) {
          for (const char* p = text; *p != '\0' && pageTextLength + 1 < FONT_PREWARM_TEXT_MAX; ++p) {
            pageText[pageTextLength++] = *p;
          }
          if (pageTextLength + 1 < FONT_PREWARM_TEXT_MAX) pageText[pageTextLength++] = ' ';
        }
        styleMask |= static_cast<uint8_t>(1U << (static_cast<uint8_t>(style) & 0x03));
      }
      if (isRtl) std::reverse(words.get() + lineStart, words.get() + wordCount);
      if (wordCount != lineStart) ++rowCount;
    }
  }

  if (styleMask == 0) styleMask = 0x01;
  if (pageText) {
    pageText[pageTextLength] = '\0';
    renderer.ensureSdCardFontReady(fontId, pageText.get(), styleMask);
  }

  const int indentThreshold = lineHeight / 2;
  int previousRowFirst = -1;
  for (size_t i = 0; i < wordCount; ++i) {
    if (i > 0 && words[i].row == words[i - 1].row) continue;
    if (previousRowFirst >= 0 && words[i].x > words[previousRowFirst].x + indentThreshold) {
      words[i].paragraphStart = true;
    }
    previousRowFirst = static_cast<int>(i);
  }
  return true;
}

int ClipSelectionActivity::closestInRow(const uint16_t row, const int centerX) const {
  int best = -1;
  int bestDistance = INT_MAX;
  for (int i = 0; i < static_cast<int>(wordCount); ++i) {
    if (words[i].row != row) continue;
    const int distance = std::abs(words[i].x + words[i].width / 2 - centerX);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = i;
    }
  }
  return best;
}

int ClipSelectionActivity::wordAt(const int x, const int y) const {
  constexpr int SLOP = 4;
  for (int i = 0; i < static_cast<int>(wordCount); ++i) {
    const WordBox& word = words[i];
    if (x >= word.x - SLOP && x < word.x + word.width + SLOP && y >= word.y - SLOP && y < word.y + word.height + SLOP) {
      return i;
    }
  }
  return -1;
}

void ClipSelectionActivity::moveVertical(const int direction) {
  const int targetRow = static_cast<int>(words[selected].row) + direction;
  if (targetRow < 0 || targetRow >= rowCount) return;
  const int next = closestInRow(static_cast<uint16_t>(targetRow), words[selected].x + words[selected].width / 2);
  if (next >= 0 && next != selected) {
    selectIndex(next);
  }
}

void ClipSelectionActivity::selectIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(wordCount) || index == selected) return;
  selected = index;
  requestUpdate();
}

bool ClipSelectionActivity::load(uint16_t target, bool allowEmpty) {
  loadPending = false;
  loadEmpty = false;
  if (target == pageNumber) return true;
  auto next = loadPage(target);
  loadPending = next.pending;
  if (!next.page) return false;
  auto oldPage = std::move(page);
  auto oldWords = std::move(words);
  const auto oldCount = wordCount;
  const auto oldRows = rowCount;
  page = std::move(next.page);
  const bool extracted = extractWords();
  loadEmpty = extracted && wordCount == 0;
  if (!extracted || (loadEmpty && !allowEmpty)) {
    page = std::move(oldPage);
    words = std::move(oldWords);
    wordCount = oldCount;
    rowCount = oldRows;
    return false;
  }
  pageNumber = target;
  selected = 0;
  return true;
}

void ClipSelectionActivity::moveToPage(int target) {
  if (target < 0 || target > UINT16_MAX || target == pageNumber) {
    pendingPage = -1;
    return;
  }
  RenderLock lock(*this);
  const int direction = target > pageNumber ? 1 : -1;
  if (!load(static_cast<uint16_t>(target))) {
    pendingPage = loadPending ? target : loadEmpty ? target + direction : -1;
    return;
  }
  pendingPage = -1;
  selected = direction > 0 ? 0 : static_cast<int>(wordCount) - 1;
  requestUpdate();
}

void ClipSelectionActivity::confirmSelection() {
  if (!wordCount) return;
  if (!hasAnchor) {
    anchor = {pageNumber, static_cast<uint16_t>(selected)};
    hasAnchor = true;
    requestUpdate();
    return;
  }
  const clipSelection::Position cursor{pageNumber, static_cast<uint16_t>(selected)};
  const auto first = std::min(anchor, cursor);
  const auto last = std::max(anchor, cursor);
  clipSelection::Text text;
  ClippingResult result;
  result.focusPage = cursor.page;
  bool complete = true;
  {
    RenderLock lock(*this);
    for (uint32_t number = first.page; number <= last.page && complete; ++number) {
      if (!load(static_cast<uint16_t>(number), true)) {
        cancel();
        return;
      }
      const size_t begin = number == first.page ? first.word : 0;
      const size_t end = number == last.page ? static_cast<size_t>(last.word) + 1 : wordCount;
      if (begin > end || end > wordCount) {
        cancel();
        return;
      }
      for (size_t i = begin; i < end; ++i) {
        const auto& word = words[i];
        const bool attached =
            i > 0 && words[i - 1].row == word.row && std::abs(word.x - (words[i - 1].x + words[i - 1].width)) <= 2;
        const uint16_t beforeCount = text.count;
        if (!text.append(word.text, {static_cast<uint16_t>(number), word.pageWordIndex}, word.paragraphStart, attached,
                         i == 0 || words[i - 1].row != word.row)) {
          complete = false;
          break;
        }
        if (beforeCount == 0 && text.count == 1) {
          result.startPage = number;
          result.startWordIndex = word.pageWordIndex;
        }
      }
    }
  }
  if (text.value.empty()) {
    cancel();
    return;
  }
  result.truncated = !complete;
  result.text = std::move(text.value);
  result.endPage = text.end.page;
  result.endWordIndex = text.end.word;
  result.wordCount = text.count;
  if (result.startPage == result.endPage && result.startWordIndex > result.endWordIndex)
    std::swap(result.startWordIndex, result.endWordIndex);
  setResult(std::move(result));
  finish();
}

void ClipSelectionActivity::cancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

bool ClipSelectionActivity::handleHomeGesture() {
  cancel();
  return true;
}

void ClipSelectionActivity::loop() {
  if (pendingPage >= 0) {
    if (mappedInput.wasScreenTouchReleased()) pendingRelease = true;
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      cancel();
      return;
    }
    moveToPage(pendingPage);
    if (pendingPage >= 0) return;
    if (pendingRelease) {
      pendingRelease = false;
      touchDragSelecting = false;
      confirmSelection();
      return;
    }
  }
  if (wordCount == 0) return;

  int touchX = 0;
  int touchY = 0;
  if (touchDragSelecting) {
    if (mappedInput.isScreenTouchHeld(touchX, touchY)) {
      const int deltaX = touchX - touchDragStartX;
      const int deltaY = touchY - touchDragStartY;
      touchDragHasMoved = touchDragHasMoved || deltaX >= TOUCH_DRAG_MOVEMENT_PX || deltaX <= -TOUCH_DRAG_MOVEMENT_PX ||
                          deltaY >= TOUCH_DRAG_MOVEMENT_PX || deltaY <= -TOUCH_DRAG_MOVEMENT_PX;

      const int turn = edgeHold.update(touchX, touchY, renderer.getScreenWidth(), renderer.getScreenHeight(), millis(),
                                       touchDragHasMoved);
      if (turn)
        moveToPage(static_cast<int>(pageNumber) + turn);
      else if (!edgeHold.suppressHit()) {
        const int hit = wordAt(touchX, touchY);
        if (hit >= 0) selectIndex(hit);
      }
      return;
    }
    if (mappedInput.wasScreenTouchReleased()) {
      touchDragSelecting = false;
      touchDragHasMoved = false;
      edgeHold.reset();
      confirmSelection();
      return;
    }
  } else if (mappedInput.wasScreenRawTouchDown(touchX, touchY)) {
    const int hit = wordAt(touchX, touchY);
    if (hit >= 0) {
      selectIndex(hit);
      if (!hasAnchor) {
        anchor = {pageNumber, static_cast<uint16_t>(selected)};
        hasAnchor = true;
      }
      touchDragSelecting = true;
      touchDragHasMoved = false;
      touchDragStartX = touchX;
      touchDragStartY = touchY;
      edgeHold.reset();
      requestUpdate();
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (hasAnchor) {
      hasAnchor = false;
      requestUpdate();
    } else {
      cancel();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    confirmSelection();
    return;
  }

  if (mappedInput.wasScreenTapped(touchX, touchY)) {
    const int hit = wordAt(touchX, touchY);
    if (hit >= 0) {
      selectIndex(hit);
      confirmSelection();
    }
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Left || swipe == MappedInputManager::SwipeDir::Up) {
    moveToPage(static_cast<int>(pageNumber) + 1);
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Right || swipe == MappedInputManager::SwipeDir::Down) {
    moveToPage(static_cast<int>(pageNumber) - 1);
    return;
  }

  const unsigned long now = millis();
  const bool repeat =
      mappedInput.getHeldTime() >= WORD_REPEAT_START_MS && now - lastHorizontalMoveTime >= WORD_REPEAT_INTERVAL_MS;
  const bool moveLeft = mappedInput.wasPressed(MappedInputManager::Button::ScreenLeft) ||
                        (repeat && mappedInput.isPressed(MappedInputManager::Button::ScreenLeft));
  const bool moveRight = mappedInput.wasPressed(MappedInputManager::Button::ScreenRight) ||
                         (repeat && mappedInput.isPressed(MappedInputManager::Button::ScreenRight));
  if (moveLeft && selected > 0) {
    selectIndex(selected - 1);
    lastHorizontalMoveTime = now;
  } else if (moveRight && selected + 1 < static_cast<int>(wordCount)) {
    selectIndex(selected + 1);
    lastHorizontalMoveTime = now;
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenUp)) {
    moveVertical(-1);
  } else if (mappedInput.wasPressed(MappedInputManager::Button::ScreenDown)) {
    moveVertical(1);
  }
}

void ClipSelectionActivity::drawSelection() const {
  const clipSelection::Position cursor{pageNumber, static_cast<uint16_t>(selected)};
  const auto firstPosition = hasAnchor ? std::min(anchor, cursor) : cursor;
  const auto lastPosition = hasAnchor ? std::max(anchor, cursor) : cursor;
  const int first = firstPosition.page == pageNumber ? firstPosition.word : 0;
  const int last = lastPosition.page == pageNumber ? lastPosition.word : static_cast<int>(wordCount) - 1;
  const WordBox* previous = nullptr;
  for (int i = first; i <= last; ++i) {
    const WordBox& word = words[i];
    if (previous && previous->row == word.row) {
      const int previousRight = previous->x + previous->width;
      const int wordRight = word.x + word.width;
      if (previousRight < word.x) {
        renderer.fillRectDither(previousRight, word.y, word.x - previousRight, word.height, Color::LightGray);
      } else if (wordRight < previous->x) {
        renderer.fillRectDither(wordRight, word.y, previous->x - wordRight, word.height, Color::LightGray);
      }
    }
    renderer.fillRectDither(word.x, word.y, word.width, word.height, Color::LightGray);
    renderer.drawText(fontId, word.x, word.y, word.text, true, word.style);
    previous = &word;
  }
  const WordBox& cursorWord = words[selected];
  renderer.drawRect(cursorWord.x, cursorWord.y, cursorWord.width, cursorWord.height, true);
}

void ClipSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();
  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  page->render(renderer, fontId, marginLeft, marginTop);
  scope.endScanAndPrewarm();
  page->render(renderer, fontId, marginLeft, marginTop);
  if (wordCount != 0) drawSelection();

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), !hasAnchor ? tr(STR_SELECT) : tr(STR_DONE), tr(STR_DIR_LEFT),
                                            tr(STR_DIR_RIGHT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
