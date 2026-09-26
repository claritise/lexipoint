#pragma once

#include <iterator>

// Lexirise feature constants in one place (docs/v0.1). Tunables of the dev harness
// live separately in src/lexirise/dev/DevConfig.h.

#include <cstddef>
#include <cstdint>

namespace lexipoint::config {

// Settings file on the SD card (settings.md §3). Hidden dot folder: the web file manager refuses it.
constexpr const char* kSettingsDir = "/.lexirise";
constexpr const char* kSettingsPath = "/.lexirise/config.ini";
constexpr const char* kSettingsTmpPath = "/.lexirise/config.ini.tmp";
constexpr const char* kSettingsBackupPath = "/.lexirise/config.ini.bak";
constexpr const char* kSettingsBadPath = "/.lexirise/config.ini.bad";  // an unreadable file, moved aside
constexpr size_t kSettingsMaxBytes = 4096;  // a hand-edited file larger than this is rejected
// Each book's lookup language (languages.md §1, step 3): `<ja|zh>=<book path>` lines, newest last.
constexpr const char* kBookLanguagesPath = "/.lexirise/books.ini";
constexpr const char* kBookLanguagesTmpPath = "/.lexirise/books.ini.tmp";
constexpr const char* kBookLanguagesBackupPath = "/.lexirise/books.ini.bak";
constexpr const char* kBookLanguagesBadPath = "/.lexirise/books.ini.bad";
constexpr size_t kBookLanguagesMax = 100;         // books remembered; setting one more forgets the oldest
constexpr size_t kBookLanguagesMaxBytes = 32768;  // ~100 typical paths; long ones forget the oldest sooner
// Each book tag's title (V2): `<slug>=<title>` lines, newest last, so a saved word's `book:<slug>` can name its book.
constexpr const char* kBookTagsPath = "/.lexirise/book-tags.ini";
constexpr const char* kBookTagsTmpPath = "/.lexirise/book-tags.ini.tmp";
constexpr const char* kBookTagsBackupPath = "/.lexirise/book-tags.ini.bak";
constexpr const char* kBookTagsBadPath = "/.lexirise/book-tags.ini.bad";
constexpr size_t kBookTagsMax = 100;           // books remembered (held in RAM once read); one more forgets the oldest
constexpr size_t kBookTagsMaxBytes = 16384;    // 100 slugs with the longest titles
constexpr size_t kBookTagTitleMaxBytes = 120;  // a title is cut (at a character) to this in the record
// The longest FAT/exFAT long name in UTF-8: 255 UTF-16 units, up to 3 bytes each (web/HiddenPath.h).
constexpr size_t kMaxFatNameBytes = 255 * 3;

// Lexirise API.
constexpr const char* kDefaultBaseUrl = "https://api.lexirise.app";
constexpr const char* kApiKeyPrefix = "lx_";
constexpr size_t kApiKeyMinLength = 20;
constexpr size_t kApiKeyMaxLength = 128;
constexpr size_t kMaskedKeyTail = 3;  // characters of the key shown after the mask (settings.md §2)
constexpr int kMaskedKeyDots = 8;     // "•" characters between the prefix and the tail

// HTTP (lexirise-client.md §1, §4). A response over a limit is treated as malformed (offline-and-errors §1).
constexpr size_t kHttpMaxHeaderBytes = 8192;  // status line + all headers
constexpr size_t kHttpMaxLineBytes = 1024;    // one header line or chunk-size line
constexpr size_t kHttpMaxBodyBytes = 65536;   // decoded body
constexpr int kHttpMaxInterimResponses = 2;   // "100 Continue" and friends before the real response
constexpr size_t kHttpReadChunkBytes = 1024;  // one socket read (on the stack)
constexpr uint32_t kHttpTimeoutMs = 6000;     // connect, handshake, and each read wait
constexpr uint32_t kRetryAfterDefaultS = 60;  // 429 without a usable Retry-After
constexpr uint32_t kRetryAfterMaxS = 3600;    // cap on a server-sent Retry-After
constexpr uint16_t kHttpsPort = 443;
// One whole request (the stale-session retry included) once a connection is open. Opening is bounded
// separately (NTP kNtpWaitMs + TCP and handshake kHttpTimeoutMs each): see kMaxCallMs below.
constexpr uint32_t kRequestDeadlineMs = 15000;
// An idle TLS session is closed after this, so it never sits on internal heap that CrossPoint's TLS users
// (KOSync, fonts, OTA) pre-flight for. Keep-alive still covers a lookup's back-to-back calls.
constexpr unsigned long kTlsIdleCloseMs = 30000;
constexpr const char* kUserAgentProduct = "Lexipoint";
// What the reader calls itself on the network (D22): File Transfer's hotspot, http://lexipoint.local/, and the
// name routers list it under (the prefix, then the WiFi MAC: "Lexipoint-AABBCCDDEEFF").
constexpr const char* kHotspotSsid = "Lexipoint";
constexpr const char* kMdnsHostname = "lexipoint";
constexpr char kDhcpHostnamePrefix[] = "Lexipoint-";
// The OTA updater's release feed: Lexipoint's own releases (firmware-base.md §6), never CrossPoint's, whose
// firmware would uninstall Lexipoint. Assets are named lexipoint-<tag>-x4pro.bin (scripts/lexipoint/
// publish_release.py, release_tag.py ASSET_PREFIX).
constexpr const char* kReleasesLatestUrl = "https://api.github.com/repos/claritise/lexipoint/releases/latest";
constexpr const char* kReleaseAssetPrefix = "lexipoint-";
#ifdef LEXIPOINT_VERSION
constexpr const char* kLexipointVersion = LEXIPOINT_VERSION;  // platformio.ini [lexirise]
#else
constexpr const char* kLexipointVersion = "dev";  // host tests
#endif
constexpr size_t kMaxAnalyzeTextBytes = 2048;  // one sentence with context (sentence-extraction.md)

// TLS session (net/TlsConnection). Certificate dates need a real clock: before the first NTP sync the
// clock reads 1970, so a connection first waits for SNTP (lexirise-client.md §1).
constexpr long kMinValidEpochS = 1767225600;  // 2026-01-01T00:00:00Z: anything earlier is "clock not set"
constexpr uint32_t kNtpWaitMs = 5000;
constexpr uint32_t kClockPollMs = 100;
constexpr const char* kNtpServerPrimary = "pool.ntp.org";
constexpr const char* kNtpServerSecondary = "time.nist.gov";
constexpr uint32_t kIoPollMs = 5;  // sleep between non-blocking socket polls

// WiFi for lookups (offline-and-errors.md §5, net/WifiLease.h): join the last-used saved network from
// radio-off only, never open the UI.
// A join goes straight to the last access point first when there's a hint (net/WifiHint.h, P11), then scans
// every channel: measured on the X4 Pro, the scan alone is ~3.5 s, and 6 s ran out twice (device-checks.md).
constexpr uint32_t kWifiDirectJoinMs = 3000;
constexpr uint32_t kWifiConnectMs = 8000;                                // the all-channel scan and join
constexpr uint32_t kWifiJoinMaxMs = kWifiDirectJoinMs + kWifiConnectMs;  // from one clock, radio restarts included
constexpr uint32_t kWifiStopWaitMs = 300;  // for the station to report stopped (its events come from another task)
// Starting the station (WiFi.mode / begin) can block ~1 s each between the join clock's checks: counted in
// kMaxCallMs, so a waiter (lxctl, the web page's key check) never gives up on a join that's still in time.
constexpr uint32_t kWifiRadioSlackMs = 2000;
constexpr uint32_t kWifiPollMs = 50;
constexpr unsigned long kMsPerMinute = 60UL * 1000UL;

// Sentence extraction (sentence-extraction.md §2).
constexpr size_t kMaxSentenceUnits = 120;  // cap in UTF-16 units (Lexirise's), centred on the tap
// Paragraph starts, read off line geometry (the laid-out page doesn't mark them): text/ParagraphBreaks.h.
constexpr float kParagraphShortLineEm = 2.0f;  // the line before stops at least this far from the right edge
constexpr float kParagraphGapFactor = 1.3f;    // or the gap to this line is this much above the usual line advance
constexpr float kParagraphIndentEm = 0.5f;     // or this line is indented (and the one before wasn't)

// Lookups (lookup-flow.md §4-5).
constexpr size_t kMaxTranslations = 2;         // senses kept for the card (phase B)
constexpr size_t kMaxTranslationBytes = 512;   // one sense's text; longer is cut at a character boundary
constexpr size_t kStarDictMaxPrefixChars = 8;  // CJK longest-prefix probe for StarDict: 8, 7, … 1 characters

// The card's rank words (popup-ui.md §1, languages.md §6): below each threshold, that band; past the
// last, "rare". Per language: Chinese ranks run higher for words as common (tuned in P5 on sampled ranks,
// lexirise-api-notes.md: 景色 #2,643 ja / #8,123 zh; HSK 6 words reach ~18k).
constexpr uint32_t kRankBandLimitsJa[] = {1000, 5000, 20000};
constexpr uint32_t kRankBandLimitsZh[] = {1000, 10000, 30000};  // 1000: the reference's 选择 #1,113 is common
static_assert(std::size(kRankBandLimitsJa) == std::size(kRankBandLimitsZh), "one set of band names");
constexpr size_t kRankBands = std::size(kRankBandLimitsJa) + 1;  // + "rare"

// The card (popup-ui.md §2, §3.2).
constexpr unsigned long kToastMs = 2000;         // "Saved as learning · Undo"
constexpr unsigned long kFailureToastMs = 6000;  // "Save failed · Retry": it comes late, the eyes are elsewhere
constexpr unsigned long kPhaseMergeMs = 300;     // phase B this soon after A: one refresh for both
constexpr int kCardHalfRefreshEvery = 5;         // the 5th card's dismiss: a half refresh (ghosts), as the reader's
constexpr int kCardPendingInputMax = 4;          // input read while a card refresh runs, handled after it
// P9: buttons are only read between loop passes, and the next sentence's analysis blocks the loop (~1 s): a
// press made during it is first seen ~20-30 ms after the jump (two debounced polls, 10 ms loop passes). A forward press
// stamped within this of the jump was made before it and is dropped; a real new one comes after the new frame (a
// refresh, ~0.5 s) and a human's reaction.
constexpr unsigned long kStepAfterJumpGraceMs = 100;
constexpr int kCardSwipeEdgeMarginPx = 85;  // ~10 mm on the X4 Pro's ~217 ppi panel (popup-ui.md §3.2)
// The bench (P4) plays the phases on a timer, as a lookup would fill them.
constexpr unsigned long kBenchPhaseAMs = 250;         // tap → analyzed
constexpr unsigned long kBenchPhaseBMs = 900;         // tap → translated
constexpr unsigned long kBenchNextSentenceMs = 1000;  // P9: a step past the end → the "next sentence" came

// Response limits (lexirise-client.md §4): past these a response is treated as malformed.
constexpr size_t kMaxOccurrences = 128;
// entryMetaById / stateByEntryId entries kept per analyze (surface, lemma and breakdown entries, so more
// than the occurrences). Past it, further entries are dropped, not failed on: the tapped word's entry
// is usually among the first.
constexpr size_t kMaxEntries = 512;
constexpr uint32_t kMaxProficiency = 4;       // stateByEntryId proficiency is 0-4
constexpr size_t kMaxTokenBytes = 256;        // one word / lemma / reading
constexpr size_t kLoggedBodyBytes = 128;      // of a response we couldn't read (it holds no key)
constexpr size_t kMaxSavedIdBytes = 64;       // a saved-expression id (it goes into a request path)
constexpr size_t kMaxScoreChars = 24;         // a frequency_score as JSON text ("0.4861234")
constexpr size_t kMaxDisplayFieldBytes = 64;  // /v1/me user.name and user.plan
constexpr size_t kJsonMaxDepth = 12;          // Lexirise responses nest ~5 deep; the reader recurses

// Settings value bounds.
constexpr int kWifiIdleChoicesMin[] = {0, 1, 2, 5, 10};  // "Keep WiFi on after a lookup" (0 = off)
constexpr int kWifiIdleDefaultMin = 5;
constexpr size_t kMaxTags = 8;
constexpr size_t kMaxTagLength = 40;
constexpr size_t kMaxDictionaryNameLength = 64;
constexpr size_t kMaxBaseUrlLength = 128;
constexpr const char* kDefaultTags = "xteink";
// The book tag (C2, V2): kBookTagPrefix + the title's slug (text/BookSlug.h). Tag names can't be deleted from an
// account, so it's one per book, and whole within a user tag's length.
constexpr char kBookTagPrefix[] = "book:";
constexpr size_t kBookSlugMaxBytes = kMaxTagLength - (sizeof(kBookTagPrefix) - 1);
constexpr size_t kBookSlugMinAlnum = 3;  // fewer ASCII letters and digits (a Japanese title): a hash instead

// The longest one Lexirise call can block (WiFi join, NTP, TCP + handshake, the request). The web page
// polls a queued key check for this long, and lxctl's LEXI wait is checked against it (test_lxctl).
constexpr uint32_t kMaxCallMs =
    kWifiJoinMaxMs + kWifiRadioSlackMs + kNtpWaitMs + 2 * kHttpTimeoutMs + kRequestDeadlineMs;

}  // namespace lexipoint::config
