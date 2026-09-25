#pragma once

// A language's name on the device's screens (the settings groups, the reader menu's Lookup language row).

#include <I18n.h>

#include "Settings.h"

namespace lexipoint {

inline StrId languageName(const Language language) {
  switch (language) {  // no default: -Wswitch names a language added without its name
    case Language::Japanese:
      return StrId::STR_LEXI_SET_JAPANESE;
    case Language::Chinese:
      return StrId::STR_LEXI_SET_CHINESE;
  }
  return StrId::STR_LEXI_SET_JAPANESE;  // not reached
}

}  // namespace lexipoint
