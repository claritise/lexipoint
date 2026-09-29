#include "IgnoredWords.h"

#include <Logging.h>

#include <algorithm>
#include <charconv>
#include <limits>
#include <numeric>

namespace lexipoint {
namespace {

constexpr const char* kLogTag = "LXCFG";
constexpr char kTextMark = '~';  // `ja:~<form>`: a word without an entry id
constexpr int kLanguageShift = 32;

// A line's parts: `<code>:<uint32>\n` and `<code>:~<form>\n`. The longest lines: the file never drops a key for size
// before its cap.
constexpr size_t kIdDigitsMax = std::numeric_limits<uint32_t>::digits10 + 1;
constexpr size_t kIdLineFixedBytes = config::kLanguageCodeBytes + 1 + 1;        // "ja" ':' … '\n'
constexpr size_t kTextLineFixedBytes = config::kLanguageCodeBytes + 1 + 1 + 1;  // "ja" ':' '~' … '\n'
constexpr size_t kIdLineMaxBytes = kIdLineFixedBytes + kIdDigitsMax;
constexpr size_t kTextLineMaxBytes = kTextLineFixedBytes + config::kIgnoredTextMaxBytes;
static_assert(config::kIgnoredIdsMax * kIdLineMaxBytes + config::kIgnoredTextsMax * kTextLineMaxBytes <=
                  config::kIgnoredMaxBytes,
              "ignored.ini fits both caps of the longest lines");

uint64_t packed(const Language language, const uint32_t entryId) {
  return (static_cast<uint64_t>(language) << kLanguageShift) | entryId;
}
Language language(const uint64_t id) { return static_cast<Language>(id >> kLanguageShift); }

// A dictionary form that can be a key: not empty, one line (no line breaks or other controls), and short enough
// never to be cut.
bool usableForm(const std::string_view text) {
  return !text.empty() && text.size() <= config::kIgnoredTextMaxBytes &&
         std::none_of(text.begin(), text.end(), isControlByte);
}

// An id's digits onto `text`: the one formatter (the file, the log). to_chars can't fail into kIdDigitsMax bytes.
void appendId(std::string& text, const uint32_t id) {
  char digits[kIdDigitsMax];
  text.append(digits, std::to_chars(digits, digits + sizeof(digits), id).ptr);
}

// How many digits appendId writes for `id` (serializeIgnored sums them to reserve the file's exact size).
size_t digitsOf(uint32_t id) {
  size_t n = 1;
  while (id >= 10) {
    id /= 10;
    n++;
  }
  return n;
}

}  // namespace

std::optional<IgnoredKey> ignoredKeyFor(const Language language, const uint32_t entryId,
                                        const std::string_view headword) {
  if (entryId != 0) return IgnoredKey{language, entryId, {}};
  if (!usableForm(headword)) return std::nullopt;
  return IgnoredKey{language, 0, std::string(headword)};
}

bool IgnoredWords::contains(const IgnoredKey& key) const {
  if (key.entryId != 0) return std::find(ids_.begin(), ids_.end(), packed(key.language, key.entryId)) != ids_.end();
  return std::find(texts_.begin(), texts_.end(), Text{key.language, key.text}) != texts_.end();
}

bool IgnoredWords::add(const IgnoredKey& key, std::optional<IgnoredKey>* evicted) {
  if (evicted) evicted->reset();
  if (contains(key)) return false;
  // At a cap the oldest goes first, so the list never holds one more than its cap (no reallocation past it).
  if (key.entryId != 0) {
    if (ids_.size() >= config::kIgnoredIdsMax) {
      if (evicted) *evicted = IgnoredKey{language(ids_.front()), static_cast<uint32_t>(ids_.front()), {}};
      ids_.erase(ids_.begin());
    }
    ids_.push_back(packed(key.language, key.entryId));
    return true;
  }
  if (!usableForm(key.text)) return false;
  if (texts_.size() >= config::kIgnoredTextsMax) {
    if (evicted) *evicted = IgnoredKey{texts_.front().language, 0, texts_.front().text};
    texts_.erase(texts_.begin());
  }
  texts_.push_back({key.language, key.text});
  return true;
}

bool IgnoredWords::remove(const IgnoredKey& key, size_t* at) {
  if (key.entryId != 0) {
    const auto it = std::find(ids_.begin(), ids_.end(), packed(key.language, key.entryId));
    if (it == ids_.end()) return false;
    if (at) *at = static_cast<size_t>(it - ids_.begin());
    ids_.erase(it);
    return true;
  }
  const auto it = std::find(texts_.begin(), texts_.end(), Text{key.language, key.text});
  if (it == texts_.end()) return false;
  if (at) *at = static_cast<size_t>(it - texts_.begin());
  texts_.erase(it);
  return true;
}

void IgnoredWords::putBack(const IgnoredKey& key, const size_t at) {
  if (key.entryId != 0) {
    ids_.insert(ids_.begin() + static_cast<std::ptrdiff_t>(std::min(at, ids_.size())),
                packed(key.language, key.entryId));
  } else {
    texts_.insert(texts_.begin() + static_cast<std::ptrdiff_t>(std::min(at, texts_.size())),
                  Text{key.language, key.text});
  }
}

void IgnoredWords::undoAdd(const IgnoredKey& key, const std::optional<IgnoredKey>& evicted) {
  remove(key);
  if (evicted) putBack(*evicted, 0);  // it was the oldest
}

bool IgnoredWords::hasRoomFor(const IgnoredKey& key) const {
  return key.entryId != 0 ? ids_.size() < config::kIgnoredIdsMax : texts_.size() < config::kIgnoredTextsMax;
}

void IgnoredWords::reserve(const size_t ids, const size_t texts) {
  ids_.reserve(std::min(ids, config::kIgnoredIdsMax));
  texts_.reserve(std::min(texts, config::kIgnoredTextsMax));
}

IgnoredWords parseIgnored(const std::string_view text) {
  IgnoredWords list;
  // One reservation of each part, from the file's lines (a form's line has ":~"), never past the caps. An estimate:
  // lines that aren't keys, or a ":~" inside a form, only make it a little off.
  const size_t lines = static_cast<size_t>(std::count(text.begin(), text.end(), '\n')) + 1;
  size_t forms = 0;
  for (size_t at = text.find(":~"); at != std::string_view::npos; at = text.find(":~", at + 2)) forms++;
  list.reserve(lines - std::min(forms, lines), forms);
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(start, end - start);
    start = end + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const size_t colon = line.find(':');
    if (colon == std::string_view::npos) continue;
    const std::optional<Language> language = languageFromCode(line.substr(0, colon));
    if (!language) continue;
    const std::string_view value = line.substr(colon + 1);
    std::optional<IgnoredKey> key;
    if (!value.empty() && value.front() == kTextMark) {
      key = ignoredKeyFor(*language, 0, value.substr(1));
    } else {
      uint32_t id = 0;
      const auto [last, error] = std::from_chars(value.data(), value.data() + value.size(), id);
      if (error == std::errc() && last == value.data() + value.size() && id != 0) key = IgnoredKey{*language, id, {}};
    }
    if (!key) continue;
    list.remove(*key);  // listed again: it's newest there
    list.add(*key);
  }
  return list;
}

std::string serializeIgnored(const IgnoredWords& list) {
  // Exactly the file's size, reserved once: no worst-case block (a full list's ~12 KB, not the cap's 20 KB).
  size_t size = std::accumulate(list.ids_.begin(), list.ids_.end(), size_t{0}, [](const size_t n, const uint64_t id) {
    return n + kIdLineFixedBytes + digitsOf(static_cast<uint32_t>(id));
  });
  size = std::accumulate(list.texts_.begin(), list.texts_.end(), size,
                         [](const size_t n, const auto& t) { return n + kTextLineFixedBytes + t.text.size(); });
  std::string text;
  text.reserve(size);
  for (const uint64_t id : list.ids_) {
    text += languageCode(language(id));
    text += ':';
    appendId(text, static_cast<uint32_t>(id));
    text += '\n';
  }
  for (const auto& t : list.texts_) {
    text += languageCode(t.language);
    text += ':';
    text += kTextMark;
    text += t.text;
    text += '\n';
  }
  return text;
}

std::string ignoredKeyText(const IgnoredKey& key) {
  std::string text = languageCode(key.language);
  text += ':';
  if (key.entryId != 0) {
    appendId(text, key.entryId);
    return text;
  }
  text += kTextMark;
  return text + key.text;
}

void IgnoredWordStore::loadLocked() {
  if (loaded_) return;
  loaded_ = true;
  ++revision_;
  std::string text;
  switch (readSafely(files_, config::kIgnoredFile, text)) {
    case SafeRead::Ok:
    case SafeRead::RecoveredBackup:
      list_ = parseIgnored(text);
      break;
    case SafeRead::Missing:
      break;
    case SafeRead::Unreadable:
    default:
      LOG_ERR(kLogTag, "Ignored words unreadable: moved to %s", config::kIgnoredBadPath);
      break;
  }
}

void IgnoredWordStore::load() {
  std::lock_guard<std::mutex> lock(mutex_);
  loadLocked();
}

IgnoredWords IgnoredWordStore::list() {
  std::lock_guard<std::mutex> lock(mutex_);
  loadLocked();
  return list_;
}

bool IgnoredWordStore::contains(const IgnoredKey& key) {
  std::lock_guard<std::mutex> lock(mutex_);
  loadLocked();
  return list_.contains(key);
}

IgnoredWordStore::Write IgnoredWordStore::write(const IgnoredKey& key, const bool ignored,
                                                std::optional<IgnoredKey>* evicted,
                                                const std::optional<IgnoredKey>& restore) {
  std::lock_guard<std::mutex> lock(mutex_);
  loadLocked();
  if (evicted) evicted->reset();
  // In place (no copy of the list: at its cap, ~8 KB), put back exactly when the write fails; the file's text is one
  // buffer reserved at its exact size for the write.
  std::optional<IgnoredKey> pushedOut;
  size_t at = 0;
  if (!(ignored ? list_.add(key, &pushedOut) : list_.remove(key, &at))) {
    return ignored == list_.contains(key) ? Write::Unchanged : Write::Failed;  // already so, or not a key
  }
  // An Undo brings back the oldest key its ignore pushed out (the list has room again: this key just left).
  const bool restoring = !ignored && restore && !list_.contains(*restore) && list_.hasRoomFor(*restore);
  if (restoring) list_.putBack(*restore, 0);
  if (replaceSafely(files_, config::kIgnoredFile, serializeIgnored(list_))) {
    ++revision_;  // a real change only: an unchanged or failed write leaves the list as it was
    if (evicted) *evicted = std::move(pushedOut);
    return Write::Written;
  }
  if (ignored) {
    list_.undoAdd(key, pushedOut);
  } else {
    if (restoring) list_.remove(*restore);
    list_.putBack(key, at);
  }
  return Write::Failed;
}

}  // namespace lexipoint
