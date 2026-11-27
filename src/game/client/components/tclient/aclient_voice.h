#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_VOICE_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_VOICE_H

#include <base/types.h>
#include <engine/console.h>
#include <engine/shared/protocol.h>
#include <game/client/component.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct OpusEncoder;
struct OpusDecoder;

class CAClientVoice : public CComponent
{
public:
	static constexpr int SAMPLE_RATE = 48000;
	static constexpr int CHANNELS = 1;
	static constexpr int FRAME_SAMPLES = 960; // 20 ms at 48 kHz
	static constexpr int PEER_RING_FRAMES = 16; // 320 ms max jitter buffer per peer
	static constexpr int PEER_RING_SAMPLES = FRAME_SAMPLES * PEER_RING_FRAMES;
	static constexpr int MAX_OPUS_BYTES = 512;
	static constexpr int HEADER_LEN = 21; // "ACV1" (4) + type (1) + room_hash (8) + sender_id (4) + seq (4)

	struct SRoomPeerInfo
	{
		uint32_t m_SenderId = 0;
		char m_aName[MAX_NAME_LENGTH] = {};
		int m_VirtualSlot = -1;
	};

	struct SPeerSavedPref
	{
		int m_Volume = 100;
		bool m_Muted = false;
		char m_aOriginalName[MAX_NAME_LENGTH] = {};
	};

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnConsoleInit() override;
	void OnUpdate() override;
	void OnRender() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnShutdown() override;

	bool IsTransmitting() const { return m_IsTransmitting; }
	float GetLocalMicLevel() const { return m_LocalMicLevel; }
	bool IsRoomActive() const { return m_CurrentRoomHash != 0; }
	int GetCurrentTeam() const { return m_LastTeam; }
	bool HasValidRelayAddr() const { return m_RelayAddrValid; }

	void RefreshMicDevices();
	const std::vector<std::string> &GetMicDevices();
	void SelectMicDevice(int Index); // -1 = system default

	void RefreshOutputDevices();
	const std::vector<std::string> &GetOutputDevices();
	void SelectOutputDevice(int Index); // -1 = system default

	bool IsClientSpeaking(int ClientId) const
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			return false;
		return (m_aSpeakingHudMask[ClientId >> 6] & (1ULL << (ClientId & 63))) != 0;
	}

	bool IsClientMuted(int ClientId) const
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			return false;
		return m_aMuted[ClientId];
	}

	void SetClientMuted(int ClientId, bool Muted);
	void ToggleClientMute(int ClientId);

	int GetClientVolume(int ClientId) const
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			return 100;
		return m_aPeerVolume[ClientId];
	}

	void SetClientVolume(int ClientId, int Volume);
	void NotifyClientVolumeChanged(int ClientId);

	int *GetClientVolumePtr(int ClientId)
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			return nullptr;
		return &m_aPeerVolume[ClientId];
	}

	// Persistent per-name volume / mute preferences
	void SyncPlayerVolumesByName();
	void SavePlayerPrefsToConfig();
	void LoadPlayerPrefsFromConfig();
	int GetPlayerVolumeByName(const char *pName) const;
	void SetPlayerVolumeByName(const char *pName, int Volume);
	bool IsPlayerMutedByName(const char *pName) const;
	void SetPlayerMutedByName(const char *pName, bool Muted);

	// Global relay moderation with moderator key
	bool IsModKeyVerified() const { return m_ModKeyVerified; }
	bool VerifyAndSaveModKey(const char *pKey);
	void ClearModKey();
	bool SendGlobalModMute(int TargetClientId, const char *pTargetName, int DurationSeconds);
	const char *GetModStatusMsg() const { return m_aModStatusMsg; }

	// Private friend rooms & invitations
	bool IsInCustomRoom() const { return m_aCustomRoomCode[0] != '\0'; }
	const char *GetCustomRoomCode() const { return m_aCustomRoomCode; }
	const char *GetCustomRoomPassword() const { return m_aCustomRoomPassword; }
	bool HasCustomRoomPassword() const { return m_aCustomRoomPassword[0] != '\0'; }
	void CreateCustomRoom(const char *pCustomCode = nullptr, const char *pPassword = nullptr);
	void JoinCustomRoom(const char *pRoomCode, const char *pPassword = nullptr);
	void LeaveCustomRoom();
	void SendRoomInvite(const char *pTargetName);
	bool OnVoiceWhisperInvite(int ClientId, int Team, const char *pLine);
	bool HasPendingInvite() const;
	const char *GetPendingInviteFrom() const { return m_aPendingInviteFrom; }
	const char *GetPendingInviteRoom() const { return m_aPendingInviteRoom; }
	const char *GetPendingInvitePassword() const { return m_aPendingInvitePassword; }
	bool HasPendingInvitePassword() const { return m_aPendingInvitePassword[0] != '\0'; }
	void AcceptPendingInvite();
	void DeclinePendingInvite();
	const std::vector<SRoomPeerInfo> &GetRoomPeers() const { return m_vRoomPeers; }
	std::vector<SRoomPeerInfo> GetAvailableVoicePeers() const;

private:
	struct SVoicePeer
	{
		OpusDecoder *m_pDecoder = nullptr;
		int16_t m_aPcmRing[PEER_RING_SAMPLES] = {};
		uint32_t m_ReadPos = 0;
		uint32_t m_WritePos = 0;
		uint32_t m_BufferedSamples = 0;
		uint32_t m_LastSenderId = 0;
		uint32_t m_LastSeq = 0;
		int64_t m_LastPacketTime = 0;
		int64_t m_SpeakingUntil = 0;
		bool m_HasSeq = false;

		void ClearRing()
		{
			m_ReadPos = 0;
			m_WritePos = 0;
			m_BufferedSamples = 0;
		}
	};

	NETSOCKET m_Socket = nullptr;
	NETADDR m_RelayAddr = {};
	bool m_RelayAddrValid = false;
	char m_aLastRelayAddrStr[128] = {};
	int64_t m_NextResolveRetryTime = 0;

	uint32_t m_CaptureDevice = 0;
	uint32_t m_PlaybackDevice = 0;
	bool m_AudioActive = false;
	bool m_SdlAudioInit = false;
	char m_aOpenedMicDevice[128] = {};
	std::vector<std::string> m_vMicDevices;
	bool m_MicDevicesScanned = false;
	char m_aOpenedOutputDevice[128] = {};
	std::vector<std::string> m_vOutputDevices;
	bool m_OutputDevicesScanned = false;

	OpusEncoder *m_pEncoder = nullptr;

	SVoicePeer m_aPeers[MAX_CLIENTS] = {};
	SVoicePeer m_LoopbackPeer = {};
	bool m_LoopbackActive = false;
	bool m_aMuted[MAX_CLIENTS] = {};
	int m_aPeerVolume[MAX_CLIENTS] = {};
	struct SVoiceStringHash
	{
		using is_transparent = void;
		size_t operator()(std::string_view Sv) const noexcept { return std::hash<std::string_view>{}(Sv); }
		size_t operator()(const std::string &S) const noexcept { return std::hash<std::string_view>{}(S); }
		size_t operator()(const char *pS) const noexcept { return std::hash<std::string_view>{}(pS ? std::string_view(pS) : std::string_view()); }
	};

	char m_aaLastSlotName[MAX_CLIENTS][MAX_NAME_LENGTH] = {};
	char m_aaRoomPeerName[MAX_CLIENTS][MAX_NAME_LENGTH] = {};
	std::unordered_map<std::string, SPeerSavedPref, SVoiceStringHash, std::equal_to<>> m_mSavedPeerPrefs;
	std::vector<SRoomPeerInfo> m_vRoomPeers;
	std::unordered_map<uint32_t, int> m_mSenderIdToSlot;
	bool m_RelaySupportsRoomPeers = false;
	char m_aLastCheckedModKey[128] = {};
	int64_t m_LastVolumeSyncTime = 0;

	// O(1) bitmasks covering all MAX_CLIENTS (128 = 2 * 64)
	uint64_t m_aActiveAudioMask[2] = {0, 0};
	uint64_t m_aSpeakingHudMask[2] = {0, 0};

	uint64_t m_CurrentRoomHash = 0;
	NETADDR m_LastServerAddr = {};
	int m_LastTeam = -1;
	int m_LastLocalId = -1;
	char m_aLastLocalPlayerName[MAX_NAME_LENGTH] = {};
	char m_aCustomRoomCode[32] = {};
	char m_aCustomRoomPassword[32] = {};
	bool m_WasInCustomRoom = false;

	// Pending invite state
	char m_aPendingInviteFrom[MAX_NAME_LENGTH] = {};
	char m_aPendingInviteRoom[32] = {};
	char m_aPendingInvitePassword[32] = {};
	int64_t m_PendingInviteExpiresAt = 0;

	// Global relay moderation state
	bool m_ModKeyVerified = false;
	char m_aModStatusMsg[160] = {};
	int64_t m_ModAckWaitUntil = 0;

	uint32_t m_SessionToken = 0;
	uint32_t m_Seq = 0;
	bool m_PttDown = false;
	bool m_IsTransmitting = false;
	int64_t m_TransmitHangoverUntil = 0;
	int64_t m_LastKeepaliveTime = 0;
	int64_t m_LastPresenceKeepaliveTime = 0;
	int64_t m_LastNameBeaconTime = 0;
	int64_t m_LastVoiceSendTime = 0;
	int64_t m_NextMixTime = 0;
	float m_LocalMicLevel = 0.0f;

	void EnsureSocketOnly();
	void EnsureStarted();
	void ReopenCaptureDevice();
	void ReopenPlaybackDevice();
	void StopSubsystem();
	void UpdateRelayAddress();
	void UpdateRoomState();
	void SendControlPacket(uint8_t Type, uint64_t RoomHash);
	void SendNameBeacon(uint64_t RoomHash);
	void PumpOutgoingAudio();
	void PollIncomingPackets();
	void DecodeIntoPeer(SVoicePeer &Peer, uint32_t SenderId, uint32_t Seq, const uint8_t *pOpusData, int OpusLen, int64_t Now);
	void MixAndQueuePlayback(int64_t Now);
	const char *GetLocalVoiceName() const;
	const char *GetSlotEffectiveName(int ClientId) const;
	int ResolveOrAllocRoomSlot(uint32_t SenderId, const char *pName);
	uint32_t ResolveSenderIdForTarget(int TargetClientId, const char *pTargetName) const;

	static void WriteHeader(uint8_t *pDst, uint8_t Type, uint64_t RoomHash, uint32_t SenderId, uint32_t Seq);
	static uint64_t ComputeRoomHash(const char *pServerAddr, int TeamId);
	static uint64_t ComputeCustomRoomHash(const char *pRoomCode, const char *pPassword);
	static void ConPtt(IConsole::IResult *pResult, void *pUserData);
	static void ConMute(IConsole::IResult *pResult, void *pUserData);
	static void ConToggle(IConsole::IResult *pResult, void *pUserData);
	static void ConRoomCreate(IConsole::IResult *pResult, void *pUserData);
	static void ConRoomJoin(IConsole::IResult *pResult, void *pUserData);
	static void ConRoomLeave(IConsole::IResult *pResult, void *pUserData);
	static void ConInvite(IConsole::IResult *pResult, void *pUserData);
	static void ConAcceptInvite(IConsole::IResult *pResult, void *pUserData);
	static void ConDeclineInvite(IConsole::IResult *pResult, void *pUserData);
};

#endif // GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_VOICE_H
