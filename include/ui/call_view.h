#ifndef CALL_VIEW_H
#define CALL_VIEW_H

#include <3ds.h>

namespace UI {

// What the top screen shows while a voice call is on.
//   COMPACT: the person speaking, small, in the top right corner, over the chat.
//   FULL:    everyone in the call as big tiles, with the call time and statistics.
//   HIDDEN:  nothing, so the chat has the whole screen.
// L+R changes the mode. The 3DS cannot play video, so a camera or a shared screen shows as a badge on the tile.
namespace CallView {

enum class Mode { COMPACT, FULL, HIDDEN };

Mode mode();
void cycle();
// True while the full view hides the chat.
bool coversScreen();
void render();

} // namespace CallView

} // namespace UI

#endif // CALL_VIEW_H
