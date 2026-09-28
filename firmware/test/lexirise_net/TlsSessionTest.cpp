// TLS session resumption's policy (net/TlsSession.h, V7c): what's offered, kept, dropped, and the full handshake a
// failed resumption falls back to. Sessions are fake handles; the real ones are WOLFSSL_SESSION* (TlsConnection.cpp).

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>
#include <vector>

#include "lexirise/net/TlsSession.h"

using lexipoint::net::Attempted;
using lexipoint::net::keepOnClose;
using lexipoint::net::OpenError;
using lexipoint::net::openResuming;
using lexipoint::net::SessionKeeper;

namespace {
bool budget = true;  // the open's budget has room for the fallback
bool timeLeft() { return budget; }
}  // namespace

namespace {

std::vector<void*> released;
void release(void* session) { released.push_back(session); }

int sessionA = 1;
int sessionB = 2;
constexpr const char* kHost = "api.lexirise.app";
constexpr uint16_t kPort = 443;

// A scripted handshake: what each attempt answers, and what it was offered. A session given is offered (wolfSSL takes
// it) unless `refuse`; a failure before the handshake (the TCP connect, memory, input) offers nothing.
struct Handshakes {
  explicit Handshakes(std::vector<OpenError> scripted, const bool refuse = false)
      : answers(std::move(scripted)), refuse(refuse) {}
  std::vector<OpenError> answers;
  bool refuse;
  std::vector<void*> offered;
  Attempted operator()(void* session) {
    offered.push_back(session);
    const OpenError e = answers.at(offered.size() - 1);
    const bool before = e == OpenError::ConnectFailed || e == OpenError::LowMemory || e == OpenError::Aborted;
    Attempted a;
    a.error = e;
    a.refused = session && refuse && !before;
    a.offered = session && !refuse && !before;
    return a;
  }
};

class TlsSessionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    released.clear();
    budget = true;
  }
  SessionKeeper keeper{release};
};

TEST_F(TlsSessionTest, TheFirstHandshakeIsFullAndItsSessionIsOfferedNext) {
  Handshakes h{{OpenError::None, OpenError::None}};
  EXPECT_EQ(openResuming(keeper, kHost, kPort, [&](void* s) { return h(s); }, timeLeft), OpenError::None);
  keeper.keep(kHost, kPort, &sessionA);  // as the connection closes
  EXPECT_EQ(openResuming(keeper, kHost, kPort, [&](void* s) { return h(s); }, timeLeft), OpenError::None);
  EXPECT_EQ(h.offered, (std::vector<void*>{nullptr, &sessionA}));
  EXPECT_TRUE(released.empty());
}

TEST_F(TlsSessionTest, ARefusedResumptionIsAFullHandshakeInTheSameExchange) {
  // The server doesn't take the ticket: the handshake still succeeds (full), and its new session replaces the old.
  keeper.keep(kHost, kPort, &sessionA);
  Handshakes h{{OpenError::None}};
  EXPECT_EQ(openResuming(keeper, kHost, kPort, [&](void* s) { return h(s); }, timeLeft), OpenError::None);
  EXPECT_EQ(h.offered.size(), 1u);
  keeper.keep(kHost, kPort, &sessionB);
  EXPECT_EQ(released, std::vector<void*>{&sessionA});
  EXPECT_EQ(keeper.offer(kHost, kPort), &sessionB);
}

TEST_F(TlsSessionTest, AFailedResumptionFallsBackToAFullHandshakeTransparently) {
  keeper.keep(kHost, kPort, &sessionA);
  Handshakes h{{OpenError::TlsFailed, OpenError::None}};
  EXPECT_EQ(openResuming(
                keeper, kHost, kPort, [&](void* s) { return h(s); }, timeLeft),
            OpenError::None);  // the call never sees it
  EXPECT_EQ(h.offered, (std::vector<void*>{&sessionA, nullptr}));
  EXPECT_EQ(released, std::vector<void*>{&sessionA});  // dropped before the full handshake
  EXPECT_FALSE(keeper.holds());
}

TEST_F(TlsSessionTest, AFallbackPastTheBudgetIsntTried) {
  // The resumed handshake failed late in the open's budget: a full one would run past the call's bound (kMaxCallMs).
  keeper.keep(kHost, kPort, &sessionA);
  budget = false;
  Handshakes h{{OpenError::TlsFailed, OpenError::None}};
  EXPECT_EQ(openResuming(keeper, kHost, kPort, [&](void* s) { return h(s); }, timeLeft), OpenError::TlsFailed);
  EXPECT_EQ(h.offered, std::vector<void*>{&sessionA});
  EXPECT_FALSE(keeper.holds());  // dropped: the next open is full
}

TEST_F(TlsSessionTest, AFullHandshakeThatFailsIsntRetried) {
  Handshakes h{{OpenError::TlsFailed}};
  EXPECT_EQ(openResuming(keeper, kHost, kPort, [&](void* s) { return h(s); }, timeLeft), OpenError::TlsFailed);
  EXPECT_EQ(h.offered.size(), 1u);
}

TEST_F(TlsSessionTest, AFailedFallbackStillFailsAndKeepsNothing) {
  keeper.keep(kHost, kPort, &sessionA);
  Handshakes h{{OpenError::TlsFailed, OpenError::TlsFailed}};
  EXPECT_EQ(openResuming(keeper, kHost, kPort, [&](void* s) { return h(s); }, timeLeft), OpenError::TlsFailed);
  EXPECT_EQ(h.offered.size(), 2u);
  EXPECT_FALSE(keeper.holds());
}

TEST_F(TlsSessionTest, ATimedOutHandshakeDropsTheSessionWithoutDoublingTheWait) {
  keeper.keep(kHost, kPort, &sessionA);
  Handshakes h{{OpenError::Timeout}};
  EXPECT_EQ(openResuming(keeper, kHost, kPort, [&](void* s) { return h(s); }, timeLeft), OpenError::Timeout);
  EXPECT_EQ(h.offered.size(), 1u);
  EXPECT_EQ(released, std::vector<void*>{&sessionA});
}

TEST_F(TlsSessionTest, FailuresBeforeTheHandshakeKeepIt) {
  for (const OpenError e : {OpenError::ConnectFailed, OpenError::LowMemory, OpenError::Aborted}) {
    keeper.keep(kHost, kPort, &sessionA);
    released.clear();
    Handshakes h{{e}};
    EXPECT_EQ(openResuming(keeper, kHost, kPort, [&](void* s) { return h(s); }, timeLeft), e);
    EXPECT_TRUE(keeper.holds());
    EXPECT_TRUE(released.empty());
  }
}

TEST_F(TlsSessionTest, AnotherHostDropsTheSessionAndIsOfferedNone) {
  keeper.keep(kHost, kPort, &sessionA);
  Handshakes h{{OpenError::None}};
  EXPECT_EQ(
      openResuming(keeper, "staging.example.test", kPort, [&](void* s) { return h(s); }, timeLeft), OpenError::None);
  EXPECT_EQ(h.offered, std::vector<void*>{nullptr});
  EXPECT_EQ(released, std::vector<void*>{&sessionA});
}

TEST_F(TlsSessionTest, AnotherPortDropsTheSessionToo) {
  keeper.keep(kHost, kPort, &sessionA);
  Handshakes h{{OpenError::None}};
  EXPECT_EQ(openResuming(keeper, kHost, 8443, [&](void* s) { return h(s); }, timeLeft), OpenError::None);
  EXPECT_EQ(h.offered, std::vector<void*>{nullptr});
  EXPECT_EQ(released, std::vector<void*>{&sessionA});
}

TEST_F(TlsSessionTest, AResumedConnectionClosedBeforeANewTicketKeepsNothing) {
  // Resumed with A, given up right after its handshake: the connection's own copy of A (wolfSSL duplicates a session
  // the keeper holds) now holds the old ticket and the new secret, so it isn't kept; the keeper's A goes with the keep.
  keeper.keep(kHost, kPort, &sessionA);
  Handshakes h{{OpenError::None, OpenError::None}};
  EXPECT_EQ(openResuming(keeper, kHost, kPort, [&](void* s) { return h(s); }, timeLeft), OpenError::None);
  keeper.keep(kHost, kPort, keepOnClose(32, /*ticketArrivedHere=*/false) ? &sessionA : nullptr);  // as close() does
  EXPECT_EQ(released, std::vector<void*>{&sessionA});
  EXPECT_FALSE(keeper.holds());
  EXPECT_EQ(openResuming(keeper, kHost, kPort, [&](void* s) { return h(s); }, timeLeft), OpenError::None);
  EXPECT_EQ(h.offered, (std::vector<void*>{&sessionA, nullptr}));  // a full handshake next, never a failed resumption
}

TEST_F(TlsSessionTest, ASessionWolfSSLRefusesIsDroppedAndNotRetried) {
  keeper.keep(kHost, kPort, &sessionA);
  Handshakes ok{{OpenError::None}, /*refuse=*/true};
  EXPECT_EQ(openResuming(keeper, kHost, kPort, [&](void* s) { return ok(s); }, timeLeft), OpenError::None);
  EXPECT_EQ(released, std::vector<void*>{&sessionA});
  EXPECT_FALSE(keeper.holds());
  keeper.keep(kHost, kPort, &sessionB);
  Handshakes failed{{OpenError::TlsFailed}, /*refuse=*/true};
  EXPECT_EQ(openResuming(keeper, kHost, kPort, [&](void* s) { return failed(s); }, timeLeft), OpenError::TlsFailed);
  EXPECT_EQ(failed.offered.size(), 1u);  // a full handshake already: no retry
}

TEST_F(TlsSessionTest, AHandshakeGivenUpMidwayDropsTheSessionItWasOffered) {
  // The reader's input inside the handshake: the session may already hold this handshake's new secret.
  keeper.keep(kHost, kPort, &sessionA);
  int attempts = 0;
  const OpenError e = openResuming(
      keeper, kHost, kPort,
      [&](void*) {
        attempts++;
        Attempted a;
        a.error = OpenError::Aborted;
        a.offered = true;
        return a;
      },
      timeLeft);
  EXPECT_EQ(e, OpenError::Aborted);
  EXPECT_EQ(attempts, 1);  // given up: no retry
  EXPECT_EQ(released, std::vector<void*>{&sessionA});
  EXPECT_FALSE(keeper.holds());
}

TEST_F(TlsSessionTest, ARejectedCertificateIsntRetried) {
  // A resumed handshake verifies no certificate: one rejected means the server did a full handshake (a captive portal,
  // an interception); another full one would be rejected the same way.
  keeper.keep(kHost, kPort, &sessionA);
  int attempts = 0;
  const OpenError e = openResuming(
      keeper, kHost, kPort,
      [&](void*) {
        attempts++;
        Attempted a;
        a.error = OpenError::TlsFailed;
        a.offered = true;
        a.certificateRejected = true;
        return a;
      },
      timeLeft);
  EXPECT_EQ(e, OpenError::TlsFailed);
  EXPECT_EQ(attempts, 1);
  EXPECT_FALSE(keeper.holds());
}

TEST_F(TlsSessionTest, TheSameSessionKeptAgainReleasesOneReference) {
  // A resumed connection's session is the object offered: wolfSSL_get1_session hands another reference, one goes.
  keeper.keep(kHost, kPort, &sessionA);
  keeper.keep(kHost, kPort, &sessionA);
  EXPECT_EQ(released, std::vector<void*>{&sessionA});
  EXPECT_EQ(keeper.offer(kHost, kPort), &sessionA);
}

TEST_F(TlsSessionTest, AConnectionWithoutATicketLeavesNothingToOffer) {
  keeper.keep(kHost, kPort, &sessionA);
  keeper.keep(kHost, kPort, nullptr);
  EXPECT_FALSE(keeper.holds());
  EXPECT_EQ(keeper.offer(kHost, kPort), nullptr);
}

TEST(TlsSession, TheKeeperReleasesItsSessionWhenItGoes) {
  released.clear();
  {
    SessionKeeper keeper(release);
    keeper.keep(kHost, kPort, &sessionB);
  }
  EXPECT_EQ(released, std::vector<void*>{&sessionB});
}

}  // namespace

TEST(TlsSession, OnlyATicketReadOnThisConnectionIsKept) {
  EXPECT_TRUE(keepOnClose(32, true));    // the API's tickets (measured), read on this connection
  EXPECT_TRUE(keepOnClose(4096, true));  // a large stateless ticket too: wolfSSL holds it on the heap
  EXPECT_FALSE(keepOnClose(0, true));    // no ticket: nothing to resume with
  EXPECT_FALSE(keepOnClose(32, false));  // an old ticket with this handshake's new secret: can't resume
}
