#include "utils/h264_sps.h"
#include <cstring>

namespace Utils {

namespace {

// Reads bits of an RBSP (the emulation prevention bytes are already taken out).
class BitReader {
  public:
	BitReader(const uint8_t *d, size_t n) : data(d), size(n) {}
	bool ok() const { return !failed; }
	uint32_t bits(int n) {
		uint32_t v = 0;
		for (int i = 0; i < n; i++) {
			v = (v << 1) | bit();
		}
		return v;
	}
	uint32_t bit() {
		if (pos >= size * 8) {
			failed = true;
			return 0;
		}
		const uint32_t b = (data[pos >> 3] >> (7 - (pos & 7))) & 1;
		pos++;
		return b;
	}
	uint32_t ue() {
		int zeros = 0;
		while (!failed && bit() == 0) {
			zeros++;
			if (zeros > 31) {
				failed = true;
				return 0;
			}
		}
		if (zeros == 0) {
			return 0;
		}
		return (1u << zeros) - 1 + bits(zeros);
	}
	int32_t se() {
		const uint32_t k = ue();
		return (k & 1) ? (int32_t)((k + 1) / 2) : -(int32_t)(k / 2);
	}

  private:
	const uint8_t *data;
	size_t size;
	size_t pos = 0;
	bool failed = false;
};

class BitWriter {
  public:
	void bit(uint32_t b) {
		if ((count & 7) == 0) {
			bytes.push_back(0);
		}
		if (b) {
			bytes.back() |= (uint8_t)(0x80 >> (count & 7));
		}
		count++;
	}
	void bits(uint32_t v, int n) {
		for (int i = n - 1; i >= 0; i--) {
			bit((v >> i) & 1);
		}
	}
	void ue(uint32_t v) {
		const uint32_t x = v + 1;
		int len = 0;
		while ((x >> len) > 1) {
			len++;
		}
		bits(0, len);
		bits(x, len + 1);
	}
	void se(int32_t v) { ue(v > 0 ? (uint32_t)(2 * v - 1) : (uint32_t)(-2 * v)); }
	std::vector<uint8_t> bytes; // the last byte is padded with zeros

  private:
	size_t count = 0;
};

// Takes the 00 00 03 emulation prevention bytes out.
std::vector<uint8_t> unescape(const uint8_t *d, size_t n) {
	std::vector<uint8_t> out;
	out.reserve(n);
	int zeros = 0;
	for (size_t i = 0; i < n; i++) {
		if (zeros >= 2 && d[i] == 3) {
			zeros = 0;
			continue;
		}
		out.push_back(d[i]);
		zeros = d[i] == 0 ? zeros + 1 : 0;
	}
	return out;
}

// Puts them back: no 00 00 00, 00 00 01, 00 00 02 or 00 00 03 may appear inside a NAL unit.
void escape(const std::vector<uint8_t> &in, std::vector<uint8_t> &out) {
	int zeros = 0;
	for (uint8_t b : in) {
		if (zeros >= 2 && b <= 3) {
			out.push_back(3);
			zeros = 0;
		}
		out.push_back(b);
		zeros = b == 0 ? zeros + 1 : 0;
	}
}

} // namespace

bool rewriteSpsForWebRtc(const uint8_t *nal, size_t len, std::vector<uint8_t> &out) {
	if (len < 5 || (nal[0] & 0x1F) != 7) {
		return false;
	}
	const std::vector<uint8_t> rbsp = unescape(nal + 1, len - 1);
	BitReader r(rbsp.data(), rbsp.size());
	BitWriter w;

	const uint32_t profile = r.bits(8);
	r.bits(8); // constraint flags: replaced below
	r.bits(8); // level: replaced below
	if (profile != 66 && profile != 77 && profile != 88) {
		return false; // the high profiles have more fields; the encoder here makes baseline
	}
	w.bits(profile, 8);
	w.bits(0xE0, 8); // constraint_set0, 1 and 2: the same as WebRTC's default "42e01f"
	w.bits(31, 8);   // level 3.1
	w.ue(r.ue());    // seq_parameter_set_id
	w.ue(r.ue());    // log2_max_frame_num_minus4
	const uint32_t pocType = r.ue();
	w.ue(pocType);
	if (pocType == 0) {
		w.ue(r.ue()); // log2_max_pic_order_cnt_lsb_minus4
	} else if (pocType == 1) {
		w.bit(r.bit());
		w.se(r.se());
		w.se(r.se());
		const uint32_t cycle = r.ue();
		w.ue(cycle);
		for (uint32_t i = 0; i < cycle && r.ok(); i++) {
			w.se(r.se());
		}
	}
	const uint32_t maxRefFrames = r.ue();
	w.ue(maxRefFrames);
	w.bit(r.bit()); // gaps_in_frame_num_value_allowed_flag
	w.ue(r.ue());   // pic_width_in_mbs_minus1
	w.ue(r.ue());   // pic_height_in_map_units_minus1
	const uint32_t frameMbsOnly = r.bit();
	w.bit(frameMbsOnly);
	if (!frameMbsOnly) {
		w.bit(r.bit());
	}
	w.bit(r.bit()); // direct_8x8_inference_flag
	const uint32_t cropping = r.bit();
	w.bit(cropping);
	if (cropping) {
		for (int i = 0; i < 4; i++) {
			w.ue(r.ue());
		}
	}
	const uint32_t vui = r.bit();
	if (!r.ok() || vui) {
		return false;
	}
	// A VUI with only the bitstream restriction: no aspect ratio, overscan, video signal type, chroma location, timing, HRD or picture
	// structure; then the restriction (the values WebRTC writes).
	w.bit(1);
	w.bits(0, 2);
	w.bit(0);
	w.bits(0, 5);
	w.bit(1);
	w.bit(1);  // motion_vectors_over_pic_boundaries_flag
	w.ue(2);   // max_bytes_per_pic_denom
	w.ue(1);   // max_bits_per_mb_denom
	w.ue(16);  // log2_max_mv_length_horizontal
	w.ue(16);  // log2_max_mv_length_vertical
	w.ue(0);   // max_num_reorder_frames: the important one
	w.ue(maxRefFrames); // max_dec_frame_buffering
	w.bit(1);  // rbsp_stop_one_bit; the rest of the byte stays zero

	out.clear();
	out.push_back(nal[0]);
	escape(w.bytes, out);
	return true;
}

bool rewriteAnnexBSps(std::vector<uint8_t> &frame) {
	const size_t n = frame.size();
	size_t i = 0;
	while (i + 4 < n) {
		if (frame[i] == 0 && frame[i + 1] == 0 && frame[i + 2] == 1) {
			const size_t begin = i + 3;
			size_t end = n;
			for (size_t j = begin; j + 2 < n; j++) {
				if (frame[j] == 0 && frame[j + 1] == 0 && (frame[j + 2] == 1 || (frame[j + 2] == 0 && j + 3 < n && frame[j + 3] == 1))) {
					end = j;
					break;
				}
			}
			if ((frame[begin] & 0x1F) == 7) {
				std::vector<uint8_t> rewritten;
				if (!rewriteSpsForWebRtc(frame.data() + begin, end - begin, rewritten)) {
					return false;
				}
				std::vector<uint8_t> result(frame.begin(), frame.begin() + begin);
				result.insert(result.end(), rewritten.begin(), rewritten.end());
				result.insert(result.end(), frame.begin() + end, frame.end());
				frame.swap(result);
				return true;
			}
			i = end;
		} else {
			i++;
		}
	}
	return false;
}

} // namespace Utils
