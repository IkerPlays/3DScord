#ifndef SHA256_H
#define SHA256_H

#include <string>

namespace Utils {

// SHA-256 of a string, as 64 lowercase hex characters (used to store the PIN without keeping it in plain text).
std::string sha256Hex(const std::string &data);

} // namespace Utils

#endif // SHA256_H
