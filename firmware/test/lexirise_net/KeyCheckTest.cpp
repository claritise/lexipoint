#include <gtest/gtest.h>

#include "lexirise/api/KeyCheck.h"

using lexipoint::api::ApiError;
using lexipoint::api::ApiResponse;
using lexipoint::api::KeyState;
using lexipoint::api::keyStatusFrom;

namespace {
ApiResponse response(ApiError error, std::string body = "") {
  ApiResponse r;
  r.error = error;
  r.body = std::move(body);
  return r;
}
}  // namespace

TEST(KeyCheck, ConnectedCarriesNameAndPlan) {
  const auto s =
      keyStatusFrom(response(ApiError::None, R"({"user":{"name":"R","plan":"pro"},"apiKey":{"rateLimitMax":1200}})"));
  EXPECT_EQ(s.state, KeyState::Connected);
  EXPECT_EQ(s.me.name, "R");
  EXPECT_EQ(s.me.plan, "pro");
}

TEST(KeyCheck, UnreadableSuccessIsAnError) {
  const auto s = keyStatusFrom(response(ApiError::None, "<html>"));
  EXPECT_EQ(s.state, KeyState::Error);
  EXPECT_EQ(s.error, ApiError::Malformed);
}

TEST(KeyCheck, MapsEveryError) {
  const std::pair<ApiError, KeyState> cases[] = {
      {ApiError::NotConfigured, KeyState::NoKey}, {ApiError::Unauthorized, KeyState::Rejected},
      {ApiError::Network, KeyState::Offline},     {ApiError::Timeout, KeyState::Offline},
      {ApiError::ClockNotSet, KeyState::Offline}, {ApiError::Tls, KeyState::Error},
      {ApiError::LowMemory, KeyState::Error},     {ApiError::RateLimited, KeyState::Error},
      {ApiError::Server, KeyState::Error},        {ApiError::Http, KeyState::Error},
      {ApiError::Malformed, KeyState::Error}};
  for (const auto& [error, state] : cases) {
    EXPECT_EQ(keyStatusFrom(response(error)).state, state) << lexipoint::api::apiErrorName(error);
  }
}

TEST(KeyCheck, StateNamesAreStable) {
  EXPECT_STREQ(lexipoint::api::keyStateName(KeyState::Connected), "connected");
  EXPECT_STREQ(lexipoint::api::keyStateName(KeyState::Rejected), "rejected");
  EXPECT_STREQ(lexipoint::api::keyStateName(KeyState::Unchecked), "unchecked");
}
