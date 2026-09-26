#if LEXIRISE

#include "BookTags.h"

#include <Logging.h>

#include <algorithm>

#include "lexirise/text/BookSlug.h"
#include "lexirise/text/Utf8Prefix.h"

namespace lexipoint {
namespace {

constexpr const char* kLogTag = "LXCFG";

bool isSlug(const std::string_view slug) {
  if (slug.empty() || slug.size() > config::kBookSlugMaxBytes) return false;
  return std::all_of(slug.begin(), slug.end(),
                     [](const char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

// One line, no control characters, no spaces at the ends, cut at a character.
std::string lineTitle(const std::string_view title) {
  std::string out;
  out.reserve(std::min(title.size(), config::kBookTagTitleMaxBytes + 1));
  for (const char c : title) {
    if (out.size() > config::kBookTagTitleMaxBytes) break;
    const bool control = static_cast<unsigned char>(c) < 0x20 || c == 0x7F;
    if (control || c == ' ') {
      if (!out.empty() && out.back() != ' ') out.push_back(' ');
    } else {
      out.push_back(c);
    }
  }
  out.resize(text::utf8Prefix(out, config::kBookTagTitleMaxBytes).size());
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

size_t serializedSize(const BookTagList& list) {
  size_t size = 0;
  for (const auto& entry : list) size += entry.slug.size() + 1 + entry.title.size() + 1;
  return size;
}

// The book file's name without its extension: an untitled book's title in the record.
std::string_view fileTitle(std::string_view path) {
  const size_t slash = path.find_last_of('/');
  if (slash != std::string_view::npos) path.remove_prefix(slash + 1);
  const size_t dot = path.find_last_of('.');
  if (dot != std::string_view::npos && dot > 0) path.remove_suffix(path.size() - dot);
  return path;
}

}  // namespace

BookTagList parseBookTags(const std::string_view text) {
  BookTagList list;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(start, end - start);
    start = end + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) continue;
    const std::string_view slug = line.substr(0, eq);
    if (!isSlug(slug)) continue;
    list.erase(std::remove_if(list.begin(), list.end(), [slug](const auto& entry) { return entry.slug == slug; }),
               list.end());
    addBookTagIn(list, slug, line.substr(eq + 1));
  }
  return list;
}

std::string serializeBookTags(const BookTagList& list) {
  std::string text;
  text.reserve(serializedSize(list));
  for (const auto& entry : list) {
    text += entry.slug;
    text += '=';
    text += entry.title;
    text += '\n';
  }
  return text;
}

std::optional<std::string> bookTitleIn(const BookTagList& list, const std::string_view slug) {
  const auto it = std::find_if(list.begin(), list.end(), [slug](const auto& entry) { return entry.slug == slug; });
  if (it == list.end()) return std::nullopt;
  return it->title;
}

bool addBookTagIn(BookTagList& list, const std::string_view slug, const std::string_view title) {
  if (!isSlug(slug) || bookTitleIn(list, slug)) return false;
  std::string line = lineTitle(title);
  if (line.empty()) return false;
  list.push_back({std::string(slug), std::move(line)});
  while (!list.empty() && (list.size() > config::kBookTagsMax || serializedSize(list) > config::kBookTagsMaxBytes)) {
    list.erase(list.begin());
  }
  return true;
}

std::vector<std::string> saveTags(const Settings& settings, const std::string_view bookTag) {
  std::vector<std::string> tags = tagList(settings.tags);
  if (settings.tagBook && !bookTag.empty() && std::find(tags.begin(), tags.end(), bookTag) == tags.end()) {
    tags.emplace_back(bookTag);
  }
  return tags;
}

void BookTagStore::loadLocked() {
  if (loaded_) return;
  loaded_ = true;
  std::string text;
  switch (readSafely(files_, config::kBookTagsFile, text)) {
    case SafeRead::Ok:
    case SafeRead::RecoveredBackup:
      list_ = parseBookTags(text);
      break;
    case SafeRead::Missing:
      break;
    case SafeRead::Unreadable:
    default:
      LOG_ERR(kLogTag, "Book tags unreadable: moved to %s", config::kBookTagsBadPath);
      break;
  }
}

std::optional<std::string> BookTagStore::title(const std::string_view slug) {
  std::lock_guard<std::mutex> lock(mutex_);
  loadLocked();
  return bookTitleIn(list_, slug);
}

bool BookTagStore::remember(const std::string_view slug, const std::string_view title) {
  std::lock_guard<std::mutex> lock(mutex_);
  loadLocked();
  if (bookTitleIn(list_, slug)) return true;
  BookTagList next = list_;
  if (!addBookTagIn(next, slug, title)) return false;
  if (!replaceSafely(files_, config::kBookTagsFile, serializeBookTags(next))) return false;
  list_ = std::move(next);
  LOG_INF(kLogTag, "Book tag %s recorded", std::string(slug).c_str());
  return true;
}

std::string bookRecordTitle(const std::string_view title, const std::string_view path) {
  return lineTitle(text::isUntitled(title) ? fileTitle(path) : title);
}

std::vector<std::string> bookSaveTags(const Settings& settings, const std::string_view title,
                                      const std::string_view path, BookTagStore& store) {
  if (!settings.tagBook) return saveTags(settings, {});
  const std::string slug = text::bookSlug(title, path);
  if (!store.remember(slug, bookRecordTitle(title, path))) {
    LOG_ERR(kLogTag, "Couldn't record the book tag's title");
  }
  return saveTags(settings, config::kBookTagPrefix + slug);
}

}  // namespace lexipoint

#endif  // LEXIRISE
