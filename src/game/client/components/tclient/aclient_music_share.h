#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_MUSIC_SHARE_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_MUSIC_SHARE_H

#include <base/types.h>
#include <engine/console.h>
#include <engine/shared/protocol.h>
#include <game/client/component.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>

struct OpusEncoder;
struct OpusDecoder;

class CAClientMusicShare : public CComponent
{
public:
	static constexpr int SAMPLE_RATE = 48000;
	static constexpr int CHANNELS = 2;
	static constexpr int FRAME_SAMPLES = 960; // 20 ms at 48 kHz
	static constexpr int FRAME_INT16 = FRAME_SAMPLES * CHANNELS; // 1920 interleaved int16
	static constexpr int MAX_OPUS_BYTES = 480;
	static constexpr int HEADER_LEN = 21; // "ACV1" (4) + type (1) + room_hash (8) + sender_id (4) + seq (4)
	static constexpr int PLAY_RING_FRAMES = 16; // 320 ms max stereo jitter buffer
	static constexpr int PLAY_RING_SAMPLES = FRAME_INT16 * PLAY_RING_FRAMES;
	static constexpr uint32_t CAPTURE_RING_CAPACITY = 65536u; // Power of 2 (682 ms stereo PCM16)
	static constexpr uint32_t CAPTURE_RING_MASK = CAPTURE_RING_CAPACITY - 1u;
	static constexpr int VIS_BARS = 12;

	enum EState
	{
		STATE_IDLE = 0,
		STATE_WAITING_ACCEPT = 1,
		STATE_HOSTING = 2,
		STATE_LISTENING = 3,
	};

	enum ECaptureStatus
	{
		CAPTURE_NONE = 0,
		CAPTURE_SEARCHING = 1,
		CAPTURE_ACTIVE = 2,
		CAPTURE_UNSUPPORTED = 3,
	};

	struct SRemoteTrackInfo
	{
		char m_aTitle[96] = {};
		char m_aArtist[96] = {};
		bool m_Playing = true;
		int64_t m_PositionMs = 0;
		int64_t m_DurationMs = 0;
		int64_t m_UpdatedAt = 0;
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnConsoleInit() override;
	void OnUpdate() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnShutdown() override;

	EState GetState() const { return m_State; }
	bool IsIdle() const { return m_State == STATE_IDLE; }
	bool IsWaitingAccept() const { return m_State == STATE_WAITING_ACCEPT; }
	bool IsHosting() const { return m_State == STATE_HOSTING; }
	bool IsListening() const { return m_State == STATE_LISTENING; }
	ECaptureStatus GetCaptureStatus() const { return (ECaptureStatus)m_CaptureStatus.load(std::memory_order_relaxed); }

	const char *GetPeerName() const { return m_aPeerName; }
	const char *GetRoomCode() const { return m_aRoomCode; }
	bool IsMuted() const { return m_Muted; }
	void SetMuted(bool Muted) { m_Muted = Muted; }
	void ToggleMute() { m_Muted = !m_Muted; }

	// 1-to-1 invite & session control
	void InviteFriend(const char *pTargetName);
	bool OnMusicWhisperInvite(int ClientId, int Team, const char *pLine);
	bool HasPendingInvite() const;
	const char *GetPendingInviteFrom() const { return m_aPendingInviteFrom; }
	const char *GetPendingInviteTrack() const { return m_aPendingInviteTrack; }
	void AcceptPendingInvite();
	void DeclinePendingInvite();
	void StopSession(bool NotifyPeer = true);

	// Remote track metadata & O(1) visualizer bars for Music Player HUD when listening
	const SRemoteTrackInfo &GetRemoteTrack() const { return m_RemoteTrack; }
	const std::array<float, VIS_BARS> &GetVisualizerBars() const { return m_aVisBars; }

private:
	EState m_State = STATE_IDLE;
	char m_aRoomCode[32] = {};
	char m_aPeerName[MAX_NAME_LENGTH] = {};
	uint64_t m_RoomHash = 0;
	int64_t m_StateStartedAt = 0;
	int64_t m_LastPeerActivityAt = 0;
	bool m_PeerSeenOnRelay = false;
	bool m_Muted = false;

	// Pending incoming invite
	char m_aPendingInviteFrom[MAX_NAME_LENGTH] = {};
	char m_aPendingInviteRoom[32] = {};
	char m_aPendingInviteTrack[160] = {};
	int64_t m_PendingInviteExpiresAt = 0;

	// Network (independent UDP socket on same Oracle relay)
	NETSOCKET m_Socket = nullptr;
	NETADDR m_RelayAddr = {};
	bool m_RelayAddrValid = false;
	char m_aLastRelayAddrStr[128] = {};
	int64_t m_NextResolveRetryTime = 0;
	uint32_t m_SessionToken = 0;
	uint32_t m_Seq = 0;
	int64_t m_LastKeepaliveTime = 0;
	int64_t m_LastMetaSendTime = 0;
	int64_t m_LastMetaPollTime = 0;
	int64_t m_LastAudibleTime = 0;
	char m_aLastSentTitle[96] = {};
	char m_aLastSentArtist[96] = {};
	bool m_LastSentPlaying = false;

	// Host capture (lock-free SPSC ring buffer filled by WASAPI Process Loopback thread)
	std::thread m_CaptureThread;
	std::atomic<bool> m_CaptureStop{false};
	std::atomic<int> m_CaptureStatus{CAPTURE_NONE};
	std::atomic<uint32_t> m_CapWriteIdx{0};
	std::atomic<uint32_t> m_CapReadIdx{0};
	std::array<int16_t, CAPTURE_RING_CAPACITY> m_aCaptureRing{};
	OpusEncoder *m_pEncoder = nullptr;

	// Listener playback (O(1) stereo jitter ring buffer + SDL2 playback)
	uint32_t m_PlaybackDevice = 0;
	bool m_SdlAudioInit = false;
	OpusDecoder *m_pDecoder = nullptr;
	std::array<int16_t, PLAY_RING_SAMPLES> m_aPlayRing{};
	uint32_t m_PlayReadPos = 0;
	uint32_t m_PlayWritePos = 0;
	uint32_t m_PlayBufferedSamples = 0;
	bool m_PlayPrimed = false;
	uint32_t m_LastRemoteSenderId = 0;
	uint32_t m_LastRemoteSeq = 0;
	bool m_HasRemoteSeq = false;
	int64_t m_NextMixTime = 0;

	SRemoteTrackInfo m_RemoteTrack;
	std::array<float, VIS_BARS> m_aVisBars{};
	float m_VisLowLp = 0.0f;
	float m_VisMidLp = 0.0f;

	void UpdateRelayAddress();
	bool EnsureSocket();
	void CloseSocket();
	void StartHostStreaming();
	void StopHostStreaming();
	void StartListenerPlayback();
	void StopListenerPlayback();
	void SendControlPacket(uint8_t Type, uint64_t RoomHash);
	void SendMetadataPacket(bool Force);
	void SendMusicWhisper(const char *pTargetName, const char *pPayload);
	void PumpHostAudio(int64_t Now);
	void PollIncomingPackets(int64_t Now);
	void PumpListenerAudio(int64_t Now);
	void PushCaptureSamples(const int16_t *pSamples, uint32_t Count);
	void UpdateListenerVisualizer(const int16_t *pStereoPcm, int FrameCount);
	void CaptureThreadMain();
	const char *GetLocalPlayerName() const;

	static void WriteHeader(uint8_t *pDst, uint8_t Type, uint64_t RoomHash, uint32_t SenderId, uint32_t Seq);
	static uint64_t ComputeMusicRoomHash(const char *pRoomCode);

	static void ConShare(IConsole::IResult *pResult, void *pUserData);
	static void ConStop(IConsole::IResult *pResult, void *pUserData);
	static void ConAccept(IConsole::IResult *pResult, void *pUserData);
	static void ConDecline(IConsole::IResult *pResult, void *pUserData);
	static void ConLeave(IConsole::IResult *pResult, void *pUserData);
	static void ConMute(IConsole::IResult *pResult, void *pUserData);
};

#endif // GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_MUSIC_SHARE_H
