#pragma once

// What a lookup found, for the card (popup-ui.md) and, in P3, its plain-text placeholder. Pure.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "lexirise/api/Responses.h"
#include "lexirise/settings/Settings.h"

namespace lexipoint::lookup {

struct LookupCard {
  Language language = Language::Japanese;
  std::string surface;         // the word as it stands in the sentence (食べさせられた)
  std::string surfaceReading;  // its reading in context (tabesaserareta)
  std::string lemma;           // the dictionary form (食べる); empty: the surface is the headword
  std::string reading;         // the headword's reading (taberu); empty when unknown
  std::string partOfSpeech;
  std::string level;  // JLPT-N5 / HSK-1 …, empty when on no list
  std::vector<api::Sense> senses;
  uint32_t entryId = 0;                  // the surface entry
  uint32_t lemmaEntryId = 0;             // the lemma's entry: Save targets it (lookup-flow.md §5)
  std::optional<api::EntryState> saved;  // the lemma's state if saved, else the surface's
  bool translationPending = false;       // phase B: the server is still translating
  bool translationUnavailable = false;   // phase B failed (offline, or an error): the word without its meaning

  // The P3 placeholder: headword and a plain-text definition for DictionaryDefinitionActivity.
  std::string headword() const;
  std::string plainText() const;
};

}  // namespace lexipoint::lookup
