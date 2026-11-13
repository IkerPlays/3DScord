#ifndef H264_SPS_H
#define H264_SPS_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Utils {

// WebRTC receivers (the browsers and the phones of Discord) take a stream whose SPS has no "bitstream restriction" as one that may
// reorder pictures, and hold frames back or show nothing. This rewrites an SPS NAL unit (the header byte included, no start code) so
// it carries a VUI with max_num_reorder_frames = 0 and max_dec_frame_buffering = max_num_ref_frames, and says it is constrained baseline
// level 3.1 (what Discord negotiates), as the SPS rewriter of WebRTC does. False when the SPS has a VUI already or cannot be read.
bool rewriteSpsForWebRtc(const uint8_t *nal, size_t len, std::vector<uint8_t> &out);

// Finds the SPS in an Annex B frame (start codes 00 00 01 or 00 00 00 01) and rewrites it in place. True when it did.
bool rewriteAnnexBSps(std::vector<uint8_t> &frame);

} // namespace Utils

#endif // H264_SPS_H
