#pragma once

// The reader's input, read straight from the hardware while a blocking Lexirise call runs (its debounced state isn't
// updated meanwhile): a side or power button (HalGPIO::rawInputActive) or a finger on the screen (TouchLine over
// HalGPIO::rawTouchLevel). A page's analysis, the mirror's pages and the card's probe give way to it (v0.2 V7b).
// Device only; the line's logic is TouchLine's.

namespace lexipoint::input {

// The loop's pass, with no call running: `fingerDown` is the debounced state (MappedInputManager). With no finger
// down, the touch line's level now is its idle level (TouchLine::idle); the dev log says it once, and says so if the
// line is given up on.
void sampleIdle(bool fingerDown);
// Input came (net::Abort, VocabPageReader::Cancel): asked in the call's waits.
bool inputCame();

}  // namespace lexipoint::input
