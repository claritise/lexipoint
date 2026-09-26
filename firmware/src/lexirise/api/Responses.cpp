#if LEXIRISE

#include "Responses.h"

#include <algorithm>

#include "Requests.h"
#include "lexirise/LexiriseConfig.h"
#include "lexirise/net/JsonReader.h"
#include "lexirise/text/Utf8Prefix.h"

namespace lexipoint::api {
namespace {

using json::Path;
using json::Type;
using text::utf8Prefix;

// A JSON number that is a whole value in [0, UINT32_MAX] ("12", not "1.5", "-1" or "1e3").
bool toUint32(const Type type, const std::string_view text, uint32_t& out) {
  if (type != Type::Number || text.empty() || text.size() > 10) return false;
  uint64_t value = 0;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
    value = value * 10 + static_cast<uint64_t>(c - '0');
  }
  if (value > UINT32_MAX) return false;
  out = static_cast<uint32_t>(value);
  return true;
}

// A JSON number in [0, 1] ("0.83", "1", "0"): a frequency score. Anything else leaves `out` alone.
bool toUnitFloat(const Type type, const std::string_view text, float& out) {
  if (type != Type::Number || text.empty() || text.size() > config::kMaxScoreChars) return false;
  float value = 0;
  float scale = 0;  // 0 before the point, then 0.1, 0.01 …
  for (const char c : text) {
    if (c == '.' && scale == 0) {
      scale = 0.1f;
    } else if (c >= '0' && c <= '9') {
      if (scale == 0) {
        value = value * 10 + static_cast<float>(c - '0');
      } else {
        value += scale * static_cast<float>(c - '0');
        scale /= 10;
      }
    } else {
      return false;  // a sign or an exponent: not a score
    }
  }
  if (value > 1) return false;
  out = value;
  return true;
}

class MeVisitor final : public json::Visitor {
 public:
  explicit MeVisitor(MeInfo& out) : out_(out) {}
  void onValue(const Path& path, const Type type, const std::string_view text) override {
    if (path.matches({"user", "name"})) {
      takeDisplay(type, text, out_.name);
    } else if (path.matches({"user", "plan"})) {
      takeDisplay(type, text, out_.plan);
    } else if (path.matches({"apiKey", "rateLimitMax"})) {
      sawMax_ = toUint32(type, text, out_.rateLimitMax);
    } else if (path.matches({"apiKey", "rateLimitTimeWindow"})) {
      toUint32(type, text, out_.rateLimitWindowMs);
    }
  }
  bool sawMax_ = false;

 private:
  static void takeDisplay(const Type type, const std::string_view text, std::string& field) {
    if (type == Type::String && text.size() <= config::kMaxDisplayFieldBytes) field.assign(text);
  }

  MeInfo& out_;
};

class AnalyzeVisitor final : public json::Visitor {
 public:
  explicit AnalyzeVisitor(AnalyzeResult& out) : out_(out) {}

  void onBegin(const Path& path, const bool isArray) override {
    if (!isArray && path.matches({"occurrences", "[]"})) {
      if (out_.occurrences.size() >= config::kMaxOccurrences) {
        overLimit_ = true;
        return;
      }
      out_.occurrences.emplace_back();
      inOccurrence_ = true;
      sawLemma_ = false;
      sawStart_ = false;
      sawEnd_ = false;
      sawOccurrences_ = true;
    } else if (isArray && path.matches({"occurrences"})) {
      sawOccurrences_ = true;
    }
  }

  void onEnd(const Path& path, const bool isArray) override {
    if (isArray || !inOccurrence_ || !path.matches({"occurrences", "[]"})) return;
    Occurrence& occ = out_.occurrences.back();
    if (!sawLemma_) occ.lemma = occ.word;
    if (occ.word.empty() || !sawStart_ || !sawEnd_ || occ.charEnd < occ.charStart) malformed_ = true;
    inOccurrence_ = false;
  }

  void onValue(const Path& path, const Type type, const std::string_view text) override {
    if (path.depth() >= 3 && !path.isIndex(1) && (path.keyIs(0, "entryMetaById") || path.keyIs(0, "stateByEntryId"))) {
      return entryValue(path, type, text);
    }
    if (path.matches({"morphoPending"})) {
      out_.morphoPending = type == Type::Bool && text == "true";
      return;
    }
    if (!inOccurrence_ || !path.matches({"occurrences", "[]", "*"})) return;
    Occurrence& occ = out_.occurrences.back();
    const std::string_view field = path.leaf();
    if (field == "word") {
      takeString(type, text, occ.word);
    } else if (field == "lemma") {
      sawLemma_ = takeString(type, text, occ.lemma);
    } else if (field == "transliteration") {
      takeString(type, text, occ.reading);
    } else if (field == "entryId") {
      toUint32(type, text, occ.entryId);
    } else if (field == "lemmaEntryId") {
      toUint32(type, text, occ.lemmaEntryId);
    } else if (field == "charStart") {
      sawStart_ = toUint32(type, text, occ.charStart);
      if (!sawStart_) malformed_ = true;
    } else if (field == "charEnd") {
      sawEnd_ = toUint32(type, text, occ.charEnd);
      if (!sawEnd_) malformed_ = true;
    } else if (field == "isWordLike") {
      occ.wordLike = type == Type::Bool && text == "true";
    }
  }

  bool sawOccurrences_ = false;
  bool overLimit_ = false;
  bool malformed_ = false;

 private:
  // entryMetaById.<id>.{transliteration, partOfSpeech[0], rank} and
  // stateByEntryId.<id>.{saved_expression_id, proficiency, seen_count}. Other fields are skipped.
  void entryValue(const Path& path, const Type type, const std::string_view text) {
    uint32_t id = 0;
    if (!toUint32(Type::Number, path.at(1).key, id)) return;  // the key is the id's digits
    const bool isMeta = path.keyIs(0, "entryMetaById");
    const size_t entries = isMeta ? out_.meta.size() : out_.state.size();
    const bool known = isMeta ? out_.meta.count(id) != 0 : out_.state.count(id) != 0;
    if (entries >= config::kMaxEntries && !known) return;  // dropped, not failed on (kMaxEntries)
    if (isMeta) {
      EntryMeta& meta = out_.meta[id];
      if (path.depth() == 3 && path.keyIs(2, "transliteration")) {
        takeString(type, text, meta.reading);
      } else if (path.depth() == 3 && path.keyIs(2, "rank")) {
        toUint32(type, text, meta.rank);
      } else if (path.depth() == 3 && path.keyIs(2, "frequencyScore")) {
        toUnitFloat(type, text, meta.frequency);
      } else if (path.depth() == 4 && path.keyIs(2, "partOfSpeech") && path.index(3) == 0) {
        takeString(type, text, meta.partOfSpeech);
      }
      return;
    }
    if (path.depth() != 3) return;
    EntryState& state = out_.state[id];
    if (path.keyIs(2, "saved_expression_id")) {
      if (type == Type::Number || type == Type::String) takeString(Type::String, text, state.savedExpressionId);
    } else if (path.keyIs(2, "proficiency")) {
      uint32_t value = 0;
      if (toUint32(type, text, value) && value <= config::kMaxProficiency) state.proficiency = static_cast<int>(value);
    } else if (path.keyIs(2, "seen_count")) {
      toUint32(type, text, state.seenCount);
    }
  }

  // Null or a non-string leaves the field empty (undocumented fields degrade quietly, §4a).
  bool takeString(const Type type, const std::string_view text, std::string& field) {
    if (type != Type::String) return false;
    if (text.size() > config::kMaxTokenBytes) {
      overLimit_ = true;
      return false;
    }
    field.assign(text);
    return true;
  }

  AnalyzeResult& out_;
  bool inOccurrence_ = false;
  bool sawLemma_ = false;
  bool sawStart_ = false;
  bool sawEnd_ = false;
};

class LookupVisitor final : public json::Visitor {
 public:
  explicit LookupVisitor(LookupResult& out) : out_(out) {}
  bool sawWord() const { return sawWord_; }

  void onValue(const Path& path, const Type type, const std::string_view text) override {
    if (path.matches({"word"})) {
      sawWord_ = type == Type::String && !text.empty();
      if (sawWord_) out_.word.assign(utf8Prefix(text, config::kMaxTokenBytes));
    } else if (path.matches({"transliteration"})) {
      if (type == Type::String) out_.reading.assign(utf8Prefix(text, config::kMaxTokenBytes));
    } else if (path.matches({"rank"})) {
      toUint32(type, text, out_.rank);
    } else if (path.matches({"frequency_score"})) {
      toUnitFloat(type, text, out_.frequency);
    } else if (path.matches({"translation_status"})) {
      out_.translationPending = type == Type::String && text != "ready";
    } else if (path.matches({"system_tags", "[]"})) {
      takeLevel(type, text);
    } else if (path.depth() >= 3 && path.keyIs(0, "translations") && path.isIndex(1)) {
      sense(path, type, text);
    }
  }

  // The element being read has ended: keep it if it has a translation, up to kMaxTranslations.
  void finishSense() {
    if (!pending_.translation.empty() && out_.senses.size() < config::kMaxTranslations) {
      out_.senses.push_back(std::move(pending_));
    }
    pending_ = Sense{};
  }

 private:
  // JLPT-N1..N5 / HSK-1..9 / HSK-7+: matched by pattern, since other tags sit beside it (kanji, char).
  void takeLevel(const Type type, const std::string_view text) {
    if (type != Type::String || !out_.level.empty()) return;
    const bool jlpt = text.size() == 7 && text.substr(0, 6) == "JLPT-N" && text[6] >= '1' && text[6] <= '5';
    const bool hsk = text.size() >= 5 && text.size() <= 6 && text.substr(0, 4) == "HSK-" && text[4] >= '1' &&
                     text[4] <= '9' && (text.size() == 5 || text[5] == '+');
    if (jlpt || hsk) out_.level.assign(text);
  }

  // translations[i].translation and translations[i].part_of_speech[0]: the first kMaxTranslations
  // elements that have a translation (one without is skipped, not counted).
  void sense(const Path& path, const Type type, const std::string_view text) {
    const int index = path.index(1);
    if (index != pendingIndex_) {
      finishSense();
      pendingIndex_ = index;
    }
    if (out_.senses.size() >= config::kMaxTranslations || type != Type::String) return;
    if (path.depth() == 3 && path.keyIs(2, "translation")) {
      pending_.translation.assign(utf8Prefix(text, config::kMaxTranslationBytes));
    } else if (path.depth() == 4 && path.keyIs(2, "part_of_speech") && path.index(3) == 0) {
      pending_.partOfSpeech.assign(utf8Prefix(text, config::kMaxTokenBytes));
    }
  }

  LookupResult& out_;
  bool sawWord_ = false;
  Sense pending_;
  int pendingIndex_ = -1;
};

class SaveVisitor final : public json::Visitor {
 public:
  explicit SaveVisitor(SaveResult& out) : out_(out) {}
  void onValue(const Path& path, const Type type, const std::string_view text) override {
    if (!path.matches({"result", "savedExpressionId"})) return;
    if ((type == Type::Number || type == Type::String) && !text.empty() && text.size() <= config::kMaxSavedIdBytes) {
      out_.savedExpressionId.assign(text);
    }
  }

 private:
  SaveResult& out_;
};

constexpr size_t kDecksExpected = 16;  // a list's first reservation: a reader's handful of decks

// A deck id: a number or a string, not empty and not too long.
bool takeId(const Type type, const std::string_view text, std::string& out) {
  if (type != Type::Number && type != Type::String) return false;
  if (!isPlainId(text, config::kMaxDeckIdBytes)) return false;  // it goes into a request path
  out.assign(text);
  return true;
}

bool isIdKey(const std::string_view key) { return key == "id" || key == "deckId" || key == "deck_id"; }

// One deck's scalar field, by the reference's name or its camelCase form.
void takeDeckField(const std::string_view key, const Type type, const std::string_view text, DeckSummary& deck) {
  const auto take = [&](std::string& field) {
    if (type == Type::String && text.size() <= config::kMaxTokenBytes) field.assign(text);
  };
  if (isIdKey(key)) {
    takeId(type, text, deck.id);
  } else if (key == "title") {
    take(deck.title);
  } else if (key == "deck_type" || key == "deckType") {
    take(deck.deckType);
  } else if (key == "unit_type" || key == "unitType") {
    take(deck.unitType);
  } else if (key == "rule_type" || key == "ruleType") {
    take(deck.ruleType);
  } else if (key == "language" || key == "lang" || key == "source_lang" || key == "sourceLang" ||
             key == "source_language" || key == "sourceLanguage") {
    take(deck.language);
  } else if (key == "starred" || key == "isStarred" || key == "is_starred") {
    if (type == Type::Bool) deck.starred = text == "true";
  } else if (key == "owned" || key == "isOwner" || key == "is_owner" || key == "isOwned" || key == "is_owned") {
    if (type == Type::Bool) deck.owned = text == "true";
  }
}

class DeckListVisitor final : public json::Visitor {
 public:
  explicit DeckListVisitor(std::vector<DeckSummary>& out) : out_(out) {}
  bool sawList_ = false;
  size_t entries_ = 0;             // every entry seen, read or not
  bool morePages_ = false;         // the answer says there's another page
  std::optional<uint32_t> total_;  // a count of every deck, when the answer gives one

  void onBegin(const Path& path, const bool isArray) override {
    if (isArray && !sawList_ &&
        (path.depth() == 0 || (path.depth() == 1 && (path.keyIs(0, "decks") || path.keyIs(0, "data"))))) {
      sawList_ = true;
      base_ = path.depth();
      if (base_ == 1) listKey_ = path.at(0).key;
      return;
    }
    if (isArray && inList(path) && path.depth() == base_ + 1) {
      entries_++;  // an array where a deck should be: an entry this can't read
      current_ = -1;
    } else if (!isArray && inList(path) && path.depth() == base_ + 1) {
      entries_++;
      if (out_.size() < config::kMaxDecksListed) {
        out_.emplace_back();
        current_ = path.index(base_);
      } else {
        current_ = -1;
      }
    }
  }
  void onValue(const Path& path, const Type type, const std::string_view text) override {
    if (inList(path) && path.depth() == base_ + 1) {
      entries_++;  // a scalar where a deck should be: an entry this can't read
      return;
    }
    // Another page is only looked for at the top level, beside the list (as GET /v1/vocabulary says it).
    if (path.depth() == 1 && !path.isIndex(0)) {
      const std::string_view key = path.leaf();
      if ((key == "hasMore" || key == "has_more") && type == Type::Bool && text == "true") morePages_ = true;
      if ((key == "nextOffset" || key == "next_offset") && type != Type::Null) morePages_ = true;
      if (key == "totalCount" || key == "total_count") {
        uint32_t total = 0;
        if (toUint32(type, text, total)) total_ = total;
      }
    }
    if (!inList(path) || path.depth() < base_ + 2 || path.index(base_) != current_ || out_.empty()) return;
    if (path.isIndex(base_ + 1)) return;
    const std::string& key = path.at(base_ + 1).key;
    DeckSummary& deck = out_.back();
    if (path.depth() == base_ + 2) {
      takeDeckField(key, type, text, deck);
    } else if (path.depth() == base_ + 3 && (key == "user_tags" || key == "userTags") && path.isIndex(base_ + 2) &&
               type == Type::String && !text.empty() && text.size() <= config::kMaxTokenBytes &&
               deck.userTags.size() < config::kMaxDeckTagsRead) {
      deck.userTags.emplace_back(text);
    }
  }

 private:
  // Inside the list: an entry, or one of its fields.
  bool inList(const Path& path) const {
    if (!sawList_ || path.depth() <= base_ || !path.isIndex(base_)) return false;
    return base_ == 0 || path.keyIs(0, listKey_);
  }

  std::vector<DeckSummary>& out_;
  size_t base_ = 0;  // the list's depth: 0 a bare array, 1 under `decks` / `data`
  std::string listKey_;
  int current_ = -1;  // the entry being read (past the cap: none)
};

class CreatedDeckVisitor final : public json::Visitor {
 public:
  // Where the deck's fields are, in the order they're trusted.
  enum Slot : size_t { UnderDeck, UnderData, AtTop, kSlots };
  DeckSummary found[kSlots];

  void onValue(const Path& path, const Type type, const std::string_view text) override {
    if (path.depth() == 2 && (path.keyIs(0, "deck") || path.keyIs(0, "data")) && !path.isIndex(1)) {
      takeDeckField(path.at(1).key, type, text, found[path.keyIs(0, "deck") ? UnderDeck : UnderData]);
    } else if (path.depth() == 1 && !path.isIndex(0)) {
      takeDeckField(path.at(0).key, type, text, found[AtTop]);
    }
  }
};

}  // namespace

const EntryMeta* AnalyzeResult::metaFor(const uint32_t entryId) const {
  const auto it = meta.find(entryId);
  return it == meta.end() ? nullptr : &it->second;
}

const EntryState* AnalyzeResult::stateFor(const uint32_t entryId) const {
  const auto it = state.find(entryId);
  return it == state.end() || it->second.savedExpressionId.empty() ? nullptr : &it->second;
}

ParseStatus parseLookup(const std::string_view body, LookupResult& out) {
  LookupResult parsed;
  LookupVisitor visitor(parsed);
  if (json::read(body, visitor) != json::Result::Ok || !visitor.sawWord()) return ParseStatus::Malformed;
  visitor.finishSense();  // the last element
  out = std::move(parsed);
  return ParseStatus::Ok;
}

ParseStatus parseSave(const std::string_view body, SaveResult& out) {
  SaveResult parsed;
  SaveVisitor visitor(parsed);
  if (json::read(body, visitor) != json::Result::Ok || parsed.savedExpressionId.empty()) return ParseStatus::Malformed;
  out = std::move(parsed);
  return ParseStatus::Ok;
}

ParseStatus parseDeckList(const std::string_view body, std::vector<DeckSummary>& out, bool* complete) {
  std::vector<DeckSummary> parsed;
  parsed.reserve(kDecksExpected);
  DeckListVisitor visitor(parsed);
  if (json::read(body, visitor) != json::Result::Ok || !visitor.sawList_) return ParseStatus::Malformed;
  parsed.erase(
      std::remove_if(parsed.begin(), parsed.end(),
                     [](const DeckSummary& d) { return d.id.empty() || (d.title.empty() && d.deckType.empty()); }),
      parsed.end());
  if (visitor.entries_ > 0 && parsed.empty()) return ParseStatus::Malformed;
  // Whole: every entry read (none past the cap, none dropped for an id or fields it couldn't use: that one may be
  // the book's deck), and no sign of another page.
  if (complete) {
    *complete = visitor.entries_ == parsed.size() && !visitor.morePages_ &&
                (!visitor.total_ || *visitor.total_ <= visitor.entries_);  // a count past what came: a page cap
  }
  out = std::move(parsed);
  return ParseStatus::Ok;
}

ParseStatus parseCreatedDeck(const std::string_view body, CreatedDeck& out) {
  CreatedDeckVisitor visitor;
  if (json::read(body, visitor) != json::Result::Ok) return ParseStatus::Malformed;
  for (const DeckSummary& deck : visitor.found) {
    if (deck.id.empty()) continue;
    out = CreatedDeck{deck.id, deck.deckType, deck.ruleType};
    return ParseStatus::Ok;
  }
  return ParseStatus::Malformed;
}

ParseStatus parseMe(const std::string_view body, MeInfo& out) {
  MeInfo parsed;
  MeVisitor visitor(parsed);
  if (json::read(body, visitor) != json::Result::Ok || !visitor.sawMax_) return ParseStatus::Malformed;
  out = parsed;
  return ParseStatus::Ok;
}

ParseStatus parseAnalyze(const std::string_view body, AnalyzeResult& out) {
  AnalyzeResult parsed;
  AnalyzeVisitor visitor(parsed);
  const json::Result result = json::read(body, visitor);
  if (visitor.overLimit_) return ParseStatus::OverLimit;
  if (result != json::Result::Ok || visitor.malformed_ || !visitor.sawOccurrences_) return ParseStatus::Malformed;
  out = std::move(parsed);
  return ParseStatus::Ok;
}

}  // namespace lexipoint::api

#endif  // LEXIRISE
