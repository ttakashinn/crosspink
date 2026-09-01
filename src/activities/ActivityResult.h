#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

struct WifiResult {
  bool connected = false;
  std::string ssid;
  std::string ip;
};

struct KeyboardResult {
  std::string text;
};

struct MenuResult {
  int action = -1;
  uint8_t orientation = 0;
  uint16_t autoPageTurnSeconds = 0;
  uint8_t wordSpacing = 0;
  uint8_t repairParagraphIndent = 0;
  uint8_t renderMode = 0;
};

struct ChapterResult {
  int spineIndex = 0;
  std::string anchor;
};

struct PercentResult {
  int percent = 0;
};

struct IntervalResult {
  uint32_t value = 0;
};

struct PageResult {
  uint32_t page = 0;
};

struct ProgressChangeResult {
  int spineIndex = 0;
  int page = 0;
  int totalPages = 0;
  std::string xpath;
  float percentage = 0.0f;
  bool hasSavedProgress = false;
  // Exact visible-codepoint offset within spineIndex, when the source (a bookmark) has one.
  // Preferred over xpath/percentage on resolution: it is immune to re-pagination.
  bool hasVisibleTextOffset = false;
  uint32_t visibleTextOffset = 0;
};

enum class NetworkMode;

struct NetworkModeResult {
  NetworkMode mode;
};

struct FootnoteResult {
  std::string href;
};

struct FilePathResult {
  std::string path;
};

struct DictionarySelectionResult {
  // Book mode uses inheritGlobal to clear the per-book override. An empty
  // name with inheritGlobal=false explicitly disables dictionary lookup for
  // that book. Temporary mode always returns a non-empty name.
  std::string name;
  bool inheritGlobal = false;
};

struct LookupQueryResult {
  std::string query;
};

struct DictionaryExitResult {
  bool exitAll = false;
};

struct ClippingSelectionResult {
  struct Segment {
    uint16_t pageHint = 0;
    uint32_t pageVisibleOffset = 0;
    uint16_t startWordIndex = 0;
    uint16_t endWordIndex = 0;
    uint16_t textOffset = 0;
    uint16_t textLength = 0;
  };

  static constexpr size_t MAX_SEGMENTS = 4;
  uint16_t startWordIndex = 0;
  uint16_t endWordIndex = 0;
  std::string text;
  uint32_t clippingId = 0;
  uint16_t spineIndex = 0;
  uint8_t segmentCount = 0;
  std::array<Segment, MAX_SEGMENTS> segments{};
};

using ResultVariant =
    std::variant<std::monostate, WifiResult, KeyboardResult, MenuResult, ChapterResult, PercentResult, IntervalResult,
                 PageResult, ProgressChangeResult, NetworkModeResult, FootnoteResult, FilePathResult,
                 DictionarySelectionResult, LookupQueryResult, DictionaryExitResult, ClippingSelectionResult>;

struct ActivityResult {
  bool isCancelled = false;
  ResultVariant data;

  explicit ActivityResult() = default;

  template <typename ResultType>
    requires std::is_constructible_v<ResultVariant, ResultType&&>
  // cppcheck-suppress noExplicitConstructor
  ActivityResult(ResultType&& result) : data{std::forward<ResultType>(result)} {}
};

using ActivityResultHandler = std::function<void(const ActivityResult&)>;
