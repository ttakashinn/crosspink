#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class DictionaryHistoryStore {
 public:
  enum class CodecStatus { OK, INVALID, NEWER_VERSION, LIMIT_EXCEEDED };

  static constexpr size_t MAX_ENTRIES = 15;
  static constexpr size_t MAX_FILE_BYTES = 4096;
  static constexpr size_t MAX_QUERY_BYTES = 255;
  static constexpr const char* FILE_PATH = "/.crosspoint/vns_dictionary_history.txt";

  static DictionaryHistoryStore& getInstance();

  static CodecStatus encode(const std::vector<std::string>& entries, std::string& data);
  static CodecStatus decode(const uint8_t* data, size_t size, std::vector<std::string>& entries);

  bool load();
  void record(const std::string& query);
  bool flush();
  bool clear();
  const std::vector<std::string>& entries();
  bool isWritable() const { return writable_; }

#ifdef UNIT_TEST
  void resetForTests(std::string path = FILE_PATH) {
    path_ = std::move(path);
    loaded_ = false;
    writable_ = true;
    dirty_ = false;
    entries_.clear();
  }
#endif

 private:
  std::string candidatePath(const char* suffix) const;

  std::string path_ = FILE_PATH;
  bool loaded_ = false;
  bool writable_ = true;
  bool dirty_ = false;
  std::vector<std::string> entries_;
};

#define DICTIONARY_HISTORY DictionaryHistoryStore::getInstance()
