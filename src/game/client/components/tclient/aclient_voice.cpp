#include "aclient_voice.h"

#include <base/hash.h>
#include <base/mem.h>
#include <base/net.h>
#include <base/str.h>
#include <base/time.h>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include <engine/client.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

#include <SDL.h>
#include <SDL_audio.h>

extern "C" {
#include <opus.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
	constexpr uint8_t PKT_KEEPALIVE = 1;
	constexpr uint8_t PKT_VOICE = 2;
	constexpr uint8_t PKT_LEAVE = 3;
	constexpr int VOICE_DEFAULT_PORT = 9987;

	inline void WriteU32LE(uint8_t *pDst, uint32_t v)
	{
		pDst[0] = (uint8_t)(v & 0xFF);
		pDst[1] = (uint8_t)((v >> 8) & 0xFF);
		pDst[2] = (uint8_t)((v >> 16) & 0xFF);
		pDst[3] = (uint8_t)((v >> 24) & 0xFF);
	}

	inline void WriteU64LE(uint8_t *pDst, uint64_t v)
	{
		for(int i = 0; i < 8; ++i)
			pDst[i] = (uint8_t)((v >> (i * 8)) & 0xFF);
	}

	inline uint32_t ReadU32LE(const uint8_t *pSrc)
	{
		return (uint32_t)pSrc[0] | ((uint32_t)pSrc[1] << 8) | ((uint32_t)pSrc[2] << 16) | ((uint32_t)pSrc[3] << 24);
	}

	inline uint64_t ReadU64LE(const uint8_t *pSrc)
	{
		uint64_t v = 0;
		for(int i = 0; i < 8; ++i)
			v |= (uint64_t)pSrc[i] << (i * 8);
		return v;
	}

	inline void SetMaskBit(uint64_t aMask[2], int Idx)
	{
		aMask[Idx >> 6] |= (1ULL << (Idx & 63));
	}

	inline void ClearMaskBit(uint64_t aMask[2], int Idx)
	{
		aMask[Idx >> 6] &= ~(1ULL << (Idx & 63));
	}

	inline int TrailingZeros64(uint64_t x)
	{
#if defined(_MSC_VER)
		unsigned long Idx = 0;
		_BitScanForward64(&Idx, x);
		return (int)Idx;
#else
		return __builtin_ctzll(x);
#endif
	}
}

void CAClientVoice::OnInit()
{
	uint64_t Seed = (uint64_t)time_get() ^ ((uint64_t)(uintptr_t)this * 0x9E3779B97F4A7C15ULL);
	Seed ^= (Seed >> 30);
	m_SessionToken = (uint32_t)((Seed & 0x00FFFFFFU) | 1U); // 24-bit non-zero session token
	std::fill(std::begin(m_aPeerVolume), std::end(m_aPeerVolume), 100);
}

void CAClientVoice::OnConsoleInit()
{
	Console()->Register("+ac_voice_ptt", "", CFGFLAG_CLIENT, ConPtt, this, "Hold to talk in Team Voice Chat");
	Console()->Register("ac_voice_mute", "i[client_id]", CFGFLAG_CLIENT, ConMute, this, "Toggle mute for a player in Voice Chat");
	Console()->Register("ac_voice_toggle", "", CFGFLAG_CLIENT, ConToggle, this, "Toggle Team Voice Chat on or off");
}

void CAClientVoice::ConToggle(IConsole::IResult *pResult, void *pUserData)
{
	CAClientVoice *pSelf = static_cast<CAClientVoice *>(pUserData);
	g_Config.m_AcVoiceEnabled ^= 1;
	if(pSelf->GameClient())
	{
		pSelf->GameClient()->Echo(g_Config.m_AcVoiceEnabled ? "[AClient] Voice Chat: Activado" : "[AClient] Voice Chat: Desactivado");
	}
}

void CAClientVoice::ConPtt(IConsole::IResult *pResult, void *pUserData)
{
	CAClientVoice *pSelf = static_cast<CAClientVoice *>(pUserData);
	pSelf->m_PttDown = pResult->GetInteger(0) != 0;
}

void CAClientVoice::ConMute(IConsole::IResult *pResult, void *pUserData)
{
	CAClientVoice *pSelf = static_cast<CAClientVoice *>(pUserData);
	const int ClientId = pResult->GetInteger(0);
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ac_voice", "Client ID invalido (0-127)");
		return;
	}
	pSelf->ToggleClientMute(ClientId);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s %s", pSelf->GameClient()->m_aClients[ClientId].m_aName[0] ? pSelf->GameClient()->m_aClients[ClientId].m_aName : "Jugador", pSelf->IsClientMuted(ClientId) ? "muteado" : "desmuteado");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ac_voice", aBuf);
}

void CAClientVoice::WriteHeader(uint8_t *pDst, uint8_t Type, uint64_t RoomHash, uint32_t SenderId, uint32_t Seq)
{
	pDst[0] = 'A';
	pDst[1] = 'C';
	pDst[2] = 'V';
	pDst[3] = '1';
	pDst[4] = Type;
	WriteU64LE(pDst + 5, RoomHash);
	WriteU32LE(pDst + 13, SenderId);
	WriteU32LE(pDst + 17, Seq);
}

uint64_t CAClientVoice::ComputeRoomHash(const char *pServerAddr, int TeamId)
{
	// FNV-1a 64-bit over "<server_ip:port>|<team_id>"
	uint64_t Hash = 14695981039346656037ULL;
	for(const unsigned char *p = (const unsigned char *)pServerAddr; *p != '\0'; ++p)
	{
		Hash ^= *p;
		Hash *= 1099511628211ULL;
	}
	char aTeamBuf[16];
	str_format(aTeamBuf, sizeof(aTeamBuf), "|%d", TeamId);
	for(const unsigned char *p = (const unsigned char *)aTeamBuf; *p != '\0'; ++p)
	{
		Hash ^= *p;
		Hash *= 1099511628211ULL;
	}
	if(Hash == 0)
		Hash = 1;
	return Hash;
}

void CAClientVoice::RefreshMicDevices()
{
	m_vMicDevices.clear();
	if(SDL_WasInit(SDL_INIT_AUDIO) == 0)
	{
		if(SDL_InitSubSystem(SDL_INIT_AUDIO) == 0)
			m_SdlAudioInit = true;
	}
	if(SDL_WasInit(SDL_INIT_AUDIO) != 0)
	{
		const int Num = SDL_GetNumAudioDevices(1);
		for(int i = 0; i < Num; ++i)
		{
			const char *pName = SDL_GetAudioDeviceName(i, 1);
			if(pName && pName[0] != '\0')
				m_vMicDevices.emplace_back(pName);
		}
	}
	m_MicDevicesScanned = true;
}

const std::vector<std::string> &CAClientVoice::GetMicDevices()
{
	if(!m_MicDevicesScanned)
		RefreshMicDevices();
	return m_vMicDevices;
}

void CAClientVoice::SelectMicDevice(int Index)
{
	if(Index < 0 || Index >= (int)m_vMicDevices.size())
		g_Config.m_AcVoiceMicDevice[0] = '\0';
	else
		str_copy(g_Config.m_AcVoiceMicDevice, m_vMicDevices[Index].c_str());

	if(m_AudioActive)
		ReopenCaptureDevice();
}

void CAClientVoice::ReopenCaptureDevice()
{
	if(m_CaptureDevice != 0)
	{
		SDL_CloseAudioDevice((SDL_AudioDeviceID)m_CaptureDevice);
		m_CaptureDevice = 0;
	}

	if(SDL_WasInit(SDL_INIT_AUDIO) == 0)
		return;

	SDL_AudioSpec WantCapture = {};
	WantCapture.freq = SAMPLE_RATE;
	WantCapture.format = AUDIO_S16SYS;
	WantCapture.channels = CHANNELS;
	WantCapture.samples = FRAME_SAMPLES;
	WantCapture.callback = nullptr;

	SDL_AudioSpec HaveCapture = {};
	const char *pWanted = g_Config.m_AcVoiceMicDevice[0] != '\0' ? g_Config.m_AcVoiceMicDevice : nullptr;
	m_CaptureDevice = SDL_OpenAudioDevice(pWanted, 1, &WantCapture, &HaveCapture, 0);
	if(m_CaptureDevice == 0 && pWanted != nullptr)
	{
		// Fallback to system default if saved device is unavailable
		m_CaptureDevice = SDL_OpenAudioDevice(nullptr, 1, &WantCapture, &HaveCapture, 0);
	}
	if(m_CaptureDevice != 0)
	{
		SDL_ClearQueuedAudio(m_CaptureDevice);
		SDL_PauseAudioDevice(m_CaptureDevice, 0);
	}
	str_copy(m_aOpenedMicDevice, g_Config.m_AcVoiceMicDevice);
}

void CAClientVoice::EnsureStarted()
{
	if(!m_Socket)
	{
		NETADDR BindAddr = {};
		BindAddr.type = NETTYPE_IPV4;
		m_Socket = net_udp_create(BindAddr);
	}

	if(!m_pEncoder)
	{
		int Err = OPUS_OK;
		m_pEncoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_VOIP, &Err);
		if(Err == OPUS_OK && m_pEncoder)
		{
			opus_encoder_ctl(m_pEncoder, OPUS_SET_BITRATE(24000));
			opus_encoder_ctl(m_pEncoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
			opus_encoder_ctl(m_pEncoder, OPUS_SET_COMPLEXITY(5));
			opus_encoder_ctl(m_pEncoder, OPUS_SET_DTX(1));
			opus_encoder_ctl(m_pEncoder, OPUS_SET_INBAND_FEC(1));
			opus_encoder_ctl(m_pEncoder, OPUS_SET_PACKET_LOSS_PERC(5));
		}
		else if(m_pEncoder)
		{
			opus_encoder_destroy(m_pEncoder);
			m_pEncoder = nullptr;
		}
	}

	if(!m_AudioActive)
	{
		if(SDL_WasInit(SDL_INIT_AUDIO) == 0)
		{
			if(SDL_InitSubSystem(SDL_INIT_AUDIO) == 0)
				m_SdlAudioInit = true;
		}

		if(SDL_WasInit(SDL_INIT_AUDIO) != 0)
		{
			ReopenCaptureDevice();

			SDL_AudioSpec WantPlayback = {};
			WantPlayback.freq = SAMPLE_RATE;
			WantPlayback.format = AUDIO_S16SYS;
			WantPlayback.channels = CHANNELS;
			WantPlayback.samples = FRAME_SAMPLES;
			WantPlayback.callback = nullptr;

			SDL_AudioSpec HavePlayback = {};
			m_PlaybackDevice = SDL_OpenAudioDevice(nullptr, 0, &WantPlayback, &HavePlayback, 0);
			if(m_PlaybackDevice != 0)
			{
				SDL_ClearQueuedAudio(m_PlaybackDevice);
				SDL_PauseAudioDevice(m_PlaybackDevice, 0);
			}
			m_AudioActive = true;
		}
	}
	else if(str_comp(m_aOpenedMicDevice, g_Config.m_AcVoiceMicDevice) != 0)
	{
		ReopenCaptureDevice();
	}
}

void CAClientVoice::StopSubsystem()
{
	if(m_CurrentRoomHash != 0 && m_Socket && m_RelayAddrValid)
	{
		SendControlPacket(PKT_LEAVE, m_CurrentRoomHash);
	}

	m_CurrentRoomHash = 0;
	m_LastTeam = -1;
	m_LastLocalId = -1;
	mem_zero(&m_LastServerAddr, sizeof(m_LastServerAddr));
	m_IsTransmitting = false;
	m_LocalMicLevel = 0.0f;
	m_aActiveAudioMask[0] = 0;
	m_aActiveAudioMask[1] = 0;
	m_aSpeakingHudMask[0] = 0;
	m_aSpeakingHudMask[1] = 0;
	m_LoopbackActive = false;
	m_LoopbackPeer.ClearRing();

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		m_aPeers[i].ClearRing();
		if(m_aPeers[i].m_pDecoder)
		{
			opus_decoder_destroy(m_aPeers[i].m_pDecoder);
			m_aPeers[i].m_pDecoder = nullptr;
		}
	}
	if(m_LoopbackPeer.m_pDecoder)
	{
		opus_decoder_destroy(m_LoopbackPeer.m_pDecoder);
		m_LoopbackPeer.m_pDecoder = nullptr;
	}

	if(m_CaptureDevice != 0)
	{
		SDL_CloseAudioDevice((SDL_AudioDeviceID)m_CaptureDevice);
		m_CaptureDevice = 0;
	}
	if(m_PlaybackDevice != 0)
	{
		SDL_CloseAudioDevice((SDL_AudioDeviceID)m_PlaybackDevice);
		m_PlaybackDevice = 0;
	}
	m_AudioActive = false;

	if(m_pEncoder)
	{
		opus_encoder_destroy(m_pEncoder);
		m_pEncoder = nullptr;
	}

	if(m_Socket)
	{
		net_udp_close(m_Socket);
		m_Socket = nullptr;
	}
}

void CAClientVoice::UpdateRelayAddress()
{
	if(g_Config.m_AcVoiceRelayAddr[0] == '\0' || str_comp(g_Config.m_AcVoiceRelayAddr, "127.0.0.1:9987") == 0)
	{
		str_copy(g_Config.m_AcVoiceRelayAddr, "137.131.163.81:9987");
	}
	const char *pCfgAddr = g_Config.m_AcVoiceRelayAddr;
	const int64_t Now = time_get();
	if(str_comp(m_aLastRelayAddrStr, pCfgAddr) == 0 && (m_RelayAddrValid || Now < m_NextResolveRetryTime))
		return;

	str_copy(m_aLastRelayAddrStr, pCfgAddr);
	m_RelayAddrValid = false;
	m_NextResolveRetryTime = Now + time_freq() * 10;

	if(pCfgAddr[0] == '\0')
		return;

	char aAddrWithPort[160];
	str_copy(aAddrWithPort, pCfgAddr);
	if(str_find(aAddrWithPort, ":") == nullptr)
	{
		str_format(aAddrWithPort, sizeof(aAddrWithPort), "%s:%d", pCfgAddr, VOICE_DEFAULT_PORT);
	}

	if(net_addr_from_str(&m_RelayAddr, aAddrWithPort) == 0)
	{
		if(m_RelayAddr.port == 0)
			m_RelayAddr.port = VOICE_DEFAULT_PORT;
		m_RelayAddrValid = true;
		return;
	}

	// Fallback: hostname:port
	char aHost[128];
	str_copy(aHost, aAddrWithPort);
	int Port = VOICE_DEFAULT_PORT;
	char *pColon = const_cast<char *>(str_rchr(aHost, ':'));
	if(pColon)
	{
		*pColon = '\0';
		Port = str_toint(pColon + 1);
		if(Port <= 0 || Port > 65535)
			Port = VOICE_DEFAULT_PORT;
	}

	if(net_host_lookup(aHost, &m_RelayAddr, NETTYPE_IPV4) == 0)
	{
		m_RelayAddr.port = Port;
		m_RelayAddrValid = true;
	}
}

void CAClientVoice::SendControlPacket(uint8_t Type, uint64_t RoomHash)
{
	if(!m_Socket || !m_RelayAddrValid || RoomHash == 0)
		return;

	const int ActiveDummy = std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
	const int LocalId = std::clamp(GameClient()->m_aLocalIds[ActiveDummy], 0, MAX_CLIENTS - 1);
	const uint32_t SenderId = (m_SessionToken << 8) | (uint32_t)(LocalId & 0x7F);

	uint8_t aPacket[HEADER_LEN];
	WriteHeader(aPacket, Type, RoomHash, SenderId, m_Seq++);
	net_udp_send(m_Socket, &m_RelayAddr, aPacket, HEADER_LEN);
}

void CAClientVoice::UpdateRoomState()
{
	const int64_t Now = time_get();
	if(Client()->State() != IClient::STATE_ONLINE)
	{
		if(m_CurrentRoomHash != 0)
		{
			SendControlPacket(PKT_LEAVE, m_CurrentRoomHash);
			m_CurrentRoomHash = 0;
			m_LastTeam = -1;
			m_LastLocalId = -1;
			mem_zero(&m_LastServerAddr, sizeof(m_LastServerAddr));
			m_aActiveAudioMask[0] = 0;
			m_aActiveAudioMask[1] = 0;
			m_aSpeakingHudMask[0] = 0;
			m_aSpeakingHudMask[1] = 0;
		}
		return;
	}

	const int ActiveDummy = std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
	const int LocalId = GameClient()->m_aLocalIds[ActiveDummy];
	if(LocalId < 0 || LocalId >= MAX_CLIENTS || !GameClient()->m_aClients[LocalId].m_Active)
		return;

	const int Team = GameClient()->m_Teams.Team(LocalId);
	const NETADDR ServerAddr = Client()->ServerAddress();

	uint64_t TargetRoom = 0;
	if(!g_Config.m_AcVoiceOnlyTeam || Team > 0)
	{
		if(net_addr_comp(&ServerAddr, &m_LastServerAddr) == 0 && Team == m_LastTeam && m_CurrentRoomHash != 0)
		{
			TargetRoom = m_CurrentRoomHash;
		}
		else
		{
			char aServerStr[NETADDR_MAXSTRSIZE];
			net_addr_str(&ServerAddr, aServerStr, sizeof(aServerStr), true);
			TargetRoom = ComputeRoomHash(aServerStr, Team);
		}
	}

	if(TargetRoom != m_CurrentRoomHash)
	{
		if(m_CurrentRoomHash != 0)
			SendControlPacket(PKT_LEAVE, m_CurrentRoomHash);

		m_CurrentRoomHash = TargetRoom;
		m_LastTeam = Team;
		m_LastLocalId = LocalId;
		m_LastServerAddr = ServerAddr;
		m_aActiveAudioMask[0] = 0;
		m_aActiveAudioMask[1] = 0;
		m_aSpeakingHudMask[0] = 0;
		m_aSpeakingHudMask[1] = 0;

		for(int i = 0; i < MAX_CLIENTS; ++i)
			m_aPeers[i].ClearRing();

		if(m_CurrentRoomHash != 0)
		{
			SendControlPacket(PKT_KEEPALIVE, m_CurrentRoomHash);
			m_LastKeepaliveTime = Now;
		}
	}
	else if(m_CurrentRoomHash != 0)
	{
		m_LastLocalId = LocalId;
		// Network optimization: only send 21-byte keepalive every 2.5s if no voice packet kept relay alive
		const int64_t KeepaliveInterval = (time_freq() * 5) / 2;
		const int64_t SilenceThreshold = time_freq() * 2;
		if((Now - m_LastKeepaliveTime) >= KeepaliveInterval && (Now - m_LastVoiceSendTime) >= SilenceThreshold)
		{
			SendControlPacket(PKT_KEEPALIVE, m_CurrentRoomHash);
			m_LastKeepaliveTime = Now;
		}
	}
}

void CAClientVoice::PumpOutgoingAudio()
{
	if(m_CaptureDevice == 0 || !m_pEncoder)
	{
		m_IsTransmitting = false;
		m_LocalMicLevel = 0.0f;
		return;
	}

	const uint32_t FrameBytes = (uint32_t)(FRAME_SAMPLES * sizeof(int16_t));
	uint32_t Queued = SDL_GetQueuedAudioSize((SDL_AudioDeviceID)m_CaptureDevice);

	// Prevent runaway capture backlog when tabbed/stuttered
	if(Queued > FrameBytes * 8)
	{
		SDL_ClearQueuedAudio((SDL_AudioDeviceID)m_CaptureDevice);
		Queued = 0;
	}

	if(Queued < FrameBytes)
		return;

	const int64_t Now = time_get();
	const int64_t Freq = time_freq();
	const float MicGain = std::clamp(g_Config.m_AcVoiceMicGain / 100.0f, 0.1f, 4.0f);
	const float VadThreshold = std::clamp(g_Config.m_AcVoiceVadThreshold / 100.0f, 0.01f, 1.0f) * 0.35f;

	int16_t aPcm[FRAME_SAMPLES];
	uint8_t aPacket[HEADER_LEN + MAX_OPUS_BYTES];
	int FramesProcessed = 0;

	while(Queued >= FrameBytes && FramesProcessed < 4)
	{
		const uint32_t Got = SDL_DequeueAudio((SDL_AudioDeviceID)m_CaptureDevice, aPcm, FrameBytes);
		if(Got < FrameBytes)
			break;
		Queued -= Got;
		++FramesProcessed;

		// Compute peak/RMS and apply gain
		double SumSq = 0.0;
		for(int i = 0; i < FRAME_SAMPLES; ++i)
		{
			int Scaled = (int)std::lround((float)aPcm[i] * MicGain);
			Scaled = std::clamp(Scaled, -32768, 32767);
			aPcm[i] = (int16_t)Scaled;
			const float Norm = (float)Scaled / 32768.0f;
			SumSq += (double)(Norm * Norm);
		}
		const float Rms = std::sqrt((float)(SumSq / (double)FRAME_SAMPLES));
		const float DisplayLevel = std::clamp(Rms * 3.5f, 0.0f, 1.0f);
		m_LocalMicLevel = DisplayLevel > m_LocalMicLevel ? DisplayLevel : (m_LocalMicLevel * 0.80f + DisplayLevel * 0.20f);

		bool ShouldSend = false;
		if(g_Config.m_AcVoiceMode == 0)
		{
			// Push-to-Talk with 120 ms tail hangover to avoid clipped words
			if(m_PttDown)
				m_TransmitHangoverUntil = Now + (Freq * 12) / 100;
			ShouldSend = m_PttDown || (Now <= m_TransmitHangoverUntil);
		}
		else
		{
			// Voice Activity Detection (VAD) with 350 ms hangover
			if(DisplayLevel >= VadThreshold)
				m_TransmitHangoverUntil = Now + (Freq * 35) / 100;
			ShouldSend = (Now <= m_TransmitHangoverUntil);
		}

		m_IsTransmitting = ShouldSend;
		if(!ShouldSend)
			continue;

		const bool CanSendNet = (m_CurrentRoomHash != 0 && m_Socket && m_RelayAddrValid);
		const bool Loopback = (g_Config.m_AcVoiceLoopback != 0);
		if(!CanSendNet && !Loopback)
			continue;

		const int EncodedBytes = opus_encode(m_pEncoder, aPcm, FRAME_SAMPLES, aPacket + HEADER_LEN, MAX_OPUS_BYTES);
		if(EncodedBytes <= 1) // 1-byte Opus packet is DTX comfort noise/silence frame: skip network send
			continue;

		const int ActiveDummy = std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
		const int LocalId = std::clamp(GameClient()->m_aLocalIds[ActiveDummy], 0, MAX_CLIENTS - 1);
		const uint32_t SenderId = (m_SessionToken << 8) | (uint32_t)(LocalId & 0x7F);
		const uint32_t Seq = m_Seq++;

		if(CanSendNet)
		{
			WriteHeader(aPacket, PKT_VOICE, m_CurrentRoomHash, SenderId, Seq);
			net_udp_send(m_Socket, &m_RelayAddr, aPacket, HEADER_LEN + EncodedBytes);
			m_LastVoiceSendTime = Now;
		}

		if(Loopback)
		{
			DecodeIntoPeer(m_LoopbackPeer, SenderId, Seq, aPacket + HEADER_LEN, EncodedBytes, Now);
			m_LoopbackActive = true;
		}
	}
}

void CAClientVoice::DecodeIntoPeer(SVoicePeer &Peer, uint32_t SenderId, uint32_t Seq, const uint8_t *pOpusData, int OpusLen, int64_t Now)
{
	if(Peer.m_LastSenderId != SenderId)
	{
		if(Peer.m_pDecoder)
			opus_decoder_ctl(Peer.m_pDecoder, OPUS_RESET_STATE);
		Peer.ClearRing();
		Peer.m_LastSenderId = SenderId;
		Peer.m_HasSeq = false;
	}

	if(!Peer.m_pDecoder)
	{
		int Err = OPUS_OK;
		Peer.m_pDecoder = opus_decoder_create(SAMPLE_RATE, CHANNELS, &Err);
		if(Err != OPUS_OK || !Peer.m_pDecoder)
		{
			if(Peer.m_pDecoder)
			{
				opus_decoder_destroy(Peer.m_pDecoder);
				Peer.m_pDecoder = nullptr;
			}
			return;
		}
	}

	// Ignore out-of-order stale packets
	if(Peer.m_HasSeq && (int32_t)(Seq - Peer.m_LastSeq) <= 0)
		return;

	auto AppendSamples = [&](const int16_t *pSamples, int Count) {
		if(Count <= 0)
			return;
		if(Peer.m_BufferedSamples + (uint32_t)Count > PEER_RING_SAMPLES)
		{
			// Drop oldest samples cleanly in O(1)
			const uint32_t Excess = (Peer.m_BufferedSamples + (uint32_t)Count) - PEER_RING_SAMPLES;
			Peer.m_ReadPos = (Peer.m_ReadPos + Excess) % PEER_RING_SAMPLES;
			Peer.m_BufferedSamples -= Excess;
		}
		const uint32_t FirstChunk = std::min((uint32_t)Count, PEER_RING_SAMPLES - Peer.m_WritePos);
		std::memcpy(Peer.m_aPcmRing + Peer.m_WritePos, pSamples, FirstChunk * sizeof(int16_t));
		if(FirstChunk < (uint32_t)Count)
		{
			std::memcpy(Peer.m_aPcmRing, pSamples + FirstChunk, ((uint32_t)Count - FirstChunk) * sizeof(int16_t));
		}
		Peer.m_WritePos = (Peer.m_WritePos + (uint32_t)Count) % PEER_RING_SAMPLES;
		Peer.m_BufferedSamples += (uint32_t)Count;
	};

	int16_t aDecoded[FRAME_SAMPLES];
	// Packet Loss Concealment (PLC) for up to 2 lost packets
	if(Peer.m_HasSeq && Seq > Peer.m_LastSeq + 1)
	{
		const uint32_t Lost = std::min<uint32_t>(Seq - Peer.m_LastSeq - 1, 2);
		for(uint32_t k = 0; k < Lost; ++k)
		{
			const int PlcSamples = opus_decode(Peer.m_pDecoder, nullptr, 0, aDecoded, FRAME_SAMPLES, 0);
			if(PlcSamples > 0)
				AppendSamples(aDecoded, PlcSamples);
		}
	}

	const int DecodedSamples = opus_decode(Peer.m_pDecoder, pOpusData, OpusLen, aDecoded, FRAME_SAMPLES, 0);
	if(DecodedSamples > 0)
	{
		AppendSamples(aDecoded, DecodedSamples);
		Peer.m_LastSeq = Seq;
		Peer.m_HasSeq = true;
		Peer.m_LastPacketTime = Now;
		Peer.m_SpeakingUntil = Now + time_freq() / 2;
	}
}

void CAClientVoice::PollIncomingPackets()
{
	if(!m_Socket || m_CurrentRoomHash == 0)
		return;

	NETADDR Addr;
	unsigned char *pData = nullptr;
	int Bytes = 0;
	const int64_t Now = time_get();
	int PacketsRead = 0;

	while(PacketsRead < 64 && (Bytes = net_udp_recv(m_Socket, &Addr, &pData)) > 0)
	{
		++PacketsRead;
		if(Bytes <= HEADER_LEN)
			continue;
		if(pData[0] != 'A' || pData[1] != 'C' || pData[2] != 'V' || pData[3] != '1')
			continue;
		if(pData[4] != PKT_VOICE)
			continue;

		const uint64_t RoomHash = ReadU64LE(pData + 5);
		if(RoomHash != m_CurrentRoomHash)
			continue;

		const uint32_t SenderId = ReadU32LE(pData + 13);
		const uint32_t Seq = ReadU32LE(pData + 17);
		const int PeerSlot = (int)(SenderId & 0x7F);
		if(PeerSlot < 0 || PeerSlot >= MAX_CLIENTS)
			continue;

		// Ignore own packets if somehow reflected, and ignore muted peers before Opus decode!
		if((SenderId >> 8) == m_SessionToken && PeerSlot == m_LastLocalId)
			continue;
		if(m_aMuted[PeerSlot] || m_aPeerVolume[PeerSlot] <= 0)
			continue;

		// Ignore packets from players no longer in our active team
		if(!GameClient()->m_aClients[PeerSlot].m_Active || GameClient()->m_Teams.Team(PeerSlot) != m_LastTeam)
			continue;

		const int OpusLen = Bytes - HEADER_LEN;
		if(OpusLen <= 0 || OpusLen > MAX_OPUS_BYTES)
			continue;

		DecodeIntoPeer(m_aPeers[PeerSlot], SenderId, Seq, pData + HEADER_LEN, OpusLen, Now);
		if(m_aPeers[PeerSlot].m_BufferedSamples > 0)
		{
			SetMaskBit(m_aActiveAudioMask, PeerSlot);
			SetMaskBit(m_aSpeakingHudMask, PeerSlot);
		}
	}
}

void CAClientVoice::MixAndQueuePlayback(int64_t Now)
{
	// Refresh HUD speaking masks in O(active speakers)
	for(int w = 0; w < 2; ++w)
	{
		uint64_t Mask = m_aSpeakingHudMask[w];
		while(Mask)
		{
			const int Bit = TrailingZeros64(Mask);
			Mask &= (Mask - 1);
			const int Slot = (w << 6) + Bit;
			if(Now > m_aPeers[Slot].m_SpeakingUntil || m_aMuted[Slot])
				ClearMaskBit(m_aSpeakingHudMask, Slot);
		}
	}
	if(m_LoopbackActive && Now > m_LoopbackPeer.m_SpeakingUntil && m_LoopbackPeer.m_BufferedSamples == 0)
		m_LoopbackActive = false;

	if(m_PlaybackDevice == 0)
		return;

	// Fast O(1) exit if nobody is sending audio
	if((m_aActiveAudioMask[0] | m_aActiveAudioMask[1]) == 0 && !m_LoopbackActive)
		return;

	// Keep SDL playback buffer small (~40-60ms) for minimal voice latency
	const uint32_t FrameBytes = (uint32_t)(FRAME_SAMPLES * sizeof(int16_t));
	uint32_t QueuedPlay = SDL_GetQueuedAudioSize((SDL_AudioDeviceID)m_PlaybackDevice);
	if(QueuedPlay >= FrameBytes * 3)
		return;

	const float VolumeScale = std::clamp(g_Config.m_AcVoiceVolume / 100.0f, 0.0f, 2.0f);

	int FramesMixed = 0;
	while(QueuedPlay < FrameBytes * 3 && FramesMixed < 3)
	{
		int32_t aMix[FRAME_SAMPLES] = {};
		bool AnyMixed = false;

		auto PopAndMix = [&](SVoicePeer &Peer, int SlotOrNegOne) {
			if(Peer.m_BufferedSamples < FRAME_SAMPLES)
			{
				if(Peer.m_BufferedSamples == 0)
				{
					if(SlotOrNegOne >= 0)
						ClearMaskBit(m_aActiveAudioMask, SlotOrNegOne);
					return;
				}
				// Wait up to 20ms for rest of frame unless stale (> 100ms)
				if((Now - Peer.m_LastPacketTime) < (time_freq() / 10))
					return;
			}

			const uint32_t Count = std::min<uint32_t>(Peer.m_BufferedSamples, FRAME_SAMPLES);
			const float PeerVol = (SlotOrNegOne >= 0) ? std::clamp(m_aPeerVolume[SlotOrNegOne] / 100.0f, 0.0f, 2.0f) : 1.0f;
			if(PeerVol == 1.0f)
			{
				for(uint32_t i = 0; i < Count; ++i)
				{
					aMix[i] += Peer.m_aPcmRing[Peer.m_ReadPos];
					Peer.m_ReadPos = (Peer.m_ReadPos + 1) % PEER_RING_SAMPLES;
				}
			}
			else
			{
				for(uint32_t i = 0; i < Count; ++i)
				{
					aMix[i] += (int32_t)std::lround((float)Peer.m_aPcmRing[Peer.m_ReadPos] * PeerVol);
					Peer.m_ReadPos = (Peer.m_ReadPos + 1) % PEER_RING_SAMPLES;
				}
			}
			Peer.m_BufferedSamples -= Count;
			AnyMixed = true;
			if(Peer.m_BufferedSamples == 0 && SlotOrNegOne >= 0)
				ClearMaskBit(m_aActiveAudioMask, SlotOrNegOne);
		};

		for(int w = 0; w < 2; ++w)
		{
			uint64_t Mask = m_aActiveAudioMask[w];
			while(Mask)
			{
				const int Bit = TrailingZeros64(Mask);
				Mask &= (Mask - 1);
				const int Slot = (w << 6) + Bit;
				if(m_aMuted[Slot])
				{
					m_aPeers[Slot].ClearRing();
					ClearMaskBit(m_aActiveAudioMask, Slot);
					continue;
				}
				PopAndMix(m_aPeers[Slot], Slot);
			}
		}

		if(m_LoopbackActive)
		{
			PopAndMix(m_LoopbackPeer, -1);
			if(m_LoopbackPeer.m_BufferedSamples == 0 && Now > m_LoopbackPeer.m_SpeakingUntil)
				m_LoopbackActive = false;
		}

		if(!AnyMixed)
			break;

		int16_t aOut[FRAME_SAMPLES];
		for(int i = 0; i < FRAME_SAMPLES; ++i)
		{
			const int Scaled = (int)std::lround((float)aMix[i] * VolumeScale);
			aOut[i] = (int16_t)std::clamp(Scaled, -32768, 32767);
		}
		SDL_QueueAudio((SDL_AudioDeviceID)m_PlaybackDevice, aOut, FrameBytes);
		QueuedPlay += FrameBytes;
		++FramesMixed;
	}
}

void CAClientVoice::OnUpdate()
{
	if(!g_Config.m_AcVoiceEnabled)
	{
		if(m_AudioActive || m_Socket || m_CurrentRoomHash != 0)
			StopSubsystem();
		return;
	}

	EnsureStarted();
	UpdateRelayAddress();
	UpdateRoomState();
	PumpOutgoingAudio();
	PollIncomingPackets();
	MixAndQueuePlayback(time_get());
}

void CAClientVoice::OnRender()
{
	if(!g_Config.m_AcVoiceEnabled || !g_Config.m_AcVoiceHud)
		return;
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

#if defined(CONF_VIDEORECORDER)
	if(!((IVideo::Current() && g_Config.m_ClVideoShowhud) || (!IVideo::Current() && g_Config.m_ClShowhud)))
		return;
#else
	if(!g_Config.m_ClShowhud)
		return;
#endif

	const bool HasPeersSpeaking = (m_aSpeakingHudMask[0] | m_aSpeakingHudMask[1]) != 0;
	if(!m_IsTransmitting && !HasPeersSpeaking)
		return;

	const CScreenRect PrevScreen = Graphics()->GetScreen();
	const float HudHeight = 300.0f;
	const float HudWidth = HudHeight * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(HudWidth, HudHeight);

	float X = 8.0f;
	float Y = 92.0f;
	const float RowH = 14.0f;
	const float RowW = 105.0f;

	auto DrawSpeakerPill = [&](const char *pName, float Level, bool IsSelf) {
		CUIRect Box(X, Y, RowW, RowH);
		Box.Draw(ColorRGBA(0.06f, 0.08f, 0.12f, 0.72f), IGraphics::CORNER_ALL, 3.0f);

		CUIRect Dot(X + 4.0f, Y + 4.5f, 5.0f, 5.0f);
		Dot.Draw(IsSelf ? ColorRGBA(0.25f, 0.95f, 0.45f, 0.95f) : ColorRGBA(0.30f, 0.78f, 1.0f, 0.95f), IGraphics::CORNER_ALL, 2.5f);

		if(IsSelf && Level > 0.01f)
		{
			CUIRect Meter(X + 2.0f, Y + RowH - 2.0f, (RowW - 4.0f) * std::clamp(Level, 0.0f, 1.0f), 1.5f);
			Meter.Draw(ColorRGBA(0.25f, 0.95f, 0.45f, 0.85f), IGraphics::CORNER_ALL, 0.75f);
		}

		CUIRect LabelRect(X + 12.0f, Y, RowW - 15.0f, RowH);
		TextRender()->TextColor(0.95f, 0.97f, 1.0f, 0.95f);
		Ui()->DoLabel(&LabelRect, pName, 8.5f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		Y += RowH + 2.5f;
	};

	if(m_IsTransmitting)
	{
		const int LocalId = GameClient()->m_aLocalIds[std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1)];
		const char *pSelfName = (LocalId >= 0 && LocalId < MAX_CLIENTS && GameClient()->m_aClients[LocalId].m_aName[0]) ? GameClient()->m_aClients[LocalId].m_aName : Localize("Tu");
		DrawSpeakerPill(pSelfName, m_LocalMicLevel, true);
	}

	int Shown = 0;
	for(int w = 0; w < 2 && Shown < 6; ++w)
	{
		uint64_t Mask = m_aSpeakingHudMask[w];
		while(Mask && Shown < 6)
		{
			const int Bit = TrailingZeros64(Mask);
			Mask &= (Mask - 1);
			const int Slot = (w << 6) + Bit;
			if(!GameClient()->m_aClients[Slot].m_Active)
				continue;
			DrawSpeakerPill(GameClient()->m_aClients[Slot].m_aName, 1.0f, false);
			++Shown;
		}
	}

	Graphics()->MapScreen(PrevScreen);
}

void CAClientVoice::OnStateChange(int NewState, int OldState)
{
	if(NewState != IClient::STATE_ONLINE && m_CurrentRoomHash != 0)
	{
		SendControlPacket(PKT_LEAVE, m_CurrentRoomHash);
		m_CurrentRoomHash = 0;
		m_LastTeam = -1;
		m_LastLocalId = -1;
		mem_zero(&m_LastServerAddr, sizeof(m_LastServerAddr));
		m_aActiveAudioMask[0] = 0;
		m_aActiveAudioMask[1] = 0;
		m_aSpeakingHudMask[0] = 0;
		m_aSpeakingHudMask[1] = 0;
	}
}

void CAClientVoice::OnShutdown()
{
	StopSubsystem();
	if(m_SdlAudioInit)
	{
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		m_SdlAudioInit = false;
	}
}
