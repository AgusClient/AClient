#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_CONFIG_SYNC_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_CONFIG_SYNC_H

#include <engine/console.h>
#include <engine/http.h>
#include <game/client/component.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

class CAClientConfigSync : public CComponent
{
public:
	enum EUploadState
	{
		UPLOAD_IDLE = 0,
		UPLOAD_SENDING,
		UPLOAD_SUCCESS,
		UPLOAD_ERROR
	};

	enum ELikeState
	{
		LIKE_IDLE = 0,
		LIKE_SENDING,
		LIKE_SUCCESS,
		LIKE_ERROR
	};

	enum EDiscordState
	{
		DISCORD_NONE = 0,
		DISCORD_WAITING_BROWSER,
		DISCORD_VERIFYING,
		DISCORD_READY,
		DISCORD_ERROR
	};

	static constexpr int DISCORD_CALLBACK_PORT = 43821;

private:
	EUploadState m_UploadState = UPLOAD_IDLE;
	char m_aStatusMessage[256] = "";
	std::shared_ptr<IHttpRequest> m_pUploadRequest;

	ELikeState m_LikeState = LIKE_IDLE;
	char m_aLikeStatusMessage[256] = "";
	int64_t m_LastLikesFetchTime = 0;
	std::shared_ptr<IHttpRequest> m_pLikesFetchRequest;
	std::shared_ptr<IHttpRequest> m_pLikeToggleRequest;
	std::unordered_map<std::string, int> m_GameSkinLikes;
	std::unordered_set<std::string> m_SessionLikedGameSkins;

	// Discord OAuth desktop loopback state
	EDiscordState m_DiscordState = DISCORD_NONE;
	char m_aDiscordStatusMessage[256] = "";
	char m_aDiscordHandle[64] = "";
	char m_aDiscordUsername[64] = "";
	std::string m_DiscordAccessToken;
	std::shared_ptr<IHttpRequest> m_pDiscordUserRequest;
	std::shared_ptr<IHttpRequest> m_pDiscordVoterRequest;
	char m_aPendingLikeSkin[64] = "";

	std::thread m_DiscordAuthThread;
	std::atomic<bool> m_DiscordStopFlag{false};
	std::mutex m_DiscordMutex;
	std::string m_ReceivedOAuthToken;
	std::string m_ReceivedOAuthError;
	bool m_HasOAuthResult = false;

	void StopDiscordListener();
	void RunDiscordLoopbackServer();
	void StartDiscordVerification(const std::string &Token);

	static void ConUploadConfig(IConsole::IResult *pResult, void *pUserData);
	static void ConLikeGameSkin(IConsole::IResult *pResult, void *pUserData);
	static void ConDiscordLogin(IConsole::IResult *pResult, void *pUserData);
	static void ConDiscordLogout(IConsole::IResult *pResult, void *pUserData);

public:
	CAClientConfigSync();
	~CAClientConfigSync() override;

	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnRender() override;

	void GetCurrentSnapshot(char *pPlayerName, int PlayerNameSize, int &MouseMaxDistance, int &MouseSens, char *pResolution, int ResolutionSize, int &Hz) const;
	void UploadCurrentConfig(int MouseDpi);

	EUploadState State() const { return m_UploadState; }
	bool IsUploading() const { return m_UploadState == UPLOAD_SENDING; }
	const char *GetStatusMessage() const { return m_aStatusMessage; }

	void RefreshGameSkinLikes(bool Force = false);
	int GetGameSkinLikes(const char *pGameSkinName) const;
	bool HasLikedGameSkinInSession(const char *pGameSkinName) const;
	void ToggleGameSkinLikeWithDiscord(const char *pGameSkinName);

	ELikeState LikeState() const { return m_LikeState; }
	bool IsLiking() const { return m_LikeState == LIKE_SENDING; }
	const char *GetLikeStatusMessage() const { return m_aLikeStatusMessage; }
	void ClearLikeStatus();

	void StartDiscordLogin(const char *pPendingLikeSkin = nullptr);
	void CancelDiscordLogin();
	void LogoutDiscord();

	EDiscordState DiscordState() const { return m_DiscordState; }
	bool HasDiscordSession() const { return m_DiscordState == DISCORD_READY && !m_DiscordAccessToken.empty(); }
	bool IsDiscordReady() const { return m_DiscordState == DISCORD_READY && !m_DiscordAccessToken.empty(); }
	bool IsDiscordBusy() const { return m_DiscordState == DISCORD_WAITING_BROWSER || m_DiscordState == DISCORD_VERIFYING; }
	const char *GetDiscordHandle() const { return m_aDiscordHandle; }
	const char *GetDiscordUsername() const { return m_aDiscordUsername; }
	const char *GetDiscordStatusMessage() const { return m_aDiscordStatusMessage; }
};

#endif // GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_CONFIG_SYNC_H
