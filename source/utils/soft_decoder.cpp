#include "utils/soft_decoder.h"
#include "codec_api.h"
#include "log.h"
#include <cstring>

namespace Utils {

bool SoftDecoder::init() {
	shutdown();
	Logger::log("[SoftDecoder] creating");
	if (WelsCreateDecoder(&decoder) != 0 || !decoder) {
		decoder = nullptr;
		return false;
	}
	SDecodingParam param;
	memset(&param, 0, sizeof(param));
	param.sVideoProperty.size = sizeof(param.sVideoProperty);
	param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
	param.uiTargetDqLayer = (uint8_t)-1;
	param.eEcActiveIdc = ERROR_CON_SLICE_COPY;
	param.bParseOnly = false;
	Logger::log("[SoftDecoder] initialising");
	if (decoder->Initialize(&param) != 0) {
		WelsDestroyDecoder(decoder);
		decoder = nullptr;
		return false;
	}
	// Default is the single-threaded path (setting 1 thread after Initialize breaks the contexts).
	Logger::log("[SoftDecoder] openh264 ready");
	return true;
}

void SoftDecoder::shutdown() {
	if (decoder) {
		decoder->Uninitialize();
		WelsDestroyDecoder(decoder);
		decoder = nullptr;
	}
}

int SoftDecoder::decode(const uint8_t *nal, size_t size, Picture &out, bool &haveFrame) {
	haveFrame = false;
	if (!decoder) {
		return -1;
	}
	uint8_t *planes[3] = {nullptr, nullptr, nullptr};
	SBufferInfo info;
	memset(&info, 0, sizeof(info));
	const DECODING_STATE state = decoder->DecodeFrameNoDelay(nal, (int)size, planes, &info);
	if (info.iBufferStatus == 1 && planes[0]) {
		out.y = planes[0];
		out.u = planes[1];
		out.v = planes[2];
		out.width = info.UsrData.sSystemBuffer.iWidth;
		out.height = info.UsrData.sSystemBuffer.iHeight;
		out.strideY = info.UsrData.sSystemBuffer.iStride[0];
		out.strideUV = info.UsrData.sSystemBuffer.iStride[1];
		haveFrame = true;
	}
	return (int)state;
}

} // namespace Utils
