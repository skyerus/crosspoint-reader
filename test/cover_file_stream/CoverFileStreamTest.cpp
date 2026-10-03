#include <gtest/gtest.h>

#include "src/clippings/CoverFileStream.h"

TEST(CoverFileStream, StreamsCoverThroughTheTransportReadOverload) {
  HalFile file;
  file.data = "cover";
  std::atomic<bool> cancelled{false};
  CoverFileStream body(file, cancelled);
  Stream& transport = body;
  uint8_t buffer[3] = {};

  EXPECT_EQ(transport.available(), 5);
  ASSERT_EQ(transport.readBytes(buffer, sizeof(buffer)), sizeof(buffer));
  EXPECT_EQ(std::string(reinterpret_cast<char*>(buffer), sizeof(buffer)), "cov");
  EXPECT_EQ(transport.available(), 2);
  EXPECT_EQ(transport.read(), 'e');
  EXPECT_EQ(transport.read(), 'r');
  EXPECT_EQ(transport.available(), -1);
  EXPECT_EQ(transport.read(), -1);
}

TEST(CoverFileStream, MidUploadCancellationExitsTheHttpClientContinuationPredicate) {
  HalFile file;
  file.data = "cover";
  std::atomic<bool> cancelled{false};
  CoverFileStream body(file, cancelled);
  Stream& transport = body;

  ASSERT_EQ(transport.read(), 'c');
  cancelled.store(true);
  // HTTPClient sends while available() > -1; zero keeps its wait loop alive.
  EXPECT_FALSE(transport.available() > -1);
  EXPECT_EQ(file.position, 1U);
}

TEST(CoverFileStream, CancellationBetweenAvailabilityAndReadLeavesTheFileUntouched) {
  HalFile file;
  file.data = "cover";
  std::atomic<bool> cancelled{false};
  CoverFileStream body(file, cancelled);
  Stream& transport = body;
  uint8_t buffer[3] = {};

  ASSERT_GT(transport.available(), 0);
  const unsigned checks = file.availabilityChecks;
  cancelled.store(true);
  EXPECT_EQ(transport.readBytes(buffer, sizeof(buffer)), 0U);
  EXPECT_EQ(transport.read(), -1);
  EXPECT_FALSE(transport.available() > -1);
  EXPECT_EQ(file.reads, 0U);
  EXPECT_EQ(file.availabilityChecks, checks);
  EXPECT_EQ(file.position, 0U);
}

TEST(CoverFileStream, UnexpectedEndOfFileExitsTheHttpClientContinuationPredicate) {
  HalFile file;
  file.data = "c";
  std::atomic<bool> cancelled{false};
  CoverFileStream body(file, cancelled);
  Stream& transport = body;

  ASSERT_EQ(transport.read(), 'c');
  EXPECT_FALSE(transport.available() > -1);
}

TEST(CoverFileStream, FailedSdReadStopsSendingEvenWhenTheFileReportsRemainingBytes) {
  HalFile file;
  file.data = "cover";
  file.failReads = true;
  std::atomic<bool> cancelled{false};
  CoverFileStream body(file, cancelled);
  Stream& transport = body;
  uint8_t buffer[3] = {};

  ASSERT_GT(transport.available(), 0);
  EXPECT_EQ(transport.readBytes(buffer, sizeof(buffer)), 0U);
  EXPECT_FALSE(transport.available() > -1);
}
