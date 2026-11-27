/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_SCOREBOARD_H
#define GAME_CLIENT_COMPONENTS_SCOREBOARD_H

#include <engine/console.h>
#include <engine/graphics.h>

#include <game/client/component.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>
#include <game/client/ui_rect.h>

class CScoreboard : public CComponent
{
	struct CScoreboardRenderState
	{
		float m_TeamStartX;
		float m_TeamStartY;
		int m_CurrentDDTeamSize;
		int m_TeamLabelIndex;

		CScoreboardRenderState() :
			m_TeamStartX(0), m_TeamStartY(0), m_CurrentDDTeamSize(0), m_TeamLabelIndex(0) {}
	};

	void RenderTitleScore(CUIRect ScoreLabel, int Team, float TitleFontSize);
	void RenderTitle(CUIRect TitleLabel, int Team, const char *pTitle, float TitleFontSize);
	void RenderTitleBar(CUIRect TitleBar, int Team, const char *pTitle);
	void RenderGoals(CUIRect Goals);
	void RenderSpectators(CUIRect Spectators);
	void RenderScoreboard(CUIRect Scoreboard, int Team, int CountStart, int CountEnd, CScoreboardRenderState &State, int NumPlayersForSize = -1);
	void RenderRecordingNotification(float x);

	static void ConKeyScoreboard(IConsole::IResult *pResult, void *pUserData);
	static void ConToggleScoreboardCursor(IConsole::IResult *pResult, void *pUserData);

	const char *GetTeamName(int Team) const;

	bool m_Active;

	IGraphics::CTextureHandle m_DeadTeeTexture;
	IGraphics::CTextureHandle m_AClientBadgeTexture;

	std::optional<vec2> m_LastMousePos;
	bool m_MouseUnlocked = false;

	void SetUiMousePos(vec2 Pos);
	void LockMouse();

	class CScoreboardPopupContext : public SPopupMenuId
	{
	public:
		enum ESubMenu
		{
			SUBMENU_MAIN = 0,
			SUBMENU_MUTE,
			SUBMENU_KICK,
			SUBMENU_CONFIRM_KICK,
			SUBMENU_BAN,
			SUBMENU_CONFIRM_BAN,
		};

		CScoreboard *m_pScoreboard = nullptr;
		CButtonContainer m_FriendAction;
		CButtonContainer m_MuteAction;
		CButtonContainer m_EmoticonAction;

		CButtonContainer m_SpectateButton;
		CButtonContainer m_StealSkinButton;
		CButtonContainer m_CopyGameSkinButton;
		CButtonContainer m_HideGameSkinButton;
		CButtonContainer m_CopyEntitiesButton;
		CButtonContainer m_HideEntitiesButton;
		CButtonContainer m_ProfileButton;
		CButtonContainer m_ProfileWebDDNetButton;
		CButtonContainer m_ProfileWebKoGButton;

		int m_ClientId;
		bool m_IsLocal;
		bool m_IsSpectating;

		int m_SubMenu = SUBMENU_MAIN;

		// Moderation buttons
		CButtonContainer m_ModMuteButton;
		CButtonContainer m_ModKickButton;
		CButtonContainer m_ModBanButton;

		CButtonContainer m_ModBack;
		CButtonContainer m_ModConfirm;
		CButtonContainer m_ModUnmute;

		CButtonContainer m_ModKickConfirmYes;
		CButtonContainer m_ModKickConfirmNo;
		CButtonContainer m_ModBanConfirmYes;
		CButtonContainer m_ModBanConfirmNo;

		// Presets
		CButtonContainer m_aSecondsPreset[6];
		CButtonContainer m_aMinutesPreset[4];
		CButtonContainer m_aReasonPreset[3];

		// Inputs
		CLineInputNumber m_SecondsInput;
		CLineInputNumber m_MinutesInput;
		CLineInputBuffered<128> m_ReasonInput;

		void Reset(int ClientId, bool IsSpectating, bool IsLocal);

		static CUi::EPopupMenuFunctionResult Render(void *pContext, CUIRect View, bool Active);
	} m_ScoreboardPopupContext;

	class CMapTitlePopupContext : public SPopupMenuId
	{
	public:
		CScoreboard *m_pScoreboard = nullptr;

		float m_FontSize;

		static CUi::EPopupMenuFunctionResult Render(void *pContext, CUIRect View, bool Active);
	} m_MapTitlePopupContext;
	char m_MapTitleButtonId;

	class CPlayerElement
	{
	public:
		char m_PlayerButtonId;
		char m_SpectatorSecondLineButtonId;

		CCachedText m_Score;
		CCachedText m_ScoreMillis;
		CCachedText m_Name;
		CCachedText m_MuteMark;
		CCachedText m_ReadyMark;
		CCachedText m_Clan;
		CCachedText m_Ping;
	};
	CPlayerElement m_aPlayers[MAX_CLIENTS];

	CCachedText m_TitleScore;
	CCachedText m_TitleScoreMillis;
	CCachedText m_HeadlineScore;
	CCachedText m_HeadlineName;
	CCachedText m_HeadlineClan;
	CCachedText m_HeadlinePing;
	CCachedText m_FriendMark;
	CCachedText m_aTitleText[2];
	CCachedText m_aTeamLabels[MAX_CLIENTS];

	struct SSpectatorEntry
	{
		int m_ClientId;
		CUIRect m_Rect;
		CUIRect m_LineBreakRect;
		bool m_HasLineBreak;
	};
	STextContainerIndex m_SpectatorsTextContainer;
	std::string m_SpectatorsCacheKey;
	std::vector<SSpectatorEntry> m_vSpectatorEntries;

	float m_CachedTitleScoreWidth = -1.0f;
	std::string m_aCachedTitleStr[2];
	float m_aCachedTitleWidth[2] = {-1.0f, -1.0f};
	float m_CachedScoreLengthFontSize = -1.0f;
	bool m_CachedScoreLengthUseTime = false;
	float m_CachedScoreLength = 0.0f;

public:
	void ResetTexts();
	void SaveModReplay(const char *pAction, const char *pTargetName);

	CScoreboard();
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnInit() override;
	void OnReset() override;
	void OnShutdown() override;
	void OnWindowResize() override;
	void OnRender() override;
	void OnRelease() override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	bool OnInput(const IInput::CEvent &Event) override;

	bool IsActive() const;
	bool IsMouseUnlocked() const { return IsActive() && m_MouseUnlocked; }
};

#endif
