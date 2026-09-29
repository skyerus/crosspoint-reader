#include "HighlightSync.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HalStorage.h>
#include <MD5Builder.h>
#include <WiFi.h>

#include "ClippingStore.h"

namespace {
// Plain HTTP is explicitly limited to the trusted home LAN. No redirects are
// followed, preventing the bearer token from leaking to a redirect target.
bool localEndpoint(const std::string& url) {
  constexpr const char* suffix = "/v1/highlights";
  if (url.rfind("http://", 0) != 0 || url.size() > 256 || url.find_first_of("\r\n@?#") != std::string::npos)
    return false;
  const auto slash = url.find('/', 7);
  if (slash == std::string::npos || url.substr(slash) != suffix) return false;
  const auto authority = url.substr(7, slash - 7);
  const auto colon = authority.find(':');
  const auto host = authority.substr(0, colon);
  if (host.size() > 6 && host.substr(host.size() - 6) == ".local") return true;
  IPAddress ip;
  if (!ip.fromString(host.c_str())) return false;
  return ip[0] == 10 || (ip[0] == 192 && ip[1] == 168) || (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31);
}
}  // namespace

bool HighlightSyncConfig::load() {
  HalFile file;
  if (!Storage.openFileForRead("HighlightSync", "/.crosspoint/highlight-sync.json", file) || file.size() > 2048)
    return false;
  // Small bounded config; dynamic JSON storage avoids putting 2 KiB on the
  // reader's small task stack. It is freed before any network operation.
  const String raw = Storage.readFile("/.crosspoint/highlight-sync.json");
  JsonDocument doc;
  if (deserializeJson(doc, raw)) return false;
  endpoint = doc["endpoint"] | "";
  token = doc["token"] | "";
  deviceId = doc["device_id"] | "";
  return localEndpoint(endpoint) && token.size() >= 32 && token.size() <= 128 &&
         token.find_first_of("\r\n") == std::string::npos && !deviceId.empty() && deviceId.size() <= 128;
}

bool uploadHighlight(const HighlightSyncConfig& config, const size_t index, const std::string& title,
                     const std::string& author) {
  const Clipping* clipping = CLIPPINGS.clippingAt(index);
  if (!clipping || WiFi.status() != WL_CONNECTED) return false;
  std::string text;
  if (!CLIPPINGS.readClippingText(index, text) || text.empty()) return false;
  // ID deliberately excludes page/layout, file path, time and list index:
  // retrying, relayout, deletion of an earlier entry and renaming cannot change
  // the identity of the same excerpt. The archive deduplicates across devices.
  MD5Builder digest;
  digest.begin();
  digest.add(title.c_str());
  digest.add("\n");
  digest.add(author.c_str());
  digest.add("\n");
  digest.add(text.c_str());
  digest.calculate();
  const std::string id = std::string("cp-") + digest.toString().c_str();
  // One <=4 KiB excerpt per request keeps heap use independent of library size.
  JsonDocument doc;
  doc["source"] = "crosspoint";
  doc["device_id"] = config.deviceId;
  auto highlight = doc["highlights"].to<JsonArray>().add<JsonObject>();
  highlight["id"] = id;
  highlight["book_title"] = title;
  highlight["author"] = author;
  highlight["text"] = text;
  char location[80];
  snprintf(location, sizeof(location), "section %u, page %u", clipping->spineIndex + 1, clipping->startPage + 1);
  highlight["location"] = location;
  if (doc.overflowed()) return false;
  std::string body;
  serializeJson(doc, body);
  doc.clear();
  text.clear();
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(client, config.endpoint.c_str())) return false;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", (std::string("Bearer ") + config.token).c_str());
  const int code = http.POST(reinterpret_cast<uint8_t*>(body.data()), body.size());
  // Collector replies with Content-Length. Refuse unbounded/chunked responses
  // rather than allocate an arbitrary response body on a constrained reader.
  if (code < 200 || code >= 300 || http.getSize() < 0 || http.getSize() > 4096) {
    http.end();
    return false;
  }
  const String response = http.getString();
  http.end();
  if (deserializeJson(doc, response)) return false;
  for (JsonVariant accepted : doc["accepted"].as<JsonArray>()) {
    if (accepted.is<const char*>() && id == accepted.as<const char*>()) return true;
  }
  return false;
}
