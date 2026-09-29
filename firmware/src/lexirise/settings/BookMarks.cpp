#include "BookMarks.h"

#include <Logging.h>

#include <algorithm>
#include <numeric>

#include "lexirise/LexiriseConfig.h"

namespace lexipoint {
namespace {

constexpr const char* kLogTag = "LXCFG";

bool isLinePath(const std::string_view path) {
  return !path.empty() && path.find_first_of("\r\n") == std::string_view::npos;
}

size_t serializedSize(const BookMarksOffList& list) {
  return std::accumulate(list.begin(), list.end(), size_t{0},
                         [](const size_t size, const std::string& path) { return size + path.size() + 1; });
}

}  // namespace

BookMarksOffList parseBookMarksOff(const std::string_view text) {
  BookMarksOffList list;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(start, end - start);
    start = end + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    if (!line.empty()) setMarksIn(list, line, false);
  }
  return list;
}

std::string serializeBookMarksOff(const BookMarksOffList& list) {
  std::string text;
  text.reserve(serializedSize(list));
  for (const std::string& path : list) {
    text += path;
    text += '\n';
  }
  return text;
}

bool marksOnIn(const BookMarksOffList& list, const std::string_view path) {
  return std::find(list.begin(), list.end(), path) == list.end();
}

bool setMarksIn(BookMarksOffList& list, const std::string_view path, const bool on) {
  if (!isLinePath(path)) return false;
  list.erase(std::remove(list.begin(), list.end(), path), list.end());
  if (!on) list.emplace_back(path);
  while (!list.empty() &&
         (list.size() > config::kBookMarksOffMax || serializedSize(list) > config::kBookMarksOffMaxBytes)) {
    list.erase(list.begin());
  }
  return true;
}

void BookMarksStore::loadLocked() {
  if (loaded_) return;
  loaded_ = true;
  std::string text;
  switch (readSafely(files_, config::kBookMarksOffFile, text)) {
    case SafeRead::Ok:
    case SafeRead::RecoveredBackup:
      list_ = parseBookMarksOff(text);
      break;
    case SafeRead::Missing:
      break;
    case SafeRead::Unreadable:
    default:
      LOG_ERR(kLogTag, "Books' page marks unreadable: moved to %s", config::kBookMarksOffBadPath);
      break;
  }
}

bool BookMarksStore::on(const std::string_view path) {
  std::lock_guard<std::mutex> lock(mutex_);
  loadLocked();
  return marksOnIn(list_, path);
}

bool BookMarksStore::set(const std::string_view path, const bool on) {
  std::lock_guard<std::mutex> lock(mutex_);
  loadLocked();
  BookMarksOffList next = list_;
  if (!setMarksIn(next, path, on)) return false;
  if (!replaceSafely(files_, config::kBookMarksOffFile, serializeBookMarksOff(next))) return false;
  list_ = std::move(next);
  LOG_INF(kLogTag, "Page marks %s: %s", on ? "on" : "off", std::string(path).c_str());
  return true;
}

void BookMarksRow::open(std::string bookPath) {
  path_ = std::move(bookPath);
  on_ = store_.on(path_);
}

bool BookMarksRow::toggle() {
  const bool next = !on_.load();
  if (!store_.set(path_, next)) {
    LOG_ERR(kLogTag, "Couldn't save the book's page marks");
    return false;
  }
  on_ = next;
  return true;
}

}  // namespace lexipoint
