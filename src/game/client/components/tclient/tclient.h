#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_TCLIENT_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_TCLIENT_H

#include <engine/client/enums.h>
#include <engine/external/regex.h>
#include <engine/http.h>
#include <engine/shared/console.h>

#include <game/client/component.h>

#include <deque>

class CTClient : public CComponent
{
	std::deque<vec2> m_aAirRescuePositions[NUM_DUMMIES];
	void AirRescue();
	static void ConAirRescue(IConsole::IResult *pResult, void *pUserData);

	static void ConCalc(IConsole::IResult *pResult, void *pUserData);
	static void ConRandomTee(IConsole::IResult *pResult, void *pUserData);
	static void ConchainRandomColor(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData);
	static void RandomBodyColor();
	static void RandomFeetColor();
	static void RandomSkin(void *pUserData);
	static void RandomFlag(void *pUserData);

	static void ConSpecId(IConsole::IResult *pResult, void *pUserData);
	void SpecId(int ClientId);

	int m_EmoteCycle = 0;
	static void ConEmoteCycle(IConsole::IResult *pResult, void *pUserData);

	class IEngineGraphics *m_pGraphics = nullptr;

	char m_PreviousOwnMessage[2048] = {};

	bool SendNonDuplicateMessage(int Team, const char *pLine);

	float m_FinishTextTimeout = 0.0f;
	void DoFinishCheck();

	bool ServerCommandExists(const char *pCommand);

public:
	enum EUpdateState
	{
		UPDATE_STATE_IDLE = 0,
		UPDATE_STATE_CHECKING,
		UPDATE_STATE_AVAILABLE,
		UPDATE_STATE_DOWNLOADING,
		UPDATE_STATE_NEED_RESTART,
		UPDATE_STATE_FAIL,
	};

	CTClient();
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnConsoleInit() override;
	void OnRender() override;

	void OnStateChange(int OldState, int NewState) override;
	void OnNewSnapshot() override;
	void SetForcedAspect();

	std::shared_ptr<IHttpRequest> m_pTClientInfoTask = nullptr;
	std::shared_ptr<IHttpRequest> m_pDownloadTask = nullptr;

	EUpdateState m_UpdateState = UPDATE_STATE_IDLE;
	char m_aLatestVersion[32] = "0";
	char m_aDownloadUrl[512] = "";
	char m_aUpdateStatus[128] = "";
	int m_UpdatePercent = 0;

	void FetchTClientInfo(bool Force = false);
	void FinishTClientInfo();
	void ResetTClientInfoTask();
	bool NeedUpdate();

	void InitiateUpdate();
	bool ReplaceExecutable();

	EUpdateState GetUpdateState() const { return m_UpdateState; }
	int GetUpdatePercent() const { return m_UpdatePercent; }
	const char *GetLatestVersion() const { return m_aLatestVersion; }
	const char *GetUpdateStatus() const { return m_aUpdateStatus; }

	void RenderMiniVoteHud();
	void RenderCenterLines();
	void RenderCtfFlag(vec2 Pos, float Alpha);

	bool ChatDoSpecId(const char *pInput);
	bool InfoTaskDone() { return m_pTClientInfoTask && m_pTClientInfoTask->State() == EHttpState::DONE; }
	bool m_FetchedTClientInfo = false;
	char m_aVersionStr[32] = "0";

	Regex m_RegexChatIgnore;
};

#endif
