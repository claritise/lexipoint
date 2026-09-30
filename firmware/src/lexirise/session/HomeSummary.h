#pragma once

// The reading session's summary on the home screen (C1, C7, v0.2 V6): the device side. HomeActivity takes the lines as
// it opens, draws them over its menu on each frame until the next input, then drops them (docs/v0.1/firmware-base.md
// §3). Device only.

#include <string>
#include <vector>

class GfxRenderer;

namespace lexipoint::session {

// The session's summary as the home screen's lines ("3 saved · 11 looked up", "1,204 words in Japanese"), taken
// (once); empty when there's none.
std::vector<std::string> takeHomeSummary();

// Draws them in their box (card::layoutSummary) into the frame; under the home screen's RenderLock.
void drawHomeSummary(GfxRenderer& renderer, const std::vector<std::string>& lines);

}  // namespace lexipoint::session
