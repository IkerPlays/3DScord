#include "ui/camera_screen.h"
#include "core/config.h"
#include "core/i18n.h"
#include "discord/discord_client.h"
#include "log.h"
#include "ui/file_preview_screen.h"
#include "utils/redraw.h"
#include "utils/screenshot.h"
#include "utils/transfers.h"
#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <cstdlib>

namespace UI {

namespace {
constexpr int CAM_W = 400;
constexpr int CAM_H = 240;
constexpr int TEX_W = 512;
constexpr int TEX_H = 256;
} // namespace

std::string CameraScreen::pendingChannel;
bool CameraScreen::pendingInner = false;

void CameraScreen::open(const std::string &channelId) {
	auto &sm = ScreenManager::getInstance();
	if (!Discord::DiscordClient::getInstance().canSendMessage(channelId)) {
		sm.showToast(TR("message.no_permission"));
		return;
	}
	pendingChannel = channelId;
	pendingInner = false;
	sm.pushScreen(ScreenType::CAMERA);
}

CameraScreen::CameraScreen() : channelId(pendingChannel), inner(pendingInner) { memset(&texture, 0, sizeof(texture)); }

CameraScreen::~CameraScreen() { stopCamera(); }

void CameraScreen::onEnter() {
	if (!startCamera()) {
		ScreenManager::getInstance().showToast(TR("photo.failed"));
	}
}

bool CameraScreen::startCamera() {
	if (R_FAILED(camInit())) {
		return false;
	}
	buffer = (unsigned short *)malloc((size_t)CAM_W * CAM_H * 2);
	if (!buffer) {
		camExit();
		return false;
	}
	const u32 select = inner ? SELECT_IN1 : SELECT_OUT1;
	bool ok = R_SUCCEEDED(CAMU_SetSize(select, SIZE_CTR_TOP_LCD, CONTEXT_A));
	ok = ok && R_SUCCEEDED(CAMU_SetOutputFormat(select, OUTPUT_RGB_565, CONTEXT_A));
	ok = ok && R_SUCCEEDED(CAMU_SetNoiseFilter(select, true));
	ok = ok && R_SUCCEEDED(CAMU_SetAutoExposure(select, true));
	ok = ok && R_SUCCEEDED(CAMU_SetAutoWhiteBalance(select, true));
	ok = ok && R_SUCCEEDED(CAMU_SetTrimming(PORT_CAM1, false));
	ok = ok && R_SUCCEEDED(CAMU_GetMaxBytes(&maxBytes, CAM_W, CAM_H));
	ok = ok && R_SUCCEEDED(CAMU_SetTransferBytes(PORT_CAM1, maxBytes, CAM_W, CAM_H));
	ok = ok && R_SUCCEEDED(CAMU_Activate(select));
	if (!ok) {
		CAMU_Activate(SELECT_NONE);
		camExit();
		free(buffer);
		buffer = nullptr;
		return false;
	}
	CAMU_ClearBuffer(PORT_CAM1);
	CAMU_StartCapture(PORT_CAM1);
	if (!textureReady) {
		textureReady = C3D_TexInit(&texture, TEX_W, TEX_H, GPU_RGB565);
		if (textureReady) {
			C3D_TexSetFilter(&texture, GPU_LINEAR, GPU_LINEAR);
		}
	}
	cameraOn = true;
	receiving = false;
	haveFrame = false;
	frameCount = 0;
	Logger::log("[Camera] Started (%s)", inner ? "inner" : "outer");
	return true;
}

void CameraScreen::stopCamera() {
	if (cameraOn) {
		CAMU_StopCapture(PORT_CAM1);
		// A frame may still be on its way into the buffer: wait for it before the buffer goes away.
		if (receiving && event) {
			svcWaitSynchronization(event, 200000000LL);
		}
		if (event) {
			svcCloseHandle(event);
			event = 0;
		}
		CAMU_Activate(SELECT_NONE);
		camExit();
		cameraOn = false;
		receiving = false;
		free(buffer);
		buffer = nullptr;
	}
	if (textureReady) {
		C3D_TexDelete(&texture);
		textureReady = false;
	}
}

// Asks the camera for the next frame and, when it has arrived, turns it into the texture shown on the screen.
void CameraScreen::pumpFrame() {
	if (!cameraOn) {
		return;
	}
	if (!receiving) {
		if (R_SUCCEEDED(CAMU_SetReceiving(&event, buffer, PORT_CAM1, (u32)CAM_W * CAM_H * 2, (s16)maxBytes))) {
			receiving = true;
		}
		return;
	}
	if (svcWaitSynchronization(event, 0) != 0) {
		return;
	}
	svcCloseHandle(event);
	event = 0;
	receiving = false;
	if (textureReady) {
		GSPGPU_FlushDataCache(buffer, (u32)CAM_W * CAM_H * 2);
		C3D_SyncDisplayTransfer((u32 *)buffer, GX_BUFFER_DIM(CAM_W, CAM_H), (u32 *)texture.data,
		                        GX_BUFFER_DIM(TEX_W, TEX_H),
		                        (GX_TRANSFER_FLIP_VERT(1) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
		                         GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGB565) |
		                         GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB565) |
		                         GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO)));
	}
	haveFrame = true;
	frameCount++;
	Utils::requestRedraw();
}

void CameraScreen::update() {
	const u32 kDown = hidKeysDown();
	if (kDown & KEY_B) {
		ScreenManager::getInstance().returnToPreviousScreen();
		return;
	}
	if (kDown & KEY_Y) {
		stopCamera();
		inner = !inner;
		pendingInner = inner;
		if (!startCamera()) {
			ScreenManager::getInstance().showToast(TR("photo.failed"));
		}
		return;
	}
	pumpFrame();

	// A frame or two are dark while the exposure settles.
	if ((kDown & KEY_A) && haveFrame && frameCount > 3 && buffer) {
		const std::string photo = Utils::Screenshot::savePhotoRgb565(buffer, CAM_W, CAM_H);
		if (photo.empty()) {
			ScreenManager::getInstance().showToast(TR("photo.failed"));
			return;
		}
		const std::string target = channelId;
		const long long limit = Discord::DiscordClient::getInstance().getUploadLimit(target);
		stopCamera();
		// The photo is shown before it goes anywhere; after sending, B or the end of the preview comes back here.
		FilePreviewScreen::open(
		    photo, limit, [target, photo]() { Utils::Transfers::getInstance().uploadFile(target, photo, ""); },
		    []() { ScreenManager::getInstance().returnToPreviousScreen(); });
	}
}

void CameraScreen::renderTop(C3D_RenderTarget *target) {
	C2D_TargetClear(target, C2D_Color32(0, 0, 0, 255));
	C2D_SceneBegin(target);
	if (cameraOn && haveFrame && textureReady) {
		Tex3DS_SubTexture sub = {(u16)CAM_W, (u16)CAM_H, 0.0f, 1.0f, CAM_W / (float)TEX_W, 1.0f - CAM_H / (float)TEX_H};
		C2D_Image img = {&texture, &sub};
		C2D_DrawImageAt(img, 0.0f, 0.0f, 0.5f, nullptr, 1.0f, 1.0f);
	} else {
		drawCenteredText(110.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(),
		                 TR(cameraOn ? "common.loading" : "photo.failed"), 400.0f);
	}
}

void CameraScreen::renderBottom(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackgroundDark());
	C2D_SceneBegin(target);
	drawCenteredText(80.0f, 0.5f, 0.6f, 0.6f, ScreenManager::colorSuccess(), TR("camera.take"), 320.0f);
	drawCenteredText(115.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(),
	                 TR(inner ? "camera.switch_to_outer" : "camera.switch_to_inner"), 320.0f);
	drawCenteredText(145.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(), TR("voice_msg.hint_cancel"), 320.0f);
}

} // namespace UI
