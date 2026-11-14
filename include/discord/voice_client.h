#ifndef VOICE_CLIENT_H
#define VOICE_CLIENT_H

#include "discord/dave_session.h"
#include "discord/voice_audio.h"
#include "discord/voice_capture.h"
#include "network/websocket_client.h"
#include <atomic>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <map>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

struct mbedtls_gcm_context;

namespace Discord {

enum class VoiceState {
	DISCONNECTED,
	AWAITING_SERVER,
	CONNECTING,
	IDENTIFYING,
	READY,
	SELECTING_PROTOCOL,
	ESTABLISHED,
	FAILED,
};

class VoiceClient {
  public:
	static VoiceClient &getInstance();
	// The second connection, to the stream (Go Live) that shares the screen: it sends video only (no audio, no microphone).
	static VoiceClient &getStreamInstance();
	// The third connection, to the stream (Go Live) of someone else that you are watching: it receives video only.
	static VoiceClient &getWatchInstance();

	void connect(const std::string &guildId, const std::string &channelId, bool ringRecipients = false);
	void disconnect();

	VoiceState getState() const { return state; }
	std::string getChannelId();
	std::string getGuildId();

	void update();

	void setStateCallback(std::function<void(VoiceState)> cb) { stateCallback = cb; }

  private:
	VoiceClient() = default;
	~VoiceClient();

	VoiceClient(const VoiceClient &) = delete;
	VoiceClient &operator=(const VoiceClient &) = delete;

	void onVoiceState(const std::string &session, bool srvMute, bool srvDeaf);
	void publishVoiceState();
	void onVoiceServer(const std::string &token, const std::string &endpoint, const std::string &serverId);
	void tryStartSession();
	void socketThread();
	void handlePayload(const std::string &message);
	void sendIdentify();
	void sendHeartbeat();
	void setState(VoiceState s);

	bool openUdp();
	void closeUdp();
	void resetGcm();
	bool discoverExternalAddress();
	void sendSelectProtocol();
	void mediaThread();
	void pumpMicrophone(const std::string &selfId);
	static void mediaThreadEntry(void *arg);
	void handleRtpPacket(uint8_t *packet, size_t len);
	void sendAudioFrame(const uint8_t *opusData, size_t opusLen);
	void sendSpeaking(bool speaking);
	void handleBinaryPayload(const std::string &message);
	void sendBinary(uint8_t opcode, const std::vector<uint8_t> &payload);
	std::set<std::string> recognizedUsers() const;
	void recoverFromInvalidGroup();

	Network::WebSocketClient ws;
	std::thread worker;
	static constexpr size_t MEDIA_STACK_SIZE = 64 * 1024;
	Thread media = nullptr;
	s32 callerPriority = 0x30;
	std::atomic<bool> stopWorker{false};
	std::atomic<bool> stopMedia{false};
	VoiceAudio audio;
	VoiceCapture capture;
	EchoCanceller echo;
	std::atomic<VoiceState> state{VoiceState::DISCONNECTED};
	std::atomic<bool> pttEnabled{false};
	std::atomic<bool> pttDown{false};
	bool sleepBlocked = false;
	std::atomic<bool> serverMuted{false};
	std::atomic<bool> serverDeafened{false};
	bool mutedBeforeDeafen = false;

	mutable std::mutex mutex;
	std::string guildId;
	std::string channelId;
	std::string sessionId;
	std::string token;
	std::string endpoint;
	std::string serverId;
	bool haveState = false;
	bool haveServer = false;

	uint64_t heartbeatInterval = 0;
	uint64_t lastHeartbeat = 0;
	int64_t lastSequence = -1;
	uint32_t ssrc = 0;
	std::string udpIp;
	int udpPort = 0;
	std::vector<std::string> serverModes;

	int udpSocket = -1;
	std::string externalIp;
	int externalPort = 0;
	std::string selectedMode;
	std::vector<uint8_t> secretKey;
	mbedtls_gcm_context *gcm = nullptr;
	mutable std::shared_mutex ssrcMutex;
	std::unordered_map<uint32_t, std::string> ssrcToUser;
	std::set<std::string> roster;
	std::atomic<bool> rosterPrimed{false};
	// Discord clients stop sending audio instead of reliably sending a
	// speaking:0, so activity is tracked by packet arrival and expires.
	mutable std::shared_mutex speakingMutex;
	std::unordered_map<std::string, uint64_t> speakingUntil;
	static constexpr uint64_t SPEAKING_HOLD_MS = 200;
	static constexpr uint64_t TRANSMIT_HOLD_MS = 200;
	static constexpr int MIC_GATE = 1036; // -30 dBFS
	static constexpr int ECHO_GATE_PERCENT = 25;
	static constexpr int ECHO_GATE_MAX = 6000;
	static constexpr int MAX_MIC_FRAMES_PER_PASS = 3;
	static constexpr int MAX_PACKETS_PER_PASS = 8;
	void markSpeaking(const std::string &userId);
	void clearSpeaking(const std::string &userId);
	void expireSpeaking();
	std::vector<uint8_t> externalSenderPackage;
	uint32_t packetsDecoded = 0;
	uint32_t decryptFailures = 0;
	uint32_t daveDecryptFailures = 0;
	uint8_t recvBuffer[1600];
	uint8_t plainBuffer[1500];
	uint8_t daveFrameBuffer[1500];
	uint8_t sendBuffer[1500];
	DaveSession dave;
	int daveVersion = 0;
	bool davePending = false;
	bool daveWaitLogged = false;
	int currentTransitionId = 0;
	int invalidGroupRetries = 0;
	static constexpr int MAX_INVALID_GROUP_RETRIES = 3;
	uint32_t packetsSent = 0;
	uint32_t micFramesDropped = 0;
	uint32_t sendNonce = 0;
	uint16_t sendSequence = 0;
	uint32_t sendTimestamp = 0;
	bool speakingSent = false;
	uint64_t transmitUntil = 0;
	std::atomic<uint64_t> establishedAt{0};
	std::map<int, std::pair<uint32_t, size_t>> videoSeen;
	// Video experiment: the packets of a frame are joined, decrypted and saved to the SD card to be checked on a computer.
	std::unordered_map<uint32_t, std::string> videoSsrcToUser;
	void handleVideoPayload(uint32_t ssrc, uint16_t seq, bool lastOfFrame, const uint8_t *payload, size_t len);
	void closeVideoCapture();
	void maybeRequestKeyFrame(uint32_t mediaSsrc, bool force = false);
	// Own camera.
	std::atomic<bool> cameraSending{false};
	std::atomic<int> videoSourceNow{0};
	std::atomic<uint32_t> rtcpSeenWhileVideo{0};
	std::atomic<uint32_t> videoEncryptFails{0};
	// EXPERIMENT: the way the video packets are built changes every few seconds (0: extension + STAP-A, 1: STAP-A, 2: neither, 3: extension),
	// shown in the call view, to learn which one the other side can show.
	std::atomic<int> videoVariant{0};
	uint64_t videoVariantAt = 0;
	uint32_t videoOwnSsrc = 0;
	uint32_t videoOwnRtxSsrc = 0;
	uint16_t videoSendSequence = 0;
	uint64_t videoClockStart = 0;
	uint32_t videoFramesSent = 0;
	std::vector<uint8_t> videoOutFrame;
	std::vector<uint8_t> videoOutCrypt;
	uint8_t videoSendBuffer[1500];
	void pumpVideoOut();
	void sendVideoFrame(const uint8_t *annexB, size_t len);
	void sendRtp(uint8_t payloadType, uint32_t ssrcOut, uint16_t seq, uint32_t timestamp, bool marker, const uint8_t *payload,
	             size_t len, bool withExtension = false);
	uint8_t videoPlainBuffer[1500];
	void sendVideoSignal(bool on);
	void sendSinkWants(int quality);
	void sendSenderReport();
	uint32_t videoPacketCount = 0;
	uint32_t videoSendFails = 0; // video packets the socket refused (counted and retried; they used to be lost without a trace)
	uint32_t videoOctetCount = 0;
	uint64_t videoReportAt = 0;
	void handleRtcp(const uint8_t *packet, size_t len);
	std::atomic<uint32_t> videoKeyAskSsrc{0}; // set when someone starts sending video: the media thread asks for a key frame at once
	uint64_t videoKeyRequestAt = 0;
	uint32_t videoKeyRequests = 0;
	uint8_t *videoAssembly = nullptr;  // the frame being joined (Annex B)
	uint8_t *videoDecrypted = nullptr; // the same frame after DAVE
	size_t videoAssemblyLen = 0;
	bool videoFrameBroken = false;
	uint16_t videoLastSeq = 0;
	bool videoHaveSeq = false;
	uint32_t videoFramesOk = 0;
	uint32_t videoFramesBad = 0;
	uint32_t videoFramesDropped = 0;
	void *videoFile = nullptr; // FILE *
	size_t videoFileBytes = 0; // video experiment: packets and bytes per payload type
	bool streamMode = false; // this object is a stream connection (to share a screen, or to watch one)
	bool watchRole = false;  // ...to watch one: video comes in, nothing goes out
	std::string watchedUser;
	std::string watchGaveUp;
	uint64_t watchStartedAt = 0;
	uint64_t nextWatchScan = 0;
	void scanStreams();
	std::atomic<int> notice{0};
	uint64_t shareStartedAt = 0;
	std::string streamKey;
	std::atomic<int> shareSource{3};
	std::atomic<bool> shareWanted{false}; // stream mode: start sending the screen when the connection is ready
	uint64_t daveGroupId() const;
	std::string ringChannelId;
	std::atomic<bool> outgoingRing{false};
	std::atomic<bool> ringPlaying{false};
	int outgoingRingTimer = 0;
	bool sawRinging = false;
	void stopOutgoingRing();

  public:
	// Go Live (the main connection): opens a stream of the top screen next to the call, and closes it.
	bool startScreenShare(int source = 3); // 3: top screen, 4: bottom screen, 5: both
	void stopScreenShare();
	bool isScreenSharing() const;
	// Watching the Go Live stream of someone in the call (it opens by itself when Settings > Voice > Watch Streams is on).
	bool startWatching(const std::string &userId);
	void stopWatching();
	bool isWatching() const;
	// Something the user should be told (1: the stream could not be opened or was lost); the screen manager shows it.
	int takeNotice() { return notice.exchange(0); }
	// The stream connection (see getStreamInstance), called by the gateway.
	void onStreamCreate(const std::string &key, const std::string &rtcServerId);
	void onStreamServer(const std::string &key, const std::string &streamEndpoint, const std::string &streamToken);
	void setMuted(bool m);
	bool isMuted() const { return capture.isMuted(); }
	// EXPERIMENTAL: what you send as video in the call: 0 nothing, 1 the inner camera, 2 the outer camera, 3 the top screen. False when
	// it cannot start (not in a call, the Video setting is off, or the camera is busy).
	bool setVideoSource(int source);
	int videoSource() const;
	bool isCameraOn() const { return cameraSending.load(); }
	// RTCP messages that came from the call while we were sending video (someone receives it when they come).
	uint32_t rtcpWhileSending() const { return rtcpSeenWhileVideo.load(); }
	// Video frames sent and frames DAVE could not encrypt (for the call view).
	uint32_t videoFramesSentCount() const { return videoFramesSent; }
	int videoVariantNow() const { return videoVariant.load(); }
	// For the diagnostics report: pictures of others that were joined and decrypted, and those that were not; state of the connection.
	uint32_t videoFramesOkCount() const { return videoFramesOk; }
	uint32_t videoFramesBadCount() const { return videoFramesBad; }
	int stateNumber() const { return (int)state.load(); }
	uint32_t videoEncryptFailures() const { return videoEncryptFails.load(); }
	void setDeafened(bool d);
	bool isDeafened() const { return audio.isDeafened(); }
	bool isSpeaking(const std::string &userId) const;
	// Seconds since the call was established, 0 when there is no call.
	int connectedSeconds() const;
	// Counters for the call statistics.
	uint32_t statPacketsDecoded() const { return packetsDecoded; }
	uint32_t statPacketsSent() const { return packetsSent; }
	uint32_t statDecryptFailures() const { return decryptFailures + daveDecryptFailures; }
	uint32_t statMicFramesDropped() const { return micFramesDropped; }
	bool isServerMuted() const { return serverMuted; }
	bool isServerDeafened() const { return serverDeafened; }

	// Push to talk: while enabled the microphone only transmits while the button is held (called every frame).
	void setPushToTalk(bool enabled, bool buttonDown) {
		pttEnabled = enabled;
		pttDown = buttonDown;
	}
	bool isPushToTalkActive() const { return pttEnabled; }

	void setUserVolume(const std::string &userId, int volumePercent);
	int getUserVolume(const std::string &userId) const;
	void setMicVolume(int volumePercent);
	int getMicVolume() const;

	std::function<void(VoiceState)> stateCallback;
};

} // namespace Discord

#endif // VOICE_CLIENT_H
