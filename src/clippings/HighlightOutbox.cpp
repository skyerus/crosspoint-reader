#include "HighlightOutbox.h"

#include <HalStorage.h>
#include <Logging.h>
#include <MD5Builder.h>
#include <Serialization.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr const char* DIRECTORY = "/.crosspoint/highlight-outbox";
constexpr const char* INTENT = "/.crosspoint/highlight-outbox/transaction.intent";
constexpr uint8_t VERSION = 1;
std::mutex queueMutex;
struct Record {
  uint64_t sequence = 0;
  std::string target;
  std::string before;
  std::string after;
  HighlightMutation mutation;
};

bool fileDigest(const std::string& path, std::string& result) {
  HalFile file;
  if (!Storage.openFileForRead("HQueue", path, file)) return false;
  MD5Builder hash;
  hash.begin();
  uint8_t buffer[128];
  size_t remaining = file.size();
  while (remaining) {
    const size_t want = std::min(remaining, sizeof(buffer));
    if (file.read(buffer, want) != static_cast<int>(want)) return false;
    hash.add(buffer, want);
    remaining -= want;
  }
  hash.calculate();
  result = hash.toString().c_str();
  return true;
}

std::string eventPath(uint64_t sequence) {
  char name[80];
  snprintf(name, sizeof(name), "%s/%016llx.ready", DIRECTORY, static_cast<unsigned long long>(sequence));
  return name;
}

// Stream directory entries rather than the UI listFiles() helper, which caps
// listings at 200. A long offline backlog must never hide older mutations.
bool scan(uint64_t& first, uint64_t& last) {
  first = UINT64_MAX;
  last = 0;
  if (!Storage.exists(DIRECTORY)) return true;
  HalFile dir = Storage.open(DIRECTORY);
  if (!dir || !dir.isDirectory()) return false;
  while (auto file = dir.openNextFile()) {
    char name[48];
    file.getName(name, sizeof(name));
    if (strlen(name) != 22 || strcmp(name + 16, ".ready") != 0) continue;
    char* end = nullptr;
    const uint64_t value = strtoull(name, &end, 16);
    if (end != name + 16 || value == 0) continue;
    first = std::min(first, value);
    last = std::max(last, value);
  }
  return true;
}

bool read(const char* path, Record& record) {
  HalFile file;
  if (!Storage.openFileForRead("HQueue", path, file) || file.size() > 32768) return false;
  uint8_t version = 0;
  return serialization::tryReadPod(file, version) && version == VERSION &&
         serialization::tryReadPod(file, record.sequence) && record.sequence > 0 &&
         serialization::tryReadString(file, record.target, 4096) &&
         serialization::tryReadString(file, record.before, 32) &&
         serialization::tryReadString(file, record.after, 32) &&
         serialization::tryReadString(file, record.mutation.id, 64) &&
         serialization::tryReadString(file, record.mutation.title, 4096) &&
         serialization::tryReadString(file, record.mutation.author, 4096) &&
         serialization::tryReadString(file, record.mutation.text, 4096) &&
         serialization::tryReadPod(file, record.mutation.deleted) && file.available() == 0;
}

bool recover() {
  if (!Storage.exists(INTENT)) return true;
  Record record;
  if (!read(INTENT, record)) return false;
  const std::string backup = record.target + ".bak";
  if (!Storage.exists(record.target.c_str()) && Storage.exists(backup.c_str()) &&
      !Storage.rename(backup.c_str(), record.target.c_str()))
    return false;
  std::string actual;
  if (Storage.exists(record.target.c_str())) {
    if (!fileDigest(record.target, actual)) return false;
  }
  if (actual == record.after) {
    return Storage.rename(INTENT, eventPath(record.sequence).c_str());
  }
  // Only a byte-identical old store (or proven originally absent file) is a
  // rollback. Any ambiguous/corrupt store preserves the journal for recovery.
  if (actual == record.before) return Storage.remove(INTENT);
  LOG_ERR("HQueue", "Ambiguous clipping transaction; preserving intent");
  return false;
}
}  // namespace

std::string highlightId(const std::string& title, const std::string& author, const std::string& text) {
  MD5Builder hash;
  hash.begin();
  hash.add(title.c_str());
  hash.add("\n");
  hash.add(author.c_str());
  hash.add("\n");
  hash.add(text.c_str());
  hash.calculate();
  return std::string("cp-") + hash.toString().c_str();
}

namespace HighlightOutbox {
Transaction::Transaction() : lock(queueMutex), recovered(recover()) {}

bool Transaction::prepare(const std::string& storePath, const std::string& temporaryPath,
                          const HighlightMutation& mutation) {
  if (!recovered || mutation.id.empty() || mutation.id.size() > 64 || storePath.size() > 4096 ||
      mutation.title.size() > 4096 || mutation.author.size() > 4096 || mutation.text.size() > 4096 ||
      (!Storage.exists(DIRECTORY) && !Storage.mkdir(DIRECTORY)))
    return false;
  uint64_t first, last;
  if (!scan(first, last) || last == UINT64_MAX) return false;
  std::string before, after;
  if (Storage.exists(storePath.c_str()) && !fileDigest(storePath, before)) return false;
  if (temporaryPath == storePath)
    after = before;
  else if (!fileDigest(temporaryPath, after))
    return false;
  const uint64_t sequence = last + 1;
  const std::string temp = std::string(INTENT) + ".tmp";
  HalFile file = Storage.open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  if (!file) return false;
  const bool written =
      serialization::tryWritePod(file, VERSION) && serialization::tryWritePod(file, sequence) &&
      serialization::tryWriteString(file, storePath) && serialization::tryWriteString(file, before) &&
      serialization::tryWriteString(file, after) && serialization::tryWriteString(file, mutation.id) &&
      serialization::tryWriteString(file, mutation.title) && serialization::tryWriteString(file, mutation.author) &&
      serialization::tryWriteString(file, mutation.text) && serialization::tryWritePod(file, mutation.deleted);
  const bool closed = file.close();
  if (!written || !closed) {
    Storage.remove(temp.c_str());
    return false;
  }
  return Storage.rename(temp.c_str(), INTENT);
}
bool Transaction::commit() { return recover(); }
bool Transaction::digest(const std::string& path, std::string& value) { return fileDigest(path, value); }

bool next(std::string& path, HighlightMutation& mutation) {
  std::lock_guard<std::mutex> lock(queueMutex);
  if (!recover()) return false;
  uint64_t first, last;
  if (!scan(first, last) || first == UINT64_MAX) return false;
  path = eventPath(first);
  Record record;
  if (!read(path.c_str(), record)) return false;
  mutation = std::move(record.mutation);
  return true;
}
bool acknowledge(const std::string& path) {
  std::lock_guard<std::mutex> lock(queueMutex);
  return Storage.remove(path.c_str());
}
bool pending() {
  std::lock_guard<std::mutex> lock(queueMutex);
  if (!recover()) return true;
  uint64_t first, last;
  return !scan(first, last) || first != UINT64_MAX;
}
}  // namespace HighlightOutbox
