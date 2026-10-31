#include "aclient_gameskins.h"

#include <base/math.h>
#include <base/str.h>
#include <base/time.h>

#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>
#include <game/client/gameclient.h>
#include <generated/client_data.h>

CAClientGameSkins::CAClientGameSkins() :
	m_PendingAnnounce(false),
	m_LastPresenceRefreshTime(0),
	m_LastRenderCheckTime(0)
{
	m_aLastServerAddress[0] = '\0';
	m_aLastAnnouncedPlayer[0] = '\0';
	m_aLastAnnouncedSkin[0] = '\0';
}

CAClientGameSkins::~CAClientGameSkins()
{
	if(m_pPresenceRequest)
		m_pPresenceRequest->Abort();
	if(m_pDownloadRequest)
		m_pDownloadRequest->Abort();
	FreeAllGameSkins();
}

void CAClientGameSkins::OnStateChange(int NewState, int OldState)
{
	if(NewState == IClient::STATE_ONLINE)
	{
		m_PendingAnnounce = true;
	}
	else if(NewState < IClient::STATE_ONLINE)
	{
		OnReset();
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
	m_mRemotePlayerGameSkins.clear();

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		GameClient()->m_aClients[i].m_pCustomGameSkin = nullptr;
		GameClient()->m_aClients[i].m_aCustomGameSkinName[0] = '\0';
	}

	FreeAllGameSkins();
	m_sFailedSkins.clear();
	m_PendingAnnounce = false;
	m_aLastServerAddress[0] = '\0';
	m_aLastAnnouncedPlayer[0] = '\0';
	m_aLastAnnouncedSkin[0] = '\0';
	m_LastPresenceRefreshTime = 0;
	m_LastRenderCheckTime = 0;
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

void CAClientGameSkins::StartDownload(const char *pSkinName)
{
	if(!g_Config.m_ClDownloadGameskins)
		return;
	if(m_pDownloadRequest)
		return;
	if(!pSkinName || !pSkinName[0] || str_comp(pSkinName, "default") == 0)
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
	m_pDownloadRequest->Header("User-Agent: AClient");
	m_DownloadingSkinName = pSkinName;
	Http()->Run(m_pDownloadRequest);
}

const SClientGameSkin *CAClientGameSkins::GetOrLoadGameSkin(const char *pName)
{
	if(!pName || !pName[0] || str_comp(pName, "default") == 0)
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

	ImgInfo.Free();
	if(FallbackInfo.has_value())
		FallbackInfo->Free();

	return &Skin;
}

void CAClientGameSkins::RefreshPresence()
{
	if(Client()->State() != IClient::STATE_ONLINE)
		return;

	char aServerAddr[128] = {0};
	net_addr_str(&Client()->ServerAddress(), aServerAddr, sizeof(aServerAddr), true);
	if(!aServerAddr[0])
		return;

	str_copy(m_aLastServerAddress, aServerAddr);
	str_copy(m_aLastAnnouncedPlayer, g_Config.m_PlayerName);
	str_copy(m_aLastAnnouncedSkin, g_Config.m_ClAssetGame);
	m_LastPresenceRefreshTime = time_get();

	if(!g_Config.m_AcPresenceUrl[0])
		return;

	// 1. Announce own presence via POST
	if(g_Config.m_PlayerName[0] != '\0')
	{
		char aEscPlayer[128];
		char aEscSkin[128];
		char aEscServer[128];
		EscapeJson(aEscPlayer, sizeof(aEscPlayer), g_Config.m_PlayerName);
		EscapeJson(aEscSkin, sizeof(aEscSkin), g_Config.m_ClAssetGame);
		EscapeJson(aEscServer, sizeof(aEscServer), aServerAddr);

		char aPayload[512];
		str_format(aPayload, sizeof(aPayload),
			"{\"server\":\"%s\",\"player\":\"%s\",\"asset_game\":\"%s\"}",
			aEscServer, aEscPlayer, aEscSkin);

		std::shared_ptr<IHttpRequest> pPost = HttpPostJson(g_Config.m_AcPresenceUrl, aPayload);
		pPost->Timeout(CTimeout{3000, 3000, 500, 3});
		pPost->IpResolve(IPRESOLVE::V4);
		pPost->Header("User-Agent: AClient");
		Http()->Run(pPost);
	}

	// 2. Fetch remote gameskins via GET
	if(m_pPresenceRequest && !m_pPresenceRequest->Done())
	{
		m_pPresenceRequest->Abort();
	}

	char aEscapedServer[256];
	EscapeUrl(aEscapedServer, sizeof(aEscapedServer), aServerAddr);

	char aGetUrl[512];
	str_format(aGetUrl, sizeof(aGetUrl), "%s?server=%s", g_Config.m_AcPresenceUrl, aEscapedServer);

	m_pPresenceRequest = HttpGet(aGetUrl);
	m_pPresenceRequest->Timeout(CTimeout{4000, 4000, 500, 4});
	m_pPresenceRequest->IpResolve(IPRESOLVE::V4);
	m_pPresenceRequest->Header("User-Agent: AClient");
	Http()->Run(m_pPresenceRequest);
}

void CAClientGameSkins::UpdateAllPlayerSkins()
{
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(!GameClient()->m_aClients[i].m_Active)
			continue;

		if(i == GameClient()->m_Snap.m_LocalClientId)
			continue;

		auto it = m_mRemotePlayerGameSkins.find(GameClient()->m_aClients[i].m_aName);
		if(it != m_mRemotePlayerGameSkins.end() && !it->second.empty() && str_comp(it->second.c_str(), "default") != 0)
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
	}
}

void CAClientGameSkins::OnRender()
{
	if(m_PendingAnnounce && Client()->State() == IClient::STATE_ONLINE)
	{
		char aServerAddr[128] = {0};
		net_addr_str(&Client()->ServerAddress(), aServerAddr, sizeof(aServerAddr), true);
		if(aServerAddr[0] != '\0')
		{
			m_PendingAnnounce = false;
			RefreshPresence();
		}
	}
	else if(Client()->State() == IClient::STATE_ONLINE && g_Config.m_AcShowRemoteGameSkins)
	{
		int64_t Now = time_get();
		bool SkinChanged = str_comp(m_aLastAnnouncedSkin, g_Config.m_ClAssetGame) != 0;
		bool NameChanged = str_comp(m_aLastAnnouncedPlayer, g_Config.m_PlayerName) != 0;
		bool PeriodicRefresh = (m_LastPresenceRefreshTime == 0) || ((Now - m_LastPresenceRefreshTime) > time_freq() * 45);

		if(SkinChanged || NameChanged || PeriodicRefresh)
		{
			RefreshPresence();
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
					int Len = json_array_length(pArr);
					for(int i = 0; i < Len; ++i)
					{
						const json_value *pItem = json_array_get(pArr, i);
						if(!pItem || pItem->type != json_object)
							continue;
						const json_value *pPl = json_object_get(pItem, "player");
						const json_value *pAs = json_object_get(pItem, "asset_game");
						if(pPl && pAs)
						{
							const char *pPlName = json_string_get(pPl);
							const char *pSkinName = json_string_get(pAs);
							if(pPlName && pSkinName && pPlName[0] && pSkinName[0])
							{
								if(str_comp(pPlName, g_Config.m_PlayerName) != 0)
								{
									NewSkins[pPlName] = pSkinName;
								}
							}
						}
					}
					m_mRemotePlayerGameSkins = std::move(NewSkins);
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
				GetOrLoadGameSkin(m_DownloadingSkinName.c_str());
				UpdateAllPlayerSkins();
			}
		}
		else
		{
			if(!m_DownloadingSkinName.empty())
				m_sFailedSkins.insert(m_DownloadingSkinName);
		}
		m_pDownloadRequest = nullptr;
		m_DownloadingSkinName.clear();
	}

	int64_t Now = time_get();
	if(g_Config.m_AcShowRemoteGameSkins && Client()->State() == IClient::STATE_ONLINE && (Now - m_LastRenderCheckTime) > time_freq() / 2)
	{
		m_LastRenderCheckTime = Now;
		for(int i = 0; i < MAX_CLIENTS; ++i)
		{
			if(!GameClient()->m_aClients[i].m_Active || i == GameClient()->m_Snap.m_LocalClientId)
				continue;

			if(GameClient()->m_aClients[i].m_pCustomGameSkin == nullptr && GameClient()->m_aClients[i].m_aCustomGameSkinName[0] == '\0')
			{
				auto it = m_mRemotePlayerGameSkins.find(GameClient()->m_aClients[i].m_aName);
				if(it != m_mRemotePlayerGameSkins.end() && !it->second.empty() && str_comp(it->second.c_str(), "default") != 0)
				{
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
		}
	}
}

void CAClientGameSkins::ConReloadRemoteGameSkins(IConsole::IResult *pResult, void *pUserData)
{
	CAClientGameSkins *pSelf = (CAClientGameSkins *)pUserData;
	pSelf->RefreshPresence();
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "aclient", "Consultando gameskins remotos...");
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

void CAClientGameSkins::OnConsoleInit()
{
	Console()->Register("reload_remote_gameskins", "", CFGFLAG_CLIENT, ConReloadRemoteGameSkins, this, "Reload custom gameskins for remote players from relay");
	Console()->Register("set_player_gameskin", "i[id] s[skin]", CFGFLAG_CLIENT, ConSetPlayerGameSkin, this, "Set custom gameskin for a specific player (for testing)");
}
