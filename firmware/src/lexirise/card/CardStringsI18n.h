#pragma once

// The card's words in the reader's language (popup-ui.md §4): CardStrings filled from I18n
// (lib/I18n/translations, one key per field, named after it; a language without them falls back to
// English, gen_i18n.py).
// Device only: the host keeps CardStrings' English defaults.

#include "CardModel.h"

namespace lexipoint::card {

CardStrings cardStringsFromI18n();

}  // namespace lexipoint::card
