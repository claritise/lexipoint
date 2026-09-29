#include "MarkSlots.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <iterator>
#include <string_view>

namespace lexipoint::page {

std::vector<Language> marksLanguages(const text::BookLanguage& book, const Settings& settings) {
  std::vector<Language> out;
  out.reserve(std::size(kLanguages));
  const text::LanguageDecision said = book.decide(std::string_view(), settings);
  std::copy_if(std::begin(kLanguages), std::end(kLanguages), std::back_inserter(out), [&](const Language language) {
    return book.dependsOnSentence() ? settings.enabled && settings.language(language).enabled
                                    : said.language == language;
  });
  return out;
}

void MarkSlots::onScreen(const uint32_t spine, const int page) {
  spine_ = spine;
  page_ = page;
  for (Slot& slot : slots_) slot.asked = false;
}

bool MarkSlots::have(const PageId& id) {
  // A page known not analyzed (read as drawn, whose language the render task can't tell) matches in any language.
  const auto held = std::find_if(slots_.begin(), slots_.end(), [&id](const Slot& s) {
    return s.used && s.id.key == id.key && s.id.units == id.units && s.id.hash == id.hash &&
           (s.id.language == id.language || !s.analysis);
  });
  if (held == slots_.end()) return false;
  held->asked = true;
  return true;
}

MarkSlots::Slot* MarkSlots::slotFor(const uint32_t spine, const int page) {
  const auto free = std::find_if(slots_.begin(), slots_.end(), [](const Slot& s) { return !s.used; });
  if (free != slots_.end()) return &*free;
  const auto distance = [](const Slot& s, const uint32_t fromSpine, const int from) {
    return s.spine != fromSpine ? INT_MAX : std::abs(s.page - from);
  };
  // A page this drawing hasn't asked for: first one at the very place of the page being kept (another key there is
  // an old layout's page, left by a reflow: never drawn again), else the farthest from the page on screen; when every
  // one is asked (pages read as drawn on fast turns), the farthest from the page being kept.
  const auto rank = [&](const Slot& s) {
    return s.spine == spine && s.page == page ? INT_MAX : distance(s, spine_, page_);
  };
  Slot* out = nullptr;
  for (Slot& s : slots_) {
    if (s.asked) continue;
    if (!out || rank(s) > rank(*out)) out = &s;
  }
  if (out) return out;
  for (Slot& s : slots_) {
    if (!out || distance(s, spine, page) > distance(*out, spine, page)) out = &s;
  }
  return out;
}

void MarkSlots::put(const PageId& id, const uint32_t spine, const int page, std::unique_ptr<PageAnalysis> analysis) {
  // The page's own slot when its text changed (a reflow, another language); else a free or the farthest one.
  const auto same =
      std::find_if(slots_.begin(), slots_.end(), [&id](const Slot& s) { return s.used && s.id.key == id.key; });
  Slot& slot = same != slots_.end() ? *same : *slotFor(spine, page);
  slot = Slot{true, id, spine, page, true, std::move(analysis)};
}

void MarkSlots::reload(const PageKey& key) {
  std::for_each(slots_.begin(), slots_.end(), [&key](Slot& slot) {
    if (slot.used && slot.id.key == key) slot = Slot{};
  });
  if (peeked_ && peeked_->first == key) peeked_.reset();  // read as drawn before it was written: read it again
}

void MarkSlots::clear() {
  std::generate(slots_.begin(), slots_.end(), [] { return Slot{}; });
  peeked_.reset();
}

const PageAnalysis* MarkSlots::find(const PageKey& key, const uint32_t units, const uint32_t hash) {
  const auto held = std::find_if(slots_.begin(), slots_.end(), [&](const Slot& s) {
    return s.used && s.analysis && s.id.key == key && s.id.units == units && s.id.hash == hash;
  });
  if (held == slots_.end()) return nullptr;
  peeked_.reset();  // a page found: one left and come back to is read again
  return held->analysis.get();
}

bool MarkSlots::peekDue(const PageKey& key, const uint32_t hash) {
  const std::pair<PageKey, uint32_t> page{key, hash};
  if (peeked_ == page) return false;
  peeked_ = page;
  return true;
}

bool MarkSlots::knownNotAnalyzed(const PageKey& key, const uint32_t units, const uint32_t hash) const {
  return std::any_of(slots_.begin(), slots_.end(), [&](const Slot& s) {
    return s.used && !s.analysis && s.id.key == key && s.id.units == units && s.id.hash == hash;
  });
}

size_t MarkSlots::kept() const {
  return static_cast<size_t>(
      std::count_if(slots_.begin(), slots_.end(), [](const Slot& s) { return s.analysis != nullptr; }));
}

}  // namespace lexipoint::page
