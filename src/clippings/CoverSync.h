#pragma once

#include <atomic>

class Epub;
struct HighlightSyncConfig;

namespace CoverSync {
// Called after an EPUB has loaded its metadata. It stages at most one original
// JPEG/PNG cover per book revision and collector identity on the SD card.
void queue(const Epub& epub);
bool pending(const HighlightSyncConfig& config);
// True also covers a durably scheduled retry; false is a storage/cancel failure.
bool uploadOne(const HighlightSyncConfig& config, const std::atomic<bool>& cancelled);
}  // namespace CoverSync
