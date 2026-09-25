#if LEXIRISE

#include "PageTap.h"

#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include "lexirise/LexiriseService.h"
#include "lexirise/settings/BookLanguages.h"
#include "lexirise/settings/SettingsStore.h"
#include "lexirise/text/PageModelAdapter.h"

namespace lexipoint::lookup {
namespace {

constexpr const char* kLogTag = "LXTAP";

}  // namespace

text::PageModel pageModelFor(GfxRenderer& renderer, const int fontId, const Page& page) {
  const int em = renderer.getTextAdvanceX(fontId, kEmProbe, EpdFontFamily::REGULAR);
  return text::buildPageModel(
      page,
      [&renderer, fontId](const char* word, const EpdFontFamily::Style style) {
        return renderer.getTextAdvanceX(fontId, word, style);
      },
      em, renderer.getFontAscenderSize(fontId));
}

text::BookLanguage bookLanguageFor(const std::string_view dcLanguage, const std::string& bookPath) {
  return text::BookLanguage(dcLanguage, bookLanguageStore().get(bookPath));
}

bool lexiriseConfigured(const text::BookLanguage& book) {
  return lookup::lexiriseConfigured(settingsStore().snapshot(), book);
}

api::AccessPolicy::Block takeUnannouncedIf(const Gate gate) {
  if (gate != Gate::Rejected && gate != Gate::RateLimited) return api::AccessPolicy::Block::None;
  return service().takeUnannouncedBlock();
}

bool takeNoKeyNotice() {
  static bool said = false;  // main task only
  const bool first = !said;
  said = true;
  return first;
}

void logTap(const text::TapContext& context) {
  if (!context.sentence) {
    LOG_DBG(kLogTag, "no text at the tap");
    return;
  }
  const text::BuiltSentence& s = *context.sentence;
  const auto code = [](const std::optional<Language>& l) { return l ? languageCode(*l) : "none"; };
  // Book text only, at debug level (dev builds): never the key or account data.
  LOG_DBG(kLogTag, "lang=%s detected=%s source=%s offset=%u len=%u truncated=%d/%d sentence=\"%s\"",
          code(context.language.language), code(context.language.detected),
          text::languageSourceName(context.language.source), static_cast<unsigned>(s.tapOffset),
          static_cast<unsigned>(s.tapLength), s.truncatedLeft ? 1 : 0, s.truncatedRight ? 1 : 0, s.text.c_str());
}

}  // namespace lexipoint::lookup

#endif  // LEXIRISE
