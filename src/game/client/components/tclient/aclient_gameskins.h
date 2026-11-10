#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_GAMESKINS_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_GAMESKINS_H

#include <engine/console.h>
#include <engine/http.h>
#include <game/client/component.h>
#include <game/client/gameskin.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>

class CAClientGameSkins : public CComponent
{
	std::map<std::string, SClientGameSkin> m_mLoadedGameSkins;
	std::map<std::string, std::string> m_mRemotePlayerGameSkins;
	std::map<std::string, std::string> m_mRemotePlayerEntities;
	std::map<std::string, std::string> m_mManualEntities;
	std::map<std::string, IGraphics::CTextureHandle> m_mLoadedEntitiesTextures;
	std::set<std::string> m_sRemoteAClientPlayers;
	std::set<std::string> m_sFailedSkins;
	std::unordered_set<std::string> m_sBlacklistedGameSkins;
	std::map<std::string, std::string> m_mActiveSkinUsers;
	uint64_t m_SkinUsersRevision;

	std::shared_ptr<IHttpRequest> m_pPresenceRequest;
	std::shared_ptr<IHttpRequest> m_pDownloadRequest;
	std::string m_DownloadingSkinName;

	std::shared_ptr<IHttpRequest> m_pDownloadEntitiesRequest;
	std::string m_DownloadingEntitiesName;
	std::set<std::string> m_sFailedEntities;

	bool m_PendingAnnounce;
	char m_aLastServerAddress[128];
	char m_aLastAnnouncedPlayer[128];
	char m_aLastAnnouncedDummy[128];
	char m_aLastAnnouncedSkin[128];
	char m_aLastAnnouncedEntities[128];
	char m_aLastAnnouncedDummyEntities[128];
	int m_LastActivePlayerCount;
	int64_t m_LastPresenceRefreshTime;
	int64_t m_LastRenderCheckTime;
	int64_t m_ServerJoinTime;
	bool m_InitialDownloadPhase;
	bool m_WasFrozen;
	bool m_BlacklistLoaded;

	bool CanDownloadGameskins() const;
	bool CanDownloadEntities() const;
	bool IsLocalPlayerFrozen() const;

	bool FindGameSkinFile(const char *pName, char *pOutPath, int OutPathSize);
	void UnloadSkin(SClientGameSkin &Skin);
	void StartDownload(const char *pSkinName);
	void StartEntitiesDownload(const char *pEntitiesName);

	void LoadBlacklist();
	void SaveBlacklist();
	void EnsureBlacklistLoaded();

	static void ConReloadRemoteGameSkins(IConsole::IResult *pResult, void *pUserData);
	static void ConReloadRemoteEntities(IConsole::IResult *pResult, void *pUserData);
	static void ConSetPlayerGameSkin(IConsole::IResult *pResult, void *pUserData);
	static void ConSetPlayerEntities(IConsole::IResult *pResult, void *pUserData);
	static void ConBlacklistGameSkin(IConsole::IResult *pResult, void *pUserData);
	static void ConUnblacklistGameSkin(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleBlacklistGameSkin(IConsole::IResult *pResult, void *pUserData);

public:
	CAClientGameSkins();
	~CAClientGameSkins() override;

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnStateChange(int NewState, int OldState) override;
	void OnRender() override;
	void OnConsoleInit() override;
	void OnReset() override;
	void OnShutdown() override;

	void SendPresenceLeave(const char *pServer, const char *pPlayer);
	void RefreshPresence();
	bool IsPlayerAClient(int ClientId) const;
	const SClientGameSkin *GetOrLoadGameSkin(const char *pName);
	IGraphics::CTextureHandle GetOrLoadEntitiesTexture(const char *pName);
	void FreeAllGameSkins();
	void UpdateAllPlayerSkins();
	void TriggerManualReload();

	bool IsGameSkinBlacklisted(const char *pName) const;
	void SetGameSkinBlacklisted(const char *pName, bool Blacklist);
	void ToggleGameSkinBlacklist(const char *pName);

	void UpdateActiveSkinUsers();
	const std::map<std::string, std::string> &GetActiveSkinUsers() const { return m_mActiveSkinUsers; }
	uint64_t SkinUsersRevision() const { return m_SkinUsersRevision; }
	bool GetSkinUsers(const char *pSkinName, char *pOutBuf, int OutBufSize, int *pOutCount = nullptr) const;
	bool IsGameSkinInUse(const char *pSkinName) const;
};

#endif // GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_GAMESKINS_H
