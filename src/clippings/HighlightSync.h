#pragma once
#include <string>

#include "HighlightOutbox.h"

// Independent credentials, never shared with reading-progress sync.
struct HighlightSyncConfig {
  std::string endpoint;
  std::string token;
  std::string deviceId;
  bool load();
};

// Source-independent transport; only an exact collector acknowledgment succeeds.
bool uploadHighlightMutation(const HighlightSyncConfig& config, const HighlightMutation& mutation);
