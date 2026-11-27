#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_GAMESKINS_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_GAMESKINS_H

#include <base/types.h>
#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/http.h>
#include <engine/shared/protocol.h>
#include <game/client/component.h>
#include <game/client/gameskin.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

class CAClientGameSkins : public CComponent
{
	struct SStringHash
	{
		using is_transparent = void;
		size_t operator()(std::string_view Sv) const noexcept { return std::hash<std::string_view>{}(Sv); }
		size_t operator()(const std::string &S) const noexcept { return std::hash<std::string_view>{}(S); }
		size_t operator()(const char *pS) const noexcept { return std::hash<std::string_view>{}(pS ? std::string_view(pS) : std::string_view()); }
	};

	template<typename V>
	using TStringMap = std::unordered_map<std::string, V, SStringHash, std::equal_to<>>;
	using TStringSet = std::unordered_set<std::string, SStringHash, std::equal_to<>>;

	TStringMap<SClientGameSkin> m_mLoadedGameSkins;
	TStringMap<std::string> m_mRemotePlayerGameSkins;
	TStringMap<std::string> m_mRemotePlayerEntities;
	TStringMap<std::string> m_mManualEntities;
	TStringMap<IGraphics::CTextureHandle> m_mLoadedEntitiesTextures;
	TStringSet m_sRemoteAClientPlayers;
	TStringSet m_sFailedSkins;
	TStringSet m_sBlacklistedGameSkins;
	TStringSet m_sBlacklistedEntities;
	std::map<std::string, std::string> m_mActiveSkinUsers;
	uint64_t m_SkinUsersRevision;

	std::shared_ptr<IHttpRequest> m_pPresenceRequest;
	std::shared_ptr<IHttpRequest> m_pDownloadRequest;
	std::string m_DownloadingSkinName;

	std::shared_ptr<IHttpRequest> m_pDownloadEntitiesRequest;
	std::string m_DownloadingEntitiesName;
	TStringSet m_sFailedEntities;
	TStringSet m_sMissingOnDiskSkins;
	TStringSet m_sMissingOnDiskEntities;
	uint64_t m_AssetsScanVersion = 1;
	uint64_t m_aSlotScanVersion[MAX_CLIENTS] = {};
	unsigned m_aSlotScanNameHash[MAX_CLIENTS] = {};
	int m_LastShowRemoteGameSkins = -1;
	int m_LastDownloadGameskinsCfg = -1;
	int m_LastDownloadEntitiesCfg = -1;
	std::optional<CImageInfo> m_FallbackGameImage;
	struct SPendingGameSkinLoad
	{
		bool m_Active = false;
		std::string m_Name;
		char m_aPath[IO_MAX_PATH_LENGTH] = {};
		CImageInfo m_ImgInfo;
		SClientGameSkin m_Skin = {};
		int m_Step = -1;
	};
	SPendingGameSkinLoad m_PendingSkinLoad;
	int64_t m_LastDownloadFinishTime = 0;
	int64_t m_LastGpuLoadTime = 0;

	bool m_PendingAnnounce;
	char m_aLastServerAddress[128];
	char m_aLastAnnouncedPlayer[128];
	char m_aLastAnnouncedDummy[128];
	char m_aLastAnnouncedSkin[128];
	char m_aLastAnnouncedEntities[128];
	char m_aLastAnnouncedDummyEntities[128];
	int m_LastActivePlayerCount;
	int64_t m_LastPresenceRefreshTime;
	int64_t m_LastPresenceAnnounceTime;
	int64_t m_LastPresencePostTime = 0;
	int64_t m_LastLeaveTime = 0;
	char m_aLastLeaveServer[128] = {};
	char m_aLastLeavePlayer[128] = {};
	std::string m_LastPresencePayload;
	int64_t m_LastRenderCheckTime;
	int64_t m_ServerJoinTime;
	bool m_InitialDownloadPhase;
	bool m_WasFrozen;
	bool m_BlacklistLoaded;
	bool m_EntitiesBlacklistLoaded = false;

	bool CanDownloadGameskins() const;
	bool CanDownloadEntities() const;
	bool IsLocalPlayerFrozen() const;

	bool FindGameSkinFile(const char *pName, char *pOutPath, int OutPathSize);
	void UnloadSkin(SClientGameSkin &Skin);
	void EnsureFallbackGameImage();
	void QueueGameSkinLoad(const char *pName);
	bool StepPendingGameSkinLoad();
	void ResetPendingGameSkinLoad(bool UnloadTextures);
	void StartDownload(const char *pSkinName);
	void StartEntitiesDownload(const char *pEntitiesName);

	void LoadBlacklist();
	void SaveBlacklist();
	void EnsureBlacklistLoaded();
	void EnsureEntitiesBlacklistLoaded();
	void SaveEntitiesBlacklist();

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
	void RefreshPresence(bool ForceAnnounce = true);
	bool IsPlayerAClient(int ClientId) const;
	const SClientGameSkin *GetOrLoadGameSkin(const char *pName);
	IGraphics::CTextureHandle GetOrLoadEntitiesTexture(const char *pName);
	void FreeAllGameSkins();
	void UpdateAllPlayerSkins();
	void TriggerManualReload();

	bool IsGameSkinBlacklisted(const char *pName) const;
	void SetGameSkinBlacklisted(const char *pName, bool Blacklist);
	void ToggleGameSkinBlacklist(const char *pName);

	bool IsEntitiesBlacklisted(const char *pName) const;
	void SetEntitiesBlacklisted(const char *pName, bool Blacklist);
	void ToggleEntitiesBlacklist(const char *pName);

	const char *GetPlayerGameSkinName(int ClientId) const;
	const char *GetPlayerEntitiesName(int ClientId) const;
	bool CopyPlayerGameSkin(int ClientId);
	bool CopyPlayerEntities(int ClientId);

	void UpdateActiveSkinUsers();
	const std::map<std::string, std::string> &GetActiveSkinUsers() const { return m_mActiveSkinUsers; }
	uint64_t SkinUsersRevision() const { return m_SkinUsersRevision; }
	bool GetSkinUsers(const char *pSkinName, char *pOutBuf, int OutBufSize, int *pOutCount = nullptr) const;
	bool IsGameSkinInUse(const char *pSkinName) const;
};

#endif // GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_GAMESKINS_H
