// Host test of the video the console sends: the H.264 encoder (minih264e) makes a stream with a moving picture, the SPS of its key frames is
// rewritten for WebRTC receivers (as the app does before sending) and both streams are written to files, which the script checks with ffmpeg.
// Build and run: see .github/workflows/tests.yml
#include "utils/h264_encoder.h"
#include "utils/h264_sps.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: %s original.h264 rewritten.h264\n", argv[0]);
		return 2;
	}
	const int W = 192, H = 144;
	Utils::H264Encoder encoder;
	if (!encoder.init(W, H, 20, 2000)) {
		fprintf(stderr, "the encoder did not start\n");
		return 1;
	}
	std::vector<uint8_t> y(W * H), u(W * H / 4, 100), v(W * H / 4, 160);
	FILE *raw = fopen(argv[1], "wb");
	FILE *rewritten = fopen(argv[2], "wb");
	if (!raw || !rewritten) {
		return 1;
	}
	int keyFrames = 0;
	int rewrites = 0;
	for (int f = 0; f < 60; f++) {
		for (int j = 0; j < H; j++) {
			for (int i = 0; i < W; i++) {
				y[j * W + i] = (uint8_t)(128 + 100 * sin((i + f * 3) * 0.1) * cos(j * 0.1));
			}
		}
		const uint8_t *out = nullptr;
		int n = 0;
		if (!encoder.encode(y.data(), u.data(), v.data(), f == 0, &out, &n) || n <= 0) {
			fprintf(stderr, "frame %d was not encoded\n", f);
			return 1;
		}
		fwrite(out, 1, (size_t)n, raw);
		std::vector<uint8_t> frame(out, out + n);
		const bool hasSps = Utils::rewriteAnnexBSps(frame);
		if (hasSps) {
			rewrites++;
			// A second rewrite must find the VUI there and leave it alone.
			std::vector<uint8_t> again = frame;
			if (Utils::rewriteAnnexBSps(again)) {
				fprintf(stderr, "the SPS was rewritten twice\n");
				return 1;
			}
		}
		if (f == 0 && !hasSps) {
			fprintf(stderr, "the first frame has no SPS to rewrite\n");
			return 1;
		}
		if (hasSps) {
			keyFrames++;
		}
		fwrite(frame.data(), 1, frame.size(), rewritten);
	}
	fclose(raw);
	fclose(rewritten);
	printf("60 frames, %d key frames, %d SPS rewritten\n", keyFrames, rewrites);
	return rewrites >= 1 ? 0 : 1;
}
