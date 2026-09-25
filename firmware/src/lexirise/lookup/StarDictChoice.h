#pragma once

// Which StarDict dictionary answers a tap (settings.md §1 "Offline dictionary", languages.md §4): the
// folder chosen for the tap's language, else CrossPoint's own Dictionary setting. The language's choice
// counts even while its Lexirise lookups are off: that is when StarDict answers every tap in it.
// Pure; tests: test/lexirise_lookup.

#include <Utf8.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include "lexirise/settings/Settings.h"
#include "lexirise/text/BookLanguage.h"
#include "lexirise/text/CharClass.h"

namespace lexipoint::lookup {

// The dictionary a tap opens, and the one it falls back on when that can't be opened (the language's own
// folder was removed from the card, say): CrossPoint's, when it's another one.
struct StarDictChoice {
  std::string folder;    // empty: no dictionary
  std::string fallback;  // empty: none
  bool operator==(const StarDictChoice& o) const { return folder == o.folder && fallback == o.fallback; }
  bool operator!=(const StarDictChoice& o) const { return !(*this == o); }
};

// Whether the dictionary must be (re)opened for `wanted`: never opened, or opened for another choice (a tap
// in the other language).
inline bool needsOpen(const std::optional<StarDictChoice>& opened, const StarDictChoice& wanted) {
  return !opened || *opened != wanted;
}

enum class OpenedFrom : uint8_t { Folder, Fallback, Neither };
// Opens the choice's folder, else its fallback. `open(folder)` is Dictionary::open on the device.
template <typename OpenFn>
OpenedFrom openStarDict(const StarDictChoice& choice, OpenFn&& open) {
  if (open(choice.folder)) return OpenedFrom::Folder;
  if (!choice.fallback.empty() && open(choice.fallback)) return OpenedFrom::Fallback;
  return OpenedFrom::Neither;
}

// Whether a tapped token has a Japanese/Chinese word character (kana, Han, ー, 々).
inline bool hasJaZhWordChar(const std::string_view token) {
  const std::string text(token);  // NUL-terminated for the decoder
  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  while (const uint32_t cp = utf8NextCodepoint(&p)) {
    if (text::chars::isJaZhWordChar(cp)) return true;
  }
  return false;
}

// The dictionary as word select holds it after prepareStarDict: opened (from the folder or its fallback, or
// not at all), and whether its .qidx sidecar must be built first (Dictionary::needsIndex).
struct StarDictState {
  OpenedFrom from = OpenedFrom::Neither;
  bool needsIndex = false;
  bool ok() const { return from != OpenedFrom::Neither; }
};

// (Re)opens the dictionary for `wanted` when it isn't what's open (`opened`, updated): a new StarDictState,
// else nullopt (keep the current one). A failed open isn't retried until the choice changes. `open(folder)`
// and `needsIndex()` are Dictionary's on the device.
template <typename OpenFn, typename NeedsIndexFn>
std::optional<StarDictState> prepareStarDict(const StarDictChoice& wanted, std::optional<StarDictChoice>& opened,
                                             OpenFn&& open, NeedsIndexFn&& needsIndex) {
  if (!needsOpen(opened, wanted)) return std::nullopt;
  opened = wanted;
  StarDictState state;
  state.from = openStarDict(wanted, open);
  // needsIndex() opens and validates the sidecar: asked once per open, as CrossPoint does.
  state.needsIndex = state.ok() && needsIndex();
  return state;
}

// `language`: whose dictionary the tapped text gets (LanguageDecision::dictionaryLanguage); nullopt for a
// non-CJK tap or a Traditional Chinese book. `token`: the
// tapped word: a language's own dictionary only answers Japanese/Chinese words, so an English word in a
// Japanese book still goes to CrossPoint's.
inline StarDictChoice chooseStarDict(const Settings& settings, const std::optional<Language> language,
                                     const std::string_view token, const std::string_view global) {
  if (language && hasJaZhWordChar(token)) {
    const std::string& own = settings.language(*language).stardict;
    if (!own.empty()) return {own, own == global ? std::string() : std::string(global)};
  }
  return {std::string(global), {}};
}

// A language has its own dictionary: a tap's language matters to StarDict.
inline bool anyLanguageStarDict(const Settings& settings) {
  return std::any_of(std::begin(kLanguages), std::end(kLanguages),
                     [&settings](const Language l) { return !settings.language(l).stardict.empty(); });
}

// Some StarDict dictionary could answer in this book (before a tap's language is known): word select has
// one to ask, so a long-press is a lookup. A language's own counts unless the book's override or metadata
// names the other language (or it's Traditional Chinese, whose taps keep CrossPoint's); a book whose
// metadata doesn't say (or names a non-CJK one) may be in either, as for Lexirise
// (BookLanguage::mayUseLexirise: Japanese novels stamped "en" exist).
inline bool anyStarDict(const Settings& settings, const std::string_view global, const text::BookLanguage& book) {
  if (!global.empty()) return true;
  if (book.dependsOnSentence()) return anyLanguageStarDict(settings);
  const std::optional<Language> own = book.decide({}, settings).dictionaryLanguage();
  return own && !settings.language(*own).stardict.empty();
}

}  // namespace lexipoint::lookup
