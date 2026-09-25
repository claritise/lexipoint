#pragma once

// The Lexirise calls a lookup and the card's saves make, as an interface: LexiriseService on the device, a fake in
// tests (test/lexirise_lookup).

#include <string_view>

#include "LexiriseClient.h"
#include "lexirise/settings/Settings.h"

namespace lexipoint::api {

class LexiriseApi {
 public:
  virtual ~LexiriseApi() = default;
  virtual ApiResponse analyze(Language language, std::string_view sentence) = 0;
  // The same sentence's word-level split (analyzeWordsRequest): asked only when analyze's answer came back
  // already refined (lookup::wholeWords).
  virtual ApiResponse analyzeWords(Language language, std::string_view sentence) = 0;
  virtual ApiResponse lookup(Language language, std::string_view lemma) = 0;
  // A /v1/vocabulary write (saveRequest, setProficiencyRequest, removeRequest, clearRequest).
  virtual ApiResponse write(const net::Request& request) = 0;
};

}  // namespace lexipoint::api
