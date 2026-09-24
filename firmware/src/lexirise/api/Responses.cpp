#if LEXIRISE

#include "Responses.h"

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
