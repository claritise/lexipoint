#pragma once

// A scripted Lexirise (api::LexiriseApi) for the lookup and card suites: replies queued per call kind
// (the last one repeats), and every call recorded.

#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include "lexirise/api/LexiriseApi.h"

namespace lexipoint::fakes {

inline api::ApiResponse apiOk(std::string body) {
  api::ApiResponse r;
  r.status = 200;
  r.body = std::move(body);
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
  std::deque<api::ApiResponse> lookupReplies;
  std::deque<api::ApiResponse> writeReplies;
  std::vector<std::string> analyzed;  // the sentences
  std::vector<std::string> looked;    // the headwords
  std::vector<net::Request> written;

  api::ApiResponse analyze(Language, const std::string_view sentence) override {
    analyzed.emplace_back(sentence);
    return next(analyzeReplies);
  }
  api::ApiResponse lookup(Language, const std::string_view lemma) override {
    looked.emplace_back(lemma);
    return next(lookupReplies);
  }
  api::ApiResponse write(const net::Request& request) override {
    written.push_back(request);
    return next(writeReplies);
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
