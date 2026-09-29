#include "I18n.h"

#include <cstddef>

#include "I18nStrings.h"

using namespace i18n_strings;

I18n& I18n::getInstance() {
  static I18n instance;
  return instance;
}

const char* I18n::get(StrId id) const {
  const auto index = static_cast<size_t>(id);
  if (index >= static_cast<size_t>(StrId::_COUNT)) {
    return "???";
  }

  // LEXIPOINT (v0.2 V8): English is the one table (gen_i18n.py refuses any other language).
  return STRINGS_EN_DATA + OFFSETS_EN[index];
}
