// When Lexirise may be asked (AccessPolicy.h, offline-and-errors.md §1, §3).

#include <gtest/gtest.h>

#include "lexirise/api/AccessPolicy.h"

using lexipoint::api::AccessPolicy;
using lexipoint::api::ApiError;
using lexipoint::api::ApiResponse;
using Block = AccessPolicy::Block;

namespace {

ApiResponse response(const ApiError error, const int status, const uint32_t retryAfterS = 0) {
  ApiResponse r;
  r.error = error;
  r.status = status;
  r.retryAfterS = retryAfterS;
  return r;
}

}  // namespace

TEST(AccessPolicy, ARejectedKeyStaysOffUntilANewKeyOrAGoodAnswer) {
  AccessPolicy p;
  EXPECT_EQ(p.blocked(0), Block::None);
  p.observe(response(ApiError::Unauthorized, 401), 0);
  EXPECT_EQ(p.blocked(10'000'000), Block::Rejected);  // until reboot…
  EXPECT_EQ(p.refusal(1).error, ApiError::Unauthorized);
  p.reset();  // …or a new key
  EXPECT_EQ(p.blocked(1), Block::None);
  p.observe(response(ApiError::Unauthorized, 403), 0);
  p.observe(response(ApiError::None, 200), 5);  // a key check that got through: the key works again
  EXPECT_EQ(p.blocked(6), Block::None);
}

TEST(AccessPolicy, ARateLimitBacksOffForItsRetryAfter) {
  AccessPolicy p;
  p.observe(response(ApiError::RateLimited, 429, 30), 1000);
  EXPECT_EQ(p.blocked(1000), Block::RateLimited);
  EXPECT_EQ(p.retryInS(1000), 30u);
  EXPECT_EQ(p.retryInS(1500), 30u);  // rounded up
  EXPECT_EQ(p.refusal(1500).retryAfterS, 30u);
  EXPECT_EQ(p.blocked(31'000), Block::None);
  EXPECT_EQ(p.retryInS(31'000), 0u);
}

TEST(AccessPolicy, ARateLimitWithoutRetryAfterAndAnAbsurdOneAreBounded) {
  AccessPolicy p;
  p.observe(response(ApiError::RateLimited, 429, 0), 0);
  EXPECT_EQ(p.retryInS(0), lexipoint::config::kRetryAfterDefaultS);
  p.observe(response(ApiError::RateLimited, 429, 999'999), 0);
  EXPECT_EQ(p.retryInS(0), lexipoint::config::kRetryAfterMaxS);
}

TEST(AccessPolicy, OtherFailuresChangeNothing) {
  AccessPolicy p;
  for (const ApiError e : {ApiError::NoWifi, ApiError::Timeout, ApiError::Server, ApiError::Malformed}) {
    p.observe(response(e, e == ApiError::Server ? 503 : 0), 0);
    EXPECT_EQ(p.blocked(0), Block::None);
  }
}

TEST(AccessPolicy, TheBackOffSurvivesTheMillisWrap) {
  AccessPolicy p;
  p.observe(response(ApiError::RateLimited, 429, 10), 0xFFFFF000UL);
  EXPECT_EQ(p.blocked(0x00000100UL), Block::RateLimited);  // 0x1100 ms later, across the wrap
  EXPECT_EQ(p.retryInS(0x00000100UL), 6u);                 // 10 s − 4.35 s, rounded up
  EXPECT_EQ(p.blocked(0x00002000UL + 10'000UL), Block::None);
}

TEST(AccessPolicy, EachNewBlockIsAnnouncedOnce) {
  AccessPolicy p;
  EXPECT_EQ(p.takeUnannounced(0), Block::None);
  p.observe(response(ApiError::Unauthorized, 401), 0);  // e.g. the web page's key check
  EXPECT_EQ(p.takeUnannounced(1), Block::Rejected);
  EXPECT_EQ(p.takeUnannounced(2), Block::None);         // told once
  p.observe(response(ApiError::Unauthorized, 401), 3);  // the same rejection again: nothing new
  EXPECT_EQ(p.takeUnannounced(4), Block::None);
  AccessPolicy q;
  q.observe(response(ApiError::RateLimited, 429, 10), 0);
  EXPECT_EQ(q.takeUnannounced(20'000), Block::None);  // over before anyone looked: nothing to say
}

TEST(AccessPolicy, ASecondRejectionBeforeAnyoneWasToldKeepsTheNotice) {
  AccessPolicy p;
  p.observe(response(ApiError::Unauthorized, 401), 0);  // the web page's Test…
  p.observe(response(ApiError::Unauthorized, 401), 1);  // …pressed again
  EXPECT_EQ(p.takeUnannounced(2), Block::Rejected);
}

TEST(AccessPolicy, AnEndedBackOffIsForgottenSoItCantComeBack) {
  AccessPolicy p;
  p.observe(response(ApiError::RateLimited, 429, 10), 0);
  EXPECT_EQ(p.blocked(10'000), Block::None);
  EXPECT_EQ(p.blocked(10'000 + 0x80000000UL), Block::None);  // 2^31 ms later: still over
}
