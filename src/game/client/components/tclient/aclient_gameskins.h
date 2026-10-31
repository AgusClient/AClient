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

class CAClientGameSkins : public CComponent
{
	std::map<std::string, SClientGameSkin> m_mLoadedGameSkins;
	std::map<std::string, std::string> m_mRemotePlayerGameSkins;
	std::set<std::string> m_sFailedSkins;

	std::shared_ptr<IHttpRequest> m_pPresenceRequest;
	std::shared_ptr<IHttpRequest> m_pDownloadRequest;
	std::string m_DownloadingSkinName;

	bool m_PendingAnnounce;
	char m_aLastServerAddress[128];
	int64_t m_LastRenderCheckTime;

	bool FindGameSkinFile(const char *pName, char *pOutPath, int OutPathSize);
	void UnloadSkin(SClientGameSkin &Skin);
	void StartDownload(const char *pSkinName);

	static void ConReloadRemoteGameSkins(IConsole::IResult *pResult, void *pUserData);
	static void ConSetPlayerGameSkin(IConsole::IResult *pResult, void *pUserData);

public:
	CAClientGameSkins();
	~CAClientGameSkins() override;

	int Sizeof() const override { return sizeof(*this); }
	void OnStateChange(int NewState, int OldState) override;
	void OnRender() override;
	void OnConsoleInit() override;
	void OnReset() override;

	void RefreshPresence();
	const SClientGameSkin *GetOrLoadGameSkin(const char *pName);
	void FreeAllGameSkins();
	void UpdateAllPlayerSkins();
};

#endif // GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_GAMESKINS_H
