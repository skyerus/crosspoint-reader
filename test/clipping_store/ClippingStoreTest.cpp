#include <HalStorage.h>
#include <gtest/gtest.h>

#include "ClippingStore.h"

class ClippingDurability : public testing::Test {
 protected:
  ClippingStore store;
  void SetUp() override {
    Storage.files.clear();
    Storage.directories.clear();
    HalFile::failClosePath.clear();
    HalStorage::failRenameFrom.clear();
    HalFile::failClose = false;
    ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  }
  ClippingStore::AddResult add(const std::string& text) {
    return store.addClipping(0, 0, 0, 1, 0, 1, 2, "Chapter", 0, text, 123);
  }
  std::string path() {
    for (auto& [p, d] : Storage.files)
      if (p.ends_with(".bin")) return p;
    return "";
  }
};
TEST_F(ClippingDurability, FailedClosePreservesPreviouslySavedExcerpt) {
  ASSERT_EQ(add("first"), ClippingStore::AddResult::Added);
  const auto p = path(), before = *Storage.files[p];
  HalFile::failClose = true;
  EXPECT_EQ(add("second"), ClippingStore::AddResult::SaveFailed);
  EXPECT_EQ(*Storage.files[p], before);
  EXPECT_EQ(store.clippingCount(), 1);
  HalFile::failClose = false;
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  std::string text;
  ASSERT_TRUE(store.readClippingText(0, text));
  EXPECT_EQ(text, "first");
}
TEST_F(ClippingDurability, TruncatedFinalTextFailsClosedAndCannotOverwriteArchive) {
  ASSERT_EQ(add("first"), ClippingStore::AddResult::Added);
  ASSERT_EQ(add("second"), ClippingStore::AddResult::Added);
  const auto p = path();
  Storage.files[p]->pop_back();
  const auto before = *Storage.files[p];
  EXPECT_FALSE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  EXPECT_EQ(store.clippingCount(), 0);
  EXPECT_EQ(add("third"), ClippingStore::AddResult::SaveFailed);
  store.clearAll();
  store.unload();
  EXPECT_EQ(*Storage.files[p], before);
}
TEST_F(ClippingDurability, InterruptedReplacementRecoversBackupBeforeAppending) {
  ASSERT_EQ(add("first"), ClippingStore::AddResult::Added);
  const auto p = path();
  ASSERT_TRUE(Storage.rename(p.c_str(), (p + ".bak").c_str()));
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  ASSERT_EQ(add("second"), ClippingStore::AddResult::Added);
  ASSERT_EQ(store.clippingCount(), 2);
  std::string text;
  ASSERT_TRUE(store.readClippingText(0, text));
  EXPECT_EQ(text, "first");
}
