#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_VOICE_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_VOICE_H

#include <base/types.h>
#include <engine/console.h>
#include <engine/shared/protocol.h>
#include <game/client/component.h>

#include <cstdint>
#include <string>
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

	void SetClientMuted(int ClientId, bool Muted)
	{
		if(ClientId >= 0 && ClientId < MAX_CLIENTS)
			m_aMuted[ClientId] = Muted;
	}

	void ToggleClientMute(int ClientId)
	{
		if(ClientId >= 0 && ClientId < MAX_CLIENTS)
			m_aMuted[ClientId] = !m_aMuted[ClientId];
	}

	int GetClientVolume(int ClientId) const
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			return 100;
		return m_aPeerVolume[ClientId];
	}

	void SetClientVolume(int ClientId, int Volume)
	{
		if(ClientId >= 0 && ClientId < MAX_CLIENTS)
			m_aPeerVolume[ClientId] = Volume < 0 ? 0 : (Volume > 200 ? 200 : Volume);
	}

	int *GetClientVolumePtr(int ClientId)
	{
		if(ClientId < 0 || ClientId >= MAX_CLIENTS)
			return nullptr;
		return &m_aPeerVolume[ClientId];
	}

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

	OpusEncoder *m_pEncoder = nullptr;

	SVoicePeer m_aPeers[MAX_CLIENTS] = {};
	SVoicePeer m_LoopbackPeer = {};
	bool m_LoopbackActive = false;
	bool m_aMuted[MAX_CLIENTS] = {};
	int m_aPeerVolume[MAX_CLIENTS] = {};

	// O(1) bitmasks covering all MAX_CLIENTS (128 = 2 * 64)
	uint64_t m_aActiveAudioMask[2] = {0, 0};
	uint64_t m_aSpeakingHudMask[2] = {0, 0};

	uint64_t m_CurrentRoomHash = 0;
	NETADDR m_LastServerAddr = {};
	int m_LastTeam = -1;
	int m_LastLocalId = -1;

	uint32_t m_SessionToken = 0;
	uint32_t m_Seq = 0;
	bool m_PttDown = false;
	bool m_IsTransmitting = false;
	int64_t m_TransmitHangoverUntil = 0;
	int64_t m_LastKeepaliveTime = 0;
	int64_t m_LastVoiceSendTime = 0;
	int64_t m_NextMixTime = 0;
	float m_LocalMicLevel = 0.0f;

	void EnsureStarted();
	void ReopenCaptureDevice();
	void StopSubsystem();
	void UpdateRelayAddress();
	void UpdateRoomState();
	void SendControlPacket(uint8_t Type, uint64_t RoomHash);
	void PumpOutgoingAudio();
	void PollIncomingPackets();
	void DecodeIntoPeer(SVoicePeer &Peer, uint32_t SenderId, uint32_t Seq, const uint8_t *pOpusData, int OpusLen, int64_t Now);
	void MixAndQueuePlayback(int64_t Now);

	static void WriteHeader(uint8_t *pDst, uint8_t Type, uint64_t RoomHash, uint32_t SenderId, uint32_t Seq);
	static uint64_t ComputeRoomHash(const char *pServerAddr, int TeamId);
	static void ConPtt(IConsole::IResult *pResult, void *pUserData);
	static void ConMute(IConsole::IResult *pResult, void *pUserData);
	static void ConToggle(IConsole::IResult *pResult, void *pUserData);
};

#endif // GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_VOICE_H
