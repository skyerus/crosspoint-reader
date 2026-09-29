#include "ClippingStore.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Serialization.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>

#include "clippings/ClippingPreview.h"
#include "clippings/HighlightOutbox.h"

namespace {
constexpr uint8_t LEGACY_VERSION = 1;
constexpr uint8_t TEXT_OFFSET_VERSION = 2;
constexpr uint8_t VERSION = 3;
constexpr size_t INITIAL_CLIPPING_RESERVE = 4;
constexpr char CLIPPINGS_DIR[] = "/.crosspoint/clippings";
constexpr size_t TEXT_COPY_BUFFER_SIZE = 128;
constexpr size_t HEADER_STRING_MAX = CLIPPING_TEXT_MAX;

struct ClippingFileHeader {
  std::string title;
  std::string author;
  std::string path;
  std::string bookType;
  uint16_t count = 0;
};

std::string storeFilePathForBook(const std::string& filePath, const std::string& bookType) {
  return std::string(CLIPPINGS_DIR) + "/" + bookType + "_" + std::to_string(std::hash<std::string>{}(filePath)) +
         ".bin";
}

void copyBounded(char* dst, const size_t dstSize, const char* src) {
  if (dstSize == 0) return;
  if (!src) src = "";
  snprintf(dst, dstSize, "%s", src);
}

bool readClippingFileHeader(const std::string& fullPath, const char* name, ClippingFileHeader& header) {
  HalFile f;
  if (!Storage.openFileForRead("CLIP", fullPath, f)) {
    return false;
  }

  uint8_t version = 0;
  uint16_t count = 0;
  if (!serialization::tryReadPod(f, version) ||
      (version != LEGACY_VERSION && version != TEXT_OFFSET_VERSION && version != VERSION) ||
      !serialization::tryReadPod(f, count) || !serialization::tryReadString(f, header.title, HEADER_STRING_MAX) ||
      !serialization::tryReadString(f, header.author, HEADER_STRING_MAX) ||
      !serialization::tryReadString(f, header.path, HEADER_STRING_MAX)) {
    f.close();
    return false;
  }
  f.close();

  if (count > CLIPPING_MAX_PER_BOOK) {
    return false;
  }
  header.count = count;
  header.bookType = "epub";
  const std::string nameStr = name ? name : "";
  const size_t underscorePos = nameStr.find('_');
  if (underscorePos != std::string::npos) {
    header.bookType = nameStr.substr(0, underscorePos);
  }
  return true;
}

bool copyBytes(HalFile& in, HalFile& out, uint16_t length) {
  std::array<uint8_t, TEXT_COPY_BUFFER_SIZE> buffer{};
  while (length > 0) {
    const size_t chunk = std::min<size_t>(length, buffer.size());
    if (in.read(buffer.data(), chunk) != static_cast<int>(chunk)) {
      return false;
    }
    if (out.write(buffer.data(), chunk) != chunk) {
      return false;
    }
    length = static_cast<uint16_t>(length - chunk);
  }
  return true;
}
}  // namespace

ClippingStore ClippingStore::instance;

bool ClippingStore::loadForBook(const std::string& filePath, const std::string& title, const std::string& author,
                                const std::string& bookType) {
  writable = false;
  if (bookType != "epub") {
    LOG_ERR("CLIP", "Unknown clipping book type: %s", bookType.c_str());
    return false;
  }

  bookFilePath = filePath;
  bookTitle = title;
  bookAuthor = author;
  dirty = false;
  clippings.clear();
  if (clippings.capacity() < INITIAL_CLIPPING_RESERVE) {
    clippings.reserve(INITIAL_CLIPPING_RESERVE);
  }

  storeFilePath = storeFilePathForBook(filePath, bookType);
  // Recover an interrupted atomic replacement before deciding this is a new
  // book. Otherwise the next save would overwrite the recovered old excerpts
  // with an empty in-memory index.
  const std::string backupPath = storeFilePath + ".bak";
  if (!Storage.exists(storeFilePath.c_str()) && Storage.exists(backupPath.c_str()) &&
      !Storage.rename(backupPath.c_str(), storeFilePath.c_str())) {
    LOG_ERR("CLIP", "Failed to recover clipping backup on load");
    return false;
  }
  if (!Storage.exists(storeFilePath.c_str())) {
    writable = true;
    return true;
  }

  writable = readFromFile();
  return writable;
}

void ClippingStore::unload() {
  if (dirty) saveToFile();
  writable = false;
  clippings.clear();
  bookFilePath.clear();
  bookTitle.clear();
  bookAuthor.clear();
  storeFilePath.clear();
  dirty = false;
}

ClippingStore::AddResult ClippingStore::addClipping(const uint16_t spineIndex, const uint16_t startPage,
                                                    const uint16_t endPage, const uint16_t pageCount,
                                                    const uint16_t startWordIndex, const uint16_t endWordIndex,
                                                    const uint16_t wordCount, const char* chapterTitle,
                                                    const uint16_t paragraphIndex, const std::string& text,
                                                    const uint32_t layoutSignature) {
  if (!writable) return AddResult::SaveFailed;
  if (clippings.size() >= CLIPPING_MAX_PER_BOOK) {
    LOG_ERR("CLIP", "Clipping limit (%u) reached", CLIPPING_MAX_PER_BOOK);
    return AddResult::LimitReached;
  }

  Clipping clipping;
  clipping.spineIndex = spineIndex;
  clipping.startPage = startPage;
  clipping.endPage = endPage;
  clipping.pageCount = std::max<uint16_t>(1, pageCount);
  clipping.startWordIndex = startWordIndex;
  clipping.endWordIndex = endWordIndex;
  clipping.wordCount = wordCount;
  clipping.paragraphIndex = paragraphIndex;
  clipping.timestamp = static_cast<uint32_t>(millis() / 1000UL);
  clipping.layoutSignature = layoutSignature;
  copyBounded(clipping.chapterTitle, sizeof(clipping.chapterTitle), chapterTitle);
  clipping.textLength = static_cast<uint16_t>(std::min(text.size(), CLIPPING_TEXT_MAX));

  clippings.push_back(std::move(clipping));
  dirty = true;
  HighlightMutation mutation;
  mutation.title = bookTitle;
  mutation.author = bookAuthor;
  mutation.text = text.substr(0, CLIPPING_TEXT_MAX);
  mutation.id = highlightId(bookTitle, bookAuthor, mutation.text);
  if (!writeToFile(&text, clippings.size() - 1, nullptr, &mutation)) {
    clippings.pop_back();
    dirty = true;
    return AddResult::SaveFailed;
  }
  dirty = false;
  return AddResult::Added;
}

bool ClippingStore::stampMissingLayoutSignature(const uint32_t layoutSignature) {
  if (!writable) return false;
  if (layoutSignature == 0) return true;

  bool changed = false;
  for (Clipping& clipping : clippings) {
    if (clipping.layoutSignature == 0) {
      clipping.layoutSignature = layoutSignature;
      changed = true;
    }
  }
  if (!changed) return true;

  dirty = true;
  if (writeToFile()) {
    dirty = false;
    return true;
  }
  return false;
}

bool ClippingStore::removeClippingAt(const size_t index) {
  if (!writable || index >= clippings.size()) return false;
  HighlightMutation mutation;
  mutation.title = bookTitle;
  mutation.author = bookAuthor;
  if (!readClippingText(index, mutation.text)) return false;
  mutation.id = highlightId(bookTitle, bookAuthor, mutation.text);
  mutation.deleted = true;
  // Identical saved passages share an archive ID. Removing just one duplicate
  // must not delete the quote while another local clipping still owns it.
  bool duplicateRemains = false;
  std::string otherText;
  for (size_t i = 0; i < clippings.size(); ++i) {
    if (i == index) continue;
    if (!readClippingText(i, otherText)) return false;
    if (otherText == mutation.text) duplicateRemains = true;
  }
  Clipping clipping = std::move(clippings[index]);
  clippings.erase(clippings.begin() + index);
  dirty = true;
  if (!writeToFile(nullptr, SIZE_MAX, nullptr, duplicateRemains ? nullptr : &mutation)) {
    clippings.insert(clippings.begin() + index, std::move(clipping));
    dirty = true;
    return false;
  }
  dirty = false;
  return true;
}

bool ClippingStore::hasClippingForPage(const uint16_t spineIndex, const uint16_t page) const {
  return std::any_of(clippings.begin(), clippings.end(), [&](const Clipping& clipping) {
    return clipping.spineIndex == spineIndex && page >= clipping.startPage && page <= clipping.endPage;
  });
}

const Clipping* ClippingStore::clippingAt(const size_t index) const {
  if (index >= clippings.size()) return nullptr;
  return &clippings[index];
}

bool ClippingStore::readClippingPreview(const size_t index, char* out, const size_t outSize, size_t& outLength) const {
  outLength = 0;
  if (out && outSize > 0) out[0] = '\0';
  const Clipping* clipping = clippingAt(index);
  if (!out || outSize == 0 || !clipping || storeFilePath.empty()) {
    LOG_ERR("CLIP", "Invalid clipping preview index: %u", static_cast<unsigned>(index));
    return false;
  }
  if (clipping->textLength == 0) return true;

  HalFile f;
  if (!Storage.openFileForRead("CLIP", storeFilePath, f)) return false;
  if (!f.seek(clipping->textOffset)) {
    LOG_ERR("CLIP", "Failed to seek clipping preview at %u", clipping->textOffset);
    return false;
  }
  const bool ok = clippingPreview::read(f, clipping->textLength, out, outSize, outLength);
  if (!ok) LOG_ERR("CLIP", "Failed to read clipping preview at %u", clipping->textOffset);
  return ok;
}

bool ClippingStore::readClippingText(const size_t index, std::string& out) const {
  const Clipping* clipping = clippingAt(index);
  if (!clipping) return false;
  return readClippingText(*clipping, out);
}

bool ClippingStore::readClippingText(const Clipping& clipping, std::string& out) const {
  out.clear();
  if (clipping.textLength == 0) return true;
  if (storeFilePath.empty()) return false;

  HalFile f;
  if (!Storage.openFileForRead("CLIP", storeFilePath, f)) {
    return false;
  }
  if (!f.seek(clipping.textOffset)) {
    f.close();
    LOG_ERR("CLIP", "Failed to seek clipping text at %u: %s", clipping.textOffset, storeFilePath.c_str());
    return false;
  }
  out.resize(clipping.textLength);
  const int expected = static_cast<int>(clipping.textLength);
  const bool ok = f.read(&out[0], clipping.textLength) == expected;
  f.close();
  if (!ok) {
    out.clear();
    LOG_ERR("CLIP", "Failed to read clipping text at %u: %s", clipping.textOffset, storeFilePath.c_str());
  }
  return ok;
}

bool ClippingStore::saveToFile() {
  if (!writable) return false;
  if (!dirty) return true;
  if (writeToFile()) {
    dirty = false;
    return true;
  }
  return false;
}

void ClippingStore::clearAll() {
  if (!writable) return;
  clippings.clear();
  dirty = false;
  if (!storeFilePath.empty() && Storage.exists(storeFilePath.c_str())) {
    Storage.remove(storeFilePath.c_str());
  }
}

bool ClippingStore::readFromFile() { return readFromFile(storeFilePath, clippings); }

bool ClippingStore::readFromFile(const std::string& path, std::vector<Clipping>& out) const {
  out.clear();
  HalFile f;
  if (!Storage.openFileForRead("CLIP", path, f)) {
    out.clear();
    return false;
  }

  uint8_t version = 0;
  uint16_t count = 0;
  std::string title;
  std::string author;
  std::string storedPath;
  if (!serialization::tryReadPod(f, version) ||
      (version != LEGACY_VERSION && version != TEXT_OFFSET_VERSION && version != VERSION) ||
      !serialization::tryReadPod(f, count) || !serialization::tryReadString(f, title, HEADER_STRING_MAX) ||
      !serialization::tryReadString(f, author, HEADER_STRING_MAX) ||
      !serialization::tryReadString(f, storedPath, HEADER_STRING_MAX)) {
    f.close();
    LOG_ERR("CLIP", "Failed to read clipping header: %s", path.c_str());
    out.clear();
    return false;
  }

  if (count > CLIPPING_MAX_PER_BOOK) {
    LOG_ERR("CLIP", "Clipping count %u exceeds max, file may be corrupt: %s", count, path.c_str());
    f.close();
    out.clear();
    return false;
  }

  out.reserve(count);
  for (uint16_t i = 0; i < count; ++i) {
    Clipping clipping;
    if (!serialization::tryReadPod(f, clipping.spineIndex) || !serialization::tryReadPod(f, clipping.startPage) ||
        !serialization::tryReadPod(f, clipping.endPage) || !serialization::tryReadPod(f, clipping.pageCount) ||
        !serialization::tryReadPod(f, clipping.startWordIndex) ||
        !serialization::tryReadPod(f, clipping.endWordIndex) || !serialization::tryReadPod(f, clipping.wordCount) ||
        !serialization::tryReadPod(f, clipping.paragraphIndex) || !serialization::tryReadPod(f, clipping.timestamp)) {
      f.close();
      LOG_ERR("CLIP", "Clipping file truncated at record %u: %s", i, path.c_str());
      out.clear();
      return false;
    }
    if (version >= VERSION && !serialization::tryReadPod(f, clipping.layoutSignature)) {
      f.close();
      LOG_ERR("CLIP", "Clipping file truncated at layout signature, record %u: %s", i, path.c_str());
      out.clear();
      return false;
    }
    if (f.read(reinterpret_cast<uint8_t*>(clipping.chapterTitle), sizeof(clipping.chapterTitle)) !=
        sizeof(clipping.chapterTitle)) {
      f.close();
      LOG_ERR("CLIP", "Clipping file truncated at chapter title, record %u: %s", i, path.c_str());
      out.clear();
      return false;
    }
    clipping.chapterTitle[sizeof(clipping.chapterTitle) - 1] = '\0';
    if (version == LEGACY_VERSION) {
      uint32_t textLen = 0;
      if (!serialization::tryReadPod(f, textLen)) {
        f.close();
        LOG_ERR("CLIP", "Clipping file truncated at text length, record %u: %s", i, path.c_str());
        out.clear();
        return false;
      }
      clipping.textOffset = static_cast<uint32_t>(f.position());
      clipping.textLength = static_cast<uint16_t>(std::min<uint32_t>(textLen, CLIPPING_TEXT_MAX));
      if (f.position() > f.size() || textLen > f.size() - f.position() || (textLen > 0 && !f.seekCur(textLen))) {
        f.close();
        LOG_ERR("CLIP", "Clipping file truncated at text, record %u: %s", i, path.c_str());
        out.clear();
        return false;
      }
    } else {
      if (!serialization::tryReadPod(f, clipping.textLength)) {
        f.close();
        LOG_ERR("CLIP", "Clipping file truncated at text length, record %u: %s", i, path.c_str());
        out.clear();
        return false;
      }
      if (clipping.textLength > CLIPPING_TEXT_MAX) {
        f.close();
        LOG_ERR("CLIP", "Clipping text length %u exceeds max, record %u: %s", clipping.textLength, i, path.c_str());
        out.clear();
        return false;
      }
      clipping.textOffset = static_cast<uint32_t>(f.position());
      if (f.position() > f.size() || clipping.textLength > f.size() - f.position() ||
          (clipping.textLength > 0 && !f.seekCur(clipping.textLength))) {
        f.close();
        LOG_ERR("CLIP", "Clipping file truncated at text, record %u: %s", i, path.c_str());
        out.clear();
        return false;
      }
    }
    out.push_back(std::move(clipping));
  }

  f.close();
  return true;
}

bool ClippingStore::writeToFile(const std::string* replacementText, const size_t replacementIndex,
                                const std::string* textSourcePath, const HighlightMutation* mutation) {
  HighlightOutbox::Transaction transaction;
  if (!transaction.ready()) return false;
  Storage.mkdir("/.crosspoint");
  Storage.mkdir(CLIPPINGS_DIR);

  const std::string tmpPath = storeFilePath + ".tmp";
  const std::string backupPath = storeFilePath + ".bak";
  if (!Storage.exists(storeFilePath.c_str()) && Storage.exists(backupPath.c_str())) {
    if (!Storage.rename(backupPath.c_str(), storeFilePath.c_str())) {
      LOG_ERR("CLIP", "Failed to recover clipping backup: %s", backupPath.c_str());
      return false;
    }
    LOG_INF("CLIP", "Recovered clipping backup: %s", storeFilePath.c_str());
  }
  if (Storage.exists(tmpPath.c_str())) Storage.remove(tmpPath.c_str());
  if (Storage.exists(backupPath.c_str()) && Storage.exists(storeFilePath.c_str())) Storage.remove(backupPath.c_str());

  HalFile source;
  const std::string& sourcePath = textSourcePath ? *textSourcePath : storeFilePath;
  const bool hasTextSource = Storage.exists(sourcePath.c_str());
  const bool hasDestination = Storage.exists(storeFilePath.c_str());
  if (hasTextSource && !Storage.openFileForRead("CLIP", sourcePath, source)) {
    LOG_ERR("CLIP", "Failed to open clipping source for rewrite: %s", sourcePath.c_str());
    return false;
  }

  HalFile f = Storage.open(tmpPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
  if (!f) {
    if (source) source.close();
    LOG_ERR("CLIP", "Failed to open clipping temp file for write: %s", tmpPath.c_str());
    return false;
  }

  const uint16_t count = static_cast<uint16_t>(std::min<size_t>(clippings.size(), CLIPPING_MAX_PER_BOOK));
  std::vector<uint32_t> newTextOffsets;
  newTextOffsets.reserve(count);
  std::vector<uint16_t> newTextLengths;
  newTextLengths.reserve(count);
  if (!serialization::tryWritePod(f, VERSION) || !serialization::tryWritePod(f, count) ||
      !serialization::tryWriteString(f, bookTitle) || !serialization::tryWriteString(f, bookAuthor) ||
      !serialization::tryWriteString(f, bookFilePath)) {
    LOG_ERR("CLIP", "Failed to write clipping header: %s", tmpPath.c_str());
    f.close();
    if (source) source.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }

  for (uint16_t i = 0; i < count; ++i) {
    const Clipping& clipping = clippings[i];
    if (!serialization::tryWritePod(f, clipping.spineIndex) || !serialization::tryWritePod(f, clipping.startPage) ||
        !serialization::tryWritePod(f, clipping.endPage) || !serialization::tryWritePod(f, clipping.pageCount) ||
        !serialization::tryWritePod(f, clipping.startWordIndex) ||
        !serialization::tryWritePod(f, clipping.endWordIndex) || !serialization::tryWritePod(f, clipping.wordCount) ||
        !serialization::tryWritePod(f, clipping.paragraphIndex) || !serialization::tryWritePod(f, clipping.timestamp) ||
        !serialization::tryWritePod(f, clipping.layoutSignature) ||
        f.write(reinterpret_cast<const uint8_t*>(clipping.chapterTitle), sizeof(clipping.chapterTitle)) !=
            sizeof(clipping.chapterTitle)) {
      LOG_ERR("CLIP", "Failed to write clipping record %u: %s", i, storeFilePath.c_str());
      f.close();
      if (source) source.close();
      Storage.remove(tmpPath.c_str());
      return false;
    }

    const bool useReplacement = replacementText && i == replacementIndex;
    const uint16_t textLen = useReplacement
                                 ? static_cast<uint16_t>(std::min(replacementText->size(), CLIPPING_TEXT_MAX))
                                 : clipping.textLength;
    if (!serialization::tryWritePod(f, textLen)) {
      LOG_ERR("CLIP", "Failed to write clipping text length %u: %s", i, tmpPath.c_str());
      f.close();
      if (source) source.close();
      Storage.remove(tmpPath.c_str());
      return false;
    }

    const uint32_t newTextOffset = static_cast<uint32_t>(f.position());
    bool wroteText = true;
    if (textLen > 0 && useReplacement) {
      wroteText = f.write(reinterpret_cast<const uint8_t*>(replacementText->data()), textLen) == textLen;
    } else if (textLen > 0) {
      wroteText = source && source.seek(clipping.textOffset) && copyBytes(source, f, textLen);
    }
    if (!wroteText) {
      LOG_ERR("CLIP", "Failed to write clipping text %u: %s", i, tmpPath.c_str());
      f.close();
      if (source) source.close();
      Storage.remove(tmpPath.c_str());
      return false;
    }
    newTextOffsets.push_back(newTextOffset);
    newTextLengths.push_back(textLen);
  }

  // close() syncs buffered writes and reports SD errors. Never replace the
  // last good store until that durable write has succeeded.
  const bool closed = f.close();
  if (source) source.close();
  if (!closed) {
    LOG_ERR("CLIP", "Failed to sync clipping temp file");
    Storage.remove(tmpPath.c_str());
    return false;
  }

  // Persist the event before replacing the authoritative clipping file. Its
  // before/after digests make reboot recovery distinguish commit from rollback.
  if (mutation && !transaction.prepare(storeFilePath, tmpPath, *mutation)) {
    Storage.remove(tmpPath.c_str());
    return false;
  }

  if (hasDestination && !Storage.rename(storeFilePath.c_str(), backupPath.c_str())) {
    LOG_ERR("CLIP", "Failed to back up clipping file: %s", storeFilePath.c_str());
    Storage.remove(tmpPath.c_str());
    return false;
  }
  if (!Storage.rename(tmpPath.c_str(), storeFilePath.c_str())) {
    LOG_ERR("CLIP", "Failed to replace clipping file: %s", storeFilePath.c_str());
    Storage.remove(tmpPath.c_str());
    if (hasDestination) Storage.rename(backupPath.c_str(), storeFilePath.c_str());
    return false;
  }
  if (hasDestination && Storage.exists(backupPath.c_str())) {
    Storage.remove(backupPath.c_str());
  }
  if (!transaction.commit()) {
    // The source is committed. Keep the durable intent and refuse subsequent
    // mutations until recovery can finalize its outbox entry.
    writable = false;
    LOG_ERR("CLIP", "Saved clipping; outbox finalization pending");
  }
  for (uint16_t i = 0; i < count; ++i) {
    clippings[i].textOffset = newTextOffsets[i];
    clippings[i].textLength = newTextLengths[i];
  }
  return true;
}

int ClippingStore::seedOneArchiveClipping() {
  HighlightOutbox::Transaction transaction;
  if (!transaction.ready()) return -1;
  if (!Storage.exists(CLIPPINGS_DIR)) return 0;
  HalFile directory = Storage.open(CLIPPINGS_DIR);
  if (!directory || !directory.isDirectory()) return -1;
  while (auto entry = directory.openNextFile()) {
    char name[96];
    entry.getName(name, sizeof(name));
    const size_t length = strlen(name);
    if (length < 4 || strcmp(name + length - 4, ".bin") != 0) continue;
    const std::string path = std::string(CLIPPINGS_DIR) + "/" + name;
    const std::string marker = path + ".archive-seeded";
    std::string digest;
    if (!transaction.digest(path, digest)) return -1;
    uint16_t cursor = 0;
    {
      HalFile saved;
      std::string previousDigest;
      if (Storage.openFileForRead("HSeed", marker, saved) && serialization::tryReadString(saved, previousDigest, 32) &&
          previousDigest == digest) {
        if (!serialization::tryReadPod(saved, cursor)) cursor = 0;
      }
    }
    // A single bounded book index is loaded, not the entire library. Heap
    // allocation keeps its strings/index off the background task's small stack.
    auto header = makeUniqueNoThrow<ClippingFileHeader>();
    auto book = makeUniqueNoThrow<ClippingStore>();
    if (!header || !book || !readClippingFileHeader(path, name, *header)) return -1;
    if (cursor >= header->count) continue;
    book->storeFilePath = path;
    if (!book->readFromFile()) return -1;
    HighlightMutation mutation;
    mutation.title = header->title;
    mutation.author = header->author;
    if (!book->readClippingText(cursor, mutation.text)) return -1;
    mutation.id = highlightId(mutation.title, mutation.author, mutation.text);
    if (!transaction.prepare(path, path, mutation) || !transaction.commit()) return -1;
    ++cursor;
    const std::string temporary = marker + ".tmp";
    HalFile output = Storage.open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    const bool written =
        output && serialization::tryWriteString(output, digest) && serialization::tryWritePod(output, cursor);
    const bool closed = output && output.close();
    if (!written || !closed) return -1;
    if (Storage.exists(marker.c_str()) && !Storage.remove(marker.c_str())) return -1;
    if (!Storage.rename(temporary.c_str(), marker.c_str())) return -1;
    return 1;
  }
  return 0;
}

bool ClippingStore::hasAnyClippings() {
  if (!Storage.exists(CLIPPINGS_DIR)) return false;
  return !Storage.listFiles(CLIPPINGS_DIR).empty();
}

bool ClippingStore::hasForFilePath(const std::string& filePath, const std::string& bookType) {
  return Storage.exists(storeFilePathForBook(filePath, bookType).c_str());
}

bool ClippingStore::getAllClippedBooks(std::vector<ClippedBookEntry>& out) {
  if (!Storage.exists(CLIPPINGS_DIR)) return true;

  const auto files = Storage.listFiles(CLIPPINGS_DIR);
  for (const auto& name : files) {
    ClippingFileHeader header;
    const std::string fullPath = std::string(CLIPPINGS_DIR) + "/" + name.c_str();
    if (!readClippingFileHeader(fullPath, name.c_str(), header)) continue;
    if (header.path.empty() || header.count == 0 || !Storage.exists(header.path.c_str())) continue;

    auto existing = std::find_if(out.begin(), out.end(), [&](const ClippedBookEntry& entry) {
      return entry.bookPath == header.path && entry.bookType == header.bookType;
    });
    if (existing != out.end()) {
      existing->count = std::max(existing->count, header.count);
      continue;
    }
    out.push_back({std::move(header.title), std::move(header.author), std::move(header.path),
                   std::move(header.bookType), header.count});
  }
  return true;
}

void ClippingStore::deleteForFilePath(const std::string& filePath, const std::string& bookType) {
  const std::string path = storeFilePathForBook(filePath, bookType);
  if (Storage.exists(path.c_str())) {
    Storage.remove(path.c_str());
  }
}

bool ClippingStore::migrateForFilePath(const std::string& oldFilePath, const std::string& newFilePath,
                                       const std::string& title, const std::string& author, const std::string& bookType,
                                       const bool preserveSource) {
  const std::string oldStorePath = storeFilePathForBook(oldFilePath, bookType);
  if (!Storage.exists(oldStorePath.c_str())) {
    return true;
  }

  ClippingStore reader;
  std::vector<Clipping> migratedClippings;
  if (!reader.readFromFile(oldStorePath, migratedClippings)) {
    return false;
  }

  const std::string newStorePath = storeFilePathForBook(newFilePath, bookType);
  ClippingStore writer;
  writer.bookFilePath = newFilePath;
  writer.bookTitle = title;
  writer.bookAuthor = author;
  writer.storeFilePath = newStorePath;
  writer.clippings = std::move(migratedClippings);
  if (oldStorePath == newStorePath) {
    return writer.writeToFile();
  }

  const std::string rewriteBackupPath = newStorePath + ".bak";
  if (!Storage.exists(newStorePath.c_str()) && Storage.exists(rewriteBackupPath.c_str())) {
    if (!Storage.rename(rewriteBackupPath.c_str(), newStorePath.c_str())) {
      LOG_ERR("CLIP", "Failed to recover destination clipping backup: %s", rewriteBackupPath.c_str());
      return false;
    }
  } else if (Storage.exists(newStorePath.c_str()) && Storage.exists(rewriteBackupPath.c_str()) &&
             !Storage.remove(rewriteBackupPath.c_str())) {
    LOG_ERR("CLIP", "Failed to remove stale destination clipping backup: %s", rewriteBackupPath.c_str());
    return false;
  }

  const std::string backupPath = newStorePath + ".migrate.bak";
  const bool hasDestination = Storage.exists(newStorePath.c_str());
  if (hasDestination) {
    if (Storage.exists(backupPath.c_str()) && !Storage.remove(backupPath.c_str())) {
      LOG_ERR("CLIP", "Failed to remove stale clipping migration backup: %s", backupPath.c_str());
      return false;
    }
    if (!Storage.rename(newStorePath.c_str(), backupPath.c_str())) {
      LOG_ERR("CLIP", "Failed to back up destination clippings: %s", newStorePath.c_str());
      return false;
    }
  }
  if (!writer.writeToFile(nullptr, SIZE_MAX, &oldStorePath)) {
    LOG_ERR("CLIP", "Failed to write migrated clippings: %s", newStorePath.c_str());
    if (hasDestination && !Storage.rename(backupPath.c_str(), newStorePath.c_str())) {
      LOG_ERR("CLIP", "Failed to restore destination clipping backup: %s", backupPath.c_str());
    }
    return false;
  }
  if (!preserveSource && !Storage.remove(oldStorePath.c_str())) {
    LOG_ERR("CLIP", "Failed to remove migrated source clippings (non-fatal): %s", oldStorePath.c_str());
  }
  if (hasDestination && Storage.exists(backupPath.c_str())) {
    Storage.remove(backupPath.c_str());
  }
  return true;
}
