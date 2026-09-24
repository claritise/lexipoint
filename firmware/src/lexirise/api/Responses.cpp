#if LEXIRISE

#include "Responses.h"

#include "lexirise/LexiriseConfig.h"
#include "lexirise/net/JsonReader.h"

namespace lexipoint::api {
namespace {

using json::Path;
using json::Type;

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
      sawOccurrences_ = true;
    } else if (isArray && path.matches({"occurrences"})) {
      sawOccurrences_ = true;
    }
  }

  void onEnd(const Path& path, const bool isArray) override {
    if (isArray || !inOccurrence_ || !path.matches({"occurrences", "[]"})) return;
    Occurrence& occ = out_.occurrences.back();
    if (!sawLemma_) occ.lemma = occ.word;
    if (occ.word.empty() || occ.charEnd < occ.charStart) malformed_ = true;
    inOccurrence_ = false;
  }

  void onValue(const Path& path, const Type type, const std::string_view text) override {
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
      if (!toUint32(type, text, occ.charStart)) malformed_ = true;
    } else if (field == "charEnd") {
      if (!toUint32(type, text, occ.charEnd)) malformed_ = true;
    } else if (field == "isWordLike") {
      occ.wordLike = type == Type::Bool && text == "true";
    }
  }

  bool sawOccurrences_ = false;
  bool overLimit_ = false;
  bool malformed_ = false;

 private:
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
};

}  // namespace

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
