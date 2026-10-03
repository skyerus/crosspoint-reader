#include <gtest/gtest.h>

#include "src/clippings/CoverSyncProtocol.h"

TEST(CoverSyncProtocol, PercentEncodesUtf8MetadataForHeaders) {
  EXPECT_EQ(coverSyncProtocol::percentEncode("A & B — "), "A%20%26%20B%20%E2%80%94%20");
}

TEST(CoverSyncProtocol, OnlyAcceptsExactStoredLowercaseSha256Acknowledgment) {
  const char* valid = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  EXPECT_TRUE(coverSyncProtocol::validStoredAck("stored", valid));
  EXPECT_FALSE(coverSyncProtocol::validStoredAck("accepted", valid));
  EXPECT_FALSE(coverSyncProtocol::validStoredAck("stored", "0123"));
  EXPECT_FALSE(
      coverSyncProtocol::validStoredAck("stored", "0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef"));
}

TEST(CoverSyncProtocol, RetryBackoffIsBoundedAndClockCorrectionDoesNotStrandCovers) {
  unsigned delay = 0;
  for (const unsigned expected : {30U, 60U, 120U, 240U, 300U, 300U}) {
    delay = coverSyncProtocol::retryDelay(delay);
    EXPECT_EQ(delay, expected);
  }
  EXPECT_FALSE(coverSyncProtocol::retryDue(1000, 1030));
  EXPECT_TRUE(coverSyncProtocol::retryDue(1030, 1030));
  EXPECT_TRUE(coverSyncProtocol::retryDue(1000, 10000));
}
