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
constexpr const char* kFullWidthProbe = "\xE5\x9B\xBD";  // 国: one full-width character, for the em

}  // namespace

text::TapContext describePageTap(GfxRenderer& renderer, const int fontId, const Page& page, const text::TokenRef tap,
                                 const text::BookLanguage& book) {
  const int em = renderer.getTextAdvanceX(fontId, kFullWidthProbe, EpdFontFamily::REGULAR);
  const text::PageModel model = text::buildPageModel(
      page,
      [&renderer, fontId](const char* word, const EpdFontFamily::Style style) {
        return renderer.getTextAdvanceX(fontId, word, style);
      },
      em);
  return text::describeTap(model, tap, book, settingsStore().snapshot());
}

void logTapContext(const text::TapContext& context) {
  if (!context.sentence) {
    LOG_DBG(kLogTag, "no text at the tap");
    return;
  }
  const text::BuiltSentence& s = *context.sentence;
  // Book text only, at debug level (dev builds): never the key or account data.
  LOG_DBG(kLogTag, "lang=%s source=%s offset=%u len=%u truncated=%d/%d sentence=\"%s\"",
          context.language.language ? languageCode(*context.language.language) : "none",
          text::languageSourceName(context.language.source), static_cast<unsigned>(s.tapOffset),
          static_cast<unsigned>(s.tapLength), s.truncatedLeft ? 1 : 0, s.truncatedRight ? 1 : 0, s.text.c_str());
}

}  // namespace lexipoint::lookup

#endif  // LEXIRISE
