#include "ui/camera_screen.h"
#include "utils/video_receiver.h"
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
std::function<void(const std::string &)> CameraScreen::pendingPick;
long long CameraScreen::pendingMax = 0;
bool CameraScreen::leaveOnReturn = false;
int CameraScreen::timerSeconds = 0;

void CameraScreen::open(const std::string &channelId) {
	auto &sm = ScreenManager::getInstance();
	if (!Discord::DiscordClient::getInstance().canSendMessage(channelId)) {
		sm.showToast(TR("message.no_permission"));
		return;
	}
	pendingChannel = channelId;
	pendingPick = nullptr;
	pendingInner = false;
	leaveOnReturn = false;
	sm.pushScreen(ScreenType::CAMERA);
}

void CameraScreen::openForPick(std::function<void(const std::string &)> onPhoto, long long maxBytes) {
	pendingChannel.clear();
	pendingPick = std::move(onPhoto);
	pendingMax = maxBytes;
	pendingInner = false;
	leaveOnReturn = false;
	ScreenManager::getInstance().pushScreen(ScreenType::CAMERA);
}

CameraScreen::CameraScreen() : channelId(pendingChannel), onPhoto(pendingPick), maxBytes_(pendingMax), inner(pendingInner) {
	memset(&texture, 0, sizeof(texture));
	// Back from the preview of a photo that was sent: nothing left to do here.
	leaving = leaveOnReturn;
	leaveOnReturn = false;
}

CameraScreen::~CameraScreen() { stopCamera(); }

void CameraScreen::onEnter() {
	if (leaving) {
		return;
	}
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
		// Written straight into the texture in the order the GPU reads it (the display transfer of the console turns and moves
		// the picture, which left this view black).
		Utils::convertRgb565ToTiledTexture((const uint16_t *)buffer, CAM_W, CAM_H, (uint16_t *)texture.data);
		GSPGPU_FlushDataCache(texture.data, (u32)TEX_W * TEX_H * 2);
	}
	haveFrame = true;
	frameCount++;
	Utils::requestRedraw();
}

void CameraScreen::takePhoto() {
	if (!haveFrame || frameCount <= 3 || !buffer) {
		return;
	}
	const std::string photo = Utils::Screenshot::savePhotoRgb565(buffer, CAM_W, CAM_H);
	if (photo.empty()) {
		ScreenManager::getInstance().showToast(TR("photo.failed"));
		return;
	}
	const std::string target = channelId;
	const auto pick = onPhoto;
	const long long limit = pick ? maxBytes_ : Discord::DiscordClient::getInstance().getUploadLimit(target);
	stopCamera();
	// The photo is shown first. It only goes anywhere when you confirm; B comes back to the camera to try again.
	FilePreviewScreen::open(
	    photo, limit,
	    [target, photo, pick]() {
		    // Coming back to the camera screen closes it: the photo is done.
		    CameraScreen::leaveOnReturn = true;
		    if (pick) {
			    pick(photo);
		    } else {
			    Utils::Transfers::getInstance().uploadFile(target, photo, "");
		    }
	    },
	    []() { ScreenManager::getInstance().returnToPreviousScreen(); });
}

void CameraScreen::update() {
	if (leaving) {
		ScreenManager::getInstance().returnToPreviousScreen();
		return;
	}
	const u32 kDown = hidKeysDown();
	if (kDown & KEY_B) {
		ScreenManager::getInstance().returnToPreviousScreen();
		return;
	}
	if (kDown & KEY_Y) {
		stopCamera();
		inner = !inner;
		pendingInner = inner;
		countdownEnd = 0;
		if (!startCamera()) {
			ScreenManager::getInstance().showToast(TR("photo.failed"));
		}
		return;
	}
	if (kDown & KEY_X) {
		// Self-timer: off, 3 seconds, 10 seconds.
		timerSeconds = timerSeconds == 0 ? 3 : timerSeconds == 3 ? 10 : 0;
		countdownEnd = 0;
		Utils::requestRedraw();
	}
	pumpFrame();

	// The photo is only taken when you press: A, R or a tap on the touch screen (with the timer, that starts the count).
	const bool shutter = (kDown & (KEY_A | KEY_R | KEY_TOUCH)) != 0;
	if (countdownEnd != 0) {
		if (osGetTime() >= countdownEnd) {
			countdownEnd = 0;
			takePhoto();
		} else {
			Utils::requestRedraw();
		}
	} else if (shutter && haveFrame && frameCount > 3) {
		if (timerSeconds > 0) {
			countdownEnd = osGetTime() + (u64)timerSeconds * 1000ULL;
		} else {
			takePhoto();
		}
	}
}

void CameraScreen::renderTop(C3D_RenderTarget *target) {
	C2D_TargetClear(target, C2D_Color32(0, 0, 0, 255));
	C2D_SceneBegin(target);
	if (cameraOn && haveFrame && textureReady) {
		Tex3DS_SubTexture sub = {(u16)TEX_W, (u16)TEX_H, 0.0f, 0.0f, 1.0f, 1.0f};
		C2D_Image img = {&texture, &sub};
		C2D_DrawImageAt(img, 0.0f, 0.0f, 0.5f, nullptr, (float)CAM_W / TEX_W, (float)CAM_H / TEX_H);
	} else {
		drawCenteredText(110.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(),
		                 TR(cameraOn ? "common.loading" : "photo.failed"), 400.0f);
	}
	// The count of the self-timer, big in the middle, so you can get ready.
	if (countdownEnd != 0) {
		const u64 now = osGetTime();
		const int left = countdownEnd > now ? (int)((countdownEnd - now + 999) / 1000) : 0;
		drawCenteredText(80.0f, 0.55f, 2.2f, 2.2f, C2D_Color32(255, 255, 255, 255), std::to_string(left), 400.0f);
	}
}

void CameraScreen::renderBottom(C3D_RenderTarget *target) {
	C2D_TargetClear(target, ScreenManager::colorBackgroundDark());
	C2D_SceneBegin(target);
	drawCenteredText(60.0f, 0.5f, 0.6f, 0.6f, ScreenManager::colorSuccess(), TR("camera.take"), 320.0f);
	drawCenteredText(95.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(),
	                 TR(inner ? "camera.switch_to_outer" : "camera.switch_to_inner"), 320.0f);
	drawCenteredText(120.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(),
	                 TR("camera.timer") + ": " + (timerSeconds == 0 ? TR("common.disabled") : std::to_string(timerSeconds) + " s"),
	                 320.0f);
	drawCenteredText(145.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(), TR("camera.nothing_sent"), 320.0f);
	drawCenteredText(170.0f, 0.5f, 0.5f, 0.5f, ScreenManager::colorTextMuted(), TR("voice_msg.hint_cancel"), 320.0f);
}

} // namespace UI
