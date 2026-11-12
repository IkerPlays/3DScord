#include "utils/mp4_reader.h"
#include <algorithm>
#include <cstring>

namespace Utils {

namespace {

struct Box {
	uint32_t type = 0;
	size_t begin = 0;   // start of the payload
	size_t end = 0;     // end of the box
};

constexpr uint32_t fourcc(const char *s) {
	return ((uint32_t)(uint8_t)s[0] << 24) | ((uint32_t)(uint8_t)s[1] << 16) | ((uint32_t)(uint8_t)s[2] << 8) |
	       (uint32_t)(uint8_t)s[3];
}

uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint64_t be64(const uint8_t *p) { return ((uint64_t)be32(p) << 32) | be32(p + 4); }

// The boxes found between `from` and `to`.
std::vector<Box> boxes(const uint8_t *d, size_t from, size_t to) {
	std::vector<Box> out;
	size_t pos = from;
	while (pos + 8 <= to) {
		uint64_t size = be32(d + pos);
		const uint32_t type = be32(d + pos + 4);
		size_t header = 8;
		if (size == 1) {
			if (pos + 16 > to) {
				break;
			}
			size = be64(d + pos + 8);
			header = 16;
		} else if (size == 0) {
			size = to - pos;
		}
		if (size < header || pos + size > to) {
			break;
		}
		out.push_back({type, pos + header, (size_t)(pos + size)});
		pos += (size_t)size;
	}
	return out;
}

const Box *find(const std::vector<Box> &list, uint32_t type) {
	for (const auto &b : list) {
		if (b.type == type) {
			return &b;
		}
	}
	return nullptr;
}

const uint8_t START[4] = {0, 0, 0, 1};

} // namespace

bool Mp4Video::parse(const uint8_t *d, size_t n, std::string &error) {
	*this = Mp4Video();
	const auto top = boxes(d, 0, n);
	const Box *moov = find(top, fourcc("moov"));
	if (!moov) {
		error = "no moov box (the file may be cut or fragmented)";
		return false;
	}
	for (const auto &trak : boxes(d, moov->begin, moov->end)) {
		if (trak.type != fourcc("trak")) {
			continue;
		}
		const auto trakBoxes = boxes(d, trak.begin, trak.end);
		const Box *mdia = find(trakBoxes, fourcc("mdia"));
		if (!mdia) {
			continue;
		}
		const auto mdiaBoxes = boxes(d, mdia->begin, mdia->end);
		const Box *hdlr = find(mdiaBoxes, fourcc("hdlr"));
		const Box *mdhd = find(mdiaBoxes, fourcc("mdhd"));
		const Box *minf = find(mdiaBoxes, fourcc("minf"));
		if (!hdlr || !mdhd || !minf || hdlr->end - hdlr->begin < 12 || be32(d + hdlr->begin + 8) != fourcc("vide")) {
			continue;
		}
		uint32_t timescale = 0;
		if (mdhd->end - mdhd->begin >= 24) {
			timescale = d[mdhd->begin] == 1 ? be32(d + mdhd->begin + 20) : be32(d + mdhd->begin + 12);
		}
		if (timescale == 0) {
			error = "bad time scale";
			return false;
		}
		const auto minfBoxes = boxes(d, minf->begin, minf->end);
		const Box *stbl = find(minfBoxes, fourcc("stbl"));
		if (!stbl) {
			continue;
		}
		const auto tbl = boxes(d, stbl->begin, stbl->end);
		const Box *stsd = find(tbl, fourcc("stsd"));
		const Box *stts = find(tbl, fourcc("stts"));
		const Box *stsz = find(tbl, fourcc("stsz"));
		const Box *stsc = find(tbl, fourcc("stsc"));
		const Box *stco = find(tbl, fourcc("stco"));
		const Box *co64 = find(tbl, fourcc("co64"));
		const Box *stss = find(tbl, fourcc("stss"));
		if (!stsd || !stts || !stsz || !stsc || (!stco && !co64)) {
			error = "missing sample tables";
			return false;
		}

		// stsd: the first entry must be H.264 (avc1/avc3).
		if (stsd->end - stsd->begin < 16) {
			error = "short stsd";
			return false;
		}
		const size_t entry = stsd->begin + 8;
		const uint32_t entryType = be32(d + entry + 4);
		if (entryType != fourcc("avc1") && entryType != fourcc("avc3")) {
			error = "the video is not H.264";
			return false;
		}
		const size_t entryEnd = std::min<size_t>(stsd->end, entry + be32(d + entry));
		if (entry + 8 + 78 > entryEnd) {
			error = "short sample entry";
			return false;
		}
		width = be16(d + entry + 8 + 24);
		height = be16(d + entry + 8 + 26);
		const auto entryBoxes = boxes(d, entry + 8 + 78, entryEnd);
		const Box *avcC = find(entryBoxes, fourcc("avcC"));
		if (!avcC || avcC->end - avcC->begin < 7) {
			error = "no avcC";
			return false;
		}
		const uint8_t *c = d + avcC->begin;
		lengthSize = (c[4] & 3) + 1;
		size_t p = avcC->begin + 5;
		const int spsCount = d[p] & 0x1F;
		p++;
		for (int i = 0; i < spsCount; i++) {
			if (p + 2 > avcC->end) {
				error = "bad SPS";
				return false;
			}
			const size_t len = be16(d + p);
			p += 2;
			if (p + len > avcC->end) {
				error = "bad SPS";
				return false;
			}
			parameterSets.insert(parameterSets.end(), START, START + 4);
			parameterSets.insert(parameterSets.end(), d + p, d + p + len);
			p += len;
		}
		if (p >= avcC->end) {
			error = "no PPS";
			return false;
		}
		const int ppsCount = d[p];
		p++;
		for (int i = 0; i < ppsCount; i++) {
			if (p + 2 > avcC->end) {
				error = "bad PPS";
				return false;
			}
			const size_t len = be16(d + p);
			p += 2;
			if (p + len > avcC->end) {
				error = "bad PPS";
				return false;
			}
			parameterSets.insert(parameterSets.end(), START, START + 4);
			parameterSets.insert(parameterSets.end(), d + p, d + p + len);
			p += len;
		}

		// Sizes.
		if (stsz->end - stsz->begin < 12) {
			error = "short stsz";
			return false;
		}
		const uint32_t fixedSize = be32(d + stsz->begin + 4);
		const uint32_t count = be32(d + stsz->begin + 8);
		if (count == 0 || count > 200000) {
			error = "too many or no frames";
			return false;
		}
		if (fixedSize == 0 && stsz->begin + 12 + (size_t)count * 4 > stsz->end) {
			error = "short stsz table";
			return false;
		}
		samples.assign(count, Sample());
		for (uint32_t i = 0; i < count; i++) {
			samples[i].size = fixedSize ? fixedSize : be32(d + stsz->begin + 12 + (size_t)i * 4);
		}

		// Times.
		{
			const uint32_t entries = stts->end - stts->begin >= 8 ? be32(d + stts->begin + 4) : 0;
			if (stts->begin + 8 + (size_t)entries * 8 > stts->end) {
				error = "short stts";
				return false;
			}
			uint64_t ticks = 0;
			uint32_t index = 0;
			for (uint32_t e = 0; e < entries && index < count; e++) {
				const uint32_t run = be32(d + stts->begin + 8 + (size_t)e * 8);
				const uint32_t delta = be32(d + stts->begin + 12 + (size_t)e * 8);
				for (uint32_t k = 0; k < run && index < count; k++, index++) {
					samples[index].ms = (uint32_t)(ticks * 1000 / timescale);
					ticks += delta;
				}
			}
			durationMs = (uint32_t)(ticks * 1000 / timescale);
		}

		// Where each sample is: chunks, and how many samples each chunk holds.
		{
			const uint32_t chunks = be32(d + (stco ? stco->begin : co64->begin) + 4);
			const size_t chunkBytes = stco ? 4 : 8;
			const Box *co = stco ? stco : co64;
			if (co->begin + 8 + (size_t)chunks * chunkBytes > co->end ||
			    stsc->end - stsc->begin < 8) {
				error = "short chunk tables";
				return false;
			}
			const uint32_t runs = be32(d + stsc->begin + 4);
			if (stsc->begin + 8 + (size_t)runs * 12 > stsc->end || runs == 0) {
				error = "short stsc";
				return false;
			}
			uint32_t index = 0;
			for (uint32_t r = 0; r < runs && index < count; r++) {
				const uint32_t first = be32(d + stsc->begin + 8 + (size_t)r * 12);
				const uint32_t perChunk = be32(d + stsc->begin + 12 + (size_t)r * 12);
				const uint32_t last = r + 1 < runs ? be32(d + stsc->begin + 8 + (size_t)(r + 1) * 12) : chunks + 1;
				for (uint32_t chunk = first; chunk < last && chunk <= chunks && index < count; chunk++) {
					uint64_t offset = stco ? be32(d + co->begin + 8 + (size_t)(chunk - 1) * 4)
					                       : be64(d + co->begin + 8 + (size_t)(chunk - 1) * 8);
					for (uint32_t k = 0; k < perChunk && index < count; k++, index++) {
						if (offset + samples[index].size > n) {
							error = "a frame is outside the file";
							return false;
						}
						samples[index].offset = (uint32_t)offset;
						offset += samples[index].size;
					}
				}
			}
			if (index != count) {
				error = "the chunk tables do not cover every frame";
				return false;
			}
		}

		// Key frames.
		if (stss && stss->end - stss->begin >= 8) {
			const uint32_t entries = be32(d + stss->begin + 4);
			if (stss->begin + 8 + (size_t)entries * 4 <= stss->end) {
				for (uint32_t e = 0; e < entries; e++) {
					const uint32_t number = be32(d + stss->begin + 8 + (size_t)e * 4);
					if (number >= 1 && number <= count) {
						samples[number - 1].key = true;
					}
				}
			}
		} else {
			for (auto &s : samples) {
				s.key = true;
			}
		}
		if (!samples.empty()) {
			samples[0].key = true;
		}
		return width > 0 && height > 0;
	}
	error = "no video track";
	return false;
}

bool Mp4Video::toAnnexB(const uint8_t *data, size_t dataSize, const Sample &sample, std::vector<uint8_t> &out) const {
	out.clear();
	if ((size_t)sample.offset + sample.size > dataSize) {
		return false;
	}
	if (sample.key) {
		out.insert(out.end(), parameterSets.begin(), parameterSets.end());
	}
	const uint8_t *p = data + sample.offset;
	const uint8_t *end = p + sample.size;
	while (p + lengthSize <= end) {
		uint32_t len = 0;
		for (int i = 0; i < lengthSize; i++) {
			len = (len << 8) | p[i];
		}
		p += lengthSize;
		if (len == 0 || p + len > end) {
			return !out.empty();
		}
		out.insert(out.end(), START, START + 4);
		out.insert(out.end(), p, p + len);
		p += len;
	}
	return !out.empty();
}

} // namespace Utils
