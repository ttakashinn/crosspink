#pragma once

#include <Epub/Page.h>
#include <I18n.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"
#include "util/Dictionary.h"

// Paged viewer for one dictionary definition. HTML definitions are laid out
// through the EPUB chapter parser into styled Pages; anything else (plain
// text, or HTML too damaged to parse) is word-wrapped once on entry and each
// page renders spans of the original string, so no per-line copies are held.
class DictionaryDefinitionActivity final : public Activity {
 public:
  explicit DictionaryDefinitionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string headword,
                                        std::string definition, bool htmlDefinition, std::string dictionaryName)
      : Activity("DictionaryDefinition", renderer, mappedInput),
        headword(std::move(headword)),
        definition(std::move(definition)),
        htmlDefinition(htmlDefinition),
        dictionaryName(std::move(dictionaryName)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // One wrapped display line: a byte span of `definition`. Wrapping keeps
  // lines under the screen width, so uint16_t length is ample.
  struct Line {
    uint32_t start;
    uint16_t len;
  };

  struct WordBox {
    int16_t x = 0;
    int16_t y = 0;
    int16_t width = 0;
    uint16_t row = 0;
    const char* text = nullptr;
    uint16_t length = 0;
    EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  };

  enum class Popup : uint8_t { None, Busy, NotFound, Error };

  // Usable body-text area between the header and the button hints.
  struct BodyArea {
    int width;
    int height;
  };

  BodyArea bodyArea() const;
  bool layoutHtmlPages();
  void wrapText();
  int measureSpan(int fontId, const char* text, size_t len) const;
  void drawBody(int fontId, int x, int startY) const;
  void extractVisibleWords();
  int wordAt(int x, int y) const;
  int closestInRow(uint16_t row, int centerX) const;
  void moveVertical(int direction);
  std::string selectedQuery() const;
  void performLookup(const std::string& query, bool offerSuggestions = true);
  void drawSelection(int fontId) const;
  void changePage(int delta);

  const std::string headword;
  // Not const: onEnter() normalizes embedded NULs (StarDict multi-type
  // separators) to newlines so C-string APIs see the whole text.
  std::string definition;
  const bool htmlDefinition;
  const std::string dictionaryName;
  // Styled path: reader-identical Pages laid out from the HTML definition.
  // Empty means the plain-text span path below is active.
  std::vector<std::unique_ptr<Page>> pages;
  std::vector<Line> lines;
  int currentPage = 0;
  int totalPages = 1;
  int linesPerPage = 1;
  ButtonNavigator buttonNavigator;
  std::vector<WordBox> words;
  int selected = 0;
  int selectionAnchor = 0;
  uint16_t rowCount = 0;
  bool selecting = false;
  bool rangeSelecting = false;
  bool touchRangeSelecting = false;
  Dictionary dictionary;
  bool dictionaryOpenAttempted = false;
  bool dictionaryOpenOk = false;
  bool dictionaryNeedsIndex = false;
  Popup popup = Popup::None;
  StrId popupMessage = StrId::STR_DICT_NOT_FOUND;
  unsigned long popupAt = 0;
};
