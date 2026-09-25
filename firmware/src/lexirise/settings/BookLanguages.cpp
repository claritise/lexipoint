#if LEXIRISE

#include "BookLanguages.h"

#include <Logging.h>

#include <algorithm>
#include <iterator>

namespace lexipoint {
namespace {

constexpr const char* kLogTag = "LXCFG";

bool isLinePath(const std::string_view path) {
  return !path.empty() && path.find_first_of("\r\n") == std::string_view::npos;
}

size_t serializedSize(const BookLanguageList& list) {
  size_t size = 0;
  for (const auto& entry : list)
    size += std::string_view(languageCode(entry.language)).size() + 1 + entry.path.size() + 1;
  return size;
}

}  // namespace

BookLanguageList parseBookLanguages(const std::string_view text) {
  BookLanguageList list;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(start, end - start);
    start = end + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) continue;
    const auto language = languageFromCode(line.substr(0, eq));
    const std::string_view path = line.substr(eq + 1);
    if (!language || path.empty()) continue;
    setBookLanguageIn(list, path, language);
  }
  return list;
}

std::string serializeBookLanguages(const BookLanguageList& list) {
  std::string text;
  text.reserve(serializedSize(list));
  for (const auto& entry : list) {
    text += languageCode(entry.language);
    text += '=';
    text += entry.path;
    text += '\n';
  }
  return text;
}

std::optional<Language> bookLanguageIn(const BookLanguageList& list, const std::string_view path) {
  const auto it = std::find_if(list.begin(), list.end(), [path](const auto& entry) { return entry.path == path; });
  if (it == list.end()) return std::nullopt;
  return it->language;
}

bool setBookLanguageIn(BookLanguageList& list, const std::string_view path, const std::optional<Language> language) {
  if (!isLinePath(path)) return false;
  list.erase(std::remove_if(list.begin(), list.end(), [path](const auto& entry) { return entry.path == path; }),
             list.end());
  if (language) list.push_back({std::string(path), *language});
  while (!list.empty() &&
         (list.size() > config::kBookLanguagesMax || serializedSize(list) > config::kBookLanguagesMaxBytes)) {
    list.erase(list.begin());
  }
  return true;
}

std::optional<Language> nextBookLanguage(const std::optional<Language> current) {
  if (!current) return kLanguages[0];
  const auto* it = std::find(std::begin(kLanguages), std::end(kLanguages), *current);
  if (it == std::end(kLanguages) || ++it == std::end(kLanguages)) return std::nullopt;
  return *it;
}

void BookLanguageStore::loadLocked() {
  if (loaded_) return;
  loaded_ = true;
  std::string text;
  switch (readSafely(files_, config::kBookLanguagesFile, text)) {
    case SafeRead::Ok:
    case SafeRead::RecoveredBackup:
      list_ = parseBookLanguages(text);
      break;
    case SafeRead::Missing:
      break;
    case SafeRead::Unreadable:
    default:
      LOG_ERR(kLogTag, "Book languages unreadable: moved to %s", config::kBookLanguagesBadPath);
      break;
  }
}

std::optional<Language> BookLanguageStore::get(const std::string_view path) {
  std::lock_guard<std::mutex> lock(mutex_);
  loadLocked();
  return bookLanguageIn(list_, path);
}

bool BookLanguageStore::set(const std::string_view path, const std::optional<Language> language) {
  std::lock_guard<std::mutex> lock(mutex_);
  loadLocked();
  BookLanguageList next = list_;
  if (!setBookLanguageIn(next, path, language)) return false;
  if (!replaceSafely(files_, config::kBookLanguagesFile, serializeBookLanguages(next))) return false;
  list_ = std::move(next);
  LOG_INF(kLogTag, "Book language %s: %s", language ? languageCode(*language) : "auto", std::string(path).c_str());
  return true;
}

void BookLanguageRow::open(std::string bookPath) {
  path_ = std::move(bookPath);
  show(store_.get(path_));
}

std::optional<Language> BookLanguageRow::language() const {
  const uint8_t slot = slot_.load();
  if (slot == 0 || slot > std::size(kLanguages)) return std::nullopt;
  return kLanguages[slot - 1];
}

void BookLanguageRow::show(const std::optional<Language> language) {
  uint8_t slot = 0;
  if (language) {
    const auto* it = std::find(std::begin(kLanguages), std::end(kLanguages), *language);
    slot = static_cast<uint8_t>(it - std::begin(kLanguages) + 1);
  }
  slot_ = slot;
}

bool BookLanguageRow::cycle() {
  const std::optional<Language> next = nextBookLanguage(language());
  if (!store_.set(path_, next)) {
    LOG_ERR(kLogTag, "Couldn't save the book's lookup language");
    return false;
  }
  show(next);
  return true;
}

}  // namespace lexipoint

#endif  // LEXIRISE
