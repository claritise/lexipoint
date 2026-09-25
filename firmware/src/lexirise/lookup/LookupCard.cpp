#if LEXIRISE

#include "LookupCard.h"

namespace lexipoint::lookup {

std::string LookupCard::headword() const { return lemma.empty() ? surface : lemma; }

}  // namespace lexipoint::lookup

#endif  // LEXIRISE
