#ifndef KEY_MAP_H
#define KEY_MAP_H

#include <3ds.h>

namespace Utils {
namespace KeyMap {

// Left-handed mode: the face buttons and the D-Pad swap roles (A <-> Right, B <-> Down, X <-> Up, Y <-> Left), so the
// hand that holds the D-Pad presses A, B, X and Y and the other hand moves around.
extern bool leftHanded;

inline u32 remap(u32 keys) {
	if (!leftHanded) {
		return keys;
	}
	const u32 swapped = KEY_A | KEY_B | KEY_X | KEY_Y | KEY_DRIGHT | KEY_DDOWN | KEY_DUP | KEY_DLEFT;
	u32 out = keys & ~swapped;
	if (keys & KEY_A) {
		out |= KEY_DRIGHT;
	}
	if (keys & KEY_DRIGHT) {
		out |= KEY_A;
	}
	if (keys & KEY_B) {
		out |= KEY_DDOWN;
	}
	if (keys & KEY_DDOWN) {
		out |= KEY_B;
	}
	if (keys & KEY_X) {
		out |= KEY_DUP;
	}
	if (keys & KEY_DUP) {
		out |= KEY_X;
	}
	if (keys & KEY_Y) {
		out |= KEY_DLEFT;
	}
	if (keys & KEY_DLEFT) {
		out |= KEY_Y;
	}
	return out;
}

} // namespace KeyMap
} // namespace Utils

// Every screen reads the buttons through these three calls; the macros route them through the mapping. A macro does
// not expand inside its own expansion, so the inner call is the real libctru function.
#define hidKeysDown() (Utils::KeyMap::remap(hidKeysDown()))
#define hidKeysHeld() (Utils::KeyMap::remap(hidKeysHeld()))
#define hidKeysUp() (Utils::KeyMap::remap(hidKeysUp()))

#endif // KEY_MAP_H
