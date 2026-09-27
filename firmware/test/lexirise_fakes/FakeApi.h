#pragma once

// A scripted Lexirise (api::LexiriseApi) for the lookup and card suites: replies queued per call kind
// (the last one repeats), and every call recorded.

#include <algorithm>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "lexirise/api/LexiriseApi.h"

namespace lexipoint::fakes {

inline api::ApiResponse apiOk(std::string body) {
  api::ApiResponse r;
  r.status = 200;
  r.body = std::move(body);
  r.sent = true;
  return r;
}

inline api::ApiResponse apiFailure(const api::ApiError error) {
  api::ApiResponse r;
  r.error = error;
  return r;
}

class FakeApi final : public api::LexiriseApi {
 public:
  std::deque<api::ApiResponse> analyzeReplies;
  std::deque<api::ApiResponse> wordsReplies;  // analyzeWords: none scripted = offline (the refined answer stands)
  std::deque<api::ApiResponse> lookupReplies;
  std::deque<api::ApiResponse> writeReplies;
  std::deque<api::ApiResponse> deckReplies;  // none scripted: offline
  std::deque<api::ApiResponse> itemReplies;  // savedItem: none scripted = offline
  std::vector<std::string> analyzed;         // the sentences
  std::vector<std::string> analyzedWords;
  std::vector<std::string> looked;  // the headwords
  std::vector<net::Request> written;
  std::vector<net::Request> decked;
  std::vector<net::Request> items;  // savedItem's requests
  // vocabularyPage (V7a): `vocabServer` answers each request when set (a fake account paging its items), else the
  // scripted replies; none = offline. A body is streamed into the sink `vocabChunkBytes` at a time, as the client
  // would (and never kept: the response's body is empty); a sink that stops it makes the answer Malformed. A failure
  // with a body is one cut after those bytes (a timeout mid-page).
  std::deque<api::ApiResponse> vocabReplies;
  std::function<api::ApiResponse(const net::Request&)> vocabServer;
  size_t vocabChunkBytes = 7;
  std::vector<net::Request> vocabRequests;

  api::ApiResponse analyze(Language, const std::string_view sentence) override {
    analyzed.emplace_back(sentence);
    return next(analyzeReplies);
  }
  api::ApiResponse analyzeWords(Language, const std::string_view sentence) override {
    analyzedWords.emplace_back(sentence);
    return next(wordsReplies);
  }
  api::ApiResponse lookup(Language, const std::string_view lemma) override {
    looked.emplace_back(lemma);
    return next(lookupReplies);
  }
  api::ApiResponse write(const net::Request& request) override {
    written.push_back(request);
    return next(writeReplies);
  }
  api::ApiResponse deck(const net::Request& request) override {
    decked.push_back(request);
    return next(deckReplies);
  }
  api::ApiResponse savedItem(const net::Request& request) override {
    items.push_back(request);
    return next(itemReplies);
  }
  api::ApiResponse vocabularyPage(const net::Request& request, net::BodySink& sink) override {
    vocabRequests.push_back(request);
    api::ApiResponse r = vocabServer ? vocabServer(request) : next(vocabReplies);
    if (!r.ok() && r.body.empty()) return r;
    const std::string body = std::move(r.body);
    r.body.clear();
    for (size_t at = 0; at < body.size(); at += vocabChunkBytes) {
      if (!sink.onBody(body.data() + at, std::min(vocabChunkBytes, body.size() - at))) {
        r.error = api::ApiError::Malformed;
        break;
      }
    }
    return r;
  }

 private:
  static api::ApiResponse next(std::deque<api::ApiResponse>& replies) {
    if (replies.empty()) return apiFailure(api::ApiError::Network);  // nothing scripted: offline
    api::ApiResponse r = replies.front();
    if (replies.size() > 1) replies.pop_front();
    return r;
  }
};

}  // namespace lexipoint::fakes
