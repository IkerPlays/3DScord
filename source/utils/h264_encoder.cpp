#include "utils/h264_encoder.h"
#include "log.h"
#include "minih264_config.h"
#include "minih264e.h"
#include <cstdlib>
#include <cstring>
#include <malloc.h>

namespace Utils {

bool H264Encoder::init(int width, int height, int keyFrameEvery, int bytesPerFrame) {
	shutdown();
	if (width <= 0 || height <= 0 || width % 16 != 0 || height % 16 != 0) {
		return false;
	}
	H264E_create_param_t param;
	memset(&param, 0, sizeof(param));
	param.width = width;
	param.height = height;
	param.gop = keyFrameEvery;
	param.const_input_flag = 1; // the camera buffer is not touched
	int persistBytes = 0;
	int scratchBytes = 0;
	if (H264E_sizeof(&param, &persistBytes, &scratchBytes) != 0) {
		return false;
	}
	encoder = memalign(64, ((size_t)persistBytes + 63) & ~(size_t)63);
	scratch = memalign(64, ((size_t)scratchBytes + 63) & ~(size_t)63);
	if (!encoder || !scratch || H264E_init((H264E_persist_t *)encoder, &param) != 0) {
		shutdown();
		return false;
	}
	w = width;
	h = height;
	targetBytes = bytesPerFrame;
	Logger::log("[H264] Encoder %dx%d ready (%d + %d bytes)", width, height, persistBytes, scratchBytes);
	return true;
}

void H264Encoder::shutdown() {
	free(encoder);
	free(scratch);
	encoder = nullptr;
	scratch = nullptr;
}

bool H264Encoder::encode(const uint8_t *y, const uint8_t *u, const uint8_t *v, bool forceKey, const uint8_t **out,
                         int *size) {
	if (!encoder) {
		return false;
	}
	H264E_io_yuv_t frame;
	frame.yuv[0] = (unsigned char *)y;
	frame.stride[0] = w;
	frame.yuv[1] = (unsigned char *)u;
	frame.stride[1] = w / 2;
	frame.yuv[2] = (unsigned char *)v;
	frame.stride[2] = w / 2;

	H264E_run_param_t run;
	memset(&run, 0, sizeof(run));
	run.encode_speed = 8; // the fastest settings: a processor like the 3DS's has little time per frame
	run.frame_type = forceKey ? H264E_FRAME_TYPE_KEY : 0;
	run.desired_frame_bytes = targetBytes;
	run.qp_min = 20;
	run.qp_max = 48;

	unsigned char *coded = nullptr;
	int codedBytes = 0;
	if (H264E_encode((H264E_persist_t *)encoder, (H264E_scratch_t *)scratch, &run, &frame, &coded, &codedBytes) != 0) {
		return false;
	}
	*out = coded;
	*size = codedBytes;
	return true;
}

} // namespace Utils
