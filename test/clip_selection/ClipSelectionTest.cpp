#include <Arduino.h>
#include <Memory.h>
#include <gtest/gtest.h>

#include <limits>
#include <map>
#include <set>

#include "activities/reader/ClipSelectionActivity.h"
#include "clippings/ClipSelectionGesture.h"
#include "clippings/ClipSelectionText.h"

using clipSelection::EdgeHold;
using clipSelection::Position;
using clipSelection::Text;

TEST(ClipSelectionEdge, RequiresMovementAndFullDwellAwayFromFinalWord) {
  EdgeHold edge;
  EXPECT_EQ(edge.update(479, 400, 480, 800, 10, false), 0);
  EXPECT_EQ(edge.update(479, 400, 480, 800, 5010, false), 0);
  EXPECT_EQ(edge.update(479, 400, 480, 800, 5020, true), 0);
  EXPECT_EQ(edge.update(479, 400, 480, 800, 6019, true), 0);
  EXPECT_EQ(edge.update(479, 400, 480, 800, 6020, true), 1);
}

TEST(ClipSelectionEdge, StationaryFingerTurnsOnceAndMustReturnToInterior) {
  EdgeHold edge;
  edge.update(479, 400, 480, 800, 100, true);
  ASSERT_EQ(edge.update(479, 400, 480, 800, 1100, true), 1);
  EXPECT_TRUE(edge.suppressHit());
  EXPECT_EQ(edge.update(479, 400, 480, 800, 20000, true), 0);
  EXPECT_EQ(edge.update(1, 400, 480, 800, 30000, true), 0);
  EXPECT_EQ(edge.update(240, 400, 480, 800, 30001, true), 0);
  EXPECT_FALSE(edge.suppressHit());
  EXPECT_EQ(edge.update(1, 400, 480, 800, 30002, true), 0);
  EXPECT_EQ(edge.update(1, 400, 480, 800, 31002, true), -1);
}

TEST(ClipSelectionEdge, LeavingEdgeAndChangingDirectionRestartDwell) {
  EdgeHold edge;
  edge.update(479, 400, 480, 800, 100, true);
  edge.update(240, 400, 480, 800, 999, true);
  EXPECT_EQ(edge.update(479, 400, 480, 800, 1100, true), 0);
  EXPECT_EQ(edge.update(1, 400, 480, 800, 2000, true), 0);
  EXPECT_EQ(edge.update(1, 400, 480, 800, 2999, true), 0);
  EXPECT_EQ(edge.update(1, 400, 480, 800, 3000, true), -1);
}

TEST(ClipSelectionEdge, DwellSurvivesMillisWraparound) {
  EdgeHold edge;
  edge.update(479, 400, 480, 800, std::numeric_limits<uint32_t>::max() - 499, true);
  EXPECT_EQ(edge.update(479, 400, 480, 800, 499, true), 0);
  EXPECT_EQ(edge.update(479, 400, 480, 800, 500, true), 1);
}

TEST(ClipSelectionEdge, LogicalPortraitAndLandscapeEdgesUseTheirOwnDimensions) {
  for (const auto [width, height] : {std::pair{480, 800}, std::pair{800, 480}}) {
    for (const auto [x, y, direction] : {std::tuple{width - 1, height / 2, 1}, std::tuple{width / 2, height - 1, 1},
                                         std::tuple{0, height / 2, -1}, std::tuple{width / 2, 0, -1}}) {
      EdgeHold edge;
      EXPECT_EQ(edge.update(x, y, width, height, 0, true), 0);
      EXPECT_EQ(edge.update(x, y, width, height, 1000, true), direction);
    }
  }
}

TEST(ClipSelectionText, WholeUtf8WordsAndEndpointStayAtomicAtLimit) {
  Text text;
  const std::string prefix(Text::LIMIT - 3, 'a');
  ASSERT_TRUE(text.append(prefix.c_str(), {2, 5}, false, false));
  ASSERT_TRUE(text.append("é", {3, 0}, false, false));
  EXPECT_EQ(text.value.size(), Text::LIMIT);
  EXPECT_EQ(text.end, (Position{3, 0}));
  EXPECT_FALSE(text.append("fin", {3, 1}, false, false));
  EXPECT_EQ(text.end, (Position{3, 0}));
  EXPECT_EQ(text.count, 2);
  EXPECT_TRUE(text.value.ends_with(" é"));
}

TEST(ClipSelectionText, OversizedWordDoesNotLeavePartialTextOrPhantomEndpoint) {
  Text text;
  ASSERT_TRUE(text.append("keep", {4, 8}, false, false));
  const std::string oversized(Text::LIMIT, 'x');
  EXPECT_FALSE(text.append(oversized.c_str(), {5, 0}, true, false));
  EXPECT_EQ(text.value, "keep");
  EXPECT_EQ(text.end, (Position{4, 8}));
  EXPECT_EQ(text.count, 1);
}

TEST(ClipSelectionText, CleansSpacingJoinsHyphenationAndKeepsParagraphs) {
  Text text;
  ASSERT_TRUE(text.append("\xE2\x80\x83  multi-", {1, 8}, false, false));
  ASSERT_TRUE(text.append("page", {2, 0}, false, false, true));
  ASSERT_TRUE(text.append("\xC2\xA0next\tword\n", {2, 1}, true, false));
  EXPECT_EQ(text.value, "multipage\nnext word");
  EXPECT_EQ(text.end, (Position{2, 1}));
  EXPECT_EQ(text.count, 3);
}

TEST(ClipSelectionText, LiteralHyphensSurviveExceptRealLineWrap) {
  Text sameLine;
  ASSERT_TRUE(sameLine.append("cost-", {1, 0}, false, false));
  ASSERT_TRUE(sameLine.append("effective", {1, 1}, false, true));
  EXPECT_EQ(sameLine.value, "cost-effective");
  Text paragraph;
  ASSERT_TRUE(paragraph.append("unfinished-", {1, 0}, false, false));
  ASSERT_TRUE(paragraph.append("New", {1, 1}, true, false, true));
  EXPECT_EQ(paragraph.value, "unfinished-\nNew");
}

TEST(ClipSelectionText, InvalidUtf8DoesNotAdvanceCapture) {
  for (const char* invalid :
       {"\x80\x80\x80", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xe2\x82"}) {
    Text text;
    ASSERT_TRUE(text.append("valid", {1, 0}, false, false));
    EXPECT_FALSE(text.append(invalid, {1, 1}, false, false));
    EXPECT_EQ(text.value, "valid");
    EXPECT_EQ(text.count, 1);
    EXPECT_EQ(text.end, (Position{1, 0}));
  }
}

class ClipSelectionActivityTest : public testing::Test {
 protected:
  GfxRenderer renderer;
  MappedInputManager input;
  std::map<uint16_t, size_t> pageSizes;
  std::set<uint16_t> failPages;
  std::map<uint16_t, int> pendingAttempts;
  std::map<std::pair<uint16_t, size_t>, std::string> customWords;
  std::vector<uint16_t> loadRequests;
  std::unique_ptr<ClipSelectionActivity> activity;

  std::unique_ptr<Page> load(uint16_t pageNumber) {
    loadRequests.push_back(pageNumber);
    if (!pageSizes.contains(pageNumber) || failPages.contains(pageNumber)) return nullptr;
    auto page = makeUniqueNoThrow<Page>();
    page->number = pageNumber;
    page->elements.reserve((pageSizes[pageNumber] + 9) / 10);
    for (size_t i = 0; i < pageSizes[pageNumber]; i += 10) {
      auto line = makeUniqueNoThrow<PageLine>();
      line->yPos = static_cast<int>(i / 10) * 20;
      line->block = std::make_shared<TestBlock>();
      line->block->words.reserve(10);
      for (size_t j = i; j < std::min(i + 10, pageSizes[pageNumber]); ++j) {
        const auto key = std::pair{pageNumber, j};
        line->block->words.push_back(
            customWords.contains(key) ? customWords[key] : "p" + std::to_string(pageNumber) + "w" + std::to_string(j));
      }
      page->elements.push_back(std::move(line));
    }
    return page;
  }

  void start(uint16_t pageNumber) {
    clipTestNow = 0;
    activity = makeUniqueNoThrow<ClipSelectionActivity>(
        renderer, input, load(pageNumber), pageNumber,
        [this](uint16_t n) -> ClipSelectionActivity::PageLoad {
          if (pendingAttempts[n] > 0) {
            --pendingAttempts[n];
            return {nullptr, true};
          }
          return {load(n), false};
        },
        30, 30);
    activity->onEnter();
    ASSERT_FALSE(Activity::finished);
  }

  void touch(size_t word, uint32_t now, bool down = false) {
    input.down = down;
    input.held = true;
    input.released = false;
    input.x = 32 + static_cast<int>(word % 10) * 40;
    input.y = 40 + static_cast<int>(word / 10) * 20;
    clipTestNow = now;
    activity->loop();
    input.down = false;
  }

  void edge(int direction, uint32_t now) {
    input.x = direction > 0 ? renderer.width - 1 : 0;
    input.y = renderer.height / 2;
    clipTestNow = now;
    activity->loop();
  }

  void release() {
    input.held = false;
    input.released = true;
    activity->loop();
  }

  const ClippingResult& result() {
    EXPECT_TRUE(Activity::finished);
    EXPECT_FALSE(Activity::lastResult.isCancelled);
    return std::get<ClippingResult>(Activity::lastResult.data);
  }

  int visiblePage() {
    activity->render(RenderLock{});
    return renderer.renderedPage;
  }

  static std::string words(uint16_t page, size_t from, size_t to) {
    std::string value;
    for (size_t word = from; word <= to; ++word) {
      if (!value.empty()) value += ' ';
      value += "p" + std::to_string(page) + "w" + std::to_string(word);
    }
    return value;
  }
};

TEST_F(ClipSelectionActivityTest, DensePageKeepsAnchorAndExactIndicesAcrossNextPage) {
  pageSizes = {{5, 300}, {6, 12}, {7, 12}};
  start(5);
  touch(275, 0, true);
  edge(1, 100);
  edge(1, 1099);
  EXPECT_EQ(visiblePage(), 5);
  edge(1, 1100);
  EXPECT_EQ(visiblePage(), 6);
  edge(1, 5000);
  EXPECT_EQ(visiblePage(), 6);
  touch(2, 5010);
  release();
  const auto& saved = result();
  EXPECT_EQ(saved.text, words(5, 275, 299) + " " + words(6, 0, 2));
  EXPECT_EQ(saved.startPage, 5);
  EXPECT_EQ(saved.endPage, 6);
  EXPECT_EQ(saved.startWordIndex, 275);
  EXPECT_EQ(saved.endWordIndex, 2);
  EXPECT_EQ(saved.wordCount, 28);
}

TEST_F(ClipSelectionActivityTest, ReleaseImmediatelyAfterPageTurnKeepsOnlyFirstNextPageWord) {
  pageSizes = {{5, 8}, {6, 8}};
  start(5);
  touch(6, 0, true);
  edge(1, 100);
  edge(1, 1100);
  release();
  const auto& saved = result();
  EXPECT_EQ(saved.text, "p5w6 p5w7 p6w0");
  EXPECT_EQ(saved.startWordIndex, 6);
  EXPECT_EQ(saved.endWordIndex, 0);
  EXPECT_EQ(saved.wordCount, 3);
}

TEST_F(ClipSelectionActivityTest, BackwardAcrossInitialPageUsesAbsolutePageAndWordIndices) {
  pageSizes = {{9, 8}, {10, 8}};
  start(10);
  touch(2, 0, true);
  edge(-1, 100);
  edge(-1, 1100);
  release();
  const auto& saved = result();
  EXPECT_EQ(saved.text, "p9w7 p10w0 p10w1 p10w2");
  EXPECT_EQ(saved.startPage, 9);
  EXPECT_EQ(saved.endPage, 10);
  EXPECT_EQ(saved.startWordIndex, 7);
  EXPECT_EQ(saved.endWordIndex, 2);
  EXPECT_EQ(saved.wordCount, 4);
}

TEST_F(ClipSelectionActivityTest, RetractingAcrossAnchorRemovesVisitedPageFromSavedRange) {
  pageSizes = {{10, 8}, {11, 8}};
  start(10);
  touch(3, 0, true);
  edge(1, 100);
  edge(1, 1100);
  touch(2, 1200);
  edge(-1, 1300);
  edge(-1, 2300);
  EXPECT_EQ(visiblePage(), 10);
  touch(1, 2400);
  release();
  const auto& saved = result();
  EXPECT_EQ(saved.text, "p10w1 p10w2 p10w3");
  EXPECT_EQ(saved.startPage, 10);
  EXPECT_EQ(saved.endPage, 10);
  EXPECT_EQ(saved.startWordIndex, 1);
  EXPECT_EQ(saved.endWordIndex, 3);
  EXPECT_EQ(saved.wordCount, 3);
}

TEST_F(ClipSelectionActivityTest, FailedPageLoadPreservesCurrentPageAndExistingSelection) {
  pageSizes = {{5, 8}, {6, 8}};
  failPages.insert(6);
  start(5);
  touch(2, 0, true);
  touch(4, 50);
  edge(1, 100);
  edge(1, 1100);
  EXPECT_EQ(visiblePage(), 5);
  release();
  const auto& saved = result();
  EXPECT_EQ(saved.text, "p5w2 p5w3 p5w4");
  EXPECT_EQ(saved.startPage, 5);
  EXPECT_EQ(saved.endPage, 5);
}

TEST_F(ClipSelectionActivityTest, ReleaseWaitsForPendingNextPageBeforeSavingBoundary) {
  pageSizes = {{5, 8}, {6, 8}};
  pendingAttempts[6] = 3;
  start(5);
  touch(6, 0, true);
  edge(1, 100);
  edge(1, 1100);
  EXPECT_EQ(visiblePage(), 5);
  release();
  ASSERT_FALSE(Activity::finished);
  input.released = false;
  activity->loop();
  ASSERT_FALSE(Activity::finished);
  activity->loop();
  const auto& saved = result();
  EXPECT_EQ(saved.text, "p5w6 p5w7 p6w0");
  EXPECT_EQ(saved.endPage, 6);
  EXPECT_EQ(saved.endWordIndex, 0);
  EXPECT_EQ(saved.focusPage, 6);
}

TEST_F(ClipSelectionActivityTest, PendingLoadFailureDoesNotInventNextPageText) {
  pageSizes = {{5, 8}};
  pendingAttempts[6] = 1;
  start(5);
  touch(2, 0, true);
  touch(4, 50);
  edge(1, 100);
  edge(1, 1100);
  release();
  const auto& saved = result();
  EXPECT_EQ(saved.text, "p5w2 p5w3 p5w4");
  EXPECT_EQ(saved.endPage, 5);
  EXPECT_EQ(saved.endWordIndex, 4);
  EXPECT_EQ(saved.focusPage, 5);
}

TEST_F(ClipSelectionActivityTest, BlankIntermediatePageIsSkippedWithoutLosingAnchor) {
  pageSizes = {{5, 8}, {6, 0}, {7, 8}};
  start(5);
  touch(6, 0, true);
  edge(1, 100);
  edge(1, 1100);
  EXPECT_EQ(visiblePage(), 5);
  release();
  const auto& saved = result();
  EXPECT_EQ(saved.text, "p5w6 p5w7 p7w0");
  EXPECT_EQ(saved.startPage, 5);
  EXPECT_EQ(saved.endPage, 7);
  EXPECT_EQ(saved.wordCount, 3);
}

TEST_F(ClipSelectionActivityTest, ChapterEndAfterBlankPageDoesNotInventText) {
  pageSizes = {{5, 8}, {6, 0}};
  start(5);
  touch(6, 0, true);
  edge(1, 100);
  edge(1, 1100);
  release();
  const auto& saved = result();
  EXPECT_EQ(saved.text, "p5w6");
  EXPECT_EQ(saved.startPage, 5);
  EXPECT_EQ(saved.endPage, 5);
  EXPECT_EQ(saved.wordCount, 1);
}

TEST_F(ClipSelectionActivityTest, ActualCaptureLimitDoesNotReportUnsavedTrailingWords) {
  pageSizes = {{5, 3}};
  customWords[{5, 0}] = std::string(4090, 'a');
  customWords[{5, 1}] = "é";
  customWords[{5, 2}] = "overflow";
  start(5);
  touch(0, 0, true);
  touch(2, 100);
  release();
  const auto& saved = result();
  EXPECT_EQ(saved.text, std::string(4090, 'a') + "é");
  EXPECT_EQ(saved.startWordIndex, 0);
  EXPECT_EQ(saved.endWordIndex, 1);
  EXPECT_EQ(saved.wordCount, 2);
  EXPECT_TRUE(saved.truncated);
}

TEST_F(ClipSelectionActivityTest, NormalizedEmptyTokenCannotMoveFirstTextWordIndex) {
  pageSizes = {{5, 3}};
  customWords[{5, 0}] = "first";
  customWords[{5, 1}] = "\xC2\xA0";
  customWords[{5, 2}] = "last";
  start(5);
  touch(0, 0, true);
  touch(2, 50);
  release();
  const auto& saved = result();
  EXPECT_EQ(saved.text, "first last");
  EXPECT_EQ(saved.startWordIndex, 0);
  EXPECT_EQ(saved.endWordIndex, 2);
  EXPECT_EQ(saved.wordCount, 2);
}

TEST_F(ClipSelectionActivityTest, RearmedDragCanContinueAcrossFivePagesWithOriginalAnchor) {
  pageSizes = {{5, 8}, {6, 8}, {7, 8}, {8, 8}, {9, 8}};
  start(5);
  touch(6, 0, true);
  edge(1, 100);
  edge(1, 1100);
  edge(1, 10000);
  EXPECT_EQ(visiblePage(), 6);
  for (int page = 7; page <= 9; ++page) {
    const uint32_t now = static_cast<uint32_t>(page) * 2000;
    touch(2, now);
    edge(1, now + 100);
    edge(1, now + 1100);
    EXPECT_EQ(visiblePage(), page);
  }
  release();
  const auto& saved = result();
  EXPECT_EQ(saved.text, "p5w6 p5w7 " + words(6, 0, 7) + " " + words(7, 0, 7) + " " + words(8, 0, 7) + " p9w0");
  EXPECT_EQ(saved.startPage, 5);
  EXPECT_EQ(saved.endPage, 9);
  EXPECT_EQ(saved.startWordIndex, 6);
  EXPECT_EQ(saved.endWordIndex, 0);
  EXPECT_EQ(saved.wordCount, 27);
}
