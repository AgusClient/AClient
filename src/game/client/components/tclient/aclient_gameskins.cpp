#include <chrono>
#include <cinttypes>
#include "aclient_gameskins.h"
#include "aclient_backend.h"

#include <base/io.h>
#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>
#include <game/client/gameclient.h>
#include <generated/client_data.h>

static void TrimPlayerName(const char *pSrc, char *pDst, int DstSize)
{
	if(!pDst || DstSize <= 0)
		return;
	pDst[0] = '\0';
	if(!pSrc)
		return;

	while(*pSrc == ' ' || *pSrc == '\t' || *pSrc == '\r' || *pSrc == '\n')
		++pSrc;

	str_copy(pDst, pSrc, DstSize);
	int Len = str_length(pDst);
	while(Len > 0 && (pDst[Len - 1] == ' ' || pDst[Len - 1] == '\t' || pDst[Len - 1] == '\r' || pDst[Len - 1] == '\n'))
	{
		pDst[Len - 1] = '\0';
		--Len;
	}
}

CAClientGameSkins::CAClientGameSkins() :
	m_PendingAnnounce(false),
	m_LastActivePlayerCount(0),
	m_LastPresenceRefreshTime(0),
	m_LastRenderCheckTime(0),
	m_ServerJoinTime(0),
	m_InitialDownloadPhase(false),
	m_WasFrozen(false),
	m_BlacklistLoaded(false),
	m_SkinUsersRevision(0)
{
	m_aLastServerAddress[0] = '\0';
	m_aLastAnnouncedPlayer[0] = '\0';
	m_aLastAnnouncedDummy[0] = '\0';
	m_aLastAnnouncedSkin[0] = '\0';
	m_aLastAnnouncedEntities[0] = '\0';
	m_aLastAnnouncedDummyEntities[0] = '\0';
}

CAClientGameSkins::~CAClientGameSkins()
{
	if(m_pPresenceRequest)
		m_pPresenceRequest->Abort();
	if(m_pDownloadRequest)
		m_pDownloadRequest->Abort();
	if(m_pDownloadEntitiesRequest)
		m_pDownloadEntitiesRequest->Abort();
	FreeAllGameSkins();
}

void CAClientGameSkins::OnInit()
{
	EnsureBlacklistLoaded();
	UpdateActiveSkinUsers();

	// Limpiar registros huerfanos al iniciar el cliente
	const char *pPlayerName = g_Config.m_PlayerName;
	if(pPlayerName && pPlayerName[0])
	{
		char aTrimmed[MAX_NAME_LENGTH];
		TrimPlayerName(pPlayerName, aTrimmed, sizeof(aTrimmed));
		if(aTrimmed[0] != '\0')
		{
			SendPresenceLeave("all", aTrimmed);
		}
	}
}

void CAClientGameSkins::EnsureBlacklistLoaded()
{
	if(m_BlacklistLoaded || !Storage())
		return;
	LoadBlacklist();
}

void CAClientGameSkins::LoadBlacklist()
{
	m_sBlacklistedGameSkins.clear();
	if(!Storage())
		return;

	IOHANDLE File = Storage()->OpenFile("gameskin_blacklist.txt", IOFLAG_READ, IStorage::TYPE_ALL);
	if(File)
	{
		CLineReader LineReader;
		if(LineReader.OpenFile(File))
		{
			while(const char *pLine = LineReader.Get())
			{
				if(!pLine || pLine[0] == '\0' || pLine[0] == '#')
					continue;
				char aName[128];
				str_copy(aName, pLine);
				str_clean_whitespaces(aName);
				if(aName[0] != '\0')
					m_sBlacklistedGameSkins.insert(aName);
			}
		}
	}
	m_BlacklistLoaded = true;
}

void CAClientGameSkins::SaveBlacklist()
{
	if(!Storage())
		return;

	IOHANDLE File = Storage()->OpenFile("gameskin_blacklist.txt", IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;

	for(const auto &SkinName : m_sBlacklistedGameSkins)
	{
		io_write(File, SkinName.c_str(), SkinName.length());
		io_write_newline(File);
	}
	io_close(File);
}

bool CAClientGameSkins::IsGameSkinBlacklisted(const char *pName) const
{
	if(!pName || !pName[0])
		return false;
	const_cast<CAClientGameSkins *>(this)->EnsureBlacklistLoaded();
	return m_sBlacklistedGameSkins.find(pName) != m_sBlacklistedGameSkins.end();
}

void CAClientGameSkins::SetGameSkinBlacklisted(const char *pName, bool Blacklist)
{
	if(!pName || !pName[0] || str_comp(pName, "default") == 0)
		return;

	EnsureBlacklistLoaded();

	if(Blacklist)
	{
		m_sBlacklistedGameSkins.insert(pName);
		auto it = m_mLoadedGameSkins.find(pName);
		if(it != m_mLoadedGameSkins.end())
		{
			UnloadSkin(it->second);
			m_mLoadedGameSkins.erase(it);
		}
	}
	else
	{
		m_sBlacklistedGameSkins.erase(pName);
	}

	SaveBlacklist();
	UpdateAllPlayerSkins();

	if(Blacklist && str_comp(g_Config.m_ClAssetGame, pName) == 0)
	{
		str_copy(g_Config.m_ClAssetGame, "default");
		GameClient()->LoadGameSkin(g_Config.m_ClAssetGame);
	}
}

void CAClientGameSkins::ToggleGameSkinBlacklist(const char *pName)
{
	EnsureBlacklistLoaded();
	SetGameSkinBlacklisted(pName, !IsGameSkinBlacklisted(pName));
}

void CAClientGameSkins::UpdateActiveSkinUsers()
{
	std::map<std::string, std::vector<std::string>> UsersPerSkin;

	if(Client()->State() == IClient::STATE_ONLINE)
	{
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(!GameClient()->m_aClients[i].m_Active)
				continue;

			const bool IsLocal = (i == GameClient()->m_Snap.m_LocalClientId);
			const char *pPlayerName = GameClient()->m_aClients[i].m_aName;

			if(IsLocal)
			{
				if(g_Config.m_ClAssetGame[0] && str_comp(g_Config.m_ClAssetGame, "default") != 0)
				{
					std::string DisplayName = pPlayerName[0] ? (std::string(pPlayerName) + " (Tú)") : "Tú";
					UsersPerSkin[g_Config.m_ClAssetGame].push_back(DisplayName);
				}
				continue;
			}

			std::string SkinName;
			auto it = m_mRemotePlayerGameSkins.find(pPlayerName);
			if(it == m_mRemotePlayerGameSkins.end())
			{
				char aTrimmed[MAX_NAME_LENGTH];
				TrimPlayerName(pPlayerName, aTrimmed, sizeof(aTrimmed));
				if(aTrimmed[0] != '\0')
					it = m_mRemotePlayerGameSkins.find(aTrimmed);
			}
			if(it != m_mRemotePlayerGameSkins.end() && !it->second.empty())
			{
				SkinName = it->second;
			}
			else if(GameClient()->m_aClients[i].m_aCustomGameSkinName[0] != '\0')
			{
				SkinName = GameClient()->m_aClients[i].m_aCustomGameSkinName;
			}

			if(!SkinName.empty() && str_comp(SkinName.c_str(), "default") != 0)
			{
				UsersPerSkin[SkinName].push_back(pPlayerName);
			}
		}
	}
	else
	{
		if(g_Config.m_ClAssetGame[0] && str_comp(g_Config.m_ClAssetGame, "default") != 0)
		{
			std::string DisplayName = g_Config.m_PlayerName[0] ? (std::string(g_Config.m_PlayerName) + " (Tú)") : "Tú";
			UsersPerSkin[g_Config.m_ClAssetGame].push_back(DisplayName);
		}
	}

	std::map<std::string, std::string> NewResult;
	for(const auto &Pair : UsersPerSkin)
	{
		const auto &Users = Pair.second;
		if(Users.empty())
			continue;

		char aBuf[128];
		if(Users.size() == 1)
			str_format(aBuf, sizeof(aBuf), "%s", Users[0].c_str());
		else if(Users.size() == 2)
			str_format(aBuf, sizeof(aBuf), "%s, %s", Users[0].c_str(), Users[1].c_str());
		else
			str_format(aBuf, sizeof(aBuf), "%s, %s (+%d)", Users[0].c_str(), Users[1].c_str(), (int)Users.size() - 2);

		NewResult[Pair.first] = aBuf;
	}

	if(NewResult != m_mActiveSkinUsers)
	{
		m_mActiveSkinUsers = std::move(NewResult);
		m_SkinUsersRevision++;
	}
}

bool CAClientGameSkins::GetSkinUsers(const char *pSkinName, char *pOutBuf, int OutBufSize, int *pOutCount) const
{
	if(pOutCount)
		*pOutCount = 0;
	if(pOutBuf && OutBufSize > 0)
		pOutBuf[0] = '\0';

	if(!pSkinName || !pSkinName[0])
		return false;

	auto it = m_mActiveSkinUsers.find(pSkinName);
	if(it == m_mActiveSkinUsers.end() || it->second.empty())
		return false;

	if(pOutBuf && OutBufSize > 0)
		str_copy(pOutBuf, it->second.c_str(), OutBufSize);
	if(pOutCount)
		*pOutCount = 1;
	return true;
}

bool CAClientGameSkins::IsGameSkinInUse(const char *pSkinName) const
{
	if(!pSkinName || !pSkinName[0])
		return false;
	return m_mActiveSkinUsers.find(pSkinName) != m_mActiveSkinUsers.end();
}

void CAClientGameSkins::OnStateChange(int NewState, int OldState)
{
	if(NewState == IClient::STATE_ONLINE)
	{
		m_PendingAnnounce = true;
		m_ServerJoinTime = time_get();
		m_InitialDownloadPhase = true;
		m_WasFrozen = false;
	}
	else if(OldState == IClient::STATE_ONLINE || NewState < IClient::STATE_ONLINE)
	{
		// Covers disconnect, switching to demo playback, and quitting (STATE_QUITTING > STATE_ONLINE)
		if(m_aLastServerAddress[0] != '\0')
		{
			if(m_aLastAnnouncedPlayer[0] != '\0')
				SendPresenceLeave(m_aLastServerAddress, m_aLastAnnouncedPlayer);
			if(m_aLastAnnouncedDummy[0] != '\0')
				SendPresenceLeave(m_aLastServerAddress, m_aLastAnnouncedDummy);
		}
		OnReset();
	}
}

void CAClientGameSkins::OnShutdown()
{
	// Game closing: make sure the presence rows are removed (the HTTP engine flushes pending requests on shutdown)
	if(m_aLastServerAddress[0] != '\0')
	{
		if(m_aLastAnnouncedPlayer[0] != '\0')
			SendPresenceLeave(m_aLastServerAddress, m_aLastAnnouncedPlayer);
		if(m_aLastAnnouncedDummy[0] != '\0')
			SendPresenceLeave(m_aLastServerAddress, m_aLastAnnouncedDummy);
		m_aLastServerAddress[0] = '\0';
		m_aLastAnnouncedPlayer[0] = '\0';
		m_aLastAnnouncedDummy[0] = '\0';
	}
}

void CAClientGameSkins::OnReset()
{
	if(m_pPresenceRequest)
	{
		m_pPresenceRequest->Abort();
		m_pPresenceRequest = nullptr;
	}
	if(m_pDownloadRequest)
	{
		m_pDownloadRequest->Abort();
		m_pDownloadRequest = nullptr;
	}
	m_DownloadingSkinName.clear();
	if(m_pDownloadEntitiesRequest)
	{
		m_pDownloadEntitiesRequest->Abort();
		m_pDownloadEntitiesRequest = nullptr;
	}
	m_DownloadingEntitiesName.clear();
	m_sFailedEntities.clear();
	m_mRemotePlayerGameSkins.clear();
	m_sRemoteAClientPlayers.clear();
	m_mActiveSkinUsers.clear();
	m_SkinUsersRevision++;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		GameClient()->m_aClients[i].m_pCustomGameSkin = nullptr;
		GameClient()->m_aClients[i].m_aCustomGameSkinName[0] = '\0';
		GameClient()->m_aClients[i].m_aCustomEntitiesName[0] = '\0';
		GameClient()->m_aClients[i].m_CustomEntitiesTexture = IGraphics::CTextureHandle();
		GameClient()->m_aClients[i].m_IsAClient = false;
	}

	FreeAllGameSkins();
	m_sFailedSkins.clear();
	m_mRemotePlayerEntities.clear();
	m_mManualEntities.clear();
	m_PendingAnnounce = false;
	m_aLastServerAddress[0] = '\0';
	m_aLastAnnouncedPlayer[0] = '\0';
	m_aLastAnnouncedDummy[0] = '\0';
	m_aLastAnnouncedSkin[0] = '\0';
	m_aLastAnnouncedEntities[0] = '\0';
	m_aLastAnnouncedDummyEntities[0] = '\0';
	m_LastActivePlayerCount = 0;
	m_LastPresenceRefreshTime = 0;
	m_LastRenderCheckTime = 0;
	m_ServerJoinTime = 0;
	m_InitialDownloadPhase = false;
	m_WasFrozen = false;
}

void CAClientGameSkins::UnloadSkin(SClientGameSkin &Skin)
{
	if(Skin.m_SpriteHookHead.IsValid())
		Graphics()->UnloadTexture(&Skin.m_SpriteHookHead);
	if(Skin.m_SpriteHookChain.IsValid())
		Graphics()->UnloadTexture(&Skin.m_SpriteHookChain);

	if(Skin.m_SpriteWeaponHammer.IsValid())
		Graphics()->UnloadTexture(&Skin.m_SpriteWeaponHammer);
	if(Skin.m_SpriteWeaponGun.IsValid())
		Graphics()->UnloadTexture(&Skin.m_SpriteWeaponGun);
	if(Skin.m_SpriteWeaponShotgun.IsValid())
		Graphics()->UnloadTexture(&Skin.m_SpriteWeaponShotgun);
	if(Skin.m_SpriteWeaponGrenade.IsValid())
		Graphics()->UnloadTexture(&Skin.m_SpriteWeaponGrenade);
	if(Skin.m_SpriteWeaponLaser.IsValid())
		Graphics()->UnloadTexture(&Skin.m_SpriteWeaponLaser);
	if(Skin.m_SpriteWeaponNinja.IsValid())
		Graphics()->UnloadTexture(&Skin.m_SpriteWeaponNinja);

	if(Skin.m_SpriteWeaponHammerCursor.IsValid())
		Graphics()->UnloadTexture(&Skin.m_SpriteWeaponHammerCursor);
	if(Skin.m_SpriteWeaponGunCursor.IsValid())
		Graphics()->UnloadTexture(&Skin.m_SpriteWeaponGunCursor);
	if(Skin.m_SpriteWeaponShotgunCursor.IsValid())
		Graphics()->UnloadTexture(&Skin.m_SpriteWeaponShotgunCursor);
	if(Skin.m_SpriteWeaponGrenadeCursor.IsValid())
		Graphics()->UnloadTexture(&Skin.m_SpriteWeaponGrenadeCursor);
	if(Skin.m_SpriteWeaponNinjaCursor.IsValid())
		Graphics()->UnloadTexture(&Skin.m_SpriteWeaponNinjaCursor);
	if(Skin.m_SpriteWeaponLaserCursor.IsValid())
		Graphics()->UnloadTexture(&Skin.m_SpriteWeaponLaserCursor);

	for(auto &SpriteWeaponCursor : Skin.m_aSpriteWeaponCursors)
		SpriteWeaponCursor = IGraphics::CTextureHandle();

	for(auto &SpriteWeapon : Skin.m_aSpriteWeapons)
		SpriteWeapon = IGraphics::CTextureHandle();

	for(int i = 0; i < 3; ++i)
	{
		if(Skin.m_aSpriteWeaponGunMuzzles[i].IsValid())
			Graphics()->UnloadTexture(&Skin.m_aSpriteWeaponGunMuzzles[i]);
		if(Skin.m_aSpriteWeaponShotgunMuzzles[i].IsValid())
			Graphics()->UnloadTexture(&Skin.m_aSpriteWeaponShotgunMuzzles[i]);
		if(Skin.m_aaSpriteWeaponNinjaMuzzles[i].IsValid())
			Graphics()->UnloadTexture(&Skin.m_aaSpriteWeaponNinjaMuzzles[i]);
	}

	for(int w = 0; w < 6; ++w)
	{
		for(int m = 0; m < 3; ++m)
			Skin.m_aaSpriteWeaponsMuzzles[w][m] = IGraphics::CTextureHandle();
	}
}

void CAClientGameSkins::FreeAllGameSkins()
{
	for(auto &Pair : m_mLoadedGameSkins)
	{
		UnloadSkin(Pair.second);
	}
	m_mLoadedGameSkins.clear();

	for(auto &Pair : m_mLoadedEntitiesTextures)
	{
		if(Pair.second.IsValid())
			Graphics()->UnloadTexture(&Pair.second);
	}
	m_mLoadedEntitiesTextures.clear();
}

bool CAClientGameSkins::FindGameSkinFile(const char *pName, char *pOutPath, int OutPathSize)
{
	if(!pName || !pName[0])
		return false;

	for(const char *p = pName; *p; ++p)
	{
		if(*p == '/' || *p == '\\' || *p == ':' || *p == '*' || *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|')
			return false;
	}
	if(str_find(pName, ".."))
		return false;

	// 1. assets/game/<name>.png
	str_format(pOutPath, OutPathSize, "assets/game/%s.png", pName);
	if(Storage()->FileExists(pOutPath, IStorage::TYPE_ALL))
		return true;

	// 2. assets/game/<name>/game.png
	str_format(pOutPath, OutPathSize, "assets/game/%s/game.png", pName);
	if(Storage()->FileExists(pOutPath, IStorage::TYPE_ALL))
		return true;

	// 3. downloaded_gameskins/<name>.png
	str_format(pOutPath, OutPathSize, "downloaded_gameskins/%s.png", pName);
	if(Storage()->FileExists(pOutPath, IStorage::TYPE_ALL))
		return true;

	// 4. downloadedgameskins/<name>.png
	str_format(pOutPath, OutPathSize, "downloadedgameskins/%s.png", pName);
	if(Storage()->FileExists(pOutPath, IStorage::TYPE_ALL))
		return true;

	return false;
}

bool CAClientGameSkins::IsLocalPlayerFrozen() const
{
	if(!GameClient())
		return false;
	const int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
		return false;
	return GameClient()->IsPlayerFrozen(LocalId);
}

bool CAClientGameSkins::CanDownloadGameskins() const
{
	if(!g_Config.m_ClDownloadGameskins)
		return false;
	if(Client()->State() != IClient::STATE_ONLINE)
		return false;

	const bool OptInitial = g_Config.m_AcGameskinDownloadInitial != 0;
	const bool OptFrozen = g_Config.m_AcGameskinDownloadFrozen != 0;

	if(!OptInitial && !OptFrozen)
		return true;

	bool InInitial = false;
	if(OptInitial && m_InitialDownloadPhase)
	{
		if(m_ServerJoinTime > 0 && (time_get() - m_ServerJoinTime) < time_freq() * 20)
			InInitial = true;
		else
			const_cast<CAClientGameSkins *>(this)->m_InitialDownloadPhase = false;
	}

	bool IsFrozen = false;
	if(OptFrozen)
	{
		IsFrozen = IsLocalPlayerFrozen();
	}

	if(OptInitial && OptFrozen)
		return InInitial || IsFrozen;
	if(OptInitial)
		return InInitial;
	if(OptFrozen)
		return IsFrozen;

	return true;
}

void CAClientGameSkins::StartDownload(const char *pSkinName)
{
	if(!CanDownloadGameskins())
		return;
	if(m_pDownloadRequest)
		return;
	if(!pSkinName || !pSkinName[0] || str_comp(pSkinName, "default") == 0)
		return;
	if(IsGameSkinBlacklisted(pSkinName))
		return;
	if(m_sFailedSkins.find(pSkinName) != m_sFailedSkins.end())
		return;

	Storage()->CreateFolder("downloaded_gameskins", IStorage::TYPE_SAVE);

	char aEscaped[128];
	EscapeUrl(aEscaped, sizeof(aEscaped), pSkinName);

	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "%s%s.png", g_Config.m_ClGameskinDownloadUrl, aEscaped);

	char aDest[128];
	str_format(aDest, sizeof(aDest), "downloaded_gameskins/%s.png", pSkinName);

	m_pDownloadRequest = HttpGetFile(aUrl, Storage(), aDest, IStorage::TYPE_SAVE);
	m_pDownloadRequest->Timeout(CTimeout{8000, 8000, 500, 8});
	m_pDownloadRequest->IpResolve(IPRESOLVE::V4);
	m_pDownloadRequest->LogProgress(HTTPLOG::NONE);
	m_pDownloadRequest->Header("User-Agent: AClient");
	m_DownloadingSkinName = pSkinName;
	Http()->Run(m_pDownloadRequest);

	if(g_Config.m_AcGameskinDownloadEcho && GameClient())
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "[AClient] Descargando gameskin '%s'...", pSkinName);
		GameClient()->m_Chat.Echo(aBuf);
	}
}

bool CAClientGameSkins::CanDownloadEntities() const
{
	if(!g_Config.m_ClDownloadEntities)
		return false;
	if(Client()->State() != IClient::STATE_ONLINE)
		return false;

	const bool OptInitial = g_Config.m_AcGameskinDownloadInitial != 0;
	const bool OptFrozen = g_Config.m_AcGameskinDownloadFrozen != 0;

	if(!OptInitial && !OptFrozen)
		return true;

	bool InInitial = false;
	if(OptInitial && m_InitialDownloadPhase)
	{
		if(m_ServerJoinTime > 0 && (time_get() - m_ServerJoinTime) < time_freq() * 20)
			InInitial = true;
		else
			const_cast<CAClientGameSkins *>(this)->m_InitialDownloadPhase = false;
	}

	bool IsFrozen = false;
	if(OptFrozen)
	{
		IsFrozen = IsLocalPlayerFrozen();
	}

	if(OptInitial && OptFrozen)
		return InInitial || IsFrozen;
	if(OptInitial)
		return InInitial;
	if(OptFrozen)
		return IsFrozen;

	return true;
}

void CAClientGameSkins::StartEntitiesDownload(const char *pEntitiesName)
{
	if(!CanDownloadEntities())
		return;
	if(m_pDownloadEntitiesRequest)
		return;
	if(!pEntitiesName || !pEntitiesName[0] || str_comp(pEntitiesName, "default") == 0)
		return;

	char aCleanName[128];
	str_copy(aCleanName, pEntitiesName);
	int NameLen = str_length(aCleanName);
	if(NameLen > 4 && str_comp_nocase(aCleanName + NameLen - 4, ".png") == 0)
	{
		aCleanName[NameLen - 4] = '\0';
	}

	if(aCleanName[0] == '\0' || str_comp(aCleanName, "default") == 0)
		return;

	if(m_sFailedEntities.find(aCleanName) != m_sFailedEntities.end())
		return;

	for(const char *p = aCleanName; *p; ++p)
	{
		if(*p == '/' || *p == '\\' || *p == ':' || *p == '*' || *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|')
			return;
	}
	if(str_find(aCleanName, ".."))
		return;

	Storage()->CreateFolder("downloaded_entities", IStorage::TYPE_SAVE);

	char aEscaped[128];
	EscapeUrl(aEscaped, sizeof(aEscaped), aCleanName);

	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "%s%s.png", g_Config.m_ClEntitiesDownloadUrl, aEscaped);

	char aDest[128];
	str_format(aDest, sizeof(aDest), "downloaded_entities/%s.png", aCleanName);

	m_pDownloadEntitiesRequest = HttpGetFile(aUrl, Storage(), aDest, IStorage::TYPE_SAVE);
	m_pDownloadEntitiesRequest->Timeout(CTimeout{8000, 8000, 500, 8});
	m_pDownloadEntitiesRequest->IpResolve(IPRESOLVE::V4);
	m_pDownloadEntitiesRequest->LogProgress(HTTPLOG::NONE);
	m_pDownloadEntitiesRequest->Header("User-Agent: AClient");
	m_DownloadingEntitiesName = aCleanName;
	Http()->Run(m_pDownloadEntitiesRequest);

	if(g_Config.m_AcGameskinDownloadEcho && GameClient())
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "[AClient] Descargando entities '%s'...", aCleanName);
		GameClient()->m_Chat.Echo(aBuf);
	}
}

IGraphics::CTextureHandle CAClientGameSkins::GetOrLoadEntitiesTexture(const char *pName)
{
	if(!pName || !pName[0] || str_comp(pName, "default") == 0)
		return IGraphics::CTextureHandle();

	char aCleanName[128];
	str_copy(aCleanName, pName);
	int NameLen = str_length(aCleanName);
	if(NameLen > 4 && str_comp_nocase(aCleanName + NameLen - 4, ".png") == 0)
	{
		aCleanName[NameLen - 4] = '\0';
	}

	if(aCleanName[0] == '\0' || str_comp(aCleanName, "default") == 0)
		return IGraphics::CTextureHandle();

	auto it = m_mLoadedEntitiesTextures.find(aCleanName);
	if(it != m_mLoadedEntitiesTextures.end())
		return it->second;

	if(m_sFailedEntities.find(aCleanName) != m_sFailedEntities.end())
		return IGraphics::CTextureHandle();

	char aPath[IO_MAX_PATH_LENGTH];
	bool Found = false;

	// 1. assets/entities/<name>/ddnet.png
	str_format(aPath, sizeof(aPath), "assets/entities/%s/ddnet.png", aCleanName);
	if(Storage()->FileExists(aPath, IStorage::TYPE_ALL))
		Found = true;

	// 2. assets/entities/<name>.png
	if(!Found)
	{
		str_format(aPath, sizeof(aPath), "assets/entities/%s.png", aCleanName);
		if(Storage()->FileExists(aPath, IStorage::TYPE_ALL))
			Found = true;
	}

	// 3. assets/entities/<name>/vanilla.png
	if(!Found)
	{
		str_format(aPath, sizeof(aPath), "assets/entities/%s/vanilla.png", aCleanName);
		if(Storage()->FileExists(aPath, IStorage::TYPE_ALL))
			Found = true;
	}

	// 4. downloaded_entities/<name>.png
	if(!Found)
	{
		str_format(aPath, sizeof(aPath), "downloaded_entities/%s.png", aCleanName);
		if(Storage()->FileExists(aPath, IStorage::TYPE_ALL))
			Found = true;
	}

	// 5. downloaded_entities/<name>/ddnet.png
	if(!Found)
	{
		str_format(aPath, sizeof(aPath), "downloaded_entities/%s/ddnet.png", aCleanName);
		if(Storage()->FileExists(aPath, IStorage::TYPE_ALL))
			Found = true;
	}

	if(Found)
	{
		IGraphics::CTextureHandle Tex = Graphics()->LoadTexture(aPath, IStorage::TYPE_ALL);
		if(Tex.IsValid())
		{
			m_mLoadedEntitiesTextures[aCleanName] = Tex;
			return Tex;
		}
		else
		{
			m_sFailedEntities.insert(aCleanName);
		}
	}
	else
	{
		if(g_Config.m_ClDownloadEntities)
			StartEntitiesDownload(aCleanName);
	}

	return IGraphics::CTextureHandle();
}

const SClientGameSkin *CAClientGameSkins::GetOrLoadGameSkin(const char *pName)
{
	if(!pName || !pName[0] || str_comp(pName, "default") == 0)
		return nullptr;
	if(IsGameSkinBlacklisted(pName))
		return nullptr;

	auto it = m_mLoadedGameSkins.find(pName);
	if(it != m_mLoadedGameSkins.end())
		return &it->second;

	if(m_sFailedSkins.find(pName) != m_sFailedSkins.end())
		return nullptr;

	char aPath[IO_MAX_PATH_LENGTH];
	if(!FindGameSkinFile(pName, aPath, sizeof(aPath)))
	{
		if(g_Config.m_ClDownloadGameskins)
			StartDownload(pName);
		return nullptr;
	}

	if(m_mLoadedGameSkins.size() >= 16)
		return nullptr;

	CImageInfo ImgInfo;
	if(!Graphics()->LoadPng(ImgInfo, aPath, IStorage::TYPE_ALL))
	{
		m_sFailedSkins.insert(pName);
		return nullptr;
	}

	CImageInfo DefaultInfo;
	std::optional<CImageInfo> FallbackInfo;
	if(Graphics()->LoadPng(DefaultInfo, g_pData->m_aImages[IMAGE_GAME].m_pFilename, IStorage::TYPE_ALL))
		FallbackInfo = std::move(DefaultInfo);

	if(!Graphics()->CheckImageDivisibility(aPath, ImgInfo, g_pData->m_aSprites[SPRITE_HOOK_HEAD].m_pSet->m_Gridx, g_pData->m_aSprites[SPRITE_HOOK_HEAD].m_pSet->m_Gridy, true) || !Graphics()->IsImageFormatRgba(aPath, ImgInfo))
	{
		ImgInfo.Free();
		if(FallbackInfo.has_value())
			FallbackInfo->Free();
		m_sFailedSkins.insert(pName);
		return nullptr;
	}

	SClientGameSkin &Skin = m_mLoadedGameSkins[pName];

	Skin.m_SpriteHookChain = Graphics()->LoadSpriteTexture(ImgInfo, FallbackInfo, &g_pData->m_aSprites[SPRITE_HOOK_CHAIN]);
	Skin.m_SpriteHookHead = Graphics()->LoadSpriteTexture(ImgInfo, FallbackInfo, &g_pData->m_aSprites[SPRITE_HOOK_HEAD]);

	Skin.m_SpriteWeaponHammer = Graphics()->LoadSpriteTexture(ImgInfo, FallbackInfo, &g_pData->m_aSprites[SPRITE_WEAPON_HAMMER_BODY]);
	Skin.m_SpriteWeaponGun = Graphics()->LoadSpriteTexture(ImgInfo, FallbackInfo, &g_pData->m_aSprites[SPRITE_WEAPON_GUN_BODY]);
	Skin.m_SpriteWeaponShotgun = Graphics()->LoadSpriteTexture(ImgInfo, FallbackInfo, &g_pData->m_aSprites[SPRITE_WEAPON_SHOTGUN_BODY]);
	Skin.m_SpriteWeaponGrenade = Graphics()->LoadSpriteTexture(ImgInfo, FallbackInfo, &g_pData->m_aSprites[SPRITE_WEAPON_GRENADE_BODY]);
	Skin.m_SpriteWeaponLaser = Graphics()->LoadSpriteTexture(ImgInfo, FallbackInfo, &g_pData->m_aSprites[SPRITE_WEAPON_LASER_BODY]);
	Skin.m_SpriteWeaponNinja = Graphics()->LoadSpriteTexture(ImgInfo, FallbackInfo, &g_pData->m_aSprites[SPRITE_WEAPON_NINJA_BODY]);

	Skin.m_aSpriteWeapons[0] = Skin.m_SpriteWeaponHammer;
	Skin.m_aSpriteWeapons[1] = Skin.m_SpriteWeaponGun;
	Skin.m_aSpriteWeapons[2] = Skin.m_SpriteWeaponShotgun;
	Skin.m_aSpriteWeapons[3] = Skin.m_SpriteWeaponGrenade;
	Skin.m_aSpriteWeapons[4] = Skin.m_SpriteWeaponLaser;
	Skin.m_aSpriteWeapons[5] = Skin.m_SpriteWeaponNinja;

	for(int i = 0; i < 3; ++i)
	{
		Skin.m_aSpriteWeaponGunMuzzles[i] = Graphics()->LoadSpriteTexture(ImgInfo, FallbackInfo, &g_pData->m_aSprites[SPRITE_WEAPON_GUN_MUZZLE1 + i]);
		Skin.m_aSpriteWeaponShotgunMuzzles[i] = Graphics()->LoadSpriteTexture(ImgInfo, FallbackInfo, &g_pData->m_aSprites[SPRITE_WEAPON_SHOTGUN_MUZZLE1 + i]);
		Skin.m_aaSpriteWeaponNinjaMuzzles[i] = Graphics()->LoadSpriteTexture(ImgInfo, FallbackInfo, &g_pData->m_aSprites[SPRITE_WEAPON_NINJA_MUZZLE1 + i]);

		Skin.m_aaSpriteWeaponsMuzzles[1][i] = Skin.m_aSpriteWeaponGunMuzzles[i];
		Skin.m_aaSpriteWeaponsMuzzles[2][i] = Skin.m_aSpriteWeaponShotgunMuzzles[i];
		Skin.m_aaSpriteWeaponsMuzzles[5][i] = Skin.m_aaSpriteWeaponNinjaMuzzles[i];
	}

	Skin.m_SpriteWeaponHammerCursor = Graphics()->LoadSpriteTexture(ImgInfo, FallbackInfo, &g_pData->m_aSprites[SPRITE_WEAPON_HAMMER_CURSOR]);
	Skin.m_SpriteWeaponGunCursor = Graphics()->LoadSpriteTexture(ImgInfo, FallbackInfo, &g_pData->m_aSprites[SPRITE_WEAPON_GUN_CURSOR]);
	Skin.m_SpriteWeaponShotgunCursor = Graphics()->LoadSpriteTexture(ImgInfo, FallbackInfo, &g_pData->m_aSprites[SPRITE_WEAPON_SHOTGUN_CURSOR]);
	Skin.m_SpriteWeaponGrenadeCursor = Graphics()->LoadSpriteTexture(ImgInfo, FallbackInfo, &g_pData->m_aSprites[SPRITE_WEAPON_GRENADE_CURSOR]);
	Skin.m_SpriteWeaponNinjaCursor = Graphics()->LoadSpriteTexture(ImgInfo, FallbackInfo, &g_pData->m_aSprites[SPRITE_WEAPON_NINJA_CURSOR]);
	Skin.m_SpriteWeaponLaserCursor = Graphics()->LoadSpriteTexture(ImgInfo, FallbackInfo, &g_pData->m_aSprites[SPRITE_WEAPON_LASER_CURSOR]);

	Skin.m_aSpriteWeaponCursors[0] = Skin.m_SpriteWeaponHammerCursor;
	Skin.m_aSpriteWeaponCursors[1] = Skin.m_SpriteWeaponGunCursor;
	Skin.m_aSpriteWeaponCursors[2] = Skin.m_SpriteWeaponShotgunCursor;
	Skin.m_aSpriteWeaponCursors[3] = Skin.m_SpriteWeaponGrenadeCursor;
	Skin.m_aSpriteWeaponCursors[4] = Skin.m_SpriteWeaponLaserCursor;
	Skin.m_aSpriteWeaponCursors[5] = Skin.m_SpriteWeaponNinjaCursor;

	ImgInfo.Free();
	if(FallbackInfo.has_value())
		FallbackInfo->Free();

	return &Skin;
}

bool CAClientGameSkins::IsPlayerAClient(int ClientId) const
{
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return false;
	if(!GameClient()->m_aClients[ClientId].m_Active)
		return false;
	if(ClientId == GameClient()->m_aLocalIds[0])
		return true;
	if(Client()->DummyConnected() && ClientId == GameClient()->m_aLocalIds[1])
		return true;
	if(GameClient()->m_aClients[ClientId].m_IsAClient)
		return true;
	const char *pName = GameClient()->m_aClients[ClientId].m_aName;
	if(!pName || !pName[0])
		return false;
	if(m_sRemoteAClientPlayers.find(pName) != m_sRemoteAClientPlayers.end())
		return true;
	char aTrimmed[MAX_NAME_LENGTH];
	TrimPlayerName(pName, aTrimmed, sizeof(aTrimmed));
	return aTrimmed[0] != '\0' && m_sRemoteAClientPlayers.find(aTrimmed) != m_sRemoteAClientPlayers.end();
}

void CAClientGameSkins::SendPresenceLeave(const char *pServer, const char *pPlayer)
{
	if(!pServer || !pServer[0] || !pPlayer || !pPlayer[0])
		return;

	const char *pBaseUrl = (g_Config.m_AcPresenceUrl[0] != '\0' && str_find(g_Config.m_AcPresenceUrl, "workers.dev") == nullptr) ? g_Config.m_AcPresenceUrl : AClientBackend::SUPABASE_URL;

	char aEscPlayer[128];
	char aEscServer[128];
	EscapeJson(aEscPlayer, sizeof(aEscPlayer), pPlayer);
	EscapeJson(aEscServer, sizeof(aEscServer), pServer);

	char aPostUrl[512];
	str_format(aPostUrl, sizeof(aPostUrl), "%s/rest/v1/rpc/aclient_leave", pBaseUrl);

	char aPayload[512];
	str_format(aPayload, sizeof(aPayload),
		"{\"p_server\":\"%s\",\"p_player\":\"%s\"}",
		aEscServer, aEscPlayer);

	std::shared_ptr<IHttpRequest> pPost = HttpPostJson(aPostUrl, aPayload);
	pPost->Timeout(CTimeout{2000, 2000, 500, 2});
	pPost->IpResolve(IPRESOLVE::V4);
	pPost->LogProgress(HTTPLOG::NONE);
	pPost->Header(AClientBackend::USER_AGENT_HEADER);
	pPost->Header(AClientBackend::API_KEY_HEADER);
	pPost->Header(AClientBackend::AUTH_BEARER_HEADER);
	Http()->Run(pPost);
}

void CAClientGameSkins::RefreshPresence()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	char aServerAddr[128] = {0};
	net_addr_str(&Client()->ServerAddress(), aServerAddr, sizeof(aServerAddr), true);
	if(!aServerAddr[0])
		return;

	const int LocalId = GameClient()->m_aLocalIds[0];
	if(LocalId < 0 || LocalId >= MAX_CLIENTS || !GameClient()->m_aClients[LocalId].m_Active || GameClient()->m_aClients[LocalId].m_aName[0] == '\0')
		return;

	char aPrimaryPlayer[128];
	TrimPlayerName(GameClient()->m_aClients[LocalId].m_aName, aPrimaryPlayer, sizeof(aPrimaryPlayer));
	if(aPrimaryPlayer[0] == '\0')
		return;

	char aPrimaryDummy[128] = {0};
	if(Client()->DummyConnected())
	{
		const int DummyId = GameClient()->m_aLocalIds[1];
		if(DummyId >= 0 && DummyId < MAX_CLIENTS && GameClient()->m_aClients[DummyId].m_Active && GameClient()->m_aClients[DummyId].m_aName[0] != '\0')
		{
			TrimPlayerName(GameClient()->m_aClients[DummyId].m_aName, aPrimaryDummy, sizeof(aPrimaryDummy));
		}
	}

	const char *pAssetGame = (!g_Config.m_AcShowRemoteGameSkins || g_Config.m_ClAssetGame[0] == '\0') ? "default" : g_Config.m_ClAssetGame;
	const char *pAssetEntities = (g_Config.m_ClAssetsEntities[0] == '\0') ? "default" : g_Config.m_ClAssetsEntities;
	const char *pDummyAssetEntities = (g_Config.m_ClDummyAssetEntities[0] != '\0') ? g_Config.m_ClDummyAssetEntities : pAssetEntities;

	// If server address changed, send leave for previous server
	if(m_aLastServerAddress[0] != '\0' && str_comp(m_aLastServerAddress, aServerAddr) != 0)
	{
		if(m_aLastAnnouncedPlayer[0] != '\0')
			SendPresenceLeave(m_aLastServerAddress, m_aLastAnnouncedPlayer);
		if(m_aLastAnnouncedDummy[0] != '\0')
			SendPresenceLeave(m_aLastServerAddress, m_aLastAnnouncedDummy);
	}
	else if(m_aLastServerAddress[0] != '\0' && m_aLastAnnouncedPlayer[0] != '\0' && str_comp(m_aLastAnnouncedPlayer, aPrimaryPlayer) != 0)
	{
		SendPresenceLeave(m_aLastServerAddress, m_aLastAnnouncedPlayer);
	}

	if(m_aLastServerAddress[0] != '\0' && m_aLastAnnouncedDummy[0] != '\0' && str_comp(m_aLastAnnouncedDummy, aPrimaryDummy) != 0)
	{
		SendPresenceLeave(m_aLastServerAddress, m_aLastAnnouncedDummy);
	}

	str_copy(m_aLastServerAddress, aServerAddr);
	str_copy(m_aLastAnnouncedPlayer, aPrimaryPlayer);
	str_copy(m_aLastAnnouncedDummy, aPrimaryDummy);
	str_copy(m_aLastAnnouncedSkin, pAssetGame);
	str_copy(m_aLastAnnouncedEntities, pAssetEntities);
	str_copy(m_aLastAnnouncedDummyEntities, pDummyAssetEntities);
	m_LastPresenceRefreshTime = time_get();

	const char *pBaseUrl = (g_Config.m_AcPresenceUrl[0] != '\0' && str_find(g_Config.m_AcPresenceUrl, "workers.dev") == nullptr) ? g_Config.m_AcPresenceUrl : AClientBackend::SUPABASE_URL;

	char aEscServer[128];
	EscapeJson(aEscServer, sizeof(aEscServer), aServerAddr);

	char aPostUrl[512];
	str_format(aPostUrl, sizeof(aPostUrl), "%s/rest/v1/rpc/aclient_presence", pBaseUrl);

	auto SendPresencePost = [&](const char *pPlayerNameToAnnounce, const char *pSkinToAnnounce, const char *pEntitiesToAnnounce) {
		if(!pPlayerNameToAnnounce || !pPlayerNameToAnnounce[0])
			return;
		char aEscPlayer[128];
		EscapeJson(aEscPlayer, sizeof(aEscPlayer), pPlayerNameToAnnounce);
		char aEscSkin[128];
		EscapeJson(aEscSkin, sizeof(aEscSkin), pSkinToAnnounce);
		char aEscEntities[128];
		EscapeJson(aEscEntities, sizeof(aEscEntities), pEntitiesToAnnounce);

		char aPayload[512];
		str_format(aPayload, sizeof(aPayload),
			"{\"p_server\":\"%s\",\"p_player\":\"%s\",\"p_asset_game\":\"%s\",\"p_asset_entities\":\"%s\"}",
			aEscServer, aEscPlayer, aEscSkin, aEscEntities);

		std::shared_ptr<IHttpRequest> pPost = HttpPostJson(aPostUrl, aPayload);
		pPost->Timeout(CTimeout{3000, 3000, 500, 3});
		pPost->IpResolve(IPRESOLVE::V4);
		pPost->LogProgress(HTTPLOG::NONE);
		pPost->Header(AClientBackend::USER_AGENT_HEADER);
		pPost->Header(AClientBackend::API_KEY_HEADER);
		pPost->Header(AClientBackend::AUTH_BEARER_HEADER);
		Http()->Run(pPost);
	};

	// 1. Announce player
	SendPresencePost(aPrimaryPlayer, pAssetGame, pAssetEntities);

	// 1b. Announce dummy if connected
	if(aPrimaryDummy[0] != '\0' && str_comp(aPrimaryDummy, aPrimaryPlayer) != 0)
		SendPresencePost(aPrimaryDummy, pAssetGame, pDummyAssetEntities);

	// 2. Fetch remote gameskins and AClient presence via GET
	if(m_pPresenceRequest && !m_pPresenceRequest->Done())
	{
		m_pPresenceRequest->Abort();
	}

	if(g_Config.m_AcShowRemoteGameSkins || g_Config.m_AcShowRemoteEntitiesOnHook)
	{
		char aEscapedServer[256];
		EscapeUrl(aEscapedServer, sizeof(aEscapedServer), aServerAddr);

		int64_t NowEpochMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		int64_t MinUpdatedAt = NowEpochMs - 3600000; // 1 hora

		char aGetUrl[512];
		str_format(aGetUrl, sizeof(aGetUrl), "%s/rest/v1/presence?server=eq.%s&updated_at=gt.%" PRId64 "&select=player,asset_game,asset_entities,updated_at", pBaseUrl, aEscapedServer, MinUpdatedAt);

		m_pPresenceRequest = HttpGet(aGetUrl);
		m_pPresenceRequest->Timeout(CTimeout{4000, 4000, 500, 4});
		m_pPresenceRequest->IpResolve(IPRESOLVE::V4);
		m_pPresenceRequest->LogProgress(HTTPLOG::NONE);
		m_pPresenceRequest->Header(AClientBackend::USER_AGENT_HEADER);
		m_pPresenceRequest->Header(AClientBackend::API_KEY_HEADER);
		m_pPresenceRequest->Header(AClientBackend::AUTH_BEARER_HEADER);
		Http()->Run(m_pPresenceRequest);
	}
	else if(!m_mRemotePlayerGameSkins.empty() || !m_mRemotePlayerEntities.empty())
	{
		m_mRemotePlayerGameSkins.clear();
		m_mRemotePlayerEntities.clear();
		UpdateAllPlayerSkins();
	}
}

void CAClientGameSkins::UpdateAllPlayerSkins()
{
	const int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	const int DummyClientId = Client()->DummyConnected() ? GameClient()->m_aLocalIds[1] : -1;

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(!GameClient()->m_aClients[i].m_Active)
			continue;

		// Manual overrides (set_player_entities) survive presence refreshes
		const char *pManualEnt = nullptr;
		{
			auto itManual = m_mManualEntities.find(GameClient()->m_aClients[i].m_aName);
			if(itManual != m_mManualEntities.end())
				pManualEnt = itManual->second.c_str();
		}

		if(i == LocalClientId)
		{
			// Load local entities texture for hooked tile rendering
			// (when controlling the dummy, the local player is the dummy)
			const char *pLocalEnt = nullptr;
			if(pManualEnt)
				pLocalEnt = pManualEnt;
			else if(i == DummyClientId && g_Config.m_ClDummyAssetEntities[0] != '\0')
				pLocalEnt = g_Config.m_ClDummyAssetEntities;
			else if(g_Config.m_ClAssetsEntities[0] != '\0')
				pLocalEnt = g_Config.m_ClAssetsEntities;
			if(pLocalEnt && str_comp(pLocalEnt, "default") == 0)
				pLocalEnt = nullptr;
			if(pLocalEnt)
			{
				str_copy(GameClient()->m_aClients[i].m_aCustomEntitiesName, pLocalEnt);
				GameClient()->m_aClients[i].m_CustomEntitiesTexture = GetOrLoadEntitiesTexture(pLocalEnt);
			}
			else
			{
				GameClient()->m_aClients[i].m_aCustomEntitiesName[0] = '\0';
				GameClient()->m_aClients[i].m_CustomEntitiesTexture = IGraphics::CTextureHandle();
			}
			continue;
		}

		bool IsAClient = false;
		if(m_sRemoteAClientPlayers.find(GameClient()->m_aClients[i].m_aName) != m_sRemoteAClientPlayers.end())
			IsAClient = true;
		else
		{
			char aTrimmedPl[MAX_NAME_LENGTH];
			TrimPlayerName(GameClient()->m_aClients[i].m_aName, aTrimmedPl, sizeof(aTrimmedPl));
			if(aTrimmedPl[0] != '\0' && m_sRemoteAClientPlayers.find(aTrimmedPl) != m_sRemoteAClientPlayers.end())
				IsAClient = true;
		}
		GameClient()->m_aClients[i].m_IsAClient = IsAClient;

		auto it = m_mRemotePlayerGameSkins.find(GameClient()->m_aClients[i].m_aName);
		if(it == m_mRemotePlayerGameSkins.end())
		{
			char aTrimmed[MAX_NAME_LENGTH];
			TrimPlayerName(GameClient()->m_aClients[i].m_aName, aTrimmed, sizeof(aTrimmed));
			if(aTrimmed[0] != '\0')
				it = m_mRemotePlayerGameSkins.find(aTrimmed);
		}
		if(it != m_mRemotePlayerGameSkins.end() && !it->second.empty() && str_comp(it->second.c_str(), "default") != 0 && !IsGameSkinBlacklisted(it->second.c_str()))
		{
			const SClientGameSkin *pSkin = GetOrLoadGameSkin(it->second.c_str());
			GameClient()->m_aClients[i].m_pCustomGameSkin = pSkin;
			str_copy(GameClient()->m_aClients[i].m_aCustomGameSkinName, it->second.c_str());
		}
		else
		{
			GameClient()->m_aClients[i].m_pCustomGameSkin = nullptr;
			GameClient()->m_aClients[i].m_aCustomGameSkinName[0] = '\0';
		}

		const char *pEntitiesToLoad = nullptr;
		if(pManualEnt)
		{
			pEntitiesToLoad = pManualEnt;
		}
		else if(i == DummyClientId && g_Config.m_ClDummyAssetEntities[0] != '\0')
		{
			pEntitiesToLoad = g_Config.m_ClDummyAssetEntities;
		}
		else
		{
			auto itEnt = m_mRemotePlayerEntities.find(GameClient()->m_aClients[i].m_aName);
			if(itEnt == m_mRemotePlayerEntities.end())
			{
				char aTrimmedEnt[MAX_NAME_LENGTH];
				TrimPlayerName(GameClient()->m_aClients[i].m_aName, aTrimmedEnt, sizeof(aTrimmedEnt));
				if(aTrimmedEnt[0] != '\0')
					itEnt = m_mRemotePlayerEntities.find(aTrimmedEnt);
			}
			if(itEnt != m_mRemotePlayerEntities.end() && !itEnt->second.empty() && str_comp(itEnt->second.c_str(), "default") != 0)
			{
				pEntitiesToLoad = itEnt->second.c_str();
			}
		}

		if(pEntitiesToLoad && pEntitiesToLoad[0] != '\0' && str_comp(pEntitiesToLoad, "default") != 0)
		{
			str_copy(GameClient()->m_aClients[i].m_aCustomEntitiesName, pEntitiesToLoad);
			GameClient()->m_aClients[i].m_CustomEntitiesTexture = GetOrLoadEntitiesTexture(pEntitiesToLoad);
		}
		else
		{
			GameClient()->m_aClients[i].m_aCustomEntitiesName[0] = '\0';
			GameClient()->m_aClients[i].m_CustomEntitiesTexture = IGraphics::CTextureHandle();
		}
	}

	UpdateActiveSkinUsers();

	if(GameClient())
	{
		GameClient()->m_Menus.InvalidateAssetsTab(ASSETS_TAB_GAME);
		GameClient()->m_Menus.InvalidateAssetsTab(ASSETS_TAB_DOWNLOADED_GAME);
	}
}

void CAClientGameSkins::OnRender()
{
	if(m_PendingAnnounce && Client()->State() == IClient::STATE_ONLINE)
	{
		const int LocalId = GameClient()->m_aLocalIds[0];
		char aServerAddr[128] = {0};
		net_addr_str(&Client()->ServerAddress(), aServerAddr, sizeof(aServerAddr), true);
		if(aServerAddr[0] != '\0' && LocalId >= 0 && LocalId < MAX_CLIENTS && GameClient()->m_aClients[LocalId].m_Active && GameClient()->m_aClients[LocalId].m_aName[0] != '\0')
		{
			m_PendingAnnounce = false;
			RefreshPresence();
		}
	}
	else if(Client()->State() == IClient::STATE_ONLINE)
	{
		const int LocalId = GameClient()->m_aLocalIds[0];
		if(LocalId >= 0 && LocalId < MAX_CLIENTS && GameClient()->m_aClients[LocalId].m_Active && GameClient()->m_aClients[LocalId].m_aName[0] != '\0')
		{
			int64_t Now = time_get();
			char aCurrentPlayer[128];
			TrimPlayerName(GameClient()->m_aClients[LocalId].m_aName, aCurrentPlayer, sizeof(aCurrentPlayer));

			char aCurrentDummy[128] = {0};
			if(Client()->DummyConnected())
			{
				const int DummyId = GameClient()->m_aLocalIds[1];
				if(DummyId >= 0 && DummyId < MAX_CLIENTS && GameClient()->m_aClients[DummyId].m_Active && GameClient()->m_aClients[DummyId].m_aName[0] != '\0')
				{
					TrimPlayerName(GameClient()->m_aClients[DummyId].m_aName, aCurrentDummy, sizeof(aCurrentDummy));
				}
			}

			const char *pEffectiveSkin = (!g_Config.m_AcShowRemoteGameSkins || g_Config.m_ClAssetGame[0] == '\0') ? "default" : g_Config.m_ClAssetGame;
			const char *pEffectiveEntities = (g_Config.m_ClAssetsEntities[0] == '\0') ? "default" : g_Config.m_ClAssetsEntities;
			const char *pEffectiveDummyEntities = (g_Config.m_ClDummyAssetEntities[0] != '\0') ? g_Config.m_ClDummyAssetEntities : pEffectiveEntities;
			bool SkinChanged = str_comp(m_aLastAnnouncedSkin, pEffectiveSkin) != 0;
			bool EntitiesChanged = str_comp(m_aLastAnnouncedEntities, pEffectiveEntities) != 0;
			bool DummyEntitiesChanged = str_comp(m_aLastAnnouncedDummyEntities, pEffectiveDummyEntities) != 0;
			bool NameChanged = aCurrentPlayer[0] != '\0' && str_comp(m_aLastAnnouncedPlayer, aCurrentPlayer) != 0;
			bool DummyChanged = str_comp(m_aLastAnnouncedDummy, aCurrentDummy) != 0;
			bool PeriodicRefresh = (m_LastPresenceRefreshTime == 0) || ((Now - m_LastPresenceRefreshTime) > time_freq() * 10);

			int RosterSignature = 0;
			for(int i = 0; i < MAX_CLIENTS; ++i)
			{
				if(GameClient()->m_aClients[i].m_Active && GameClient()->m_aClients[i].m_aName[0] != '\0')
				{
					RosterSignature = RosterSignature * 31 + (int)str_quickhash(GameClient()->m_aClients[i].m_aName);
				}
			}
			bool RosterChanged = (RosterSignature != m_LastActivePlayerCount) && ((Now - m_LastPresenceRefreshTime) > time_freq() * 2);
			if(RosterChanged)
				m_LastActivePlayerCount = RosterSignature;

			if(SkinChanged || EntitiesChanged || DummyEntitiesChanged || NameChanged || DummyChanged || PeriodicRefresh || RosterChanged)
			{
				RefreshPresence();
			}
		}
	}

	if(m_pPresenceRequest && m_pPresenceRequest->Done())
	{
		if(m_pPresenceRequest->State() == EHttpState::DONE && m_pPresenceRequest->StatusCode() == 200)
		{
			json_value *pJson = m_pPresenceRequest->ResultJson();
			if(pJson)
			{
				const json_value *pArr = nullptr;
				if(pJson->type == json_array)
					pArr = pJson;
				else if(pJson->type == json_object)
				{
					pArr = json_object_get(pJson, "players");
					if(pArr && pArr->type != json_array)
						pArr = nullptr;
				}

				if(pArr)
				{
					std::map<std::string, std::string> NewSkins;
					std::map<std::string, std::string> NewEntities;
					std::set<std::string> NewAClientPlayers;
					int64_t NowEpochMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
					int64_t MinUpdatedAt = NowEpochMs - 3600000; // 1 hora
					int Len = json_array_length(pArr);
					for(int i = 0; i < Len; ++i)
					{
						const json_value *pItem = json_array_get(pArr, i);
						if(!pItem || pItem->type != json_object)
							continue;
						const json_value *pUp = json_object_get(pItem, "updated_at");
						if(pUp && (pUp->type == json_integer || pUp->type == json_double))
						{
							int64_t UpdatedAt = (pUp->type == json_integer) ? (int64_t)pUp->u.integer : (int64_t)pUp->u.dbl;
							if(UpdatedAt < MinUpdatedAt)
								continue;
						}
						const json_value *pPl = json_object_get(pItem, "player");
						const json_value *pAs = json_object_get(pItem, "asset_game");
						const json_value *pEnt = json_object_get(pItem, "asset_entities");
						if(pPl && pPl->type == json_string)
						{
							const char *pPlName = json_string_get(pPl);
							if(pPlName && pPlName[0])
							{
								char aTrimmedPl[128];
								TrimPlayerName(pPlName, aTrimmedPl, sizeof(aTrimmedPl));
								NewAClientPlayers.insert(pPlName);
								if(aTrimmedPl[0] != '\0')
									NewAClientPlayers.insert(aTrimmedPl);

								if(pAs && pAs->type == json_string)
								{
									const char *pSkinName = json_string_get(pAs);
									if(pSkinName && pSkinName[0])
									{
										NewSkins[pPlName] = pSkinName;
										if(aTrimmedPl[0] != '\0')
											NewSkins[aTrimmedPl] = pSkinName;
									}
								}
								if(pEnt && pEnt->type == json_string)
								{
									const char *pEntName = json_string_get(pEnt);
									if(pEntName && pEntName[0])
									{
										NewEntities[pPlName] = pEntName;
										if(aTrimmedPl[0] != '\0')
											NewEntities[aTrimmedPl] = pEntName;
									}
								}
							}
						}
					}
					m_sRemoteAClientPlayers = std::move(NewAClientPlayers);
					m_mRemotePlayerGameSkins = std::move(NewSkins);
					m_mRemotePlayerEntities = std::move(NewEntities);
					UpdateAllPlayerSkins();
				}
				json_value_free(pJson);
			}
		}
		m_pPresenceRequest = nullptr;
	}

	if(m_pDownloadRequest && m_pDownloadRequest->Done())
	{
		if(m_pDownloadRequest->State() == EHttpState::DONE && m_pDownloadRequest->StatusCode() == 200)
		{
			if(!m_DownloadingSkinName.empty())
			{
				if(g_Config.m_AcGameskinDownloadEcho && GameClient())
				{
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "[AClient] Gameskin '%s' descargado.", m_DownloadingSkinName.c_str());
					GameClient()->m_Chat.Echo(aBuf);
				}
				GetOrLoadGameSkin(m_DownloadingSkinName.c_str());
				UpdateAllPlayerSkins();
			}
		}
		else
		{
			if(!m_DownloadingSkinName.empty())
			{
				if(g_Config.m_AcGameskinDownloadEcho && GameClient())
				{
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "[AClient] Error al descargar gameskin '%s'.", m_DownloadingSkinName.c_str());
					GameClient()->m_Chat.Echo(aBuf);
				}
				m_sFailedSkins.insert(m_DownloadingSkinName);
			}
		}
		m_pDownloadRequest = nullptr;
		m_DownloadingSkinName.clear();
	}

	if(m_pDownloadEntitiesRequest && m_pDownloadEntitiesRequest->Done())
	{
		if(m_pDownloadEntitiesRequest->State() == EHttpState::DONE && m_pDownloadEntitiesRequest->StatusCode() == 200)
		{
			if(!m_DownloadingEntitiesName.empty())
			{
				if(g_Config.m_AcGameskinDownloadEcho && GameClient())
				{
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "[AClient] Entities '%s' descargadas.", m_DownloadingEntitiesName.c_str());
					GameClient()->m_Chat.Echo(aBuf);
				}
				GetOrLoadEntitiesTexture(m_DownloadingEntitiesName.c_str());
				UpdateAllPlayerSkins();
			}
		}
		else
		{
			if(!m_DownloadingEntitiesName.empty())
			{
				if(g_Config.m_AcGameskinDownloadEcho && GameClient())
				{
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "[AClient] Error al descargar entities '%s'.", m_DownloadingEntitiesName.c_str());
					GameClient()->m_Chat.Echo(aBuf);
				}
				m_sFailedEntities.insert(m_DownloadingEntitiesName);
			}
		}
		m_pDownloadEntitiesRequest = nullptr;
		m_DownloadingEntitiesName.clear();
	}

	bool CurrentlyFrozen = IsLocalPlayerFrozen();
	bool JustGotFrozen = CurrentlyFrozen && !m_WasFrozen;
	m_WasFrozen = CurrentlyFrozen;
	if(JustGotFrozen && (m_pDownloadRequest == nullptr || m_pDownloadEntitiesRequest == nullptr))
	{
		m_LastRenderCheckTime = 0;
	}

	int64_t Now = time_get();
	if(Client()->State() == IClient::STATE_ONLINE && (Now - m_LastRenderCheckTime) > time_freq() / 2)
	{
		m_LastRenderCheckTime = Now;
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(!GameClient()->m_aClients[i].m_Active || i == GameClient()->m_Snap.m_LocalClientId)
				continue;

			if(g_Config.m_AcShowRemoteGameSkins && GameClient()->m_aClients[i].m_pCustomGameSkin == nullptr && GameClient()->m_aClients[i].m_aCustomGameSkinName[0] == '\0')
			{
				auto it = m_mRemotePlayerGameSkins.find(GameClient()->m_aClients[i].m_aName);
				if(it == m_mRemotePlayerGameSkins.end())
				{
					char aTrimmed[MAX_NAME_LENGTH];
					TrimPlayerName(GameClient()->m_aClients[i].m_aName, aTrimmed, sizeof(aTrimmed));
					if(aTrimmed[0] != '\0')
						it = m_mRemotePlayerGameSkins.find(aTrimmed);
				}
				if(it != m_mRemotePlayerGameSkins.end() && !it->second.empty() && str_comp(it->second.c_str(), "default") != 0)
				{
					if(IsGameSkinBlacklisted(it->second.c_str()))
					{
						str_copy(GameClient()->m_aClients[i].m_aCustomGameSkinName, it->second.c_str());
						continue;
					}
					const SClientGameSkin *pSkin = GetOrLoadGameSkin(it->second.c_str());
					if(pSkin)
					{
						GameClient()->m_aClients[i].m_pCustomGameSkin = pSkin;
						str_copy(GameClient()->m_aClients[i].m_aCustomGameSkinName, it->second.c_str());
					}
					else if(m_sFailedSkins.find(it->second) != m_sFailedSkins.end())
					{
						str_copy(GameClient()->m_aClients[i].m_aCustomGameSkinName, it->second.c_str());
					}
				}
			}

			if(g_Config.m_ClDownloadEntities && GameClient()->m_aClients[i].m_aCustomEntitiesName[0] != '\0' && !GameClient()->m_aClients[i].m_CustomEntitiesTexture.IsValid())
			{
				if(m_sFailedEntities.find(GameClient()->m_aClients[i].m_aCustomEntitiesName) == m_sFailedEntities.end())
				{
					GameClient()->m_aClients[i].m_CustomEntitiesTexture = GetOrLoadEntitiesTexture(GameClient()->m_aClients[i].m_aCustomEntitiesName);
				}
			}
		}
	}
}

void CAClientGameSkins::TriggerManualReload()
{
	m_ServerJoinTime = time_get();
	m_InitialDownloadPhase = true;
	RefreshPresence();
}

void CAClientGameSkins::ConReloadRemoteGameSkins(IConsole::IResult *pResult, void *pUserData)
{
	CAClientGameSkins *pSelf = (CAClientGameSkins *)pUserData;
	pSelf->TriggerManualReload();
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "aclient", "Consultando gameskins y entities remotos...");
	return;
}

void CAClientGameSkins::ConReloadRemoteEntities(IConsole::IResult *pResult, void *pUserData)
{
	CAClientGameSkins *pSelf = (CAClientGameSkins *)pUserData;
	pSelf->TriggerManualReload();
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "aclient", "Consultando entities y gameskins remotos...");
	return;
}

void CAClientGameSkins::ConSetPlayerGameSkin(IConsole::IResult *pResult, void *pUserData)
{
	CAClientGameSkins *pSelf = (CAClientGameSkins *)pUserData;
	int ClientId = pResult->GetInteger(0);
	const char *pSkinName = pResult->GetString(1);
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "aclient", "ClientId fuera de rango");
		return;
	}
	const SClientGameSkin *pSkin = pSelf->GetOrLoadGameSkin(pSkinName);
	pSelf->GameClient()->m_aClients[ClientId].m_pCustomGameSkin = pSkin;
	str_copy(pSelf->GameClient()->m_aClients[ClientId].m_aCustomGameSkinName, pSkinName);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Asignado gameskin '%s' a cliente %d (cargado: %s)", pSkinName, ClientId, pSkin ? "si" : "no (fallback default)");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "aclient", aBuf);
}

void CAClientGameSkins::ConSetPlayerEntities(IConsole::IResult *pResult, void *pUserData)
{
	CAClientGameSkins *pSelf = (CAClientGameSkins *)pUserData;
	int ClientId = pResult->GetInteger(0);
	const char *pEntitiesName = pResult->GetString(1);
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "aclient", "ClientId fuera de rango");
		return;
	}
	IGraphics::CTextureHandle Tex = pSelf->GetOrLoadEntitiesTexture(pEntitiesName);
	pSelf->GameClient()->m_aClients[ClientId].m_CustomEntitiesTexture = Tex;
	str_copy(pSelf->GameClient()->m_aClients[ClientId].m_aCustomEntitiesName, pEntitiesName);
	pSelf->m_mManualEntities[pSelf->GameClient()->m_aClients[ClientId].m_aName] = pEntitiesName;
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "Asignadas entities '%s' a cliente %d (cargado: %s)", pEntitiesName, ClientId, Tex.IsValid() ? "si" : "no / descargando");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "aclient", aBuf);
}


void CAClientGameSkins::ConBlacklistGameSkin(IConsole::IResult *pResult, void *pUserData)
{
	CAClientGameSkins *pSelf = (CAClientGameSkins *)pUserData;
	const char *pSkinName = pResult->GetString(0);
	pSelf->SetGameSkinBlacklisted(pSkinName, true);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Gameskin '%s' agregado a la blacklist", pSkinName);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "aclient", aBuf);
}

void CAClientGameSkins::ConUnblacklistGameSkin(IConsole::IResult *pResult, void *pUserData)
{
	CAClientGameSkins *pSelf = (CAClientGameSkins *)pUserData;
	const char *pSkinName = pResult->GetString(0);
	pSelf->SetGameSkinBlacklisted(pSkinName, false);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Gameskin '%s' removido de la blacklist", pSkinName);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "aclient", aBuf);
}

void CAClientGameSkins::ConToggleBlacklistGameSkin(IConsole::IResult *pResult, void *pUserData)
{
	CAClientGameSkins *pSelf = (CAClientGameSkins *)pUserData;
	const char *pSkinName = pResult->GetString(0);
	pSelf->ToggleGameSkinBlacklist(pSkinName);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "Gameskin '%s' blacklist: %s", pSkinName, pSelf->IsGameSkinBlacklisted(pSkinName) ? "activado" : "desactivado");
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "aclient", aBuf);
}

void CAClientGameSkins::OnConsoleInit()
{
	Console()->Register("reload_remote_gameskins", "", CFGFLAG_CLIENT, ConReloadRemoteGameSkins, this, "Reload custom gameskins for remote players from relay");
	Console()->Register("reload_remote_entities", "", CFGFLAG_CLIENT, ConReloadRemoteEntities, this, "Reload custom entities and gameskins for remote players from relay");
	Console()->Register("set_player_gameskin", "i[id] s[skin]", CFGFLAG_CLIENT, ConSetPlayerGameSkin, this, "Set custom gameskin for a specific player (for testing)");
	Console()->Register("set_player_entities", "i[id] s[entities]", CFGFLAG_CLIENT, ConSetPlayerEntities, this, "Set custom entities for a specific player (for testing)");
	Console()->Register("cl_blacklist_gameskin", "s[skin]", CFGFLAG_CLIENT, ConBlacklistGameSkin, this, "Blacklist a gameskin");
	Console()->Register("cl_unblacklist_gameskin", "s[skin]", CFGFLAG_CLIENT, ConUnblacklistGameSkin, this, "Unblacklist a gameskin");
	Console()->Register("cl_toggle_blacklist_gameskin", "s[skin]", CFGFLAG_CLIENT, ConToggleBlacklistGameSkin, this, "Toggle blacklist for a gameskin");
}
