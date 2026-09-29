#pragma once

#include <iterator>

// Lexirise feature constants in one place (docs/v0.1). Tunables of the dev harness
// live separately in src/lexirise/dev/DevConfig.h.

#include <cstddef>
#include <cstdint>

#include "lexirise/util/Timing.h"

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
// Each book's Lexirise deck (V3): `<language>:<slug>=<deck id>` lines, newest last.
constexpr const char* kDecksPath = "/.lexirise/decks.ini";
constexpr const char* kDecksTmpPath = "/.lexirise/decks.ini.tmp";
constexpr const char* kDecksBackupPath = "/.lexirise/decks.ini.bak";
constexpr const char* kDecksBadPath = "/.lexirise/decks.ini.bad";
// The words the reader ignored (C17, V5): on the reader only, never sent to Lexirise (settings.md §3).
constexpr const char* kIgnoredPath = "/.lexirise/ignored.ini";
constexpr const char* kIgnoredTmpPath = "/.lexirise/ignored.ini.tmp";
constexpr const char* kIgnoredBackupPath = "/.lexirise/ignored.ini.bak";
constexpr const char* kIgnoredBadPath = "/.lexirise/ignored.ini.bad";
constexpr size_t kIgnoredIdsMax = 1000;      // words kept by entry id (8 B each in RAM); one more forgets the oldest
constexpr size_t kIgnoredTextsMax = 50;      // words without an entry id, kept by their dictionary form (rare)
constexpr size_t kIgnoredTextMaxBytes = 64;  // such a form, longer: not ignorable (a key is never cut)
constexpr size_t kIgnoredMaxBytes = 20480;   // the file: both caps' longest lines (checked in IgnoredWords.cpp)
constexpr size_t kDecksMax = 100;            // decks remembered; one more forgets the oldest (found again by the list)
constexpr size_t kDecksMaxBytes = 16384;     // kDecksMax of the longest lines (checked below, with the slug's size)
// The vocab mirror (C13, V7a): each language's saved words, read-only from the user's Lexirise account, one binary
// file per language (vocab/VocabMirror.h; its format in docs/v0.1/settings.md §3).
constexpr const char* kVocabPathJa = "/.lexirise/vocab-ja.bin";
constexpr const char* kVocabTmpPathJa = "/.lexirise/vocab-ja.bin.tmp";
constexpr const char* kVocabBackupPathJa = "/.lexirise/vocab-ja.bin.bak";
constexpr const char* kVocabBadPathJa = "/.lexirise/vocab-ja.bin.bad";
constexpr const char* kVocabPathZh = "/.lexirise/vocab-zh.bin";
constexpr const char* kVocabTmpPathZh = "/.lexirise/vocab-zh.bin.tmp";
constexpr const char* kVocabBackupPathZh = "/.lexirise/vocab-zh.bin.bak";
constexpr const char* kVocabBadPathZh = "/.lexirise/vocab-zh.bin.bad";
constexpr size_t kVocabMirrorMax = 20000;  // words kept per language (20 B each); past it, new ones aren't taken
constexpr size_t kVocabHeaderBytes = 72;
constexpr size_t kVocabRecordBytes = 20;  // V7b R5: with its time (version 3)
constexpr size_t kVocabMaxBytes = kVocabHeaderBytes + kVocabMirrorMax * kVocabRecordBytes;
// Syncing it (GET /v1/vocabulary, newest change first). Items are ~6.8 KB each (lexirise-api-notes.md "V7's
// foundations"): a page of kVocabPageItems is ~340 KB, streamed and never held, so one page blocks the card's loop
// for a few seconds, not the ~1.35 MB of a 200-item page.
constexpr uint32_t kVocabPageItems = 50;
constexpr uint32_t kVocabListLimitMax = 200;  // the reference's `limit` cap: a page with more items is malformed
constexpr uint32_t kVocabPageOverlap = 2;     // the next page starts this many items back: an item deleted meanwhile
                                              // moves the rest up, and the overlap keeps them from being skipped
// An incremental pass's first page: a probe, since the usual answer is "nothing new" (a whole page of ~6.8 KB items
// to learn that was the cost); pages after it are whole. limit=2 answered as asked (measured).
constexpr uint32_t kVocabProbeItems = 5;
static_assert(kVocabProbeItems > kVocabPageOverlap, "a probe leaves the whole overlap");
constexpr size_t kVocabPageMaxBytes = 4UL * 1024UL * 1024UL;  // a streamed page larger than this is malformed
constexpr size_t kJsonStreamMaxStringBytes = 256;  // a streamed string kept (the rest walked): kMaxTokenBytes
constexpr unsigned kVocabPagesPerCard = 2;         // pages one card may fetch (each blocks the loop)
constexpr size_t kVocabPendingMax = 256;     // card answers kept for a mirror not loaded yet (28 B each on Xtensa)
constexpr unsigned kVocabPagesPerHour = 60;  // pages fetched per hour of uptime: 5% of the 1200 req/h limit
constexpr unsigned long kVocabSyncIntervalMs = 15UL * timing::kMsPerMinute;  // an incremental sync at most this often
constexpr unsigned long kVocabFailureWaitMs = 5UL * timing::kMsPerMinute;  // after a page failed (not a 429's own wait)
constexpr uint32_t kVocabResyncS =
    7U * static_cast<uint32_t>(timing::kSecondsPerDay);  // a full pass again after this (deletions: VocabMirror.h)
// Page analysis (C12, V7b; docs/v0.2/page-annotations.md §1.1): one analyze/text per page, the answer streamed into a
// compact page/PageAnalysis (76-93 KB answers for 300-380 UTF-16 units, measured), kept on SD per page.
constexpr const char* kPageCacheDir = "/.lexirise/pages";             // a folder per book under it
constexpr const char* kPageIndexPath = "/.lexirise/pages/index.bin";  // the pages kept, oldest first (eviction)
constexpr const char* kPageIndexTmpPath = "/.lexirise/pages/index.bin.tmp";
constexpr const char* kPageIndexBackupPath = "/.lexirise/pages/index.bin.bak";
constexpr const char* kPageIndexBadPath = "/.lexirise/pages/index.bin.bad";
constexpr size_t kPageCacheFiles = 1000;      // pages kept (13-15 KB each measured: ~15 MB); one more drops the oldest
constexpr size_t kPageIndexRecordBytes = 12;  // a kept page: its book, section and start (page/PageStore.cpp)
constexpr size_t kPageIndexMaxBytes = 16 + kPageCacheFiles * kPageIndexRecordBytes;
constexpr unsigned kPageIndexSaveEvery = 8;   // pages written between the index's saves (SD wear: it's rewritten whole)
constexpr size_t kPageMaxTextUnits = 4096;    // a page's text longer than this (UTF-16 units) isn't analyzed
constexpr size_t kPageMaxOccurrences = 2048;  // an answer with more is over its limit (not cached)
constexpr size_t kPageMaxEntries = 2048;      // entryMetaById / stateByEntryId entries kept (past it: dropped)
constexpr size_t kPageMaxPoolBytes = 0xFFFF;  // the page's strings (words, lemmas, readings), past it: over limit
constexpr size_t kPageAnswerMaxBytes = 2UL * 1024UL * 1024UL;  // a streamed answer larger than this is malformed
constexpr size_t kPageFileMaxBytes = 256UL * 1024UL;           // a cache file larger than this is unreadable
// When the reader analyzes: a page up this long (the debounce: pages turning faster are never analyzed), at most this
// share of the key's hourly limit used by this reader's own requests, the limit when /v1/me hasn't said (measured).
constexpr unsigned long kPagePrefetchDwellMs = 1500;
constexpr unsigned kPageBudgetPercent = 70;
constexpr uint32_t kRateLimitDefault = 1200;
constexpr unsigned long kRateWindowMs = 60UL * timing::kMsPerMinute;
constexpr size_t kRateWindowBuckets = 60;  // one a minute
// A failed page analysis waits this long before any page is tried again (a 429 waits its own time).
constexpr unsigned long kPageFailureWaitMs = 2UL * timing::kMsPerMinute;
// The lemma cache (C21, V7c; docs/v0.2/00-overview.md C21 "V7c design"): phase B's answers on SD, by language and the
// text looked up, in kLookupBuckets files per language (the bucket from the text's FNV-1a 32). A record is what the
// card keeps of an answer (39-140 bytes of text measured); one over kLookupRecordMaxBytes isn't kept. A bucket holds at
// most kLookupBucketMax records and kLookupBucketMaxBytes (the oldest go first), so the read before phase B is one
// small file.
constexpr const char* kLookupCacheDir = "/.lexirise/lookups";  // a folder per language under it
constexpr size_t kLookupBuckets = 64;
constexpr size_t kLookupBucketMax = 64;
constexpr size_t kLookupRecordMaxBytes = 1024;
constexpr size_t kLookupBucketMaxBytes = 16UL * 1024UL;
constexpr uint32_t kLookupMaxAgeS = 30U * static_cast<uint32_t>(timing::kSecondsPerDay);  // older: asked again
constexpr size_t kLookupPendingMax = 8;  // answers a card keeps for its next idle window's write (the newest)
// The touch line (input/TouchLine.h, V7b R1): given up on after this many changes with no finger down within the
// window (a line at rest never changes; a bad guess at its level would make every call give up).
constexpr unsigned kTouchLineFlipsMax = 4;
constexpr unsigned long kTouchLineFlipWindowMs = 60UL * timing::kMsPerSecond;
// The home screen's Sync Vocabulary (V7b, claritise 2026-09-28): at most this many pages per press (a large first
// sync goes on at the next press or on idle cards: the passes are resumable), and its result popup's time.
constexpr unsigned kVocabManualSyncPagesMax = 200;
// And at most this many pages an hour across presses (R4: repeated presses mustn't run the key into a 429 that would
// block the cards). Not enough alone (R10): with the idle pages (kVocabPagesPerHour) and the page analysis (up to
// kPageBudgetPercent of the limit) it could pass 1200, so a press doesn't join, and a run stops between pages, once
// this reader's requests in the last hour reach kVocabManualSyncStopPercent of the key's limit.
constexpr unsigned kVocabManualSyncPagesPerHour = 300;
constexpr unsigned kVocabManualSyncStopPercent = 90;
constexpr unsigned long kVocabSyncResultMs = 2000;
// V7b (claritise, 2026-09-28): a card's probe of the vocabulary (kVocabProbeItems), once its phase B is on screen and
// it has been idle this long, at most once per kVocabCardProbeIntervalMs (page-annotations.md §1.1 "claritise's
// decisions").
constexpr unsigned long kVocabCardProbeIdleMs = 1000;
constexpr unsigned long kVocabCardProbeIntervalMs = 5UL * timing::kMsPerMinute;
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
// An idle TLS session is closed after this, so it never sits on internal heap that CrossPoint's TLS user
// (OTA) pre-flights for. Keep-alive still covers a lookup's back-to-back calls.
constexpr unsigned long kTlsIdleCloseMs = 30000;
// One TLS open's budget: a TCP connect and a handshake, each up to kHttpTimeoutMs (kMaxCallMs counts it once). V7c: a
// handshake offered the kept session that failed is tried once more in full only inside it, with at least
// kTlsFallbackMinMs of it left: a full handshake, TCP connect included, took 2.5 s on the device
// (docs/v0.1/device-checks.md), and 4 s leaves room for a slower network; less than that and the fallback would likely
// time out anyway (the call fails as it did, the next open is full).
constexpr uint32_t kTlsOpenBudgetMs = 2 * kHttpTimeoutMs;
constexpr uint32_t kTlsFallbackMinMs = 4000;
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
constexpr const char* kReleaseAssetSuffix = "-x4pro.bin";  // the one board (publish_release.py DEVICE)
constexpr size_t kReleaseAssetNameBytes = 48;              // the updater's buffer (release_tag.py ASSET_NAME_BYTES)
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
constexpr unsigned long kMsPerMinute = timing::kMsPerMinute;

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
constexpr unsigned long kToastMs = 2000;  // "Saved as learning · Undo"
// "Ignored: won't be marked again · Undo" (C17, V5) stays longer: once it's gone an ignore can't be undone on the card
// (until V6 adds an un-ignore); a save or a level keeps kToastMs (either can be changed again from the card).
constexpr unsigned long kIgnoreToastMs = 5000;
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
// A saved word's notes (the sentence it was saved with: "Met before", C14) are kept to this, cut at a character: a
// sentence Lexipoint saves is at most kMaxSentenceUnits UTF-16 units, kMaxUtf8BytesPerUtf16Unit bytes each at most
// (a BMP character is one unit and up to 3 bytes; a non-BMP one two units and 4 bytes).
constexpr size_t kMaxUtf8BytesPerUtf16Unit = 3;
constexpr size_t kMaxSavedNoteBytes = kMaxUtf8BytesPerUtf16Unit * kMaxSentenceUnits;
constexpr size_t kMaxSavedTags = 16;  // a saved word's user_tags read (the book tag among them)
// Saved items ("Met before": GET /v1/vocabulary/{id}) a card first makes room for; each is asked once per card, only
// for a saved word the card is on, so a card holds a handful.
constexpr size_t kSavedItemsReserved = 4;
// "Met before" leaves out the sentence on the page itself: also a cut of the same long sentence (cut around another
// tap), or a sentence inside the other, when the two share at least this share of the longer.
constexpr size_t kSameSentenceOverlapPercent = 50;
// A saved note at least this share of kMaxSentenceUnits long may be a cut the cap made (Lexipoint saves the cut
// sentence): inside the page's sentence at any length it's the same sentence. A shorter one can't be told from a
// short sentence of its own.
constexpr size_t kCutNoteMinPercentOfCap = 80;
constexpr size_t kJsonMaxDepth = 12;  // Lexirise responses nest ~5 deep; the reader recurses

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
// book-tags.ini's longest line, `<slug>=<title>\n`: the file never drops a book for size before kBookTagsMax (its byte
// cap is a backstop, as decks.ini's is below).
static_assert(kBookTagsMax * (kBookSlugMaxBytes + 1 + kBookTagTitleMaxBytes + 1) <= kBookTagsMaxBytes,
              "the book tags file fits kBookTagsMax of the longest lines");
// A book's deck (C4, V3): a dynamic deck on its book tag, titled kDeckTitlePrefix + the book's title. The type,
// unit and rule names are the reference's (lexirise-api-notes.md, Decks), sent and matched as they are.
constexpr const char* kDeckTitlePrefix = "Lexipoint: ";
constexpr const char* kDeckTypeDynamic = "dynamic";
constexpr const char* kDeckUnitWord = "word";
constexpr const char* kDeckRuleTagFilter = "user_tag_filter";
constexpr size_t kMaxDeckIdBytes = 64;   // a deck id as sent (it goes into a request path)
constexpr size_t kMaxDecksListed = 256;  // GET /v1/decks entries read; past it, the rest are ignored
constexpr size_t kMaxDeckTagsRead = 16;  // a listed deck's user_tags read (a book deck has one)
constexpr size_t kDeckWorkMax = 32;      // book decks (book and language) kept in memory since boot
// decks.ini's longest line, `<ja|zh>:<slug>=<id>\n`: the file never drops a deck for size before kDecksMax.
constexpr size_t kLanguageCodeBytes = 2;  // "ja" / "zh" (Settings.h languageCode)
static_assert(kDecksMax * (kLanguageCodeBytes + 1 + kBookSlugMaxBytes + 1 + kMaxDeckIdBytes + 1) <= kDecksMaxBytes,
              "decks.ini fits kDecksMax of the longest lines");
// A card starts its book's deck work only once it has been idle this long (no input, no answer, no write
// waiting): a deck call blocks the loop, so it keeps clear of stepping and a save's Undo window.
constexpr unsigned long kDeckIdleMs = 3000;
// A vocab mirror page (V7a) the same way, after any deck step, but only after longer idle: a page blocks the loop for
// seconds (a side-button press gives it up, VocabPageReader's cancel). Its file is read and written after kDeckIdleMs.
constexpr unsigned long kVocabIdleMs = 8000;

// The longest one Lexirise call can block (WiFi join, NTP, TCP + handshake, the request). The web page
// polls a queued key check for this long, and lxctl's LEXI wait is checked against it (test_lxctl).
constexpr uint32_t kMaxCallMs = kWifiJoinMaxMs + kWifiRadioSlackMs + kNtpWaitMs + kTlsOpenBudgetMs + kRequestDeadlineMs;

}  // namespace lexipoint::config
