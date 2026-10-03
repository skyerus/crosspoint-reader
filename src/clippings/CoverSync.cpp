#include "CoverSync.h"

#include <ArduinoJson.h>
#include <Epub.h>
#include <HTTPClient.h>
#include <HalStorage.h>
#include <Logging.h>
#include <MD5Builder.h>
#include <WiFi.h>

#include <cstring>
#include <ctime>
#include <mutex>

#include "CoverSyncProtocol.h"
#include "HighlightSync.h"

namespace {
constexpr const char* DIRECTORY = "/.crosspoint/cover-sync";
constexpr size_t MAX_COVER_BYTES = 5U * 1024U * 1024U;
// Escaped title/author plus paths fit in a bounded metadata record; artwork is streamed.
constexpr size_t MAX_RECORD_BYTES = 8192;
constexpr size_t MAX_METADATA_BYTES = 1024;
std::mutex queueMutex;

// HTTPClient's streaming request overload takes Stream, while HalFile is a
// Print to keep its writable use narrow. This adapter reads directly through
// HalStorage's mutex without buffering cover bytes in heap.
class CoverFileStream final : public Stream {
 public:
  CoverFileStream(HalFile& file, const std::atomic<bool>& cancelled) : file(file), cancelled(cancelled) {}
  int available() override { return cancelled.load() ? 0 : file.available(); }
  int read() override { return cancelled.load() ? -1 : file.read(); }
  int peek() override { return -1; }
  size_t readBytes(char* buffer, size_t length) override {
    if (cancelled.load()) return 0;
    const int read = file.read(buffer, length);
    return read > 0 ? static_cast<size_t>(read) : 0;
  }
  void flush() override {}
  size_t write(uint8_t) override { return 0; }

 private:
  HalFile& file;
  const std::atomic<bool>& cancelled;
};

std::string digest(const std::string& value) {
  MD5Builder hash;
  hash.begin();
  hash.add(value.c_str());
  hash.calculate();
  return hash.toString().c_str();
}

bool readRecord(const char* path, JsonDocument& doc) {
  std::string raw;
  return Storage.readFileToString("Cover", path, MAX_RECORD_BYTES, raw) &&
         deserializeJson(doc, raw) == DeserializationError::Ok;
}

bool writeRecord(const char* path, JsonDocument& doc) {
  std::string raw;
  serializeJson(doc, raw);
  if (raw.size() > MAX_RECORD_BYTES) {
    LOG_ERR("Cover", "Cover metadata exceeds limit");
    return false;
  }
  const std::string tmp = std::string(path) + ".tmp";
  HalFile file;
  if (!Storage.openFileForWrite("Cover", tmp, file)) return false;
  const bool written = file.write(raw.data(), raw.size()) == raw.size();
  file.flush();
  const bool closed = file.close();
  if (!written || !closed) {
    Storage.remove(tmp.c_str());
    return false;
  }
  return Storage.replaceFile(tmp.c_str(), path);
}

bool nextRecord(const HighlightSyncConfig& config, std::string& recordPath, JsonDocument& doc) {
  const time_t now = time(nullptr);
  time_t oldest = 0;
  recordPath.clear();
  const std::string target = digest(config.targetEndpoint + "\n" + config.token);
  HalFile directory = Storage.open(DIRECTORY);
  if (!directory || !directory.isDirectory()) return false;
  while (HalFile file = directory.openNextFile()) {
    char name[48] = {};
    file.getName(name, sizeof(name));
    if (strncmp(name, "cover-", 6) || !strstr(name, ".json")) continue;
    const std::string candidate = std::string(DIRECTORY) + "/" + name;
    JsonDocument candidateDoc;
    if (!readRecord(candidate.c_str(), candidateDoc)) continue;
    if (candidateDoc["state"] == "pending" && candidateDoc["target"] == target) {
      const time_t due = candidateDoc["next_attempt"] | static_cast<time_t>(0);
      if (!coverSyncProtocol::retryDue(now, due)) continue;
      if (!recordPath.empty() && due >= oldest) continue;
      oldest = due;
      recordPath = candidate;
      doc.set(candidateDoc);
    }
  }
  return !recordPath.empty();
}
}  // namespace

namespace CoverSync {
void queue(const Epub& epub) {
  const std::lock_guard<std::mutex> lock(queueMutex);
  HighlightSyncConfig config;
  if (!config.load()) return;
  HalFile source;
  if (!Storage.openFileForRead("Cover", epub.getPath(), source)) return;
  const std::string target = digest(config.targetEndpoint + "\n" + config.token);
  const std::string key = digest(epub.getPath() + "\n" + std::to_string(source.fileSize64()) + "\n" +
                                 std::to_string(source.modificationTime()) + "\n" + epub.getTitle() + "\n" +
                                 epub.getAuthor() + "\n" + target);
  const std::string recordPath = std::string(DIRECTORY) + "/cover-" + key + ".json";
  JsonDocument existing;
  if (readRecord(recordPath.c_str(), existing)) {
    const char* state = existing["state"] | "";
    const char* staged = existing["file"] | "";
    if (strcmp(state, "stored") == 0 || strcmp(state, "unavailable") == 0 ||
        (strcmp(state, "pending") == 0 && Storage.exists(staged)))
      return;
    Storage.remove(recordPath.c_str());
  }
  if (!Storage.exists(DIRECTORY) && !Storage.mkdir(DIRECTORY)) return;

  // Staged artwork must survive book moves and reader-cache eviction.
  const std::string stageBase = std::string(DIRECTORY) + "/artwork-" + key;
  std::string contentType;
  size_t size = 0;
  std::string stagePath = stageBase;
  if (!epub.extractOriginalCoverToFile(contentType, size, stageBase + ".part", MAX_COVER_BYTES)) {
    // An empty type means metadata named no usable cover. I/O failures retain
    // the opportunity to stage the same source on a later reader open.
    if (!contentType.empty() && size <= MAX_COVER_BYTES) return;
    JsonDocument record;
    record["state"] = "unavailable";
    record["target"] = target;
    record["book"] = key;
    writeRecord(recordPath.c_str(), record);
    return;
  }
  if (size == 0 || size > MAX_COVER_BYTES || epub.getTitle().size() > MAX_METADATA_BYTES ||
      epub.getAuthor().size() > MAX_METADATA_BYTES) {
    Storage.remove((stageBase + ".part").c_str());
    JsonDocument record;
    record["state"] = "unavailable";
    record["target"] = target;
    record["book"] = key;
    writeRecord(recordPath.c_str(), record);
    return;
  }
  const char* extension = contentType == "image/png" ? ".png" : ".jpg";
  stagePath += extension;
  if (!Storage.replaceFile((stageBase + ".part").c_str(), stagePath.c_str())) return;

  JsonDocument record;
  record["state"] = "pending";
  record["target"] = target;
  record["book"] = key;
  record["file"] = stagePath;
  record["type"] = contentType;
  record["title"] = epub.getTitle();
  record["author"] = epub.getAuthor();
  if (!writeRecord(recordPath.c_str(), record)) Storage.remove(stagePath.c_str());
}

bool pending(const HighlightSyncConfig& config) {
  const std::lock_guard<std::mutex> lock(queueMutex);
  std::string path;
  JsonDocument doc;
  return nextRecord(config, path, doc);
}

bool uploadOne(const HighlightSyncConfig& config, const std::atomic<bool>& cancelled) {
  std::string recordPath;
  JsonDocument record;
  {
    const std::lock_guard<std::mutex> lock(queueMutex);
    if (!nextRecord(config, recordPath, record)) return true;
  }
  const auto failed = [&]() {
    if (!cancelled.load()) {
      record["backoff"] = coverSyncProtocol::retryDelay(record["backoff"] | 0U);
      record["next_attempt"] = time(nullptr) + record["backoff"].as<unsigned>();
      const std::lock_guard<std::mutex> lock(queueMutex);
      // A durable cover retry is handled independently: it must not increase
      // the radio backoff for subsequent highlight uploads.
      return writeRecord(recordPath.c_str(), record);
    }
    return false;
  };
  const char* filePath = record["file"] | "";
  const char* contentType = record["type"] | "";
  const char* title = record["title"] | "";
  const char* author = record["author"] | "";
  HalFile file;
  if (!Storage.openFileForRead("Cover", filePath, file) || file.fileSize64() == 0 ||
      file.fileSize64() > MAX_COVER_BYTES) {
    record["state"] = "recover";
    const std::lock_guard<std::mutex> lock(queueMutex);
    return writeRecord(recordPath.c_str(), record);
  }
  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(1500);
  http.setTimeout(12000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  const std::string url = config.coversEndpoint();
  if (url.empty() || !http.begin(client, url.c_str())) return failed();
  http.addHeader("Content-Type", contentType);
  http.addHeader("Authorization", (std::string("Bearer ") + config.token).c_str());
  http.addHeader("X-Book-Title", coverSyncProtocol::percentEncode(title).c_str());
  http.addHeader("X-Book-Author", coverSyncProtocol::percentEncode(author).c_str());
  CoverFileStream body(file, cancelled);
  const int status = http.sendRequest("POST", &body, file.fileSize64());
  if (cancelled.load()) {
    http.end();
    return false;
  }
  if (status == 400 || status == 413 || status == 415) {
    // Bad/unsupported artwork cannot improve by waking the radio repeatedly.
    // A changed book revision gets a fresh job; quotes remain independent.
    http.end();
    const std::lock_guard<std::mutex> lock(queueMutex);
    record["state"] = "unavailable";
    if (!writeRecord(recordPath.c_str(), record)) return false;
    file.close();
    Storage.remove(filePath);
    return true;
  }
  if (status != 200 || http.getSize() < 0 || http.getSize() > 256) {
    http.end();
    return failed();
  }
  const String response = http.getString();
  http.end();
  JsonDocument responseDoc;
  if (deserializeJson(responseDoc, response) != DeserializationError::Ok ||
      !coverSyncProtocol::validStoredAck(responseDoc["status"].as<const char*>(),
                                         responseDoc["sha256"].as<const char*>()))
    return failed();
  const std::lock_guard<std::mutex> lock(queueMutex);
  record["state"] = "stored";
  record["sha256"] = responseDoc["sha256"].as<const char*>();
  if (!writeRecord(recordPath.c_str(), record)) return false;
  file.close();  // Release SD handle before deleting the acknowledged staging file.
  Storage.remove(filePath);
  return true;
}
}  // namespace CoverSync
