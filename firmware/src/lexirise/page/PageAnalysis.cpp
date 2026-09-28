#if LEXIRISE

#include "PageAnalysis.h"

#include <algorithm>
#include <cstring>

#include "lexirise/api/JsonNumbers.h"
#include "lexirise/util/ByteOrder.h"
#include "lexirise/util/Crc32.h"

namespace lexipoint::page {
namespace {

using bytes::crc32;
using bytes::get16;
using bytes::get32;
using bytes::get64;
using bytes::get8;
using bytes::put16;
using bytes::put32;
using bytes::put64;

using json::Path;
using json::Type;

// --- the file ---

constexpr char kMagic[4] = {'L', 'X', 'P', 'A'};
constexpr uint16_t kVersion = 1;
constexpr uint8_t kFlagRefined = 0x01;
// The header's fields, by offset (little-endian).
constexpr size_t kAtVersion = 4;
constexpr size_t kAtLanguage = 6;
constexpr size_t kAtFlags = 8;
constexpr size_t kAtTextUnits = 12;
constexpr size_t kAtTextHash = 16;
constexpr size_t kAtAnalyzed = 20;
constexpr size_t kAtOccurrences = 28;
constexpr size_t kAtMeta = 32;
constexpr size_t kAtState = 36;
constexpr size_t kAtPool = 40;
constexpr size_t kAtCrc = 44;
constexpr size_t kHeaderBytes = 48;
// Records: an occurrence (span, entry, lemma entry: u32 each; word, lemma, reading: u16 place and length each; flags,
// three spare), an entry's facts (id, rank, the frequency's bits: u32 each; reading, part of speech), a saved state
// (id; saved id; level, three spare; seen count).
constexpr size_t kOccBytes = 32;
constexpr size_t kMetaBytes = 20;
constexpr size_t kStateBytes = 16;
constexpr uint8_t kOccWordLike = 0x01;
// The reader's reservations per UTF-16 unit of text (measured on the probe's pages: 0.54-0.73 occurrences per unit,
// 0.52-0.59 entries per occurrence, 28 saved states in 220 occurrences, 20-21 pool bytes per occurrence; each
// reservation is at or under what a page needs, so nothing large is reserved for nothing).
constexpr size_t kReserveOccurrencesPercentOfUnits = 75;
constexpr size_t kReserveOccurrencesPerEntry = 2;
constexpr size_t kReserveOccurrencesPerState = 8;
constexpr size_t kReservePoolBytesPerOccurrence = 16;
static_assert(config::kPageMaxPoolBytes <= 0xFFFF, "a string's place in the pool fits its u16");

void putStr(std::string& out, const size_t at, const Str& s) {
  put16(out, at, static_cast<uint16_t>(s.at));
  put16(out, at + 2, s.len);
}
Str getStr(const std::string_view in, const size_t at) { return Str{get16(in, at), get16(in, at + 2)}; }

template <typename T>
const T* findById(const std::vector<T>& sorted, const uint32_t id) {
  const auto it =
      std::lower_bound(sorted.begin(), sorted.end(), id, [](const T& e, const uint32_t key) { return e.id < key; });
  return it != sorted.end() && it->id == id ? &*it : nullptr;
}

// Copies strings from one page's pool into another's.
class PoolCopy {
 public:
  PoolCopy(const PageAnalysis& from, PageAnalysis& to) : from_(from), to_(to) {}
  Str operator()(const Str& s) const {
    const std::string_view text = from_.str(s);
    const Str out{static_cast<uint32_t>(to_.pool.size()), static_cast<uint16_t>(text.size())};
    to_.pool.append(text);
    return out;
  }
  Occ occ(Occ o) const {
    const bool lemmaIsWord = o.lemma.at == o.word.at && o.lemma.len == o.word.len;  // shared, not copied twice
    o.word = (*this)(o.word);
    o.lemma = lemmaIsWord ? o.word : (*this)(o.lemma);
    o.reading = (*this)(o.reading);
    return o;
  }
  Meta meta(Meta m) const {
    m.reading = (*this)(m.reading);
    m.partOfSpeech = (*this)(m.partOfSpeech);
    return m;
  }
  State state(State s) const {
    s.savedId = (*this)(s.savedId);
    return s;
  }

 private:
  const PageAnalysis& from_;
  PageAnalysis& to_;
};

// Sorts by id (the first of a repeated id kept), keeps only `wanted` ids.
template <typename T>
void sortAndKeep(std::vector<T>& list, const std::vector<uint32_t>& wanted) {
  std::stable_sort(list.begin(), list.end(), [](const T& a, const T& b) { return a.id < b.id; });
  list.erase(std::unique(list.begin(), list.end(), [](const T& a, const T& b) { return a.id == b.id; }), list.end());
  list.erase(std::remove_if(list.begin(), list.end(),
                            [&wanted](const T& e) { return !std::binary_search(wanted.begin(), wanted.end(), e.id); }),
             list.end());
}

// The page with only the entries its occurrences name, and a pool holding only what's used.
PageAnalysis compacted(const PageAnalysis& page) {
  PageAnalysis out;
  out.language = page.language;
  out.refined = page.refined;
  out.textUnits = page.textUnits;
  out.textHash = page.textHash;
  out.analyzedMs = page.analyzedMs;
  out.morphoPending = page.morphoPending;
  out.statesCut = page.statesCut;
  const std::vector<uint32_t> wanted = entriesOf(page);
  std::vector<Meta> meta = page.meta;
  std::vector<State> state = page.state;
  sortAndKeep(meta, wanted);
  sortAndKeep(state, wanted);
  out.pool.reserve(page.pool.size());
  const PoolCopy copy(page, out);
  out.occurrences.reserve(page.occurrences.size());
  for (const Occ& o : page.occurrences) out.occurrences.push_back(copy.occ(o));
  out.meta.reserve(meta.size());
  for (const Meta& m : meta) out.meta.push_back(copy.meta(m));
  out.state.reserve(state.size());
  for (const State& s : state) out.state.push_back(copy.state(s));
  return out;
}

}  // namespace

const Meta* PageAnalysis::metaFor(const uint32_t id) const { return findById(meta, id); }
const State* PageAnalysis::stateFor(const uint32_t id) const { return findById(state, id); }

uint32_t textHash(const std::string_view text) { return bytes::Fnv1a().add(text).value(); }

std::vector<uint32_t> entriesOf(const PageAnalysis& page) {
  std::vector<uint32_t> ids;
  ids.reserve(page.occurrences.size() * 2);
  for (const Occ& o : page.occurrences) {
    if (o.entryId != 0) ids.push_back(o.entryId);
    if (o.lemmaEntryId != 0) ids.push_back(o.lemmaEntryId);
  }
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  return ids;
}

// --- the streamed answer ---

Str PageReader::Visitor::add(const std::string_view text) {
  if (page_.pool.capacity() == 0) page_.pool.reserve(reserve.pool);
  if (page_.pool.size() + text.size() > config::kPageMaxPoolBytes) {
    overLimit = true;
    return {};
  }
  const Str s{static_cast<uint32_t>(page_.pool.size()), static_cast<uint16_t>(text.size())};
  page_.pool.append(text);
  return s;
}

void PageReader::Visitor::onBegin(const Path& path, const bool isArray) {
  if (path.depth() == 1 && isArray && path.keyIs(0, "occurrences")) {
    sawOccurrences = true;
  } else if (path.depth() == 2 && !isArray && path.keyIs(0, "occurrences") && path.isIndex(1)) {
    if (page_.occurrences.size() >= config::kPageMaxOccurrences) {
      overLimit = true;
      return;
    }
    if (page_.occurrences.capacity() == 0) page_.occurrences.reserve(reserve.occurrences);
    page_.occurrences.emplace_back();
    inOcc_ = true;
    sawWord_ = sawLemma_ = sawStart_ = sawEnd_ = false;
  } else if (path.depth() == 2 && !isArray && !path.isIndex(1) &&
             (path.keyIs(0, "entryMetaById") || path.keyIs(0, "stateByEntryId"))) {
    uint32_t id = 0;
    if (!api::toUint32(Type::Number, path.at(1).key, id)) return;  // the key is the id's digits
    if (path.keyIs(0, "entryMetaById")) {
      if (page_.meta.size() >= config::kPageMaxEntries) return;  // dropped, not failed on (as kMaxEntries)
      if (page_.meta.capacity() == 0) page_.meta.reserve(reserve.meta);
      page_.meta.push_back(Meta{id, 0, 0, {}, {}});
      inMeta_ = true;
    } else {
      if (page_.state.size() >= config::kPageMaxEntries) {
        page_.statesCut = true;  // the answer says more than is kept: an entry missing here isn't "not saved"
        return;
      }
      if (page_.state.capacity() == 0) page_.state.reserve(reserve.state);
      page_.state.push_back(State{id, {}, 0, 0});
      inState_ = true;
    }
  }
}

void PageReader::Visitor::onEnd(const Path& path, const bool isArray) {
  if (isArray || path.depth() != 2) return;
  if (inOcc_ && path.keyIs(0, "occurrences")) {
    Occ& occ = page_.occurrences.back();
    if (!sawLemma_) occ.lemma = occ.word;
    if (!sawWord_ || occ.word.len == 0 || !sawStart_ || !sawEnd_ || occ.end < occ.start) malformed = true;
    inOcc_ = false;
  } else if (path.keyIs(0, "entryMetaById")) {
    inMeta_ = false;
  } else if (path.keyIs(0, "stateByEntryId")) {
    inState_ = false;
  }
}

void PageReader::Visitor::onValue(const Path& path, const Type type, const std::string_view text) {
  if (path.depth() == 1 && path.keyIs(0, "morphoPending")) {
    page_.morphoPending = type == Type::Bool && text == "true";
    return;
  }
  if (inOcc_ && path.depth() == 3 && path.keyIs(0, "occurrences")) {
    Occ& occ = page_.occurrences.back();
    const std::string_view field = path.leaf();
    if (field == "word") {
      if (type == Type::String) {
        occ.word = add(text);
        sawWord_ = true;
      }
    } else if (field == "lemma") {
      if (type == Type::String) {
        occ.lemma = add(text);
        sawLemma_ = true;
      }
    } else if (field == "transliteration") {
      if (type == Type::String) occ.reading = add(text);
    } else if (field == "entryId") {
      api::toUint32(type, text, occ.entryId);
    } else if (field == "lemmaEntryId") {
      api::toUint32(type, text, occ.lemmaEntryId);
    } else if (field == "charStart") {
      sawStart_ = api::toUint32(type, text, occ.start);
      if (!sawStart_) malformed = true;
    } else if (field == "charEnd") {
      sawEnd_ = api::toUint32(type, text, occ.end);
      if (!sawEnd_) malformed = true;
    } else if (field == "isWordLike") {
      occ.wordLike = type == Type::Bool && text == "true";
    }
    return;
  }
  if (inMeta_ && path.keyIs(0, "entryMetaById")) {
    Meta& meta = page_.meta.back();
    if (path.depth() == 3 && path.keyIs(2, "transliteration")) {
      if (type == Type::String) meta.reading = add(text);
    } else if (path.depth() == 3 && path.keyIs(2, "rank")) {
      api::toUint32(type, text, meta.rank);
    } else if (path.depth() == 3 && path.keyIs(2, "frequencyScore")) {
      api::toUnitFloat(type, text, meta.frequency);
    } else if (path.depth() == 4 && path.keyIs(2, "partOfSpeech") && path.index(3) == 0) {
      if (type == Type::String) meta.partOfSpeech = add(text);
    }
    return;
  }
  if (inState_ && path.depth() == 3 && path.keyIs(0, "stateByEntryId")) {
    State& state = page_.state.back();
    if (path.keyIs(2, "saved_expression_id")) {
      if (type == Type::Number || type == Type::String) state.savedId = add(text);
    } else if (path.keyIs(2, "proficiency")) {
      uint32_t value = 0;
      if (api::toUint32(type, text, value) && value <= config::kMaxProficiency) {
        state.proficiency = static_cast<uint8_t>(value);
      }
    } else if (path.keyIs(2, "seen_count")) {
      api::toUint32(type, text, state.seenCount);
    }
  }
}

PageReader::PageReader(const Language language, const Cancel cancel, const size_t textUnits)
    : visitor_(page_), reader_(visitor_), cancel_(cancel) {
  page_.language = language;
  // Reserved once from the text's length (the measured ratios: kReserve*), as each first arrives (after the handshake,
  // never holding memory through it), grown only past it.
  const size_t occurrences = std::min(textUnits * kReserveOccurrencesPercentOfUnits / 100, config::kPageMaxOccurrences);
  visitor_.reserve = {occurrences, occurrences / kReserveOccurrencesPerEntry, occurrences / kReserveOccurrencesPerState,
                      std::min(occurrences * kReservePoolBytesPerOccurrence, config::kPageMaxPoolBytes)};
}

bool PageReader::onBody(const char* data, const size_t len) {
  if (cancel_ && cancel_()) {
    cancelled_ = true;
    return false;
  }
  return reader_.feed(std::string_view(data, len)) && !visitor_.overLimit;
}

api::ParseStatus PageReader::finish(PageAnalysis& out) {
  if (cancelled_) return api::ParseStatus::Malformed;  // cut short on purpose: cancelled() says so
  if (visitor_.overLimit) return api::ParseStatus::OverLimit;
  if (reader_.finish() != json::Result::Ok || visitor_.malformed || !visitor_.sawOccurrences) {
    return api::ParseStatus::Malformed;
  }
  out = compacted(page_);
  return api::ParseStatus::Ok;
}

api::ParseStatus parsePage(const std::string_view body, const Language language, PageAnalysis& out) {
  PageReader reader(language);
  reader.onBody(body.data(), body.size());
  return reader.finish(out);
}

// --- V1's whole words ---

PageAnalysis mergeWholeWords(const PageAnalysis& refined, const PageAnalysis& words) {
  if (words.occurrences.empty()) return refined;
  PageAnalysis merged;
  merged.language = refined.language;
  merged.refined = true;
  merged.textUnits = refined.textUnits;
  merged.textHash = refined.textHash;
  merged.analyzedMs = refined.analyzedMs;
  merged.statesCut = refined.statesCut || words.statesCut;  // either answer cut: an entry missing isn't "not saved"
  merged.pool.reserve(refined.pool.size() + words.pool.size());
  const PoolCopy fromRefined(refined, merged);
  const PoolCopy fromWords(words, merged);
  merged.occurrences.reserve(words.occurrences.size() + refined.occurrences.size());
  std::vector<uint32_t> ownEntries;  // the word-level answer's words: their saved state is its alone
  ownEntries.reserve(words.occurrences.size());
  for (const Occ& word : words.occurrences) ownEntries.push_back(word.entryId);
  std::sort(ownEntries.begin(), ownEntries.end());
  for (const Occ& word : words.occurrences) {
    // The refined occurrences exactly tiling [word.start, word.end), in order (lookup/WholeWords.cpp piecesOf).
    std::vector<const Occ*> pieces;
    uint32_t at = word.start;
    bool tiles = true;
    for (const Occ& piece : refined.occurrences) {
      if (piece.end <= word.start || piece.start >= word.end) continue;
      if (piece.start != at || piece.end > word.end) {
        tiles = false;
        break;
      }
      pieces.push_back(&piece);
      at = piece.end;
    }
    if (!tiles || at != word.end) pieces.clear();
    const Meta* meta = words.metaFor(word.entryId);
    const bool ranked = meta != nullptr && meta->rank != 0;
    if (pieces.size() == 1) {
      merged.occurrences.push_back(fromRefined.occ(*pieces.front()));  // the same token: the refined one has the lemma
    } else if (!pieces.empty() && !ranked) {
      for (const Occ* piece : pieces) merged.occurrences.push_back(fromRefined.occ(*piece));  // 一日中雨
    } else {
      merged.occurrences.push_back(fromWords.occ(word));  // a ranked whole word, or pieces that don't tile it
    }
  }
  for (const Meta& m : words.meta) merged.meta.push_back(fromWords.meta(m));
  for (const Meta& m : refined.meta) {
    if (!words.metaFor(m.id)) merged.meta.push_back(fromRefined.meta(m));
  }
  for (const State& s : words.state) merged.state.push_back(fromWords.state(s));
  for (const State& s : refined.state) {
    if (!std::binary_search(ownEntries.begin(), ownEntries.end(), s.id)) merged.state.push_back(fromRefined.state(s));
  }
  return compacted(merged);
}

// --- a sentence of it ---

std::optional<api::AnalyzeResult> sliceSentence(const PageAnalysis& page, const uint32_t start, const uint32_t end) {
  if (start > end || end > page.textUnits) return std::nullopt;
  api::AnalyzeResult out;
  out.morphoPending = !page.refined;
  for (const Occ& o : page.occurrences) {
    if (o.start < start || o.end > end) continue;  // outside, or across its edge
    if (out.occurrences.size() >= config::kMaxOccurrences) break;
    api::Occurrence occ;
    occ.word = page.str(o.word);
    occ.lemma = page.str(o.lemma);
    occ.reading = page.str(o.reading);
    occ.entryId = o.entryId;
    occ.lemmaEntryId = o.lemmaEntryId;
    occ.charStart = o.start - start;
    occ.charEnd = o.end - start;
    occ.wordLike = o.wordLike;
    out.occurrences.push_back(std::move(occ));
  }
  for (const api::Occurrence& occ : out.occurrences) {
    for (const uint32_t id : {occ.entryId, occ.lemmaEntryId}) {
      if (id == 0) continue;
      if (const Meta* m = page.metaFor(id); m && out.meta.count(id) == 0) {
        api::EntryMeta meta;
        meta.reading = page.str(m->reading);
        meta.partOfSpeech = page.str(m->partOfSpeech);
        meta.rank = m->rank;
        meta.frequency = m->frequency;
        out.meta.emplace(id, std::move(meta));
      }
      if (const State* s = page.stateFor(id); s && out.state.count(id) == 0) {
        api::EntryState state;
        state.savedExpressionId = page.str(s->savedId);
        state.proficiency = s->proficiency;
        state.seenCount = s->seenCount;
        out.state.emplace(id, std::move(state));
      }
    }
  }
  return out;
}

// --- the file ---

bool fitsFile(const PageAnalysis& page) {
  return page.occurrences.size() <= config::kPageMaxOccurrences && page.meta.size() <= config::kPageMaxEntries &&
         page.state.size() <= config::kPageMaxEntries && page.pool.size() <= config::kPageMaxPoolBytes &&
         page.textUnits <= config::kPageMaxTextUnits;
}

std::string serializePage(const PageAnalysis& page) {
  const size_t bytes = kHeaderBytes + page.occurrences.size() * kOccBytes + page.meta.size() * kMetaBytes +
                       page.state.size() * kStateBytes + page.pool.size();
  std::string out(bytes, '\0');
  std::memcpy(out.data(), kMagic, sizeof(kMagic));
  put16(out, kAtVersion, kVersion);
  const char* code = languageCode(page.language);
  out[kAtLanguage] = code[0];
  out[kAtLanguage + 1] = code[1];
  out[kAtFlags] = static_cast<char>(page.refined ? kFlagRefined : 0);
  put32(out, kAtTextUnits, page.textUnits);
  put32(out, kAtTextHash, page.textHash);
  put64(out, kAtAnalyzed, page.analyzedMs);
  put32(out, kAtOccurrences, static_cast<uint32_t>(page.occurrences.size()));
  put32(out, kAtMeta, static_cast<uint32_t>(page.meta.size()));
  put32(out, kAtState, static_cast<uint32_t>(page.state.size()));
  put32(out, kAtPool, static_cast<uint32_t>(page.pool.size()));
  size_t at = kHeaderBytes;
  for (const Occ& o : page.occurrences) {
    put32(out, at, o.start);
    put32(out, at + 4, o.end);
    put32(out, at + 8, o.entryId);
    put32(out, at + 12, o.lemmaEntryId);
    putStr(out, at + 16, o.word);
    putStr(out, at + 20, o.lemma);
    putStr(out, at + 24, o.reading);
    out[at + 28] = static_cast<char>(o.wordLike ? kOccWordLike : 0);
    at += kOccBytes;
  }
  for (const Meta& m : page.meta) {
    uint32_t frequencyBits = 0;
    std::memcpy(&frequencyBits, &m.frequency, sizeof(frequencyBits));
    put32(out, at, m.id);
    put32(out, at + 4, m.rank);
    put32(out, at + 8, frequencyBits);
    putStr(out, at + 12, m.reading);
    putStr(out, at + 16, m.partOfSpeech);
    at += kMetaBytes;
  }
  for (const State& s : page.state) {
    put32(out, at, s.id);
    putStr(out, at + 4, s.savedId);
    out[at + 8] = static_cast<char>(s.proficiency);
    put32(out, at + 12, s.seenCount);
    at += kStateBytes;
  }
  std::memcpy(out.data() + at, page.pool.data(), page.pool.size());
  const std::string_view view(out);
  put32(out, kAtCrc, crc32(view.substr(0, kAtCrc), view.substr(kHeaderBytes)));
  return out;
}

bool parsePageFile(const std::string_view bytes, PageAnalysis& out) {
  if (bytes.size() < kHeaderBytes || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0) return false;
  if (get16(bytes, kAtVersion) != kVersion) return false;
  PageAnalysis page;
  const std::optional<Language> language = languageFromCode(bytes.substr(kAtLanguage, 2));
  if (!language) return false;
  page.language = *language;
  page.refined = (get8(bytes, kAtFlags) & kFlagRefined) != 0;
  page.textUnits = get32(bytes, kAtTextUnits);
  page.textHash = get32(bytes, kAtTextHash);
  page.analyzedMs = get64(bytes, kAtAnalyzed);
  const uint32_t occurrences = get32(bytes, kAtOccurrences);
  const uint32_t meta = get32(bytes, kAtMeta);
  const uint32_t state = get32(bytes, kAtState);
  const uint32_t pool = get32(bytes, kAtPool);
  if (occurrences > config::kPageMaxOccurrences || meta > config::kPageMaxEntries || state > config::kPageMaxEntries ||
      pool > config::kPageMaxPoolBytes) {
    return false;
  }
  const size_t expected = kHeaderBytes + occurrences * kOccBytes + meta * kMetaBytes + state * kStateBytes + pool;
  if (bytes.size() != expected) return false;
  if (get32(bytes, kAtCrc) != crc32(bytes.substr(0, kAtCrc), bytes.substr(kHeaderBytes))) return false;
  const auto inPool = [pool](const Str& s) { return static_cast<size_t>(s.at) + s.len <= pool; };
  size_t at = kHeaderBytes;
  page.occurrences.reserve(occurrences);
  for (uint32_t i = 0; i < occurrences; i++, at += kOccBytes) {
    Occ o;
    o.start = get32(bytes, at);
    o.end = get32(bytes, at + 4);
    o.entryId = get32(bytes, at + 8);
    o.lemmaEntryId = get32(bytes, at + 12);
    o.word = getStr(bytes, at + 16);
    o.lemma = getStr(bytes, at + 20);
    o.reading = getStr(bytes, at + 24);
    o.wordLike = (get8(bytes, at + 28) & kOccWordLike) != 0;
    if (o.end < o.start || o.end > page.textUnits || !inPool(o.word) || !inPool(o.lemma) || !inPool(o.reading)) {
      return false;
    }
    page.occurrences.push_back(o);
  }
  page.meta.reserve(meta);
  for (uint32_t i = 0; i < meta; i++, at += kMetaBytes) {
    Meta m;
    m.id = get32(bytes, at);
    m.rank = get32(bytes, at + 4);
    const uint32_t frequencyBits = get32(bytes, at + 8);
    std::memcpy(&m.frequency, &frequencyBits, sizeof(frequencyBits));
    m.reading = getStr(bytes, at + 12);
    m.partOfSpeech = getStr(bytes, at + 16);
    if (!(m.frequency >= 0 && m.frequency <= 1) || !inPool(m.reading) || !inPool(m.partOfSpeech)) return false;
    if (!page.meta.empty() && m.id <= page.meta.back().id) return false;  // sorted, once each
    page.meta.push_back(m);
  }
  page.state.reserve(state);
  for (uint32_t i = 0; i < state; i++, at += kStateBytes) {
    State s;
    s.id = get32(bytes, at);
    s.savedId = getStr(bytes, at + 4);
    s.proficiency = get8(bytes, at + 8);
    s.seenCount = get32(bytes, at + 12);
    if (s.proficiency > config::kMaxProficiency || !inPool(s.savedId)) return false;
    if (!page.state.empty() && s.id <= page.state.back().id) return false;
    page.state.push_back(s);
  }
  page.pool.assign(bytes.substr(at, pool));
  page.morphoPending = !page.refined;
  out = std::move(page);
  return true;
}

}  // namespace lexipoint::page

#endif  // LEXIRISE
