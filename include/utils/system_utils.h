#pragma once

#include <cstddef>
#include <string>

namespace Utils {
namespace System {

const std::string &getDeviceModelName();
bool isNew3DS();
// Compatibility mode: makes the app behave like an Old 3DS (smaller queues, lighter images) even on a New 3DS.
void setLightMode(bool enabled);
size_t heapFreeBytes();
size_t linearHeapSize();

} // namespace System
} // namespace Utils
