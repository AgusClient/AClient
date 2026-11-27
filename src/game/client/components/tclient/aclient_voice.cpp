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
#include <engine/shared/protocol7.h>
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
#include <cctype>
#include <cmath>
#include <cstring>

namespace
{
	constexpr uint8_t PKT_KEEPALIVE = 1;
	constexpr uint8_t PKT_VOICE = 2;
	constexpr uint8_t PKT_LEAVE = 3;
	constexpr uint8_t PKT_ROOM_PEERS = 4;
	constexpr uint8_t PKT_INVITE = 5;
	constexpr uint8_t PKT_MOD_MUTE = 6;
	constexpr uint8_t PKT_MOD_ACK = 7;

	constexpr int VOICE_DEFAULT_PORT = 9987;
	constexpr int NAME_FIELD_LEN = 16;
	constexpr int ROOM_CODE_FIELD_LEN = 32;
	constexpr int KEEPALIVE_WITH_NAME_LEN = CAClientVoice::HEADER_LEN + NAME_FIELD_LEN; // 37
	constexpr int NAME_BEACON_LEN = CAClientVoice::HEADER_LEN + CAClientVoice::MAX_OPUS_BYTES + 1; // 534 (> MAX_OPUS_BYTES so legacy clients ignore Opus decode)
	constexpr int INVITE_LEN = CAClientVoice::HEADER_LEN + NAME_FIELD_LEN + NAME_FIELD_LEN + ROOM_CODE_FIELD_LEN; // 85
	constexpr int MOD_MUTE_LEN = CAClientVoice::HEADER_LEN + 32 + 4 + 4 + NAME_FIELD_LEN; // 77
	constexpr int MOD_ACK_LEN = CAClientVoice::HEADER_LEN + 1 + 4 + NAME_FIELD_LEN; // 42
	constexpr uint64_t CUSTOM_ROOM_HASH_TAG = 0x7FFFFF;

	// SHA-256 of default moderator key ("AClientMod2026")
	constexpr unsigned char EXPECTED_MOD_KEY_SHA256[SHA256_DIGEST_LENGTH] = {
		0xfa, 0xbb, 0x46, 0x3b, 0xd0, 0x0b, 0x77, 0x25, 0x89, 0xd8, 0x6e, 0x8d, 0x8a, 0xd5, 0x42, 0x4f,
		0x2f, 0x19, 0x8e, 0x1b, 0x7c, 0xac, 0xe6, 0x42, 0xfb, 0x7d, 0xe7, 0x4a, 0x3a, 0x85, 0xff, 0xd3,
	};

	inline void WriteU32LE(uint8_t *pDst, uint32_t v)
	{
		pDst[0] = (uint8_t)(v & 0xFF);
		pDst[1] = (uint8_t)((v >> 8) & 0xFF);
		pDst[2] = (uint8_t)((v >> 16) & 0xFF);
		pDst[3] = (uint8_t)((v >> 24) & 0xFF);
	}

	inline void WriteI32LE(uint8_t *pDst, int32_t v)
	{
		WriteU32LE(pDst, (uint32_t)v);
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

	inline int32_t ReadI32LE(const uint8_t *pSrc)
	{
		return (int32_t)ReadU32LE(pSrc);
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
		const size_t SrcLen = std::strlen(pSrc);
		const size_t CopyLen = std::min(SrcLen, FieldLen - 1);
		std::memcpy(pDst, pSrc, CopyLen);
	}

	inline void ReadFixedStr(char *pDst, size_t DstSize, const uint8_t *pSrc, size_t FieldLen)
	{
		if(DstSize == 0)
			return;
		size_t ActualLen = 0;
		while(ActualLen < FieldLen && pSrc[ActualLen] != '\0')
			++ActualLen;
		const size_t CopyLen = std::min(ActualLen, DstSize - 1);
		if(CopyLen > 0)
			std::memcpy(pDst, pSrc, CopyLen);
		pDst[CopyLen] = '\0';
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

	std::string NormalizeNameKey(const char *pName)
	{
		std::string Key = TrimCopy(pName);
		for(char &Ch : Key)
			Ch = (char)std::tolower((unsigned char)Ch);
		return Key;
	}

	bool IsGenericPlaceholderName(const char *pName)
	{
		const std::string Key = NormalizeNameKey(pName);
		if(Key.empty() || Key == "amigo" || Key == "jugador" || Key == "alguien")
			return true;
		if(Key.rfind("amigo #", 0) == 0 || Key.rfind("jugador #", 0) == 0)
			return true;
		return false;
	}

	std::string EscapePrefField(const char *pStr)
	{
		std::string Out;
		if(!pStr)
			return Out;
		for(const unsigned char *p = (const unsigned char *)pStr; *p; ++p)
		{
			if(*p == '%')
				Out += "%25";
			else if(*p == '=')
				Out += "%3D";
			else if(*p == ':')
				Out += "%3A";
			else if(*p == ';')
				Out += "%3B";
			else
				Out.push_back((char)*p);
		}
		return Out;
	}

	int HexVal(char Ch)
	{
		if(Ch >= '0' && Ch <= '9')
			return Ch - '0';
		if(Ch >= 'A' && Ch <= 'F')
			return 10 + (Ch - 'A');
		if(Ch >= 'a' && Ch <= 'f')
			return 10 + (Ch - 'a');
		return -1;
	}

	std::string UnescapePrefField(const std::string &Str)
	{
		std::string Out;
		Out.reserve(Str.size());
		for(size_t i = 0; i < Str.size(); ++i)
		{
			if(Str[i] == '%' && i + 2 < Str.size())
			{
				const int Hi = HexVal(Str[i + 1]);
				const int Lo = HexVal(Str[i + 2]);
				if(Hi >= 0 && Lo >= 0)
				{
					Out.push_back((char)((Hi << 4) | Lo));
					i += 2;
					continue;
				}
			}
			Out.push_back(Str[i]);
		}
		return Out;
	}

	bool CheckModKeyHash(const char *pKey)
	{
		if(!pKey || !pKey[0])
			return false;
		const SHA256_DIGEST Digest = sha256(pKey, std::strlen(pKey));
		return std::memcmp(Digest.data, EXPECTED_MOD_KEY_SHA256, SHA256_DIGEST_LENGTH) == 0;
	}
} // namespace

void CAClientVoice::OnInit()
{
	uint64_t Seed = (uint64_t)time_get() ^ ((uint64_t)(uintptr_t)this * 0x9E3779B97F4A7C15ULL);
	Seed ^= (Seed >> 30);
	m_SessionToken = (uint32_t)((Seed & 0x00FFFFFFU) | 1U); // 24-bit non-zero session token
	std::fill(std::begin(m_aPeerVolume), std::end(m_aPeerVolume), 100);
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		m_aaLastSlotName[i][0] = '\0';
		m_aaRoomPeerName[i][0] = '\0';
	}
	LoadPlayerPrefsFromConfig();
	if(g_Config.m_AcVoiceModKey[0] != '\0')
	{
		const std::string Clean = TrimCopy(g_Config.m_AcVoiceModKey);
		if(CheckModKeyHash(Clean.c_str()))
		{
			m_ModKeyVerified = true;
			str_copy(m_aModStatusMsg, "Clave de moderador activa (guardada en config)");
		}
	}
}

void CAClientVoice::OnConsoleInit()
{
	Console()->Register("+ac_voice_ptt", "", CFGFLAG_CLIENT, ConPtt, this, "Hold to talk in Team Voice Chat");
	Console()->Register("ac_voice_mute", "i[client_id]", CFGFLAG_CLIENT, ConMute, this, "Toggle mute for a player in Voice Chat");
	Console()->Register("ac_voice_toggle", "", CFGFLAG_CLIENT, ConToggle, this, "Toggle Team Voice Chat on or off");
	Console()->Register("ac_voice_room_create", "?s[code] ?s[password]", CFGFLAG_CLIENT, ConRoomCreate, this, "Create and join a private voice chat room (optional password)");
	Console()->Register("ac_voice_room_join", "s[code] ?s[password]", CFGFLAG_CLIENT, ConRoomJoin, this, "Join a private voice chat room by code (optional password)");
	Console()->Register("ac_voice_room_leave", "", CFGFLAG_CLIENT, ConRoomLeave, this, "Leave current private voice chat room");
	Console()->Register("ac_voice_invite", "s[player_name]", CFGFLAG_CLIENT, ConInvite, this, "Invite a friend to your private voice chat room");
	Console()->Register("ac_voice_accept", "", CFGFLAG_CLIENT, ConAcceptInvite, this, "Accept pending private voice chat room invite");
	Console()->Register("ac_voice_decline", "", CFGFLAG_CLIENT, ConDeclineInvite, this, "Decline pending private voice chat room invite");
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
	const char *pName = pSelf->GetSlotEffectiveName(ClientId);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s %s", (pName && pName[0]) ? pName : "Jugador", pSelf->IsClientMuted(ClientId) ? "muteado" : "desmuteado");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "ac_voice", aBuf);
}

void CAClientVoice::ConRoomCreate(IConsole::IResult *pResult, void *pUserData)
{
	CAClientVoice *pSelf = static_cast<CAClientVoice *>(pUserData);
	const char *pCode = pResult->NumArguments() > 0 ? pResult->GetString(0) : nullptr;
	const char *pPass = pResult->NumArguments() > 1 ? pResult->GetString(1) : nullptr;
	pSelf->CreateCustomRoom(pCode, pPass);
}

void CAClientVoice::ConRoomJoin(IConsole::IResult *pResult, void *pUserData)
{
	CAClientVoice *pSelf = static_cast<CAClientVoice *>(pUserData);
	const char *pPass = pResult->NumArguments() > 1 ? pResult->GetString(1) : nullptr;
	pSelf->JoinCustomRoom(pResult->GetString(0), pPass);
}

void CAClientVoice::ConRoomLeave(IConsole::IResult *pResult, void *pUserData)
{
	CAClientVoice *pSelf = static_cast<CAClientVoice *>(pUserData);
	pSelf->LeaveCustomRoom();
}

void CAClientVoice::ConInvite(IConsole::IResult *pResult, void *pUserData)
{
	CAClientVoice *pSelf = static_cast<CAClientVoice *>(pUserData);
	pSelf->SendRoomInvite(pResult->GetString(0));
}

void CAClientVoice::ConAcceptInvite(IConsole::IResult *pResult, void *pUserData)
{
	CAClientVoice *pSelf = static_cast<CAClientVoice *>(pUserData);
	pSelf->AcceptPendingInvite();
}

void CAClientVoice::ConDeclineInvite(IConsole::IResult *pResult, void *pUserData)
{
	CAClientVoice *pSelf = static_cast<CAClientVoice *>(pUserData);
	pSelf->DeclinePendingInvite();
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
	char aTeamBuf[32];
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

uint64_t CAClientVoice::ComputeCustomRoomHash(const char *pRoomCode, const char *pPassword)
{
	if(!pPassword || pPassword[0] == '\0')
		return ComputeRoomHash(pRoomCode, CUSTOM_ROOM_HASH_TAG);
	char aCombined[80];
	str_format(aCombined, sizeof(aCombined), "%s\n%s", pRoomCode ? pRoomCode : "", pPassword);
	return ComputeRoomHash(aCombined, CUSTOM_ROOM_HASH_TAG);
}

const char *CAClientVoice::GetLocalVoiceName() const
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
	return "";
}

const char *CAClientVoice::GetSlotEffectiveName(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return "";
	if(IsInCustomRoom())
		return m_aaRoomPeerName[ClientId];
	if(GameClient() && GameClient()->m_aClients[ClientId].m_Active)
		return GameClient()->m_aClients[ClientId].m_aName;
	return "";
}

void CAClientVoice::LoadPlayerPrefsFromConfig()
{
	m_mSavedPeerPrefs.clear();
	const std::string Raw(g_Config.m_AcVoiceSavedVolumes);
	size_t Pos = 0;
	while(Pos < Raw.size())
	{
		size_t Semi = Raw.find(';', Pos);
		if(Semi == std::string::npos)
			Semi = Raw.size();
		const std::string Item = Raw.substr(Pos, Semi - Pos);
		Pos = Semi + 1;

		const size_t Eq = Item.find('=');
		if(Eq == std::string::npos || Eq == 0)
			continue;

		const std::string Name = UnescapePrefField(Item.substr(0, Eq));
		const std::string Key = NormalizeNameKey(Name.c_str());
		if(Key.empty() || IsGenericPlaceholderName(Name.c_str()))
			continue;

		const std::string ValPart = Item.substr(Eq + 1);
		const size_t Colon = ValPart.find(':');
		int Vol = 100;
		bool Muted = false;
		if(Colon == std::string::npos)
		{
			Vol = std::clamp(str_toint(ValPart.c_str()), 0, 200);
		}
		else
		{
			Vol = std::clamp(str_toint(ValPart.substr(0, Colon).c_str()), 0, 200);
			Muted = str_toint(ValPart.substr(Colon + 1).c_str()) != 0;
		}

		if(Vol != 100 || Muted)
		{
			SPeerSavedPref Pref;
			Pref.m_Volume = Vol;
			Pref.m_Muted = Muted;
			str_copy(Pref.m_aOriginalName, Name.c_str());
			m_mSavedPeerPrefs[Key] = Pref;
		}
	}
}

void CAClientVoice::SavePlayerPrefsToConfig()
{
	std::string Out;
	for(const auto &[Key, Pref] : m_mSavedPeerPrefs)
	{
		if(Key.empty() || (Pref.m_Volume == 100 && !Pref.m_Muted))
			continue;

		const char *pNameToWrite = Pref.m_aOriginalName[0] ? Pref.m_aOriginalName : Key.c_str();
		char aEntry[192];
		str_format(aEntry, sizeof(aEntry), "%s=%d:%d;", EscapePrefField(pNameToWrite).c_str(), Pref.m_Volume, Pref.m_Muted ? 1 : 0);
		if(Out.size() + std::strlen(aEntry) >= sizeof(g_Config.m_AcVoiceSavedVolumes))
			break;
		Out += aEntry;
	}
	str_copy(g_Config.m_AcVoiceSavedVolumes, Out.c_str());
}

int CAClientVoice::GetPlayerVolumeByName(const char *pName) const
{
	if(IsGenericPlaceholderName(pName))
		return 100;
	const std::string Key = NormalizeNameKey(pName);
	if(Key.empty())
		return 100;
	auto It = m_mSavedPeerPrefs.find(Key);
	return It != m_mSavedPeerPrefs.end() ? It->second.m_Volume : 100;
}

bool CAClientVoice::IsPlayerMutedByName(const char *pName) const
{
	if(IsGenericPlaceholderName(pName))
		return false;
	const std::string Key = NormalizeNameKey(pName);
	if(Key.empty())
		return false;
	auto It = m_mSavedPeerPrefs.find(Key);
	return It != m_mSavedPeerPrefs.end() ? It->second.m_Muted : false;
}

void CAClientVoice::SetPlayerVolumeByName(const char *pName, int Volume)
{
	if(IsGenericPlaceholderName(pName))
		return;
	const std::string Key = NormalizeNameKey(pName);
	if(Key.empty())
		return;
	const int Clamped = std::clamp(Volume, 0, 200);
	auto &Pref = m_mSavedPeerPrefs[Key];
	Pref.m_Volume = Clamped;
	str_copy(Pref.m_aOriginalName, TrimCopy(pName).c_str());
	if(Pref.m_Volume == 100 && !Pref.m_Muted)
		m_mSavedPeerPrefs.erase(Key);
	SavePlayerPrefsToConfig();
}

void CAClientVoice::SetPlayerMutedByName(const char *pName, bool Muted)
{
	if(IsGenericPlaceholderName(pName))
		return;
	const std::string Key = NormalizeNameKey(pName);
	if(Key.empty())
		return;
	auto &Pref = m_mSavedPeerPrefs[Key];
	Pref.m_Muted = Muted;
	str_copy(Pref.m_aOriginalName, TrimCopy(pName).c_str());
	if(Pref.m_Volume == 100 && !Pref.m_Muted)
		m_mSavedPeerPrefs.erase(Key);
	SavePlayerPrefsToConfig();
}

void CAClientVoice::SyncPlayerVolumesByName()
{
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		const char *pEffName = GetSlotEffectiveName(i);
		if(str_comp(m_aaLastSlotName[i], pEffName) != 0)
		{
			str_copy(m_aaLastSlotName[i], pEffName);
			if(pEffName[0] != '\0')
			{
				if(!IsGenericPlaceholderName(pEffName))
				{
					m_aPeerVolume[i] = GetPlayerVolumeByName(pEffName);
					m_aMuted[i] = IsPlayerMutedByName(pEffName);
				}
			}
			else
			{
				m_aPeerVolume[i] = 100;
				m_aMuted[i] = false;
			}
		}
	}
}

void CAClientVoice::SetClientMuted(int ClientId, bool Muted)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	m_aMuted[ClientId] = Muted;
	const char *pName = GetSlotEffectiveName(ClientId);
	if(pName && pName[0] != '\0' && !IsGenericPlaceholderName(pName))
	{
		SetPlayerMutedByName(pName, Muted);
		const std::string Key = NormalizeNameKey(pName);
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			const char *pOther = GetSlotEffectiveName(i);
			if(pOther && pOther[0] != '\0' && !IsGenericPlaceholderName(pOther) && NormalizeNameKey(pOther) == Key)
				m_aMuted[i] = Muted;
		}
	}
}

void CAClientVoice::ToggleClientMute(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	SetClientMuted(ClientId, !m_aMuted[ClientId]);
}

void CAClientVoice::SetClientVolume(int ClientId, int Volume)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	const int Clamped = std::clamp(Volume, 0, 200);
	m_aPeerVolume[ClientId] = Clamped;
	NotifyClientVolumeChanged(ClientId);
}

void CAClientVoice::NotifyClientVolumeChanged(int ClientId)
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;
	m_aPeerVolume[ClientId] = std::clamp(m_aPeerVolume[ClientId], 0, 200);
	const char *pName = GetSlotEffectiveName(ClientId);
	if(pName && pName[0] != '\0' && !IsGenericPlaceholderName(pName))
	{
		SetPlayerVolumeByName(pName, m_aPeerVolume[ClientId]);
		const std::string Key = NormalizeNameKey(pName);
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			const char *pOther = GetSlotEffectiveName(i);
			if(pOther && pOther[0] != '\0' && !IsGenericPlaceholderName(pOther) && NormalizeNameKey(pOther) == Key)
				m_aPeerVolume[i] = m_aPeerVolume[ClientId];
		}
	}
}

bool CAClientVoice::VerifyAndSaveModKey(const char *pKey)
{
	const std::string Clean = TrimCopy(pKey);
	if(Clean.empty())
	{
		m_ModKeyVerified = false;
		str_copy(m_aModStatusMsg, "Ingresa una clave");
		return false;
	}

	if(!CheckModKeyHash(Clean.c_str()))
	{
		m_ModKeyVerified = false;
		str_copy(m_aModStatusMsg, "Clave incorrecta");
		return false;
	}

	str_copy(g_Config.m_AcVoiceModKey, Clean.c_str());
	str_copy(m_aLastCheckedModKey, g_Config.m_AcVoiceModKey);
	m_ModKeyVerified = true;
	str_copy(m_aModStatusMsg, "Clave correcta y guardada en config");
	return true;
}

void CAClientVoice::ClearModKey()
{
	g_Config.m_AcVoiceModKey[0] = '\0';
	m_aLastCheckedModKey[0] = '\0';
	m_ModKeyVerified = false;
	str_copy(m_aModStatusMsg, "Sesion de moderador cerrada");
}

uint32_t CAClientVoice::ResolveSenderIdForTarget(int TargetClientId, const char *pTargetName) const
{
	if(TargetClientId >= 0 && TargetClientId < MAX_CLIENTS)
	{
		if(IsInCustomRoom())
		{
			for(const auto &Peer : m_vRoomPeers)
			{
				if(Peer.m_VirtualSlot == TargetClientId && Peer.m_SenderId != 0)
					return Peer.m_SenderId;
			}
		}
		else
		{
			if(m_aPeers[TargetClientId].m_LastSenderId != 0)
				return m_aPeers[TargetClientId].m_LastSenderId;
			for(const auto &Peer : m_vRoomPeers)
			{
				if((int)(Peer.m_SenderId & 0x7F) == TargetClientId && Peer.m_SenderId != 0)
					return Peer.m_SenderId;
			}
		}
	}

	if(pTargetName && pTargetName[0] != '\0')
	{
		const std::string Key = NormalizeNameKey(pTargetName);
		for(const auto &Peer : m_vRoomPeers)
		{
			if(Peer.m_SenderId != 0 && NormalizeNameKey(Peer.m_aName) == Key)
				return Peer.m_SenderId;
		}
	}

	return 0;
}

bool CAClientVoice::SendGlobalModMute(int TargetClientId, const char *pTargetName, int DurationSeconds)
{
	if(!m_ModKeyVerified)
	{
		str_copy(m_aModStatusMsg, "Primero verifica la clave de moderador");
		return false;
	}

	const std::string CleanKey = TrimCopy(g_Config.m_AcVoiceModKey);
	if(!CheckModKeyHash(CleanKey.c_str()))
	{
		m_ModKeyVerified = false;
		str_copy(m_aModStatusMsg, "La clave guardada ya no es valida");
		return false;
	}

	EnsureSocketOnly();
	UpdateRelayAddress();
	if(!m_Socket || !m_RelayAddrValid)
	{
		str_copy(m_aModStatusMsg, "No se pudo conectar al relay de voz");
		return false;
	}

	char aTargetName[MAX_NAME_LENGTH] = {};
	if(pTargetName && pTargetName[0] != '\0')
	{
		str_copy(aTargetName, TrimCopy(pTargetName).c_str());
	}
	else if(TargetClientId >= 0 && TargetClientId < MAX_CLIENTS)
	{
		const char *pEff = GetSlotEffectiveName(TargetClientId);
		if(pEff && pEff[0] != '\0')
			str_copy(aTargetName, pEff);
		else if(GameClient() && GameClient()->m_aClients[TargetClientId].m_Active)
			str_copy(aTargetName, GameClient()->m_aClients[TargetClientId].m_aName);
	}

	const uint32_t TargetSenderId = ResolveSenderIdForTarget(TargetClientId, aTargetName);
	if(TargetSenderId == 0 && aTargetName[0] == '\0')
	{
		str_copy(m_aModStatusMsg, "Ingresa un nombre o selecciona un jugador valido");
		return false;
	}

	const int ActiveDummy = std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
	const int LocalId = (GameClient() && GameClient()->m_aLocalIds[ActiveDummy] >= 0) ? GameClient()->m_aLocalIds[ActiveDummy] : 0;
	const uint32_t MySenderId = (m_SessionToken << 8) | (uint32_t)(LocalId & 0x7F);

	const SHA256_DIGEST Digest = sha256(CleanKey.c_str(), CleanKey.size());
	uint8_t aPacket[MOD_MUTE_LEN];
	WriteHeader(aPacket, PKT_MOD_MUTE, m_CurrentRoomHash, MySenderId, m_Seq++);
	std::memcpy(aPacket + HEADER_LEN, Digest.data, 32);
	WriteU32LE(aPacket + HEADER_LEN + 32, TargetSenderId);
	WriteI32LE(aPacket + HEADER_LEN + 36, (int32_t)DurationSeconds);
	WriteFixedStr(aPacket + HEADER_LEN + 40, NAME_FIELD_LEN, aTargetName);

	net_udp_send(m_Socket, &m_RelayAddr, aPacket, MOD_MUTE_LEN);
	m_ModAckWaitUntil = time_get() + time_freq() * 3;
	str_format(m_aModStatusMsg, sizeof(m_aModStatusMsg), "Enviando %s para '%s'...", DurationSeconds < 0 ? "unmute" : "mute global", aTargetName[0] ? aTargetName : "ID");
	return true;
}

void CAClientVoice::CreateCustomRoom(const char *pCustomCode, const char *pPassword)
{
	std::string Code = TrimCopy(pCustomCode);
	if(Code.empty())
	{
		const uint32_t RandNum = 1000U + (uint32_t)(((uint64_t)time_get() ^ (uint64_t)m_SessionToken) % 9000ULL);
		char aGen[16];
		str_format(aGen, sizeof(aGen), "AC-%u", RandNum);
		Code = aGen;
	}
	JoinCustomRoom(Code.c_str(), pPassword);
}

void CAClientVoice::JoinCustomRoom(const char *pRoomCode, const char *pPassword)
{
	const std::string Clean = TrimCopy(pRoomCode);
	if(Clean.empty())
		return;
	const std::string CleanPass = TrimCopy(pPassword);

	str_copy(m_aCustomRoomCode, Clean.c_str());
	str_copy(m_aCustomRoomPassword, CleanPass.c_str());
	g_Config.m_AcVoiceEnabled = 1;
	m_vRoomPeers.clear();
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		m_aaRoomPeerName[i][0] = '\0';
		m_aaLastSlotName[i][0] = '\0';
		m_aPeerVolume[i] = 100;
		m_aMuted[i] = false;
		m_aPeers[i].ClearRing();
	}
	m_aActiveAudioMask[0] = 0;
	m_aActiveAudioMask[1] = 0;
	m_aSpeakingHudMask[0] = 0;
	m_aSpeakingHudMask[1] = 0;

	if(GameClient())
	{
		char aBuf[160];
		if(m_aCustomRoomPassword[0] != '\0')
			str_format(aBuf, sizeof(aBuf), "[AClient Voice] Conectado a sala privada: %s (con clave)", m_aCustomRoomCode);
		else
			str_format(aBuf, sizeof(aBuf), "[AClient Voice] Conectado a sala privada: %s", m_aCustomRoomCode);
		GameClient()->Echo(aBuf);
	}
}

void CAClientVoice::LeaveCustomRoom()
{
	if(!IsInCustomRoom())
		return;

	m_aCustomRoomCode[0] = '\0';
	m_aCustomRoomPassword[0] = '\0';
	m_vRoomPeers.clear();
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		m_aaRoomPeerName[i][0] = '\0';
		m_aaLastSlotName[i][0] = '\0';
		m_aPeerVolume[i] = 100;
		m_aMuted[i] = false;
		m_aPeers[i].ClearRing();
	}
	m_aActiveAudioMask[0] = 0;
	m_aActiveAudioMask[1] = 0;
	m_aSpeakingHudMask[0] = 0;
	m_aSpeakingHudMask[1] = 0;

	if(GameClient())
		GameClient()->Echo("[AClient Voice] Saliste de la sala privada");
}

void CAClientVoice::SendRoomInvite(const char *pTargetName)
{
	const std::string Target = TrimCopy(pTargetName);
	if(Target.empty())
		return;

	if(!IsInCustomRoom())
		CreateCustomRoom();

	// 1) Send in-game /w whisper with room code + password (if online, just like Chess)
	bool SentWhisper = false;
	if(GameClient() && Client() && Client()->State() == IClient::STATE_ONLINE)
	{
		int TargetId = -1;
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(GameClient()->m_aClients[i].m_Active && str_comp_nocase(GameClient()->m_aClients[i].m_aName, Target.c_str()) == 0)
			{
				TargetId = i;
				break;
			}
		}

		char aPayload[MAX_CHAT_LENGTH];
		if(m_aCustomRoomPassword[0] != '\0')
			str_format(aPayload, sizeof(aPayload), "[AC-VOICE] Sala: %s | Clave: %s", m_aCustomRoomCode, m_aCustomRoomPassword);
		else
			str_format(aPayload, sizeof(aPayload), "[AC-VOICE] Sala: %s", m_aCustomRoomCode);

		if(Client()->IsSixup() && TargetId >= 0 && TargetId < MAX_CLIENTS)
		{
			protocol7::CNetMsg_Cl_Say Msg7;
			Msg7.m_Mode = protocol7::CHAT_WHISPER;
			Msg7.m_Target = TargetId;
			Msg7.m_pMessage = aPayload;
			Client()->SendPackMsgActive(&Msg7, MSGFLAG_VITAL, true);
			SentWhisper = true;
		}
		else
		{
			char aSafeTarget[MAX_NAME_LENGTH];
			str_copy(aSafeTarget, Target.c_str());
			for(char *p = aSafeTarget; *p; ++p)
			{
				if(*p == '"')
					*p = '\'';
			}
			char aCmd[MAX_CHAT_LENGTH];
			str_format(aCmd, sizeof(aCmd), "/w \"%s\" %s", aSafeTarget, aPayload);
			GameClient()->m_Chat.SendChat(0, aCmd);
			SentWhisper = true;
		}
	}

	// 2) Also send relay UDP invite for passwordless rooms (works cross-server if friend has voice active)
	bool SentRelay = false;
	if(!HasCustomRoomPassword())
	{
		EnsureSocketOnly();
		UpdateRelayAddress();
		if(m_Socket && m_RelayAddrValid)
		{
			const int ActiveDummy = std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
			const int LocalId = (GameClient() && GameClient()->m_aLocalIds[ActiveDummy] >= 0) ? GameClient()->m_aLocalIds[ActiveDummy] : 0;
			const uint32_t MySenderId = (m_SessionToken << 8) | (uint32_t)(LocalId & 0x7F);
			const char *pMyName = GetLocalVoiceName();
			if(!pMyName || !pMyName[0])
				pMyName = "Jugador";

			const uint64_t TargetHash = ComputeCustomRoomHash(m_aCustomRoomCode, m_aCustomRoomPassword);
			uint8_t aPacket[INVITE_LEN];
			WriteHeader(aPacket, PKT_INVITE, TargetHash, MySenderId, m_Seq++);
			WriteFixedStr(aPacket + HEADER_LEN, NAME_FIELD_LEN, pMyName);
			WriteFixedStr(aPacket + HEADER_LEN + NAME_FIELD_LEN, NAME_FIELD_LEN, Target.c_str());
			WriteFixedStr(aPacket + HEADER_LEN + NAME_FIELD_LEN * 2, ROOM_CODE_FIELD_LEN, m_aCustomRoomCode);

			net_udp_send(m_Socket, &m_RelayAddr, aPacket, INVITE_LEN);
			SentRelay = true;
		}
	}

	if(!SentWhisper && GameClient())
	{
		char aBuf[192];
		if(SentRelay)
			str_format(aBuf, sizeof(aBuf), "[AClient Voice] Invitacion enviada por relay a '%s' (sala: %s)", Target.c_str(), m_aCustomRoomCode);
		else if(HasCustomRoomPassword())
			str_format(aBuf, sizeof(aBuf), "[AClient Voice] Conectate a un servidor para enviar /w (Sala: %s | Clave: %s)", m_aCustomRoomCode, m_aCustomRoomPassword);
		else
			str_copy(aBuf, "[AClient Voice] No se pudo enviar invitacion (sin servidor ni relay)");
		GameClient()->Echo(aBuf);
	}
}

bool CAClientVoice::OnVoiceWhisperInvite(int ClientId, int Team, const char *pLine)
{
	if(!pLine || !GameClient())
		return false;
	const char *pTag = str_find(pLine, "[AC-VOICE]");
	if(!pTag)
		return false;

	// Ignore our own outgoing whisper echoes
	if(Team == 2 || ClientId == GameClient()->m_aLocalIds[0] || ClientId == GameClient()->m_aLocalIds[1])
		return false;
	if(str_find(pLine, "\xe2\x86\x92 ") != nullptr) // "→ " outgoing whisper on legacy servers
		return false;

	char aSenderName[MAX_NAME_LENGTH] = {};
	if(ClientId >= 0 && ClientId < MAX_CLIENTS && GameClient()->m_aClients[ClientId].m_Active)
	{
		str_copy(aSenderName, GameClient()->m_aClients[ClientId].m_aName);
	}
	else
	{
		// Fallback for servers without VERSION_DDNET_WHISPER: "[← Name] [AC-VOICE] ..."
		const char *pArrow = str_find(pLine, "\xe2\x86\x90 "); // "← "
		const char *pBracket = pArrow ? str_find(pArrow + 4, "]") : nullptr;
		if(pArrow && pBracket && pBracket > pArrow + 4)
			str_truncate(aSenderName, sizeof(aSenderName), pArrow + 4, (int)(pBracket - (pArrow + 4)));
	}

	const char *pMyName = GetLocalVoiceName();
	if(aSenderName[0] != '\0' && pMyName && pMyName[0] != '\0' && str_comp(aSenderName, pMyName) == 0)
		return false;

	const char *pBody = pTag + 10; // past "[AC-VOICE]"
	while(*pBody == ' ')
		++pBody;

	const char *pSala = str_find_nocase(pBody, "Sala:");
	if(!pSala)
		return false;
	pSala += 5;
	while(*pSala == ' ')
		++pSala;

	std::string RoomStr;
	std::string PassStr;
	const char *pClaveSep = str_find_nocase(pSala, "| Clave:");
	if(!pClaveSep)
		pClaveSep = str_find_nocase(pSala, "|Clave:");
	if(pClaveSep)
	{
		RoomStr = TrimCopy(std::string(pSala, (size_t)(pClaveSep - pSala)).c_str());
		const char *pColon = str_find(pClaveSep, ":");
		if(pColon)
			PassStr = TrimCopy(pColon + 1);
	}
	else
	{
		RoomStr = TrimCopy(pSala);
	}

	if(RoomStr.empty())
		return false;

	str_copy(m_aPendingInviteFrom, aSenderName[0] ? aSenderName : "Alguien");
	str_copy(m_aPendingInviteRoom, RoomStr.c_str());
	str_copy(m_aPendingInvitePassword, PassStr.c_str());
	m_PendingInviteExpiresAt = time_get() + time_freq() * 120;

	char aEcho[224];
	str_format(aEcho, sizeof(aEcho), "[AClient Voice] '%s' te invito a la sala '%s'%s. Acepta en el menu (Esc / AClient) o con /ac_voice_accept",
		m_aPendingInviteFrom, m_aPendingInviteRoom, m_aPendingInvitePassword[0] ? " (con clave)" : "");
	GameClient()->Echo(aEcho);
	return false; // Keep the /w visible in chat
}

bool CAClientVoice::HasPendingInvite() const
{
	return m_aPendingInviteRoom[0] != '\0' && time_get() <= m_PendingInviteExpiresAt;
}

void CAClientVoice::AcceptPendingInvite()
{
	if(!HasPendingInvite())
		return;
	char aRoom[32];
	char aPass[32];
	str_copy(aRoom, m_aPendingInviteRoom);
	str_copy(aPass, m_aPendingInvitePassword);
	m_aPendingInviteFrom[0] = '\0';
	m_aPendingInviteRoom[0] = '\0';
	m_aPendingInvitePassword[0] = '\0';
	m_PendingInviteExpiresAt = 0;
	JoinCustomRoom(aRoom, aPass);
}

void CAClientVoice::DeclinePendingInvite()
{
	m_aPendingInviteFrom[0] = '\0';
	m_aPendingInviteRoom[0] = '\0';
	m_aPendingInvitePassword[0] = '\0';
	m_PendingInviteExpiresAt = 0;
}

static void ScanSdlAudioDevices(std::vector<std::string> &vDevices, int IsCapture, bool &SdlAudioInit, bool &Scanned)
{
	vDevices.clear();
	if(SDL_WasInit(SDL_INIT_AUDIO) == 0)
	{
		if(SDL_InitSubSystem(SDL_INIT_AUDIO) == 0)
			SdlAudioInit = true;
	}
	if(SDL_WasInit(SDL_INIT_AUDIO) != 0)
	{
		const int Num = SDL_GetNumAudioDevices(IsCapture);
		for(int i = 0; i < Num; ++i)
		{
			const char *pName = SDL_GetAudioDeviceName(i, IsCapture);
			if(pName && pName[0] != '\0')
				vDevices.emplace_back(pName);
		}
	}
	Scanned = true;
}

void CAClientVoice::RefreshMicDevices()
{
	ScanSdlAudioDevices(m_vMicDevices, 1, m_SdlAudioInit, m_MicDevicesScanned);
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

void CAClientVoice::RefreshOutputDevices()
{
	ScanSdlAudioDevices(m_vOutputDevices, 0, m_SdlAudioInit, m_OutputDevicesScanned);
}

const std::vector<std::string> &CAClientVoice::GetOutputDevices()
{
	if(!m_OutputDevicesScanned)
		RefreshOutputDevices();
	return m_vOutputDevices;
}

void CAClientVoice::SelectOutputDevice(int Index)
{
	if(Index < 0 || Index >= (int)m_vOutputDevices.size())
		g_Config.m_AcVoiceOutputDevice[0] = '\0';
	else
		str_copy(g_Config.m_AcVoiceOutputDevice, m_vOutputDevices[Index].c_str());

	if(m_AudioActive)
		ReopenPlaybackDevice();
}

void CAClientVoice::ReopenPlaybackDevice()
{
	if(m_PlaybackDevice != 0)
	{
		SDL_CloseAudioDevice((SDL_AudioDeviceID)m_PlaybackDevice);
		m_PlaybackDevice = 0;
	}

	if(SDL_WasInit(SDL_INIT_AUDIO) == 0)
		return;

	SDL_AudioSpec WantPlayback = {};
	WantPlayback.freq = SAMPLE_RATE;
	WantPlayback.format = AUDIO_S16SYS;
	WantPlayback.channels = CHANNELS;
	WantPlayback.samples = FRAME_SAMPLES;
	WantPlayback.callback = nullptr;

	SDL_AudioSpec HavePlayback = {};
	const char *pWanted = g_Config.m_AcVoiceOutputDevice[0] != '\0' ? g_Config.m_AcVoiceOutputDevice : nullptr;
	m_PlaybackDevice = SDL_OpenAudioDevice(pWanted, 0, &WantPlayback, &HavePlayback, 0);
	if(m_PlaybackDevice == 0 && pWanted != nullptr)
	{
		// Fallback to system default if saved output device is unavailable
		m_PlaybackDevice = SDL_OpenAudioDevice(nullptr, 0, &WantPlayback, &HavePlayback, 0);
	}
	if(m_PlaybackDevice != 0)
	{
		SDL_ClearQueuedAudio(m_PlaybackDevice);
		SDL_PauseAudioDevice(m_PlaybackDevice, 0);
	}
	str_copy(m_aOpenedOutputDevice, g_Config.m_AcVoiceOutputDevice);
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

void CAClientVoice::EnsureSocketOnly()
{
	if(!m_Socket)
	{
		NETADDR BindAddr = {};
		BindAddr.type = NETTYPE_IPV4;
		m_Socket = net_udp_create(BindAddr);
	}
}

void CAClientVoice::EnsureStarted()
{
	EnsureSocketOnly();

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
			ReopenPlaybackDevice();
			m_AudioActive = true;
		}
	}
	else
	{
		if(str_comp(m_aOpenedMicDevice, g_Config.m_AcVoiceMicDevice) != 0)
			ReopenCaptureDevice();
		if(str_comp(m_aOpenedOutputDevice, g_Config.m_AcVoiceOutputDevice) != 0)
			ReopenPlaybackDevice();
	}
}

void CAClientVoice::StopSubsystem()
{
	if(m_Socket && m_RelayAddrValid && (m_CurrentRoomHash != 0 || m_LastPresenceKeepaliveTime != 0))
	{
		SendControlPacket(PKT_LEAVE, m_CurrentRoomHash);
	}

	m_CurrentRoomHash = 0;
	m_LastTeam = -1;
	m_LastLocalId = -1;
	m_LastPresenceKeepaliveTime = 0;
	m_aLastLocalPlayerName[0] = '\0';
	mem_zero(&m_LastServerAddr, sizeof(m_LastServerAddr));
	m_aActiveAudioMask[0] = 0;
	m_aActiveAudioMask[1] = 0;
	m_aSpeakingHudMask[0] = 0;
	m_aSpeakingHudMask[1] = 0;
	m_LoopbackActive = false;
	m_LoopbackPeer.ClearRing();
	m_vRoomPeers.clear();
	m_mSenderIdToSlot.clear();
	m_LastVolumeSyncTime = 0;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		m_aPeers[i].ClearRing();
		m_aaRoomPeerName[i][0] = '\0';
		m_aaLastSlotName[i][0] = '\0';
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
	m_aOpenedMicDevice[0] = '\0';
	m_aOpenedOutputDevice[0] = '\0';
	m_AudioActive = false;

	if(m_pEncoder)
	{
		opus_encoder_destroy(m_pEncoder);
		m_pEncoder = nullptr;
	}

	if(m_Socket && time_get() > m_ModAckWaitUntil)
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
	m_RelaySupportsRoomPeers = false;
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
	if(!m_Socket || !m_RelayAddrValid)
		return;
	if(Type != PKT_KEEPALIVE && Type != PKT_LEAVE)
		return;
	if(RoomHash == 0 && Type != PKT_KEEPALIVE && m_LastPresenceKeepaliveTime == 0)
		return;

	const int ActiveDummy = std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
	const int RawLocalId = GameClient() ? GameClient()->m_aLocalIds[ActiveDummy] : 0;
	const int LocalId = (RawLocalId >= 0 && RawLocalId < MAX_CLIENTS) ? RawLocalId : 0;
	const uint32_t SenderId = (m_SessionToken << 8) | (uint32_t)(LocalId & 0x7F);

	if(Type == PKT_KEEPALIVE)
	{
		uint8_t aPacket[KEEPALIVE_WITH_NAME_LEN];
		WriteHeader(aPacket, Type, RoomHash, SenderId, m_Seq++);
		const char *pPlayerName = GetLocalVoiceName();
		WriteFixedStr(aPacket + HEADER_LEN, NAME_FIELD_LEN, pPlayerName);
		net_udp_send(m_Socket, &m_RelayAddr, aPacket, KEEPALIVE_WITH_NAME_LEN);
		str_copy(m_aLastLocalPlayerName, pPlayerName);
	}
	else
	{
		uint8_t aPacket[HEADER_LEN];
		WriteHeader(aPacket, Type, RoomHash, SenderId, m_Seq++);
		net_udp_send(m_Socket, &m_RelayAddr, aPacket, HEADER_LEN);
	}
}

void CAClientVoice::SendNameBeacon(uint64_t RoomHash)
{
	if(!m_Socket || !m_RelayAddrValid || RoomHash == 0)
		return;
	const char *pPlayerName = GetLocalVoiceName();
	if(!pPlayerName || pPlayerName[0] == '\0')
		return;

	const int ActiveDummy = std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
	const int RawLocalId = GameClient() ? GameClient()->m_aLocalIds[ActiveDummy] : 0;
	const int LocalId = (RawLocalId >= 0 && RawLocalId < MAX_CLIENTS) ? RawLocalId : 0;
	const uint32_t SenderId = (m_SessionToken << 8) | (uint32_t)(LocalId & 0x7F);

	uint8_t aPacket[NAME_BEACON_LEN] = {};
	WriteHeader(aPacket, PKT_VOICE, RoomHash, SenderId, 0);
	aPacket[HEADER_LEN + 0] = 'A';
	aPacket[HEADER_LEN + 1] = 'C';
	aPacket[HEADER_LEN + 2] = 'N';
	aPacket[HEADER_LEN + 3] = 'M';
	WriteFixedStr(aPacket + HEADER_LEN + 4, NAME_FIELD_LEN, pPlayerName);
	net_udp_send(m_Socket, &m_RelayAddr, aPacket, NAME_BEACON_LEN);
}

void CAClientVoice::UpdateRoomState()
{
	const int64_t Now = time_get();
	const bool CustomRoom = IsInCustomRoom();

	if(CustomRoom != m_WasInCustomRoom)
	{
		m_WasInCustomRoom = CustomRoom;
		m_vRoomPeers.clear();
		m_mSenderIdToSlot.clear();
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			m_aaRoomPeerName[i][0] = '\0';
			m_aaLastSlotName[i][0] = '\0';
			m_aPeers[i].ClearRing();
		}
		m_aActiveAudioMask[0] = 0;
		m_aActiveAudioMask[1] = 0;
		m_aSpeakingHudMask[0] = 0;
		m_aSpeakingHudMask[1] = 0;
	}

	if(!CustomRoom && Client()->State() != IClient::STATE_ONLINE)
	{
		if(m_CurrentRoomHash != 0 || m_LastPresenceKeepaliveTime != 0)
		{
			SendControlPacket(PKT_LEAVE, m_CurrentRoomHash);
			m_CurrentRoomHash = 0;
			m_LastTeam = -1;
			m_LastLocalId = -1;
			m_LastPresenceKeepaliveTime = 0;
			mem_zero(&m_LastServerAddr, sizeof(m_LastServerAddr));
			m_aActiveAudioMask[0] = 0;
			m_aActiveAudioMask[1] = 0;
			m_aSpeakingHudMask[0] = 0;
			m_aSpeakingHudMask[1] = 0;
			m_vRoomPeers.clear();
			m_mSenderIdToSlot.clear();
		}
		return;
	}

	const int ActiveDummy = std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
	int LocalId = (GameClient() && Client()->State() == IClient::STATE_ONLINE) ? GameClient()->m_aLocalIds[ActiveDummy] : 0;
	if(!CustomRoom && (LocalId < 0 || LocalId >= MAX_CLIENTS || !GameClient()->m_aClients[LocalId].m_Active))
		return;
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
		LocalId = 0;

	const int Team = (Client()->State() == IClient::STATE_ONLINE && GameClient()) ? GameClient()->m_Teams.Team(LocalId) : 0;
	const NETADDR ServerAddr = (Client()->State() == IClient::STATE_ONLINE) ? Client()->ServerAddress() : NETADDR{};

	uint64_t TargetRoom = 0;
	if(CustomRoom)
	{
		TargetRoom = ComputeCustomRoomHash(m_aCustomRoomCode, m_aCustomRoomPassword);
	}
	else if(!g_Config.m_AcVoiceOnlyTeam || Team > 0)
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

	const char *pMyName = GetLocalVoiceName();
	const bool NameChanged = str_comp(m_aLastLocalPlayerName, pMyName) != 0;

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
		m_vRoomPeers.clear();
		m_mSenderIdToSlot.clear();

		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			m_aPeers[i].ClearRing();
			m_aaRoomPeerName[i][0] = '\0';
		}

		if(m_CurrentRoomHash != 0)
		{
			SendControlPacket(PKT_KEEPALIVE, m_CurrentRoomHash);
			m_LastKeepaliveTime = Now;
			m_LastPresenceKeepaliveTime = Now;
			if(CustomRoom && !m_RelaySupportsRoomPeers)
			{
				SendNameBeacon(m_CurrentRoomHash);
				m_LastNameBeaconTime = Now;
			}
		}
		else if(Client()->State() == IClient::STATE_ONLINE)
		{
			SendControlPacket(PKT_KEEPALIVE, 0);
			m_LastPresenceKeepaliveTime = Now;
		}
	}
	else if(m_CurrentRoomHash != 0)
	{
		m_LastLocalId = LocalId;
		// 5s steady-state keepalive (3x safety margin vs 15s relay timeout); immediate on name change
		const int64_t KeepaliveInterval = time_freq() * 5;
		if(NameChanged || (Now - m_LastKeepaliveTime) >= KeepaliveInterval)
		{
			SendControlPacket(PKT_KEEPALIVE, m_CurrentRoomHash);
			m_LastKeepaliveTime = Now;
			m_LastPresenceKeepaliveTime = Now;
		}
		// Skip redundant 534B ACNM beacon once relay PKT_ROOM_PEERS support is confirmed
		if(CustomRoom && !m_RelaySupportsRoomPeers && (NameChanged || (Now - m_LastNameBeaconTime) >= time_freq() * 3))
		{
			SendNameBeacon(m_CurrentRoomHash);
			m_LastNameBeaconTime = Now;
		}
	}
	else if(Client()->State() == IClient::STATE_ONLINE)
	{
		// Keep relay presence alive with RoomHash 0 so friends can still send us invites in Team 0
		const int64_t KeepaliveInterval = time_freq() * 5;
		if(NameChanged || (Now - m_LastPresenceKeepaliveTime) >= KeepaliveInterval)
		{
			SendControlPacket(PKT_KEEPALIVE, 0);
			m_LastPresenceKeepaliveTime = Now;
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
		const int RawLocalId = GameClient() ? GameClient()->m_aLocalIds[ActiveDummy] : 0;
		const int LocalId = (RawLocalId >= 0 && RawLocalId < MAX_CLIENTS) ? RawLocalId : 0;
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

int CAClientVoice::ResolveOrAllocRoomSlot(uint32_t SenderId, const char *pName)
{
	const bool HasRealName = (pName && pName[0] != '\0' && !IsGenericPlaceholderName(pName));

	// Fast O(1) lookup for hot-path incoming audio packets in custom rooms
	auto ItSlot = m_mSenderIdToSlot.find(SenderId);
	if(ItSlot != m_mSenderIdToSlot.end())
	{
		const int CachedSlot = ItSlot->second;
		if(CachedSlot >= 0 && CachedSlot < MAX_CLIENTS)
		{
			if(!HasRealName || str_comp(m_aaRoomPeerName[CachedSlot], pName) == 0)
				return CachedSlot;
		}
	}

	bool aUsed[MAX_CLIENTS] = {};
	for(auto &Peer : m_vRoomPeers)
	{
		if(Peer.m_SenderId == SenderId && Peer.m_VirtualSlot >= 0 && Peer.m_VirtualSlot < MAX_CLIENTS)
		{
			m_mSenderIdToSlot[SenderId] = Peer.m_VirtualSlot;
			if(HasRealName && str_comp(Peer.m_aName, pName) != 0)
			{
				str_copy(Peer.m_aName, pName);
				if(IsInCustomRoom())
				{
					str_copy(m_aaRoomPeerName[Peer.m_VirtualSlot], pName);
					SyncPlayerVolumesByName();
				}
			}
			return Peer.m_VirtualSlot;
		}
		if(Peer.m_VirtualSlot >= 0 && Peer.m_VirtualSlot < MAX_CLIENTS)
			aUsed[Peer.m_VirtualSlot] = true;
	}

	int Chosen = -1;
	const int Preferred = (int)(SenderId & 0x7F);
	if(Preferred >= 0 && Preferred < MAX_CLIENTS && !aUsed[Preferred])
	{
		Chosen = Preferred;
	}
	else
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(!aUsed[i])
			{
				Chosen = i;
				break;
			}
		}
	}

	if(Chosen >= 0)
	{
		SRoomPeerInfo Info;
		Info.m_SenderId = SenderId;
		Info.m_VirtualSlot = Chosen;
		if(HasRealName)
		{
			str_copy(Info.m_aName, pName);
		}
		else
		{
			str_format(Info.m_aName, sizeof(Info.m_aName), "Amigo #%d", (int)m_vRoomPeers.size() + 1);
		}
		m_vRoomPeers.push_back(Info);
		m_mSenderIdToSlot[SenderId] = Chosen;
		if(IsInCustomRoom())
		{
			str_copy(m_aaRoomPeerName[Chosen], Info.m_aName);
			SyncPlayerVolumesByName();
		}
	}
	return Chosen;
}

void CAClientVoice::PollIncomingPackets()
{
	if(!m_Socket)
		return;

	NETADDR Addr;
	unsigned char *pData = nullptr;
	int Bytes = 0;
	const int64_t Now = time_get();
	int PacketsRead = 0;

	while(PacketsRead < 64 && (Bytes = net_udp_recv(m_Socket, &Addr, &pData)) > 0)
	{
		++PacketsRead;
		if(Bytes < HEADER_LEN)
			continue;
		if(pData[0] != 'A' || pData[1] != 'C' || pData[2] != 'V' || pData[3] != '1')
			continue;

		const uint8_t PktType = pData[4];
		const uint64_t RoomHash = ReadU64LE(pData + 5);
		const uint32_t SenderId = ReadU32LE(pData + 13);
		const uint32_t Seq = ReadU32LE(pData + 17);

		if(PktType == PKT_INVITE && Bytes >= INVITE_LEN)
		{
			char aFrom[MAX_NAME_LENGTH];
			char aTo[MAX_NAME_LENGTH];
			char aRoom[32];
			ReadFixedStr(aFrom, sizeof(aFrom), pData + HEADER_LEN, NAME_FIELD_LEN);
			ReadFixedStr(aTo, sizeof(aTo), pData + HEADER_LEN + NAME_FIELD_LEN, NAME_FIELD_LEN);
			ReadFixedStr(aRoom, sizeof(aRoom), pData + HEADER_LEN + NAME_FIELD_LEN * 2, ROOM_CODE_FIELD_LEN);

			if(aRoom[0] != '\0')
			{
				str_copy(m_aPendingInviteFrom, aFrom[0] ? aFrom : "Alguien");
				str_copy(m_aPendingInviteRoom, aRoom);
				m_aPendingInvitePassword[0] = '\0';
				m_PendingInviteExpiresAt = Now + time_freq() * 120;
				if(GameClient())
				{
					char aMsg[192];
					str_format(aMsg, sizeof(aMsg), "[AClient Voice] '%s' te invito a la sala '%s'. Acepta en el menu (Esc / AClient) o con /ac_voice_accept", m_aPendingInviteFrom, m_aPendingInviteRoom);
					GameClient()->Echo(aMsg);
				}
			}
			continue;
		}

		if(PktType == PKT_MOD_ACK && Bytes >= MOD_ACK_LEN)
		{
			const uint8_t Status = pData[HEADER_LEN];
			const int32_t DurSecs = ReadI32LE(pData + HEADER_LEN + 1);
			char aTargetName[MAX_NAME_LENGTH];
			ReadFixedStr(aTargetName, sizeof(aTargetName), pData + HEADER_LEN + 5, NAME_FIELD_LEN);

			if(Status == 0)
			{
				str_copy(m_aModStatusMsg, "Relay rechazo la clave de moderador");
			}
			else if(Status == 2)
			{
				str_format(m_aModStatusMsg, sizeof(m_aModStatusMsg), "Unmute global aplicado a '%s'", aTargetName[0] ? aTargetName : "jugador");
			}
			else if(DurSecs == 0)
			{
				str_format(m_aModStatusMsg, sizeof(m_aModStatusMsg), "Mute permanente aplicado a '%s'", aTargetName[0] ? aTargetName : "jugador");
			}
			else
			{
				str_format(m_aModStatusMsg, sizeof(m_aModStatusMsg), "Mute global (%d min) aplicado a '%s'", std::max(1, (int)(DurSecs / 60)), aTargetName[0] ? aTargetName : "jugador");
			}

			if(GameClient())
			{
				char aMsg[192];
				str_format(aMsg, sizeof(aMsg), "[AClient MOD Voice] %s", m_aModStatusMsg);
				GameClient()->Echo(aMsg);
			}
			continue;
		}

		if(m_CurrentRoomHash == 0 || RoomHash != m_CurrentRoomHash)
			continue;

		if(PktType == PKT_ROOM_PEERS && Bytes >= HEADER_LEN + 1)
		{
			m_RelaySupportsRoomPeers = true;
			const int Count = pData[HEADER_LEN];
			const int ExpectedBytes = HEADER_LEN + 1 + Count * (4 + NAME_FIELD_LEN);
			if(Count >= 0 && Bytes >= ExpectedBytes)
			{
				std::vector<SRoomPeerInfo> vPrev = std::move(m_vRoomPeers);
				m_vRoomPeers.clear();
				m_mSenderIdToSlot.clear();
				bool aUsed[MAX_CLIENTS] = {};
				if(IsInCustomRoom())
				{
					for(int i = 0; i < MAX_CLIENTS; ++i)
						m_aaRoomPeerName[i][0] = '\0';
				}

				for(int p = 0; p < Count; ++p)
				{
					const int Off = HEADER_LEN + 1 + p * (4 + NAME_FIELD_LEN);
					const uint32_t PeerSid = ReadU32LE(pData + Off);
					char aPeerName[MAX_NAME_LENGTH];
					ReadFixedStr(aPeerName, sizeof(aPeerName), pData + Off + 4, NAME_FIELD_LEN);

					const int PeerRawSlot = (int)(PeerSid & 0x7F);
					if((PeerSid >> 8) == m_SessionToken && PeerRawSlot == m_LastLocalId)
						continue;

					int Slot = -1;
					const char *pPrevKnownName = nullptr;
					if(IsInCustomRoom())
					{
						for(const auto &Prev : vPrev)
						{
							if(Prev.m_SenderId == PeerSid)
							{
								if(Prev.m_aName[0] != '\0' && !IsGenericPlaceholderName(Prev.m_aName))
									pPrevKnownName = Prev.m_aName;
								if(Prev.m_VirtualSlot >= 0 && Prev.m_VirtualSlot < MAX_CLIENTS && !aUsed[Prev.m_VirtualSlot])
									Slot = Prev.m_VirtualSlot;
								break;
							}
						}
						if(Slot < 0 && PeerRawSlot >= 0 && PeerRawSlot < MAX_CLIENTS && !aUsed[PeerRawSlot])
							Slot = PeerRawSlot;
						if(Slot < 0)
						{
							for(int i = 0; i < MAX_CLIENTS; ++i)
							{
								if(!aUsed[i])
								{
									Slot = i;
									break;
								}
							}
						}
						if(Slot >= 0)
						{
							aUsed[Slot] = true;
							if(aPeerName[0] != '\0' && !IsGenericPlaceholderName(aPeerName))
								str_copy(m_aaRoomPeerName[Slot], aPeerName);
							else if(pPrevKnownName)
								str_copy(m_aaRoomPeerName[Slot], pPrevKnownName);
							else
								str_format(m_aaRoomPeerName[Slot], sizeof(m_aaRoomPeerName[Slot]), "Amigo #%d", (int)m_vRoomPeers.size() + 1);
						}
					}
					else
					{
						Slot = PeerRawSlot;
					}

					if(Slot >= 0 && Slot < MAX_CLIENTS)
					{
						SRoomPeerInfo Info;
						Info.m_SenderId = PeerSid;
						Info.m_VirtualSlot = Slot;
						if(IsInCustomRoom())
							str_copy(Info.m_aName, m_aaRoomPeerName[Slot]);
						else
							str_copy(Info.m_aName, aPeerName[0] ? aPeerName : "Jugador");
						m_vRoomPeers.push_back(Info);
						m_mSenderIdToSlot[PeerSid] = Slot;
					}
				}
				SyncPlayerVolumesByName();
			}
			continue;
		}

		if(PktType != PKT_VOICE || Bytes <= HEADER_LEN)
			continue;

		const int RawPeerSlot = (int)(SenderId & 0x7F);
		if((SenderId >> 8) == m_SessionToken && RawPeerSlot == m_LastLocalId)
			continue;

		// Client-to-client name beacon (works even on older relays without PKT_ROOM_PEERS)
		if(Bytes == NAME_BEACON_LEN && std::memcmp(pData + HEADER_LEN, "ACNM", 4) == 0)
		{
			char aBeaconName[MAX_NAME_LENGTH];
			ReadFixedStr(aBeaconName, sizeof(aBeaconName), pData + HEADER_LEN + 4, NAME_FIELD_LEN);
			if(IsInCustomRoom() && aBeaconName[0] != '\0')
			{
				const int BeaconSlot = ResolveOrAllocRoomSlot(SenderId, aBeaconName);
				if(BeaconSlot >= 0 && BeaconSlot < MAX_CLIENTS)
				{
					m_aPeers[BeaconSlot].m_LastSenderId = SenderId;
					m_aPeers[BeaconSlot].m_LastPacketTime = Now;
				}
			}
			continue;
		}

		int PeerSlot = RawPeerSlot;
		if(IsInCustomRoom())
		{
			PeerSlot = ResolveOrAllocRoomSlot(SenderId, nullptr);
			if(PeerSlot < 0 || PeerSlot >= MAX_CLIENTS)
				continue;
		}
		else
		{
			if(PeerSlot < 0 || PeerSlot >= MAX_CLIENTS)
				continue;
			// Ignore packets from players no longer in our active team
			if(!GameClient()->m_aClients[PeerSlot].m_Active || GameClient()->m_Teams.Team(PeerSlot) != m_LastTeam)
				continue;
		}

		if(m_aPeers[PeerSlot].m_LastSenderId != SenderId)
		{
			m_aPeers[PeerSlot].m_LastSenderId = SenderId;
		}
		m_aPeers[PeerSlot].m_LastPacketTime = Now;

		if(m_aMuted[PeerSlot] || m_aPeerVolume[PeerSlot] <= 0)
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
			const uint32_t FirstChunk = std::min<uint32_t>(Count, (uint32_t)PEER_RING_SAMPLES - Peer.m_ReadPos);
			const int16_t *pChunk1 = Peer.m_aPcmRing + Peer.m_ReadPos;
			const int16_t *pChunk2 = Peer.m_aPcmRing;
			if(PeerVol == 1.0f)
			{
				for(uint32_t i = 0; i < FirstChunk; ++i)
					aMix[i] += pChunk1[i];
				for(uint32_t i = FirstChunk; i < Count; ++i)
					aMix[i] += pChunk2[i - FirstChunk];
			}
			else
			{
				for(uint32_t i = 0; i < FirstChunk; ++i)
					aMix[i] += (int32_t)std::lround((float)pChunk1[i] * PeerVol);
				for(uint32_t i = FirstChunk; i < Count; ++i)
					aMix[i] += (int32_t)std::lround((float)pChunk2[i - FirstChunk] * PeerVol);
			}
			Peer.m_ReadPos += Count;
			if(Peer.m_ReadPos >= (uint32_t)PEER_RING_SAMPLES)
				Peer.m_ReadPos -= (uint32_t)PEER_RING_SAMPLES;
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
	if(!m_ModKeyVerified && g_Config.m_AcVoiceModKey[0] != '\0' && str_comp(m_aLastCheckedModKey, g_Config.m_AcVoiceModKey) != 0)
	{
		str_copy(m_aLastCheckedModKey, g_Config.m_AcVoiceModKey);
		const std::string Clean = TrimCopy(g_Config.m_AcVoiceModKey);
		if(CheckModKeyHash(Clean.c_str()))
		{
			m_ModKeyVerified = true;
			str_copy(m_aModStatusMsg, "Clave de moderador activa (guardada en config)");
		}
	}

	if(!g_Config.m_AcVoiceEnabled)
	{
		if(m_AudioActive || m_CurrentRoomHash != 0 || m_LastPresenceKeepaliveTime != 0)
			StopSubsystem();
		if(m_Socket && time_get() <= m_ModAckWaitUntil)
			PollIncomingPackets();
		else if(m_Socket)
		{
			net_udp_close(m_Socket);
			m_Socket = nullptr;
		}
		return;
	}

	const int64_t Now = time_get();
	if(m_LastVolumeSyncTime == 0 || (Now - m_LastVolumeSyncTime) >= time_freq() / 4)
	{
		m_LastVolumeSyncTime = Now;
		SyncPlayerVolumesByName();
	}

	EnsureStarted();
	UpdateRelayAddress();
	UpdateRoomState();
	PumpOutgoingAudio();
	PollIncomingPackets();
	MixAndQueuePlayback(Now);
}

void CAClientVoice::OnRender()
{
	const bool PendingInvite = HasPendingInvite();
	if(!g_Config.m_AcVoiceHud)
		return;
	if(!g_Config.m_AcVoiceEnabled && !PendingInvite)
		return;
	if(g_Config.m_ClFocusMode && (g_Config.m_ClFocusModeHideHud || g_Config.m_ClFocusModeHideUI))
		return;
	if(Client()->State() != IClient::STATE_ONLINE && !IsInCustomRoom() && !PendingInvite)
		return;

#if defined(CONF_VIDEORECORDER)
	if(!((IVideo::Current() && g_Config.m_ClVideoShowhud) || (!IVideo::Current() && g_Config.m_ClShowhud)))
		return;
#else
	if(!g_Config.m_ClShowhud)
		return;
#endif

	const bool HasPeersSpeaking = g_Config.m_AcVoiceEnabled && (m_aSpeakingHudMask[0] | m_aSpeakingHudMask[1]) != 0;
	const bool ShowSelf = g_Config.m_AcVoiceEnabled && m_IsTransmitting;
	if(!ShowSelf && !HasPeersSpeaking && !PendingInvite)
		return;

	const CScreenRect PrevScreen = Graphics()->GetScreen();
	const float HudHeight = 300.0f;
	const float HudWidth = HudHeight * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(HudWidth, HudHeight);

	float X = 5.0f;
	float Y = 74.0f;
	const float RowH = 9.0f;
	const float RowGap = 1.5f;
	const float PillFontSize = 5.8f;
	const float DotSize = 2.8f;

	if(PendingInvite)
	{
		char aInvText[160];
		str_format(aInvText, sizeof(aInvText), "Invitacion voz: %s -> %s%s (Esc / /ac_voice_accept)",
			m_aPendingInviteFrom, m_aPendingInviteRoom, m_aPendingInvitePassword[0] ? " [Clave]" : "");
		const float InvW = std::clamp(TextRender()->TextWidth(PillFontSize, aInvText, -1, -1.0f) + 8.0f, 96.0f, 195.0f);
		CUIRect InviteBox(X, Y, InvW, 10.5f);
		InviteBox.Draw(ColorRGBA(0.12f, 0.28f, 0.50f, 0.86f), IGraphics::CORNER_ALL, 2.0f);
		CUIRect TextR = InviteBox;
		TextR.VMargin(3.5f, &TextR);
		TextRender()->TextColor(0.95f, 0.98f, 1.0f, 1.0f);
		Ui()->DoLabel(&TextR, aInvText, PillFontSize, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		Y += 12.0f;
	}

	auto DrawSpeakerPill = [&](const char *pName, float Level, bool IsSelf) {
		const float TextW = TextRender()->TextWidth(PillFontSize, pName, -1, -1.0f);
		const float PillW = std::clamp(TextW + 12.0f, 28.0f, 74.0f);

		CUIRect Box(X, Y, PillW, RowH);
		Box.Draw(ColorRGBA(0.06f, 0.08f, 0.12f, 0.62f), IGraphics::CORNER_ALL, 2.0f);

		CUIRect Dot(X + 3.0f, Y + (RowH - DotSize) * 0.5f, DotSize, DotSize);
		Dot.Draw(IsSelf ? ColorRGBA(0.25f, 0.95f, 0.45f, 0.95f) : ColorRGBA(0.30f, 0.78f, 1.0f, 0.95f), IGraphics::CORNER_ALL, DotSize * 0.5f);

		if(IsSelf && Level > 0.01f)
		{
			CUIRect Meter(X + 1.5f, Y + RowH - 1.1f, (PillW - 3.0f) * std::clamp(Level, 0.0f, 1.0f), 0.9f);
			Meter.Draw(ColorRGBA(0.25f, 0.95f, 0.45f, 0.85f), IGraphics::CORNER_ALL, 0.45f);
		}

		CUIRect LabelRect(X + 8.0f, Y, PillW - 10.0f, RowH);
		TextRender()->TextColor(0.95f, 0.97f, 1.0f, 0.95f);
		Ui()->DoLabel(&LabelRect, pName, PillFontSize, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		Y += RowH + RowGap;
	};

	if(ShowSelf)
	{
		const char *pSelfName = GetLocalVoiceName();
		if(!pSelfName || !pSelfName[0])
			pSelfName = Localize("Tu");
		DrawSpeakerPill(pSelfName, m_LocalMicLevel, true);
	}

	int Shown = 0;
	for(int w = 0; g_Config.m_AcVoiceEnabled && w < 2 && Shown < 6; ++w)
	{
		uint64_t Mask = m_aSpeakingHudMask[w];
		while(Mask && Shown < 6)
		{
			const int Bit = TrailingZeros64(Mask);
			Mask &= (Mask - 1);
			const int Slot = (w << 6) + Bit;
			const char *pName = GetSlotEffectiveName(Slot);
			if(!pName || !pName[0])
				continue;
			DrawSpeakerPill(pName, 1.0f, false);
			++Shown;
		}
	}

	Graphics()->MapScreen(PrevScreen);
}

std::vector<CAClientVoice::SRoomPeerInfo> CAClientVoice::GetAvailableVoicePeers() const
{
	std::vector<SRoomPeerInfo> vResult;
	if(!g_Config.m_AcVoiceEnabled || m_CurrentRoomHash == 0)
		return vResult;
	if(!IsInCustomRoom() && Client()->State() != IClient::STATE_ONLINE)
		return vResult;

	const int ActiveDummy = std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
	const int LocalId = GameClient() ? GameClient()->m_aLocalIds[ActiveDummy] : -1;
	bool aIncluded[MAX_CLIENTS] = {};

	for(const SRoomPeerInfo &Peer : m_vRoomPeers)
	{
		const int Slot = Peer.m_VirtualSlot;
		if(Slot < 0 || Slot >= MAX_CLIENTS || aIncluded[Slot])
			continue;

		if(!IsInCustomRoom() && GameClient())
		{
			if(Slot == LocalId && (Peer.m_SenderId >> 8) == m_SessionToken)
				continue;
			if(!GameClient()->m_aClients[Slot].m_Active)
				continue;
			if(g_Config.m_AcVoiceOnlyTeam && GameClient()->m_Teams.Team(Slot) != m_LastTeam)
				continue;
		}

		SRoomPeerInfo Out = Peer;
		const char *pEff = GetSlotEffectiveName(Slot);
		if(pEff && pEff[0] != '\0')
			str_copy(Out.m_aName, pEff);
		else if(!Out.m_aName[0])
			str_copy(Out.m_aName, IsInCustomRoom() ? "Amigo" : "Jugador");

		aIncluded[Slot] = true;
		vResult.push_back(Out);
	}

	// Fallback if relay has not yet broadcast PKT_ROOM_PEERS: include peers heard recently
	const int64_t Now = time_get();
	const int64_t RecentThreshold = time_freq() * 20;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(aIncluded[i] || (!IsInCustomRoom() && i == LocalId))
			continue;
		if(m_aPeers[i].m_LastPacketTime > 0 && (Now - m_aPeers[i].m_LastPacketTime) <= RecentThreshold)
		{
			if(!IsInCustomRoom() && GameClient())
			{
				if(!GameClient()->m_aClients[i].m_Active)
					continue;
				if(g_Config.m_AcVoiceOnlyTeam && GameClient()->m_Teams.Team(i) != m_LastTeam)
					continue;
			}
			const char *pEff = GetSlotEffectiveName(i);
			if(!pEff || !pEff[0])
				continue;
			SRoomPeerInfo Out;
			Out.m_SenderId = m_aPeers[i].m_LastSenderId;
			Out.m_VirtualSlot = i;
			str_copy(Out.m_aName, pEff);
			aIncluded[i] = true;
			vResult.push_back(Out);
		}
	}

	return vResult;
}

void CAClientVoice::OnStateChange(int NewState, int OldState)
{
	if(NewState != IClient::STATE_ONLINE && !IsInCustomRoom() && (m_CurrentRoomHash != 0 || m_LastPresenceKeepaliveTime != 0))
	{
		SendControlPacket(PKT_LEAVE, m_CurrentRoomHash);
		m_CurrentRoomHash = 0;
		m_LastTeam = -1;
		m_LastLocalId = -1;
		m_LastPresenceKeepaliveTime = 0;
		mem_zero(&m_LastServerAddr, sizeof(m_LastServerAddr));
		m_aActiveAudioMask[0] = 0;
		m_aActiveAudioMask[1] = 0;
		m_aSpeakingHudMask[0] = 0;
		m_aSpeakingHudMask[1] = 0;
		m_vRoomPeers.clear();
		m_mSenderIdToSlot.clear();
	}
}

void CAClientVoice::OnShutdown()
{
	m_ModAckWaitUntil = 0;
	StopSubsystem();
	if(m_SdlAudioInit)
	{
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		m_SdlAudioInit = false;
	}
}
