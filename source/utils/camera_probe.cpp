#include "utils/camera_probe.h"
#include "log.h"
#include <cstdlib>

namespace Utils {
namespace CameraProbe {

namespace {
constexpr int W = 400;
constexpr int H = 240;

// How many pixels are not black (any colour of the pixel above 2 of 31).
int litPixels(const u16 *buf) {
	int lit = 0;
	for (int i = 0; i < W * H; i++) {
		const u16 p = buf[i];
		if ((p >> 11) > 2 || ((p >> 5) & 0x3F) > 5 || (p & 0x1F) > 2) {
			lit++;
		}
	}
	return lit;
}
} // namespace

u32 outerCamera() {
	static int cached = -1;
	if (cached >= 0) {
		return (u32)cached;
	}
	u16 *buf = (u16 *)malloc((size_t)W * H * 2);
	if (!buf) {
		return SELECT_OUT1;
	}
	const u32 candidates[2] = {SELECT_OUT1, SELECT_OUT2};
	u32 chosen = SELECT_OUT1;
	bool found = false;
	for (int c = 0; c < 2 && !found; c++) {
		const u32 sel = candidates[c];
		u32 maxBytes = 0;
		bool ok = R_SUCCEEDED(CAMU_SetSize(sel, SIZE_CTR_TOP_LCD, CONTEXT_A));
		ok = ok && R_SUCCEEDED(CAMU_SetOutputFormat(sel, OUTPUT_RGB_565, CONTEXT_A));
		ok = ok && R_SUCCEEDED(CAMU_SetNoiseFilter(sel, true));
		ok = ok && R_SUCCEEDED(CAMU_SetAutoExposure(sel, true));
		ok = ok && R_SUCCEEDED(CAMU_SetAutoWhiteBalance(sel, true));
		ok = ok && R_SUCCEEDED(CAMU_SetTrimming(PORT_CAM1, false));
		ok = ok && R_SUCCEEDED(CAMU_GetMaxBytes(&maxBytes, W, H));
		ok = ok && R_SUCCEEDED(CAMU_SetTransferBytes(PORT_CAM1, maxBytes, W, H));
		ok = ok && R_SUCCEEDED(CAMU_Activate(sel));
		int lit = 0;
		if (ok) {
			CAMU_ClearBuffer(PORT_CAM1);
			CAMU_StartCapture(PORT_CAM1);
			// The first frames are dark while the exposure settles.
			for (int i = 0; i < 6 && ok; i++) {
				Handle event = 0;
				ok = R_SUCCEEDED(CAMU_SetReceiving(&event, buf, PORT_CAM1, (u32)W * H * 2, (s16)maxBytes)) &&
				     R_SUCCEEDED(svcWaitSynchronization(event, 800000000LL));
				if (event) {
					svcCloseHandle(event);
				}
			}
			if (ok) {
				lit = litPixels(buf);
			}
			CAMU_StopCapture(PORT_CAM1);
		}
		CAMU_Activate(SELECT_NONE);
		Logger::log("[Camera] Outer camera %s: %s, %d of %d pixels lit", sel == SELECT_OUT1 ? "OUT1 (right)" : "OUT2 (left)",
		            ok ? "answers" : "does not answer", lit, W * H);
		if (!ok) {
			// After a failed transfer the camera may still write into the buffer: it is kept rather than freed.
			buf = (u16 *)malloc((size_t)W * H * 2);
			if (!buf) {
				return SELECT_OUT1;
			}
			continue;
		}
		if (lit > 1500) {
			chosen = sel;
			found = true;
		}
	}
	free(buf);
	cached = (int)chosen;
	Logger::log("[Camera] Using outer camera %s", chosen == SELECT_OUT1 ? "OUT1" : "OUT2");
	return chosen;
}

} // namespace CameraProbe
} // namespace Utils
