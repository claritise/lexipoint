#if LEXIRISE

#include "CardStringsI18n.h"

#include <I18n.h>

namespace lexipoint::card {

// One line per CardStrings field; test_card_strings.py checks every field has its key and its line here.
CardStrings cardStringsFromI18n() {
  CardStrings s;
  s.levels[0] = tr(STR_LEXI_CARD_LEVELS_0);
  s.levels[1] = tr(STR_LEXI_CARD_LEVELS_1);
  s.levels[2] = tr(STR_LEXI_CARD_LEVELS_2);
  s.levels[3] = tr(STR_LEXI_CARD_LEVELS_3);
  s.levelNames[0] = tr(STR_LEXI_CARD_LEVEL_NAMES_0);
  s.levelNames[1] = tr(STR_LEXI_CARD_LEVEL_NAMES_1);
  s.levelNames[2] = tr(STR_LEXI_CARD_LEVEL_NAMES_2);
  s.levelNames[3] = tr(STR_LEXI_CARD_LEVEL_NAMES_3);
  s.notSaved = tr(STR_LEXI_CARD_NOT_SAVED);
  s.bands[0] = tr(STR_LEXI_CARD_BANDS_0);
  s.bands[1] = tr(STR_LEXI_CARD_BANDS_1);
  s.bands[2] = tr(STR_LEXI_CARD_BANDS_2);
  s.bands[3] = tr(STR_LEXI_CARD_BANDS_3);
  s.translationPending = tr(STR_LEXI_CARD_TRANSLATION_PENDING);
  s.tabsJa[0] = tr(STR_LEXI_CARD_TABS_JA_0);
  s.tabsJa[1] = tr(STR_LEXI_CARD_TABS_JA_1);
  s.tabsJa[2] = tr(STR_LEXI_CARD_TABS_JA_2);
  s.tabsJa[3] = tr(STR_LEXI_CARD_TABS_JA_3);
  s.tabsJa[4] = tr(STR_LEXI_CARD_TABS_JA_4);
  s.tabsZh[0] = tr(STR_LEXI_CARD_TABS_ZH_0);
  s.tabsZh[1] = tr(STR_LEXI_CARD_TABS_ZH_1);
  s.tabsZh[2] = tr(STR_LEXI_CARD_TABS_ZH_2);
  s.tabsZh[3] = tr(STR_LEXI_CARD_TABS_ZH_3);
  s.noExamples = tr(STR_LEXI_CARD_NO_EXAMPLES);
  s.onlyTraditional = tr(STR_LEXI_CARD_ONLY_TRADITIONAL);
  s.hiddenInSimplified = tr(STR_LEXI_CARD_HIDDEN_IN_SIMPLIFIED);
  s.separator = tr(STR_LEXI_CARD_SEPARATOR);
  s.fromYourReading = tr(STR_LEXI_CARD_FROM_YOUR_READING);
  s.thisBook = tr(STR_LEXI_CARD_THIS_BOOK);
  s.page = tr(STR_LEXI_CARD_PAGE);
  s.metBefore = tr(STR_LEXI_CARD_MET_BEFORE);
  s.firstTime = tr(STR_LEXI_CARD_FIRST_TIME);
  s.notInflected = tr(STR_LEXI_CARD_NOT_INFLECTED);
  s.actionUndo = tr(STR_LEXI_CARD_ACTION_UNDO);
  s.actions[0] = tr(STR_LEXI_CARD_ACTIONS_0);
  s.actions[1] = tr(STR_LEXI_CARD_ACTIONS_1);
  s.actions[2] = tr(STR_LEXI_CARD_ACTIONS_2);
  s.line = tr(STR_LEXI_CARD_LINE);
  s.savedAs = tr(STR_LEXI_CARD_SAVED_AS);
  s.now = tr(STR_LEXI_CARD_NOW);
  s.undoSuffix = tr(STR_LEXI_CARD_UNDO_SUFFIX);
  s.readings = tr(STR_LEXI_CARD_READINGS);
  s.kana = tr(STR_LEXI_CARD_KANA);
  s.romaji = tr(STR_LEXI_CARD_ROMAJI);
  s.removed = tr(STR_LEXI_CARD_REMOVED);
  s.saveFailed = tr(STR_LEXI_CARD_SAVE_FAILED);
  s.retrySuffix = tr(STR_LEXI_CARD_RETRY_SUFFIX);
  s.keyRejected = tr(STR_LEXI_CARD_KEY_REJECTED);
  s.rateLimitedTryIn = tr(STR_LEXI_CARD_RATE_LIMITED_TRY_IN);
  s.seconds = tr(STR_LEXI_CARD_SECONDS);
  s.retrying = tr(STR_LEXI_CARD_RETRYING);
  s.offline = tr(STR_LEXI_CARD_OFFLINE);
  s.rateLimited = tr(STR_LEXI_CARD_RATE_LIMITED);
  s.meaningUnavailable = tr(STR_LEXI_CARD_MEANING_UNAVAILABLE);
  s.notYet = tr(STR_LEXI_CARD_NOT_YET);
  s.actionDone[0] = tr(STR_LEXI_CARD_ACTION_DONE_0);
  s.actionDone[1] = tr(STR_LEXI_CARD_ACTION_DONE_1);
  s.actionDone[2] = tr(STR_LEXI_CARD_ACTION_DONE_2);
  return s;
}

}  // namespace lexipoint::card

#endif  // LEXIRISE
