#include "CssParser.h"

#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstring>
#include <string_view>

namespace {

// Stack-allocated string buffer to avoid heap reallocations during parsing
// Provides string-like interface with fixed capacity
struct StackBuffer {
  static constexpr size_t CAPACITY = 1024;
  char data[CAPACITY];
  size_t len = 0;

  bool push_back(char c) {
    if (len >= CAPACITY) return false;
    data[len++] = c;
    return true;
  }

  void clear() { len = 0; }
  bool empty() const { return len == 0; }
  size_t size() const { return len; }

  // Get string view of current content (zero-copy)
  std::string_view view() const { return std::string_view(data, len); }
  operator std::string_view() const noexcept { return view(); }
};

// Buffer size for reading CSS files
constexpr size_t READ_BUFFER_SIZE = 512;

// Flat rule-store caps. The index is at most 18KB at MAX_RULES, selector text is
// bounded to 32KB, and deduplicated style bodies are bounded to about 26KB.
constexpr size_t MAX_RULES = 1500;
constexpr size_t SELECTOR_POOL_CAP = 32 * 1024;
constexpr size_t MAX_UNIQUE_STYLES = 256;
constexpr size_t MAX_PROPERTY_ORDER_OVERRIDES = 512;
constexpr uint8_t CSS_PROPERTY_COUNT = 21;

// Maximum length for a single selector string
// Prevents parsing of extremely long or malformed selectors
constexpr size_t MAX_SELECTOR_LENGTH = 256;
constexpr size_t RULE_CASCADE_FIXED_WIRE_BYTES = sizeof(uint16_t) * 2;
constexpr size_t PROPERTY_ORDER_WIRE_BYTES = sizeof(uint8_t) + sizeof(uint16_t);

// Check if character is CSS whitespace
constexpr bool isCssWhitespace(const char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

constexpr std::string_view trimCssWhitespace(std::string_view s) {
  while (!s.empty() && isCssWhitespace(s.front())) s.remove_prefix(1);
  while (!s.empty() && isCssWhitespace(s.back())) s.remove_suffix(1);
  return s;
}

uint16_t selectorSpecificity(const std::string_view selector) {
  const uint16_t classWeight = selector.find('.') == std::string_view::npos ? 0 : 256;
  const uint16_t elementWeight = !selector.empty() && selector.front() != '.' ? 1 : 0;
  return static_cast<uint16_t>(classWeight + elementWeight);
}

uint16_t descendantSpecificity(const std::string_view ancestor, const std::string_view subject) {
  return static_cast<uint16_t>(selectorSpecificity(ancestor) + selectorSpecificity(subject));
}

constexpr char asciiToLower(const char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

// Case-insensitive equality on ASCII. lowercaseKeyword MUST already be
// lowercase; CSS keywords are ASCII by spec so byte-wise tolower is safe.
constexpr bool iequalsAscii(std::string_view value, std::string_view lowercaseKeyword) {
  return std::equal(value.begin(), value.end(), lowercaseKeyword.begin(), lowercaseKeyword.end(),
                    [](char a, char b) { return asciiToLower(a) == b; });
}

// Walk s and invoke fn(token) for each non-empty run between delimiters.
// Tokens are boundary-trimmed and yielded as string_views into s; no
// allocation. Runs of consecutive delimiters coalesce — no empty tokens are
// emitted. `isDelimiter` is invoked once per character.
template <typename Pred, typename F>
void forEachDelimitedToken(std::string_view s, Pred isDelimiter, F&& fn) {
  size_t start = 0;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i == s.size() || isDelimiter(s[i])) {
      const std::string_view trimmed = trimCssWhitespace(s.substr(start, i - start));
      if (!trimmed.empty()) {
        fn(trimmed);
      }
      start = i + 1;
    }
  }
}

// Parse the entirety of s as a number into `out`. Accepts an optional leading
// '+' (which std::from_chars rejects by spec) so callers can pass CSS-style
// signed numbers without manual trimming. Returns false on empty input, a
// non-numeric suffix, or any from_chars error.
template <typename T>
bool tryParseNumber(std::string_view s, T& out) {
  const char* begin = s.data();
  const char* end = s.data() + s.size();
  if (begin < end && *begin == '+') ++begin;
  const auto r = std::from_chars(begin, end, out);
  return r.ec == std::errc{} && r.ptr == end;
}

// Collect up to 4 whitespace-separated tokens for a CSS edge-value shorthand
// (margin, padding, and the border-* family). Returns the number of tokens
// written; extras are silently dropped. Callers apply the 1/2/3/4-value
// fallback rule using the returned count.
size_t collectEdgeValueTokens(std::string_view s, std::string_view (&out)[4]) {
  size_t count = 0;
  forEachDelimitedToken(s, isCssWhitespace, [&](std::string_view tok) {
    if (count < 4) out[count++] = tok;
  });
  return count;
}

std::string_view stripTrailingImportant(std::string_view value, bool* importantOut = nullptr) {
  constexpr std::string_view IMPORTANT = "important";
  if (importantOut) *importantOut = false;

  while (!value.empty() && isCssWhitespace(value.back())) {
    value.remove_suffix(1);
  }

  if (value.size() < IMPORTANT.size()) {
    return value;
  }

  const size_t suffixPos = value.size() - IMPORTANT.size();
  if (!iequalsAscii(value.substr(suffixPos), IMPORTANT)) {
    return value;
  }

  std::string_view prefix = value.substr(0, suffixPos);
  while (!prefix.empty() && isCssWhitespace(prefix.back())) prefix.remove_suffix(1);
  if (prefix.empty() || prefix.back() != '!') return value;
  prefix.remove_suffix(1);
  while (!prefix.empty() && isCssWhitespace(prefix.back())) prefix.remove_suffix(1);
  if (importantOut) *importantOut = true;
  return prefix;
}

constexpr std::array STYLE_LENGTH_FIELDS = {
    &CssStyle::textIndent,   &CssStyle::marginTop,   &CssStyle::marginBottom,  &CssStyle::marginLeft,
    &CssStyle::marginRight,  &CssStyle::paddingTop,  &CssStyle::paddingBottom, &CssStyle::paddingLeft,
    &CssStyle::paddingRight, &CssStyle::imageHeight, &CssStyle::imageWidth,
};
constexpr size_t STYLE_LENGTH_FIELD_COUNT = STYLE_LENGTH_FIELDS.size();
constexpr size_t STYLE_WIRE_BYTES =
    6 + STYLE_LENGTH_FIELD_COUNT * (sizeof(decltype(CssLength::value)) + 1) + 4 + sizeof(uint32_t) * 2;
constexpr uint32_t CSS_DEFINED_BITS_MASK = (1u << 21) - 1;

bool styleDefinesProperty(const CssStyle& style, const uint8_t property) {
  switch (property) {
    case 0:
      return style.hasTextAlign();
    case 1:
      return style.hasFontStyle();
    case 2:
      return style.hasFontWeight();
    case 3:
      return style.hasTextDecoration();
    case 4:
      return style.hasTextIndent();
    case 5:
      return style.hasMarginTop();
    case 6:
      return style.hasMarginBottom();
    case 7:
      return style.hasMarginLeft();
    case 8:
      return style.hasMarginRight();
    case 9:
      return style.hasPaddingTop();
    case 10:
      return style.hasPaddingBottom();
    case 11:
      return style.hasPaddingLeft();
    case 12:
      return style.hasPaddingRight();
    case 13:
      return style.hasImageHeight();
    case 14:
      return style.hasImageWidth();
    case 15:
      return style.hasDisplay();
    case 16:
      return style.hasDirection();
    case 17:
      return style.hasVerticalAlign();
    case 18:
      return style.hasPageBreakBefore();
    case 19:
      return style.hasPageBreakAfter();
    case 20:
      return style.hasFontVariantCaps();
    default:
      return false;
  }
}

void copyStyleProperty(CssStyle& destination, const CssStyle& source, const uint8_t property) {
  switch (property) {
    case 0:
      destination.textAlign = source.textAlign;
      destination.defined.textAlign = 1;
      break;
    case 1:
      destination.fontStyle = source.fontStyle;
      destination.defined.fontStyle = 1;
      break;
    case 2:
      destination.fontWeight = source.fontWeight;
      destination.defined.fontWeight = 1;
      break;
    case 3:
      destination.textDecoration = source.textDecoration;
      destination.defined.textDecoration = 1;
      break;
    case 4:
      destination.textIndent = source.textIndent;
      destination.defined.textIndent = 1;
      break;
    case 5:
      destination.marginTop = source.marginTop;
      destination.defined.marginTop = 1;
      break;
    case 6:
      destination.marginBottom = source.marginBottom;
      destination.defined.marginBottom = 1;
      break;
    case 7:
      destination.marginLeft = source.marginLeft;
      destination.defined.marginLeft = 1;
      break;
    case 8:
      destination.marginRight = source.marginRight;
      destination.defined.marginRight = 1;
      break;
    case 9:
      destination.paddingTop = source.paddingTop;
      destination.defined.paddingTop = 1;
      break;
    case 10:
      destination.paddingBottom = source.paddingBottom;
      destination.defined.paddingBottom = 1;
      break;
    case 11:
      destination.paddingLeft = source.paddingLeft;
      destination.defined.paddingLeft = 1;
      break;
    case 12:
      destination.paddingRight = source.paddingRight;
      destination.defined.paddingRight = 1;
      break;
    case 13:
      destination.imageHeight = source.imageHeight;
      destination.defined.imageHeight = 1;
      break;
    case 14:
      destination.imageWidth = source.imageWidth;
      destination.defined.imageWidth = 1;
      break;
    case 15:
      destination.display = source.display;
      destination.defined.display = 1;
      break;
    case 16:
      destination.direction = source.direction;
      destination.defined.direction = 1;
      break;
    case 17:
      destination.verticalAlign = source.verticalAlign;
      destination.defined.verticalAlign = 1;
      break;
    case 18:
      destination.pageBreakBefore = source.pageBreakBefore;
      destination.defined.pageBreakBefore = 1;
      break;
    case 19:
      destination.pageBreakAfter = source.pageBreakAfter;
      destination.defined.pageBreakAfter = 1;
      break;
    case 20:
      destination.fontVariantCaps = source.fontVariantCaps;
      destination.defined.fontVariantCaps = 1;
      break;
    default:
      break;
  }
  destination.setImportant(property, source.isImportant(property));
}

void encodeStyleWire(const CssStyle& style, uint8_t (&out)[STYLE_WIRE_BYTES]) {
  size_t offset = 0;
  out[offset++] = static_cast<uint8_t>(style.textAlign);
  out[offset++] = static_cast<uint8_t>(style.fontStyle);
  out[offset++] = static_cast<uint8_t>(style.fontWeight);
  out[offset++] = static_cast<uint8_t>(style.fontVariantCaps);
  out[offset++] = static_cast<uint8_t>(style.textDecoration);
  out[offset++] = static_cast<uint8_t>(style.direction);

  const auto putLength = [&out, &offset](const CssLength& length) {
    memcpy(out + offset, &length.value, sizeof(length.value));
    offset += sizeof(length.value);
    out[offset++] = static_cast<uint8_t>(length.unit);
  };
  for (const auto field : STYLE_LENGTH_FIELDS) {
    putLength(style.*field);
  }
  out[offset++] = static_cast<uint8_t>(style.pageBreakBefore);
  out[offset++] = static_cast<uint8_t>(style.pageBreakAfter);
  out[offset++] = static_cast<uint8_t>(style.display);
  out[offset++] = static_cast<uint8_t>(style.verticalAlign);

  uint32_t definedBits = 0;
  if (style.defined.textAlign) definedBits |= 1 << 0;
  if (style.defined.fontStyle) definedBits |= 1 << 1;
  if (style.defined.fontWeight) definedBits |= 1 << 2;
  if (style.defined.textDecoration) definedBits |= 1 << 3;
  if (style.defined.textIndent) definedBits |= 1 << 4;
  if (style.defined.marginTop) definedBits |= 1 << 5;
  if (style.defined.marginBottom) definedBits |= 1 << 6;
  if (style.defined.marginLeft) definedBits |= 1 << 7;
  if (style.defined.marginRight) definedBits |= 1 << 8;
  if (style.defined.paddingTop) definedBits |= 1 << 9;
  if (style.defined.paddingBottom) definedBits |= 1 << 10;
  if (style.defined.paddingLeft) definedBits |= 1 << 11;
  if (style.defined.paddingRight) definedBits |= 1 << 12;
  if (style.defined.imageHeight) definedBits |= 1 << 13;
  if (style.defined.imageWidth) definedBits |= 1 << 14;
  if (style.defined.display) definedBits |= 1 << 15;
  if (style.defined.direction) definedBits |= 1 << 16;
  if (style.defined.verticalAlign) definedBits |= 1 << 17;
  if (style.defined.pageBreakBefore) definedBits |= 1 << 18;
  if (style.defined.pageBreakAfter) definedBits |= 1 << 19;
  if (style.defined.fontVariantCaps) definedBits |= 1 << 20;
  memcpy(out + offset, &definedBits, sizeof(definedBits));
  offset += sizeof(definedBits);
  memcpy(out + offset, &style.importantBits, sizeof(style.importantBits));
}

bool decodeStyleWire(const uint8_t (&in)[STYLE_WIRE_BYTES], CssStyle& style) {
  size_t offset = 0;
  const uint8_t textAlign = in[offset++];
  const uint8_t fontStyle = in[offset++];
  const uint8_t fontWeight = in[offset++];
  const uint8_t fontVariantCaps = in[offset++];
  const uint8_t textDecoration = in[offset++];
  const uint8_t direction = in[offset++];
  if (textAlign > static_cast<uint8_t>(CssTextAlign::None) || fontStyle > static_cast<uint8_t>(CssFontStyle::Italic) ||
      fontWeight > static_cast<uint8_t>(CssFontWeight::Bold) ||
      fontVariantCaps > static_cast<uint8_t>(CssFontVariantCaps::SmallCaps) ||
      (textDecoration & ~CSS_TEXT_DECORATION_MASK) != 0 || direction > static_cast<uint8_t>(CssTextDirection::Rtl)) {
    return false;
  }
  style.textAlign = static_cast<CssTextAlign>(textAlign);
  style.fontStyle = static_cast<CssFontStyle>(fontStyle);
  style.fontWeight = static_cast<CssFontWeight>(fontWeight);
  style.fontVariantCaps = static_cast<CssFontVariantCaps>(fontVariantCaps);
  style.textDecoration = static_cast<CssTextDecoration>(textDecoration);
  style.direction = static_cast<CssTextDirection>(direction);

  const auto getLength = [&in, &offset](CssLength& length) {
    decltype(CssLength::value) value = 0;
    memcpy(&value, in + offset, sizeof(value));
    offset += sizeof(length.value);
    const uint8_t unit = in[offset++];
    if (!std::isfinite(value) || unit > static_cast<uint8_t>(CssUnit::Percent)) return false;
    length.value = value;
    length.unit = static_cast<CssUnit>(unit);
    return true;
  };
  for (const auto field : STYLE_LENGTH_FIELDS) {
    if (!getLength(style.*field)) return false;
  }

  const uint8_t pageBreakBefore = in[offset++];
  const uint8_t pageBreakAfter = in[offset++];
  const uint8_t display = in[offset++];
  const uint8_t verticalAlign = in[offset++];
  if (pageBreakBefore > static_cast<uint8_t>(CssPageBreak::Always) ||
      pageBreakAfter > static_cast<uint8_t>(CssPageBreak::Always) || display > static_cast<uint8_t>(CssDisplay::None) ||
      verticalAlign > static_cast<uint8_t>(CssVerticalAlign::Sub)) {
    return false;
  }
  style.pageBreakBefore = static_cast<CssPageBreak>(pageBreakBefore);
  style.pageBreakAfter = static_cast<CssPageBreak>(pageBreakAfter);
  style.display = static_cast<CssDisplay>(display);
  style.verticalAlign = static_cast<CssVerticalAlign>(verticalAlign);

  uint32_t definedBits = 0;
  memcpy(&definedBits, in + offset, sizeof(definedBits));
  if ((definedBits & ~CSS_DEFINED_BITS_MASK) != 0) return false;
  style.defined.textAlign = (definedBits & 1 << 0) != 0;
  style.defined.fontStyle = (definedBits & 1 << 1) != 0;
  style.defined.fontWeight = (definedBits & 1 << 2) != 0;
  style.defined.textDecoration = (definedBits & 1 << 3) != 0;
  style.defined.textIndent = (definedBits & 1 << 4) != 0;
  style.defined.marginTop = (definedBits & 1 << 5) != 0;
  style.defined.marginBottom = (definedBits & 1 << 6) != 0;
  style.defined.marginLeft = (definedBits & 1 << 7) != 0;
  style.defined.marginRight = (definedBits & 1 << 8) != 0;
  style.defined.paddingTop = (definedBits & 1 << 9) != 0;
  style.defined.paddingBottom = (definedBits & 1 << 10) != 0;
  style.defined.paddingLeft = (definedBits & 1 << 11) != 0;
  style.defined.paddingRight = (definedBits & 1 << 12) != 0;
  style.defined.imageHeight = (definedBits & 1 << 13) != 0;
  style.defined.imageWidth = (definedBits & 1 << 14) != 0;
  style.defined.display = (definedBits & 1 << 15) != 0;
  style.defined.direction = (definedBits & 1 << 16) != 0;
  style.defined.verticalAlign = (definedBits & 1 << 17) != 0;
  style.defined.pageBreakBefore = (definedBits & 1 << 18) != 0;
  style.defined.pageBreakAfter = (definedBits & 1 << 19) != 0;
  style.defined.fontVariantCaps = (definedBits & 1 << 20) != 0;
  offset += sizeof(definedBits);
  memcpy(&style.importantBits, in + offset, sizeof(style.importantBits));
  if ((style.importantBits & ~CSS_DEFINED_BITS_MASK) != 0 || (style.importantBits & ~definedBits) != 0) return false;
  return true;
}

}  // anonymous namespace

int CssParser::compareEntryToPieces(const SelectorEntry& entry, const std::string_view p0, const std::string_view p1,
                                    const std::string_view p2) const {
  const char* stored = selectorPool_.get() + entry.offset;
  const std::string_view pieces[] = {p0, p1, p2};
  size_t index = 0;
  for (const std::string_view piece : pieces) {
    for (const char c : piece) {
      if (index == entry.length) return -1;
      const auto storedByte = static_cast<unsigned char>(stored[index]);
      const auto probeByte = static_cast<unsigned char>(asciiToLower(c));
      if (storedByte != probeByte) return storedByte < probeByte ? -1 : 1;
      ++index;
    }
  }
  return index == entry.length ? 0 : 1;
}

size_t CssParser::lowerBound(const std::string_view p0, const std::string_view p1, const std::string_view p2,
                             bool& exact) const {
  size_t low = 0;
  size_t high = entryCount_;
  while (low < high) {
    const size_t middle = low + (high - low) / 2;
    if (compareEntryToPieces(entries_[middle], p0, p1, p2) < 0) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }
  exact = low < entryCount_ && compareEntryToPieces(entries_[low], p0, p1, p2) == 0;
  return low;
}

const CssParser::SelectorEntry* CssParser::findEntry(const std::string_view p0, const std::string_view p1,
                                                     const std::string_view p2) const {
  bool exact = false;
  const size_t index = lowerBound(p0, p1, p2, exact);
  return exact ? &entries_[index] : nullptr;
}

std::string_view CssParser::selectorAt(const size_t index) const {
  const SelectorEntry& entry = entries_[index];
  return {selectorPool_.get() + entry.offset, entry.length};
}

std::string_view CssParser::descendantAncestorAt(const size_t index) const {
  const DescendantEntry& entry = descendantEntries_[index];
  return {selectorPool_.get() + entry.ancestorOffset, entry.ancestorLength};
}

std::string_view CssParser::descendantSubjectAt(const size_t index) const {
  const DescendantEntry& entry = descendantEntries_[index];
  return {selectorPool_.get() + entry.subjectOffset, entry.subjectLength};
}

CssParser::PoolResult CssParser::ensureEntryCapacity(const size_t needed) {
  if (needed <= entryCapacity_) return PoolResult::Ready;
  if (needed > MAX_RULES) return PoolResult::Limit;

  size_t capacity = entryCapacity_ ? entryCapacity_ * 2u : 128u;
  while (capacity < needed) capacity *= 2u;
  capacity = std::min(capacity, MAX_RULES);
  auto grown = makeUniqueNoThrow<SelectorEntry[]>(capacity);
  if (!grown) {
    LOG_ERR("CSS", "OOM: selector index (%zu entries)", capacity);
    return PoolResult::OutOfMemory;
  }
  if (entryCount_ > 0) memcpy(grown.get(), entries_.get(), entryCount_ * sizeof(SelectorEntry));
  entries_ = std::move(grown);
  entryCapacity_ = static_cast<uint16_t>(capacity);
  return PoolResult::Ready;
}

CssParser::PoolResult CssParser::ensureDescendantEntryCapacity(const size_t needed) {
  if (needed <= descendantEntryCapacity_) return PoolResult::Ready;
  if (needed > MAX_DESCENDANT_RULES) return PoolResult::Limit;

  size_t capacity = descendantEntryCapacity_ ? descendantEntryCapacity_ * 2u : 8u;
  while (capacity < needed) capacity *= 2u;
  capacity = std::min(capacity, MAX_DESCENDANT_RULES);
  auto grown = makeUniqueNoThrow<DescendantEntry[]>(capacity);
  if (!grown) {
    LOG_ERR("CSS", "OOM: descendant selector index (%zu entries)", capacity);
    return PoolResult::OutOfMemory;
  }
  if (descendantEntryCount_ > 0) {
    memcpy(grown.get(), descendantEntries_.get(), descendantEntryCount_ * sizeof(DescendantEntry));
  }
  descendantEntries_ = std::move(grown);
  descendantEntryCapacity_ = static_cast<uint16_t>(capacity);
  return PoolResult::Ready;
}

CssParser::PoolResult CssParser::ensureSelectorPoolCapacity(const size_t needed) {
  if (needed <= selectorPoolCapacity_) return PoolResult::Ready;
  if (needed > SELECTOR_POOL_CAP) return PoolResult::Limit;

  size_t capacity = selectorPoolCapacity_ ? selectorPoolCapacity_ * 2u : 4096u;
  while (capacity < needed) capacity *= 2u;
  capacity = std::min(capacity, SELECTOR_POOL_CAP);
  auto grown = makeUniqueNoThrow<char[]>(capacity);
  if (!grown) {
    LOG_ERR("CSS", "OOM: selector pool (%zu bytes)", capacity);
    return PoolResult::OutOfMemory;
  }
  if (selectorPoolSize_ > 0) memcpy(grown.get(), selectorPool_.get(), selectorPoolSize_);
  selectorPool_ = std::move(grown);
  selectorPoolCapacity_ = static_cast<uint32_t>(capacity);
  return PoolResult::Ready;
}

CssParser::PoolResult CssParser::ensureStyleCapacity(const size_t needed) {
  if (needed <= styleCapacity_) return PoolResult::Ready;
  if (needed > MAX_UNIQUE_STYLES) return PoolResult::Limit;

  size_t capacity = styleCapacity_ ? styleCapacity_ * 2u : 16u;
  while (capacity < needed) capacity *= 2u;
  capacity = std::min(capacity, MAX_UNIQUE_STYLES);
  auto grownStyles = makeUniqueNoThrow<CssStyle[]>(capacity);
  if (!grownStyles) {
    LOG_ERR("CSS", "OOM: style pool (%zu styles)", capacity);
    return PoolResult::OutOfMemory;
  }
  for (size_t i = 0; i < styleCount_; ++i) grownStyles[i] = stylePool_[i];
  stylePool_ = std::move(grownStyles);
  styleCapacity_ = static_cast<uint16_t>(capacity);
  return PoolResult::Ready;
}

CssParser::PoolResult CssParser::ensurePropertyOrderOverrideCapacity(const size_t needed) {
  if (needed <= propertyOrderOverrideCapacity_) return PoolResult::Ready;
  if (needed > MAX_PROPERTY_ORDER_OVERRIDES) return PoolResult::Limit;

  size_t capacity = propertyOrderOverrideCapacity_ ? propertyOrderOverrideCapacity_ * 2u : 32u;
  while (capacity < needed) capacity *= 2u;
  capacity = std::min(capacity, MAX_PROPERTY_ORDER_OVERRIDES);
  auto grown = makeUniqueNoThrow<PropertyOrderOverride[]>(capacity);
  if (!grown) {
    LOG_ERR("CSS", "OOM: property cascade metadata (%zu entries)", capacity);
    return PoolResult::OutOfMemory;
  }
  if (propertyOrderOverrideCount_ > 0) {
    memcpy(grown.get(), propertyOrderOverrides_.get(),
           static_cast<size_t>(propertyOrderOverrideCount_) * sizeof(PropertyOrderOverride));
  }
  propertyOrderOverrides_ = std::move(grown);
  propertyOrderOverrideCapacity_ = static_cast<uint16_t>(capacity);
  return PoolResult::Ready;
}

CssParser::PoolResult CssParser::recordPropertyOrders(const uint32_t selectorOffset, const bool descendant,
                                                      const CssStyle& style, const uint16_t sourceOrder,
                                                      const CssStyle* existing) {
  const auto declarationWins = [&](const uint8_t property) {
    return !existing || !styleDefinesProperty(*existing, property) || style.isImportant(property) ||
           !existing->isImportant(property);
  };
  size_t missing = 0;
  for (uint8_t property = 0; property < CSS_PROPERTY_COUNT; ++property) {
    if (!styleDefinesProperty(style, property) || !declarationWins(property)) continue;
    bool found = false;
    for (uint16_t i = 0; i < propertyOrderOverrideCount_; ++i) {
      const PropertyOrderOverride& entry = propertyOrderOverrides_[i];
      if (entry.selectorOffset == selectorOffset && entry.descendant == descendant && entry.property == property) {
        found = true;
        break;
      }
    }
    if (!found) ++missing;
  }

  const PoolResult capacityResult =
      ensurePropertyOrderOverrideCapacity(static_cast<size_t>(propertyOrderOverrideCount_) + missing);
  if (capacityResult != PoolResult::Ready) return capacityResult;

  for (uint8_t property = 0; property < CSS_PROPERTY_COUNT; ++property) {
    if (!styleDefinesProperty(style, property) || !declarationWins(property)) continue;
    bool found = false;
    for (uint16_t i = 0; i < propertyOrderOverrideCount_; ++i) {
      PropertyOrderOverride& entry = propertyOrderOverrides_[i];
      if (entry.selectorOffset == selectorOffset && entry.descendant == descendant && entry.property == property) {
        entry.sourceOrder = sourceOrder;
        found = true;
        break;
      }
    }
    if (!found) {
      propertyOrderOverrides_[propertyOrderOverrideCount_++] = {selectorOffset, sourceOrder, property,
                                                                static_cast<uint8_t>(descendant)};
    }
  }
  return PoolResult::Ready;
}

CssParser::PoolResult CssParser::recordPropertyOrder(const uint32_t selectorOffset, const bool descendant,
                                                     const uint8_t property, const uint16_t sourceOrder) {
  if (property >= CSS_PROPERTY_COUNT) return PoolResult::Limit;
  for (uint16_t i = 0; i < propertyOrderOverrideCount_; ++i) {
    PropertyOrderOverride& entry = propertyOrderOverrides_[i];
    if (entry.selectorOffset == selectorOffset && entry.descendant == descendant && entry.property == property) {
      entry.sourceOrder = sourceOrder;
      return PoolResult::Ready;
    }
  }
  const PoolResult capacityResult =
      ensurePropertyOrderOverrideCapacity(static_cast<size_t>(propertyOrderOverrideCount_) + 1);
  if (capacityResult != PoolResult::Ready) return capacityResult;
  propertyOrderOverrides_[propertyOrderOverrideCount_++] = {selectorOffset, sourceOrder, property,
                                                            static_cast<uint8_t>(descendant)};
  return PoolResult::Ready;
}

uint16_t CssParser::propertySourceOrder(const uint32_t selectorOffset, const bool descendant, const uint8_t property,
                                        const uint16_t fallback) const {
  for (uint16_t i = 0; i < propertyOrderOverrideCount_; ++i) {
    const PropertyOrderOverride& entry = propertyOrderOverrides_[i];
    if (entry.selectorOffset == selectorOffset && entry.descendant == descendant && entry.property == property) {
      return entry.sourceOrder;
    }
  }
  return fallback;
}

CssParser::PoolResult CssParser::internStyle(const CssStyle& style, uint16_t& indexOut) {
  uint8_t wire[STYLE_WIRE_BYTES];
  encodeStyleWire(style, wire);
  for (uint16_t i = 0; i < styleCount_; ++i) {
    uint8_t existingWire[STYLE_WIRE_BYTES];
    encodeStyleWire(stylePool_[i], existingWire);
    if (memcmp(existingWire, wire, STYLE_WIRE_BYTES) == 0) {
      indexOut = i;
      return PoolResult::Ready;
    }
  }

  const PoolResult capacityResult = ensureStyleCapacity(static_cast<size_t>(styleCount_) + 1);
  if (capacityResult != PoolResult::Ready) return capacityResult;
  stylePool_[styleCount_] = style;
  indexOut = styleCount_++;
  return PoolResult::Ready;
}

CssParser::RuleInsertResult CssParser::insertOrMerge(const std::string_view selector, const CssStyle& style,
                                                     const uint16_t sourceOrder) {
  bool exact = false;
  const size_t position = lowerBound(selector, {}, {}, exact);
  if (exact) {
    const uint16_t currentStyleIndex = entries_[position].styleIndex;
    CssStyle merged = stylePool_[currentStyleIndex];
    merged.applyOver(style);

    bool styleIsShared = false;
    for (uint16_t i = 0; i < entryCount_; ++i) {
      if (i != position && entries_[i].styleIndex == currentStyleIndex) {
        styleIsShared = true;
        break;
      }
    }
    for (uint16_t i = 0; !styleIsShared && i < descendantEntryCount_; ++i) {
      styleIsShared = descendantEntries_[i].styleIndex == currentStyleIndex;
    }
    uint16_t styleIndex = currentStyleIndex;
    if (styleIsShared) {
      const PoolResult styleResult = internStyle(merged, styleIndex);
      if (styleResult == PoolResult::Limit) return RuleInsertResult::Limit;
      if (styleResult == PoolResult::OutOfMemory) return RuleInsertResult::OutOfMemory;
    }
    const PoolResult cascadeResult =
        recordPropertyOrders(entries_[position].offset, false, style, sourceOrder, &stylePool_[currentStyleIndex]);
    if (cascadeResult == PoolResult::Limit) return RuleInsertResult::Limit;
    if (cascadeResult == PoolResult::OutOfMemory) return RuleInsertResult::OutOfMemory;
    if (styleIsShared) {
      entries_[position].styleIndex = styleIndex;
    } else {
      stylePool_[currentStyleIndex] = merged;
    }
    return RuleInsertResult::Merged;
  }

  if (ruleCount() >= MAX_RULES) return RuleInsertResult::Limit;
  const PoolResult entryResult = ensureEntryCapacity(static_cast<size_t>(entryCount_) + 1);
  if (entryResult == PoolResult::Limit) return RuleInsertResult::Limit;
  if (entryResult == PoolResult::OutOfMemory) return RuleInsertResult::OutOfMemory;

  const size_t requiredSelectorBytes = static_cast<size_t>(selectorPoolSize_) + selector.size();
  const PoolResult selectorResult = ensureSelectorPoolCapacity(requiredSelectorBytes);
  if (selectorResult == PoolResult::Limit) return RuleInsertResult::Limit;
  if (selectorResult == PoolResult::OutOfMemory) return RuleInsertResult::OutOfMemory;

  uint16_t styleIndex = 0;
  const PoolResult styleResult = internStyle(style, styleIndex);
  if (styleResult == PoolResult::Limit) return RuleInsertResult::Limit;
  if (styleResult == PoolResult::OutOfMemory) return RuleInsertResult::OutOfMemory;

  const uint32_t selectorOffset = selectorPoolSize_;
  char* destination = selectorPool_.get() + selectorOffset;
  for (const char c : selector) *destination++ = asciiToLower(c);
  selectorPoolSize_ = static_cast<uint32_t>(requiredSelectorBytes);

  SelectorEntry* entries = entries_.get();
  memmove(entries + position + 1, entries + position, (entryCount_ - position) * sizeof(SelectorEntry));
  entries[position] = {selectorOffset, styleIndex, static_cast<uint16_t>(selector.size()),
                       selectorSpecificity(selector), sourceOrder};
  ++entryCount_;
  return RuleInsertResult::Inserted;
}

CssParser::RuleInsertResult CssParser::insertOrMergeDescendant(const std::string_view ancestor,
                                                               const std::string_view subject, const CssStyle& style,
                                                               const uint16_t sourceOrder) {
  for (uint16_t i = 0; i < descendantEntryCount_; ++i) {
    const DescendantEntry& entry = descendantEntries_[i];
    const SelectorEntry ancestorEntry = {entry.ancestorOffset, entry.styleIndex, entry.ancestorLength, 0, 0};
    const SelectorEntry subjectEntry = {entry.subjectOffset, entry.styleIndex, entry.subjectLength, 0, 0};
    if (compareEntryToPieces(ancestorEntry, ancestor, {}, {}) != 0 ||
        compareEntryToPieces(subjectEntry, subject, {}, {}) != 0) {
      continue;
    }

    const uint16_t currentStyleIndex = entry.styleIndex;
    CssStyle merged = stylePool_[currentStyleIndex];
    merged.applyOver(style);

    bool styleIsShared = false;
    for (uint16_t simple = 0; simple < entryCount_; ++simple) {
      if (entries_[simple].styleIndex == currentStyleIndex) {
        styleIsShared = true;
        break;
      }
    }
    for (uint16_t descendant = 0; !styleIsShared && descendant < descendantEntryCount_; ++descendant) {
      if (descendant != i && descendantEntries_[descendant].styleIndex == currentStyleIndex) {
        styleIsShared = true;
      }
    }
    uint16_t styleIndex = currentStyleIndex;
    if (styleIsShared) {
      const PoolResult styleResult = internStyle(merged, styleIndex);
      if (styleResult == PoolResult::Limit) return RuleInsertResult::Limit;
      if (styleResult == PoolResult::OutOfMemory) return RuleInsertResult::OutOfMemory;
    }
    const PoolResult cascadeResult =
        recordPropertyOrders(entry.ancestorOffset, true, style, sourceOrder, &stylePool_[currentStyleIndex]);
    if (cascadeResult == PoolResult::Limit) return RuleInsertResult::Limit;
    if (cascadeResult == PoolResult::OutOfMemory) return RuleInsertResult::OutOfMemory;
    if (styleIsShared) {
      descendantEntries_[i].styleIndex = styleIndex;
    } else {
      stylePool_[currentStyleIndex] = merged;
    }
    return RuleInsertResult::Merged;
  }

  if (ruleCount() >= MAX_RULES) return RuleInsertResult::Limit;
  const PoolResult entryResult = ensureDescendantEntryCapacity(static_cast<size_t>(descendantEntryCount_) + 1);
  if (entryResult == PoolResult::Limit) return RuleInsertResult::Limit;
  if (entryResult == PoolResult::OutOfMemory) return RuleInsertResult::OutOfMemory;

  const size_t requiredSelectorBytes = static_cast<size_t>(selectorPoolSize_) + ancestor.size() + subject.size();
  const PoolResult selectorResult = ensureSelectorPoolCapacity(requiredSelectorBytes);
  if (selectorResult == PoolResult::Limit) return RuleInsertResult::Limit;
  if (selectorResult == PoolResult::OutOfMemory) return RuleInsertResult::OutOfMemory;

  uint16_t styleIndex = 0;
  const PoolResult styleResult = internStyle(style, styleIndex);
  if (styleResult == PoolResult::Limit) return RuleInsertResult::Limit;
  if (styleResult == PoolResult::OutOfMemory) return RuleInsertResult::OutOfMemory;

  const uint32_t ancestorOffset = selectorPoolSize_;
  char* destination = selectorPool_.get() + ancestorOffset;
  for (const char c : ancestor) *destination++ = asciiToLower(c);
  const uint32_t subjectOffset = static_cast<uint32_t>(ancestorOffset + ancestor.size());
  for (const char c : subject) *destination++ = asciiToLower(c);
  selectorPoolSize_ = static_cast<uint32_t>(requiredSelectorBytes);

  descendantEntries_[descendantEntryCount_++] = {ancestorOffset,
                                                 subjectOffset,
                                                 styleIndex,
                                                 static_cast<uint16_t>(ancestor.size()),
                                                 static_cast<uint16_t>(subject.size()),
                                                 descendantSpecificity(ancestor, subject),
                                                 sourceOrder};
  return RuleInsertResult::Inserted;
}

// Property value interpreters

CssTextAlign CssParser::interpretAlignment(std::string_view val) {
  val = trimCssWhitespace(val);

  if (iequalsAscii(val, "left") || iequalsAscii(val, "start")) return CssTextAlign::Left;
  if (iequalsAscii(val, "right") || iequalsAscii(val, "end")) return CssTextAlign::Right;
  if (iequalsAscii(val, "center")) return CssTextAlign::Center;
  if (iequalsAscii(val, "justify")) return CssTextAlign::Justify;

  return CssTextAlign::Left;
}

CssFontStyle CssParser::interpretFontStyle(std::string_view val) {
  val = trimCssWhitespace(val);

  if (iequalsAscii(val, "italic") || iequalsAscii(val, "oblique")) return CssFontStyle::Italic;
  return CssFontStyle::Normal;
}

CssFontWeight CssParser::interpretFontWeight(std::string_view val) {
  val = trimCssWhitespace(val);

  // Named values
  if (iequalsAscii(val, "bold") || iequalsAscii(val, "bolder")) return CssFontWeight::Bold;
  if (iequalsAscii(val, "normal") || iequalsAscii(val, "lighter")) return CssFontWeight::Normal;

  // Numeric values: 100-900
  // CSS spec: 400 = normal, 700 = bold
  // We use: 0-400 = normal, 700+ = bold, 500-600 = normal (conservative)
  long numericWeight = 0;
  if (tryParseNumber(val, numericWeight)) {
    return numericWeight >= 700 ? CssFontWeight::Bold : CssFontWeight::Normal;
  }
  return CssFontWeight::Normal;
}

CssFontVariantCaps CssParser::interpretFontVariantCaps(std::string_view val) {
  val = trimCssWhitespace(stripTrailingImportant(val));
  CssFontVariantCaps result = CssFontVariantCaps::Normal;
  forEachDelimitedToken(val, isCssWhitespace, [&](const std::string_view token) {
    if (iequalsAscii(token, "small-caps")) {
      result = CssFontVariantCaps::SmallCaps;
    } else if (iequalsAscii(token, "normal")) {
      result = CssFontVariantCaps::Normal;
    }
  });
  return result;
}

CssTextDecoration CssParser::interpretDecoration(std::string_view val) {
  // text-decoration can have multiple space-separated values. Compare whole tokens
  // so malformed values like "notunderline" do not accidentally enable a line.
  CssTextDecoration result = CssTextDecoration::None;
  bool explicitNone = false;
  forEachDelimitedToken(val, isCssWhitespace, [&](const std::string_view token) {
    if (iequalsAscii(token, "none")) {
      explicitNone = true;
    } else if (iequalsAscii(token, "underline")) {
      result = result | CssTextDecoration::Underline;
    } else if (iequalsAscii(token, "line-through")) {
      result = result | CssTextDecoration::LineThrough;
    }
  });
  return explicitNone ? CssTextDecoration::None : result;
}

CssLength CssParser::interpretLength(std::string_view val) {
  CssLength result;
  tryInterpretLength(val, result);
  return result;
}

bool CssParser::tryInterpretLength(std::string_view val, CssLength& out) {
  val = trimCssWhitespace(val);
  if (val.empty()) {
    out = CssLength{};
    return false;
  }

  size_t unitStart = val.size();
  for (size_t i = 0; i < val.size(); ++i) {
    const char c = val[i];
    if (!std::isdigit(c) && c != '.' && c != '-' && c != '+') {
      unitStart = i;
      break;
    }
  }

  float numericValue;
  if (!tryParseNumber(val.substr(0, unitStart), numericValue)) {
    out = CssLength{};
    return false;  // No number parsed (e.g. auto, inherit, initial)
  }

  const std::string_view unitPart = val.substr(unitStart);
  auto unit = CssUnit::Pixels;
  if (iequalsAscii(unitPart, "em")) {
    unit = CssUnit::Em;
  } else if (iequalsAscii(unitPart, "rem")) {
    unit = CssUnit::Rem;
  } else if (iequalsAscii(unitPart, "pt")) {
    unit = CssUnit::Points;
  } else if (iequalsAscii(unitPart, "px")) {
    unit = CssUnit::Pixels;
  } else if (unitPart == "%") {
    unit = CssUnit::Percent;
  } else if (!unitPart.empty() || numericValue != 0.0f) {
    out = CssLength{};
    return false;
  }

  out = CssLength{numericValue, unit};
  return true;
}

// Declaration parsing

void CssParser::parseDeclarationIntoStyle(std::string_view decl, CssStyle& style) {
  const size_t colonPos = decl.find(':');
  if (colonPos == std::string_view::npos || colonPos == 0) return;

  const std::string_view name = trimCssWhitespace(decl.substr(0, colonPos));
  bool important = false;
  const std::string_view value =
      trimCssWhitespace(stripTrailingImportant(trimCssWhitespace(decl.substr(colonPos + 1)), &important));

  if (name.empty() || value.empty()) return;

  CssStyle parsed;
  const auto tryMarginLength = [](const std::string_view token, CssLength& length) {
    if (iequalsAscii(trimCssWhitespace(token), "auto")) {
      length = CssLength{};
      return true;
    }
    return CssParser::tryInterpretLength(token, length);
  };

  if (iequalsAscii(name, "text-align")) {
    if (iequalsAscii(value, "left") || iequalsAscii(value, "right") || iequalsAscii(value, "center") ||
        iequalsAscii(value, "justify") || iequalsAscii(value, "start") || iequalsAscii(value, "end")) {
      parsed.textAlign = interpretAlignment(value);
      parsed.defined.textAlign = 1;
    }
  } else if (iequalsAscii(name, "font-style")) {
    if (iequalsAscii(value, "normal") || iequalsAscii(value, "italic") || iequalsAscii(value, "oblique")) {
      parsed.fontStyle = interpretFontStyle(value);
      parsed.defined.fontStyle = 1;
    }
  } else if (iequalsAscii(name, "font-weight")) {
    long numericWeight = 0;
    if (iequalsAscii(value, "normal") || iequalsAscii(value, "bold") || iequalsAscii(value, "bolder") ||
        iequalsAscii(value, "lighter") ||
        (tryParseNumber(value, numericWeight) && numericWeight >= 1 && numericWeight <= 1000)) {
      parsed.fontWeight = interpretFontWeight(value);
      parsed.defined.fontWeight = 1;
    }
  } else if (iequalsAscii(name, "font-variant") || iequalsAscii(name, "font-variant-caps")) {
    bool supported = false;
    forEachDelimitedToken(value, isCssWhitespace, [&](const std::string_view token) {
      supported = supported || iequalsAscii(token, "small-caps") || iequalsAscii(token, "normal");
    });
    if (supported) {
      parsed.fontVariantCaps = interpretFontVariantCaps(value);
      parsed.defined.fontVariantCaps = 1;
    }
  } else if (iequalsAscii(name, "text-decoration") || iequalsAscii(name, "text-decoration-line")) {
    bool supported = false;
    forEachDelimitedToken(value, isCssWhitespace, [&](const std::string_view token) {
      supported = supported || iequalsAscii(token, "none") || iequalsAscii(token, "underline") ||
                  iequalsAscii(token, "line-through");
    });
    if (supported) {
      parsed.textDecoration = interpretDecoration(value);
      parsed.defined.textDecoration = 1;
    }
  } else if (iequalsAscii(name, "text-indent")) {
    if (tryInterpretLength(value, parsed.textIndent)) parsed.defined.textIndent = 1;
  } else if (iequalsAscii(name, "margin-top")) {
    if (tryMarginLength(value, parsed.marginTop)) parsed.defined.marginTop = 1;
  } else if (iequalsAscii(name, "margin-bottom")) {
    if (tryMarginLength(value, parsed.marginBottom)) parsed.defined.marginBottom = 1;
  } else if (iequalsAscii(name, "margin-left")) {
    if (tryMarginLength(value, parsed.marginLeft)) parsed.defined.marginLeft = 1;
  } else if (iequalsAscii(name, "margin-right")) {
    if (tryMarginLength(value, parsed.marginRight)) parsed.defined.marginRight = 1;
  } else if (iequalsAscii(name, "margin")) {
    std::string_view margins[4];
    const size_t count = collectEdgeValueTokens(value, margins);
    CssLength values[4];
    bool valid = count > 0;
    for (size_t i = 0; i < count; ++i) valid = valid && tryMarginLength(margins[i], values[i]);
    if (valid) {
      parsed.marginTop = values[0];
      parsed.marginRight = count >= 2 ? values[1] : parsed.marginTop;
      parsed.marginBottom = count >= 3 ? values[2] : parsed.marginTop;
      parsed.marginLeft = count >= 4 ? values[3] : parsed.marginRight;
      parsed.defined.marginTop = parsed.defined.marginRight = parsed.defined.marginBottom = parsed.defined.marginLeft =
          1;
    }
  } else if (iequalsAscii(name, "padding-top")) {
    if (tryInterpretLength(value, parsed.paddingTop)) parsed.defined.paddingTop = 1;
  } else if (iequalsAscii(name, "padding-bottom")) {
    if (tryInterpretLength(value, parsed.paddingBottom)) parsed.defined.paddingBottom = 1;
  } else if (iequalsAscii(name, "padding-left")) {
    if (tryInterpretLength(value, parsed.paddingLeft)) parsed.defined.paddingLeft = 1;
  } else if (iequalsAscii(name, "padding-right")) {
    if (tryInterpretLength(value, parsed.paddingRight)) parsed.defined.paddingRight = 1;
  } else if (iequalsAscii(name, "padding")) {
    std::string_view paddings[4];
    const size_t count = collectEdgeValueTokens(value, paddings);
    CssLength values[4];
    bool valid = count > 0;
    for (size_t i = 0; i < count; ++i) valid = valid && tryInterpretLength(paddings[i], values[i]);
    if (valid) {
      parsed.paddingTop = values[0];
      parsed.paddingRight = count >= 2 ? values[1] : parsed.paddingTop;
      parsed.paddingBottom = count >= 3 ? values[2] : parsed.paddingTop;
      parsed.paddingLeft = count >= 4 ? values[3] : parsed.paddingRight;
      parsed.defined.paddingTop = parsed.defined.paddingRight = parsed.defined.paddingBottom =
          parsed.defined.paddingLeft = 1;
    }
  } else if (iequalsAscii(name, "height")) {
    CssLength len;
    if (tryInterpretLength(value, len)) {
      parsed.imageHeight = len;
      parsed.defined.imageHeight = 1;
    }
  } else if (iequalsAscii(name, "width")) {
    CssLength len;
    if (tryInterpretLength(value, len)) {
      parsed.imageWidth = len;
      parsed.defined.imageWidth = 1;
    }
  } else if (iequalsAscii(name, "display")) {
    if (iequalsAscii(value, "none")) {
      parsed.display = CssDisplay::None;
      parsed.defined.display = 1;
    } else if (iequalsAscii(value, "block") || iequalsAscii(value, "inline") || iequalsAscii(value, "inline-block") ||
               iequalsAscii(value, "list-item") || iequalsAscii(value, "table") || iequalsAscii(value, "table-row") ||
               iequalsAscii(value, "table-cell")) {
      parsed.display = CssDisplay::Block;
      parsed.defined.display = 1;
    }
  } else if (iequalsAscii(name, "page-break-before") || iequalsAscii(name, "break-before")) {
    if (iequalsAscii(value, "always") || iequalsAscii(value, "page") || iequalsAscii(value, "left") ||
        iequalsAscii(value, "right") || iequalsAscii(value, "recto") || iequalsAscii(value, "verso") ||
        iequalsAscii(value, "auto") || iequalsAscii(value, "avoid") || iequalsAscii(value, "avoid-page")) {
      parsed.pageBreakBefore =
          iequalsAscii(value, "auto") || iequalsAscii(value, "avoid") || iequalsAscii(value, "avoid-page")
              ? CssPageBreak::Auto
              : CssPageBreak::Always;
      parsed.defined.pageBreakBefore = 1;
    }
  } else if (iequalsAscii(name, "page-break-after") || iequalsAscii(name, "break-after")) {
    if (iequalsAscii(value, "always") || iequalsAscii(value, "page") || iequalsAscii(value, "left") ||
        iequalsAscii(value, "right") || iequalsAscii(value, "recto") || iequalsAscii(value, "verso") ||
        iequalsAscii(value, "auto") || iequalsAscii(value, "avoid") || iequalsAscii(value, "avoid-page")) {
      parsed.pageBreakAfter =
          iequalsAscii(value, "auto") || iequalsAscii(value, "avoid") || iequalsAscii(value, "avoid-page")
              ? CssPageBreak::Auto
              : CssPageBreak::Always;
      parsed.defined.pageBreakAfter = 1;
    }
  } else if (iequalsAscii(name, "direction")) {
    if (iequalsAscii(value, "rtl")) {
      parsed.direction = CssTextDirection::Rtl;
      parsed.defined.direction = 1;
    } else if (iequalsAscii(value, "ltr")) {
      parsed.direction = CssTextDirection::Ltr;
      parsed.defined.direction = 1;
    }
  } else if (iequalsAscii(name, "vertical-align")) {
    if (iequalsAscii(value, "super")) {
      parsed.verticalAlign = CssVerticalAlign::Super;
      parsed.defined.verticalAlign = 1;
    } else if (iequalsAscii(value, "sub")) {
      parsed.verticalAlign = CssVerticalAlign::Sub;
      parsed.defined.verticalAlign = 1;
    } else if (iequalsAscii(value, "baseline")) {
      parsed.verticalAlign = CssVerticalAlign::Baseline;
      parsed.defined.verticalAlign = 1;
    }
  }

  if (!parsed.defined.anySet()) return;
  if (important) {
    for (uint8_t property = 0; property < CSS_PROPERTY_COUNT; ++property) {
      if (styleDefinesProperty(parsed, property)) parsed.setImportant(property, true);
    }
  }
  style.applyOver(parsed);
}

CssStyle CssParser::parseDeclarations(std::string_view declBlock) {
  CssStyle style;

  size_t start = 0;
  for (size_t i = 0; i <= declBlock.size(); ++i) {
    if (i == declBlock.size() || declBlock[i] == ';') {
      if (i > start) {
        parseDeclarationIntoStyle(declBlock.substr(start, i - start), style);
      }
      start = i + 1;
    }
  }

  return style;
}

// Rule processing

void CssParser::processRuleBlockWithStyle(std::string_view selectorGroup, const CssStyle& style) {
  // Skip rules that don't define any supported properties to save RAM.
  if (!style.defined.anySet()) {
    return;
  }

  // Every selector in a comma-separated group belongs to the same CSS rule
  // and therefore has the same source order. Saturation is deterministic and
  // only reachable after far more rule blocks than the bounded store retains.
  const uint16_t sourceOrder = nextSourceOrder_;
  if (nextSourceOrder_ < UINT16_MAX) ++nextSourceOrder_;

  // Walk comma-separated selectors in place. The bounded store reports every
  // capacity or allocation failure without crossing a throwing STL boundary.
  forEachDelimitedToken(
      selectorGroup, [](char c) { return c == ','; },
      [&](std::string_view sel) {
        if (sel.size() > MAX_SELECTOR_LENGTH) {
          LOG_DBG("CSS", "Selector too long (%zu > %zu), skipping", sel.size(), MAX_SELECTOR_LENGTH);
          return;
        }

        // Support a bounded two-part descendant selector in addition to
        // `tag`, `.class`, or `tag.class`. Reject anything containing a
        // character that introduces unsupported syntax:
        //   '+'  adjacent sibling combinator
        //   '>'  child combinator
        //   '['  attribute selector
        //   ':'  pseudo class/element
        //   '#'  ID selector
        //   '~'  general sibling combinator
        //   '*'  wildcard
        constexpr std::string_view kUnsupportedSelectorChars = "+>[:#~*";
        if (sel.find_first_of(kUnsupportedSelectorChars) != std::string_view::npos) return;

        std::string_view parts[3];
        size_t partCount = 0;
        forEachDelimitedToken(sel, isCssWhitespace, [&](const std::string_view part) {
          if (partCount < std::size(parts)) parts[partCount] = part;
          ++partCount;
        });
        if (partCount == 0 || partCount > 2) return;

        const auto isSimpleSelector = [](const std::string_view selector) {
          if (selector.empty()) return false;
          size_t dotCount = 0;
          for (const char c : selector) {
            if (c == '.') ++dotCount;
          }
          if (dotCount > 1 || selector.back() == '.') return false;
          return selector.front() != '.' || selector.size() > 1;
        };
        if (!isSimpleSelector(parts[0]) || (partCount == 2 && !isSimpleSelector(parts[1]))) return;

        const bool descendant = partCount == 2;
        if (descendant && parts[0].size() + 1 + parts[1].size() > MAX_SELECTOR_LENGTH) return;
        if (descendant && descendantEntryCount_ >= MAX_DESCENDANT_RULES) {
          bool existing = false;
          for (uint16_t i = 0; i < descendantEntryCount_ && !existing; ++i) {
            const DescendantEntry& entry = descendantEntries_[i];
            const SelectorEntry ancestorEntry = {entry.ancestorOffset, entry.styleIndex, entry.ancestorLength, 0, 0};
            const SelectorEntry subjectEntry = {entry.subjectOffset, entry.styleIndex, entry.subjectLength, 0, 0};
            existing = compareEntryToPieces(ancestorEntry, parts[0], {}, {}) == 0 &&
                       compareEntryToPieces(subjectEntry, parts[1], {}, {}) == 0;
          }
          if (!existing) {
            descendantRulesTruncated_ = true;
            return;
          }
        }

        if (ruleGrowthStopped_) {
          // Continue the cascade for stored selectors without retrying failed
          // allocations for new rules.
          if (!descendant) {
            bool exact = false;
            const size_t matchingIndex = lowerBound(parts[0], {}, {}, exact);
            if (!exact || matchingIndex >= entryCount_) return;
          } else {
            bool exact = false;
            for (uint16_t i = 0; i < descendantEntryCount_ && !exact; ++i) {
              const DescendantEntry& entry = descendantEntries_[i];
              const SelectorEntry ancestorEntry = {entry.ancestorOffset, entry.styleIndex, entry.ancestorLength, 0, 0};
              const SelectorEntry subjectEntry = {entry.subjectOffset, entry.styleIndex, entry.subjectLength, 0, 0};
              exact = compareEntryToPieces(ancestorEntry, parts[0], {}, {}) == 0 &&
                      compareEntryToPieces(subjectEntry, parts[1], {}, {}) == 0;
            }
            if (!exact) return;
          }
        }
        const RuleInsertResult result = descendant ? insertOrMergeDescendant(parts[0], parts[1], style, sourceOrder)
                                                   : insertOrMerge(parts[0], style, sourceOrder);
        if (result == RuleInsertResult::Limit) {
          LOG_ERR("CSS", "CSS rule store limit reached at %zu rules", ruleCount());
          ruleGrowthStopped_ = true;
        } else if (result == RuleInsertResult::OutOfMemory) {
          LOG_ERR("CSS", "OOM while growing CSS rule store at %zu rules", ruleCount());
          ruleGrowthStopped_ = true;
        }
      });
}

// Main parsing entry point

CssParser::ParseResult CssParser::loadFromStream(HalFile& source) {
  if (!source) {
    LOG_ERR("CSS", "Cannot read from invalid file");
    return ParseResult::Error;
  }

  size_t totalRead = 0;

  // Use stack-allocated buffers for parsing to avoid heap reallocations
  StackBuffer selector;
  StackBuffer declBuffer;

  bool inComment = false;
  bool maybeSlash = false;
  bool prevStar = false;

  bool inAtRule = false;
  int atDepth = 0;

  int bodyDepth = 0;
  bool skippingRule = false;
  bool selectorTruncated = false;
  bool declarationTruncated = false;
  bool inputTruncated = false;
  CssStyle currentStyle;

  auto handleChar = [&](const char c) {
    if (inAtRule) {
      if (c == '{') {
        ++atDepth;
      } else if (c == '}') {
        if (atDepth > 0) --atDepth;
        if (atDepth == 0) inAtRule = false;
      } else if (c == ';' && atDepth == 0) {
        inAtRule = false;
      }
      return;
    }

    if (bodyDepth == 0) {
      if (selector.empty() && isCssWhitespace(c)) {
        return;
      }
      if (c == '@' && selector.empty()) {
        inAtRule = true;
        atDepth = 0;
        return;
      }
      if (c == '{') {
        bodyDepth = 1;
        currentStyle = CssStyle{};
        declBuffer.clear();
        skippingRule = selectorTruncated || selector.size() > MAX_SELECTOR_LENGTH * 4;
        return;
      }
      if (!selector.push_back(c)) {
        selectorTruncated = true;
        inputTruncated = true;
      }
      return;
    }

    // bodyDepth > 0
    if (c == '{') {
      ++bodyDepth;
      return;
    }
    if (c == '}') {
      --bodyDepth;
      if (bodyDepth == 0) {
        if (!skippingRule && !declarationTruncated && !declBuffer.empty()) {
          parseDeclarationIntoStyle(declBuffer, currentStyle);
        }
        if (!skippingRule) {
          processRuleBlockWithStyle(selector, currentStyle);
        }
        selector.clear();
        declBuffer.clear();
        skippingRule = false;
        selectorTruncated = false;
        declarationTruncated = false;
        return;
      }
      return;
    }
    if (bodyDepth > 1) {
      return;
    }
    if (!skippingRule) {
      if (c == ';') {
        if (!declarationTruncated && !declBuffer.empty()) {
          parseDeclarationIntoStyle(declBuffer, currentStyle);
        }
        declBuffer.clear();
        declarationTruncated = false;
      } else {
        if (!declBuffer.push_back(c)) {
          declarationTruncated = true;
          inputTruncated = true;
        }
      }
    }
  };

  char buffer[READ_BUFFER_SIZE];
  while (source.available()) {
    int bytesRead = source.read(buffer, sizeof(buffer));
    if (bytesRead <= 0) break;

    totalRead += static_cast<size_t>(bytesRead);

    for (int i = 0; i < bytesRead; ++i) {
      const char c = buffer[i];

      if (inComment) {
        if (prevStar && c == '/') {
          inComment = false;
          prevStar = false;
          continue;
        }
        prevStar = c == '*';
        continue;
      }

      if (maybeSlash) {
        if (c == '*') {
          inComment = true;
          maybeSlash = false;
          prevStar = false;
          continue;
        }
        handleChar('/');
        maybeSlash = false;
        // fall through to process current char
      }

      if (c == '/') {
        maybeSlash = true;
        continue;
      }

      handleChar(c);
    }
  }

  if (maybeSlash) {
    handleChar('/');
  }

  if (inputTruncated) {
    LOG_ERR("CSS", "CSS input exceeded parser buffer; cache will remain partial");
  }
  const bool incompleteInput = bodyDepth > 0 || inAtRule || inComment || !selector.empty();
  LOG_DBG("CSS", "Parsed %zu rules from %zu bytes", ruleCount(), totalRead);
  return ruleGrowthStopped_ || descendantRulesTruncated_ || inputTruncated || incompleteInput ? ParseResult::Partial
                                                                                              : ParseResult::Complete;
}

// Style resolution

bool CssParser::selectorMatchesElement(const std::string_view selector, const std::string_view tagName,
                                       const std::string_view classAttr) {
  if (selector.empty()) return false;
  const auto equalsLowerSelector = [](const std::string_view value, const std::string_view lowerSelector) {
    return value.size() == lowerSelector.size() &&
           std::equal(
               value.begin(), value.end(), lowerSelector.begin(), lowerSelector.end(),
               [](const char valueByte, const char selectorByte) { return asciiToLower(valueByte) == selectorByte; });
  };

  const size_t dot = selector.find('.');
  if (dot == std::string_view::npos) return equalsLowerSelector(tagName, selector);

  const std::string_view selectorTag = selector.substr(0, dot);
  const std::string_view selectorClass = selector.substr(dot + 1);
  if ((!selectorTag.empty() && !equalsLowerSelector(tagName, selectorTag)) || selectorClass.empty()) return false;

  bool matched = false;
  forEachDelimitedToken(classAttr, isCssWhitespace, [&](const std::string_view cls) {
    matched = matched || equalsLowerSelector(cls, selectorClass);
  });
  return matched;
}

CssParser::DescendantMask CssParser::matchingAncestorMask(const std::string_view tagName,
                                                          const std::string_view classAttr) const {
  DescendantMask mask = 0;
  for (uint16_t i = 0; i < descendantEntryCount_; ++i) {
    if (selectorMatchesElement(descendantAncestorAt(i), tagName, classAttr)) {
      mask |= DescendantMask{1} << i;
    }
  }
  return mask;
}

CssStyle CssParser::resolveStyle(const std::string_view tagName, const std::string_view classAttr,
                                 const DescendantMask activeAncestors) const {
  struct PropertyWinner {
    uint16_t specificity;
    uint16_t sourceOrder;
    bool important;
    bool set;
  };
  std::array<PropertyWinner, CSS_PROPERTY_COUNT> winners{};
  CssStyle result;
  const auto applyRule = [&](const uint16_t styleIndex, const uint32_t selectorOffset, const bool descendant,
                             const uint16_t specificity, const uint16_t baseSourceOrder) {
    const CssStyle& style = stylePool_[styleIndex];
    for (uint8_t property = 0; property < CSS_PROPERTY_COUNT; ++property) {
      if (!styleDefinesProperty(style, property)) continue;
      const uint16_t sourceOrder = propertySourceOrder(selectorOffset, descendant, property, baseSourceOrder);
      PropertyWinner& winner = winners[property];
      const bool important = style.isImportant(property);
      if (!winner.set || important > winner.important ||
          (important == winner.important &&
           (specificity > winner.specificity ||
            (specificity == winner.specificity && sourceOrder >= winner.sourceOrder)))) {
        copyStyleProperty(result, style, property);
        winner = {specificity, sourceOrder, important, true};
      }
    }
  };

  if (const SelectorEntry* entry = findEntry(tagName)) {
    applyRule(entry->styleIndex, entry->offset, false, entry->specificity, entry->sourceOrder);
  }

  // The parser passes a cumulative bit mask, so matching descendants does not
  // retain tag/class strings for every open HTML node.
  if (activeAncestors != 0) {
    for (uint16_t i = 0; i < descendantEntryCount_; ++i) {
      if ((activeAncestors & (DescendantMask{1} << i)) != 0 &&
          selectorMatchesElement(descendantSubjectAt(i), tagName, classAttr)) {
        const DescendantEntry& entry = descendantEntries_[i];
        applyRule(entry.styleIndex, entry.ancestorOffset, true, entry.specificity, entry.sourceOrder);
      }
    }
  }

  if (!classAttr.empty()) {
    // TODO: Support combinations of classes (e.g. .class1.class2).
    forEachDelimitedToken(classAttr, isCssWhitespace, [&](std::string_view cls) {
      if (const SelectorEntry* entry = findEntry(".", cls)) {
        applyRule(entry->styleIndex, entry->offset, false, entry->specificity, entry->sourceOrder);
      }
      if (const SelectorEntry* entry = findEntry(tagName, ".", cls)) {
        applyRule(entry->styleIndex, entry->offset, false, entry->specificity, entry->sourceOrder);
      }
    });
  }

  return result;
}

// Inline style parsing (static - doesn't need rule database)

CssStyle CssParser::parseInlineStyle(std::string_view styleValue) { return parseDeclarations(styleValue); }

// Cache serialization

// Cache file name (version is CssParser::CSS_CACHE_VERSION)
constexpr char rulesCache[] = "/css_rules.cache";
constexpr char rulesCacheTmp[] = "/css_rules.cache.tmp";
constexpr char rulesCacheBackup[] = "/css_rules.cache.bak";
constexpr uint8_t CSS_CACHE_FLAG_PARTIAL = 1 << 0;
constexpr uint8_t CSS_CACHE_KNOWN_FLAGS = CSS_CACHE_FLAG_PARTIAL;

bool CssParser::hasCache() const { return Storage.exists((cachePath + rulesCache).c_str()); }

bool CssParser::restoreCacheBackupIfNeeded() const {
  if (cachePath.empty()) {
    return false;
  }

  const std::string finalPath = cachePath + rulesCache;
  if (Storage.exists(finalPath.c_str())) {
    return true;
  }

  const std::string backupPath = cachePath + rulesCacheBackup;
  if (!Storage.exists(backupPath.c_str())) {
    return false;
  }

  if (!Storage.rename(backupPath.c_str(), finalPath.c_str())) {
    LOG_ERR("CSS", "Failed to restore CSS cache backup");
    return false;
  }

  LOG_DBG("CSS", "Restored CSS cache backup after interrupted replacement");
  return true;
}

void CssParser::deleteCache() const {
  if (hasCache()) Storage.remove((cachePath + rulesCache).c_str());
  Storage.remove((cachePath + rulesCacheTmp).c_str());
  Storage.remove((cachePath + rulesCacheBackup).c_str());
}

CssParser::CacheStatus CssParser::inspectCache() const {
  if (cachePath.empty() || (!hasCache() && !restoreCacheBackupIfNeeded())) {
    return CacheStatus::Missing;
  }

  HalFile file;
  if (!Storage.openFileForRead("CSS", cachePath + rulesCache, file)) {
    return CacheStatus::Invalid;
  }

  uint8_t version = 0;
  uint8_t flags = 0;
  uint16_t ruleCount = 0;
  if (file.read(&version, sizeof(version)) != sizeof(version) || version != CSS_CACHE_VERSION ||
      file.read(&flags, sizeof(flags)) != sizeof(flags) || (flags & ~CSS_CACHE_KNOWN_FLAGS) != 0 ||
      file.read(&ruleCount, sizeof(ruleCount)) != sizeof(ruleCount) || ruleCount > MAX_RULES) {
    return CacheStatus::Invalid;
  }

  const bool partial = (flags & CSS_CACHE_FLAG_PARTIAL) != 0;
  if (!partial) {
    // Complete caches are fully validated while hydrating, avoiding a second
    // payload scan on every EPUB open.
    return CacheStatus::Complete;
  }

  const auto skipBytes = [&file](const size_t byteCount) {
    return static_cast<size_t>(file.available()) >= byteCount && file.seekCur(byteCount);
  };
  size_t selectorBytes = 0;
  size_t propertyOrderOverrides = 0;
  for (uint16_t i = 0; i < ruleCount; ++i) {
    uint16_t selectorLen = 0;
    if (file.read(&selectorLen, sizeof(selectorLen)) != sizeof(selectorLen) || selectorLen == 0 ||
        selectorLen > MAX_SELECTOR_LENGTH) {
      return CacheStatus::Invalid;
    }
    selectorBytes += selectorLen;
    if (selectorBytes > SELECTOR_POOL_CAP ||
        !skipBytes(static_cast<size_t>(selectorLen) + STYLE_WIRE_BYTES + RULE_CASCADE_FIXED_WIRE_BYTES)) {
      return CacheStatus::Invalid;
    }
    uint8_t overrideCount = 0;
    if (file.read(&overrideCount, sizeof(overrideCount)) != sizeof(overrideCount) ||
        overrideCount > CSS_PROPERTY_COUNT) {
      return CacheStatus::Invalid;
    }
    propertyOrderOverrides += overrideCount;
    if (propertyOrderOverrides > MAX_PROPERTY_ORDER_OVERRIDES ||
        !skipBytes(static_cast<size_t>(overrideCount) * PROPERTY_ORDER_WIRE_BYTES)) {
      return CacheStatus::Invalid;
    }
  }

  if (file.available() != 0) {
    return CacheStatus::Invalid;
  }

  return CacheStatus::Partial;
}

bool CssParser::saveToCache(const bool complete) const {
  if (cachePath.empty()) {
    return false;
  }

  const std::string finalPath = cachePath + rulesCache;
  const std::string tmpPath = cachePath + rulesCacheTmp;
  const std::string backupPath = cachePath + rulesCacheBackup;

  Storage.remove(tmpPath.c_str());

  HalFile file;
  if (!Storage.openFileForWrite("CSS", tmpPath, file)) {
    return false;
  }

  bool writeOk = true;
  const auto writeBytes = [&file, &writeOk](const void* data, const size_t size) {
    if (writeOk && size > 0 && file.write(data, size) != size) {
      writeOk = false;
    }
  };
  const auto writeByte = [&writeBytes](const uint8_t value) { writeBytes(&value, sizeof(value)); };
  const auto writeCascade = [this, &writeBytes, &writeByte](const uint32_t selectorOffset, const bool descendant,
                                                            const uint16_t specificity, const uint16_t sourceOrder) {
    writeBytes(&specificity, sizeof(specificity));
    writeBytes(&sourceOrder, sizeof(sourceOrder));
    uint8_t overrideCount = 0;
    for (uint16_t i = 0; i < propertyOrderOverrideCount_; ++i) {
      const PropertyOrderOverride& entry = propertyOrderOverrides_[i];
      if (entry.selectorOffset == selectorOffset && entry.descendant == descendant) ++overrideCount;
    }
    writeByte(overrideCount);
    for (uint16_t i = 0; i < propertyOrderOverrideCount_; ++i) {
      const PropertyOrderOverride& entry = propertyOrderOverrides_[i];
      if (entry.selectorOffset != selectorOffset || entry.descendant != descendant) continue;
      writeByte(entry.property);
      writeBytes(&entry.sourceOrder, sizeof(entry.sourceOrder));
    }
  };

  writeByte(CssParser::CSS_CACHE_VERSION);

  // A partial cache can style the current low-memory session, but the next
  // EPUB load must retry the source stylesheets instead of trusting it.
  writeByte(complete ? 0 : CSS_CACHE_FLAG_PARTIAL);

  // Write rule count
  const uint16_t serializedRuleCount = static_cast<uint16_t>(ruleCount());
  writeBytes(&serializedRuleCount, sizeof(serializedRuleCount));

  // Write each rule: selector string + CssStyle fields
  for (uint16_t i = 0; i < entryCount_; ++i) {
    const std::string_view selector = selectorAt(i);
    // Write selector string (length-prefixed)
    const auto selectorLen = static_cast<uint16_t>(selector.size());
    writeBytes(&selectorLen, sizeof(selectorLen));
    writeBytes(selector.data(), selectorLen);

    uint8_t styleWire[STYLE_WIRE_BYTES];
    encodeStyleWire(stylePool_[entries_[i].styleIndex], styleWire);
    writeBytes(styleWire, sizeof(styleWire));
    writeCascade(entries_[i].offset, false, entries_[i].specificity, entries_[i].sourceOrder);
    if (!writeOk) break;
  }

  // Descendant selectors use the same wire record as simple selectors, with
  // one canonical ASCII space between their two parts. Cache hydration
  // dispatches them back into the bounded descendant index.
  for (uint16_t i = 0; writeOk && i < descendantEntryCount_; ++i) {
    const std::string_view ancestor = descendantAncestorAt(i);
    const std::string_view subject = descendantSubjectAt(i);
    const auto selectorLen = static_cast<uint16_t>(ancestor.size() + 1 + subject.size());
    writeBytes(&selectorLen, sizeof(selectorLen));
    writeBytes(ancestor.data(), ancestor.size());
    writeBytes(" ", 1);
    writeBytes(subject.data(), subject.size());

    uint8_t styleWire[STYLE_WIRE_BYTES];
    encodeStyleWire(stylePool_[descendantEntries_[i].styleIndex], styleWire);
    writeBytes(styleWire, sizeof(styleWire));
    writeCascade(descendantEntries_[i].ancestorOffset, true, descendantEntries_[i].specificity,
                 descendantEntries_[i].sourceOrder);
  }

  if (!writeOk || !file.close()) {
    LOG_ERR("CSS", "Failed to write temporary CSS cache");
    file.close();
    Storage.remove(tmpPath.c_str());
    return false;
  }

  const bool hadExistingCache = Storage.exists(finalPath.c_str());
  if (hadExistingCache) {
    Storage.remove(backupPath.c_str());
    if (!Storage.rename(finalPath.c_str(), backupPath.c_str())) {
      LOG_ERR("CSS", "Failed to back up existing CSS cache");
      Storage.remove(tmpPath.c_str());
      return false;
    }
  }

  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    LOG_ERR("CSS", "Failed to promote temporary CSS cache");
    Storage.remove(tmpPath.c_str());
    if (Storage.exists(backupPath.c_str()) && !Storage.rename(backupPath.c_str(), finalPath.c_str())) {
      LOG_ERR("CSS", "Failed to restore previous CSS cache");
    }
    return false;
  }

  Storage.remove(backupPath.c_str());

  LOG_DBG("CSS", "Saved %u rules to %s cache", serializedRuleCount, complete ? "complete" : "partial");
  return true;
}

CssParser::CacheLoadResult CssParser::loadFromCache() {
  if (cachePath.empty()) {
    return CacheLoadResult::Invalid;
  }

  HalFile file;
  if (!Storage.openFileForRead("CSS", cachePath + rulesCache, file)) {
    if (!restoreCacheBackupIfNeeded() || !Storage.openFileForRead("CSS", cachePath + rulesCache, file)) {
      return CacheLoadResult::Invalid;
    }
  }

  // Clear existing rules
  clear();

  // Read and verify version
  uint8_t version = 0;
  if (file.read(&version, 1) != 1 || version != CssParser::CSS_CACHE_VERSION) {
    LOG_DBG("CSS", "Cache version mismatch (got %u, expected %u), removing stale cache for rebuild", version,
            CssParser::CSS_CACHE_VERSION);
    // Explicitly close() file before calling Storage.remove()
    file.close();
    Storage.remove((cachePath + rulesCache).c_str());
    return CacheLoadResult::Invalid;
  }

  uint8_t flags = 0;
  if (file.read(&flags, sizeof(flags)) != sizeof(flags) || (flags & ~CSS_CACHE_KNOWN_FLAGS) != 0) {
    LOG_DBG("CSS", "Invalid CSS cache flags: %u", flags);
    return CacheLoadResult::Invalid;
  }

  // Read rule count
  uint16_t ruleCount = 0;
  if (file.read(&ruleCount, sizeof(ruleCount)) != sizeof(ruleCount)) {
    return CacheLoadResult::Invalid;
  }

  if (ruleCount > MAX_RULES) {
    LOG_DBG("CSS", "Invalid cache rule count (%u > %zu)", ruleCount, MAX_RULES);
    clear();
    return CacheLoadResult::Invalid;
  }

  // The wire count includes descendants. Reserve only the first flat-index
  // chunk here; each store grows independently as records are dispatched.
  const PoolResult entryCapacityResult = ensureEntryCapacity(std::min<uint16_t>(ruleCount, 128));
  if (entryCapacityResult == PoolResult::OutOfMemory) {
    clear();
    return CacheLoadResult::LowMemory;
  }
  if (entryCapacityResult == PoolResult::Limit) {
    clear();
    return CacheLoadResult::Invalid;
  }

  auto selectorBuffer = ruleCount > 0 ? makeUniqueNoThrow<char[]>(MAX_SELECTOR_LENGTH) : nullptr;
  if (ruleCount > 0 && !selectorBuffer) {
    clear();
    return CacheLoadResult::LowMemory;
  }

  // Read each rule
  for (uint16_t i = 0; i < ruleCount; ++i) {
    // Read selector string
    uint16_t selectorLen = 0;
    if (file.read(&selectorLen, sizeof(selectorLen)) != sizeof(selectorLen)) {
      clear();
      return CacheLoadResult::Invalid;
    }

    if (selectorLen == 0 || selectorLen > MAX_SELECTOR_LENGTH) {
      LOG_DBG("CSS", "Invalid selector length in cache: %u", selectorLen);
      clear();
      return CacheLoadResult::Invalid;
    }

    if (file.read(selectorBuffer.get(), selectorLen) != selectorLen) {
      clear();
      return CacheLoadResult::Invalid;
    }

    uint8_t styleWire[STYLE_WIRE_BYTES];
    if (file.read(styleWire, sizeof(styleWire)) != sizeof(styleWire)) {
      clear();
      return CacheLoadResult::Invalid;
    }

    CssStyle style;
    if (!decodeStyleWire(styleWire, style)) {
      clear();
      return CacheLoadResult::Invalid;
    }

    uint16_t cachedSpecificity = 0;
    uint16_t sourceOrder = 0;
    if (file.read(&cachedSpecificity, sizeof(cachedSpecificity)) != sizeof(cachedSpecificity) ||
        file.read(&sourceOrder, sizeof(sourceOrder)) != sizeof(sourceOrder)) {
      clear();
      return CacheLoadResult::Invalid;
    }
    uint8_t overrideCount = 0;
    std::array<uint8_t, CSS_PROPERTY_COUNT> overrideProperties{};
    std::array<uint16_t, CSS_PROPERTY_COUNT> overrideOrders{};
    std::array<bool, CSS_PROPERTY_COUNT> seenOverride{};
    if (file.read(&overrideCount, sizeof(overrideCount)) != sizeof(overrideCount) ||
        overrideCount > CSS_PROPERTY_COUNT) {
      clear();
      return CacheLoadResult::Invalid;
    }
    for (uint8_t overrideIndex = 0; overrideIndex < overrideCount; ++overrideIndex) {
      uint8_t property = 0;
      uint16_t order = 0;
      if (file.read(&property, sizeof(property)) != sizeof(property) ||
          file.read(&order, sizeof(order)) != sizeof(order) || property >= CSS_PROPERTY_COUNT ||
          seenOverride[property] || order < sourceOrder || !styleDefinesProperty(style, property)) {
        clear();
        return CacheLoadResult::Invalid;
      }
      seenOverride[property] = true;
      overrideProperties[overrideIndex] = property;
      overrideOrders[overrideIndex] = order;
    }

    const std::string_view selector(selectorBuffer.get(), selectorLen);
    std::string_view parts[3];
    size_t partCount = 0;
    forEachDelimitedToken(selector, isCssWhitespace, [&](const std::string_view part) {
      if (partCount < std::size(parts)) parts[partCount] = part;
      ++partCount;
    });
    if (partCount == 0 || partCount > 2) {
      clear();
      return CacheLoadResult::Invalid;
    }
    const uint16_t expectedSpecificity =
        partCount == 2 ? descendantSpecificity(parts[0], parts[1]) : selectorSpecificity(parts[0]);
    if (cachedSpecificity != expectedSpecificity) {
      clear();
      return CacheLoadResult::Invalid;
    }
    const RuleInsertResult insertResult = partCount == 2
                                              ? insertOrMergeDescendant(parts[0], parts[1], style, sourceOrder)
                                              : insertOrMerge(parts[0], style, sourceOrder);
    if (insertResult == RuleInsertResult::OutOfMemory) {
      clear();
      return CacheLoadResult::LowMemory;
    }
    if (insertResult == RuleInsertResult::Limit) {
      clear();
      return CacheLoadResult::Invalid;
    }
    if (insertResult == RuleInsertResult::Merged) {
      LOG_DBG("CSS", "Duplicate selector in CSS cache");
      clear();
      return CacheLoadResult::Invalid;
    }
    const bool descendant = partCount == 2;
    uint32_t selectorOffset = 0;
    if (descendant) {
      selectorOffset = descendantEntries_[descendantEntryCount_ - 1].ancestorOffset;
    } else {
      const SelectorEntry* inserted = findEntry(parts[0]);
      if (!inserted) {
        clear();
        return CacheLoadResult::Invalid;
      }
      selectorOffset = inserted->offset;
    }
    for (uint8_t overrideIndex = 0; overrideIndex < overrideCount; ++overrideIndex) {
      const PoolResult overrideResult = recordPropertyOrder(
          selectorOffset, descendant, overrideProperties[overrideIndex], overrideOrders[overrideIndex]);
      if (overrideResult != PoolResult::Ready) {
        clear();
        return overrideResult == PoolResult::OutOfMemory ? CacheLoadResult::LowMemory : CacheLoadResult::Invalid;
      }
      const uint16_t overrideOrder = overrideOrders[overrideIndex];
      nextSourceOrder_ = overrideOrder == UINT16_MAX
                             ? UINT16_MAX
                             : std::max(nextSourceOrder_, static_cast<uint16_t>(overrideOrder + 1));
    }
    nextSourceOrder_ =
        sourceOrder == UINT16_MAX ? UINT16_MAX : std::max(nextSourceOrder_, static_cast<uint16_t>(sourceOrder + 1));
  }

  if (file.available() != 0) {
    clear();
    return CacheLoadResult::Invalid;
  }

  const bool partial = (flags & CSS_CACHE_FLAG_PARTIAL) != 0;
  LOG_DBG("CSS", "Loaded %u rules from %s cache", ruleCount, partial ? "partial" : "complete");
  return CacheLoadResult::Complete;
}
