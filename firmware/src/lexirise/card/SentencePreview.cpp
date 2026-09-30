#include "SentencePreview.h"

#include <algorithm>
#include <iterator>
#include <numeric>

#include "lexirise/text/Punctuation.h"
#include "lexirise/text/SentenceBuilder.h"
#include "lexirise/text/Utf8Prefix.h"

namespace lexipoint::card {

namespace {

// ，；。！？ then 、 (Japanese only): each three bytes in UTF-8, lead bytes that never occur inside another character.
constexpr std::string_view kClauseMarks[] = {"\xEF\xBC\x8C", "\xEF\xBC\x9B", "\xE3\x80\x82",
                                             "\xEF\xBC\x81", "\xEF\xBC\x9F", "\xE3\x80\x81"};
constexpr size_t kMarkBytes = 3;
constexpr size_t kChineseMarks = std::size(kClauseMarks) - 1;  // all but 、

bool clauseMarkAt(const std::string_view text, const size_t at, const Language language) {
  const size_t marks = language == Language::Chinese ? kChineseMarks : std::size(kClauseMarks);
  return std::any_of(std::begin(kClauseMarks), std::begin(kClauseMarks) + marks,
                     [&](const std::string_view mark) { return text.compare(at, mark.size(), mark) == 0; });
}

}  // namespace

std::vector<std::string_view> splitClauses(const std::string_view text, const Language language) {
  size_t marks = 0;
  for (size_t i = 0; i < text.size(); i++) marks += clauseMarkAt(text, i, language) ? 1 : 0;
  std::vector<std::string_view> out;
  out.reserve(marks + 1);
  const text::Script script = language == Language::Chinese ? text::Script::Chinese : text::Script::Japanese;
  // A clause mark's run: further marks and closers stay with it (！？, 。」, ？！」), as the sentence builder keeps
  // them.
  const auto runEnd = [&](size_t at) {
    while (at < text.size()) {
      if (clauseMarkAt(text, at, language)) {
        at += kMarkBytes;
        continue;
      }
      const std::string_view c = text::utf8FirstChars(text.substr(at), 1);
      if (c.empty() || !text::Punctuation::isCloser(text::utf8FirstCodepoint(c), script)) break;
      at += c.size();
    }
    return at;
  };
  size_t start = 0;
  for (size_t i = 0; i < text.size(); i++) {
    if (!clauseMarkAt(text, i, language)) continue;
    const size_t end = runEnd(i);
    out.push_back(text.substr(start, end - start));
    start = end;
    i = end - 1;
  }
  if (start < text.size()) out.push_back(text.substr(start));
  return out;
}

SentencePreview::SentencePreview(const SentenceForSave& sentence) {
  const std::vector<std::string_view> own = splitClauses(sentence.text, sentence.language);
  // Each later sentence split on its own, so its end ends a clause even without a mark (「はい」, a line ending ……).
  std::vector<std::string_view> after;
  std::vector<std::string> afterSeparators;
  for (const LaterSentence& later : sentence.after) {
    const std::vector<std::string_view> clauses = splitClauses(text::trimmedSpaces(later.text), sentence.language);
    if (after.empty()) {
      after.reserve(clauses.size() * sentence.after.size());
      afterSeparators.reserve(clauses.size() * sentence.after.size());
    }
    for (size_t k = 0; k < clauses.size(); k++) afterSeparators.push_back(k == 0 ? later.separator : std::string());
    after.insert(after.end(), clauses.begin(), clauses.end());
  }
  clauses_.reserve(own.size() + after.size());
  separators_.assign(own.size(), std::string());
  separators_.insert(separators_.end(), std::make_move_iterator(afterSeparators.begin()),
                     std::make_move_iterator(afterSeparators.end()));
  drops_.reserve(own.size() + after.size());
  size_t start = 0;
  const size_t mark = std::min(sentence.markStart, sentence.text.size());
  for (size_t i = 0; i < own.size(); i++) {
    const size_t end = start + own[i].size();
    if (mark >= start && (mark < end || i + 1 == own.size())) {
      wordClause_ = i;
      wordAt_ = mark - start;
      wordLength_ = std::min(sentence.markLength, sentence.text.size() - mark);
    }
    clauses_.emplace_back(own[i]);
    start = end;
  }
  first_ = 0;
  last_ = own.empty() ? 0 : own.size() - 1;
  clauses_.insert(clauses_.end(), after.begin(), after.end());
}

bool SentencePreview::shorter() {
  if (first_ < wordClause_) {
    first_++;
    drops_.push_back(Drop::First);
    return true;
  }
  if (last_ > wordClause_) {
    last_--;
    drops_.push_back(Drop::Last);
    return true;
  }
  return false;
}

bool SentencePreview::longer() {
  if (!drops_.empty()) {
    if (drops_.back() == Drop::First) {
      first_--;
    } else {
      last_++;
    }
    drops_.pop_back();
    return true;
  }
  if (last_ + 1 < clauses_.size()) {
    last_++;
    return true;
  }
  return false;
}

MarkedText SentencePreview::shown() const { return window(first_, last_); }

MarkedText SentencePreview::window(const size_t first, const size_t last) const {
  MarkedText out;
  if (clauses_.empty()) return out;
  std::string joined;
  joined.reserve(std::accumulate(
      clauses_.begin() + static_cast<std::ptrdiff_t>(first), clauses_.begin() + static_cast<std::ptrdiff_t>(last) + 1,
      size_t{0}, [](const size_t n, const std::string& c) { return n + c.size() + text::kMaxSeparatorBytes; }));
  size_t word = 0;
  for (size_t i = first; i <= last; i++) {
    if (i > first) joined += separators_[i];
    if (i == wordClause_) word = joined.size() + wordAt_;
    joined += clauses_[i];
  }
  const std::string_view trimmed = text::trimmedSpaces(joined);
  const size_t lead = static_cast<size_t>(trimmed.data() - joined.data());
  out.text = std::string(trimmed);
  const size_t start = word > lead ? word - lead : 0;
  out.markStart = std::min(start, out.text.size());
  out.markLength = std::min(wordLength_, out.text.size() - out.markStart);
  return out;
}

std::optional<MarkedText> SentencePreview::longerShown() const {
  // The window longer() would leave (the last drop undone, else the next clause), without changing this one.
  if (!drops_.empty()) {
    return drops_.back() == Drop::First ? window(first_ - 1, last_) : window(first_, last_ + 1);
  }
  if (last_ + 1 < clauses_.size()) return window(first_, last_ + 1);
  return std::nullopt;
}

}  // namespace lexipoint::card
