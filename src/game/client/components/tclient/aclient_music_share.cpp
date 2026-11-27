#include "aclient_music_share.h"

#include <base/math.h>
#include <base/mem.h>
#include <base/net.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/client.h>
#include <engine/shared/config.h>
#include <engine/shared/protocol7.h>

#include <game/client/gameclient.h>

#include <SDL.h>
#include <SDL_audio.h>

extern "C" {
#include <opus.h>
}

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#if defined(CONF_FAMILY_WINDOWS) && __has_include(<winrt/base.h>)
#define AC_MUSICSHARE_HAS_WASAPI 1
#include <winrt/base.h>
#if !defined(NOBITMAP)
#define AC_MUSICSHARE_DEFINED_NOBITMAP
#define NOBITMAP
#endif
#define IStorage ACMusicShareIStorage
#include <windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <propsys.h>
#include <tlhelp32.h>
#undef IStorage
#if defined(AC_MUSICSHARE_DEFINED_NOBITMAP)
#undef AC_MUSICSHARE_DEFINED_NOBITMAP
#undef NOBITMAP
#endif

#if !defined(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK)
#define VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK L"VAD\\Process_Loopback"
enum PROCESS_LOOPBACK_MODE
{
	PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE = 0,
	PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE = 1,
};
struct AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS
{
	DWORD TargetProcessId;
	PROCESS_LOOPBACK_MODE ProcessLoopbackMode;
};
enum AUDIOCLIENT_ACTIVATION_TYPE
{
	AUDIOCLIENT_ACTIVATION_TYPE_DEFAULT = 0,
	AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK = 1,
};
struct AUDIOCLIENT_ACTIVATION_PARAMS
{
	AUDIOCLIENT_ACTIVATION_TYPE ActivationType;
	union
	{
		AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS ProcessLoopbackParams;
	};
};
#endif
#else
#define AC_MUSICSHARE_HAS_WASAPI 0
#endif

namespace
{
	constexpr uint8_t PKT_KEEPALIVE = 1;
	constexpr uint8_t PKT_VOICE = 2;
	constexpr uint8_t PKT_LEAVE = 3;
	constexpr uint8_t PKT_ROOM_PEERS = 4;

	constexpr int DEFAULT_RELAY_PORT = 9987;
	constexpr int NAME_FIELD_LEN = 16;
	constexpr int KEEPALIVE_LEN = CAClientMusicShare::HEADER_LEN + NAME_FIELD_LEN; // 37
	constexpr int META_TITLE_LEN = 96;
	constexpr int META_ARTIST_LEN = 96;
	constexpr int META_PACKET_LEN = CAClientMusicShare::HEADER_LEN + 4 + META_TITLE_LEN + META_ARTIST_LEN + 1 + 4 + 4; // 226

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

	inline void WriteFixedStr(uint8_t *pDst, size_t FieldLen, const char *pSrc)
	{
		std::memset(pDst, 0, FieldLen);
		if(!pSrc || !pSrc[0] || FieldLen == 0)
			return;
		const size_t CopyLen = std::min(std::strlen(pSrc), FieldLen - 1);
		std::memcpy(pDst, pSrc, CopyLen);
	}

	inline void ReadFixedStr(char *pDst, size_t DstSize, const uint8_t *pSrc, size_t FieldLen)
	{
		if(DstSize == 0)
			return;
		size_t Len = 0;
		while(Len < FieldLen && pSrc[Len] != '\0')
			++Len;
		const size_t CopyLen = std::min(Len, DstSize - 1);
		if(CopyLen > 0)
			std::memcpy(pDst, pSrc, CopyLen);
		pDst[CopyLen] = '\0';
	}

	std::string TrimCopy(const char *pStr)
	{
		if(!pStr)
			return "";
		const char *pStart = pStr;
		while(*pStart && (unsigned char)*pStart <= ' ')
			++pStart;
		const char *pEnd = pStart + std::strlen(pStart);
		while(pEnd > pStart && (unsigned char)*(pEnd - 1) <= ' ')
			--pEnd;
		return std::string(pStart, (size_t)(pEnd - pStart));
	}

#if AC_MUSICSHARE_HAS_WASAPI
	class CMusicLoopbackActivationHandler final : public IActivateAudioInterfaceCompletionHandler, public IAgileObject
	{
	public:
		CMusicLoopbackActivationHandler()
		{
			m_Event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		}
		~CMusicLoopbackActivationHandler()
		{
			if(m_Event)
				CloseHandle(m_Event);
		}

		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Riid, void **ppvObject) override
		{
			if(!ppvObject)
				return E_POINTER;
			*ppvObject = nullptr;
			if(IsEqualIID(Riid, __uuidof(IUnknown)) || IsEqualIID(Riid, __uuidof(IActivateAudioInterfaceCompletionHandler)))
			{
				*ppvObject = static_cast<IActivateAudioInterfaceCompletionHandler *>(this);
				AddRef();
				return S_OK;
			}
			if(IsEqualIID(Riid, __uuidof(IAgileObject)))
			{
				*ppvObject = static_cast<IAgileObject *>(this);
				AddRef();
				return S_OK;
			}
			return E_NOINTERFACE;
		}
		ULONG STDMETHODCALLTYPE AddRef() override { return ++m_RefCount; }
		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG N = --m_RefCount;
			if(N == 0)
				delete this;
			return N;
		}
		HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation *pOp) override
		{
			if(!pOp)
			{
				m_Hr = E_POINTER;
			}
			else
			{
				IUnknown *pUnk = nullptr;
				HRESULT ResHr = E_FAIL;
				m_Hr = pOp->GetActivateResult(&ResHr, &pUnk);
				if(SUCCEEDED(m_Hr))
				{
					m_Hr = ResHr;
					m_Activated.attach(pUnk);
				}
				else if(pUnk)
				{
					pUnk->Release();
				}
			}
			if(m_Event)
				SetEvent(m_Event);
			return S_OK;
		}
		HRESULT Wait()
		{
			if(!m_Event)
				return E_HANDLE;
			const DWORD W = WaitForSingleObject(m_Event, 1000);
			if(W != WAIT_OBJECT_0)
				return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
			return m_Hr;
		}
		IUnknown *Interface() const { return m_Activated.get(); }

	private:
		std::atomic<ULONG> m_RefCount{1};
		HANDLE m_Event = nullptr;
		HRESULT m_Hr = E_PENDING;
		winrt::com_ptr<IUnknown> m_Activated;
	};

	std::string ToLowerAscii(std::string Str)
	{
		for(char &Ch : Str)
			Ch = (char)std::tolower((unsigned char)Ch);
		return Str;
	}

	std::string ProcessBaseNameLower(DWORD Pid)
	{
		if(Pid == 0)
			return {};
		HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, Pid);
		if(!hProc)
			return {};
		char aPath[1024];
		DWORD Size = sizeof(aPath);
		std::string Out;
		if(QueryFullProcessImageNameA(hProc, 0, aPath, &Size))
		{
			const char *pB1 = std::strrchr(aPath, '\\');
			const char *pB2 = std::strrchr(aPath, '/');
			const char *pBase = pB1 ? pB1 : pB2;
			Out = ToLowerAscii(pBase ? pBase + 1 : aPath);
		}
		CloseHandle(hProc);
		return Out;
	}

	bool IsBlockedProcess(const std::string &Name)
	{
		if(Name.empty())
			return true;
		static constexpr const char *s_apBlocked[] = {
			"discord.exe", "discordcanary.exe", "discordptb.exe",
			"ddnet.exe", "aclient.exe", "bestclient.exe", "tclient.exe", "teeworlds.exe",
			"steam.exe", "steamservice.exe", "steamwebhelper.exe",
		};
		for(const char *pB : s_apBlocked)
		{
			if(Name == pB)
				return true;
		}
		return false;
	}

	int MusicProcessPriority(const std::string &Name)
	{
		if(IsBlockedProcess(Name))
			return -1;
		static constexpr const char *s_apDedicated[] = {
			"spotify.exe", "spotifywebhelper.exe", "applemusic.exe", "apple music.exe",
			"applemusicpreview.exe", "itunes.exe", "tidal.exe", "deezer.exe", "soundcloud.exe",
			"cider.exe", "foobar2000.exe", "musicbee.exe", "aimp.exe", "vlc.exe", "mpv.exe",
			"qobuz.exe", "qobuzdesktop.exe", "yandexmusic.exe", "youtube music.exe",
			"youtubemusic.exe", "ytmdesktop.exe", "pear-desktop.exe", "music.ui.exe",
		};
		for(const char *pM : s_apDedicated)
		{
			if(Name == pM)
				return 100;
		}
		static constexpr const char *s_apBrowsers[] = {
			"chrome.exe", "chrome_pwa_launcher.exe", "brave.exe", "msedge.exe", "msedge_proxy.exe",
			"firefox.exe", "opera.exe", "opera_gx.exe", "vivaldi.exe", "arc.exe", "chromium.exe",
			"floorp.exe", "librewolf.exe", "zen.exe", "waterfox.exe",
		};
		for(const char *pBr : s_apBrowsers)
		{
			if(Name == pBr)
				return 50;
		}
		return -1;
	}

	DWORD ResolveRootMusicPid(DWORD Pid)
	{
		if(Pid == 0)
			return 0;
		HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if(hSnap == INVALID_HANDLE_VALUE)
			return Pid;

		auto FindParent = [&](DWORD CurPid, std::string &OutName) -> DWORD {
			PROCESSENTRY32 Entry{};
			Entry.dwSize = sizeof(Entry);
			if(Process32First(hSnap, &Entry))
			{
				do
				{
					if(Entry.th32ProcessID == CurPid)
					{
						OutName = ProcessBaseNameLower(CurPid);
						return Entry.th32ParentProcessID;
					}
				} while(Process32Next(hSnap, &Entry));
			}
			return 0;
		};

		DWORD Root = Pid;
		std::string CurName;
		DWORD ParentPid = FindParent(Pid, CurName);
		for(int Depth = 0; Depth < 8 && ParentPid != 0; ++Depth)
		{
			std::string ParentName;
			const DWORD NextParent = FindParent(ParentPid, ParentName);
			if(ParentName.empty() || (ParentName != CurName &&
				!(CurName == "chrome_pwa_launcher.exe" && ParentName == "chrome.exe") &&
				!(CurName == "msedge_proxy.exe" && ParentName == "msedge.exe") &&
				!(CurName == "spotifywebhelper.exe" && ParentName == "spotify.exe")))
			{
				break;
			}
			Root = ParentPid;
			CurName = ParentName;
			ParentPid = NextParent;
		}
		CloseHandle(hSnap);
		return Root;
	}

	DWORD FindActiveMusicProcessId()
	{
		winrt::com_ptr<IMMDeviceEnumerator> pEnum;
		if(FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(pEnum.put()))))
			return 0;

		winrt::com_ptr<IMMDevice> pDev;
		if(FAILED(pEnum->GetDefaultAudioEndpoint(eRender, eMultimedia, pDev.put())) &&
			FAILED(pEnum->GetDefaultAudioEndpoint(eRender, eConsole, pDev.put())))
			return 0;

		winrt::com_ptr<IAudioSessionManager2> pMgr;
		if(FAILED(pDev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(pMgr.put()))))
			return 0;

		winrt::com_ptr<IAudioSessionEnumerator> pSessions;
		if(FAILED(pMgr->GetSessionEnumerator(pSessions.put())))
			return 0;

		int Count = 0;
		if(FAILED(pSessions->GetCount(&Count)))
			return 0;

		DWORD BestPid = 0;
		int BestScore = -1;
		float BestPeak = 0.0f;

		for(int i = 0; i < Count; ++i)
		{
			winrt::com_ptr<IAudioSessionControl> pCtrl;
			if(FAILED(pSessions->GetSession(i, pCtrl.put())) || !pCtrl)
				continue;

			AudioSessionState State = AudioSessionStateInactive;
			if(FAILED(pCtrl->GetState(&State)) || State != AudioSessionStateActive)
				continue;

			winrt::com_ptr<IAudioSessionControl2> pCtrl2;
			if(FAILED(pCtrl->QueryInterface(IID_PPV_ARGS(pCtrl2.put()))) || !pCtrl2)
				continue;

			DWORD Pid = 0;
			if(FAILED(pCtrl2->GetProcessId(&Pid)) || Pid == 0)
				continue;

			const std::string Name = ProcessBaseNameLower(Pid);
			const int Prio = MusicProcessPriority(Name);
			if(Prio < 0)
				continue;

			float Peak = 0.0f;
			winrt::com_ptr<IAudioMeterInformation> pMeter;
			if(SUCCEEDED(pCtrl->QueryInterface(IID_PPV_ARGS(pMeter.put()))) && pMeter)
				pMeter->GetPeakValue(&Peak);

			const int Score = Prio + (Peak > 0.0005f ? 200 : 0);
			if(Score > BestScore || (Score == BestScore && Peak > BestPeak))
			{
				BestScore = Score;
				BestPeak = Peak;
				BestPid = Pid;
			}
		}

		return BestPid ? ResolveRootMusicPid(BestPid) : 0;
	}
#endif
} // namespace

void CAClientMusicShare::OnInit()
{
	uint64_t Seed = (uint64_t)time_get() ^ ((uint64_t)(uintptr_t)this * 0xBF58476D1CE4E5B9ULL);
	Seed ^= (Seed >> 30);
	m_SessionToken = (uint32_t)((Seed & 0x00FFFFFFU) | 1U);
}

void CAClientMusicShare::OnConsoleInit()
{
	Console()->Register("ac_music_share", "s[player_name]", CFGFLAG_CLIENT, ConShare, this, "Invite an AClient friend to listen to your music (1-to-1)");
	Console()->Register("ac_music_stop", "", CFGFLAG_CLIENT, ConStop, this, "Stop sharing or listening to shared music");
	Console()->Register("ac_music_accept", "", CFGFLAG_CLIENT, ConAccept, this, "Accept pending shared music invite");
	Console()->Register("ac_music_decline", "", CFGFLAG_CLIENT, ConDecline, this, "Decline pending shared music invite");
	Console()->Register("ac_music_leave", "", CFGFLAG_CLIENT, ConLeave, this, "Leave current shared music listening session");
	Console()->Register("ac_music_mute", "", CFGFLAG_CLIENT, ConMute, this, "Toggle mute for shared music playback");
}

void CAClientMusicShare::ConShare(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CAClientMusicShare *>(pUserData)->InviteFriend(pResult->GetString(0));
}

void CAClientMusicShare::ConStop(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CAClientMusicShare *>(pUserData)->StopSession(true);
}

void CAClientMusicShare::ConAccept(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CAClientMusicShare *>(pUserData)->AcceptPendingInvite();
}

void CAClientMusicShare::ConDecline(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CAClientMusicShare *>(pUserData)->DeclinePendingInvite();
}

void CAClientMusicShare::ConLeave(IConsole::IResult *pResult, void *pUserData)
{
	static_cast<CAClientMusicShare *>(pUserData)->StopSession(true);
}

void CAClientMusicShare::ConMute(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CAClientMusicShare *>(pUserData);
	pSelf->ToggleMute();
	if(pSelf->GameClient())
		pSelf->GameClient()->Echo(pSelf->IsMuted() ? "[AClient Music] Musica compartida silenciada" : "[AClient Music] Musica compartida activada");
}

void CAClientMusicShare::WriteHeader(uint8_t *pDst, uint8_t Type, uint64_t RoomHash, uint32_t SenderId, uint32_t Seq)
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

uint64_t CAClientMusicShare::ComputeMusicRoomHash(const char *pRoomCode)
{
	char aKey[64];
	str_format(aKey, sizeof(aKey), "AC_MUSIC|%s", pRoomCode ? pRoomCode : "");
	uint64_t Hash = 14695981039346656037ULL;
	for(const unsigned char *p = (const unsigned char *)aKey; *p; ++p)
	{
		Hash ^= *p;
		Hash *= 1099511628211ULL;
	}
	return Hash == 0 ? 1ULL : Hash;
}

const char *CAClientMusicShare::GetLocalPlayerName() const
{
	const int ActiveDummy = std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
	if(GameClient() && Client() && Client()->State() == IClient::STATE_ONLINE)
	{
		const int LocalId = GameClient()->m_aLocalIds[ActiveDummy];
		if(LocalId >= 0 && LocalId < MAX_CLIENTS && GameClient()->m_aClients[LocalId].m_Active && GameClient()->m_aClients[LocalId].m_aName[0] != '\0')
			return GameClient()->m_aClients[LocalId].m_aName;
	}
	if(Client() && Client()->PlayerName()[0] != '\0')
		return Client()->PlayerName();
	if(ActiveDummy == 1 && g_Config.m_ClDummyName[0] != '\0')
		return g_Config.m_ClDummyName;
	if(g_Config.m_PlayerName[0] != '\0')
		return g_Config.m_PlayerName;
	return "Jugador";
}

void CAClientMusicShare::UpdateRelayAddress()
{
	const char *pCfgAddr = g_Config.m_AcVoiceRelayAddr;
	const int64_t Now = time_get();
	if(m_RelayAddrValid && str_comp(pCfgAddr, m_aLastRelayAddrStr) == 0)
		return;
	if(!m_RelayAddrValid && str_comp(pCfgAddr, m_aLastRelayAddrStr) == 0 && Now < m_NextResolveRetryTime)
		return;

	str_copy(m_aLastRelayAddrStr, pCfgAddr);
	m_RelayAddrValid = false;
	m_NextResolveRetryTime = Now + time_freq() * 5;
	if(!pCfgAddr || pCfgAddr[0] == '\0')
		return;

	char aAddrWithPort[128];
	if(!str_find(pCfgAddr, ":"))
		str_format(aAddrWithPort, sizeof(aAddrWithPort), "%s:%d", pCfgAddr, DEFAULT_RELAY_PORT);
	else
		str_copy(aAddrWithPort, pCfgAddr);

	if(net_addr_from_str(&m_RelayAddr, aAddrWithPort) == 0)
	{
		if(m_RelayAddr.port == 0)
			m_RelayAddr.port = DEFAULT_RELAY_PORT;
		m_RelayAddrValid = true;
		return;
	}

	char aHost[128];
	str_copy(aHost, aAddrWithPort);
	int Port = DEFAULT_RELAY_PORT;
	char *pColon = const_cast<char *>(str_rchr(aHost, ':'));
	if(pColon)
	{
		*pColon = '\0';
		Port = str_toint(pColon + 1);
		if(Port <= 0 || Port > 65535)
			Port = DEFAULT_RELAY_PORT;
	}
	if(net_host_lookup(aHost, &m_RelayAddr, NETTYPE_IPV4) == 0)
	{
		m_RelayAddr.port = Port;
		m_RelayAddrValid = true;
	}
}

bool CAClientMusicShare::EnsureSocket()
{
	if(!m_Socket)
	{
		NETADDR BindAddr = {};
		BindAddr.type = NETTYPE_IPV4;
		m_Socket = net_udp_create(BindAddr);
	}
	UpdateRelayAddress();
	return m_Socket != nullptr && m_RelayAddrValid;
}

void CAClientMusicShare::CloseSocket()
{
	if(m_Socket)
	{
		if(m_RoomHash != 0 && m_RelayAddrValid)
			SendControlPacket(PKT_LEAVE, m_RoomHash);
		net_udp_close(m_Socket);
		m_Socket = nullptr;
	}
}

void CAClientMusicShare::SendMusicWhisper(const char *pTargetName, const char *pPayload)
{
	if(!GameClient() || !Client() || Client()->State() != IClient::STATE_ONLINE || !pTargetName || !pTargetName[0] || !pPayload)
		return;

	int TargetId = -1;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(GameClient()->m_aClients[i].m_Active && str_comp_nocase(GameClient()->m_aClients[i].m_aName, pTargetName) == 0)
		{
			TargetId = i;
			break;
		}
	}

	char aFullMsg[MAX_CHAT_LENGTH];
	str_format(aFullMsg, sizeof(aFullMsg), "[AC-MUSIC] %s", pPayload);

	if(Client()->IsSixup() && TargetId >= 0 && TargetId < MAX_CLIENTS)
	{
		protocol7::CNetMsg_Cl_Say Msg7;
		Msg7.m_Mode = protocol7::CHAT_WHISPER;
		Msg7.m_Target = TargetId;
		Msg7.m_pMessage = aFullMsg;
		Client()->SendPackMsgActive(&Msg7, MSGFLAG_VITAL, true);
		return;
	}

	char aSafeTarget[MAX_NAME_LENGTH];
	str_copy(aSafeTarget, pTargetName);
	for(char *p = aSafeTarget; *p; ++p)
	{
		if(*p == '"')
			*p = '\'';
	}
	char aCmd[MAX_CHAT_LENGTH];
	str_format(aCmd, sizeof(aCmd), "/w \"%s\" %s", aSafeTarget, aFullMsg);
	GameClient()->m_Chat.SendChat(0, aCmd);
}

void CAClientMusicShare::SendControlPacket(uint8_t Type, uint64_t RoomHash)
{
	if(!m_Socket || !m_RelayAddrValid || RoomHash == 0)
		return;

	const uint32_t SenderId = (m_SessionToken << 8) | 1U;
	if(Type == PKT_KEEPALIVE)
	{
		uint8_t aPacket[KEEPALIVE_LEN];
		WriteHeader(aPacket, PKT_KEEPALIVE, RoomHash, SenderId, m_Seq++);
		WriteFixedStr(aPacket + HEADER_LEN, NAME_FIELD_LEN, GetLocalPlayerName());
		net_udp_send(m_Socket, &m_RelayAddr, aPacket, KEEPALIVE_LEN);
	}
	else if(Type == PKT_LEAVE)
	{
		uint8_t aPacket[HEADER_LEN];
		WriteHeader(aPacket, PKT_LEAVE, RoomHash, SenderId, m_Seq++);
		net_udp_send(m_Socket, &m_RelayAddr, aPacket, HEADER_LEN);
	}
}

void CAClientMusicShare::SendMetadataPacket(bool Force)
{
	if(m_State != STATE_HOSTING || !m_Socket || !m_RelayAddrValid || m_RoomHash == 0 || !GameClient())
		return;

	const int64_t Now = time_get();
	if(!Force && m_LastMetaPollTime != 0 && (Now - m_LastMetaPollTime) < time_freq() / 4)
		return;
	m_LastMetaPollTime = Now;

	char aTitle[META_TITLE_LEN] = {};
	char aArtist[META_ARTIST_LEN] = {};
	bool Playing = true;
	int64_t PosMs = 0;
	int64_t DurMs = 0;

	if(!GameClient()->m_MusicPlayer.GetCurrentTrackInfo(aTitle, sizeof(aTitle), aArtist, sizeof(aArtist), &Playing, &PosMs, &DurMs))
	{
		str_copy(aTitle, "Musica en vivo");
		aArtist[0] = '\0';
		Playing = (GetCaptureStatus() == CAPTURE_ACTIVE);
	}

	const bool Changed = str_comp(aTitle, m_aLastSentTitle) != 0 ||
		str_comp(aArtist, m_aLastSentArtist) != 0 ||
		Playing != m_LastSentPlaying;
	if(!Force && !Changed && (Now - m_LastMetaSendTime) < time_freq() * 4)
		return;

	str_copy(m_aLastSentTitle, aTitle);
	str_copy(m_aLastSentArtist, aArtist);
	m_LastSentPlaying = Playing;
	m_LastMetaSendTime = Now;

	uint8_t aPacket[META_PACKET_LEN] = {};
	const uint32_t SenderId = (m_SessionToken << 8) | 1U;
	WriteHeader(aPacket, PKT_VOICE, m_RoomHash, SenderId, 0);
	int Off = HEADER_LEN;
	aPacket[Off + 0] = 'A';
	aPacket[Off + 1] = 'C';
	aPacket[Off + 2] = 'M';
	aPacket[Off + 3] = 'T';
	Off += 4;
	WriteFixedStr(aPacket + Off, META_TITLE_LEN, aTitle);
	Off += META_TITLE_LEN;
	WriteFixedStr(aPacket + Off, META_ARTIST_LEN, aArtist);
	Off += META_ARTIST_LEN;
	aPacket[Off++] = Playing ? 1 : 0;
	WriteU32LE(aPacket + Off, (uint32_t)std::clamp<int64_t>(PosMs, 0, 0x7FFFFFFF));
	Off += 4;
	WriteU32LE(aPacket + Off, (uint32_t)std::clamp<int64_t>(DurMs, 0, 0x7FFFFFFF));
	Off += 4;
	net_udp_send(m_Socket, &m_RelayAddr, aPacket, Off);
}

void CAClientMusicShare::InviteFriend(const char *pTargetName)
{
	const std::string Target = TrimCopy(pTargetName);
	if(Target.empty() || !GameClient())
		return;

	if(!Client() || Client()->State() != IClient::STATE_ONLINE)
	{
		GameClient()->Echo("[AClient Music] Conectate a un servidor para compartir musica con un amigo");
		return;
	}

	// Strict 1-to-1: stop any prior session before starting a new invite
	if(m_State != STATE_IDLE)
		StopSession(true);

	const uint32_t RandNum = 1000U + (uint32_t)(((uint64_t)time_get() ^ (uint64_t)m_SessionToken) % 9000ULL);
	str_format(m_aRoomCode, sizeof(m_aRoomCode), "MUS-%u", RandNum);
	str_copy(m_aPeerName, Target.c_str());
	m_RoomHash = ComputeMusicRoomHash(m_aRoomCode);
	m_State = STATE_WAITING_ACCEPT;
	m_StateStartedAt = time_get();
	m_LastPeerActivityAt = m_StateStartedAt;
	m_PeerSeenOnRelay = false;

	char aTitle[96] = {};
	char aArtist[96] = {};
	char aTrackSummary[128] = "Musica en vivo";
	if(GameClient()->m_MusicPlayer.GetCurrentTrackInfo(aTitle, sizeof(aTitle), aArtist, sizeof(aArtist), nullptr, nullptr, nullptr) && aTitle[0] != '\0')
	{
		if(aArtist[0] != '\0')
			str_format(aTrackSummary, sizeof(aTrackSummary), "%.55s - %.40s", aTitle, aArtist);
		else
			str_format(aTrackSummary, sizeof(aTrackSummary), "%.90s", aTitle);
	}

	char aPayload[MAX_CHAT_LENGTH];
	str_format(aPayload, sizeof(aPayload), "INV %s | %s", m_aRoomCode, aTrackSummary);
	SendMusicWhisper(m_aPeerName, aPayload);

	char aEcho[192];
	str_format(aEcho, sizeof(aEcho), "[AClient Music] Invitacion enviada a '%s' (%s). Esperando aceptacion...", m_aPeerName, aTrackSummary);
	GameClient()->Echo(aEcho);
}

bool CAClientMusicShare::OnMusicWhisperInvite(int ClientId, int Team, const char *pLine)
{
	if(!pLine || !GameClient())
		return false;
	const char *pTag = str_find(pLine, "[AC-MUSIC]");
	if(!pTag)
		return false;

	// Suppress our own outgoing whisper echoes cleanly (just like Chess)
	if(Team == 2 || ClientId == GameClient()->m_aLocalIds[0] || ClientId == GameClient()->m_aLocalIds[1])
		return true;
	if(str_find(pLine, "\xe2\x86\x92 ") != nullptr)
		return true;

	char aSenderName[MAX_NAME_LENGTH] = {};
	if(ClientId >= 0 && ClientId < MAX_CLIENTS && GameClient()->m_aClients[ClientId].m_Active)
	{
		str_copy(aSenderName, GameClient()->m_aClients[ClientId].m_aName);
	}
	else
	{
		const char *pArrow = str_find(pLine, "\xe2\x86\x90 ");
		const char *pBracket = pArrow ? str_find(pArrow + 4, "]") : nullptr;
		if(pArrow && pBracket && pBracket > pArrow + 4)
			str_truncate(aSenderName, sizeof(aSenderName), pArrow + 4, (int)(pBracket - (pArrow + 4)));
	}

	const char *pMyName = GetLocalPlayerName();
	if(aSenderName[0] != '\0' && pMyName && str_comp(aSenderName, pMyName) == 0)
		return true;

	const char *pCmd = pTag + 10;
	while(*pCmd == ' ')
		++pCmd;

	if(str_startswith(pCmd, "INV "))
	{
		const char *pRest = pCmd + 4;
		while(*pRest == ' ')
			++pRest;

		std::string RoomStr;
		std::string TrackStr = "Musica en vivo";
		const char *pPipe = str_find(pRest, "|");
		if(pPipe)
		{
			RoomStr = TrimCopy(std::string(pRest, (size_t)(pPipe - pRest)).c_str());
			const std::string Tr = TrimCopy(pPipe + 1);
			if(!Tr.empty())
				TrackStr = Tr;
		}
		else
		{
			RoomStr = TrimCopy(pRest);
		}

		if(RoomStr.empty())
			return true;

		str_copy(m_aPendingInviteFrom, aSenderName[0] ? aSenderName : "Amigo");
		str_copy(m_aPendingInviteRoom, RoomStr.c_str());
		str_copy(m_aPendingInviteTrack, TrackStr.c_str());
		m_PendingInviteExpiresAt = time_get() + time_freq() * 120;

		char aEcho[224];
		str_format(aEcho, sizeof(aEcho), "[AClient Music] '%s' quiere compartirte su musica (%s). Toca ESC en la isla de musica o usa /ac_music_accept",
			m_aPendingInviteFrom, m_aPendingInviteTrack);
		GameClient()->Echo(aEcho);
		return true;
	}

	if(str_startswith(pCmd, "ACC "))
	{
		const std::string RoomStr = TrimCopy(pCmd + 4);
		if(m_State == STATE_WAITING_ACCEPT && str_comp(RoomStr.c_str(), m_aRoomCode) == 0)
		{
			if(aSenderName[0] != '\0')
				str_copy(m_aPeerName, aSenderName);
			StartHostStreaming();
		}
		return true;
	}

	if(str_startswith(pCmd, "DEC "))
	{
		const std::string RoomStr = TrimCopy(pCmd + 4);
		if(m_State == STATE_WAITING_ACCEPT && str_comp(RoomStr.c_str(), m_aRoomCode) == 0)
		{
			char aEcho[160];
			str_format(aEcho, sizeof(aEcho), "[AClient Music] '%s' rechazo la invitacion de musica", m_aPeerName);
			GameClient()->Echo(aEcho);
			StopSession(false);
		}
		return true;
	}

	if(str_startswith(pCmd, "STOP"))
	{
		const std::string RoomStr = TrimCopy(pCmd + 4);
		if(m_State != STATE_IDLE && (RoomStr.empty() || str_comp(RoomStr.c_str(), m_aRoomCode) == 0))
		{
			char aEcho[160];
			if(m_State == STATE_LISTENING)
				str_format(aEcho, sizeof(aEcho), "[AClient Music] '%s' dejo de compartir su musica", m_aPeerName);
			else
				str_format(aEcho, sizeof(aEcho), "[AClient Music] '%s' dejo de escuchar tu musica", m_aPeerName);
			GameClient()->Echo(aEcho);
			StopSession(false);
		}
		return true;
	}

	return true;
}

bool CAClientMusicShare::HasPendingInvite() const
{
	return m_aPendingInviteRoom[0] != '\0' && time_get() <= m_PendingInviteExpiresAt;
}

void CAClientMusicShare::AcceptPendingInvite()
{
	if(!HasPendingInvite())
		return;

	char aFrom[MAX_NAME_LENGTH];
	char aRoom[32];
	char aTrack[160];
	str_copy(aFrom, m_aPendingInviteFrom);
	str_copy(aRoom, m_aPendingInviteRoom);
	str_copy(aTrack, m_aPendingInviteTrack);

	m_aPendingInviteFrom[0] = '\0';
	m_aPendingInviteRoom[0] = '\0';
	m_aPendingInviteTrack[0] = '\0';
	m_PendingInviteExpiresAt = 0;

	if(m_State != STATE_IDLE)
		StopSession(true);

	str_copy(m_aPeerName, aFrom);
	str_copy(m_aRoomCode, aRoom);
	m_RoomHash = ComputeMusicRoomHash(m_aRoomCode);
	m_State = STATE_LISTENING;
	m_StateStartedAt = time_get();
	m_LastPeerActivityAt = m_StateStartedAt;
	m_PeerSeenOnRelay = false;
	m_Muted = false;

	// Seed initial remote track title from invite text before first ACMT arrives
	m_RemoteTrack = SRemoteTrackInfo{};
	const char *pDash = str_find(aTrack, " - ");
	if(pDash)
	{
		str_truncate(m_RemoteTrack.m_aTitle, sizeof(m_RemoteTrack.m_aTitle), aTrack, (int)(pDash - aTrack));
		str_copy(m_RemoteTrack.m_aArtist, pDash + 3);
	}
	else
	{
		str_copy(m_RemoteTrack.m_aTitle, aTrack);
	}
	m_RemoteTrack.m_Playing = true;
	m_RemoteTrack.m_UpdatedAt = m_StateStartedAt;

	StartListenerPlayback();

	char aPayload[64];
	str_format(aPayload, sizeof(aPayload), "ACC %s", m_aRoomCode);
	SendMusicWhisper(m_aPeerName, aPayload);

	if(GameClient())
	{
		char aEcho[192];
		str_format(aEcho, sizeof(aEcho), "[AClient Music] Escuchando musica con '%s'", m_aPeerName);
		GameClient()->Echo(aEcho);
	}
}

void CAClientMusicShare::DeclinePendingInvite()
{
	if(m_aPendingInviteRoom[0] != '\0' && m_aPendingInviteFrom[0] != '\0')
	{
		char aPayload[64];
		str_format(aPayload, sizeof(aPayload), "DEC %s", m_aPendingInviteRoom);
		SendMusicWhisper(m_aPendingInviteFrom, aPayload);
	}
	m_aPendingInviteFrom[0] = '\0';
	m_aPendingInviteRoom[0] = '\0';
	m_aPendingInviteTrack[0] = '\0';
	m_PendingInviteExpiresAt = 0;
}

void CAClientMusicShare::StopSession(bool NotifyPeer)
{
	if(NotifyPeer && m_State != STATE_IDLE && m_aPeerName[0] != '\0' && m_aRoomCode[0] != '\0')
	{
		char aPayload[64];
		str_format(aPayload, sizeof(aPayload), "STOP %s", m_aRoomCode);
		SendMusicWhisper(m_aPeerName, aPayload);
	}

	StopHostStreaming();
	StopListenerPlayback();
	CloseSocket();

	m_State = STATE_IDLE;
	m_aRoomCode[0] = '\0';
	m_aPeerName[0] = '\0';
	m_RoomHash = 0;
	m_PeerSeenOnRelay = false;
	m_LastMetaPollTime = 0;
	m_RemoteTrack = SRemoteTrackInfo{};
	m_aVisBars.fill(0.0f);
}

void CAClientMusicShare::StartHostStreaming()
{
	StopHostStreaming();
	if(!EnsureSocket())
	{
		if(GameClient())
			GameClient()->Echo("[AClient Music] No se pudo conectar al relay de audio");
		StopSession(true);
		return;
	}

	int Err = OPUS_OK;
	m_pEncoder = opus_encoder_create(SAMPLE_RATE, CHANNELS, OPUS_APPLICATION_AUDIO, &Err);
	if(Err != OPUS_OK || !m_pEncoder)
	{
		if(m_pEncoder)
		{
			opus_encoder_destroy(m_pEncoder);
			m_pEncoder = nullptr;
		}
		StopSession(true);
		return;
	}

	opus_encoder_ctl(m_pEncoder, OPUS_SET_BITRATE(96000));
	opus_encoder_ctl(m_pEncoder, OPUS_SET_VBR(1));
	opus_encoder_ctl(m_pEncoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
	opus_encoder_ctl(m_pEncoder, OPUS_SET_COMPLEXITY(5));
	opus_encoder_ctl(m_pEncoder, OPUS_SET_INBAND_FEC(1));
	opus_encoder_ctl(m_pEncoder, OPUS_SET_PACKET_LOSS_PERC(5));

	m_CapWriteIdx.store(0, std::memory_order_relaxed);
	m_CapReadIdx.store(0, std::memory_order_relaxed);
	m_CaptureStop.store(false, std::memory_order_relaxed);
	m_CaptureStatus.store(CAPTURE_SEARCHING, std::memory_order_relaxed);

	m_State = STATE_HOSTING;
	m_StateStartedAt = time_get();
	m_LastPeerActivityAt = m_StateStartedAt;
	m_LastAudibleTime = m_StateStartedAt;
	m_LastKeepaliveTime = 0;
	m_LastMetaSendTime = 0;
	m_aLastSentTitle[0] = '\0';
	m_aLastSentArtist[0] = '\0';
	m_Seq = 1;

	SendControlPacket(PKT_KEEPALIVE, m_RoomHash);
	SendMetadataPacket(true);

	m_CaptureThread = std::thread(&CAClientMusicShare::CaptureThreadMain, this);

	if(GameClient())
	{
		char aEcho[192];
		str_format(aEcho, sizeof(aEcho), "[AClient Music] '%s' acepto! Transmitiendo tu musica en estereo.", m_aPeerName);
		GameClient()->Echo(aEcho);
	}
}

void CAClientMusicShare::StopHostStreaming()
{
	m_CaptureStop.store(true, std::memory_order_release);
	if(m_CaptureThread.joinable())
		m_CaptureThread.join();
	m_CaptureStatus.store(CAPTURE_NONE, std::memory_order_relaxed);
	if(m_pEncoder)
	{
		opus_encoder_destroy(m_pEncoder);
		m_pEncoder = nullptr;
	}
	m_CapWriteIdx.store(0, std::memory_order_relaxed);
	m_CapReadIdx.store(0, std::memory_order_relaxed);
}

void CAClientMusicShare::StartListenerPlayback()
{
	StopListenerPlayback();
	if(!EnsureSocket())
	{
		if(GameClient())
			GameClient()->Echo("[AClient Music] No se pudo conectar al relay de audio");
		StopSession(false);
		return;
	}

	if(SDL_WasInit(SDL_INIT_AUDIO) == 0)
	{
		if(SDL_InitSubSystem(SDL_INIT_AUDIO) == 0)
			m_SdlAudioInit = true;
	}

	SDL_AudioSpec Want = {};
	Want.freq = SAMPLE_RATE;
	Want.format = AUDIO_S16SYS;
	Want.channels = CHANNELS;
	Want.samples = FRAME_SAMPLES;
	m_PlaybackDevice = SDL_OpenAudioDevice(nullptr, 0, &Want, nullptr, 0);
	if(m_PlaybackDevice != 0)
		SDL_PauseAudioDevice((SDL_AudioDeviceID)m_PlaybackDevice, 0);

	int Err = OPUS_OK;
	m_pDecoder = opus_decoder_create(SAMPLE_RATE, CHANNELS, &Err);
	if(Err != OPUS_OK && m_pDecoder)
	{
		opus_decoder_destroy(m_pDecoder);
		m_pDecoder = nullptr;
	}

	m_PlayReadPos = 0;
	m_PlayWritePos = 0;
	m_PlayBufferedSamples = 0;
	m_PlayPrimed = false;
	m_LastRemoteSenderId = 0;
	m_LastRemoteSeq = 0;
	m_HasRemoteSeq = false;
	m_NextMixTime = time_get();
	m_LastKeepaliveTime = 0;
	m_aVisBars.fill(0.0f);
	m_VisLowLp = 0.0f;
	m_VisMidLp = 0.0f;

	SendControlPacket(PKT_KEEPALIVE, m_RoomHash);
}

void CAClientMusicShare::StopListenerPlayback()
{
	if(m_PlaybackDevice != 0)
	{
		SDL_ClearQueuedAudio((SDL_AudioDeviceID)m_PlaybackDevice);
		SDL_CloseAudioDevice((SDL_AudioDeviceID)m_PlaybackDevice);
		m_PlaybackDevice = 0;
	}
	if(m_pDecoder)
	{
		opus_decoder_destroy(m_pDecoder);
		m_pDecoder = nullptr;
	}
	m_PlayReadPos = 0;
	m_PlayWritePos = 0;
	m_PlayBufferedSamples = 0;
	m_PlayPrimed = false;
	m_HasRemoteSeq = false;
}

void CAClientMusicShare::PushCaptureSamples(const int16_t *pSamples, uint32_t Count)
{
	if(!pSamples || Count == 0)
		return;
	const uint32_t W = m_CapWriteIdx.load(std::memory_order_relaxed);
	const uint32_t R = m_CapReadIdx.load(std::memory_order_acquire);
	const uint32_t Used = W - R;
	if(Used >= CAPTURE_RING_CAPACITY)
		return;
	const uint32_t Free = CAPTURE_RING_CAPACITY - Used;
	const uint32_t ToWrite = std::min(Count, Free);
	const uint32_t Start = W & CAPTURE_RING_MASK;
	const uint32_t First = std::min(ToWrite, CAPTURE_RING_CAPACITY - Start);
	std::memcpy(m_aCaptureRing.data() + Start, pSamples, First * sizeof(int16_t));
	if(First < ToWrite)
		std::memcpy(m_aCaptureRing.data(), pSamples + First, (ToWrite - First) * sizeof(int16_t));
	m_CapWriteIdx.store(W + ToWrite, std::memory_order_release);
}

void CAClientMusicShare::CaptureThreadMain()
{
#if AC_MUSICSHARE_HAS_WASAPI
	const HRESULT ComHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	const bool ComInited = SUCCEEDED(ComHr);

	winrt::com_ptr<IAudioClient> pAudioClient;
	winrt::com_ptr<IAudioCaptureClient> pCaptureClient;
	DWORD ActivePid = 0;
	int SrcSampleRate = SAMPLE_RATE;
	auto LastScan = std::chrono::steady_clock::now() - std::chrono::seconds(5);
	std::vector<int16_t> vResampleBuf;
	vResampleBuf.reserve(8192);
	double ResamplePhase = 0.0;
	int16_t LastL = 0;
	int16_t LastR = 0;

	auto CleanupCapture = [&]() {
		if(pAudioClient)
			pAudioClient->Stop();
		pCaptureClient = nullptr;
		pAudioClient = nullptr;
		ActivePid = 0;
		ResamplePhase = 0.0;
	};

	auto StartPidCapture = [&](DWORD Pid) -> bool {
		CleanupCapture();
		if(Pid == 0)
			return false;

		AUDIOCLIENT_ACTIVATION_PARAMS Params{};
		Params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
		Params.ProcessLoopbackParams.TargetProcessId = Pid;
		Params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

		PROPVARIANT Prop;
		PropVariantInit(&Prop);
		Prop.vt = VT_BLOB;
		Prop.blob.cbSize = sizeof(Params);
		Prop.blob.pBlobData = reinterpret_cast<BYTE *>(&Params);

		auto *pHandler = new(std::nothrow) CMusicLoopbackActivationHandler();
		if(!pHandler)
			return false;

		winrt::com_ptr<IActivateAudioInterfaceAsyncOperation> pOp;
		HRESULT Hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &Prop, pHandler, pOp.put());
		if(FAILED(Hr))
		{
			pHandler->Release();
			return false;
		}
		Hr = pHandler->Wait();
		bool GotClient = false;
		if(SUCCEEDED(Hr) && pHandler->Interface())
			GotClient = SUCCEEDED(pHandler->Interface()->QueryInterface(__uuidof(IAudioClient), pAudioClient.put_void()));
		pHandler->Release();
		if(!GotClient || !pAudioClient)
			return false;

		// Prefer 48000 Hz stereo PCM16 natively (zero resampling), fallback to 44100 Hz
		const int aRates[] = {48000, 44100};
		bool Inited = false;
		for(int Rate : aRates)
		{
			WAVEFORMATEX Fmt{};
			Fmt.wFormatTag = WAVE_FORMAT_PCM;
			Fmt.nChannels = 2;
			Fmt.nSamplesPerSec = (DWORD)Rate;
			Fmt.wBitsPerSample = 16;
			Fmt.nBlockAlign = 4;
			Fmt.nAvgBytesPerSec = (DWORD)(Rate * 4);
			if(SUCCEEDED(pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, &Fmt, nullptr)))
			{
				SrcSampleRate = Rate;
				Inited = true;
				break;
			}
		}
		if(!Inited)
		{
			CleanupCapture();
			return false;
		}
		if(FAILED(pAudioClient->GetService(IID_PPV_ARGS(pCaptureClient.put()))) || !pCaptureClient)
		{
			CleanupCapture();
			return false;
		}
		if(FAILED(pAudioClient->Start()))
		{
			CleanupCapture();
			return false;
		}
		ActivePid = Pid;
		m_CaptureStatus.store(CAPTURE_ACTIVE, std::memory_order_relaxed);
		return true;
	};

	while(!m_CaptureStop.load(std::memory_order_acquire))
	{
		const auto Now = std::chrono::steady_clock::now();
		if(!pCaptureClient || (Now - LastScan) >= std::chrono::milliseconds(900))
		{
			LastScan = Now;
			const DWORD TargetPid = FindActiveMusicProcessId();
			if(TargetPid == 0)
			{
				if(pCaptureClient)
					CleanupCapture();
				m_CaptureStatus.store(CAPTURE_SEARCHING, std::memory_order_relaxed);
			}
			else if(TargetPid != ActivePid || !pCaptureClient)
			{
				if(!StartPidCapture(TargetPid))
					m_CaptureStatus.store(CAPTURE_SEARCHING, std::memory_order_relaxed);
			}
		}

		if(!pCaptureClient)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(40));
			continue;
		}

		UINT32 PacketFrames = 0;
		HRESULT Hr = pCaptureClient->GetNextPacketSize(&PacketFrames);
		if(FAILED(Hr))
		{
			CleanupCapture();
			m_CaptureStatus.store(CAPTURE_SEARCHING, std::memory_order_relaxed);
			continue;
		}

		if(PacketFrames == 0)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			continue;
		}

		while(PacketFrames > 0 && !m_CaptureStop.load(std::memory_order_acquire))
		{
			BYTE *pData = nullptr;
			UINT32 NumFrames = 0;
			DWORD Flags = 0;
			Hr = pCaptureClient->GetBuffer(&pData, &NumFrames, &Flags, nullptr, nullptr);
			if(FAILED(Hr))
			{
				CleanupCapture();
				break;
			}

			if(NumFrames > 0)
			{
				if((Flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || !pData)
				{
					// Push silence only if needed or skip; skipping avoids unnecessary work
					LastL = 0;
					LastR = 0;
				}
				else if(SrcSampleRate == SAMPLE_RATE)
				{
					PushCaptureSamples(reinterpret_cast<const int16_t *>(pData), NumFrames * 2U);
					const int16_t *pS = reinterpret_cast<const int16_t *>(pData);
					LastL = pS[(NumFrames - 1) * 2];
					LastR = pS[(NumFrames - 1) * 2 + 1];
				}
				else
				{
					// Linear resample 44100 -> 48000 stereo without heap churn
					const int16_t *pIn = reinterpret_cast<const int16_t *>(pData);
					const double Step = (double)SrcSampleRate / (double)SAMPLE_RATE;
					vResampleBuf.clear();
					while(ResamplePhase < (double)NumFrames)
					{
						const int Idx = (int)std::floor(ResamplePhase);
						const float Frac = (float)(ResamplePhase - (double)Idx);
						const int16_t L0 = (Idx < 0) ? LastL : pIn[Idx * 2];
						const int16_t R0 = (Idx < 0) ? LastR : pIn[Idx * 2 + 1];
						const int NextIdx = std::min(Idx + 1, (int)NumFrames - 1);
						const int16_t L1 = pIn[NextIdx * 2];
						const int16_t R1 = pIn[NextIdx * 2 + 1];
						vResampleBuf.push_back((int16_t)std::clamp((int)std::lround((float)L0 + ((float)L1 - (float)L0) * Frac), -32768, 32767));
						vResampleBuf.push_back((int16_t)std::clamp((int)std::lround((float)R0 + ((float)R1 - (float)R0) * Frac), -32768, 32767));
						ResamplePhase += Step;
					}
					ResamplePhase -= (double)NumFrames;
					LastL = pIn[(NumFrames - 1) * 2];
					LastR = pIn[(NumFrames - 1) * 2 + 1];
					if(!vResampleBuf.empty())
						PushCaptureSamples(vResampleBuf.data(), (uint32_t)vResampleBuf.size());
				}
			}

			pCaptureClient->ReleaseBuffer(NumFrames);
			if(FAILED(pCaptureClient->GetNextPacketSize(&PacketFrames)))
			{
				CleanupCapture();
				break;
			}
		}
	}

	CleanupCapture();
	if(ComInited)
		CoUninitialize();
#else
	m_CaptureStatus.store(CAPTURE_UNSUPPORTED, std::memory_order_relaxed);
#endif
}

void CAClientMusicShare::PumpHostAudio(int64_t Now)
{
	if(m_State != STATE_HOSTING || !m_pEncoder || !m_Socket || !m_RelayAddrValid || m_RoomHash == 0)
		return;

	int16_t aFrame[FRAME_INT16];
	uint8_t aPacket[HEADER_LEN + 4 + MAX_OPUS_BYTES];
	int FramesSent = 0;
	const uint32_t SenderId = (m_SessionToken << 8) | 1U;

	while(FramesSent < 4)
	{
		const uint32_t W = m_CapWriteIdx.load(std::memory_order_acquire);
		const uint32_t R = m_CapReadIdx.load(std::memory_order_relaxed);
		const uint32_t Avail = W - R;
		if(Avail < (uint32_t)FRAME_INT16)
			break;

		// Drop excess backlog if main thread stalled
		uint32_t ReadIdx = R;
		if(Avail > (uint32_t)(FRAME_INT16 * 8))
			ReadIdx = W - (uint32_t)(FRAME_INT16 * 3);

		const uint32_t Start = ReadIdx & CAPTURE_RING_MASK;
		const uint32_t First = std::min<uint32_t>(FRAME_INT16, CAPTURE_RING_CAPACITY - Start);
		std::memcpy(aFrame, m_aCaptureRing.data() + Start, First * sizeof(int16_t));
		if(First < (uint32_t)FRAME_INT16)
			std::memcpy(aFrame + First, m_aCaptureRing.data(), (FRAME_INT16 - First) * sizeof(int16_t));
		m_CapReadIdx.store(ReadIdx + FRAME_INT16, std::memory_order_release);
		++FramesSent;

		// DTX / Silence gate: save bandwidth (0 B/s audio) when track is silent/paused > 1.5s
		double SumSq = 0.0;
		for(int i = 0; i < FRAME_INT16; i += 4)
		{
			const float s = (float)aFrame[i] / 32768.0f;
			SumSq += (double)(s * s);
		}
		const float Rms = std::sqrt((float)(SumSq / (double)(FRAME_INT16 / 4)));
		if(Rms >= 0.0004f)
			m_LastAudibleTime = Now;
		if((Now - m_LastAudibleTime) > (time_freq() * 3) / 2)
			continue;

		const int Encoded = opus_encode(m_pEncoder, aFrame, FRAME_SAMPLES, aPacket + HEADER_LEN + 4, MAX_OPUS_BYTES);
		if(Encoded <= 1)
			continue;

		WriteHeader(aPacket, PKT_VOICE, m_RoomHash, SenderId, m_Seq++);
		aPacket[HEADER_LEN + 0] = 'A';
		aPacket[HEADER_LEN + 1] = 'C';
		aPacket[HEADER_LEN + 2] = 'M';
		aPacket[HEADER_LEN + 3] = 'S';
		net_udp_send(m_Socket, &m_RelayAddr, aPacket, HEADER_LEN + 4 + Encoded);
	}
}

void CAClientMusicShare::UpdateListenerVisualizer(const int16_t *pStereoPcm, int FrameCount)
{
	if(!pStereoPcm || FrameCount <= 0)
		return;

	double LowSq = 0.0;
	double MidSq = 0.0;
	double HighSq = 0.0;
	for(int i = 0; i < FrameCount; i += 2)
	{
		const float Mono = ((float)pStereoPcm[i * 2] + (float)pStereoPcm[i * 2 + 1]) * (0.5f / 32768.0f);
		m_VisLowLp += 0.045f * (Mono - m_VisLowLp);
		m_VisMidLp += 0.24f * (Mono - m_VisMidLp);
		const float Low = m_VisLowLp;
		const float Mid = m_VisMidLp - m_VisLowLp;
		const float High = Mono - m_VisMidLp;
		LowSq += (double)(Low * Low);
		MidSq += (double)(Mid * Mid);
		HighSq += (double)(High * High);
	}
	const double InvN = 1.0 / (double)std::max(1, FrameCount / 2);
	const float LowLvl = std::clamp(std::sqrt((float)(LowSq * InvN)) * 4.5f, 0.0f, 1.0f);
	const float MidLvl = std::clamp(std::sqrt((float)(MidSq * InvN)) * 5.2f, 0.0f, 1.0f);
	const float HighLvl = std::clamp(std::sqrt((float)(HighSq * InvN)) * 6.0f, 0.0f, 1.0f);

	for(int b = 0; b < VIS_BARS; ++b)
	{
		const float t = (float)b / (float)(VIS_BARS - 1);
		float Target = 0.0f;
		if(t < 0.5f)
		{
			const float u = t * 2.0f;
			Target = LowLvl * (1.0f - u) + MidLvl * u;
		}
		else
		{
			const float u = (t - 0.5f) * 2.0f;
			Target = MidLvl * (1.0f - u) + HighLvl * u;
		}
		const float Prev = m_aVisBars[b];
		m_aVisBars[b] = (Target > Prev) ? (Prev * 0.35f + Target * 0.65f) : (Prev * 0.80f + Target * 0.20f);
	}
}

void CAClientMusicShare::PollIncomingPackets(int64_t Now)
{
	if(!m_Socket || m_RoomHash == 0)
		return;

	NETADDR Addr;
	unsigned char *pData = nullptr;
	int Bytes = 0;
	int ReadCount = 0;

	while(ReadCount < 64 && (Bytes = net_udp_recv(m_Socket, &Addr, &pData)) > 0)
	{
		++ReadCount;
		if(Bytes < HEADER_LEN)
			continue;
		if(pData[0] != 'A' || pData[1] != 'C' || pData[2] != 'V' || pData[3] != '1')
			continue;

		const uint8_t PktType = pData[4];
		const uint64_t RoomHash = ReadU64LE(pData + 5);
		const uint32_t SenderId = ReadU32LE(pData + 13);
		const uint32_t Seq = ReadU32LE(pData + 17);
		if(RoomHash != m_RoomHash)
			continue;

		if(PktType == PKT_ROOM_PEERS && Bytes >= HEADER_LEN + 1)
		{
			const int Count = pData[HEADER_LEN];
			int OtherPeers = 0;
			const int Expected = HEADER_LEN + 1 + Count * (4 + NAME_FIELD_LEN);
			if(Count >= 0 && Bytes >= Expected)
			{
				for(int p = 0; p < Count; ++p)
				{
					const uint32_t PeerSid = ReadU32LE(pData + HEADER_LEN + 1 + p * (4 + NAME_FIELD_LEN));
					if((PeerSid >> 8) != m_SessionToken)
						++OtherPeers;
				}
			}
			if(OtherPeers > 0)
			{
				m_PeerSeenOnRelay = true;
				m_LastPeerActivityAt = Now;
			}
			else if(m_PeerSeenOnRelay && m_State == STATE_HOSTING)
			{
				// Listener disconnected from relay
				if(GameClient())
				{
					char aEcho[160];
					str_format(aEcho, sizeof(aEcho), "[AClient Music] '%s' se desconecto de la musica compartida", m_aPeerName);
					GameClient()->Echo(aEcho);
				}
				StopSession(false);
				return;
			}
			continue;
		}

		if((SenderId >> 8) == m_SessionToken)
			continue;

		if(PktType != PKT_VOICE || Bytes < HEADER_LEN + 4)
			continue;

		m_PeerSeenOnRelay = true;
		m_LastPeerActivityAt = Now;

		if(m_State != STATE_LISTENING)
			continue;

		// Metadata packet ("ACMT")
		if(Bytes >= META_PACKET_LEN && std::memcmp(pData + HEADER_LEN, "ACMT", 4) == 0)
		{
			int Off = HEADER_LEN + 4;
			ReadFixedStr(m_RemoteTrack.m_aTitle, sizeof(m_RemoteTrack.m_aTitle), pData + Off, META_TITLE_LEN);
			Off += META_TITLE_LEN;
			ReadFixedStr(m_RemoteTrack.m_aArtist, sizeof(m_RemoteTrack.m_aArtist), pData + Off, META_ARTIST_LEN);
			Off += META_ARTIST_LEN;
			m_RemoteTrack.m_Playing = pData[Off++] != 0;
			m_RemoteTrack.m_PositionMs = (int64_t)ReadU32LE(pData + Off);
			Off += 4;
			m_RemoteTrack.m_DurationMs = (int64_t)ReadU32LE(pData + Off);
			m_RemoteTrack.m_UpdatedAt = Now;
			continue;
		}

		// Stereo Opus audio packet ("ACMS")
		if(Bytes > HEADER_LEN + 4 && std::memcmp(pData + HEADER_LEN, "ACMS", 4) == 0 && m_pDecoder)
		{
			const int OpusLen = Bytes - (HEADER_LEN + 4);
			if(OpusLen <= 0 || OpusLen > MAX_OPUS_BYTES)
				continue;

			if(m_LastRemoteSenderId != SenderId)
			{
				opus_decoder_ctl(m_pDecoder, OPUS_RESET_STATE);
				m_PlayReadPos = 0;
				m_PlayWritePos = 0;
				m_PlayBufferedSamples = 0;
				m_PlayPrimed = false;
				m_LastRemoteSenderId = SenderId;
				m_HasRemoteSeq = false;
			}

			if(m_HasRemoteSeq && (int32_t)(Seq - m_LastRemoteSeq) <= 0)
				continue;

			auto AppendStereo = [&](const int16_t *pSamples, int StereoFrames) {
				const uint32_t Count = (uint32_t)(StereoFrames * CHANNELS);
				if(Count == 0)
					return;
				if(m_PlayBufferedSamples + Count > (uint32_t)PLAY_RING_SAMPLES)
				{
					const uint32_t Excess = (m_PlayBufferedSamples + Count) - (uint32_t)PLAY_RING_SAMPLES;
					m_PlayReadPos = (m_PlayReadPos + Excess) % (uint32_t)PLAY_RING_SAMPLES;
					m_PlayBufferedSamples -= Excess;
				}
				const uint32_t First = std::min(Count, (uint32_t)PLAY_RING_SAMPLES - m_PlayWritePos);
				std::memcpy(m_aPlayRing.data() + m_PlayWritePos, pSamples, First * sizeof(int16_t));
				if(First < Count)
					std::memcpy(m_aPlayRing.data(), pSamples + First, (Count - First) * sizeof(int16_t));
				m_PlayWritePos = (m_PlayWritePos + Count) % (uint32_t)PLAY_RING_SAMPLES;
				m_PlayBufferedSamples += Count;
			};

			int16_t aDecoded[FRAME_INT16];
			if(m_HasRemoteSeq && Seq > m_LastRemoteSeq + 1)
			{
				const uint32_t Lost = std::min<uint32_t>(Seq - m_LastRemoteSeq - 1, 2);
				for(uint32_t k = 0; k < Lost; ++k)
				{
					const int Plc = opus_decode(m_pDecoder, nullptr, 0, aDecoded, FRAME_SAMPLES, 0);
					if(Plc > 0)
						AppendStereo(aDecoded, Plc);
				}
			}

			const int Decoded = opus_decode(m_pDecoder, pData + HEADER_LEN + 4, OpusLen, aDecoded, FRAME_SAMPLES, 0);
			if(Decoded > 0)
			{
				AppendStereo(aDecoded, Decoded);
				m_LastRemoteSeq = Seq;
				m_HasRemoteSeq = true;
			}
		}
	}
}

void CAClientMusicShare::PumpListenerAudio(int64_t Now)
{
	if(m_State != STATE_LISTENING || m_PlaybackDevice == 0)
		return;

	const uint32_t FrameBytes = (uint32_t)(FRAME_INT16 * sizeof(int16_t));
	uint32_t QueuedPlay = SDL_GetQueuedAudioSize((SDL_AudioDeviceID)m_PlaybackDevice);

	// Smooth 100 ms pre-buffer on initial start or after underrun
	if(!m_PlayPrimed)
	{
		if(m_PlayBufferedSamples < (uint32_t)(FRAME_INT16 * 5))
		{
			for(float &Bar : m_aVisBars)
				Bar *= 0.90f;
			return;
		}
		m_PlayPrimed = true;
		m_NextMixTime = Now;
	}

	if(QueuedPlay > FrameBytes * 10)
		return;

	const float VolScale = m_Muted ? 0.0f : std::clamp(g_Config.m_AcMusicShareVolume / 100.0f, 0.0f, 2.0f);
	const int64_t FrameTicks = time_freq() / 50; // 20 ms
	int FramesMixed = 0;

	while(FramesMixed < 4 && m_PlayBufferedSamples >= (uint32_t)FRAME_INT16 && QueuedPlay < FrameBytes * 7)
	{
		if(Now < m_NextMixTime && QueuedPlay >= FrameBytes * 3)
			break;

		int16_t aOut[FRAME_INT16];
		const uint32_t First = std::min<uint32_t>(FRAME_INT16, (uint32_t)PLAY_RING_SAMPLES - m_PlayReadPos);
		std::memcpy(aOut, m_aPlayRing.data() + m_PlayReadPos, First * sizeof(int16_t));
		if(First < (uint32_t)FRAME_INT16)
			std::memcpy(aOut + First, m_aPlayRing.data(), (FRAME_INT16 - First) * sizeof(int16_t));
		m_PlayReadPos = (m_PlayReadPos + FRAME_INT16) % (uint32_t)PLAY_RING_SAMPLES;
		m_PlayBufferedSamples -= FRAME_INT16;

		UpdateListenerVisualizer(aOut, FRAME_SAMPLES);

		if(VolScale <= 0.001f)
		{
			std::memset(aOut, 0, sizeof(aOut));
		}
		else if(std::fabs(VolScale - 1.0f) > 0.005f)
		{
			for(int i = 0; i < FRAME_INT16; ++i)
			{
				const int Scaled = (int)std::lround((float)aOut[i] * VolScale);
				aOut[i] = (int16_t)std::clamp(Scaled, -32768, 32767);
			}
		}

		SDL_QueueAudio((SDL_AudioDeviceID)m_PlaybackDevice, aOut, FrameBytes);
		QueuedPlay += FrameBytes;
		m_NextMixTime = std::max(m_NextMixTime + FrameTicks, Now);
		++FramesMixed;
	}

	if(m_PlayBufferedSamples < (uint32_t)FRAME_INT16 && QueuedPlay == 0)
	{
		m_PlayPrimed = false;
		for(float &Bar : m_aVisBars)
			Bar *= 0.88f;
	}
}

void CAClientMusicShare::OnUpdate()
{
	if(m_State == STATE_IDLE)
		return; // O(1), 0% CPU, 0 B/s when idle

	const int64_t Now = time_get();
	const int64_t Freq = time_freq();

	if(m_State == STATE_WAITING_ACCEPT)
	{
		if((Now - m_StateStartedAt) >= Freq * 120)
		{
			if(GameClient())
			{
				char aEcho[160];
				str_format(aEcho, sizeof(aEcho), "[AClient Music] La invitacion a '%s' expiro", m_aPeerName);
				GameClient()->Echo(aEcho);
			}
			StopSession(false);
		}
		return;
	}

	// 5s steady-state keepalive (3x safety margin vs 15s relay timeout)
	if((Now - m_LastKeepaliveTime) >= Freq * 5)
	{
		SendControlPacket(PKT_KEEPALIVE, m_RoomHash);
		m_LastKeepaliveTime = Now;
	}

	PollIncomingPackets(Now);
	if(m_State == STATE_IDLE)
		return;

	if(m_State == STATE_HOSTING)
	{
		SendMetadataPacket(false);
		PumpHostAudio(Now);
		// Auto-stop if listener never connected within 20s or timed out
		const int64_t Timeout = m_PeerSeenOnRelay ? (Freq * 16) : (Freq * 20);
		if((Now - m_LastPeerActivityAt) >= Timeout)
		{
			if(GameClient())
			{
				char aEcho[160];
				str_format(aEcho, sizeof(aEcho), "[AClient Music] '%s' se desconecto, transmision detenida", m_aPeerName);
				GameClient()->Echo(aEcho);
			}
			StopSession(false);
		}
	}
	else if(m_State == STATE_LISTENING)
	{
		PumpListenerAudio(Now);
		const int64_t Timeout = m_PeerSeenOnRelay ? (Freq * 16) : (Freq * 20);
		if((Now - m_LastPeerActivityAt) >= Timeout)
		{
			if(GameClient())
			{
				char aEcho[160];
				str_format(aEcho, sizeof(aEcho), "[AClient Music] Se perdio la conexion de musica con '%s'", m_aPeerName);
				GameClient()->Echo(aEcho);
			}
			StopSession(false);
		}
	}
}

void CAClientMusicShare::OnStateChange(int NewState, int OldState)
{
	if(NewState != IClient::STATE_ONLINE && m_State == STATE_WAITING_ACCEPT)
		StopSession(false);
}

void CAClientMusicShare::OnShutdown()
{
	StopSession(false);
	if(m_SdlAudioInit)
	{
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		m_SdlAudioInit = false;
	}
}
