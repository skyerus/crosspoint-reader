#pragma once
struct FontCacheManager {
  struct Scope {
    void endScanAndPrewarm() {}
  };
  Scope createPrewarmScope() { return {}; }
};
