#include <HalStorage.h>
#include <gtest/gtest.h>

#include "ClippingStore.h"
#include "clippings/HighlightOutbox.h"
#include "clippings/HighlightRetryPolicy.h"

namespace {
constexpr const char* INTENT = "/.crosspoint/highlight-outbox/transaction.intent";
class ArchiveDurability : public testing::Test {
 protected:
  void SetUp() override {
    Storage.files.clear();
    Storage.directories.clear();
    HalFile::failClose = false;
    HalFile::failClosePath.clear();
    HalStorage::failRenameFrom.clear();
  }
  void put(const std::string& path, const std::string& data) {
    Storage.files[path] = std::make_shared<std::string>(data);
  }
  HighlightMutation mutation(bool deleted = false) {
    return {"stable-id", "Book", "Author", "complete excerpt", deleted};
  }
  void prepare(bool deleted = false) {
    put("/store", "old");
    put("/store.tmp", "new");
    HighlightOutbox::Transaction transaction;
    ASSERT_TRUE(transaction.ready());
    ASSERT_TRUE(transaction.prepare("/store", "/store.tmp", mutation(deleted)));
  }
  void commitSource() {
    ASSERT_TRUE(Storage.rename("/store", "/store.bak"));
    ASSERT_TRUE(Storage.rename("/store.tmp", "/store"));
  }
  ClippingStore::AddResult add(ClippingStore& store, const std::string& text) {
    return store.addClipping(0, 0, 0, 1, 0, 1, 2, "Chapter", 0, text, 123);
  }
  void drain() {
    std::string path;
    HighlightMutation event;
    while (HighlightOutbox::next(path, event)) ASSERT_TRUE(HighlightOutbox::acknowledge(path));
  }
};
TEST_F(ArchiveDurability, IntentBeforeSourceCommitRollsBackWithoutFalseDeletion) {
  prepare(true);
  EXPECT_TRUE(Storage.exists(INTENT));
  EXPECT_FALSE(HighlightOutbox::pending());
  EXPECT_FALSE(Storage.exists(INTENT));
  EXPECT_EQ(*Storage.files["/store"], "old");
}
TEST_F(ArchiveDurability, CrashBetweenSourceRenamesRestoresOldStoreWithoutDeletion) {
  prepare(true);
  ASSERT_TRUE(Storage.rename("/store", "/store.bak"));
  EXPECT_FALSE(HighlightOutbox::pending());
  EXPECT_EQ(*Storage.files["/store"], "old");
  EXPECT_FALSE(Storage.exists(INTENT));
}
TEST_F(ArchiveDurability, SourceCommittedBeforeCrashFinalizesExactlyOneDeletion) {
  prepare(true);
  commitSource();
  ASSERT_TRUE(HighlightOutbox::pending());
  std::string first, retry;
  HighlightMutation event;
  ASSERT_TRUE(HighlightOutbox::next(first, event));
  EXPECT_TRUE(event.deleted);
  EXPECT_EQ(event.text, "complete excerpt");
  ASSERT_TRUE(HighlightOutbox::next(retry, event));
  EXPECT_EQ(first, retry);
  ASSERT_TRUE(HighlightOutbox::acknowledge(first));
  EXPECT_FALSE(HighlightOutbox::pending());
}
TEST_F(ArchiveDurability, AmbiguousStorePreservesIntentAndBlocksFurtherTransactions) {
  prepare(true);
  put("/store", "corrupt");
  EXPECT_TRUE(HighlightOutbox::pending());
  EXPECT_TRUE(Storage.exists(INTENT));
  HighlightOutbox::Transaction transaction;
  EXPECT_FALSE(transaction.ready());
}
TEST_F(ArchiveDurability, NewBookIntentWithoutSourceCommitDoesNotInventHighlight) {
  put("/store.tmp", "new");
  {
    HighlightOutbox::Transaction transaction;
    ASSERT_TRUE(transaction.prepare("/store", "/store.tmp", mutation()));
  }
  EXPECT_FALSE(HighlightOutbox::pending());
  EXPECT_FALSE(Storage.exists(INTENT));
}
TEST_F(ArchiveDurability, FailedIntentClosePreventsLocalDeletion) {
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  ASSERT_EQ(add(store, "keep me"), ClippingStore::AddResult::Added);
  drain();
  HalFile::failClosePath = std::string(INTENT) + ".tmp";
  EXPECT_FALSE(store.removeClippingAt(0));
  EXPECT_EQ(store.clippingCount(), 1u);
  EXPECT_FALSE(HighlightOutbox::pending());
  HalFile::failClosePath.clear();
  std::string text;
  ASSERT_TRUE(store.readClippingText(0, text));
  EXPECT_EQ(text, "keep me");
}
TEST_F(ArchiveDurability, FailedStoreReplacementDoesNotQueueDeletion) {
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  ASSERT_EQ(add(store, "keep me"), ClippingStore::AddResult::Added);
  drain();
  for (const auto& [path, data] : Storage.files) {
    if (path.ends_with(".bin")) HalStorage::failRenameFrom = path + ".tmp";
  }
  EXPECT_FALSE(store.removeClippingAt(0));
  EXPECT_EQ(store.clippingCount(), 1u);
  EXPECT_FALSE(HighlightOutbox::pending());
}
TEST_F(ArchiveDurability, FailedFinalizationRetainsCommittedDeletionUntilRecovery) {
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  ASSERT_EQ(add(store, "delete me"), ClippingStore::AddResult::Added);
  drain();
  HalStorage::failRenameFrom = INTENT;
  ASSERT_TRUE(store.removeClippingAt(0));
  EXPECT_TRUE(Storage.exists(INTENT));
  EXPECT_EQ(store.clippingCount(), 0u);
  EXPECT_EQ(add(store, "blocked"), ClippingStore::AddResult::SaveFailed);
  HalStorage::failRenameFrom.clear();
  std::string path;
  HighlightMutation event;
  ASSERT_TRUE(HighlightOutbox::next(path, event));
  EXPECT_TRUE(event.deleted);
}
TEST_F(ArchiveDurability, DuplicatePassageDeletionWaitsUntilLastLocalCopy) {
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  ASSERT_EQ(add(store, "same"), ClippingStore::AddResult::Added);
  ASSERT_EQ(add(store, "same"), ClippingStore::AddResult::Added);
  drain();
  ASSERT_TRUE(store.removeClippingAt(0));
  EXPECT_FALSE(HighlightOutbox::pending());
  ASSERT_TRUE(store.removeClippingAt(0));
  std::string path;
  HighlightMutation event;
  ASSERT_TRUE(HighlightOutbox::next(path, event));
  EXPECT_TRUE(event.deleted);
}
TEST_F(ArchiveDurability, FullExcerptIsRetainedForUpsertAndDelete) {
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/book.epub", "Book", "Author", "epub"));
  const std::string text = std::string(4000, 'x') + "é中😀";
  ASSERT_EQ(add(store, text), ClippingStore::AddResult::Added);
  std::string path;
  HighlightMutation event;
  ASSERT_TRUE(HighlightOutbox::next(path, event));
  EXPECT_EQ(event.text, text);
  const auto id = event.id;
  ASSERT_TRUE(HighlightOutbox::acknowledge(path));
  ASSERT_TRUE(store.removeClippingAt(0));
  ASSERT_TRUE(HighlightOutbox::next(path, event));
  EXPECT_EQ(event.text, text);
  EXPECT_EQ(event.id, id);
  EXPECT_TRUE(event.deleted);
}
TEST_F(ArchiveDurability, BacklogOverTwoHundredEventsDrainsInOrderWithoutHidingTail) {
  put("/store", "unchanged");
  for (int i = 0; i < 205; ++i) {
    HighlightOutbox::Transaction transaction;
    auto event = mutation();
    event.id = std::to_string(i);
    ASSERT_TRUE(transaction.prepare("/store", "/store", event));
    ASSERT_TRUE(transaction.commit());
  }
  for (int i = 0; i < 205; ++i) {
    std::string path, retry;
    HighlightMutation event;
    ASSERT_TRUE(HighlightOutbox::next(path, event));
    EXPECT_EQ(event.id, std::to_string(i));
    ASSERT_TRUE(HighlightOutbox::next(retry, event));
    EXPECT_EQ(path, retry);
    ASSERT_TRUE(HighlightOutbox::acknowledge(path));
  }
  EXPECT_FALSE(HighlightOutbox::pending());
}
TEST_F(ArchiveDurability, AcknowledgementDoesNotDeleteNewerQueuedMutation) {
  put("/store", "unchanged");
  for (int i = 0; i < 2; ++i) {
    HighlightOutbox::Transaction transaction;
    ASSERT_TRUE(transaction.prepare("/store", "/store", mutation(i == 1)));
    ASSERT_TRUE(transaction.commit());
  }
  std::string oldPath, nextPath;
  HighlightMutation event;
  ASSERT_TRUE(HighlightOutbox::next(oldPath, event));
  ASSERT_TRUE(HighlightOutbox::acknowledge(oldPath));
  ASSERT_TRUE(HighlightOutbox::next(nextPath, event));
  EXPECT_NE(oldPath, nextPath);
  EXPECT_TRUE(event.deleted);
}
TEST_F(ArchiveDurability, LegacyCrossBookSeedingCheckpointsEachExcerptAndResumes) {
  ClippingStore store;
  ASSERT_TRUE(store.loadForBook("/first.epub", "First", "Author", "epub"));
  ASSERT_EQ(add(store, "one"), ClippingStore::AddResult::Added);
  ASSERT_EQ(add(store, "two"), ClippingStore::AddResult::Added);
  ASSERT_TRUE(store.loadForBook("/second.epub", "Second", "Author", "epub"));
  ASSERT_EQ(add(store, "three"), ClippingStore::AddResult::Added);
  drain();
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(ClippingStore::seedOneArchiveClipping(), 1);
    std::string path;
    HighlightMutation event;
    ASSERT_TRUE(HighlightOutbox::next(path, event));
    EXPECT_FALSE(event.deleted);
    ASSERT_TRUE(HighlightOutbox::acknowledge(path));
  }
  EXPECT_EQ(ClippingStore::seedOneArchiveClipping(), 0);
  EXPECT_EQ(ClippingStore::seedOneArchiveClipping(), 0);
  EXPECT_FALSE(HighlightOutbox::pending());
}
}  // namespace

TEST(HighlightScheduling, BackoffIsBoundedAndClockCorrectionsRecover) {
  EXPECT_EQ(highlightRetry::delaySeconds(0), 5u);
  EXPECT_EQ(highlightRetry::delaySeconds(1), 60u);
  EXPECT_EQ(highlightRetry::delaySeconds(2), 120u);
  EXPECT_EQ(highlightRetry::delaySeconds(5), 900u);
  EXPECT_EQ(highlightRetry::delaySeconds(100), 900u);
  EXPECT_EQ(highlightRetry::repairClock(1000, 5000), 1060);
  EXPECT_EQ(highlightRetry::repairClock(1000, 1300), 1300);
  EXPECT_LT(highlightRetry::repairClock(5000, 1300), 5000);
}
TEST(HighlightScheduling, DisconnectedStationCanReconnectWithoutClaimingActiveOrApRadio) {
  EXPECT_TRUE(highlightRetry::claimRadio(true, false, false));
  EXPECT_TRUE(highlightRetry::claimRadio(false, true, false));
  EXPECT_FALSE(highlightRetry::claimRadio(false, true, true));
  EXPECT_FALSE(highlightRetry::claimRadio(false, false, false));
  EXPECT_FALSE(highlightRetry::claimRadio(false, false, true));
}

TEST_F(ArchiveDurability, MaximumBoundedMetadataRoundTripsAndOversizeIsRejected) {
  const std::string target = "/" + std::string(4095, 'p');
  put(target, "unchanged");
  auto event = mutation();
  event.title.assign(4096, 't');
  event.author.assign(4096, 'a');
  event.text.assign(4096, 'x');
  {
    HighlightOutbox::Transaction transaction;
    ASSERT_TRUE(transaction.prepare(target, target, event));
    ASSERT_TRUE(transaction.commit());
  }
  std::string path;
  HighlightMutation actual;
  ASSERT_TRUE(HighlightOutbox::next(path, actual));
  EXPECT_EQ(actual.title, event.title);
  EXPECT_EQ(actual.author, event.author);
  EXPECT_EQ(actual.text, event.text);
  ASSERT_TRUE(HighlightOutbox::acknowledge(path));
  event.text.push_back('x');
  {
    HighlightOutbox::Transaction transaction;
    EXPECT_FALSE(transaction.prepare(target, target, event));
  }
  EXPECT_FALSE(HighlightOutbox::pending());
}

TEST_F(ArchiveDurability, UnreadableOutboxDirectoryMustNotReportQueueEmpty) {
  put("/.crosspoint/highlight-outbox", "not a readable directory");
  std::string path;
  HighlightMutation event;
  EXPECT_FALSE(HighlightOutbox::next(path, event));
  EXPECT_TRUE(HighlightOutbox::pending());
}
