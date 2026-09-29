#pragma once
#include <mutex>
#include <string>

struct HighlightMutation {
  std::string id;
  std::string title;
  std::string author;
  std::string text;
  bool deleted = false;
};
std::string highlightId(const std::string& title, const std::string& author, const std::string& text);

namespace HighlightOutbox {
// The clipping replacement and durable outbox intent share this guard. Network
// work never holds it; the renderer accesses SD through HalStorage's own mutex.
class Transaction {
 public:
  Transaction();
  bool ready() const { return recovered; }
  bool prepare(const std::string& storePath, const std::string& temporaryPath, const HighlightMutation& mutation);
  bool commit();
  bool digest(const std::string& path, std::string& value);

 private:
  std::unique_lock<std::mutex> lock;
  bool recovered;
};
bool next(std::string& path, HighlightMutation& mutation);
bool acknowledge(const std::string& path);
bool pending();
}  // namespace HighlightOutbox
