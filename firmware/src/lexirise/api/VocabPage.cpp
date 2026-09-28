#if LEXIRISE

#include "VocabPage.h"

#include "lexirise/LexiriseConfig.h"
#include "lexirise/util/Timing.h"

namespace lexipoint::api {
namespace {

using json::Path;
using json::Type;

constexpr size_t kUint32DigitsMax = 10;  // "4294967295"
constexpr size_t kIsoMinBytes = 20;      // "YYYY-MM-DDTHH:MM:SSZ"
constexpr size_t kIsoSecondsEnd = 19;    // where a fraction or the zone starts, after "YYYY-MM-DDTHH:MM:SS"
constexpr size_t kMsDigits = 3;          // a fraction's digits that make its milliseconds

// A whole number in [0, UINT32_MAX], as a JSON number or a string of digits (an id may come either way).
bool wholeNumber(const Type type, const std::string_view text, uint32_t& out) {
  if ((type != Type::Number && type != Type::String) || text.empty() || text.size() > kUint32DigitsMax) return false;
  uint64_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + static_cast<uint64_t>(c - '0');
  }
  if (value > UINT32_MAX) return false;
  out = static_cast<uint32_t>(value);
  return true;
}

// `count` digits at `at` as a number; false if any isn't a digit.
bool digitsAt(const std::string_view text, const size_t at, const size_t count, int& out) {
  if (at + count > text.size()) return false;
  int value = 0;
  for (size_t i = at; i < at + count; i++) {
    if (text[i] < '0' || text[i] > '9') return false;
    value = value * 10 + (text[i] - '0');
  }
  out = value;
  return true;
}

// Days from 1970-01-01 to a proleptic Gregorian date (the civil-from-days inverse).
int64_t daysFromCivil(int64_t y, const int m, const int d) {
  y -= m <= 2 ? 1 : 0;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

}  // namespace

std::optional<uint64_t> parseIsoTimeMs(const std::string_view text) {
  // YYYY-MM-DDTHH:MM:SS
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  if (text.size() < kIsoMinBytes || !digitsAt(text, 0, 4, year) || text[4] != '-' || !digitsAt(text, 5, 2, month) ||
      text[7] != '-' || !digitsAt(text, 8, 2, day) || (text[10] != 'T' && text[10] != 't') ||
      !digitsAt(text, 11, 2, hour) || text[13] != ':' || !digitsAt(text, 14, 2, minute) || text[16] != ':' ||
      !digitsAt(text, 17, 2, second)) {
    return std::nullopt;
  }
  if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) return std::nullopt;
  size_t at = kIsoSecondsEnd;
  int ms = 0;
  if (text[at] == '.') {  // a fraction: its first kMsDigits digits are the ms
    at++;
    const size_t start = at;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
      if (at - start < kMsDigits) ms = ms * 10 + (text[at] - '0');
      at++;
    }
    if (at == start) return std::nullopt;
    for (size_t n = at - start; n < kMsDigits; n++) ms *= 10;
  }
  int64_t offsetS = 0;
  if (at < text.size() && (text[at] == 'Z' || text[at] == 'z') && at + 1 == text.size()) {
    // UTC
  } else if (at + 6 == text.size() && (text[at] == '+' || text[at] == '-') && text[at + 3] == ':') {
    int oh = 0, om = 0;
    if (!digitsAt(text, at + 1, 2, oh) || !digitsAt(text, at + 4, 2, om) || oh > 23 || om > 59) return std::nullopt;
    offsetS = (oh * timing::kSecondsPerHour + om * timing::kSecondsPerMinute) * (text[at] == '+' ? 1 : -1);
  } else {
    return std::nullopt;
  }
  const int64_t seconds = daysFromCivil(year, month, day) * timing::kSecondsPerDay + hour * timing::kSecondsPerHour +
                          minute * timing::kSecondsPerMinute + second - offsetS;
  if (seconds < 0) return std::nullopt;
  return static_cast<uint64_t>(seconds) * timing::kMsPerSecond + static_cast<uint64_t>(ms);
}

void VocabPageReader::Visitor::onBegin(const Path& path, const bool isArray) {
  if (path.depth() == 0) {
    sawObject = !isArray;
  } else if (path.depth() == 1 && path.keyIs(0, "items")) {
    sawItems = isArray;
  } else if (sawItems && path.depth() == 2 && path.keyIs(0, "items") && path.isIndex(1)) {
    if (page_.items.size() >= config::kVocabListLimitMax) {
      overLimit = true;
      return;
    }
    page_.items.emplace_back();
    inItem_ = !isArray;
    if (isArray) page_.items.back().word = false;  // an array where an item should be: not one the mirror can keep
  }
}

void VocabPageReader::Visitor::onEnd(const Path& path, bool) {
  if (path.depth() != 2 || !inItem_ || !path.keyIs(0, "items")) return;
  inItem_ = false;
  const VocabItem& item = page_.items.back();
  if (item.updatedMs > page_.newestMs) page_.newestMs = item.updatedMs;
}

void VocabPageReader::Visitor::onValue(const Path& path, const Type type, const std::string_view text) {
  if (path.depth() == 1 && !path.isIndex(0)) {
    uint32_t value = 0;
    if (path.keyIs(0, "nextOffset") && type == Type::Number && wholeNumber(type, text, value)) page_.nextOffset = value;
    if (path.keyIs(0, "totalCount") && type == Type::Number && wholeNumber(type, text, value)) page_.totalCount = value;
    if (path.keyIs(0, "languageCount") && type == Type::Number && wholeNumber(type, text, value)) {
      page_.languageCount = value;
    }
    return;
  }
  if (!path.keyIs(0, "items") || !path.isIndex(1)) return;
  if (path.depth() == 2) {  // a scalar where an item should be
    if (page_.items.size() >= config::kVocabListLimitMax) {
      overLimit = true;
      return;
    }
    page_.items.emplace_back().word = false;
    return;
  }
  if (path.depth() != 3 || !inItem_ || path.isIndex(2)) return;  // only the item's own fields
  VocabItem& item = page_.items.back();
  const std::string_view key = path.leaf();
  if (key == "id") {
    wholeNumber(type, text, item.savedId);
  } else if (key == "dictionary_id") {
    wholeNumber(type, text, item.entryId);
  } else if (key == "proficiency") {
    uint32_t value = 0;
    if (type == Type::Number && wholeNumber(type, text, value) && value <= config::kMaxProficiency) {
      item.proficiency = static_cast<int>(value);
    }
  } else if (key == "suspended") {
    item.suspended = type == Type::Bool && text == "true";
  } else if (key == "next_review_at") {
    const std::optional<uint64_t> ms = type == Type::String ? parseIsoTimeMs(text) : std::nullopt;
    const uint64_t seconds = ms ? *ms / timing::kMsPerSecond : 0;
    item.nextReviewS = seconds <= UINT32_MAX ? static_cast<uint32_t>(seconds) : 0;
  } else if (key == "updated_at") {
    const std::optional<uint64_t> ms = type == Type::String ? parseIsoTimeMs(text) : std::nullopt;
    item.updatedMs = ms.value_or(0);
  } else if (key == "unit_type") {
    item.word = type != Type::String || text == "word";  // null: not said
  }
}

VocabPageReader::VocabPageReader(const Cancel cancel) : visitor_(page_), reader_(visitor_), cancel_(cancel) {
  page_.items.reserve(config::kVocabPageItems);
}

bool VocabPageReader::onBody(const char* data, const size_t len) {
  if (cancel_ && cancel_()) {
    cancelled_ = true;
    return false;
  }
  return reader_.feed(std::string_view(data, len)) && !visitor_.overLimit;
}

ParseStatus VocabPageReader::finish(VocabPage& out) {
  if (cancelled_) return ParseStatus::Malformed;  // cut short on purpose: cancelled() says so
  if (visitor_.overLimit) return ParseStatus::OverLimit;
  if (reader_.finish() != json::Result::Ok || !visitor_.sawObject || !visitor_.sawItems) return ParseStatus::Malformed;
  out = std::move(page_);
  return ParseStatus::Ok;
}

ParseStatus parseVocabPage(const std::string_view body, VocabPage& out) {
  VocabPageReader reader;
  reader.onBody(body.data(), body.size());
  return reader.finish(out);
}

}  // namespace lexipoint::api

#endif  // LEXIRISE
