#include <gtest/gtest.h>

#include "lexirise/web/Origin.h"

using lexipoint::web::isSameOriginRequest;

TEST(Origin, NoOriginIsANonBrowserClient) { EXPECT_TRUE(isSameOriginRequest("", "192.168.1.5")); }

TEST(Origin, SameHostIsAllowed) {
  EXPECT_TRUE(isSameOriginRequest("http://192.168.1.5", "192.168.1.5"));
  EXPECT_TRUE(isSameOriginRequest("http://192.168.1.5:80", "192.168.1.5"));
  EXPECT_TRUE(isSameOriginRequest("http://CrossPoint.local", "crosspoint.local:80"));
}

TEST(Origin, OtherSitesAreRejected) {
  for (const char* origin : {"http://evil.example", "https://192.168.1.5", "null", "http://192.168.1.50",
                             "http://192.168.1.5:8080", "http://192.168.1.5.evil.example"}) {
    EXPECT_FALSE(isSameOriginRequest(origin, "192.168.1.5")) << origin;
  }
  EXPECT_FALSE(isSameOriginRequest("http://192.168.1.5", ""));
}
