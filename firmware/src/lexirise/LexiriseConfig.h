#pragma once

// Lexirise feature constants in one place (docs/v0.1 in the lexipoint repo). Tunables of the dev harness
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
// An idle TLS session is closed after this, so it never sits on internal heap that upstream TLS users
// (KOSync, fonts, OTA) pre-flight for. Keep-alive still covers a lookup's back-to-back calls.
constexpr unsigned long kTlsIdleCloseMs = 30000;
constexpr const char* kUserAgentProduct = "Lexipoint";
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
constexpr uint32_t kWifiConnectMs = 6000;
constexpr uint32_t kWifiPollMs = 50;
constexpr unsigned long kMsPerMinute = 60UL * 1000UL;

// Sentence extraction (sentence-extraction.md §2).
constexpr size_t kMaxSentenceUnits = 120;  // cap in UTF-16 units (Lexirise's), centred on the tap
// Paragraph starts, read off line geometry (the laid-out page doesn't mark them): text/ParagraphBreaks.h.
constexpr float kParagraphShortLineEm = 2.0f;  // the line before stops at least this far from the right edge
constexpr float kParagraphGapFactor = 1.3f;    // or the gap to this line is this much above the usual line advance
constexpr float kParagraphIndentEm = 0.5f;     // or this line is indented (and the one before wasn't)

// Response limits (lexirise-client.md §4): past these a response is treated as malformed.
constexpr size_t kMaxOccurrences = 128;
constexpr size_t kMaxTokenBytes = 256;        // one word / lemma / reading
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

// The longest one Lexirise call can block (WiFi join, NTP, TCP + handshake, the request). The web page
// polls a queued key check for this long, and lxctl's LEXI wait is checked against it (test_lxctl).
constexpr uint32_t kMaxCallMs = kWifiConnectMs + kNtpWaitMs + 2 * kHttpTimeoutMs + kRequestDeadlineMs;

}  // namespace lexipoint::config
