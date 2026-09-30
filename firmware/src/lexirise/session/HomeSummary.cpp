#include "HomeSummary.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include "ReadingSession.h"
#include "fontIds.h"
#include "lexirise/card/CardLayout.h"
#include "lexirise/card/CardPainter.h"

namespace lexipoint::session {

std::vector<std::string> takeHomeSummary() {
  std::vector<std::string> lines;
  const std::optional<Summary> summary = readingSession().takeSummary();
  if (!summary) return lines;
  SummaryFormats formats;
  formats.counts = tr(STR_LEXI_SESSION_COUNTS);
  formats.wordsJa = tr(STR_LEXI_SESSION_WORDS_JA);
  formats.wordJa = tr(STR_LEXI_SESSION_WORDS_JA_ONE);
  formats.wordsZh = tr(STR_LEXI_SESSION_WORDS_ZH);
  formats.wordZh = tr(STR_LEXI_SESSION_WORDS_ZH_ONE);
  lines.reserve(2);
  lines.push_back(countsLine(*summary, formats));
  std::string words = wordsLine(*summary, formats);
  if (!words.empty()) lines.push_back(std::move(words));
  LOG_INF("LXSESSION", "summary: %u saved, %u looked up, count %s", summary->saved, summary->lookedUp,
          summary->words ? std::to_string(*summary->words).c_str() : "-");
  return lines;
}

void drawHomeSummary(GfxRenderer& renderer, const std::vector<std::string>& lines) {
  if (lines.empty()) return;
  // The summary holds no CJK: the UI font alone (no reader size is loaded for it).
  card::CardFonts fonts;
  fonts.uiSmall = fonts.ui = fonts.readerSmall = fonts.readerMedium = fonts.readerLarge = fonts.page = SMALL_FONT_ID;
  const card::RendererMetrics metrics(renderer, fonts);
  card::paint(renderer, card::layoutSummary(lines, metrics, renderer.getScreenWidth()), fonts, metrics);
}

}  // namespace lexipoint::session
