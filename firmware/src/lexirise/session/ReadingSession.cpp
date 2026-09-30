#include "ReadingSession.h"

#include <algorithm>
#include <cstdio>

#include "lexirise/LexiriseConfig.h"
#include "lexirise/util/Crc32.h"

namespace lexipoint::session {

namespace {

uint32_t sentenceHash(const Language language, const std::string_view text) {
  const uint32_t h = bytes::Fnv1a().add(languageCode(language)).add(":").add(text).value();
  return h != 0 ? h : 1;  // 0 means a word
}

}  // namespace

void ReadingSession::bookOpened() {
  *this = ReadingSession();
  active_ = true;
}

void ReadingSession::bookClosed() {
  std::optional<Summary> summary;
  if (active_ && homeNext_ && lookedUp_ > 0 && language_) summary = Summary{saved_, lookedUp_, *language_, words_};
  *this = ReadingSession();
  summary_ = std::move(summary);
}

std::optional<Summary> ReadingSession::takeSummary() {
  std::optional<Summary> out = std::move(summary_);
  summary_.reset();
  homeNext_ = false;
  return out;
}

void ReadingSession::lookedUp(const Language language) {
  if (!active_) return;
  lookedUp_++;
  if (!language_) language_ = language;
}

void ReadingSession::saved(const Language language, const std::string_view id, const bool word) {
  if (!active_) return;
  saved_++;
  if (word && words_ && language_ == language) (*words_)++;
  if (id.empty() || saves_.size() >= config::kSessionSavesMax) return;  // counted; its Undo won't uncount it
  if (saves_.capacity() == 0) saves_.reserve(config::kSessionSavesReserved);
  saves_.push_back({std::string(id), language, word, 0});
}

void ReadingSession::removed(const std::string_view id) {
  if (!active_ || id.empty()) return;
  const auto it = std::find_if(saves_.begin(), saves_.end(), [id](const Saved& s) { return s.id == id; });
  if (it == saves_.end()) return;  // not this session's save
  if (saved_ > 0) saved_--;
  if (it->word && words_ && *words_ > 0 && language_ == it->language) (*words_)--;
  saves_.erase(it);
}

std::optional<Language> ReadingSession::countWanted() const {
  if (!active_ || words_) return std::nullopt;
  return language_;
}

void ReadingSession::countFetched(const Language language, const uint32_t totalCount) {
  if (!active_ || language_ != language || words_) return;
  words_ = totalCount;  // it counts every word saved so far: later ones are counted here
}

void ReadingSession::sentenceSaved(const Language language, const std::string_view text, const std::string_view id) {
  const auto it = std::find_if(saves_.begin(), saves_.end(), [id](const Saved& s) { return s.id == id; });
  if (it != saves_.end() && !it->word) it->sentenceHash = sentenceHash(language, text);
}

std::optional<std::string> ReadingSession::sentenceId(const Language language, const std::string_view text) const {
  const uint32_t hash = sentenceHash(language, text);
  const auto it = std::find_if(saves_.begin(), saves_.end(), [hash](const Saved& s) { return s.sentenceHash == hash; });
  if (it == saves_.end()) return std::nullopt;
  return it->id;
}

std::string groupedNumber(const uint32_t n) {
  const std::string digits = std::to_string(n);
  std::string out;
  out.reserve(digits.size() + digits.size() / 3);
  for (size_t i = 0; i < digits.size(); i++) {
    if (i != 0 && (digits.size() - i) % 3 == 0) out += ',';
    out += digits[i];
  }
  return out;
}

namespace {

constexpr size_t kLineBytes = 96;  // a summary line, with room for any translation's words

// `format` with its %s as `first` (and a second %s as `second`: an unused argument is ignored).
std::string formatted(const char* format, const std::string& first, const std::string& second = {}) {
  char line[kLineBytes];
  std::snprintf(line, sizeof(line), format, first.c_str(), second.c_str());
  return line;
}

}  // namespace

std::string countsLine(const Summary& summary, const SummaryFormats& formats) {
  return formatted(formats.counts, groupedNumber(summary.saved), groupedNumber(summary.lookedUp));
}

std::string wordsLine(const Summary& summary, const SummaryFormats& formats) {
  if (!summary.words) return {};
  const bool ja = summary.language == Language::Japanese;
  if (*summary.words == 1) return ja ? formats.wordJa : formats.wordZh;
  return formatted(ja ? formats.wordsJa : formats.wordsZh, groupedNumber(*summary.words));
}

ReadingSession& readingSession() {
  static ReadingSession session;
  return session;
}

}  // namespace lexipoint::session
