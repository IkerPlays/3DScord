#ifndef CALL_VIEW_H
#define CALL_VIEW_H

#include <3ds.h>

namespace UI {

// What the top screen shows while a voice call is on.
//   COMPACT: the person speaking, small, in the top right corner, over the chat.
//   FULL:    everyone in the call as big tiles, with the call time and statistics.
//   VIDEO:   the camera or shared screen that is coming in, on the whole screen (only when there is one).
//   HIDDEN:  nothing, so the chat has the whole screen.
// L+R changes the mode. A camera or shared screen is decoded in software (EXPERIMENTAL, Settings > Voice > Video).
namespace CallView {

enum class Mode { COMPACT, FULL, VIDEO, HIDDEN };

Mode mode();
void cycle();
// True while the full view hides the chat.
bool coversScreen();
void render();

} // namespace CallView

} // namespace UI

#endif // CALL_VIEW_H
