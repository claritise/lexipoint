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
  // A saved word's item (savedItemRequest, read-only): "Met before"'s sentence and book tag (C14).
  virtual ApiResponse savedItem(const net::Request& request) = 0;
  // A /v1/decks call for the book's deck (deckListRequest, deckRequest, createDeckRequest).
  virtual ApiResponse deck(const net::Request& request) = 0;
  // A page of the user's vocabulary (vocabularyPageRequest, V7a's mirror), its body streamed into `sink` (never
  // held: up to ~1.35 MB for 200 items). Only over WiFi that is already up: it never brings WiFi up (NoWifi).
  // `abort` (optional, V7b) is asked in every wait of the call: true gives it up (Cancelled).
  virtual ApiResponse vocabularyPage(const net::Request& request, net::BodySink& sink, net::Abort abort) = 0;
  // A page's analyze/text (analyzePageRequest, V7b), its body streamed into `sink` (76-93 KB, measured). Like
  // vocabularyPage, only over WiFi already up (never a join: NoWifi); `abort` (optional) is asked in every wait of
  // the call, and true gives it up (Cancelled).
  virtual ApiResponse analyzePage(const net::Request& request, net::BodySink& sink, net::Abort abort) = 0;
};

}  // namespace lexipoint::api
