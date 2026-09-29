#pragma once
#include <string>

// Independent credentials, never shared with reading-progress sync.
struct HighlightSyncConfig {
  std::string endpoint;
  std::string token;
  std::string deviceId;
  bool load();
};

// Uploads one full saved excerpt, acknowledging only the exact ID returned by
// the collector. Source clippings are never changed/deleted, so every failure
// remains retryable and a lost response is safe to retry.
bool uploadHighlight(const HighlightSyncConfig& config, size_t index, const std::string& title,
                     const std::string& author);
