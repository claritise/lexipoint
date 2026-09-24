#include <gtest/gtest.h>

#include "lexirise/web/Origin.h"

using lexipoint::web::isSameOriginRequest;
using lexipoint::web::isTrustedHost;

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

TEST(Origin, TrustedHostsAreIpLiteralsAndMdnsNames) {
  for (const char* ok :
       {"192.168.1.5", "192.168.4.1:80", "10.0.0.2:8080", "CrossPoint-Reader.local", "crosspoint.local:80"}) {
    EXPECT_TRUE(isTrustedHost(ok)) << ok;
  }
  for (const char* bad : {"", "evil.example", "evil.example:80", "192.168.1", "192.168.1.256", "1.2.3.4.5", "a.b.local",
                          ".local", "-x.local", "192.168.1.5:", "192.168.1.5:123456", "x.local:ab", "[::1]", "local"}) {
    EXPECT_FALSE(isTrustedHost(bad)) << bad;
  }
}
