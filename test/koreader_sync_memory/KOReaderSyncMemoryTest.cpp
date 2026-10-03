#include <gtest/gtest.h>

#include "KOReaderSync/KOReaderSyncMemory.h"

TEST(KOReaderSyncMemory, LocalHttpDoesNotRequireTlsHandshakeSpace) {
  EXPECT_FALSE(koReaderSyncMemory::insufficientHeap("http://192.168.1.2:8085/syncs/progress", 14000, 8000));
}

TEST(KOReaderSyncMemory, HttpsRetainsBothHandshakeLimits) {
  constexpr auto url = "https://sync.example.test/syncs/progress";
  EXPECT_TRUE(koReaderSyncMemory::insufficientHeap(url, 14000, 8000));
  EXPECT_TRUE(koReaderSyncMemory::insufficientHeap(url, 34999, 20000));
  EXPECT_TRUE(koReaderSyncMemory::insufficientHeap(url, 50000, 19999));
  EXPECT_FALSE(koReaderSyncMemory::insufficientHeap(url, 35000, 20000));
}

TEST(KOReaderSyncMemory, SchemeCaseMatchesTheSdkTransportParser) {
  EXPECT_TRUE(koReaderSyncMemory::insufficientHeap("HtTpS://sync.example.test", 14000, 8000));
  EXPECT_FALSE(koReaderSyncMemory::insufficientHeap("HTTP://192.168.1.2:8085", 14000, 8000));
  EXPECT_FALSE(koReaderSyncMemory::needsTls("http://192.168.1.2/https://example.test"));
}
