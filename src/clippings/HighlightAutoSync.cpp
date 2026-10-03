#include "HighlightAutoSync.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <HalMemory.h>
#include <HalPowerManager.h>
#include <Logging.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <atomic>
#include <ctime>

#include "ClippingStore.h"
#include "CoverSync.h"
#include "HighlightOutbox.h"
#include "HighlightRetryPolicy.h"
#include "HighlightSync.h"
#include "WifiCredentialStore.h"

namespace {
std::atomic<bool> running{false};
std::atomic<bool> cancelled{false};
std::atomic<int> outcome{0};
std::atomic<bool> seeded{false};
// RTC clock continues across deep sleep; failed joins cannot be retried on
// every short wake cycle. No SD write is needed for battery retry scheduling.
RTC_DATA_ATTR time_t retryAfter = 0;
RTC_DATA_ATTR unsigned failures = 0;
unsigned long checkAfter = 0;

bool hasUploadMemory(const HalMemory::HeapStats& heap) {
  return heap.freeBytes >= 80000 && heap.largestBlockBytes >= 24000;
}

bool resolveLocalHost(HighlightSyncConfig& config) {
  const size_t slash = config.endpoint.find('/', 7);
  const std::string authority = config.endpoint.substr(7, slash - 7);
  const auto colon = authority.find(':');
  const std::string host = authority.substr(0, colon);
  IPAddress ip;
  if (ip.fromString(host.c_str())) return true;
  // Config validation allows .local only. Resolve with a fixed deadline rather
  // than NetworkClient's blocking system DNS retry loop.
  if (!MDNS.begin("crosspoint-highlight-sync")) return false;
  ip = MDNS.queryHost(host.substr(0, host.size() - 6).c_str(), 1000);
  MDNS.end();
  if (!(ip[0] == 10 || (ip[0] == 192 && ip[1] == 168) || (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31))) return false;
  config.endpoint = std::string("http://") + ip.toString().c_str() +
                    (colon == std::string::npos ? "" : authority.substr(colon)) + "/v1/highlights";
  return true;
}

void worker(void*) {
  // Task has a fixed 8 KiB stack; only one <=4 KiB excerpt is on the heap at a
  // time. A task prevents TCP deadlines from blocking touch/page navigation.
  bool success = false;
  bool ownedRadio = false;
  bool hadWork = false;
  {
    HalPowerManager::Lock powerLock;
    HighlightSyncConfig config;
    if (config.load() && !cancelled.load()) {
      if (!seeded.load()) {
        for (unsigned i = 0; i < 8 && !cancelled.load(); ++i) {
          const int result = ClippingStore::seedOneArchiveClipping();
          if (result == 0) seeded.store(true);
          if (result != 1) break;
        }
      }
      const bool hasHighlights = HighlightOutbox::pending();
      const bool hasCovers = CoverSync::pending(config);
      hadWork = hasHighlights || hasCovers;
      if (!hasHighlights && !hasCovers)
        success = seeded.load();
      else {
        if (highlightRetry::claimRadio(WiFi.getMode() == WIFI_MODE_NULL, WiFi.getMode() == WIFI_STA,
                                       WiFi.status() == WL_CONNECTED)) {
          WIFI_STORE.loadFromFile();
          auto credential = WIFI_STORE.findCredential(WIFI_STORE.getLastConnectedSsid());
          if (!credential) credential = WIFI_STORE.getCredentialAt(0);
          if (credential && !cancelled.load()) {
            ownedRadio = true;
            WiFi.persistent(false);
            WiFi.mode(WIFI_STA);
            WiFi.begin(credential->ssid.c_str(), credential->password.c_str());
            const unsigned long started = millis();
            while (!cancelled.load() && WiFi.status() != WL_CONNECTED && millis() - started < 8000) {
              vTaskDelay(pdMS_TO_TICKS(50));
            }
          }
        }
        if (!cancelled.load() && WiFi.status() == WL_CONNECTED && resolveLocalHost(config)) {
          success = true;
          const unsigned long started = millis();
          for (unsigned sent = 0; sent < 8 && !cancelled.load() && millis() - started < 12000; ++sent) {
            std::string path;
            HighlightMutation mutation;
            if (!HighlightOutbox::next(path, mutation)) {
              success = !HighlightOutbox::pending();
              break;
            }
            if (!uploadHighlightMutation(config, mutation) || !HighlightOutbox::acknowledge(path)) {
              success = false;
              break;
            }
          }
          // Cover failures never prevent queued highlights from draining, and
          // cover-only work shares this existing retry/radio lifecycle.
          if (!cancelled.load() && CoverSync::pending(config) && !CoverSync::uploadOne(config, cancelled))
            success = false;
          if (!HighlightOutbox::pending() && !CoverSync::pending(config)) success = seeded.load() || !hasHighlights;
        }
      }
    }
  }
  if (ownedRadio) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
  const time_t now = time(nullptr);
  const int result = cancelled.load() ? 3 : !success ? 2 : seeded.load() && !HighlightOutbox::pending() ? 4 : 1;
  if (result == 2) {
    failures = std::min(failures + 1, 5u);
    retryAfter = now + highlightRetry::delaySeconds(failures);
  } else {
    if (result == 1 || result == 4) failures = 0;
    retryAfter = now + (result == 3 ? 15 : result == 4 ? 30 : 5);
  }
  outcome.store(result);
  if (hadWork) {
    const auto heap = HalMemory::getInternalHeap();
    LOG_INF("HSync", "Upload %s; heap %zu free/%zu max",
            cancelled.load() ? "cancelled"
            : success        ? "complete"
                             : "retry",
            heap.freeBytes, heap.largestBlockBytes);
  }
  // All filesystem/radio work is finished before transitions may resume.
  running.store(false);
  vTaskDelete(nullptr);
}
}  // namespace

namespace HighlightAutoSync {
bool active() { return running.load(); }
bool cancelAndReady() {
  cancelled.store(true);
  return !running.load();
}
void stopAndWait() {
  cancelled.store(true);
  // Never kill a task that might own HalStorage's mutex. HTTP deadlines and
  // cancellation checks bound this cooperative drain before raw SD/sleep.
  while (running.load()) delay(10);
}
void tick(bool eligible, void (*reclaimMemory)()) {
  if (running.load()) {
    if (!eligible) cancelled.store(true);
    return;
  }
  const time_t now = time(nullptr);
  retryAfter = highlightRetry::repairClock(now, retryAfter);
  if (outcome.exchange(0)) checkAfter = millis() + 1000;
  if (!eligible) {
    cancelled.store(true);
    return;
  }
  if (running.load() || now < retryAfter || static_cast<int32_t>(millis() - checkAfter) < 0) return;
  checkAfter = millis() + 5000;
  auto heap = HalMemory::getInternalHeap();
  if (!hasUploadMemory(heap)) {
    if (!reclaimMemory) return;
    // Do not evict reading caches for an unpaired reader or an empty queue.
    // Drop the temporary config strings before measuring reclaimed memory.
    {
      HighlightSyncConfig config;
      if (!config.load()) return;
      if (!HighlightOutbox::pending() && !CoverSync::pending(config) &&
          (seeded.load() || !ClippingStore::hasAnyClippings()))
        return;
    }
    const size_t before = heap.freeBytes;
    reclaimMemory();
    heap = HalMemory::getInternalHeap();
    if (heap.freeBytes > before) {
      LOG_INF("HSync", "Released display caches: heap %zu -> %zu free/%zu max", before, heap.freeBytes,
              heap.largestBlockBytes);
    }
    if (!hasUploadMemory(heap)) return;
  }
  cancelled.store(false);
  running.store(true);
  TaskHandle_t handle = nullptr;
  if (xTaskCreate(worker, "highlight-sync", 8192, nullptr, 1, &handle) != pdPASS) {
    running.store(false);
    retryAfter = now + 60;
    LOG_ERR("HSync", "Could not start background upload");
  }
}
}  // namespace HighlightAutoSync
