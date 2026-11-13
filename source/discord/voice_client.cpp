#include "discord/voice_client.h"
#include "utils/video_receiver.h"
#include "utils/video_sender.h"
#include "core/config.h"
#include <ctime>
#include <sys/stat.h>
#include "utils/call_recorder.h"
#include "discord/discord_client.h"
#include "log.h"
#include "utils/json_utils.h"
#include "utils/redraw.h"
#include "utils/sound_player.h"
#include "utils/worker_thread.h"

#include <3ds.h>
#include <algorithm>
#include <arpa/inet.h>
#include <cstring>
#include <malloc.h>
#include <mbedtls/gcm.h>
#include <netinet/in.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <sys/socket.h>
#include <unistd.h>

namespace Discord {

namespace {
constexpr int VOICE_GATEWAY_VERSION = 8;
constexpr int CLOSE_NORMAL = 1000;
constexpr int CLOSE_DAVE_REQUIRED = 4017;
constexpr int OPUS_PAYLOAD_TYPE = 120;
constexpr int VIDEO_PAYLOAD_TYPE = 101; // H.264, only offered with the video experiment on
// Opus RTP always uses a 48kHz clock regardless of the encoder rate.
constexpr uint32_t OPUS_FRAME_TICKS = 960;
constexpr uint8_t OPUS_SILENCE_FRAME[] = {0xF8, 0xFF, 0xFE};
constexpr int OPUS_SILENCE_REPEATS = 5;

bool isOpusSilence(const uint8_t *data, size_t len) {
	return len == sizeof(OPUS_SILENCE_FRAME) && memcmp(data, OPUS_SILENCE_FRAME, len) == 0;
}
constexpr size_t IP_DISCOVERY_SIZE = 74;
constexpr uint8_t IP_DISCOVERY_BODY_SIZE = 70;
constexpr int RTCP_PT_MIN = 200;
constexpr int RTCP_PT_MAX = 206;
constexpr size_t GCM_TAG_SIZE = 16;
constexpr size_t NONCE_SUFFIX_SIZE = 4;
constexpr int DAVE_PROTOCOL_VERSION = 1;

constexpr int OP_READY = 2;
constexpr int OP_HEARTBEAT = 3;
constexpr int OP_SESSION_DESCRIPTION = 4;
constexpr int OP_SPEAKING = 5;
constexpr int OP_VIDEO = 12;
constexpr int OP_MEDIA_SINK_WANTS = 15;
constexpr int VIDEO_QUALITY_WHILE_SENDING = 20; // less to decode while the encoder runs on the same core
constexpr int VIDEO_QUALITY_WANTED = 40; // percent; the 3DS cannot decode a full quality stream in time
constexpr int OP_HEARTBEAT_ACK = 6;
constexpr int OP_HELLO = 8;
constexpr int OP_CLIENTS_CONNECT = 11;
constexpr int OP_CLIENT_DISCONNECT = 13;
constexpr int OP_DAVE_PREPARE_TRANSITION = 21;
constexpr int OP_DAVE_EXECUTE_TRANSITION = 22;
constexpr int OP_DAVE_TRANSITION_READY = 23;
constexpr int OP_DAVE_PREPARE_EPOCH = 24;
constexpr int OP_MLS_INVALID_COMMIT_WELCOME = 31;

constexpr uint8_t OP_MLS_EXTERNAL_SENDER = 25;
constexpr uint8_t OP_MLS_KEY_PACKAGE = 26;
constexpr uint8_t OP_MLS_PROPOSALS = 27;
constexpr uint8_t OP_MLS_COMMIT_WELCOME = 28;
constexpr uint8_t OP_MLS_ANNOUNCE_COMMIT = 29;
constexpr uint8_t OP_MLS_WELCOME = 30;

// mbedtls ships AES-GCM but only 12-byte-nonce ChaCha20-Poly1305, so XChaCha20
// would need HChaCha20 written by hand. Prefer GCM while the server offers it.
const char *PREFERRED_MODE = "aead_aes256_gcm_rtpsize";

void logMemory(const char *where) {
	struct mallinfo mi = mallinfo();
	Logger::log("[Mem] %-22s heapUsed=%dKB heapFree=%dKB arena=%dKB osFree=%luKB linear=%luKB", where,
	            mi.uordblks / 1024, mi.fordblks / 1024, mi.arena / 1024,
	            (unsigned long)(osGetMemRegionFree(MEMREGION_APPLICATION) / 1024),
	            (unsigned long)(linearSpaceFree() / 1024));
}
} // namespace

VoiceClient &VoiceClient::getInstance() {
	static VoiceClient instance;
	return instance;
}

VoiceClient::~VoiceClient() { disconnect(); }

int VoiceClient::connectedSeconds() const {
	const uint64_t since = establishedAt;
	return since == 0 ? 0 : (int)((osGetTime() - since) / 1000);
}

void VoiceClient::setState(VoiceState s) {
	Utils::requestRedraw();
	if (s == VoiceState::ESTABLISHED && state != VoiceState::ESTABLISHED) {
		Utils::SoundPlayer::getInstance().play(Utils::Sound::VOICE_JOIN);
		rosterPrimed = true;
		if (!ringChannelId.empty()) {
			DiscordClient::getInstance().ringCall(ringChannelId);
			ringChannelId.clear();
			outgoingRingTimer = Utils::SoundPlayer::getInstance().clipFrames(Utils::Sound::VOICE_JOIN);
			ringPlaying = false;
			outgoingRing = true;
		}
	}
	if (s == VoiceState::ESTABLISHED && state != VoiceState::ESTABLISHED) {
		establishedAt = osGetTime();
	} else if (s != VoiceState::ESTABLISHED) {
		establishedAt = 0;
	}
	state = s;
	if (stateCallback) {
		stateCallback(s);
	}
}

void VoiceClient::connect(const std::string &guild, const std::string &channel, bool ringRecipients) {
	disconnect();

	svcGetThreadPriority(&callerPriority, CUR_THREAD_HANDLE);

	ringChannelId = ringRecipients ? channel : std::string();
	outgoingRing = false;
	ringPlaying = false;
	outgoingRingTimer = 0;
	sawRinging = false;

	{
		std::lock_guard<std::mutex> lock(mutex);
		guildId = guild;
		channelId = channel;
		haveState = false;
		haveServer = false;
		sessionId.clear();
		token.clear();
		endpoint.clear();
		serverId.clear();
	}
	serverMuted = false;
	serverDeafened = false;
	mutedBeforeDeafen = false;
	rosterPrimed = false;
	audio.setDeafened(false);
	lastSequence = -1;

	DiscordClient &client = DiscordClient::getInstance();
	client.setVoiceStateCallback(
	    [this](const std::string &s, bool srvMute, bool srvDeaf) { onVoiceState(s, srvMute, srvDeaf); });
	client.setVoiceServerCallback(
	    [this](const std::string &t, const std::string &e, const std::string &s) { onVoiceServer(t, e, s); });

	setState(VoiceState::AWAITING_SERVER);
	client.updateVoiceState(guild, channel, false, false);
}

void VoiceClient::markSpeaking(const std::string &userId) {
	uint64_t now = osGetTime();
	std::unique_lock<std::shared_mutex> lock(speakingMutex);
	auto it = speakingUntil.try_emplace(userId, 0).first;
	if (now >= it->second) {
		Utils::requestRedraw();
	}
	it->second = now + SPEAKING_HOLD_MS;
}

void VoiceClient::clearSpeaking(const std::string &userId) {
	std::unique_lock<std::shared_mutex> lock(speakingMutex);
	if (speakingUntil.erase(userId) > 0) {
		Utils::requestRedraw();
	}
}

void VoiceClient::expireSpeaking() {
	uint64_t now = osGetTime();
	std::unique_lock<std::shared_mutex> lock(speakingMutex);
	for (auto it = speakingUntil.begin(); it != speakingUntil.end();) {
		if (now >= it->second) {
			it = speakingUntil.erase(it);
			Utils::requestRedraw();
		} else {
			++it;
		}
	}
}

bool VoiceClient::isSpeaking(const std::string &userId) const {
	std::shared_lock<std::shared_mutex> lock(speakingMutex);
	auto it = speakingUntil.find(userId);
	return it != speakingUntil.end() && osGetTime() < it->second;
}

void VoiceClient::setUserVolume(const std::string &userId, int volumePercent) {
	Config::getInstance().setUserVolume(userId, volumePercent);
	float factor = (float)volumePercent / 100.0f;
	std::shared_lock<std::shared_mutex> lock(ssrcMutex);
	for (const auto &pair : ssrcToUser) {
		if (pair.second == userId) {
			audio.setSpeakerVolume(pair.first, factor);
		}
	}
}

int VoiceClient::getUserVolume(const std::string &userId) const { return Config::getInstance().getUserVolume(userId); }

void VoiceClient::setMicVolume(int volumePercent) {
	Config::getInstance().setMicVolume(volumePercent);
	capture.setVolume(Config::getInstance().getMicVolume());
}

int VoiceClient::getMicVolume() const { return Config::getInstance().getMicVolume(); }

void VoiceClient::setMuted(bool m) {
	if (capture.isMuted() == m) {
		return;
	}
	capture.setMuted(m);
	Utils::SoundPlayer::getInstance().play(m ? Utils::Sound::MIC_OFF : Utils::Sound::MIC_ON);
	publishVoiceState();
}

void VoiceClient::setDeafened(bool d) {
	if (audio.isDeafened() == d) {
		return;
	}

	if (d) {
		mutedBeforeDeafen = capture.isMuted();
		capture.setMuted(true);
	} else {
		capture.setMuted(mutedBeforeDeafen);
	}

	audio.setDeafened(d);
	Utils::SoundPlayer::getInstance().play(d ? Utils::Sound::HEADPHONE_OFF : Utils::Sound::HEADPHONE_ON);
	publishVoiceState();
}

void VoiceClient::stopOutgoingRing() {
	if (!outgoingRing.exchange(false)) {
		return;
	}
	if (ringPlaying.exchange(false)) {
		Utils::SoundPlayer::getInstance().stop();
	}
}

void VoiceClient::update() {
	expireSpeaking();

	// With the lid closed the console would sleep and the call would stop: keep it awake while a call is on.
	const bool wantAwake = state == VoiceState::ESTABLISHED && Config::getInstance().isKeepCallsAwake();
	if (wantAwake != sleepBlocked) {
		sleepBlocked = wantAwake;
		aptSetSleepAllowed(!wantAwake);
	}

	if (!outgoingRing) {
		return;
	}

	if (state != VoiceState::ESTABLISHED) {
		stopOutgoingRing();
		return;
	}

	bool othersPresent = false;
	{
		std::lock_guard<std::mutex> lock(mutex);
		othersPresent = !roster.empty();
	}
	if (othersPresent) {
		stopOutgoingRing();
		return;
	}

	auto ringingLeft = DiscordClient::getInstance().getCallRingingCount(getChannelId());
	if (ringingLeft) {
		if (*ringingLeft > 0) {
			sawRinging = true;
		} else if (sawRinging) {
			stopOutgoingRing();
			return;
		}
	}

	if (--outgoingRingTimer <= 0) {
		Utils::SoundPlayer &sound = Utils::SoundPlayer::getInstance();
		sound.play(Utils::Sound::CALL_OUTGOING);
		ringPlaying = true;
		outgoingRingTimer = sound.clipFrames(Utils::Sound::CALL_OUTGOING);
		if (outgoingRingTimer <= 0) {
			outgoingRingTimer = 60;
		}
	}
}

std::string VoiceClient::getChannelId() {
	std::lock_guard<std::mutex> lock(mutex);
	return channelId;
}

std::string VoiceClient::getGuildId() {
	std::lock_guard<std::mutex> lock(mutex);
	return guildId;
}

void VoiceClient::publishVoiceState() {
	std::string guild, channel;
	{
		std::lock_guard<std::mutex> lock(mutex);
		guild = guildId;
		channel = channelId;
	}
	if (channel.empty()) {
		return;
	}
	DiscordClient::getInstance().updateVoiceState(guild, channel, capture.isMuted(), audio.isDeafened(), videoSourceNow.load() != 0);
}

void VoiceClient::onVoiceState(const std::string &session, bool srvMute, bool srvDeaf) {
	if (session != DiscordClient::getInstance().getSessionId()) {
		return;
	}

	serverMuted = srvMute;
	serverDeafened = srvDeaf;
	if (srvMute) {
		capture.setMuted(true);
	}

	{
		std::lock_guard<std::mutex> lock(mutex);
		sessionId = session;
		haveState = true;
	}
	tryStartSession();
}

void VoiceClient::onVoiceServer(const std::string &t, const std::string &e, const std::string &s) {
	{
		std::lock_guard<std::mutex> lock(mutex);
		token = t;
		endpoint = e;
		serverId = s;
		haveServer = !t.empty() && !e.empty();
	}
	tryStartSession();
}

void VoiceClient::tryStartSession() {
	{
		std::lock_guard<std::mutex> lock(mutex);
		if (!haveState || !haveServer) {
			return;
		}
	}

	if (worker.joinable()) {
		return;
	}

	stopWorker = false;
	setState(VoiceState::CONNECTING);
	worker = std::thread(&VoiceClient::socketThread, this);
}

void VoiceClient::socketThread() {
	std::string url;
	{
		std::lock_guard<std::mutex> lock(mutex);
		url = "wss://" + endpoint + "/?v=" + std::to_string(VOICE_GATEWAY_VERSION);
	}

	Logger::log("[Voice] Connecting to %s", url.c_str());

	ws.setOnMessage([this](std::string &msg) { handlePayload(msg); });
	ws.setOnBinaryMessage([this](std::string &msg) { handleBinaryPayload(msg); });
	ws.setOnClose([this](int code, const std::string &reason) {
		Logger::log("[Voice] Closed code=%d reason=%s", code, reason.c_str());
		if (code == CLOSE_DAVE_REQUIRED) {
			Logger::log("[Voice] Server requires the DAVE protocol; this client advertises version 0.");
		}
		setState(code == CLOSE_NORMAL ? VoiceState::DISCONNECTED : VoiceState::FAILED);
	});
	ws.setOnError([](const std::string &err) { Logger::log("[Voice] Error: %s", err.c_str()); });

	if (!ws.connect(url)) {
		Logger::log("[Voice] WebSocket connect failed");
		setState(VoiceState::FAILED);
		return;
	}

	setState(VoiceState::IDENTIFYING);
	sendIdentify();

	while (!stopWorker && ws.isConnected()) {
		const bool received = ws.poll();

		if (heartbeatInterval > 0) {
			uint64_t now = osGetTime();
			if (now - lastHeartbeat >= heartbeatInterval) {
				sendHeartbeat();
				lastHeartbeat = now;
			}
		}

		svcSleepThread(received ? Network::WebSocketClient::BUSY_POLL_NS : Network::WebSocketClient::IDLE_POLL_NS);
	}

	ws.disconnect();
	closeUdp();
}

void VoiceClient::sendIdentify() {
	std::lock_guard<std::mutex> lock(mutex);

	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> writer(s);
	writer.StartObject();
	writer.Key("op");
	writer.Int(0);
	writer.Key("d");
	writer.StartObject();
	writer.Key("server_id");
	writer.String(serverId.c_str());
	writer.Key("channel_id");
	writer.String(channelId.c_str());
	writer.Key("user_id");
	writer.String(DiscordClient::getInstance().getCurrentUser().id.c_str());
	writer.Key("session_id");
	writer.String(sessionId.c_str());
	writer.Key("token");
	writer.String(token.c_str());
	writer.Key("video");
	// Settings > Voice > Video (experimental): tells Discord this client can receive video.
	writer.Bool(Config::getInstance().isVideoExperimental());
	if (Config::getInstance().isVideoExperimental()) {
		// The video stream we may send (the camera or the screen), as Discord's own clients declare it.
		writer.Key("streams");
		writer.StartArray();
		writer.StartObject();
		writer.Key("type");
		writer.String("video");
		writer.Key("rid");
		writer.String("100");
		writer.Key("quality");
		writer.Int(100);
		writer.EndObject();
		writer.EndArray();
	}
	writer.Key("max_dave_protocol_version");
	writer.Int(DAVE_PROTOCOL_VERSION);
	writer.EndObject();
	writer.EndObject();

	ws.send(s.GetString());
	Logger::log("[Voice] Sent Identify server=%s channel=%s user=%s session=%s tokenLen=%zu", serverId.c_str(),
	            channelId.c_str(), DiscordClient::getInstance().getCurrentUser().id.c_str(), sessionId.c_str(),
	            token.length());
}

void VoiceClient::sendHeartbeat() {
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> writer(s);
	writer.StartObject();
	writer.Key("op");
	writer.Int(OP_HEARTBEAT);
	writer.Key("d");
	writer.StartObject();
	writer.Key("t");
	writer.Int64((int64_t)osGetTime());
	if (lastSequence >= 0) {
		writer.Key("seq_ack");
		writer.Int64(lastSequence);
	}
	writer.EndObject();
	writer.EndObject();

	ws.send(s.GetString());
}

bool VoiceClient::openUdp() {
	closeUdp();

	udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
	if (udpSocket < 0) {
		Logger::log("[Voice] UDP socket() failed");
		return false;
	}

	sockaddr_in server{};
	server.sin_family = AF_INET;
	server.sin_port = htons((uint16_t)udpPort);
	server.sin_addr.s_addr = inet_addr(udpIp.c_str());

	// Connecting a datagram socket fixes the peer so the NAT mapping that IP
	// discovery reports is the one media will arrive on.
	if (::connect(udpSocket, (sockaddr *)&server, sizeof(server)) < 0) {
		Logger::log("[Voice] UDP connect() to %s:%d failed", udpIp.c_str(), udpPort);
		closeUdp();
		return false;
	}

	Logger::log("[Voice] UDP socket connected to %s:%d", udpIp.c_str(), udpPort);
	return true;
}

void VoiceClient::resetGcm() {
	if (gcm) {
		mbedtls_gcm_free(gcm);
		delete gcm;
		gcm = nullptr;
	}
}

void VoiceClient::closeUdp() {
	if (udpSocket >= 0) {
		close(udpSocket);
		udpSocket = -1;
	}
}

bool VoiceClient::discoverExternalAddress() {
	uint8_t packet[IP_DISCOVERY_SIZE];
	memset(packet, 0, sizeof(packet));
	packet[0] = 0x00;
	packet[1] = 0x01;
	packet[2] = 0x00;
	packet[3] = IP_DISCOVERY_BODY_SIZE;
	packet[4] = (uint8_t)(ssrc >> 24);
	packet[5] = (uint8_t)(ssrc >> 16);
	packet[6] = (uint8_t)(ssrc >> 8);
	packet[7] = (uint8_t)ssrc;

	for (int attempt = 0; attempt < 5 && !stopWorker; attempt++) {
		if (send(udpSocket, packet, sizeof(packet), 0) < 0) {
			Logger::log("[Voice] IP discovery send failed");
			return false;
		}

		fd_set readSet;
		FD_ZERO(&readSet);
		FD_SET(udpSocket, &readSet);
		timeval timeout{};
		timeout.tv_sec = 1;

		int ready = select(udpSocket + 1, &readSet, nullptr, nullptr, &timeout);
		if (ready <= 0) {
			continue;
		}

		uint8_t response[128];
		int received = recv(udpSocket, response, sizeof(response), 0);
		if (received < (int)IP_DISCOVERY_SIZE) {
			continue;
		}
		if (response[0] != 0x00 || response[1] != 0x02) {
			continue;
		}

		char address[65];
		memcpy(address, response + 8, 64);
		address[64] = '\0';

		externalIp = address;
		externalPort = (response[72] << 8) | response[73];
		Logger::log("[Voice] IP discovery: %s:%d", externalIp.c_str(), externalPort);
		return true;
	}

	Logger::log("[Voice] IP discovery timed out");
	return false;
}

void VoiceClient::sendSelectProtocol() {
	std::string mode = PREFERRED_MODE;
	bool offered = false;
	for (const std::string &m : serverModes) {
		if (m == mode) {
			offered = true;
			break;
		}
	}
	if (!offered) {
		Logger::log("[Voice] %s not offered by server; cannot negotiate", mode.c_str());
		setState(VoiceState::FAILED);
		return;
	}

	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> writer(s);
	writer.StartObject();
	writer.Key("op");
	writer.Int(1);
	writer.Key("d");
	writer.StartObject();
	writer.Key("protocol");
	writer.String("udp");
	writer.Key("data");
	writer.StartObject();
	writer.Key("address");
	writer.String(externalIp.c_str());
	writer.Key("port");
	writer.Int(externalPort);
	writer.Key("mode");
	writer.String(mode.c_str());
	writer.EndObject();
	writer.Key("codecs");
	writer.StartArray();
	writer.StartObject();
	writer.Key("name");
	writer.String("opus");
	writer.Key("type");
	writer.String("audio");
	writer.Key("priority");
	writer.Int(1000);
	writer.Key("payload_type");
	writer.Int(OPUS_PAYLOAD_TYPE);
	writer.Key("encode");
	writer.Bool(false);
	writer.Key("decode");
	writer.Bool(true);
	writer.EndObject();
	if (Config::getInstance().isVideoExperimental()) {
		// EXPERIMENTAL: offers H.264 so that Discord sends cameras and screens. They are only counted in the log for now.
		writer.StartObject();
		writer.Key("name");
		writer.String("H264");
		writer.Key("type");
		writer.String("video");
		writer.Key("priority");
		writer.Int(1000);
		writer.Key("payload_type");
		writer.Int(VIDEO_PAYLOAD_TYPE);
		writer.Key("rtx_payload_type");
		writer.Int(VIDEO_PAYLOAD_TYPE + 1);
		writer.Key("encode");
		writer.Bool(true); // the camera can be sent (see setCamera)
		writer.Key("decode");
		writer.Bool(true);
		writer.EndObject();
	}
	writer.EndArray();
	writer.EndObject();
	writer.EndObject();

	ws.send(s.GetString());
	setState(VoiceState::SELECTING_PROTOCOL);
	Logger::log("[Voice] Sent Select Protocol mode=%s addr=%s:%d", mode.c_str(), externalIp.c_str(), externalPort);
}

namespace {
constexpr size_t VIDEO_FRAME_MAX = 256 * 1024;      // the biggest frame that is kept
constexpr size_t VIDEO_FILE_MAX = 3 * 1024 * 1024; // the file of the experiment stops growing here
const uint8_t START_CODE[4] = {0, 0, 0, 1};
} // namespace

void VoiceClient::closeVideoCapture() {
	if (videoFile) {
		fclose((FILE *)videoFile);
		videoFile = nullptr;
	}
	Utils::VideoReceiver::getInstance().stop();
	free(videoAssembly);
	free(videoDecrypted);
	videoAssembly = nullptr;
	videoDecrypted = nullptr;
	videoAssemblyLen = 0;
	videoFramesOk = videoFramesBad = videoFramesDropped = 0;
	videoKeyRequestAt = 0;
	videoKeyRequests = 0;
	videoHaveSeq = false;
	videoFileBytes = 0;
}

// RTCP picture loss indication (RFC 4585): asks the sender for a new key frame. The first 8 bytes stay in the clear and are
// authenticated; the rest is encrypted like the media (the "rtpsize" modes).
void VoiceClient::maybeRequestKeyFrame(uint32_t mediaSsrc, bool force) {
	// Also while nothing has been decrypted yet: the picture cannot start without a key frame, and Discord sends one only every
	// 25 seconds or so.
	if (!gcm || udpSocket < 0 ||
	    !(force || videoFramesOk == 0 || Utils::VideoReceiver::getInstance().wantsKeyFrame())) {
		return;
	}
	const uint64_t now = osGetTime();
	if (!force && now < videoKeyRequestAt) {
		return;
	}
	videoKeyRequestAt = now + 1500;

	uint8_t packet[8 + 4 + GCM_TAG_SIZE + NONCE_SUFFIX_SIZE];
	packet[0] = 0x81; // version 2, feedback type 1 (PLI)
	packet[1] = 206;  // payload specific feedback
	packet[2] = 0;
	packet[3] = 2; // length in 32-bit words minus one
	packet[4] = (uint8_t)(ssrc >> 24);
	packet[5] = (uint8_t)(ssrc >> 16);
	packet[6] = (uint8_t)(ssrc >> 8);
	packet[7] = (uint8_t)ssrc;
	const uint8_t media[4] = {(uint8_t)(mediaSsrc >> 24), (uint8_t)(mediaSsrc >> 16), (uint8_t)(mediaSsrc >> 8),
	                          (uint8_t)mediaSsrc};

	uint8_t iv[12];
	memset(iv, 0, sizeof(iv));
	iv[0] = (uint8_t)(sendNonce >> 24);
	iv[1] = (uint8_t)(sendNonce >> 16);
	iv[2] = (uint8_t)(sendNonce >> 8);
	iv[3] = (uint8_t)sendNonce;
	uint8_t *tag = packet + 8 + 4;
	if (mbedtls_gcm_crypt_and_tag(gcm, MBEDTLS_GCM_ENCRYPT, 4, iv, sizeof(iv), packet, 8, media, packet + 8,
	                              GCM_TAG_SIZE, tag) != 0) {
		return;
	}
	memcpy(tag + GCM_TAG_SIZE, iv, NONCE_SUFFIX_SIZE);
	send(udpSocket, packet, sizeof(packet), 0);
	sendNonce++;
	videoKeyRequests++;
	if (videoKeyRequests <= 3 || videoKeyRequests % 20 == 0) {
		Logger::log("[Video] Asked for a key frame (request %u)", videoKeyRequests);
	}
}

// Joins the RTP packets of an H.264 frame (single NAL units, STAP-A and FU-A) into one Annex B frame, decrypts it with DAVE
// hands it to the video receiver (decoded and shown in the call view) and saves the first part to recordings/video_in_*.264.
void VoiceClient::handleVideoPayload(uint32_t ssrc, uint16_t seq, bool lastOfFrame, const uint8_t *payload, size_t len) {
	if (const uint32_t ask = videoKeyAskSsrc.exchange(0)) {
		maybeRequestKeyFrame(ask, true); // someone just started sending video
	}
	std::string userId;
	{
		std::shared_lock<std::shared_mutex> lock(ssrcMutex);
		auto it = videoSsrcToUser.find(ssrc);
		if (it == videoSsrcToUser.end()) {
			return;
		}
		userId = it->second;
	}
	if (!videoAssembly) {
		videoAssembly = (uint8_t *)malloc(VIDEO_FRAME_MAX);
		videoDecrypted = (uint8_t *)malloc(VIDEO_FRAME_MAX);
		if (!videoAssembly || !videoDecrypted) {
			closeVideoCapture();
			return;
		}
	}

	// A missing packet breaks the frame; it is dropped when its last packet arrives.
	if (videoHaveSeq && seq != (uint16_t)(videoLastSeq + 1)) {
		videoFrameBroken = true;
	}
	videoLastSeq = seq;
	videoHaveSeq = true;

	auto append = [&](const uint8_t *data, size_t n) {
		if (videoAssemblyLen + n > VIDEO_FRAME_MAX) {
			videoFrameBroken = true;
			return;
		}
		memcpy(videoAssembly + videoAssemblyLen, data, n);
		videoAssemblyLen += n;
	};

	if (len >= 2) {
		const int nalType = payload[0] & 0x1F;
		if (nalType >= 1 && nalType <= 23) {
			append(START_CODE, 4);
			append(payload, len);
		} else if (nalType == 24) { // STAP-A: several small NAL units in one packet
			size_t pos = 1;
			while (pos + 2 <= len) {
				const size_t size = ((size_t)payload[pos] << 8) | payload[pos + 1];
				pos += 2;
				if (size == 0 || pos + size > len) {
					videoFrameBroken = true;
					break;
				}
				append(START_CODE, 4);
				append(payload + pos, size);
				pos += size;
			}
		} else if (nalType == 28) { // FU-A: one NAL unit cut into pieces
			const uint8_t indicator = payload[0];
			const uint8_t header = payload[1];
			if (header & 0x80) {
				append(START_CODE, 4);
				const uint8_t nalHeader = (uint8_t)((indicator & 0xE0) | (header & 0x1F));
				append(&nalHeader, 1);
			}
			append(payload + 2, len - 2);
		}
	}

	if (!lastOfFrame) {
		return;
	}

	const bool broken = videoFrameBroken;
	const size_t frameLen = videoAssemblyLen;
	videoFrameBroken = false;
	videoAssemblyLen = 0;
	if (broken || frameLen == 0) {
		videoFramesDropped++;
		Utils::VideoReceiver::getInstance().lost();
		maybeRequestKeyFrame(ssrc);
		return;
	}

	size_t written = 0;
	if (daveVersion != 0) {
		if (!dave.decryptVideo(userId, videoAssembly, frameLen, videoDecrypted, VIDEO_FRAME_MAX, &written) ||
		    written == 0) {
			videoFramesBad++;
			Utils::VideoReceiver::getInstance().lost();
			maybeRequestKeyFrame(ssrc);
			if (videoFramesBad <= 3 || videoFramesBad % 100 == 0) {
				Logger::log("[Video] DAVE could not decrypt a frame of %zu bytes (count %u, %s)", frameLen, videoFramesBad,
				            dave.lastError().c_str());
			}
			return;
		}
	} else {
		memcpy(videoDecrypted, videoAssembly, frameLen);
		written = frameLen;
	}

	videoFramesOk++;
	if (videoFramesOk <= 3 || videoFramesOk % 150 == 0) {
		Logger::log("[Video] Frame %u: %zu bytes sent, %zu decrypted, first NAL type %d (frames lost %u, bad %u)",
		            videoFramesOk, frameLen, written, written > 4 ? (videoDecrypted[4] & 0x1F) : -1, videoFramesDropped,
		            videoFramesBad);
	}

	Utils::VideoReceiver::getInstance().submit(userId, videoDecrypted, written);
	maybeRequestKeyFrame(ssrc);

	// The decrypted frames go to the SD card so the stream can be checked on a computer (VLC, ffprobe).
	if (videoFileBytes < VIDEO_FILE_MAX) {
		if (!videoFile) {
			mkdir("sdmc:/3ds", 0777);
			mkdir(CONFIG_DIR_PATH, 0777);
			const std::string dir = std::string(CONFIG_DIR_PATH) + "/recordings";
			mkdir(dir.c_str(), 0777);
			char name[64];
			const time_t now = time(nullptr);
			strftime(name, sizeof(name), "/video_in_%Y%m%d_%H%M%S.264", gmtime(&now));
			videoFile = fopen((dir + name).c_str(), "wb");
			Logger::log("[Video] Saving what %s sends to %s%s", userId.c_str(), dir.c_str(), name);
		}
		if (videoFile) {
			fwrite(videoDecrypted, 1, written, (FILE *)videoFile);
			videoFileBytes += written;
			if (videoFileBytes >= VIDEO_FILE_MAX) {
				fclose((FILE *)videoFile);
				videoFile = nullptr;
				Logger::log("[Video] Saved %zu bytes: enough for the experiment", videoFileBytes);
			}
		}
	}
}

void VoiceClient::handleRtpPacket(uint8_t *packet, size_t len) {
	if (len < 12 + GCM_TAG_SIZE + NONCE_SUFFIX_SIZE) {
		return;
	}
	if ((packet[0] >> 6) != 2) {
		return;
	}

	uint8_t payloadType = packet[1] & 0x7F;
	// RTCP packets have their type (200-206) in the whole second byte, the marker bit included; masked like an RTP payload type they
	// look like 72-78 (this check used the masked value for a long time, so no RTCP packet was ever recognised).
	const uint8_t rawType = packet[1];
	if (rawType >= RTCP_PT_MIN && rawType <= RTCP_PT_MAX) {
		if (cameraSending.load()) {
			const uint32_t seen = ++rtcpSeenWhileVideo;
			if (seen <= 8 || seen % 100 == 0) {
				Logger::log("[VideoTx] RTCP from the call: type %d, format %d, %zu bytes (count %u)", rawType, packet[0] & 0x1F, len,
				            (unsigned)seen);
			}
			handleRtcp(packet, len);
		}
		return;
	}
	const bool isVideo = payloadType == VIDEO_PAYLOAD_TYPE && Config::getInstance().isVideoExperimental();
	if (payloadType != OPUS_PAYLOAD_TYPE && !isVideo) {
		// With the experiment on, the other kinds of packets are only counted and written to the log.
		if (Config::getInstance().isVideoExperimental()) {
			auto &seen = videoSeen[payloadType];
			seen.first++;
			seen.second += len;
			if (seen.first == 1 || seen.first % 300 == 0) {
				Logger::log("[Voice] Video experiment: payload type %d, %u packets, %zu bytes", payloadType, seen.first,
				            seen.second);
			}
		}
		return;
	}

	int csrcCount = packet[0] & 0x0F;
	bool hasExtension = (packet[0] & 0x10) != 0;
	size_t headerLen = 12 + (size_t)csrcCount * 4 + (hasExtension ? 4 : 0);
	if (len < headerLen + GCM_TAG_SIZE + NONCE_SUFFIX_SIZE) {
		return;
	}

	uint32_t ssrc =
	    ((uint32_t)packet[8] << 24) | ((uint32_t)packet[9] << 16) | ((uint32_t)packet[10] << 8) | (uint32_t)packet[11];
	const uint16_t seq = (uint16_t)(((uint16_t)packet[2] << 8) | packet[3]);

	const uint8_t *suffix = packet + len - NONCE_SUFFIX_SIZE;
	uint8_t iv[12];
	memset(iv, 0, sizeof(iv));
	memcpy(iv, suffix, NONCE_SUFFIX_SIZE);

	size_t bodyLen = len - headerLen - NONCE_SUFFIX_SIZE;
	if (bodyLen < GCM_TAG_SIZE) {
		return;
	}
	size_t cipherLen = bodyLen - GCM_TAG_SIZE;
	const uint8_t *cipher = packet + headerLen;
	const uint8_t *tag = cipher + cipherLen;

	uint8_t *plain = plainBuffer;
	if (cipherLen > sizeof(plainBuffer) || !gcm) {
		return;
	}

	int rc =
	    mbedtls_gcm_auth_decrypt(gcm, cipherLen, iv, sizeof(iv), packet, headerLen, tag, GCM_TAG_SIZE, cipher, plain);

	if (rc != 0) {
		decryptFailures++;
		if (decryptFailures <= 5 || decryptFailures % 200 == 0) {
			Logger::log("[Voice] decrypt failed (%d) count=%lu", rc, (unsigned long)decryptFailures);
		}
		return;
	}

	const uint8_t *opusData = plain;
	size_t opusLen = cipherLen;

	// With rtpsize only the extension preamble is authenticated in the clear;
	// the elements themselves sit at the front of the decrypted body.
	if (hasExtension) {
		size_t extWords = ((size_t)packet[headerLen - 2] << 8) | packet[headerLen - 1];
		size_t extBytes = extWords * 4;
		if (extBytes > opusLen) {
			return;
		}
		if (isVideo && packet[headerLen - 4] == 0xBE && packet[headerLen - 3] == 0xDE) {
			// One-byte header extension elements: id and length in one byte, then the value. The video orientation (id 13, 3GPP
			// "CVO") says how many quarter turns the picture needs (a phone sends the picture as the sensor sees it).
			static uint32_t idsSeen = 0;
			size_t i = 0;
			while (i < extBytes) {
				const uint8_t b = plain[i];
				if (b == 0) {
					i++;
					continue;
				}
				const int id = b >> 4;
				const size_t l = (size_t)(b & 15) + 1;
				if (id == 15 || i + 1 + l > extBytes) {
					break;
				}
				if (!(idsSeen & (1u << id))) {
					idsSeen |= 1u << id;
					Logger::log("[Video] RTP extension id %d, %zu bytes, first byte %02x", id, l, plain[i + 1]);
				}
				if (id == 13) {
					Utils::VideoReceiver::getInstance().setOrientation(plain[i + 1] & 7); // bits 1-0: quarter turns; bit 2: flipped sideways
				}
				i += 1 + l;
			}
		}
		opusData += extBytes;
		opusLen -= extBytes;
	}

	if (opusLen == 0) {
		return;
	}

	if (isVideo) {
		handleVideoPayload(ssrc, seq, (packet[1] & 0x80) != 0, opusData, opusLen);
		return;
	}

	packetsDecoded++;
	if (packetsDecoded == 1) {
		Logger::log("[Voice] First media packet decrypted, ssrc=%lu bytes=%zu", (unsigned long)ssrc, opusLen);
	}

	std::shared_lock<std::shared_mutex> lock(ssrcMutex);
	auto it = ssrcToUser.find(ssrc);
	const std::string *userId = it != ssrcToUser.end() ? &it->second : nullptr;

	if (daveVersion != 0) {
		if (!userId) {
			return;
		}

		size_t written = 0;
		if (!dave.decryptOpus(*userId, opusData, opusLen, daveFrameBuffer, sizeof(daveFrameBuffer), &written) ||
		    written == 0) {
			daveDecryptFailures++;
			if (daveDecryptFailures <= 5 || daveDecryptFailures % 200 == 0) {
				Logger::log("[Voice] DAVE decrypt failed count=%lu", (unsigned long)daveDecryptFailures);
			}
			return;
		}
		opusData = daveFrameBuffer;
		opusLen = written;
	}

	if (userId) {
		if (isOpusSilence(opusData, opusLen)) {
			clearSpeaking(*userId);
		} else {
			markSpeaking(*userId);
		}
	}
	lock.unlock();

	float vol = 1.0f;
	if (userId) {
		vol = (float)Config::getInstance().getUserVolume(*userId) / 100.0f;
	}
	vol *= (float)Config::getInstance().getCallVolume() / 100.0f;
	audio.pushOpus(ssrc, seq, opusData, opusLen, vol);
}

void VoiceClient::sendSpeaking(bool speaking) {
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> writer(s);
	writer.StartObject();
	writer.Key("op");
	writer.Int(5);
	writer.Key("d");
	writer.StartObject();
	writer.Key("speaking");
	writer.Int(speaking ? 1 : 0);
	writer.Key("delay");
	writer.Int(0);
	writer.Key("ssrc");
	writer.Uint(ssrc);
	writer.EndObject();
	writer.EndObject();

	ws.send(s.GetString());
	Logger::log("[Voice] Speaking=%d ssrc=%lu", speaking ? 1 : 0, (unsigned long)ssrc);
}

void VoiceClient::sendAudioFrame(const uint8_t *opusData, size_t opusLen) {
	if (!gcm || udpSocket < 0) {
		return;
	}

	if (daveVersion != 0) {
		size_t written = 0;
		if (!dave.encryptOpus(ssrc, opusData, opusLen, daveFrameBuffer, sizeof(daveFrameBuffer), &written) ||
		    written == 0) {
			return;
		}
		opusData = daveFrameBuffer;
		opusLen = written;
	}

	uint8_t *packet = sendBuffer;
	packet[0] = 0x80;
	packet[1] = (uint8_t)OPUS_PAYLOAD_TYPE;
	packet[2] = (uint8_t)(sendSequence >> 8);
	packet[3] = (uint8_t)sendSequence;
	packet[4] = (uint8_t)(sendTimestamp >> 24);
	packet[5] = (uint8_t)(sendTimestamp >> 16);
	packet[6] = (uint8_t)(sendTimestamp >> 8);
	packet[7] = (uint8_t)sendTimestamp;
	packet[8] = (uint8_t)(ssrc >> 24);
	packet[9] = (uint8_t)(ssrc >> 16);
	packet[10] = (uint8_t)(ssrc >> 8);
	packet[11] = (uint8_t)ssrc;

	if (12 + opusLen + GCM_TAG_SIZE + NONCE_SUFFIX_SIZE > sizeof(sendBuffer)) {
		return;
	}

	uint8_t iv[12];
	memset(iv, 0, sizeof(iv));
	iv[0] = (uint8_t)(sendNonce >> 24);
	iv[1] = (uint8_t)(sendNonce >> 16);
	iv[2] = (uint8_t)(sendNonce >> 8);
	iv[3] = (uint8_t)sendNonce;

	uint8_t *cipher = packet + 12;
	uint8_t *tag = cipher + opusLen;

	if (mbedtls_gcm_crypt_and_tag(gcm, MBEDTLS_GCM_ENCRYPT, opusLen, iv, sizeof(iv), packet, 12, opusData, cipher,
	                              GCM_TAG_SIZE, tag) != 0) {
		return;
	}

	uint8_t *suffix = tag + GCM_TAG_SIZE;
	suffix[0] = iv[0];
	suffix[1] = iv[1];
	suffix[2] = iv[2];
	suffix[3] = iv[3];

	size_t total = 12 + opusLen + GCM_TAG_SIZE + NONCE_SUFFIX_SIZE;
	send(udpSocket, packet, total, 0);

	sendNonce++;
	sendSequence++;
	sendTimestamp += OPUS_FRAME_TICKS;
	packetsSent++;
	if (packetsSent == 1) {
		Logger::log("[Voice] First audio frame sent (%zu bytes opus)", opusLen);
	}
}

// Someone asks for a new picture (RTCP feedback: PLI or FIR). The first 8 bytes are in the clear, the rest is encrypted.
void VoiceClient::handleRtcp(const uint8_t *packet, size_t len) {
	if (!gcm || len < 8 + GCM_TAG_SIZE + NONCE_SUFFIX_SIZE + 4) {
		return;
	}
	const int pt = packet[1];
	const int fmt = packet[0] & 0x1F;
	uint8_t iv[12];
	memset(iv, 0, sizeof(iv));
	memcpy(iv, packet + len - NONCE_SUFFIX_SIZE, NONCE_SUFFIX_SIZE);
	const size_t cipherLen = len - 8 - NONCE_SUFFIX_SIZE - GCM_TAG_SIZE;
	uint8_t plain[248];
	if (cipherLen > sizeof(plain)) {
		return;
	}
	if (mbedtls_gcm_auth_decrypt(gcm, cipherLen, iv, sizeof(iv), packet, 8, packet + 8 + cipherLen, GCM_TAG_SIZE, packet + 8,
	                             plain) != 0) {
		return;
	}
	// What the call says to us, for the log: the type, the format, who sent it and the first bytes of the body.
	static int logged = 0;
	if (logged < 24) {
		logged++;
		char hex[40];
		const size_t n = std::min<size_t>(cipherLen, 12);
		for (size_t i = 0; i < n; i++) {
			snprintf(hex + i * 2, 3, "%02x", plain[i]);
		}
		const uint32_t from = ((uint32_t)packet[4] << 24) | ((uint32_t)packet[5] << 16) | ((uint32_t)packet[6] << 8) | packet[7];
		Logger::log("[VideoTx] RTCP type %d format %d from ssrc %u, body %s", pt, fmt, (unsigned)from, hex);
	}
	// Walk the whole compound packet and describe it (above all the receiver reports about our video: does Discord get it?).
	static int described = 0;
	if (described < 40) {
		described++;
		uint8_t all[8 + 256];
		memcpy(all, packet, 8);
		memcpy(all + 8, plain, cipherLen);
		const size_t total = 8 + cipherLen;
		std::string text;
		char tmp[112];
		size_t pos = 0;
		while (pos + 4 <= total) {
			const int type = all[pos + 1];
			const int count = all[pos] & 0x1F;
			const size_t size = ((((size_t)all[pos + 2] << 8) | all[pos + 3]) + 1) * 4;
			if (pos + size > total) {
				break;
			}
			if (type == 201 || type == 200) {
				size_t at = pos + 8 + (type == 200 ? 20 : 0);
				for (int b = 0; b < count && at + 24 <= pos + size; b++, at += 24) {
					const uint8_t *r = all + at;
					const uint32_t src = ((uint32_t)r[0] << 24) | ((uint32_t)r[1] << 16) | ((uint32_t)r[2] << 8) | r[3];
					const uint32_t seqMax = ((uint32_t)r[8] << 24) | ((uint32_t)r[9] << 16) | ((uint32_t)r[10] << 8) | r[11];
					snprintf(tmp, sizeof(tmp), " %s[src %u lost %d/255 total %d maxseq %u]", type == 201 ? "RR" : "SR", (unsigned)src, r[4],
					         (r[5] << 16) | (r[6] << 8) | r[7], (unsigned)seqMax);
					text += tmp;
				}
				if (count == 0) {
					text += type == 201 ? " RR[empty]" : " SR[empty]";
				}
			} else {
				snprintf(tmp, sizeof(tmp), " pt%d/%d(%zu)", type, count, size);
				text += tmp;
			}
			pos += size;
		}
		Logger::log("[VideoTx] RTCP compound (%zu bytes):%s", len, text.c_str());
	}
	// A picture loss indication (or full intra request): someone could not show a picture and wants a new key frame.
	if (pt == 206 && (fmt == 1 || fmt == 4)) {
		Utils::VideoSender::getInstance().requestKeyFrame();
	}
}

void VoiceClient::sendRtp(uint8_t payloadType, uint32_t ssrcOut, uint16_t seq, uint32_t timestamp, bool marker,
                          const uint8_t *payload, size_t len, bool withExtension) {
	// With the extension (playout delay 0, element id 5; the same bytes as the working UDP sender of Discord-video-stream) the header is 16 bytes and the element goes at the front of the encrypted
	// body, as the "rtpsize" modes want.
	const size_t headerLen = withExtension ? 16 : 12;
	const size_t extLen = withExtension ? 4 : 0;
	if (!gcm || udpSocket < 0 || headerLen + extLen + len + GCM_TAG_SIZE + NONCE_SUFFIX_SIZE > sizeof(videoSendBuffer) ||
	    extLen + len > sizeof(videoPlainBuffer)) {
		return;
	}
	uint8_t *packet = videoSendBuffer;
	packet[0] = withExtension ? 0x90 : 0x80;
	packet[1] = (uint8_t)((marker ? 0x80 : 0) | payloadType);
	packet[2] = (uint8_t)(seq >> 8);
	packet[3] = (uint8_t)seq;
	packet[4] = (uint8_t)(timestamp >> 24);
	packet[5] = (uint8_t)(timestamp >> 16);
	packet[6] = (uint8_t)(timestamp >> 8);
	packet[7] = (uint8_t)timestamp;
	packet[8] = (uint8_t)(ssrcOut >> 24);
	packet[9] = (uint8_t)(ssrcOut >> 16);
	packet[10] = (uint8_t)(ssrcOut >> 8);
	packet[11] = (uint8_t)ssrcOut;
	if (withExtension) {
		packet[12] = 0xBE;
		packet[13] = 0xDE;
		packet[14] = 0x00;
		packet[15] = 0x01;
		// Playout delay (extension id 5): the element is 4 bytes, an id and a length (the length is the number of data bytes minus one, so
		// 2 for three bytes), then the minimum and maximum delay in 12 bits each (units of 10 ms). Zero for both asks for no delay.
		videoPlainBuffer[0] = 0x52;
		videoPlainBuffer[1] = 0x00;
		videoPlainBuffer[2] = 0x00;
		videoPlainBuffer[3] = 0x0A; // maximum delay 10 x 10 ms, as the community sender (Discord-video-stream) sends for video
	}
	memcpy(videoPlainBuffer + extLen, payload, len);

	uint8_t iv[12];
	memset(iv, 0, sizeof(iv));
	iv[0] = (uint8_t)(sendNonce >> 24);
	iv[1] = (uint8_t)(sendNonce >> 16);
	iv[2] = (uint8_t)(sendNonce >> 8);
	iv[3] = (uint8_t)sendNonce;
	uint8_t *cipher = packet + headerLen;
	uint8_t *tag = cipher + extLen + len;
	if (mbedtls_gcm_crypt_and_tag(gcm, MBEDTLS_GCM_ENCRYPT, extLen + len, iv, sizeof(iv), packet, headerLen, videoPlainBuffer,
	                              cipher, GCM_TAG_SIZE, tag) != 0) {
		return;
	}
	memcpy(tag + GCM_TAG_SIZE, iv, NONCE_SUFFIX_SIZE);
	send(udpSocket, packet, headerLen + extLen + len + GCM_TAG_SIZE + NONCE_SUFFIX_SIZE, 0);
	sendNonce++;
	videoPacketCount++;
	videoOctetCount += (uint32_t)len;
}

// A whole coded frame (Annex B): encrypted with DAVE as a frame, cut into NAL units, and each one sent as one RTP packet or as
// FU-A fragments (RFC 6184). The marker bit closes the frame.
void VoiceClient::sendVideoFrame(const uint8_t *annexB, size_t len) {
	const uint8_t *data = annexB;
	size_t n = len;
	if (daveVersion != 0) {
		videoOutCrypt.resize(len * 2 + 1024);
		size_t written = 0;
		if (!dave.encryptVideo(videoOwnSsrc, annexB, len, videoOutCrypt.data(), videoOutCrypt.size(), &written) || written == 0) {
			const uint32_t failed = ++videoEncryptFails;
			if (failed <= 3 || failed % 100 == 0) {
				Logger::log("[VideoTx] DAVE could not encrypt a frame of %zu bytes (%s)", len, dave.lastError().c_str());
			}
			return;
		}
		data = videoOutCrypt.data();
		n = written;
	}

	struct Nal {
		const uint8_t *p;
		size_t n;
	};
	std::vector<Nal> nals;
	size_t i = 0;
	while (i + 3 < n) {
		if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1) {
			const size_t begin = i + 3;
			size_t end = n;
			for (size_t j = begin; j + 2 < n; j++) {
				if (data[j] == 0 && data[j + 1] == 0 && (data[j + 2] == 1 || (data[j + 2] == 0 && j + 3 < n && data[j + 3] == 1))) {
					end = j;
					break;
				}
			}
			if (end > begin) {
				nals.push_back({data + begin, end - begin});
			}
			i = end;
		} else {
			i++;
		}
	}
	if (nals.empty()) {
		return;
	}

	constexpr size_t MTU = 1000;
	const uint32_t timestamp = (uint32_t)((osGetTime() - videoClockStart) * 90);
	uint8_t fragment[MTU + 4];
	for (size_t k = 0; k < nals.size(); k++) {
		const bool lastNal = k + 1 == nals.size();
		const Nal &nal = nals[k];
		// Parameter sets travel together in a STAP-A (RFC 6184 5.7), like WebRTC does before a key frame.
		if ((nal.p[0] & 0x1F) == 7 && k + 1 < nals.size() && (nals[k + 1].p[0] & 0x1F) == 8) {
			const Nal &pps = nals[k + 1];
			if (nal.n + pps.n + 5 <= MTU) {
				uint8_t stap[MTU + 8];
				size_t o = 0;
				stap[o++] = (uint8_t)((nal.p[0] & 0x60) | 24);
				stap[o++] = (uint8_t)(nal.n >> 8);
				stap[o++] = (uint8_t)nal.n;
				memcpy(stap + o, nal.p, nal.n);
				o += nal.n;
				stap[o++] = (uint8_t)(pps.n >> 8);
				stap[o++] = (uint8_t)pps.n;
				memcpy(stap + o, pps.p, pps.n);
				o += pps.n;
				sendRtp(VIDEO_PAYLOAD_TYPE, videoOwnSsrc, videoSendSequence++, timestamp, k + 2 == nals.size(), stap, o, true);
				k++;
				continue;
			}
		}
		if (nal.n <= MTU) {
			sendRtp(VIDEO_PAYLOAD_TYPE, videoOwnSsrc, videoSendSequence++, timestamp, lastNal, nal.p, nal.n, true);
			continue;
		}
		const uint8_t indicator = (uint8_t)((nal.p[0] & 0xE0) | 28);
		const uint8_t type = nal.p[0] & 0x1F;
		size_t offset = 1;
		bool first = true;
		while (offset < nal.n) {
			const size_t chunk = std::min(MTU - 2, nal.n - offset);
			const bool end = offset + chunk == nal.n;
			fragment[0] = indicator;
			fragment[1] = (uint8_t)(type | (first ? 0x80 : 0) | (end ? 0x40 : 0));
			memcpy(fragment + 2, nal.p + offset, chunk);
			sendRtp(VIDEO_PAYLOAD_TYPE, videoOwnSsrc, videoSendSequence++, timestamp, lastNal && end, fragment, chunk + 2, true);
			offset += chunk;
			first = false;
		}
	}
	videoFramesSent++;
	if (videoFramesSent <= 3 || videoFramesSent % 150 == 0) {
		Logger::log("[VideoTx] Sent %u frames (last %zu bytes, %zu NAL units)", videoFramesSent, len, nals.size());
	}
}

// RTCP sender report (RFC 3550) for the video stream, like the clients of Discord send: when it is, how many packets and bytes went. The
// first 8 bytes are in the clear, the rest is encrypted like the media.
void VoiceClient::sendSenderReport() {
	if (!gcm || udpSocket < 0) {
		return;
	}
	uint8_t packet[8 + 20 + GCM_TAG_SIZE + NONCE_SUFFIX_SIZE];
	packet[0] = 0x80;
	packet[1] = 200; // sender report
	packet[2] = 0;
	packet[3] = 6; // seven 32-bit words, minus one
	packet[4] = (uint8_t)(videoOwnSsrc >> 24);
	packet[5] = (uint8_t)(videoOwnSsrc >> 16);
	packet[6] = (uint8_t)(videoOwnSsrc >> 8);
	packet[7] = (uint8_t)videoOwnSsrc;

	uint8_t body[20];
	const time_t now = time(nullptr);
	const uint32_t ntpSeconds = (uint32_t)((uint64_t)now + 2208988800ULL); // seconds since 1900
	const uint32_t rtpNow = (uint32_t)((osGetTime() - videoClockStart) * 90);
	const uint32_t words[5] = {ntpSeconds, 0, rtpNow, videoPacketCount, videoOctetCount};
	for (int i = 0; i < 5; i++) {
		body[i * 4] = (uint8_t)(words[i] >> 24);
		body[i * 4 + 1] = (uint8_t)(words[i] >> 16);
		body[i * 4 + 2] = (uint8_t)(words[i] >> 8);
		body[i * 4 + 3] = (uint8_t)words[i];
	}
	uint8_t iv[12];
	memset(iv, 0, sizeof(iv));
	iv[0] = (uint8_t)(sendNonce >> 24);
	iv[1] = (uint8_t)(sendNonce >> 16);
	iv[2] = (uint8_t)(sendNonce >> 8);
	iv[3] = (uint8_t)sendNonce;
	uint8_t *tag = packet + 8 + 20;
	if (mbedtls_gcm_crypt_and_tag(gcm, MBEDTLS_GCM_ENCRYPT, sizeof(body), iv, sizeof(iv), packet, 8, body, packet + 8,
	                              GCM_TAG_SIZE, tag) != 0) {
		return;
	}
	memcpy(tag + GCM_TAG_SIZE, iv, NONCE_SUFFIX_SIZE);
	send(udpSocket, packet, sizeof(packet), 0);
	sendNonce++;
}

void VoiceClient::pumpVideoOut() {
	if (!cameraSending.load()) {
		return;
	}
	if (Utils::VideoSender::getInstance().takeResolutionChange()) {
		sendVideoSignal(true); // the size of the picture changed: tell Discord
	}
	if (osGetTime() >= videoReportAt) {
		videoReportAt = osGetTime() + 1000;
		sendSenderReport();
	}
	bool key = false;
	// Not more than a frame or two per pass: the audio comes first.
	for (int i = 0; i < 2; i++) {
		if (!Utils::VideoSender::getInstance().takeFrame(videoOutFrame, key)) {
			break;
		}
		sendVideoFrame(videoOutFrame.data(), videoOutFrame.size());
	}
}

// Voice gateway op 12: tells Discord that we send video, and with which ssrc (or that we stopped).
// Media sink wants: how good the pictures we want are (percent). Lower means a smaller picture and fewer frames, which is all a 3DS can
// decode, and less still while it also encodes its own video.
void VoiceClient::sendSinkWants(int quality) {
	rapidjson::StringBuffer sink;
	rapidjson::Writer<rapidjson::StringBuffer> sinkWriter(sink);
	sinkWriter.StartObject();
	sinkWriter.Key("op");
	sinkWriter.Int(OP_MEDIA_SINK_WANTS);
	sinkWriter.Key("d");
	sinkWriter.StartObject();
	sinkWriter.Key("any");
	sinkWriter.Int(quality);
	sinkWriter.EndObject();
	sinkWriter.EndObject();
	ws.send(sink.GetString());
	Logger::log("[Video] Asked Discord for video quality %d%%", quality);
}

void VoiceClient::sendVideoSignal(bool on) {
	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> writer(s);
	writer.StartObject();
	writer.Key("op");
	writer.Int(OP_VIDEO);
	writer.Key("d");
	writer.StartObject();
	writer.Key("audio_ssrc");
	writer.Uint(ssrc);
	writer.Key("video_ssrc");
	writer.Uint(on ? videoOwnSsrc : 0);
	writer.Key("rtx_ssrc");
	writer.Uint(on ? videoOwnRtxSsrc : 0);
	writer.Key("streams");
	writer.StartArray();
	if (!on) {
		writer.EndArray(); // when it stops: no streams, like the clients of Discord
		writer.EndObject();
		writer.EndObject();
		ws.send(s.GetString());
		Logger::log("[VideoTx] Told Discord the camera is off");
		return;
	}
	writer.StartObject();
	writer.Key("type");
	writer.String("video");
	writer.Key("rid");
	writer.String("100");
	writer.Key("ssrc");
	writer.Uint(videoOwnSsrc);
	writer.Key("active");
	writer.Bool(on);
	writer.Key("quality");
	writer.Int(100);
	writer.Key("rtx_ssrc");
	writer.Uint(videoOwnRtxSsrc);
	writer.Key("max_bitrate");
	writer.Int(10000000);
	writer.Key("max_framerate");
	writer.Int(Utils::VideoSender::getInstance().fps());
	writer.Key("max_resolution");
	writer.StartObject();
	writer.Key("type");
	writer.String("fixed");
	writer.Key("width");
	writer.Int(Utils::VideoSender::getInstance().width());
	writer.Key("height");
	writer.Int(Utils::VideoSender::getInstance().height());
	writer.EndObject();
	writer.EndObject();
	writer.EndArray();
	writer.EndObject();
	writer.EndObject();
	ws.send(s.GetString());
	Logger::log("[VideoTx] Told Discord the camera is %s (video ssrc %u)", on ? "on" : "off", (unsigned)videoOwnSsrc);
}

bool VoiceClient::setVideoSource(int source) {
	if (source == videoSourceNow.load()) {
		return true;
	}
	if (videoSourceNow.load() != 0) {
		// Stop what was sent before (the camera and the screen use the same stream).
		cameraSending = false;
		videoSourceNow = 0;
		sendVideoSignal(false);
		Utils::VideoSender::getInstance().stop();
	}
	if (source == 0) {
		publishVoiceState();
		sendSinkWants(VIDEO_QUALITY_WANTED);
		return true;
	}
	if (state != VoiceState::ESTABLISHED || !gcm || udpSocket < 0 || !Config::getInstance().isVideoExperimental()) {
		return false;
	}
	if (!Utils::VideoSender::getInstance().start(source == 3   ? Utils::VideoSender::Source::SCREEN
	                                             : source == 2 ? Utils::VideoSender::Source::OUTER
	                                                           : Utils::VideoSender::Source::CAMERA)) {
		return false;
	}
	videoClockStart = osGetTime();
	videoSendSequence = 0;
	videoFramesSent = 0;
	sendSinkWants(VIDEO_QUALITY_WHILE_SENDING);
	rtcpSeenWhileVideo = 0;
	videoEncryptFails = 0;
	videoPacketCount = 0;
	videoOctetCount = 0;
	videoReportAt = 0;
	DiscordClient::getInstance().selfVideoEcho = -1;
	videoSourceNow = source;
	// The same order as the community sender that works with DAVE: first the voice state (self_video), then speaking, then the video
	// signal, and only then the packets. Before, the first key frame could leave before Discord knew about the stream.
	publishVoiceState();
	sendSpeaking(true);
	sendVideoSignal(true);
	cameraSending = true;
	return true;
}

void VoiceClient::mediaThreadEntry(void *arg) { static_cast<VoiceClient *>(arg)->mediaThread(); }

void VoiceClient::mediaThread() {
	Logger::log("[Voice] Media thread started");

	const std::string selfId = DiscordClient::getInstance().getCurrentUser().id;

	constexpr uint64_t TICKS_PER_US = SYSCLOCK_ARM11 / 1000000ULL;
	constexpr uint64_t STATS_INTERVAL_MS = 10000;
	constexpr uint64_t ECHO_CHECK_INTERVAL_MS = 250;
	constexpr uint64_t MIN_WAIT_NS = 2000000ULL;
	constexpr uint64_t MAX_WAIT_NS = 10000000ULL;
	uint64_t statsAt = osGetTime() + STATS_INTERVAL_MS;
	uint64_t echoCheckAt = 0;
	uint64_t lastLoop = svcGetSystemTick();
	uint64_t maxGap = 0, pumpSum = 0, pumpMax = 0, micSum = 0, micMax = 0;
	uint32_t loops = 0;

	while (!stopMedia) {
		int sock = udpSocket;
		if (sock < 0) {
			break;
		}

		const uint64_t woke = svcGetSystemTick();
		if (woke - lastLoop > maxGap) {
			maxGap = woke - lastLoop;
		}
		lastLoop = woke;

		// Not select: it allocates on every call, and moonlight's 3DS port saw poll take 14ms
		// even with no timeout, which is more than the spare time in a 20ms frame.
		for (int i = 0; i < MAX_PACKETS_PER_PASS; i++) {
			const int received = recv(sock, recvBuffer, sizeof(recvBuffer), MSG_DONTWAIT);
			if (received <= 0) {
				break;
			}
			handleRtpPacket(recvBuffer, (size_t)received);
		}

		// Both sides stop together, so the canceller keeps its estimate of the echo
		// path; re-initialising would leak echo for a second after unmuting.
		echo.setPaused(capture.isMuted());

		audio.pump();
		const uint64_t afterPump = svcGetSystemTick();

		const uint64_t now = osGetTime();
		if (now >= echoCheckAt) {
			echo.setEnabled(!osIsHeadsetConnected());
			echoCheckAt = now + ECHO_CHECK_INTERVAL_MS;
		}

		pumpMicrophone(selfId);
		pumpVideoOut();
		const uint64_t afterMic = svcGetSystemTick();

		const uint64_t pumpTicks = afterPump - woke;
		const uint64_t micTicks = afterMic - afterPump;
		pumpSum += pumpTicks;
		micSum += micTicks;
		if (pumpTicks > pumpMax) {
			pumpMax = pumpTicks;
		}
		if (micTicks > micMax) {
			micMax = micTicks;
		}
		loops++;

		if (now >= statsAt) {
			Logger::log(
			    "[VoiceStats] loops=%lu gapMax=%lums recv+pump avg/max=%lu/%luus mic avg/max=%lu/%luus micDropped=%lu",
			    (unsigned long)loops, (unsigned long)(maxGap / TICKS_PER_US / 1000),
			    (unsigned long)(pumpSum / loops / TICKS_PER_US), (unsigned long)(pumpMax / TICKS_PER_US),
			    (unsigned long)(micSum / loops / TICKS_PER_US), (unsigned long)(micMax / TICKS_PER_US),
			    (unsigned long)micFramesDropped);
			audio.logStats();
			loops = 0;
			maxGap = pumpSum = pumpMax = micSum = micMax = 0;
			micFramesDropped = 0;
			statsAt = now + STATS_INTERVAL_MS;
		}

		svcSleepThread(std::clamp(capture.nanosUntilFrame(), MIN_WAIT_NS, MAX_WAIT_NS));
	}

	Logger::log("[Voice] Media thread stopped (decoded=%lu failures=%lu sent=%lu)", (unsigned long)packetsDecoded,
	            (unsigned long)decryptFailures, (unsigned long)packetsSent);
}

void VoiceClient::pumpMicrophone(const std::string &selfId) {
	const bool daveReady = daveVersion == 0 || dave.hasGroup();

	auto setTransmitting = [&](bool active) {
		if (active) {
			markSpeaking(selfId);
		}
		if (active == speakingSent) {
			return;
		}
		// Documented stop signal: five silence frames tell the far side the user
		// stopped and stop Opus interpolating across the gap.
		if (!active) {
			for (int i = 0; i < OPUS_SILENCE_REPEATS; i++) {
				sendAudioFrame(OPUS_SILENCE_FRAME, sizeof(OPUS_SILENCE_FRAME));
			}
		}
		sendSpeaking(active);
		speakingSent = active;
	};

	int16_t pcm[VoiceCapture::FRAME_SAMPLES];
	uint8_t frame[512];

	for (int handled = 0; handled < MAX_MIC_FRAMES_PER_PASS && capture.readFrame(pcm); handled++) {
		if (Utils::CallRecorder::getInstance().isRecording()) {
			int16_t silence[VoiceCapture::FRAME_SAMPLES] = {};
			Utils::CallRecorder::getInstance().addNear(capture.isMuted() ? silence : pcm, VoiceCapture::FRAME_SAMPLES);
		}
		if (!daveReady && !daveWaitLogged) {
			Logger::log("[Voice] Holding audio until the DAVE group is established");
			daveWaitLogged = true;
		}

		// The echo canceller has seen this frame; sending it late would only add delay.
		if (capture.backlogFrames() >= 2) {
			sendTimestamp += OPUS_FRAME_TICKS;
			micFramesDropped++;
			continue;
		}

		// The echo canceller's residue must not count as the user speaking.
		// Settings > Voice > Noise Gate sets how loud the microphone has to be (level 5 is the usual -30 dBFS).
		static const int GATE_LEVELS[11] = {150, 300, 500, 700, 900, MIC_GATE, 1300, 1700, 2200, 3000, 4000};
		int gate = GATE_LEVELS[std::clamp(Config::getInstance().getNoiseGate(), 0, 10)];
		if (echo.isRunning() && echo.isEnabled()) {
			int farGate = audio.farLevel() * ECHO_GATE_PERCENT / 100;
			if (farGate > ECHO_GATE_MAX) {
				farGate = ECHO_GATE_MAX;
			}
			farGate = farGate * capture.getVolume() / 100;
			if (farGate > gate) {
				gate = farGate;
			}
		}

		const bool muted = capture.isMuted();
		const uint64_t now = osGetTime();
		if (pttEnabled) {
			// Push to talk: the button decides, the loudness does not.
			transmitUntil = pttDown ? now + TRANSMIT_HOLD_MS : 0;
		} else if (!muted && capture.lastPeak() >= gate) {
			transmitUntil = now + TRANSMIT_HOLD_MS;
		}
		const bool active = daveReady && !muted && now < transmitUntil;

		setTransmitting(active);

		int encoded = 0;
		if (active) {
			encoded = capture.encode(pcm, frame, sizeof(frame));
		}
		if (encoded > 0) {
			sendAudioFrame(frame, (size_t)encoded);
		} else {
			sendTimestamp += OPUS_FRAME_TICKS;
		}

		audio.pump();
	}

	if (speakingSent && (capture.isMuted() || osGetTime() >= transmitUntil)) {
		setTransmitting(false);
	}
}

std::set<std::string> VoiceClient::recognizedUsers() const {
	std::lock_guard<std::mutex> lock(mutex);
	std::set<std::string> users = roster;
	users.insert(DiscordClient::getInstance().getCurrentUser().id);
	return users;
}

void VoiceClient::recoverFromInvalidGroup() {
	if (invalidGroupRetries >= MAX_INVALID_GROUP_RETRIES) {
		Logger::log("[Voice] Giving up after %d re-add attempts", invalidGroupRetries);
		return;
	}
	invalidGroupRetries++;

	rapidjson::StringBuffer s;
	rapidjson::Writer<rapidjson::StringBuffer> writer(s);
	writer.StartObject();
	writer.Key("op");
	writer.Int(OP_MLS_INVALID_COMMIT_WELCOME);
	writer.Key("d");
	writer.StartObject();
	writer.Key("transition_id");
	writer.Int(currentTransitionId);
	writer.EndObject();
	writer.EndObject();
	ws.send(s.GetString());

	std::string groupIdStr;
	{
		std::lock_guard<std::mutex> lock(mutex);
		groupIdStr = channelId;
	}
	uint64_t groupId = strtoull(groupIdStr.c_str(), nullptr, 10);
	if (!dave.init((uint16_t)daveVersion, groupId, DiscordClient::getInstance().getCurrentUser().id)) {
		Logger::log("[Voice] Re-init failed: %s", dave.lastError().c_str());
		return;
	}

	if (externalSenderPackage.empty()) {
		Logger::log("[Voice] No stored external sender; cannot rebuild the group");
		return;
	}
	dave.setExternalSender(externalSenderPackage);

	auto kp = dave.keyPackage();
	if (kp.empty()) {
		Logger::log("[Voice] Re-add produced an empty key package");
		return;
	}
	sendBinary(OP_MLS_KEY_PACKAGE, kp);

	Logger::log("[Voice] Requested re-add (attempt %d, transition %d)", invalidGroupRetries, currentTransitionId);
}

void VoiceClient::sendBinary(uint8_t opcode, const std::vector<uint8_t> &payload) {
	std::vector<uint8_t> frame;
	frame.reserve(1 + payload.size());
	frame.push_back(opcode);
	frame.insert(frame.end(), payload.begin(), payload.end());
	const bool sent = ws.sendBinary(frame);
	Logger::log("[Voice] -> binary op=%u len=%zu sent=%d", opcode, payload.size(), sent ? 1 : 0);
}

void VoiceClient::handleBinaryPayload(const std::string &message) {
	if (message.size() < 3) {
		return;
	}

	const uint8_t *data = (const uint8_t *)message.data();
	lastSequence = ((int64_t)data[0] << 8) | data[1];
	const uint8_t opcode = data[2];
	const std::vector<uint8_t> payload(data + 3, data + message.size());

	Logger::log("[Voice] <- binary op=%u len=%zu", opcode, payload.size());

	switch (opcode) {
	case OP_MLS_EXTERNAL_SENDER:
		externalSenderPackage = payload;
		dave.setExternalSender(payload);
		if (davePending) {
			auto kp = dave.keyPackage();
			if (!kp.empty()) {
				sendBinary(OP_MLS_KEY_PACKAGE, kp);
			}
		}
		break;

	case OP_MLS_PROPOSALS: {
		std::vector<uint8_t> commitWelcome;
		if (dave.processProposals(payload, recognizedUsers(), commitWelcome) && !commitWelcome.empty()) {
			sendBinary(OP_MLS_COMMIT_WELCOME, commitWelcome);
		}
		break;
	}

	// Only these two carry a transition id ahead of the MLS data; feeding it to
	// MLS makes every parse fail.
	case OP_MLS_WELCOME: {
		if (payload.size() < 2) {
			break;
		}
		currentTransitionId = ((int)payload[0] << 8) | payload[1];
		const std::vector<uint8_t> welcome(payload.begin() + 2, payload.end());

		if (dave.processWelcome(welcome, recognizedUsers())) {
			dave.refreshKeyRatchets(recognizedUsers());
			Logger::log("[Voice] DAVE group established via welcome (transition %d)", currentTransitionId);
			logMemory("after welcome");
		} else {
			recoverFromInvalidGroup();
		}
		break;
	}

	case OP_MLS_ANNOUNCE_COMMIT: {
		if (payload.size() < 2) {
			break;
		}
		currentTransitionId = ((int)payload[0] << 8) | payload[1];
		const std::vector<uint8_t> commit(payload.begin() + 2, payload.end());

		if (dave.processCommit(commit)) {
			dave.refreshKeyRatchets(recognizedUsers());
			Logger::log("[Voice] DAVE group established via commit (transition %d)", currentTransitionId);
			logMemory("after commit");
		} else {
			recoverFromInvalidGroup();
		}
		break;
	}

	default:
		break;
	}
}

void VoiceClient::handlePayload(const std::string &message) {
	Logger::log("[Voice] <- %.300s", message.c_str());

	rapidjson::Document doc;
	doc.Parse(message.c_str());
	if (doc.HasParseError() || !doc.IsObject()) {
		return;
	}

	int op = Utils::Json::getInt(doc, "op", -1);

	if (doc.HasMember("seq") && doc["seq"].IsInt64()) {
		lastSequence = doc["seq"].GetInt64();
	}

	switch (op) {
	case OP_HELLO: {
		if (doc.HasMember("d") && doc["d"].IsObject()) {
			heartbeatInterval = Utils::Json::getUint64(doc["d"], "heartbeat_interval");
		}
		lastHeartbeat = osGetTime();
		Logger::log("[Voice] Hello, heartbeat interval %llu ms", heartbeatInterval);
		break;
	}
	case OP_READY: {
		if (!doc.HasMember("d") || !doc["d"].IsObject()) {
			break;
		}
		const rapidjson::Value &d = doc["d"];
		ssrc = (uint32_t)Utils::Json::getUint64(d, "ssrc");
		// The ssrc of our own video: Discord lists it in "streams", otherwise it is the next two numbers.
		videoOwnSsrc = ssrc + 1;
		videoOwnRtxSsrc = ssrc + 2;
		if (d.HasMember("streams") && d["streams"].IsArray() && !d["streams"].Empty() && d["streams"][0].IsObject()) {
			const uint32_t v = (uint32_t)Utils::Json::getUint64(d["streams"][0], "ssrc");
			const uint32_t r = (uint32_t)Utils::Json::getUint64(d["streams"][0], "rtx_ssrc");
			if (v != 0) {
				videoOwnSsrc = v;
			}
			if (r != 0) {
				videoOwnRtxSsrc = r;
			}
		}
		udpIp = Utils::Json::getString(d, "ip");
		udpPort = Utils::Json::getInt(d, "port");

		serverModes.clear();
		if (d.HasMember("modes") && d["modes"].IsArray()) {
			for (const auto &m : d["modes"].GetArray()) {
				if (m.IsString()) {
					serverModes.push_back(m.GetString());
				}
			}
		}

		std::string modeList;
		for (const std::string &m : serverModes) {
			modeList += (modeList.empty() ? "" : ", ") + m;
		}
		Logger::log("[Voice] Ready ssrc=%lu udp=%s:%d modes=[%s]", (unsigned long)ssrc, udpIp.c_str(), udpPort,
		            modeList.c_str());
		setState(VoiceState::READY);

		if (openUdp() && discoverExternalAddress()) {
			sendSelectProtocol();
		} else {
			setState(VoiceState::FAILED);
		}
		break;
	}
	case OP_SESSION_DESCRIPTION: {
		if (!doc.HasMember("d") || !doc["d"].IsObject()) {
			break;
		}
		const rapidjson::Value &d = doc["d"];
		selectedMode = Utils::Json::getString(d, "mode");

		secretKey.clear();
		if (d.HasMember("secret_key") && d["secret_key"].IsArray()) {
			for (const auto &b : d["secret_key"].GetArray()) {
				if (b.IsInt()) {
					secretKey.push_back((uint8_t)b.GetInt());
				}
			}
		}

		daveVersion = Utils::Json::getInt(d, "dave_protocol_version", 0);
		Logger::log("[Voice] Session Description mode=%s keyBytes=%zu dave=%d audio=%s", selectedMode.c_str(),
		            secretKey.size(), daveVersion, Utils::Json::getString(d, "audio_codec").c_str());

		if (secretKey.size() != 32) {
			Logger::log("[Voice] Unexpected secret key length");
			setState(VoiceState::FAILED);
			break;
		}
		if (daveVersion != 0) {
			// The MLS group is keyed by the voice channel, not the guild.
			uint64_t groupId = strtoull(channelId.c_str(), nullptr, 10);
			if (!dave.init((uint16_t)daveVersion, groupId, DiscordClient::getInstance().getCurrentUser().id)) {
				Logger::log("[Voice] DAVE init failed: %s", dave.lastError().c_str());
				setState(VoiceState::FAILED);
				break;
			}
			davePending = true;
			daveWaitLogged = false;
			invalidGroupRetries = 0;
		}

		resetGcm();
		gcm = new mbedtls_gcm_context;
		mbedtls_gcm_init(gcm);
		if (mbedtls_gcm_setkey(gcm, MBEDTLS_CIPHER_ID_AES, secretKey.data(), 256) != 0) {
			Logger::log("[Voice] gcm_setkey failed");
			setState(VoiceState::FAILED);
			break;
		}

		if (!echo.start()) {
			Logger::log("[Voice] Echo cancellation unavailable");
		}
		audio.setEchoCanceller(&echo);
		capture.setEchoCanceller(&echo);

		if (!audio.start()) {
			setState(VoiceState::FAILED);
			break;
		}

		capture.setMuted(true);
		capture.setVolume(Config::getInstance().getMicVolume());
		if (!capture.start()) {
			Logger::log("[Voice] Capture unavailable; receive only");
		}
		publishVoiceState();
		speakingSent = false;
		sendNonce = 0;
		sendSequence = 0;
		sendTimestamp = 0;
		packetsSent = 0;

		stopMedia = false;
		if (!media && !stopWorker) {
			int wanted = (int)callerPriority - 0x10;
			if (wanted < 0x18) {
				wanted = 0x18;
			}
			const int candidates[] = {wanted, (int)callerPriority, 0x3F};
			auto createOn = [&](int core) {
				for (int prio : candidates) {
					media = threadCreate(mediaThreadEntry, this, MEDIA_STACK_SIZE, prio, core, false);
					if (media) {
						Logger::log("[Voice] Media thread on core %d, priority 0x%X (UI 0x%X)", core, prio,
						            (unsigned)callerPriority);
						return;
					}
				}
			};
			// On a New 3DS the call gets a core of its own instead of preempting the UI.
			if (Utils::WorkerThread::extraCoreAvailable()) {
				createOn(2);
			}
			if (!media) {
				createOn(0);
			}
			if (!media) {
				Logger::log("[Voice] Media thread creation failed");
				setState(VoiceState::FAILED);
				break;
			}
		}

		logMemory("established");
		setState(VoiceState::ESTABLISHED);
		if (Config::getInstance().isVideoExperimental()) {
			sendSinkWants(VIDEO_QUALITY_WANTED);
		}
		break;
	}
	case OP_VIDEO: {
		if (!doc.HasMember("d") || !doc["d"].IsObject()) {
			break;
		}
		const rapidjson::Value &d = doc["d"];
		const uint32_t videoSsrc = (uint32_t)Utils::Json::getUint64(d, "video_ssrc");
		const std::string userId = Utils::Json::getString(d, "user_id");
		if (videoSsrc != 0 && !userId.empty()) {
			std::unique_lock<std::shared_mutex> lock(ssrcMutex);
			videoSsrcToUser[videoSsrc] = userId;
			videoKeyAskSsrc = videoSsrc;
		}
		break;
	}
	case OP_SPEAKING: {
		if (!doc.HasMember("d") || !doc["d"].IsObject()) {
			break;
		}
		const rapidjson::Value &d = doc["d"];
		uint32_t speakerSsrc = (uint32_t)Utils::Json::getUint64(d, "ssrc");
		std::string userId = Utils::Json::getString(d, "user_id");
		if (speakerSsrc != 0 && !userId.empty()) {
			std::unique_lock<std::shared_mutex> lock(ssrcMutex);
			ssrcToUser[speakerSsrc] = userId;
		}
		break;
	}
	case OP_CLIENTS_CONNECT: {
		if (!doc.HasMember("d") || !doc["d"].IsObject()) {
			break;
		}
		const rapidjson::Value &d = doc["d"];
		if (d.HasMember("user_ids") && d["user_ids"].IsArray()) {
			std::lock_guard<std::mutex> lock(mutex);
			bool joined = false;
			for (const auto &u : d["user_ids"].GetArray()) {
				if (u.IsString() && roster.insert(u.GetString()).second) {
					joined = true;
				}
			}
			if (joined) {
				stopOutgoingRing();
			}
			if (joined && rosterPrimed) {
				Utils::SoundPlayer::getInstance().play(Utils::Sound::VOICE_JOIN);
			}
			Logger::log("[Voice] Roster now %zu members", roster.size());
			logMemory("roster change");
		}
		break;
	}
	case OP_CLIENT_DISCONNECT: {
		if (!doc.HasMember("d") || !doc["d"].IsObject()) {
			break;
		}
		std::string userId = Utils::Json::getString(doc["d"], "user_id");
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (roster.erase(userId) > 0) {
				Utils::SoundPlayer::getInstance().play(Utils::Sound::VOICE_PEER_LEFT);
			}
		}
		{
			std::unique_lock<std::shared_mutex> ssrcLock(ssrcMutex);
			for (auto it = ssrcToUser.begin(); it != ssrcToUser.end();) {
				if (it->second == userId) {
					audio.dropSpeaker(it->first);
					it = ssrcToUser.erase(it);
				} else {
					++it;
				}
			}
		}
		break;
	}
	case OP_DAVE_PREPARE_TRANSITION:
	case OP_DAVE_PREPARE_EPOCH: {
		int transitionId = 0;
		if (doc.HasMember("d") && doc["d"].IsObject()) {
			transitionId = Utils::Json::getInt(doc["d"], "transition_id", 0);
		}
		currentTransitionId = transitionId;
		rapidjson::StringBuffer s;
		rapidjson::Writer<rapidjson::StringBuffer> writer(s);
		writer.StartObject();
		writer.Key("op");
		writer.Int(OP_DAVE_TRANSITION_READY);
		writer.Key("d");
		writer.StartObject();
		writer.Key("transition_id");
		writer.Int(transitionId);
		writer.EndObject();
		writer.EndObject();
		ws.send(s.GetString());
		Logger::log("[Voice] DAVE transition %d ready", transitionId);
		break;
	}
	case OP_DAVE_EXECUTE_TRANSITION: {
		dave.refreshKeyRatchets(recognizedUsers());
		Logger::log("[Voice] DAVE transition executed");
		break;
	}
	case OP_MLS_ANNOUNCE_COMMIT:
		if (doc.HasMember("d") && doc["d"].IsObject()) {
			Logger::log("[Voice] MLS commit announced");
		}
		break;
	case OP_HEARTBEAT_ACK:
		break;
	default:
		Logger::log("[Voice] Opcode %d", op);
		break;
	}
}

void VoiceClient::disconnect() {
	stopOutgoingRing();

	if (state == VoiceState::ESTABLISHED) {
		Utils::SoundPlayer::getInstance().play(Utils::Sound::VOICE_LEFT);
	}

	// The worker spawns the media thread, so it must stop first.
	stopWorker = true;
	stopMedia = true;
	if (worker.joinable()) {
		worker.join();
	}

	if (media) {
		threadJoin(media, U64_MAX);
		threadFree(media);
		media = nullptr;
	}
	capture.stop();
	audio.stop();
	capture.setEchoCanceller(nullptr);
	audio.setEchoCanceller(nullptr);
	echo.stop();

	resetGcm();
	{
		std::unique_lock<std::shared_mutex> lock(ssrcMutex);
		ssrcToUser.clear();
		videoSsrcToUser.clear();
	}
	videoSourceNow = 0;
	if (cameraSending.exchange(false)) {
		Utils::VideoSender::getInstance().stop();
	}
	closeVideoCapture(); // the media thread has stopped by now
	{
		std::unique_lock<std::shared_mutex> lock(speakingMutex);
		speakingUntil.clear();
	}

	DiscordClient &client = DiscordClient::getInstance();
	client.setVoiceStateCallback(nullptr);
	client.setVoiceServerCallback(nullptr);

	std::string guild;
	{
		std::lock_guard<std::mutex> lock(mutex);
		guild = guildId;
	}
	if (!guild.empty() || !channelId.empty()) {
		client.updateVoiceState(guild, "", false, false);
	}

	{
		std::lock_guard<std::mutex> lock(mutex);
		guildId.clear();
		channelId.clear();
		roster.clear();
		speakingUntil.clear();
		externalSenderPackage.clear();
		haveState = false;
		haveServer = false;
	}

	heartbeatInterval = 0;
	if (state != VoiceState::FAILED) {
		setState(VoiceState::DISCONNECTED);
	}
}

} // namespace Discord
