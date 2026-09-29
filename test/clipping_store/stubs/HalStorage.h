#pragma once
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
constexpr int O_WRONLY = 1, O_CREAT = 2, O_TRUNC = 4;
class HalFile {
 public:
  std::shared_ptr<std::string> data;
  size_t pos = 0;
  bool writable = false;
  std::string path;
  bool directory = false;
  std::vector<HalFile> entries;
  size_t entryIndex = 0;
  static inline std::string failClosePath;
  bool isDirectory() const { return directory; }
  HalFile openNextFile() { return entryIndex < entries.size() ? entries[entryIndex++] : HalFile{}; }
  size_t getName(char* out, size_t size) const {
    const auto name = path.substr(path.find_last_of('/') + 1);
    return std::snprintf(out, size, "%s", name.c_str());
  }
  static inline bool failClose = false;
  explicit operator bool() const { return bool(data) || directory; }
  size_t size() const { return data ? data->size() : 0; }
  size_t position() const { return pos; }
  int available() const { return int(size() - std::min(pos, size())); }
  bool seek(size_t offset) {
    pos = offset;
    return bool(data);
  }  // SD permits past EOF.
  bool seekCur(size_t delta) { return seek(pos + delta); }
  int read(void* out, size_t len) {
    size_t n = std::min(len, size() - std::min(pos, size()));
    if (n) std::memcpy(out, data->data() + pos, n);
    pos += n;
    return int(n);
  }
  size_t write(const void* in, size_t len) {
    if (!data || !writable) return 0;
    if (pos + len > size()) data->resize(pos + len);
    std::memcpy(data->data() + pos, in, len);
    pos += len;
    return len;
  }
  bool close() { return !(writable && (failClose || path == failClosePath)); }
};
class HalStorage {
 public:
  std::map<std::string, std::shared_ptr<std::string>> files;
  std::set<std::string> directories;
  static inline std::string failRenameFrom;
  static HalStorage& getInstance() {
    static HalStorage store;
    return store;
  }
  bool exists(const char* path) const { return files.count(path) || directories.count(path); }
  bool mkdir(const char* path) {
    directories.insert(path);
    return true;
  }
  bool remove(const char* path) { return files.erase(path); }
  bool rename(const char* from, const char* to) {
    if (failRenameFrom == from) return false;
    auto it = files.find(from);
    if (it == files.end() || files.count(to)) return false;
    files[to] = it->second;
    files.erase(it);
    return true;
  }
  HalFile open(const char* path, int flags = 0) {
    HalFile f;
    f.path = path;
    if (directories.count(path)) {
      f.directory = true;
      const std::string prefix = std::string(path) + "/";
      for (const auto& [name, contents] : files) {
        if (name.rfind(prefix, 0) != 0 || name.find('/', prefix.size()) != std::string::npos) continue;
        HalFile entry;
        entry.path = name;
        entry.data = contents;
        f.entries.push_back(std::move(entry));
      }
      return f;
    }
    if (flags & O_CREAT) files[path] = std::make_shared<std::string>();
    auto it = files.find(path);
    if (it != files.end()) f.data = it->second;
    f.writable = flags & O_WRONLY;
    return f;
  }
  bool openFileForRead(const char*, const std::string& path, HalFile& f) {
    f = open(path.c_str(), 0);
    return bool(f);
  }
  std::vector<std::string> listFiles(const char* dir) {
    std::vector<std::string> names;
    const std::string prefix = std::string(dir) + "/";
    for (auto& [path, data] : files)
      if (path.rfind(prefix, 0) == 0) names.push_back(path.substr(prefix.size()));
    return names;
  }
};
#define Storage HalStorage::getInstance()
