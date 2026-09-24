#if LEXIRISE

#include "PageTap.h"

#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <Logging.h>

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

void logTap(const text::PageModel& page, const text::TokenRef tap, const text::BookLanguage& book) {
  const text::TapContext context = text::describeTap(page, tap, book, settingsStore().snapshot());
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
