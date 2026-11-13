#ifndef CAMERA_PROBE_H
#define CAMERA_PROBE_H

#include <3ds.h>

namespace Utils {
namespace CameraProbe {

// The outer camera that gives a picture. A New 2DS XL has only one outer camera, and which of the two selectors it answers to is not
// the same on every console: asking for the other one gives a black picture without an error. camInit must have been called and
// the camera must not be capturing. The first call tries both (about a second); the answer is remembered until the app closes.
u32 outerCamera();

} // namespace CameraProbe
} // namespace Utils

#endif // CAMERA_PROBE_H
