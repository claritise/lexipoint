#if LEXIRISE

#include "Requests.h"

#include "lexirise/LexiriseConfig.h"
#include "lexirise/net/JsonWriter.h"
#include "lexirise/text/Utf8Prefix.h"

namespace lexipoint::api {

using text::utf8Prefix;

constexpr const char* kVocabularyPath = "/v1/vocabulary";
constexpr const char* kDecksPath = "/v1/decks";
constexpr const char* kDeckProbeQuery = "?limit=1";  // one item's page: only whether the deck is there matters

net::Request meRequest() { return {net::Method::Get, "/v1/me", ""}; }

namespace {

// analyze/text, the full analysis or (`fast`) the word-level one.
net::Request analyze(const Language language, const std::string_view text, const bool fast) {
  net::JsonObject body;
  body.add("text", utf8Prefix(text, config::kMaxAnalyzeTextBytes)).add("language", languageCode(language));
  if (fast) body.add("fast", true);
  net::Request request{net::Method::Post, "/v1/analyze/text", body.str()};
  request.idempotent = true;  // read-only analysis: a repeat is harmless
  return request;
}

}  // namespace

net::Request analyzeRequest(const Language language, const std::string_view text) {
  return analyze(language, text, false);
}

net::Request analyzeWordsRequest(const Language language, const std::string_view text) {
  return analyze(language, text, true);
}

net::Request lookupRequest(const Language language, const std::string_view lemma) {
  net::Request request{net::Method::Post, "/v1/dictionary/lookup",
                       net::JsonObject()
                           .add("text", utf8Prefix(lemma, config::kMaxTokenBytes))
                           .add("language", languageCode(language))
                           .str()};
  request.idempotent = true;  // a lookup only reads (the server may queue translation work: harmless twice)
  return request;
}

namespace {

std::optional<net::Request> vocabularyItem(const net::Method method, const std::string_view id, std::string body) {
  if (!isPlainId(id, config::kMaxSavedIdBytes)) return std::nullopt;
  net::Request request{method, std::string(kVocabularyPath) + "/" + std::string(id), std::move(body)};
  request.idempotent = true;  // setting a value (or deleting) twice changes nothing more
  return request;
}

}  // namespace

bool isPlainId(const std::string_view id, const size_t maxBytes) {
  if (id.empty() || id.size() > maxBytes) return false;
  for (const char c : id) {
    const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

net::Request saveRequest(const SaveWord& word) {
  net::JsonObject body;
  body.add("language", languageCode(word.language))
      .add("text", utf8Prefix(word.text, config::kMaxTokenBytes))
      .add("mode", "word");
  if (!word.translation.empty()) body.add("translation", utf8Prefix(word.translation, config::kMaxTranslationBytes));
  body.add("proficiency", word.proficiency).add("tags", word.tags);
  if (!word.notes.empty()) body.add("notes", utf8Prefix(word.notes, config::kMaxAnalyzeTextBytes));
  return {net::Method::Post, kVocabularyPath, body.str()};  // not idempotent: an upsert that replaces
}

std::optional<net::Request> setProficiencyRequest(const std::string_view id, const int proficiency) {
  return vocabularyItem(net::Method::Patch, id, net::JsonObject().add("proficiency", proficiency).str());
}

std::optional<net::Request> removeRequest(const std::string_view id) {
  return vocabularyItem(net::Method::Delete, id, "");
}

std::optional<net::Request> clearRequest(const std::string_view id) {
  return vocabularyItem(
      net::Method::Patch, id,
      net::JsonObject().addNull("notes").addNull("customTranslation").add("tags", std::vector<std::string>{}).str());
}

net::Request deckListRequest(const Language language) {
  return {net::Method::Get, std::string(kDecksPath) + "?language=" + languageCode(language), ""};
}

std::optional<net::Request> deckRequest(const std::string_view id) {
  if (!isPlainId(id, config::kMaxDeckIdBytes)) return std::nullopt;
  return net::Request{net::Method::Get, std::string(kDecksPath) + "/" + std::string(id) + kDeckProbeQuery, ""};
}

net::Request createDeckRequest(const NewDeck& deck) {
  return {net::Method::Post, kDecksPath,
          net::JsonObject()
              .add("title", deck.title)
              .add("language", languageCode(deck.language))
              .add("unit_type", config::kDeckUnitWord)
              .add("deck_type", config::kDeckTypeDynamic)
              .add("rule_type", config::kDeckRuleTagFilter)
              .add("user_tags", std::vector<std::string>{std::string(deck.tag)})
              .str()};  // not idempotent: a second deck
}

std::string loggablePath(const std::string_view path) {
  for (const char* collection : {kVocabularyPath, kDecksPath}) {
    const std::string prefix = std::string(collection) + "/";
    if (path.size() > prefix.size() && path.substr(0, prefix.size()) == prefix) return prefix + "{id}";
  }
  return std::string(path);
}

std::string userAgent(const std::string_view crossPointVersion) {
  std::string ua = config::kUserAgentProduct;
  ua += '/';
  ua += config::kLexipointVersion;
  ua += " CrossPoint/";
  ua += crossPointVersion;
  return ua;
}

}  // namespace lexipoint::api

#endif  // LEXIRISE
