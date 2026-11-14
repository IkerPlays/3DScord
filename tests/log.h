// A stand-in for the logger of the app so that the encoder code can be built and tested on a computer.
#pragma once
namespace Logger {
inline void log(const char *, ...) {}
} // namespace Logger
