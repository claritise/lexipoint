#if LEXIRISE

#include "Requests.h"

#include "lexirise/LexiriseConfig.h"
#include "lexirise/net/JsonWriter.h"

namespace lexipoint::api {
namespace {

// Longest prefix of at most maxBytes that doesn't split a UTF-8 sequence.
std::string_view utf8Prefix(const std::string_view text, const size_t maxBytes) {
  if (text.size() <= maxBytes) return text;
  size_t end = maxBytes;
  while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) end--;
  return text.substr(0, end);
}

}  // namespace

net::Request meRequest() { return {net::Method::Get, "/v1/me", ""}; }

net::Request analyzeRequest(const Language language, const std::string_view text) {
  return {net::Method::Post, "/v1/analyze/text",
          net::JsonObject()
              .add("text", utf8Prefix(text, config::kMaxAnalyzeTextBytes))
              .add("language", languageCode(language))
              .str()};
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
