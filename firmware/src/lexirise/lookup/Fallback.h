#pragma once

// Whether a lookup asks Lexirise, and what the user is told when it doesn't answer (offline-and-errors.md
// §1-2). Pure; word select acts on it. Tests: test/lexirise_lookup.

#include <cstdint>

#include "lexirise/api/AccessPolicy.h"
#include "lexirise/settings/Settings.h"
#include "lexirise/text/BookLanguage.h"

namespace lexipoint::lookup {

// Asked, or why not: switched off (or not for this book's language), no key, a rejected key (until
// reboot or a new key), a rate limit's back-off.
enum class Gate : uint8_t { Ask, Off, NoKey, Rejected, RateLimited };

inline Gate lexiriseGate(const Settings& settings, const text::BookLanguage& book,
                         const api::AccessPolicy::Block block) {
  if (!book.mayUseLexirise(settings)) return Gate::Off;
  if (!settings.hasApiKey()) return Gate::NoKey;
  if (block == api::AccessPolicy::Block::Rejected) return Gate::Rejected;
  if (block == api::AccessPolicy::Block::RateLimited) return Gate::RateLimited;
  return Gate::Ask;
}

// A short message before StarDict answers (the notices are shown once: a rejected key or a rate limit
// when it happens, then Lexirise is skipped quietly; the missing key once per boot).
enum class Notice : uint8_t { None, NoKey, KeyRejected, RateLimited, SaveFailed };

struct Fallback {
  Notice notice = Notice::None;
  bool offline = false;  // StarDict's answer carries the `offline` mark: Lexirise couldn't be reached
};

// What a lookup that doesn't ask Lexirise says first: a block the reader hasn't been told about (found by
// a key check on the web page, say), or, with no StarDict to answer instead, the block every time (it
// says more than "No dictionary set"); the missing key once per boot (`noKeyNotice`).
inline Fallback gateFallback(const Gate gate, const bool noKeyNotice, const api::AccessPolicy::Block unannounced,
                             const bool starDictSet) {
  Fallback f;
  if (gate == Gate::NoKey && noKeyNotice) f.notice = Notice::NoKey;
  const bool tell = unannounced != api::AccessPolicy::Block::None || !starDictSet;
  if (gate == Gate::Rejected && tell) f.notice = Notice::KeyRejected;
  if (gate == Gate::RateLimited && tell) f.notice = Notice::RateLimited;
  return f;
}

// Why the card handed back (LiveOutcome's error).
inline Fallback fallbackFor(const api::ApiError error) {
  switch (error) {
    case api::ApiError::Unauthorized:
      return {Notice::KeyRejected, false};
    case api::ApiError::RateLimited:
      return {Notice::RateLimited, false};
    case api::ApiError::NoWifi:
    case api::ApiError::Network:
    case api::ApiError::Timeout:
    case api::ApiError::ClockNotSet:
    case api::ApiError::Server:
      return {Notice::None, true};  // "this isn't your Lexirise data: saved state unknown" (§2)
    default:
      return {};  // not configured, TLS, low memory, a bad response: StarDict, unmarked (the log says why)
  }
}

// What word select says when the card closed with a save it couldn't send (a Retry still waiting out a
// 429, WiFi gone): the card can't say it any more, and a save never fails silently (§0).
inline Notice noticeForUnsentSave(const api::ApiError error) {
  if (error == api::ApiError::Unauthorized) return Notice::KeyRejected;
  if (error == api::ApiError::RateLimited) return Notice::RateLimited;
  return Notice::SaveFailed;
}

// What word select does with a Fallback: a notice first (then StarDict, when there's one, once it has
// been read), or StarDict at once, or "No dictionary set"; StarDict's answer carries the offline mark.
struct FallbackPlan {
  Notice notice = Notice::None;  // shown first (1.5 s)
  bool starDict = false;         // StarDict answers (after the notice, when there's one)
  bool noDictionary = false;     // "No dictionary set" (no notice, no StarDict)
  bool offline = false;          // StarDict's title gets `· offline`
};
inline FallbackPlan planFallback(const Fallback f, const bool starDictSet) {
  FallbackPlan p;
  p.notice = f.notice;
  p.starDict = starDictSet;
  p.noDictionary = !starDictSet && f.notice == Notice::None;
  p.offline = starDictSet && f.offline;
  return p;
}

}  // namespace lexipoint::lookup
