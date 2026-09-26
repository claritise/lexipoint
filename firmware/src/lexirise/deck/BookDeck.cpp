#if LEXIRISE

#include "BookDeck.h"

#include <Logging.h>

#include <algorithm>

#include "lexirise/api/Requests.h"
#include "lexirise/text/BookSlug.h"
#include "lexirise/text/Utf8Prefix.h"

namespace lexipoint::deck {
namespace {

constexpr const char* kLogTag = "LXDECK";

// "<ja|zh>:<slug>", the slug [a-z0-9-].
bool isKey(const std::string_view key) {
  const size_t colon = key.find(':');
  if (colon == std::string_view::npos) return false;
  const std::optional<Language> language = languageFromCode(key.substr(0, colon));
  if (!language || key.substr(0, colon) != languageCode(*language)) return false;  // as deckKey writes it
  const std::string_view slug = key.substr(colon + 1);
  if (slug.empty() || slug.size() > config::kBookSlugMaxBytes) return false;
  return std::all_of(slug.begin(), slug.end(),
                     [](const char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

size_t serializedSize(const DeckList& list) {
  size_t size = 0;
  for (const auto& entry : list) size += entry.key.size() + 1 + entry.id.size() + 1;
  return size;
}

bool hasTag(const api::DeckSummary& deck, const std::string& tag) {
  return std::find(deck.userTags.begin(), deck.userTags.end(), tag) != deck.userTags.end();
}

const char* deckStepName(const DeckStep step) {
  switch (step) {
    case DeckStep::Check:
      return "check";
    case DeckStep::List:
      return "list";
    case DeckStep::Create:
      return "create";
    case DeckStep::None:
      break;
  }
  return "none";
}

std::string bodyHead(const std::string_view body) {
  return std::string(text::utf8Prefix(body, config::kLoggedBodyBytes));
}

}  // namespace

std::string BookDeck::tag() const { return config::kBookTagPrefix + slug; }

bool deckAllowed(const Settings& settings) { return settings.enabled && settings.tagBook && settings.deckPerBook; }

std::optional<BookDeck> bookDeckFor(const Settings& settings, const std::string_view title, const std::string_view path,
                                    BookTagStore& titles) {
  if (!deckAllowed(settings)) return std::nullopt;
  BookDeck deck;
  deck.slug = text::bookSlug(title, path);
  const std::string name = titles.title(deck.slug).value_or(bookRecordTitle(title, path));
  deck.title = config::kDeckTitlePrefix + (name.empty() ? deck.slug : name);
  return deck;
}

std::string deckKey(const Language language, const std::string_view slug) {
  std::string key = languageCode(language);
  key += ':';
  key += slug;
  return key;
}

std::optional<std::string> findBookDeck(const std::vector<api::DeckSummary>& decks, const BookDeck& deck,
                                        const Language language) {
  const std::string tag = deck.tag();
  const auto candidate = [language](const api::DeckSummary& d) {
    if (d.othersDeck()) return false;
    // Only a deck that says the other language is left out: one this can't read ("japanese", "jpn") may be ours.
    const std::string_view code = std::string_view(d.language).substr(0, d.language.find_first_of("-_"));  // zh-Hans
    const std::optional<Language> said = languageFromCode(code);
    return !said || *said == language;
  };
  for (const api::DeckSummary& d : decks) {
    if (candidate(d) && d.deckType == config::kDeckTypeDynamic && d.unitType == config::kDeckUnitWord &&
        d.ruleType == config::kDeckRuleTagFilter && hasTag(d, tag)) {
      return d.id;
    }
  }
  for (const api::DeckSummary& d : decks) {
    const bool maybeDynamic = d.deckType.empty() || d.deckType == config::kDeckTypeDynamic;
    const bool maybeWords = d.unitType.empty() || d.unitType == config::kDeckUnitWord;  // not C3's sentence deck
    // Not a saved_vocab_query deck the user made with our title: only a tag rule, or one the list doesn't say.
    const bool maybeTagRule = d.ruleType.empty() || d.ruleType == config::kDeckRuleTagFilter;
    const bool tagsFit = d.userTags.empty() || hasTag(d, tag);  // tags listed: they must be the book's
    if (candidate(d) && d.title == deck.title && maybeDynamic && maybeWords && maybeTagRule && tagsFit) return d.id;
  }
  return std::nullopt;
}

void want(DeckState& state) {
  if (!(state.id && state.checked)) state.wanted = true;
}

DeckStep nextStep(const DeckState& state) {
  if (!state.wanted) return DeckStep::None;
  if (state.id) return state.checked ? DeckStep::None : DeckStep::Check;
  if (!state.listedNone) return DeckStep::List;
  return state.createdThisBoot ? DeckStep::None : DeckStep::Create;
}

void answer(DeckState& state, const DeckStep step, const DeckAnswer& a) {
  const bool ok = a.error == api::ApiError::None && !a.unreadable;
  const auto found = [&state](const std::string& id) {
    state.id = id;
    state.checked = true;
    state.wanted = false;
    state.listedNone = false;
  };
  switch (step) {
    case DeckStep::Check:
      if (ok) {
        state.checked = true;
        state.wanted = false;
      } else if (a.status == api::kHttpNotFound) {
        state.id.reset();  // deleted in Lexirise: listed, then made again
        state.checked = false;
        state.listedNone = false;
      } else {
        state.wanted = false;
      }
      return;
    case DeckStep::List:
      if (ok && a.deckId) {
        found(*a.deckId);
      } else if (ok && a.listComplete) {
        state.listedNone = true;
        if (state.createdThisBoot) state.wanted = false;  // this boot's one creation went: nothing left to do
      } else {
        state.wanted = false;  // unreadable, offline, or not the whole list: never "not there"
      }
      return;
    case DeckStep::Create:
      if (a.sent) state.createdThisBoot = true;
      if (ok && a.deckId) {
        found(*a.deckId);
      } else {
        state.wanted = false;
        state.listedNone = false;  // listed again first: a lost answer's deck is found there
      }
      return;
    case DeckStep::None:
      return;
  }
}

DeckList parseDecks(const std::string_view text) {
  DeckList list;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) end = text.size();
    std::string_view line = text.substr(start, end - start);
    start = end + 1;
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
    const size_t eq = line.find('=');
    if (eq == std::string_view::npos) continue;
    setDeckIn(list, line.substr(0, eq), line.substr(eq + 1));
  }
  return list;
}

std::string serializeDecks(const DeckList& list) {
  std::string text;
  text.reserve(serializedSize(list));
  for (const auto& entry : list) {
    text += entry.key;
    text += '=';
    text += entry.id;
    text += '\n';
  }
  return text;
}

bool setDeckIn(DeckList& list, const std::string_view key, const std::string_view id) {
  if (!isKey(key) || !api::isPlainId(id, config::kMaxDeckIdBytes)) return false;
  forgetDeckIn(list, key);
  list.push_back({std::string(key), std::string(id)});
  while (!list.empty() && (list.size() > config::kDecksMax || serializedSize(list) > config::kDecksMaxBytes)) {
    list.erase(list.begin());
  }
  return true;
}

void forgetDeckIn(DeckList& list, const std::string_view key) {
  list.erase(std::remove_if(list.begin(), list.end(), [key](const DeckEntry& e) { return e.key == key; }), list.end());
}

DeckCall sendDeckStep(api::LexiriseApi& api, const BookDeck& deck, const Language language, const DeckStep step,
                      const std::optional<std::string>& id) {
  DeckCall call;
  call.key = deckKey(language, deck.slug);
  call.step = step;
  // Logged as the call starts (the service logs it once answered): lxctl deck-smoke checks the idle time by it.
  LOG_INF(kLogTag, "step %s %s", deckStepName(step), call.key.c_str());
  DeckAnswer& a = call.answer;
  api::ApiResponse response;
  switch (step) {
    case DeckStep::Check: {
      const std::optional<net::Request> request = api::deckRequest(id.value_or(""));
      if (!request) {
        a.error = api::ApiError::Malformed;
        return call;
      }
      response = api.deck(*request);
      break;
    }
    case DeckStep::List:
      response = api.deck(api::deckListRequest(language));
      if (response.ok()) {
        std::vector<api::DeckSummary> decks;
        if (api::parseDeckList(response.body, decks, &a.listComplete) == api::ParseStatus::Ok) {
          a.deckId = findBookDeck(decks, deck, language);
        } else {
          a.unreadable = true;
        }
      }
      break;
    case DeckStep::Create: {
      const std::string tag = deck.tag();
      response = api.deck(api::createDeckRequest({language, deck.title, tag}));
      if (response.ok()) {
        api::CreatedDeck created;
        if (api::parseCreatedDeck(response.body, created) == api::ParseStatus::Ok) {
          a.deckId = created.id;
          const bool typeFits = created.deckType.empty() || created.deckType == config::kDeckTypeDynamic;
          const bool ruleFits = created.ruleType.empty() || created.ruleType == config::kDeckRuleTagFilter;
          if (!typeFits || !ruleFits) {
            LOG_ERR(kLogTag, "Created deck is %s / %s, not a tag deck", created.deckType.c_str(),
                    created.ruleType.c_str());
          }
        } else {
          a.unreadable = true;
        }
      } else if (response.status > 0) {
        LOG_INF(kLogTag, "deck creation refused: HTTP %d", response.status);
      }
      break;
    }
    case DeckStep::None:
      return call;
  }
  a.error = response.error;
  a.status = response.status;
  a.sent = response.sent;
  if (a.unreadable) LOG_INF(kLogTag, "unreadable deck answer: %s", bodyHead(response.body).c_str());
  return call;
}

void DeckStore::load() {
  std::lock_guard<std::mutex> lock(mutex_);
  loadLocked();
}

void DeckStore::loadLocked() {
  if (loaded_) return;
  loaded_ = true;
  std::string text;
  switch (readSafely(files_, config::kDecksFile, text)) {
    case SafeRead::Ok:
    case SafeRead::RecoveredBackup:
      list_ = parseDecks(text);
      break;
    case SafeRead::Missing:
      break;
    case SafeRead::Unreadable:
    default:
      LOG_ERR(kLogTag, "Decks unreadable: moved to %s", config::kDecksBadPath);
      break;
  }
}

DeckState DeckStore::stateLocked(const std::string_view key) {
  loadLocked();
  const auto it = std::find_if(work_.begin(), work_.end(), [key](const Work& w) { return w.key == key; });
  if (it != work_.end()) return it->state;
  DeckState state;  // untouched this boot: what the file says, nothing wanted
  const auto entry = std::find_if(list_.begin(), list_.end(), [key](const DeckEntry& e) { return e.key == key; });
  if (entry != list_.end()) state.id = entry->id;
  return state;
}

DeckStore::Work& DeckStore::workLocked(const std::string_view key) {
  const auto it = std::find_if(work_.begin(), work_.end(), [key](const Work& w) { return w.key == key; });
  if (it != work_.end()) return *it;
  Work work;
  work.state = stateLocked(key);
  work.key = std::string(key);
  if (work_.size() >= config::kDeckWorkMax) {
    // The oldest book deck that is only "checked" goes (at worst it is checked once more this boot). One with
    // work left, or whose creation went this boot, stays: forgetting that would allow a second creation.
    const auto settled = std::find_if(work_.begin(), work_.end(),
                                      [](const Work& w) { return !w.state.wanted && !w.state.createdThisBoot; });
    if (settled != work_.end()) work_.erase(settled);
  }
  work_.push_back(std::move(work));
  return work_.back();
}

DeckState DeckStore::state(const std::string_view key) {
  std::lock_guard<std::mutex> lock(mutex_);
  return stateLocked(key);
}

void DeckStore::want(const std::string_view key) {
  std::lock_guard<std::mutex> lock(mutex_);
  deck::want(workLocked(key).state);
}

bool DeckStore::anyWanted() {
  std::lock_guard<std::mutex> lock(mutex_);
  return std::any_of(work_.begin(), work_.end(), [](const Work& w) { return w.state.wanted; });
}

DeckStep DeckStore::next(const std::string_view key) {
  std::lock_guard<std::mutex> lock(mutex_);
  return nextStep(stateLocked(key));  // a key never wanted reads as nothing to do, and isn't kept
}

bool DeckStore::apply(const DeckCall& call) {
  std::lock_guard<std::mutex> lock(mutex_);
  DeckState& state = workLocked(call.key).state;
  const DeckState before = state;
  answer(state, call.step, call.answer);
  if (state.id == before.id) return true;  // nothing on the card changes
  if (state.id && !setDeckIn(list_, call.key, *state.id)) {
    LOG_ERR(kLogTag, "Deck %s: not recorded (not a key or id a line can hold)", call.key.c_str());
    return false;  // memory keeps it: no repeat call this boot
  }
  if (!state.id) forgetDeckIn(list_, call.key);
  LOG_INF(kLogTag, "Deck %s: %s", call.key.c_str(), state.id ? "recorded" : "forgotten");
  if (!replaceSafely(files_, config::kDecksFile, serializeDecks(list_))) {
    LOG_ERR(kLogTag, "Couldn't save the book's deck");  // memory keeps it: no repeat call this boot
    return false;
  }
  return true;
}

}  // namespace lexipoint::deck

#endif  // LEXIRISE
