#ifndef SHA256_H
#define SHA256_H

#include <cstddef>
#include <cstdint>
#include <string>

namespace Utils {

// SHA-256 of a long input fed in pieces (used to check downloaded files without holding them in memory).
class Sha256 {
  public:
	Sha256();
	void update(const void *data, size_t size);
	std::string finishHex();

  private:
	uint32_t state[8];
	uint8_t buffer[64];
	size_t bufferSize;
	uint64_t totalBytes;
};

// SHA-256 of a string, as 64 lowercase hex characters (used to store the PIN without keeping it in plain text).
std::string sha256Hex(const std::string &data);

} // namespace Utils

#endif // SHA256_H
