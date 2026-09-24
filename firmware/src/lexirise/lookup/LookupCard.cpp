#if LEXIRISE

#include "LookupCard.h"

namespace lexipoint::lookup {

std::string LookupCard::headword() const { return lemma.empty() ? surface : lemma; }

// 食べる  taberu · verb · JLPT-N5
// (食べさせられた)
//
// 1. eat
// 2. …
std::string LookupCard::plainText() const {
  std::string out = headword();
  const auto add = [&out](const std::string& part) {
    if (part.empty()) return;
    out += out.empty() ? "" : " · ";
    out += part;
  };
  if (!reading.empty()) out += "  " + reading;
  add(partOfSpeech);
  add(level);
  if (saved) add("saved");
  if (!surface.empty() && surface != headword()) out += "\n(" + surface + ")";
  out += "\n\n";
  if (senses.empty()) {
    out += translationUnavailable ? "(meaning unavailable)" : translationPending ? "(translation pending)" : "";
  }
  for (size_t i = 0; i < senses.size(); i++) {
    out += std::to_string(i + 1) + ". " + senses[i].translation + "\n";
  }
  return out;
}

}  // namespace lexipoint::lookup

#endif  // LEXIRISE
