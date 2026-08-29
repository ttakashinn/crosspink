#include "SavedItemsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <new>

#include "MappedInputManager.h"
#include "activities/reader/EpubReaderBookmarksActivity.h"
#include "activities/reader/EpubReaderClippingsActivity.h"
#include "clippings/ClippingStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"
#include "util/BookmarkFile.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long DELETE_HOLD_MS = 700;
}

void SavedItemsActivity::onEnter() {
  UiListActivity::onEnter();
  reload();
}

const char* SavedItemsActivity::headerTitle() const {
  return clippingsOnly ? tr(STR_MY_CLIPPINGS) : tr(STR_BOOKMARKS_AND_CLIPPINGS);
}

void SavedItemsActivity::reload() {
  const auto status = SavedItemsCatalog::load(books);
  catalogError = status == SavedItemsCatalog::LoadStatus::INVALID ||
                 status == SavedItemsCatalog::LoadStatus::NEWER_VERSION ||
                 status == SavedItemsCatalog::LoadStatus::IO_ERROR;
  if (catalogError) books.clear();
  books.erase(std::remove_if(books.begin(), books.end(),
                             [this](const SavedItemsCatalog::Entry& entry) {
                               return clippingsOnly ? entry.clippingCount == 0
                                                    : entry.bookmarkCount == 0 && entry.clippingCount == 0;
                             }),
              books.end());
  rebuildRows();
  if (nav.selected >= listCount()) nav.selected = std::max(0, listCount() - 1);
  nav.follow(listCount());
}

void SavedItemsActivity::rebuildRows() {
  subtitles.clear();
  rowItems.clear();
  subtitles.reserve(books.size());
  rowItems.reserve(books.size());
  for (size_t i = 0; i < books.size(); ++i) {
    const auto& book = books[i];
    char counts[96];
    if (clippingsOnly) {
      const StrId countText = book.clippingCount == 1 ? StrId::STR_CLIPPING_COUNT_ONE : StrId::STR_CLIPPING_COUNT;
      snprintf(counts, sizeof(counts), I18N.get(countText), static_cast<unsigned>(book.clippingCount));
    } else {
      snprintf(counts, sizeof(counts), tr(STR_SAVED_ITEM_COUNTS), static_cast<unsigned>(book.bookmarkCount),
               static_cast<unsigned>(book.clippingCount));
    }
    std::string subtitle;
    if (!book.author.empty()) {
      subtitle = book.author;
      subtitle += " · ";
    }
    subtitle += counts;
    subtitles.push_back(std::move(subtitle));

    fui::ListItem item;
    item.label = book.title.c_str();
    item.subtitle = subtitles.back().c_str();
    item.icon = listIconFor(UIIcon::Bookmark, 32);
    item.actionValue = static_cast<int16_t>(i);
    rowItems.push_back(item);
  }
}

void SavedItemsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));
  if (catalogError) {
    screen.centeredText(tr(STR_SAVED_ITEMS_OPEN_FAILED), screen.theme().bodyText);
    return;
  }
  if (books.empty()) {
    screen.centeredText(clippingsOnly ? tr(STR_NO_GLOBAL_CLIPPINGS) : tr(STR_NO_SAVED_ITEMS), screen.theme().bodyText);
    return;
  }
  if (!mappedInput.hasTouch()) {
    const int helpHeight = renderer.getLineHeight(SMALL_FONT_ID);
    const fui::Rect help = screen.takeBottom(static_cast<int16_t>(helpHeight + metrics.verticalSpacing));
    GUI.drawHelpText(renderer, Rect{help.x, help.y + metrics.verticalSpacing, help.width, helpHeight},
                     tr(STR_HOLD_OPEN_TO_DELETE));
  }

  fui::ListProps props;
  props.items = rowItems.data();
  props.count = static_cast<uint16_t>(rowItems.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch | fui::InputLongPress;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

bool SavedItemsActivity::handleCustomInput() {
  return popup.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool SavedItemsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return true;
  }
  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, DELETE_HOLD_MS)) {
    if (listCount() > 0) showDeleteMenu(nav.selected);
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (listCount() == 0) return true;
    activateIndex(nav.selected);
    return true;
  }
  return false;
}

void SavedItemsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  const auto& book = books[index];
  if (clippingsOnly) {
    openClippings(index);
    return;
  }
  if (book.bookmarkCount > 0 && book.clippingCount > 0) {
    showOpenMenu(index);
  } else if (book.bookmarkCount > 0) {
    openBookmarks(index);
  } else {
    openClippings(index);
  }
}

void SavedItemsActivity::onRowLongPress(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  showDeleteMenu(index);
}

void SavedItemsActivity::showOpenMenu(const int index) {
  const char* options[] = {tr(STR_BOOKMARKS), tr(STR_CLIPPINGS)};
  popup.show(books[index].title.c_str(), options, 2, 0, [this, index](const int selected) {
    if (selected == 0)
      openBookmarks(index);
    else
      openClippings(index);
  });
  requestUpdate();
}

void SavedItemsActivity::showDeleteMenu(const int index) {
  if (index < 0 || index >= listCount()) return;
  if (clippingsOnly) {
    showDeleteConfirmation(index, false);
    return;
  }
  std::vector<std::string> options;
  if (books[index].bookmarkCount > 0) options.emplace_back(tr(STR_DELETE_BOOKMARKS));
  if (books[index].clippingCount > 0) options.emplace_back(tr(STR_DELETE_CLIPPINGS));
  const bool firstIsBookmarks = books[index].bookmarkCount > 0;
  popup.show(StrId::STR_SAVED_ITEMS, options, 0, [this, index, firstIsBookmarks](const int selected) {
    const bool bookmarks = firstIsBookmarks && selected == 0;
    showDeleteConfirmation(index, bookmarks);
  });
  requestUpdate();
}

void SavedItemsActivity::showDeleteConfirmation(const int index, const bool bookmarks) {
  const char* options[] = {tr(STR_CANCEL), tr(STR_DELETE)};
  popup.show(bookmarks ? tr(STR_CONFIRM_DELETE_BOOKMARKS) : tr(STR_CONFIRM_DELETE_CLIPPINGS), options, 2, 0,
             [this, index, bookmarks](const int selected) {
               if (selected == 1) deleteAll(index, bookmarks);
             });
  requestUpdate();
}

void SavedItemsActivity::deleteAll(const int index, const bool bookmarks) {
  if (index < 0 || index >= listCount()) return;
  const auto entry = books[index];
  bool saved = false;
  if (bookmarks) {
    saved = BookmarkFile::save(entry.sourcePath, {});
    if (saved) {
      SavedItemsCatalog::updateBookmarks(entry.sourcePath, entry.title, entry.author, 0);
    }
  } else {
    std::string cachePath;
    if (getBookCachePath(entry.sourcePath, cachePath)) {
      saved = ClippingStore::save(entry.sourcePath, cachePath, {}) == ClippingStore::SaveStatus::SAVED;
    }
    if (saved) {
      SavedItemsCatalog::updateClippings(entry.sourcePath, entry.title, entry.author, 0);
    }
  }
  if (!saved) LOG_ERR("SAVED", "Failed to clear saved items for %s", entry.sourcePath.c_str());
  if (saved) {
    // A destructive change removes both the black selected row and a modal
    // dialog. Clean the panel once so those large high-contrast regions do
    // not ghost into the empty state on e-paper.
    renderer.promoteNextRefresh(HalDisplay::FULL_REFRESH);
  }
  reload();
  requestUpdate();
}

std::shared_ptr<Epub> SavedItemsActivity::makeBook(const std::string& path) {
  auto book = std::shared_ptr<Epub>(new (std::nothrow) Epub(path, "/.crosspoint"));
  if (!book) LOG_ERR("SAVED", "OOM: EPUB metadata object");
  if (book && !book->load(false, true)) {
    LOG_DBG("SAVED", "Saved-items list will use fallback labels until the book cache is rebuilt");
  }
  return book;
}

void SavedItemsActivity::openBookmarks(const int index) {
  if (index < 0 || index >= listCount()) return;
  const auto entry = books[index];
  activeEpub = makeBook(entry.sourcePath);
  if (!activeEpub) {
    catalogError = true;
    requestUpdate();
    return;
  }
  auto activity = makeUniqueNoThrow<EpubReaderBookmarksActivity>(renderer, mappedInput, activeEpub, entry.sourcePath);
  if (!activity) {
    LOG_ERR("SAVED", "OOM: bookmarks activity");
    activeEpub.reset();
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(activity), [this, entry](const ActivityResult& result) {
    if (!result.isCancelled) {
      if (const auto* position = std::get_if<ProgressChangeResult>(&result.data)) {
        activityManager.goToReaderAt(entry.sourcePath, *position);
        return;
      }
    }
    activeEpub.reset();
    reload();
    requestUpdate();
  });
}

void SavedItemsActivity::openClippings(const int index) {
  if (index < 0 || index >= listCount()) return;
  const auto entry = books[index];
  activeEpub = makeBook(entry.sourcePath);
  if (!activeEpub) {
    catalogError = true;
    requestUpdate();
    return;
  }
  activeClippings.clear();
  const auto status = ClippingStore::load(entry.sourcePath, activeEpub->getCachePath(), activeClippings);
  activeClippingsWritable =
      status != ClippingStore::LoadStatus::NEWER_VERSION && status != ClippingStore::LoadStatus::IO_ERROR;
  if (status == ClippingStore::LoadStatus::MISSING || status == ClippingStore::LoadStatus::INVALID) {
    SavedItemsCatalog::updateClippings(entry.sourcePath, entry.title, entry.author, 0);
    activeEpub.reset();
    reload();
    requestUpdate();
    return;
  }
  auto activity = makeUniqueNoThrow<EpubReaderClippingsActivity>(renderer, mappedInput, activeEpub, activeClippings,
                                                                 activeClippingsWritable);
  if (!activity) {
    LOG_ERR("SAVED", "OOM: clippings activity");
    activeClippings.clear();
    activeEpub.reset();
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(activity), [this, entry](const ActivityResult& result) {
    if (!result.isCancelled) {
      if (const auto* position = std::get_if<ProgressChangeResult>(&result.data)) {
        activityManager.goToReaderAt(entry.sourcePath, *position);
        return;
      }
    }
    activeClippings.clear();
    activeEpub.reset();
    reload();
    requestUpdate();
  });
}

void SavedItemsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawChrome();
  renderUi();
  for (int pass = 0; nav.consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }
  if (popup.processRender(renderer, mappedInput)) return;
  drawFooter();
  renderer.displayBuffer();
}
