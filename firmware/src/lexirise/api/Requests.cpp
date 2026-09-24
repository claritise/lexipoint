#if LEXIRISE

#include "Requests.h"

#include "lexirise/LexiriseConfig.h"
#include "lexirise/net/JsonWriter.h"
#include "lexirise/text/Utf8Prefix.h"

namespace lexipoint::api {

using text::utf8Prefix;

net::Request meRequest() { return {net::Method::Get, "/v1/me", ""}; }

net::Request analyzeRequest(const Language language, const std::string_view text) {
  net::Request request{net::Method::Post, "/v1/analyze/text",
                       net::JsonObject()
                           .add("text", utf8Prefix(text, config::kMaxAnalyzeTextBytes))
                           .add("language", languageCode(language))
                           .str()};
  request.idempotent = true;  // read-only analysis: a repeat is harmless
  return request;
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
