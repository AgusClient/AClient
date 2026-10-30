/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "scoreboard.h"

#include <base/dbg.h>
#include <base/time.h>

#include <engine/console.h>
#include <engine/demo.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data7.h>
#include <generated/protocol.h>

#include <game/client/animstate.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/motd.h>
#include <game/client/components/statboard.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>

// Horizontal spacing of the scoreboard contents, both to its edges and between columns
static constexpr float MARGIN = 10.0f;

CScoreboard::CScoreboard()
{
	CScoreboard::OnReset();
}

void CScoreboard::SetUiMousePos(vec2 Pos)
{
	const vec2 WindowSize = vec2(Graphics()->WindowWidth(), Graphics()->WindowHeight());
	const CUIRect *pScreen = Ui()->Screen();

	const vec2 UpdatedMousePos = Ui()->UpdatedMousePos();
	Pos = Pos / vec2(pScreen->w, pScreen->h) * WindowSize;
	Ui()->OnCursorMove(Pos.x - UpdatedMousePos.x, Pos.y - UpdatedMousePos.y);
}

void CScoreboard::LockMouse()
{
	Ui()->ClosePopupMenus();
	m_MouseUnlocked = false;
	SetUiMousePos(m_LastMousePos.value());
	m_LastMousePos = Ui()->MousePos();
}

void CScoreboard::ConKeyScoreboard(IConsole::IResult *pResult, void *pUserData)
{
	CScoreboard *pSelf = static_cast<CScoreboard *>(pUserData);

	pSelf->GameClient()->m_Spectator.OnRelease();
	pSelf->GameClient()->m_Emoticon.OnRelease();

	pSelf->m_Active = pResult->GetInteger(0) != 0;

	if(!pSelf->IsActive() && pSelf->m_MouseUnlocked)
	{
		if(!pSelf->Ui()->IsPopupOpen(&pSelf->m_ScoreboardPopupContext))
		{
			pSelf->LockMouse();
		}
	}
}

void CScoreboard::ConToggleScoreboardCursor(IConsole::IResult *pResult, void *pUserData)
{
	CScoreboard *pSelf = static_cast<CScoreboard *>(pUserData);

	if(!pSelf->IsActive() ||
		pSelf->GameClient()->m_Menus.IsActive() ||
		pSelf->GameClient()->m_Chat.IsActive() ||
		pSelf->Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		return;
	}

	pSelf->m_MouseUnlocked = !pSelf->m_MouseUnlocked;

	if(!pSelf->m_MouseUnlocked)
	{
		pSelf->Ui()->ClosePopupMenus();
	}

	vec2 OldMousePos = pSelf->Ui()->MousePos();

	if(pSelf->m_LastMousePos == std::nullopt)
	{
		pSelf->SetUiMousePos(pSelf->Ui()->Screen()->Center());
	}
	else
	{
		pSelf->SetUiMousePos(pSelf->m_LastMousePos.value());
	}

	// save pos, so moving the mouse in esc menu doesn't change the position
	pSelf->m_LastMousePos = OldMousePos;
}

void CScoreboard::OnConsoleInit()
{
	Console()->Register("+scoreboard", "", CFGFLAG_CLIENT, ConKeyScoreboard, this, "Show scoreboard");
	Console()->Register("toggle_scoreboard_cursor", "", CFGFLAG_CLIENT, ConToggleScoreboardCursor, this, "Toggle scoreboard cursor");
}

void CScoreboard::OnInit()
{
	m_DeadTeeTexture = Graphics()->LoadTexture("deadtee.png", IStorage::TYPE_ALL);
}

void CScoreboard::OnReset()
{
	m_Active = false;
	m_MouseUnlocked = false;
	m_LastMousePos = std::nullopt;
}

void CScoreboard::ResetTexts()
{
	for(CPlayerElement &Player : m_aPlayers)
	{
		Player.m_Score.Reset(TextRender());
		Player.m_ScoreMillis.Reset(TextRender());
		Player.m_Name.Reset(TextRender());
		Player.m_MuteMark.Reset(TextRender());
		Player.m_ReadyMark.Reset(TextRender());
		Player.m_Clan.Reset(TextRender());
		Player.m_Ping.Reset(TextRender());
	}
	m_TitleScore.Reset(TextRender());
	m_TitleScoreMillis.Reset(TextRender());
	m_HeadlineScore.Reset(TextRender());
	m_HeadlineName.Reset(TextRender());
	m_HeadlineClan.Reset(TextRender());
	m_HeadlinePing.Reset(TextRender());
}

void CScoreboard::OnShutdown()
{
	ResetTexts();
}

void CScoreboard::OnWindowResize()
{
	ResetTexts();
}

void CScoreboard::OnRelease()
{
	m_Active = false;

	if(m_MouseUnlocked && !Ui()->IsPopupOpen(&m_ScoreboardPopupContext))
	{
		LockMouse();
	}
}

bool CScoreboard::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!IsActive() || !m_MouseUnlocked)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);

	return true;
}

bool CScoreboard::OnInput(const IInput::CEvent &Event)
{
	if(m_MouseUnlocked)
	{
		if(Event.m_Key == KEY_ESCAPE && (Event.m_Flags & IInput::FLAG_PRESS))
		{
			if(Ui()->IsPopupOpen(&m_ScoreboardPopupContext))
			{
				Ui()->ClosePopupMenu(&m_ScoreboardPopupContext);
				if(!m_Active)
					LockMouse();
			}
			else
			{
				LockMouse();
			}
			return true;
		}

		if(Ui()->OnInput(Event))
			return true;

		return IsActive();
	}

	return false;
}

void CScoreboard::RenderTitle(CUIRect TitleLabel, int Team, const char *pTitle, float TitleFontSize)
{
	const bool IsMapTitle = !GameClient()->IsTeamPlay();
	if(IsMapTitle && m_MouseUnlocked && GameClient()->m_aMapDescription[0] != '\0')
	{
		const int ButtonResult = Ui()->DoButtonLogic(&m_MapTitleButtonId, 0, &TitleLabel, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
		if(ButtonResult != 0)
		{
			m_MapTitlePopupContext.m_pScoreboard = this;

			m_MapTitlePopupContext.m_FontSize = 12.0f;
			const float MaxWidth = 300.0f;
			const float Margin = 5.0f;
			const char *pDescription = GameClient()->m_aMapDescription;
			const float TextWidth = std::min(std::ceil(TextRender()->TextWidth(m_MapTitlePopupContext.m_FontSize, pDescription) + 0.5f), MaxWidth);
			float TextHeight = 0.0f;
			STextSizeProperties TextSizeProps{};
			TextSizeProps.m_pHeight = &TextHeight;
			TextRender()->TextWidth(m_MapTitlePopupContext.m_FontSize, pDescription, -1, TextWidth, 0, TextSizeProps);

			Ui()->DoPopupMenu(&m_MapTitlePopupContext, Ui()->MouseX(), Ui()->MouseY(), TextWidth + Margin * 2, TextHeight + Margin * 2, &m_MapTitlePopupContext, CMapTitlePopupContext::Render);
		}
		if(Ui()->HotItem() == &m_MapTitleButtonId)
		{
			TitleLabel.Draw(ColorRGBA(0.7f, 0.7f, 0.7f, 0.3f), IGraphics::CORNER_ALL, 5.0f);
		}
	}

	SLabelProperties Props;
	Props.m_MaxWidth = TitleLabel.w;
	Props.m_EllipsisAtEnd = true;
	Ui()->DoLabel(&TitleLabel, pTitle, TitleFontSize, Team == TEAM_RED ? TEXTALIGN_ML : TEXTALIGN_MR, Props);
}

void CScoreboard::RenderTitleScore(CUIRect ScoreLabel, int Team, float TitleFontSize)
{
	// map best
	char aScore[128] = "";
	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	const bool TimeScore = GameClient()->m_GameInfo.m_TimeScore;
	const bool Race7 = Client()->IsSixup() && pGameInfoObj && pGameInfoObj->m_GameFlags & protocol7::GAMEFLAG_RACE;
	if(GameClient()->m_ReceivedDDNetPlayerFinishTimes || TimeScore || Race7)
	{
		if(GameClient()->m_MapBestTimeSeconds != FinishTime::UNSET)
		{
			Ui()->RenderTime(ScoreLabel,
				TitleFontSize,
				GameClient()->m_MapBestTimeSeconds,
				GameClient()->m_MapBestTimeSeconds == FinishTime::NOT_FINISHED_MILLIS,
				GameClient()->m_MapBestTimeMillis,
				GameClient()->m_ReceivedDDNetPlayerFinishTimesMillis,
				m_TitleScore, m_TitleScoreMillis, TextRender()->DefaultTextColor());
			return;
		}
	}
	else if(GameClient()->IsTeamPlay()) // normal score
	{
		const CNetObj_GameData *pGameDataObj = GameClient()->m_Snap.m_pGameDataObj;
		if(pGameDataObj)
		{
			str_format(aScore, sizeof(aScore), "%d", Team == TEAM_RED ? pGameDataObj->m_TeamscoreRed : pGameDataObj->m_TeamscoreBlue);
		}
	}
	else
	{
		if(GameClient()->m_Snap.m_SpecInfo.m_Active &&
			GameClient()->m_Snap.m_SpecInfo.m_SpectatorId != SPEC_FREEVIEW &&
			GameClient()->m_Snap.m_apPlayerInfos[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId])
		{
			str_format(aScore, sizeof(aScore), "%d", GameClient()->m_Snap.m_apPlayerInfos[GameClient()->m_Snap.m_SpecInfo.m_SpectatorId]->m_Score);
		}
		else if(GameClient()->m_Snap.m_pLocalInfo)
		{
			str_format(aScore, sizeof(aScore), "%d", GameClient()->m_Snap.m_pLocalInfo->m_Score);
		}
	}

	const float ScoreTextWidth = aScore[0] != '\0' ? TextRender()->TextWidth(TitleFontSize, aScore, -1, -1.0f, 0) : 0.0f;
	if(ScoreTextWidth != 0.0f)
	{
		Ui()->DoLabel(&ScoreLabel, aScore, TitleFontSize, Team == TEAM_RED ? TEXTALIGN_MR : TEXTALIGN_ML);
	}
}

void CScoreboard::RenderTitleBar(CUIRect TitleBar, int Team, const char *pTitle)
{
	dbg_assert(Team == TEAM_RED || Team == TEAM_BLUE, "Team invalid");

	const float TitleFontSize = 20.0f;
	const float ScoreTextWidth = TextRender()->TextWidth(TitleFontSize, "00:00:00");
	const float TitleTextWidth = TextRender()->TextWidth(TitleFontSize, pTitle);

	TitleBar.VMargin(MARGIN, &TitleBar);
	CUIRect TitleLabel, ScoreLabel;
	if(Team == TEAM_RED)
	{
		TitleBar.VSplitRight(ScoreTextWidth, &TitleLabel, &ScoreLabel);
		TitleLabel.VSplitRight(5.0f, &TitleLabel, nullptr);
		TitleLabel.VSplitLeft(std::min(TitleTextWidth + 2.0f, TitleLabel.w), &TitleLabel, nullptr);
	}
	else
	{
		TitleBar.VSplitLeft(ScoreTextWidth, &ScoreLabel, &TitleLabel);
		TitleLabel.VSplitLeft(5.0f, nullptr, &TitleLabel);
		TitleLabel.VSplitRight(std::min(TitleTextWidth + 2.0f, TitleLabel.w), nullptr, &TitleLabel);
	}

	RenderTitle(TitleLabel, Team, pTitle, TitleFontSize);
	RenderTitleScore(ScoreLabel, Team, TitleFontSize);
}

void CScoreboard::RenderGoals(CUIRect Goals)
{
	Goals.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 7.5f);
	Goals.VMargin(5.0f, &Goals);

	const float FontSize = 10.0f;
	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	char aBuf[64];

	if(pGameInfoObj->m_ScoreLimit)
	{
		str_format(aBuf, sizeof(aBuf), "%s: %d", Localize("Score limit"), pGameInfoObj->m_ScoreLimit);
		Ui()->DoLabel(&Goals, aBuf, FontSize, TEXTALIGN_ML);
	}

	if(pGameInfoObj->m_TimeLimit)
	{
		str_format(aBuf, sizeof(aBuf), Localize("Time limit: %d min"), pGameInfoObj->m_TimeLimit);
		Ui()->DoLabel(&Goals, aBuf, FontSize, TEXTALIGN_MC);
	}

	if(pGameInfoObj->m_RoundNum && pGameInfoObj->m_RoundCurrent)
	{
		str_format(aBuf, sizeof(aBuf), Localize("Round %d/%d"), pGameInfoObj->m_RoundCurrent, pGameInfoObj->m_RoundNum);
		Ui()->DoLabel(&Goals, aBuf, FontSize, TEXTALIGN_MR);
	}
}

void CScoreboard::RenderSpectators(CUIRect Spectators)
{
	Spectators.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 7.5f);
	constexpr float SpectatorCut = 5.0f;
	Spectators.Margin(SpectatorCut, &Spectators);

	CTextCursor Cursor;
	Cursor.SetPosition(Spectators.TopLeft());
	Cursor.m_FontSize = 11.0f;
	Cursor.m_LineWidth = Spectators.w;
	Cursor.m_MaxLines = round_truncate(Spectators.h / Cursor.m_FontSize);

	int RemainingSpectators = 0;
	for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByName)
	{
		if(!pInfo || pInfo->m_Team != TEAM_SPECTATORS)
			continue;
		++RemainingSpectators;
	}

	TextRender()->TextEx(&Cursor, Localize("Spectators"));

	if(RemainingSpectators > 0)
	{
		TextRender()->TextEx(&Cursor, ": ");
	}

	bool CommaNeeded = false;
	for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByName)
	{
		if(!pInfo || pInfo->m_Team != TEAM_SPECTATORS)
			continue;

		if(CommaNeeded)
		{
			TextRender()->TextEx(&Cursor, ", ");
		}

		if(Cursor.m_LineCount == Cursor.m_MaxLines && RemainingSpectators >= 2)
		{
			// This is less expensive than checking with a separate invisible
			// text cursor though we waste some space at the end of the line.
			char aRemaining[64];
			str_format(aRemaining, sizeof(aRemaining), Localize("%d others…", "Spectators"), RemainingSpectators);
			TextRender()->TextEx(&Cursor, aRemaining);
			break;
		}

		CUIRect SpectatorRect, SpectatorRectLineBreak;
		float Margin = 1.0f;
		SpectatorRect.x = Cursor.m_X - Margin;
		SpectatorRect.y = Cursor.m_Y;

		if(g_Config.m_ClShowIds)
		{
			char aClientId[16];
			GameClient()->FormatClientId(pInfo->m_ClientId, aClientId, EClientIdFormat::NO_INDENT);
			TextRender()->TextEx(&Cursor, aClientId);
		}

		const CGameClient::CClientData &ClientData = GameClient()->m_aClients[pInfo->m_ClientId];
		{
			const char *pClanName = ClientData.m_aClan;
			if(pClanName[0] != '\0')
			{
				if(GameClient()->m_aLocalIds[g_Config.m_ClDummy] >= 0 && str_comp(pClanName, GameClient()->m_aClients[GameClient()->m_aLocalIds[g_Config.m_ClDummy]].m_aClan) == 0)
				{
					TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClSameClanColor)));
				}
				else
				{
					TextRender()->TextColor(ColorRGBA(0.7f, 0.7f, 0.7f));
				}

				TextRender()->TextEx(&Cursor, pClanName);
				TextRender()->TextEx(&Cursor, " ");

				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
		}

		if(GameClient()->m_aClients[pInfo->m_ClientId].m_AuthLevel)
		{
			TextRender()->TextColor(color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClAuthedPlayerColor)));
		}

		TextRender()->TextEx(&Cursor, GameClient()->m_aClients[pInfo->m_ClientId].m_aName);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		CommaNeeded = true;
		--RemainingSpectators;

		bool LineBreakDetected = false;
		SpectatorRect.h = Cursor.m_FontSize;

		// detect line breaks
		if(Cursor.m_Y != SpectatorRect.y)
		{
			LineBreakDetected = true;
			SpectatorRectLineBreak.x = Spectators.x - SpectatorCut;
			SpectatorRectLineBreak.y = Cursor.m_Y;
			SpectatorRectLineBreak.h = Cursor.m_FontSize;
			SpectatorRectLineBreak.w = Cursor.m_X - Spectators.x + SpectatorCut + 2 * Margin;

			SpectatorRect.w = Spectators.x + Spectators.w + SpectatorCut - SpectatorRect.x;
		}
		else
		{
			SpectatorRect.w = Cursor.m_X - SpectatorRect.x + 2 * Margin;
		}

		if(m_MouseUnlocked)
		{
			int ButtonResult = Ui()->DoButtonLogic(&m_aPlayers[pInfo->m_ClientId].m_PlayerButtonId, 0, &SpectatorRect, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);

			if(LineBreakDetected && ButtonResult == 0)
			{
				ButtonResult = Ui()->DoButtonLogic(&m_aPlayers[pInfo->m_ClientId].m_SpectatorSecondLineButtonId, 0, &SpectatorRectLineBreak, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
			}
			if(ButtonResult != 0)
			{
				m_ScoreboardPopupContext.m_pScoreboard = this;
				bool IsLocalTarget = GameClient()->m_aLocalIds[0] == pInfo->m_ClientId;
				if(!g_Config.m_ClModAllowTargetDummy)
					IsLocalTarget |= (Client()->DummyConnected() && GameClient()->m_aLocalIds[1] == pInfo->m_ClientId);
				m_ScoreboardPopupContext.Reset(pInfo->m_ClientId, true, IsLocalTarget);

				const bool CanMod = Client()->RconAuthed() && !m_ScoreboardPopupContext.m_IsLocal;
				const float Width = CanMod ? 160.0f : 125.0f;
				const float Height = CanMod ? 238.0f : (m_ScoreboardPopupContext.m_IsLocal ? 73.0f : 124.5f);

				Ui()->DoPopupMenu(&m_ScoreboardPopupContext, Ui()->MouseX(), Ui()->MouseY(), Width,
					Height, &m_ScoreboardPopupContext, CScoreboardPopupContext::Render);
			}

			if(Ui()->HotItem() == &m_aPlayers[pInfo->m_ClientId].m_PlayerButtonId ||
				Ui()->HotItem() == &m_aPlayers[pInfo->m_ClientId].m_SpectatorSecondLineButtonId ||
				(Ui()->IsPopupOpen(&m_ScoreboardPopupContext) && m_ScoreboardPopupContext.m_ClientId == pInfo->m_ClientId))
			{
				if(!LineBreakDetected)
				{
					SpectatorRect.Draw(TextRender()->DefaultTextSelectionColor(), IGraphics::CORNER_ALL, 2.5f);
				}
				else
				{
					SpectatorRect.Draw(TextRender()->DefaultTextSelectionColor(), IGraphics::CORNER_L, 2.5f);
					SpectatorRectLineBreak.Draw(TextRender()->DefaultTextSelectionColor(), IGraphics::CORNER_R, 2.5f);
				}
			}
		}
	}
}

void CScoreboard::RenderScoreboard(CUIRect Scoreboard, int Team, int CountStart, int CountEnd, CScoreboardRenderState &State, int NumPlayersForSize)
{
	dbg_assert(Team == TEAM_RED || Team == TEAM_BLUE, "Team invalid");

	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	const CNetObj_GameData *pGameDataObj = GameClient()->m_Snap.m_pGameDataObj;
	const bool TimeScore = GameClient()->m_GameInfo.m_TimeScore;
	const bool MillisecondScore = GameClient()->m_ReceivedDDNetPlayerFinishTimes;
	const bool TrueMilliseconds = GameClient()->m_ReceivedDDNetPlayerFinishTimesMillis;
	const int NumPlayers = NumPlayersForSize >= 0 ? NumPlayersForSize : (CountEnd - CountStart);
	const bool LowScoreboardWidth = Scoreboard.w < 350.0f;

	bool Race7 = Client()->IsSixup() && pGameInfoObj && pGameInfoObj->m_GameFlags & protocol7::GAMEFLAG_RACE;

	const bool UseTime = Race7 || TimeScore || MillisecondScore;

	// calculate measurements
	float LineHeight;
	float TeeSizeMod;
	float Spacing;
	float RoundRadius;
	float FontSize;
	if(NumPlayers <= 8)
	{
		LineHeight = 30.0f;
		TeeSizeMod = 0.5f;
		Spacing = 8.0f;
		RoundRadius = 5.0f;
		FontSize = 12.0f;
	}
	else if(NumPlayers <= 12)
	{
		LineHeight = 25.0f;
		TeeSizeMod = 0.45f;
		Spacing = 2.5f;
		RoundRadius = 5.0f;
		FontSize = 12.0f;
	}
	else if(NumPlayers <= 16)
	{
		LineHeight = 20.0f;
		TeeSizeMod = 0.4f;
		Spacing = 0.0f;
		RoundRadius = 2.5f;
		FontSize = 12.0f;
	}
	else if(NumPlayers <= 24)
	{
		LineHeight = 13.5f;
		TeeSizeMod = 0.3f;
		Spacing = 0.0f;
		RoundRadius = 2.5f;
		FontSize = 10.0f;
	}
	else if(NumPlayers <= 32)
	{
		LineHeight = 10.0f;
		TeeSizeMod = 0.2f;
		Spacing = 0.0f;
		RoundRadius = 2.5f;
		FontSize = 8.0f;
	}
	else if(LowScoreboardWidth && NumPlayers <= 48)
	{
		LineHeight = 7.5f;
		TeeSizeMod = 0.125f;
		Spacing = 0.0f;
		RoundRadius = 1.0f;
		FontSize = 7.0f;
	}
	else
	{
		LineHeight = 5.0f;
		TeeSizeMod = 0.1f;
		Spacing = 0.0f;
		RoundRadius = 1.0f;
		FontSize = 5.0f;
	}

	const float ScoreOffset = Scoreboard.x + MARGIN;
	const float ScoreLength = TextRender()->TextWidth(FontSize, UseTime ? "00:00:00" : "99999");
	const float TeeOffset = ScoreOffset + ScoreLength + MARGIN;
	const float TeeLength = 60.0f * TeeSizeMod;
	const float NameOffset = TeeOffset + TeeLength;
	const float CountryLength = (LineHeight - Spacing - TeeSizeMod * 5.0f) * 2.0f;
	const float PingLength = 27.5f;
	const float PingOffset = Scoreboard.x + Scoreboard.w - PingLength - MARGIN;
	const float CountryOffset = PingOffset - CountryLength;

	float NameLength = (LowScoreboardWidth ? 90.0f : 150.0f) - TeeLength;
	const float MinMiddleGap = 5.0f; // 2.5 before and after clan
	const float AvailableMiddle = CountryOffset - NameOffset;
	if(NameLength + MinMiddleGap > AvailableMiddle)
	{
		const float Shrinkable = AvailableMiddle - MinMiddleGap;
		NameLength = std::max(0.0f, Shrinkable * 0.7f);
	}

	const float ClanOffset = NameOffset + NameLength + 2.5f;
	const float ClanLength = std::max(0.0f, CountryOffset - ClanOffset - 2.5f);

	// render headlines
	const float HeadlineFontsize = 11.0f;
	CUIRect Headline;
	Scoreboard.HSplitTop(HeadlineFontsize * 2.0f, &Headline, &Scoreboard);
	const float HeadlineY = Headline.y + Headline.h / 2.0f - HeadlineFontsize / 2.0f;
	const ColorRGBA HeadlineColor = TextRender()->DefaultTextColor();
	m_HeadlineScore.Update(TextRender(), UseTime ? Localize("Time") : Localize("Score"), HeadlineFontsize);
	m_HeadlineName.Update(TextRender(), Localize("Name"), HeadlineFontsize);
	m_HeadlineClan.Update(TextRender(), Localize("Clan"), HeadlineFontsize);
	m_HeadlinePing.Update(TextRender(), Localize("Ping"), HeadlineFontsize);
	m_HeadlineScore.Render(TextRender(), vec2(ScoreOffset + ScoreLength - m_HeadlineScore.Width(), HeadlineY), HeadlineColor);
	m_HeadlineName.Render(TextRender(), vec2(NameOffset, HeadlineY), HeadlineColor);
	m_HeadlineClan.Render(TextRender(), vec2(ClanOffset + (ClanLength - m_HeadlineClan.Width()) / 2.0f, HeadlineY), HeadlineColor);
	m_HeadlinePing.Render(TextRender(), vec2(PingOffset + PingLength - m_HeadlinePing.Width(), HeadlineY), HeadlineColor);

	// render player entries
	int CountRendered = 0;
	int PrevDDTeam = -1;
	int &CurrentDDTeamSize = State.m_CurrentDDTeamSize;

	char aBuf[64];
	int MaxTeamSize = GameClient()->MaxTeamSize();

	for(int RenderDead = 0; RenderDead < 2; RenderDead++)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			// make sure that we render the correct team
			const CNetObj_PlayerInfo *pInfo = GameClient()->m_Snap.m_apInfoByDDTeamScore[i];
			if(!pInfo || pInfo->m_Team != Team)
				continue;
			bool IsDead = Client()->m_TranslationContext.m_aClients[pInfo->m_ClientId].m_PlayerFlags7 & protocol7::PLAYERFLAG_DEAD;
			if(!RenderDead && IsDead)
				continue;
			if(RenderDead && !IsDead)
				continue;
			if(CountRendered++ < CountStart)
				continue;

			int DDTeam = GameClient()->m_Teams.Team(pInfo->m_ClientId);
			int NextDDTeam = 0;

			ColorRGBA TextColor = TextRender()->DefaultTextColor();
			TextColor.a = RenderDead ? 0.5f : 1.0f;
			TextRender()->TextColor(TextColor);

			for(int j = i + 1; j < MAX_CLIENTS; j++)
			{
				const CNetObj_PlayerInfo *pInfoNext = GameClient()->m_Snap.m_apInfoByDDTeamScore[j];
				if(!pInfoNext || pInfoNext->m_Team != Team)
					continue;

				NextDDTeam = GameClient()->m_Teams.Team(pInfoNext->m_ClientId);
				break;
			}

			if(PrevDDTeam == -1)
			{
				for(int j = i - 1; j >= 0; j--)
				{
					const CNetObj_PlayerInfo *pInfoPrev = GameClient()->m_Snap.m_apInfoByDDTeamScore[j];
					if(!pInfoPrev || pInfoPrev->m_Team != Team)
						continue;

					PrevDDTeam = GameClient()->m_Teams.Team(pInfoPrev->m_ClientId);
					break;
				}
			}

			CUIRect RowAndSpacing, Row;
			Scoreboard.HSplitTop(LineHeight + Spacing, &RowAndSpacing, &Scoreboard);
			RowAndSpacing.HSplitTop(LineHeight, &Row, nullptr);

			// team background
			if(DDTeam != TEAM_FLOCK)
			{
				const ColorRGBA Color = GameClient()->GetDDTeamColor(DDTeam).WithAlpha(0.5f);
				int TeamRectCorners = 0;
				if(PrevDDTeam != DDTeam)
				{
					TeamRectCorners |= IGraphics::CORNER_T;
					State.m_TeamStartX = Row.x;
					State.m_TeamStartY = Row.y;
				}
				if(NextDDTeam != DDTeam)
					TeamRectCorners |= IGraphics::CORNER_B;
				RowAndSpacing.Draw(Color, TeamRectCorners, RoundRadius);

				CurrentDDTeamSize++;

				if(NextDDTeam != DDTeam)
				{
					const float TeamFontSize = FontSize / 1.5f;

					if(NumPlayers > 8)
					{
						if(DDTeam == GameClient()->m_Teams.TeamSuper())
							str_copy(aBuf, Localize("Super"));
						else if(CurrentDDTeamSize <= 1)
							str_format(aBuf, sizeof(aBuf), "%d", DDTeam);
						else
							str_format(aBuf, sizeof(aBuf), Localize("%d\n(%d/%d)", "Team and size"), DDTeam, CurrentDDTeamSize, MaxTeamSize);
						TextRender()->Text(State.m_TeamStartX, std::max(State.m_TeamStartY + Row.h / 2.0f - TeamFontSize, State.m_TeamStartY + 1.5f /* padding top */), TeamFontSize, aBuf);
					}
					else
					{
						if(DDTeam == GameClient()->m_Teams.TeamSuper())
							str_copy(aBuf, Localize("Super"));
						else if(CurrentDDTeamSize > 1)
							str_format(aBuf, sizeof(aBuf), Localize("Team %d (%d/%d)"), DDTeam, CurrentDDTeamSize, MaxTeamSize);
						else
							str_format(aBuf, sizeof(aBuf), Localize("Team %d"), DDTeam);
						TextRender()->Text(Row.x + Row.w / 2.0f - TextRender()->TextWidth(TeamFontSize, aBuf) / 2.0f + 5.0f, Row.y + Row.h, TeamFontSize, aBuf);
					}

					CurrentDDTeamSize = 0;
				}
			}
			PrevDDTeam = DDTeam;

			// background so it's easy to find the local player or the followed one in spectator mode
			if((!GameClient()->m_Snap.m_SpecInfo.m_Active && pInfo->m_Local) ||
				(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == SPEC_FREEVIEW && pInfo->m_Local) ||
				(GameClient()->m_Snap.m_SpecInfo.m_Active && pInfo->m_ClientId == GameClient()->m_Snap.m_SpecInfo.m_SpectatorId))
			{
				Row.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), IGraphics::CORNER_ALL, RoundRadius);
			}

			const CGameClient::CClientData &ClientData = GameClient()->m_aClients[pInfo->m_ClientId];
			CPlayerElement &Player = m_aPlayers[pInfo->m_ClientId];

			if(m_MouseUnlocked)
			{
				const int ButtonResult = Ui()->DoButtonLogic(&Player.m_PlayerButtonId, 0, &Row, BUTTONFLAG_LEFT | BUTTONFLAG_RIGHT);
				if(ButtonResult != 0)
				{
					m_ScoreboardPopupContext.m_pScoreboard = this;
					bool IsLocalTarget = GameClient()->m_aLocalIds[0] == pInfo->m_ClientId;
					if(!g_Config.m_ClModAllowTargetDummy)
						IsLocalTarget |= (Client()->DummyConnected() && GameClient()->m_aLocalIds[1] == pInfo->m_ClientId);
					m_ScoreboardPopupContext.Reset(pInfo->m_ClientId, false, IsLocalTarget);

					const bool CanMod = Client()->RconAuthed() && !m_ScoreboardPopupContext.m_IsLocal;
					const float Width = CanMod ? 160.0f : 125.0f;
					const float Height = CanMod ? 238.0f : (m_ScoreboardPopupContext.m_IsLocal ? 101.5f : 152.0f);

					Ui()->DoPopupMenu(&m_ScoreboardPopupContext, Ui()->MouseX(), Ui()->MouseY(), Width,
						Height, &m_ScoreboardPopupContext, CScoreboardPopupContext::Render);
				}

				if(Ui()->HotItem() == &Player.m_PlayerButtonId ||
					(Ui()->IsPopupOpen(&m_ScoreboardPopupContext) && m_ScoreboardPopupContext.m_ClientId == pInfo->m_ClientId))
				{
					Row.Draw(ColorRGBA(0.7f, 0.7f, 0.7f, 0.7f), IGraphics::CORNER_ALL, RoundRadius);
				}
			}

			// score
			CUIRect ScorePosition;
			ScorePosition.x = ScoreOffset;
			ScorePosition.w = ScoreLength;
			ScorePosition.y = Row.y;
			ScorePosition.h = Row.h;

			if(Race7)
			{
				Ui()->RenderTime(ScorePosition, FontSize, pInfo->m_Score / 1000, pInfo->m_Score == protocol7::FinishTime::NOT_FINISHED, pInfo->m_Score % 1000, true,
					Player.m_Score, Player.m_ScoreMillis, TextColor);
			}
			else if(MillisecondScore)
			{
				Ui()->RenderTime(ScorePosition, FontSize, ClientData.m_FinishTimeSeconds, ClientData.m_FinishTimeSeconds == FinishTime::NOT_FINISHED_MILLIS, ClientData.m_FinishTimeMillis, TrueMilliseconds,
					Player.m_Score, Player.m_ScoreMillis, TextColor);
			}
			else if(TimeScore)
			{
				Ui()->RenderTime(ScorePosition, FontSize, pInfo->m_Score, pInfo->m_Score == FinishTime::NOT_FINISHED_TIMESCORE, -1, false,
					Player.m_Score, Player.m_ScoreMillis, TextColor);
			}
			else
			{
				str_format(aBuf, sizeof(aBuf), "%d", std::clamp(pInfo->m_Score, -999, 99999));
				Player.m_Score.Update(TextRender(), aBuf, FontSize);
				Player.m_Score.Render(TextRender(), vec2(ScoreOffset + ScoreLength - Player.m_Score.Width(), ScorePosition.y + (Row.h - FontSize) / 2.0f), TextColor);
			}

			// CTF flag
			if(pGameInfoObj && (pGameInfoObj->m_GameFlags & GAMEFLAG_FLAGS) &&
				pGameDataObj && (pGameDataObj->m_FlagCarrierRed == pInfo->m_ClientId || pGameDataObj->m_FlagCarrierBlue == pInfo->m_ClientId))
			{
				Graphics()->TextureSet(pGameDataObj->m_FlagCarrierBlue == pInfo->m_ClientId ? GameClient()->m_GameSkin.m_SpriteFlagBlue : GameClient()->m_GameSkin.m_SpriteFlagRed);
				Graphics()->QuadsBegin();
				Graphics()->QuadsSetSubset(1.0f, 0.0f, 0.0f, 1.0f);
				IGraphics::CQuadItem QuadItem(TeeOffset, Row.y - 2.5f - Spacing / 2.0f, Row.h / 2.0f, Row.h);
				Graphics()->QuadsDrawTL(&QuadItem, 1);
				Graphics()->QuadsEnd();
			}

			// skin
			if(RenderDead)
			{
				Graphics()->TextureSet(m_DeadTeeTexture);
				Graphics()->QuadsBegin();
				if(GameClient()->IsTeamPlay())
				{
					Graphics()->SetColor(GameClient()->m_Skins7.GetTeamColor(true, 0, GameClient()->m_aClients[pInfo->m_ClientId].m_Team, protocol7::SKINPART_BODY));
				}
				CTeeRenderInfo TeeInfo = GameClient()->m_aClients[pInfo->m_ClientId].m_RenderInfo;
				TeeInfo.m_Size *= TeeSizeMod;
				IGraphics::CQuadItem QuadItem(TeeOffset, Row.y, TeeInfo.m_Size, TeeInfo.m_Size);
				Graphics()->QuadsDrawTL(&QuadItem, 1);
				Graphics()->QuadsEnd();
			}
			else
			{
				CTeeRenderInfo TeeInfo = ClientData.m_RenderInfo;
				TeeInfo.m_Size *= TeeSizeMod;
				vec2 OffsetToMid;
				CRenderTools::GetRenderTeeOffsetToRenderedTee(CAnimState::GetIdle(), &TeeInfo, OffsetToMid);
				const vec2 TeeRenderPos = vec2(TeeOffset + TeeLength / 2, Row.y + Row.h / 2.0f + OffsetToMid.y);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeeRenderPos);

				if(m_MouseUnlocked)
				{
					const CUIRect SkinRect = {TeeOffset, Row.y, TeeLength, Row.h};
					GameClient()->m_Tooltips.DoToolTip(&m_aPlayers[pInfo->m_ClientId].m_PlayerButtonId, &SkinRect, ClientData.m_aSkinName);
				}
			}

			const float TextY = Row.y + (Row.h - FontSize) / 2.0f;

			// name
			{
				if(g_Config.m_ClShowIds)
				{
					char aClientId[16];
					GameClient()->FormatClientId(pInfo->m_ClientId, aClientId, EClientIdFormat::INDENT_AUTO);
					str_copy(aBuf, aClientId);
					str_append(aBuf, ClientData.m_aName);
				}
				else
				{
					str_copy(aBuf, ClientData.m_aName);
				}
				ColorRGBA NameColor = TextColor;
				if(ClientData.m_AuthLevel)
				{
					NameColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClAuthedPlayerColor));
				}
				// TClient
				if(pInfo->m_ClientId >= 0 && g_Config.m_TcWarList && g_Config.m_TcWarListScoreboard && GameClient()->m_WarList.GetAnyWar(pInfo->m_ClientId))
				{
					NameColor = GameClient()->m_WarList.GetNameplateColor(pInfo->m_ClientId);
				}

				float NameTextOffset = NameOffset;
				const bool Muted = pInfo->m_ClientId >= 0 && (GameClient()->m_aClients[pInfo->m_ClientId].m_Foe || GameClient()->m_aClients[pInfo->m_ClientId].m_ChatIgnore);
				if(Muted)
				{
					TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
					Player.m_MuteMark.Update(TextRender(), FontIcon::COMMENT_SLASH, FontSize);
					TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
					Player.m_MuteMark.Render(TextRender(), vec2(NameTextOffset, TextY), NameColor);
					NameTextOffset += Player.m_MuteMark.Width();
				}
				Player.m_Name.Update(TextRender(), aBuf, FontSize, std::max(NameLength - (NameTextOffset - NameOffset), 0.0f), TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END);
				Player.m_Name.Render(TextRender(), vec2(NameTextOffset, TextY), NameColor);

				// ready / watching
				if(Client()->IsSixup() && Client()->m_TranslationContext.m_aClients[pInfo->m_ClientId].m_PlayerFlags7 & protocol7::PLAYERFLAG_READY)
				{
					Player.m_ReadyMark.Update(TextRender(), "✓", FontSize);
					Player.m_ReadyMark.Render(TextRender(), vec2(NameTextOffset + Player.m_Name.Width(), TextY), ColorRGBA(0.1f, 1.0f, 0.1f, TextColor.a));
				}
			}

			// clan
			{
				ColorRGBA ClanColor = TextColor;
				if(GameClient()->m_aLocalIds[g_Config.m_ClDummy] >= 0 && str_comp(ClientData.m_aClan, GameClient()->m_aClients[GameClient()->m_aLocalIds[g_Config.m_ClDummy]].m_aClan) == 0)
				{
					ClanColor = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClSameClanColor));
				}
				// TClient
				if(pInfo->m_ClientId >= 0 && g_Config.m_TcWarList && g_Config.m_TcWarListScoreboard && GameClient()->m_WarList.GetAnyWar(pInfo->m_ClientId))
				{
					ClanColor = GameClient()->m_WarList.GetClanColor(pInfo->m_ClientId);
				}

				Player.m_Clan.Update(TextRender(), ClientData.m_aClan, FontSize, ClanLength, TEXTFLAG_RENDER | TEXTFLAG_ELLIPSIS_AT_END);
				Player.m_Clan.Render(TextRender(), vec2(ClanOffset + (ClanLength - std::min(Player.m_Clan.Width(), ClanLength)) / 2.0f, TextY), ClanColor);
			}

			// country flag
			GameClient()->m_CountryFlags.Render(ClientData.m_Country, ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f),
				CountryOffset, Row.y + (Spacing + TeeSizeMod * 5.0f) / 2.0f, CountryLength, Row.h - Spacing - TeeSizeMod * 5.0f);

			// ping
			ColorRGBA PingColor = TextRender()->DefaultTextColor();
			if(g_Config.m_ClEnablePingColor)
			{
				PingColor = color_cast<ColorRGBA>(ColorHSLA((300.0f - std::clamp(pInfo->m_Latency, 0, 300)) / 1000.0f, 1.0f, 0.5f));
			}
			str_format(aBuf, sizeof(aBuf), "%d", std::clamp(pInfo->m_Latency, 0, 999));
			Player.m_Ping.Update(TextRender(), aBuf, FontSize);
			Player.m_Ping.Render(TextRender(), vec2(PingOffset + PingLength - Player.m_Ping.Width(), TextY), PingColor);
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			if(CountRendered == CountEnd)
				break;
		}
		if(CountRendered == CountEnd)
			break;
	}
}

void CScoreboard::RenderRecordingNotification(float x)
{
	char aBuf[512] = "";

	const auto &&AppendRecorderInfo = [&](int Recorder, const char *pName) {
		if(GameClient()->DemoRecorder(Recorder)->IsRecording())
		{
			char aTime[32];
			str_time((int64_t)GameClient()->DemoRecorder(Recorder)->Length() * 100, ETimeFormat::HOURS, aTime, sizeof(aTime));
			str_append(aBuf, pName);
			str_append(aBuf, " ");
			str_append(aBuf, aTime);
			str_append(aBuf, "  ");
		}
	};

	AppendRecorderInfo(RECORDER_MANUAL, Localize("Manual"));
	AppendRecorderInfo(RECORDER_RACE, Localize("Race"));
	AppendRecorderInfo(RECORDER_AUTO, Localize("Auto"));
	AppendRecorderInfo(RECORDER_REPLAYS, Localize("Replay"));

	if(aBuf[0] == '\0')
		return;

	const float FontSize = 10.0f;

	CUIRect Rect = {x, 0.0f, TextRender()->TextWidth(FontSize, aBuf) + 30.0f, 25.0f};
	Rect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.4f), IGraphics::CORNER_B, 7.5f);
	Rect.VSplitLeft(10.0f, nullptr, &Rect);
	Rect.VSplitRight(5.0f, &Rect, nullptr);

	CUIRect Circle;
	Rect.VSplitLeft(10.0f, &Circle, &Rect);
	Circle.HMargin((Circle.h - Circle.w) / 2.0f, &Circle);
	Circle.Draw(ColorRGBA(1.0f, 0.0f, 0.0f, 1.0f), IGraphics::CORNER_ALL, Circle.h / 2.0f);

	Rect.VSplitLeft(5.0f, nullptr, &Rect);
	Ui()->DoLabel(&Rect, aBuf, FontSize, TEXTALIGN_ML);
}

void CScoreboard::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!IsActive())
	{
		// lock mouse if scoreboard was opened by being dead or game pause
		if(m_MouseUnlocked)
		{
			LockMouse();
		}
		return;
	}

	if(!GameClient()->m_Menus.IsActive() && !GameClient()->m_Chat.IsActive())
	{
		Ui()->StartCheck();
		Ui()->Update();
	}

	// if the score board is active, then we should clear the motd message as well
	if(GameClient()->m_Motd.IsActive())
		GameClient()->m_Motd.Clear();

	const CUIRect Screen = *Ui()->Screen();
	Ui()->MapScreen();

	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	const bool Teams = GameClient()->IsTeamPlay();
	const auto &aTeamSize = GameClient()->m_Snap.m_aTeamSize;
	const int NumPlayers = Teams ? std::max(aTeamSize[TEAM_RED], aTeamSize[TEAM_BLUE]) : aTeamSize[TEAM_RED];

	const float ScoreboardSmallWidth = 375.0f + 10.0f;
	const float ScoreboardWidth = !Teams && NumPlayers <= 16 ? ScoreboardSmallWidth : 750.0f;
	const float TitleHeight = 30.0f;

	CUIRect Scoreboard = {(Screen.w - ScoreboardWidth) / 2.0f, 75.0f, ScoreboardWidth, 355.0f + TitleHeight};
	CScoreboardRenderState RenderState{};

	if(Teams)
	{
		const char *pRedTeamName = GetTeamName(TEAM_RED);
		const char *pBlueTeamName = GetTeamName(TEAM_BLUE);

		// Game over title
		const CNetObj_GameData *pGameDataObj = GameClient()->m_Snap.m_pGameDataObj;
		if((pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER) && pGameDataObj)
		{
			char aTitle[256];
			if(pGameDataObj->m_TeamscoreRed > pGameDataObj->m_TeamscoreBlue)
			{
				TextRender()->TextColor(ColorRGBA(0.975f, 0.17f, 0.17f, 1.0f));
				if(pRedTeamName == nullptr)
				{
					str_copy(aTitle, Localize("Red team wins!"));
				}
				else
				{
					str_format(aTitle, sizeof(aTitle), Localize("%s wins!"), pRedTeamName);
				}
			}
			else if(pGameDataObj->m_TeamscoreBlue > pGameDataObj->m_TeamscoreRed)
			{
				TextRender()->TextColor(ColorRGBA(0.17f, 0.46f, 0.975f, 1.0f));
				if(pBlueTeamName == nullptr)
				{
					str_copy(aTitle, Localize("Blue team wins!"));
				}
				else
				{
					str_format(aTitle, sizeof(aTitle), Localize("%s wins!"), pBlueTeamName);
				}
			}
			else
			{
				TextRender()->TextColor(ColorRGBA(0.91f, 0.78f, 0.33f, 1.0f));
				str_copy(aTitle, Localize("Draw!"));
			}

			const float TitleFontSize = 36.0f;
			CUIRect GameOverTitle = {Scoreboard.x, Scoreboard.y - TitleFontSize - 6.0f, Scoreboard.w, TitleFontSize};
			Ui()->DoLabel(&GameOverTitle, aTitle, TitleFontSize, TEXTALIGN_MC);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}

		CUIRect RedScoreboard, BlueScoreboard, RedTitle, BlueTitle;
		Scoreboard.VSplitMid(&RedScoreboard, &BlueScoreboard, 7.5f);
		RedScoreboard.HSplitTop(TitleHeight, &RedTitle, &RedScoreboard);
		BlueScoreboard.HSplitTop(TitleHeight, &BlueTitle, &BlueScoreboard);

		RedTitle.Draw(ColorRGBA(0.975f, 0.17f, 0.17f, 0.5f), IGraphics::CORNER_T, 7.5f);
		BlueTitle.Draw(ColorRGBA(0.17f, 0.46f, 0.975f, 0.5f), IGraphics::CORNER_T, 7.5f);
		RedScoreboard.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_B, 7.5f);
		BlueScoreboard.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_B, 7.5f);

		RenderTitleBar(RedTitle, TEAM_RED, pRedTeamName == nullptr ? Localize("Red team") : pRedTeamName);
		RenderTitleBar(BlueTitle, TEAM_BLUE, pBlueTeamName == nullptr ? Localize("Blue team") : pBlueTeamName);

		auto RenderTeamScoreboard = [&](CUIRect TeamScoreboard, int Team, int TeamSize) {
			if(TeamSize <= 64)
			{
				RenderScoreboard(TeamScoreboard, Team, 0, TeamSize, RenderState);
			}
			else
			{
				const int FirstColumnSize = 64;
				CUIRect LeftColumn, RightColumn;
				TeamScoreboard.VSplitMid(&LeftColumn, &RightColumn, 2.5f);

				RenderScoreboard(LeftColumn, Team, 0, FirstColumnSize, RenderState, FirstColumnSize);
				RenderScoreboard(RightColumn, Team, FirstColumnSize, TeamSize, RenderState, FirstColumnSize);
			}
		};

		RenderTeamScoreboard(RedScoreboard, TEAM_RED, aTeamSize[TEAM_RED]);
		RenderTeamScoreboard(BlueScoreboard, TEAM_BLUE, aTeamSize[TEAM_BLUE]);
	}
	else
	{
		Scoreboard.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.5f), IGraphics::CORNER_ALL, 7.5f);

		const char *pTitle;
		if(pGameInfoObj && (pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER))
		{
			pTitle = Localize("Game over");
		}
		else
		{
			pTitle = GameClient()->Map()->BaseName();
		}

		CUIRect Title;
		Scoreboard.HSplitTop(TitleHeight, &Title, &Scoreboard);
		RenderTitleBar(Title, TEAM_GAME, pTitle);

		if(NumPlayers <= 16)
		{
			RenderScoreboard(Scoreboard, TEAM_GAME, 0, NumPlayers, RenderState);
		}
		else if(NumPlayers <= 64)
		{
			int PlayersPerSide;
			if(NumPlayers <= 24)
				PlayersPerSide = 12;
			else if(NumPlayers <= 32)
				PlayersPerSide = 16;
			else if(NumPlayers <= 48)
				PlayersPerSide = 24;
			else
				PlayersPerSide = 32;

			CUIRect LeftScoreboard, RightScoreboard;
			Scoreboard.VSplitMid(&LeftScoreboard, &RightScoreboard);
			RenderScoreboard(LeftScoreboard, TEAM_GAME, 0, PlayersPerSide, RenderState);
			RenderScoreboard(RightScoreboard, TEAM_GAME, PlayersPerSide, 2 * PlayersPerSide, RenderState);
		}
		else
		{
			const int NumColumns = 3;
			const int PlayersPerColumn = std::ceil(128.0f / NumColumns);
			CUIRect RemainingScoreboard = Scoreboard;
			for(int i = 0; i < NumColumns; ++i)
			{
				CUIRect Column;
				RemainingScoreboard.VSplitLeft(Scoreboard.w / NumColumns, &Column, &RemainingScoreboard);
				RenderScoreboard(Column, TEAM_GAME, i * PlayersPerColumn, (i + 1) * PlayersPerColumn, RenderState);
			}
		}
	}

	CUIRect Spectators = {(Screen.w - ScoreboardSmallWidth) / 2.0f, Scoreboard.y + Scoreboard.h + 5.0f, ScoreboardSmallWidth, 100.0f};
	if(pGameInfoObj && (pGameInfoObj->m_ScoreLimit || pGameInfoObj->m_TimeLimit || (pGameInfoObj->m_RoundNum && pGameInfoObj->m_RoundCurrent)))
	{
		CUIRect Goals;
		Spectators.HSplitTop(25.0f, &Goals, &Spectators);
		Spectators.HSplitTop(5.0f, nullptr, &Spectators);
		RenderGoals(Goals);
	}
	RenderSpectators(Spectators);

	RenderRecordingNotification((Screen.w / 7) * 4 + 10);

	if(!GameClient()->m_Menus.IsActive() && !GameClient()->m_Chat.IsActive())
	{
		Ui()->RenderPopupMenus();

		if(m_MouseUnlocked)
			RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);

		Ui()->FinishCheck();
	}
}

bool CScoreboard::IsActive() const
{
	// if statboard is active don't show scoreboard
	if(GameClient()->m_Statboard.IsActive())
		return false;

	if(m_MouseUnlocked && Ui()->IsPopupOpen(&m_ScoreboardPopupContext))
		return true;

	if(m_Active)
		return true;

	const CNetObj_GameInfo *pGameInfoObj = GameClient()->m_Snap.m_pGameInfoObj;
	if(GameClient()->m_Snap.m_pLocalInfo && !GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		// we are not a spectator, check if we are dead and the game isn't paused
		if(!GameClient()->m_Snap.m_pLocalCharacter && g_Config.m_ClScoreboardOnDeath &&
			!(pGameInfoObj && pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_PAUSED))
			return true;
	}

	// if the game is over
	if(pGameInfoObj && pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER)
		return true;

	return false;
}

const char *CScoreboard::GetTeamName(int Team) const
{
	dbg_assert(Team == TEAM_RED || Team == TEAM_BLUE, "Team invalid");

	int ClanPlayers = 0;
	const char *pClanName = nullptr;
	for(const CNetObj_PlayerInfo *pInfo : GameClient()->m_Snap.m_apInfoByScore)
	{
		if(!pInfo || pInfo->m_Team != Team)
			continue;

		if(!pClanName)
		{
			pClanName = GameClient()->m_aClients[pInfo->m_ClientId].m_aClan;
			ClanPlayers++;
		}
		else
		{
			if(str_comp(GameClient()->m_aClients[pInfo->m_ClientId].m_aClan, pClanName) == 0)
				ClanPlayers++;
			else
				return nullptr;
		}
	}

	if(ClanPlayers > 1 && pClanName[0] != '\0')
		return pClanName;
	else
		return nullptr;
}

void CScoreboard::SaveModReplay(const char *pAction, const char *pTargetName)
{
	char aCleanName[MAX_NAME_LENGTH];
	str_copy(aCleanName, pTargetName, sizeof(aCleanName));
	for(char &ch : aCleanName)
	{
		if(ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' || ch == '"' || ch == '<' || ch == '>' || ch == '|')
			ch = '_';
	}

	char aTimestamp[32];
	str_timestamp_format(aTimestamp, sizeof(aTimestamp), "%Y-%m-%d_%H-%M-%S");

	char aReplayName[128];
	str_format(aReplayName, sizeof(aReplayName), "mod_%s_%s_%s", pAction, aCleanName, aTimestamp);

	char aFullReplayName[128];
	str_format(aFullReplayName, sizeof(aFullReplayName), "mod_%s_%s_%s_full", pAction, aCleanName, aTimestamp);

	if(!g_Config.m_ClReplays)
	{
		g_Config.m_ClReplays = 1;
		Client()->DemoRecorder_UpdateReplayRecorder();
	}

	Client()->SaveReplay(180, aReplayName, aFullReplayName);

	char aMsg[256];
	str_format(aMsg, sizeof(aMsg), "Demos guardadas: 3 min (%s.demo) y completa (%s.demo) en demos/replays/", aReplayName, aFullReplayName);
	GameClient()->m_Chat.Echo(aMsg);
}

static void FormatDurationSeconds(char *pBuf, int Size, int Secs)
{
	if(Secs >= 86400 && Secs % 86400 == 0)
		str_format(pBuf, Size, "%dd", Secs / 86400);
	else if(Secs >= 3600 && Secs % 3600 == 0)
		str_format(pBuf, Size, "%dh", Secs / 3600);
	else if(Secs >= 60 && Secs % 60 == 0)
		str_format(pBuf, Size, "%dm", Secs / 60);
	else
		str_format(pBuf, Size, "%ds", Secs);
}

static void FormatDurationMinutes(char *pBuf, int Size, int Mins)
{
	if(Mins >= 10080 && Mins % 10080 == 0)
		str_format(pBuf, Size, "%dw", Mins / 10080);
	else if(Mins >= 1440 && Mins % 1440 == 0)
		str_format(pBuf, Size, "%dd", Mins / 1440);
	else if(Mins >= 60 && Mins % 60 == 0)
		str_format(pBuf, Size, "%dh", Mins / 60);
	else
		str_format(pBuf, Size, "%dm", Mins);
}

void CScoreboard::CScoreboardPopupContext::Reset(int ClientId, bool IsSpectating, bool IsLocal)
{
	m_ClientId = ClientId;
	m_IsSpectating = IsSpectating;
	m_IsLocal = IsLocal;
	m_SubMenu = SUBMENU_MAIN;
	m_SecondsInput.SetInteger(g_Config.m_ClModMuteSec4);
	m_MinutesInput.SetInteger(g_Config.m_ClModBanMin2);
	m_ReasonInput.Clear();
}

CUi::EPopupMenuFunctionResult CScoreboard::CScoreboardPopupContext::Render(void *pContext, CUIRect View, bool Active)
{
	CScoreboardPopupContext *pPopupContext = static_cast<CScoreboardPopupContext *>(pContext);
	CScoreboard *pScoreboard = pPopupContext->m_pScoreboard;
	CUi *pUi = pPopupContext->m_pScoreboard->Ui();

	if(pPopupContext->m_ClientId < 0 || pPopupContext->m_ClientId >= MAX_CLIENTS)
		return CUi::POPUP_CLOSE_CURRENT;

	CGameClient::CClientData &Client = pScoreboard->GameClient()->m_aClients[pPopupContext->m_ClientId];

	if(!Client.m_Active)
		return CUi::POPUP_CLOSE_CURRENT;

	const float Margin = 5.0f;
	View.Margin(Margin, &View);

	CUIRect Label, Container, Action;
	const float ItemSpacing = 2.0f;
	const float FontSize = 12.0f;
	const float ButtonSize = 17.5f;

	if(pPopupContext->m_SubMenu == SUBMENU_MAIN)
	{
		char aHeader[128];
		if(pScoreboard->Client()->RconAuthed() && !pPopupContext->m_IsLocal)
			str_format(aHeader, sizeof(aHeader), "%s (ID: %d)", Client.m_aName, pPopupContext->m_ClientId);
		else
			str_copy(aHeader, Client.m_aName);

		View.HSplitTop(FontSize, &Label, &View);
		pUi->DoLabel(&Label, aHeader, FontSize, TEXTALIGN_ML);

		if(!pPopupContext->m_IsLocal)
		{
			const int ActionsNum = 3;
			const float ActionSize = 25.0f;
			const float ActionSpacing = (View.w - (ActionsNum * ActionSize)) / 2;
			int ActionCorners = IGraphics::CORNER_ALL;

			View.HSplitTop(ItemSpacing * 2, nullptr, &View);
			View.HSplitTop(ActionSize, &Container, &View);

			Container.VSplitLeft(ActionSize, &Action, &Container);

			ColorRGBA FriendActionColor = Client.m_Friend ? ColorRGBA(0.95f, 0.3f, 0.3f, 0.85f * pUi->ButtonColorMul(&pPopupContext->m_FriendAction)) :
									ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * pUi->ButtonColorMul(&pPopupContext->m_FriendAction));
			const char *pFriendActionIcon = pUi->HotItem() == &pPopupContext->m_FriendAction && Client.m_Friend ? FontIcon::HEART_CRACK : FontIcon::HEART;
			if(pUi->DoButton_FontIcon(&pPopupContext->m_FriendAction, pFriendActionIcon, Client.m_Friend, &Action, BUTTONFLAG_LEFT, ActionCorners, true, FriendActionColor))
			{
				if(Client.m_Friend)
				{
					pScoreboard->GameClient()->Friends()->RemoveFriend(Client.m_aName, Client.m_aClan);
				}
				else
				{
					pScoreboard->GameClient()->Friends()->AddFriend(Client.m_aName, Client.m_aClan);
				}
			}

			pScoreboard->GameClient()->m_Tooltips.DoToolTip(&pPopupContext->m_FriendAction, &Action, Client.m_Friend ? Localize("Remove friend") : Localize("Add friend"));

			Container.VSplitLeft(ActionSpacing, nullptr, &Container);
			Container.VSplitLeft(ActionSize, &Action, &Container);

			if(pUi->DoButton_FontIcon(&pPopupContext->m_MuteAction, FontIcon::BAN, Client.m_ChatIgnore, &Action, BUTTONFLAG_LEFT, ActionCorners))
			{
				Client.m_ChatIgnore ^= 1;
			}
			pScoreboard->GameClient()->m_Tooltips.DoToolTip(&pPopupContext->m_MuteAction, &Action, Client.m_ChatIgnore ? Localize("Unmute") : Localize("Mute"));

			Container.VSplitLeft(ActionSpacing, nullptr, &Container);
			Container.VSplitLeft(ActionSize, &Action, &Container);

			const char *EmoticonActionIcon = Client.m_EmoticonIgnore ? FontIcon::COMMENT_SLASH : FontIcon::COMMENT;
			if(pUi->DoButton_FontIcon(&pPopupContext->m_EmoticonAction, EmoticonActionIcon, Client.m_EmoticonIgnore, &Action, BUTTONFLAG_LEFT, ActionCorners))
			{
				Client.m_EmoticonIgnore ^= 1;
			}
			pScoreboard->GameClient()->m_Tooltips.DoToolTip(&pPopupContext->m_EmoticonAction, &Action, Client.m_EmoticonIgnore ? Localize("Unmute emoticons") : Localize("Mute emoticons"));
		}

		View.HSplitTop(ItemSpacing * 2, nullptr, &View);
		View.HSplitTop(ButtonSize, &Container, &View);

		bool IsSpectating = pScoreboard->GameClient()->m_Snap.m_SpecInfo.m_Active && pScoreboard->GameClient()->m_Snap.m_SpecInfo.m_SpectatorId == pPopupContext->m_ClientId;
		ColorRGBA SpectateButtonColor = ColorRGBA(1.0f, 1.0f, 1.0f, (IsSpectating ? 0.25f : 0.5f) * pUi->ButtonColorMul(&pPopupContext->m_SpectateButton));
		if(!pPopupContext->m_IsSpectating)
		{
			if(pUi->DoButton_PopupMenu(&pPopupContext->m_SpectateButton, Localize("Spectate"), &Container, FontSize, TEXTALIGN_MC, 0.0f, false, true, SpectateButtonColor))
			{
				if(IsSpectating)
				{
					pScoreboard->GameClient()->m_Spectator.Spectate(SPEC_FREEVIEW);
					pScoreboard->Console()->ExecuteLine("say /spec", IConsole::CLIENT_ID_UNSPECIFIED);
				}
				else
				{
					if(pScoreboard->GameClient()->m_Snap.m_SpecInfo.m_Active)
					{
						pScoreboard->GameClient()->m_Spectator.Spectate(pPopupContext->m_ClientId);
					}
					else
					{
						// escape the name
						char aEscapedCommand[2 * MAX_NAME_LENGTH + 32];
						str_copy(aEscapedCommand, "say /spec \"");
						char *pDst = aEscapedCommand + str_length(aEscapedCommand);
						str_escape(&pDst, Client.m_aName, aEscapedCommand + sizeof(aEscapedCommand));
						str_append(aEscapedCommand, "\"");

						pScoreboard->Console()->ExecuteLine(aEscapedCommand, IConsole::CLIENT_ID_UNSPECIFIED);
					}
				}
			}
		}

		if(!pPopupContext->m_IsLocal)
		{
			View.HSplitTop(ItemSpacing * 2, nullptr, &View);
			View.HSplitTop(ButtonSize, &Container, &View);
			ColorRGBA StealButtonColor = ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f * pUi->ButtonColorMul(&pPopupContext->m_StealSkinButton));
			if(pUi->DoButton_PopupMenu(&pPopupContext->m_StealSkinButton, TCLocalize("Steal Skin"), &Container, FontSize, TEXTALIGN_MC, 0.0f, false, true, StealButtonColor))
			{
				pScoreboard->GameClient()->StealSkin(pPopupContext->m_ClientId);
				if(!pScoreboard->m_Active)
					pScoreboard->LockMouse();
				return CUi::POPUP_CLOSE_CURRENT;
			}
		}

		// Profile buttons (available for both local and remote players)
		View.HSplitTop(ItemSpacing * 2, nullptr, &View);
		View.HSplitTop(ButtonSize, &Container, &View);
		ColorRGBA ProfileBtnColor = ColorRGBA(0.3f, 0.7f, 1.0f, 0.6f * pUi->ButtonColorMul(&pPopupContext->m_ProfileButton));
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_ProfileButton, TCLocalize("Ver Perfil DDNet"), &Container, FontSize - 1.0f, TEXTALIGN_MC, 0.0f, false, true, ProfileBtnColor))
		{
			pScoreboard->GameClient()->m_PlayerProfiles.FetchProfile(Client.m_aName);
			g_Config.m_UiPage = CMenus::PAGE_SETTINGS;
			g_Config.m_UiSettingsPage = CMenus::SETTINGS_ACLIENT;
			pScoreboard->GameClient()->m_Menus.SetAClientTab(CMenus::ACLIENT_TAB_PROFILES);
			pScoreboard->GameClient()->m_Menus.SetActive(true);
			return CUi::POPUP_CLOSE_CURRENT;
		}

		// Web links row (DDNet Web | KoG Web)
		View.HSplitTop(ItemSpacing * 1.5f, nullptr, &View);
		View.HSplitTop(ButtonSize, &Container, &View);
		CUIRect WebDDNetBtn, WebKoGBtn;
		Container.VSplitMid(&WebDDNetBtn, &WebKoGBtn, ItemSpacing);

		ColorRGBA DDNetWebColor = ColorRGBA(0.4f, 0.8f, 0.4f, 0.6f * pUi->ButtonColorMul(&pPopupContext->m_ProfileWebDDNetButton));
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_ProfileWebDDNetButton, TCLocalize("DDNet Web"), &WebDDNetBtn, FontSize - 2.0f, TEXTALIGN_MC, 0.0f, false, true, DDNetWebColor))
		{
			pScoreboard->GameClient()->m_PlayerProfiles.OpenDDNetWeb(Client.m_aName);
			return CUi::POPUP_CLOSE_CURRENT;
		}

		ColorRGBA KoGWebColor = ColorRGBA(1.0f, 0.6f, 0.2f, 0.6f * pUi->ButtonColorMul(&pPopupContext->m_ProfileWebKoGButton));
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_ProfileWebKoGButton, TCLocalize("KoG Web"), &WebKoGBtn, FontSize - 2.0f, TEXTALIGN_MC, 0.0f, false, true, KoGWebColor))
		{
			pScoreboard->GameClient()->m_PlayerProfiles.OpenKoGWeb(Client.m_aName);
			return CUi::POPUP_CLOSE_CURRENT;
		}

		// Moderation Section (RCON authed only)
		if(pScoreboard->Client()->RconAuthed() && !pPopupContext->m_IsLocal)
		{
			View.HSplitTop(ItemSpacing * 2.5f, nullptr, &View);
			View.HSplitTop(FontSize + 1.0f, &Label, &View);
			Label.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f), IGraphics::CORNER_ALL, 3.0f);
			pUi->DoLabel(&Label, TCLocalize("--- Moderación ---"), FontSize - 2.0f, TEXTALIGN_MC);

			// Mute button
			View.HSplitTop(ItemSpacing, nullptr, &View);
			View.HSplitTop(ButtonSize, &Container, &View);
			ColorRGBA MuteBtnColor = ColorRGBA(1.0f, 0.7f, 0.2f, 0.6f * pUi->ButtonColorMul(&pPopupContext->m_ModMuteButton));
			if(pUi->DoButton_PopupMenu(&pPopupContext->m_ModMuteButton, TCLocalize("Mutear ID..."), &Container, FontSize, TEXTALIGN_MC, 0.0f, false, true, MuteBtnColor))
			{
				pPopupContext->m_SubMenu = SUBMENU_MUTE;
			}

			// Kick button
			View.HSplitTop(ItemSpacing, nullptr, &View);
			View.HSplitTop(ButtonSize, &Container, &View);
			ColorRGBA KickBtnColor = ColorRGBA(0.95f, 0.45f, 0.2f, 0.6f * pUi->ButtonColorMul(&pPopupContext->m_ModKickButton));
			if(pUi->DoButton_PopupMenu(&pPopupContext->m_ModKickButton, TCLocalize("Kickear..."), &Container, FontSize, TEXTALIGN_MC, 0.0f, false, true, KickBtnColor))
			{
				pPopupContext->m_SubMenu = SUBMENU_KICK;
			}

			// Ban button
			View.HSplitTop(ItemSpacing, nullptr, &View);
			View.HSplitTop(ButtonSize, &Container, &View);
			ColorRGBA BanBtnColor = ColorRGBA(0.95f, 0.25f, 0.25f, 0.6f * pUi->ButtonColorMul(&pPopupContext->m_ModBanButton));
			if(pUi->DoButton_PopupMenu(&pPopupContext->m_ModBanButton, TCLocalize("Banear..."), &Container, FontSize, TEXTALIGN_MC, 0.0f, false, true, BanBtnColor))
			{
				pPopupContext->m_SubMenu = SUBMENU_BAN;
			}
		}
	}
	else if(pPopupContext->m_SubMenu == SUBMENU_MUTE)
	{
		char aHeader[128];
		str_format(aHeader, sizeof(aHeader), "%s: %s (ID: %d)", TCLocalize("Mute"), Client.m_aName, pPopupContext->m_ClientId);
		View.HSplitTop(FontSize + 2.0f, &Label, &View);
		pUi->DoLabel(&Label, aHeader, FontSize - 1.0f, TEXTALIGN_ML);

		View.HSplitTop(ItemSpacing, nullptr, &View);

		// Presets Row 1 (configured)
		View.HSplitTop(ButtonSize - 1.0f, &Container, &View);
		const float W3 = (Container.w - ItemSpacing * 2.0f) / 3.0f;
		CUIRect Btn;
		char aSecBuf[32];

		Container.VSplitLeft(W3, &Btn, &Container);
		FormatDurationSeconds(aSecBuf, sizeof(aSecBuf), g_Config.m_ClModMuteSec1);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_aSecondsPreset[0], aSecBuf, &Btn, FontSize - 2.0f, TEXTALIGN_MC))
			pPopupContext->m_SecondsInput.SetInteger(g_Config.m_ClModMuteSec1);

		Container.VSplitLeft(ItemSpacing, nullptr, &Container);
		Container.VSplitLeft(W3, &Btn, &Container);
		FormatDurationSeconds(aSecBuf, sizeof(aSecBuf), g_Config.m_ClModMuteSec2);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_aSecondsPreset[1], aSecBuf, &Btn, FontSize - 2.0f, TEXTALIGN_MC))
			pPopupContext->m_SecondsInput.SetInteger(g_Config.m_ClModMuteSec2);

		Container.VSplitLeft(ItemSpacing, nullptr, &Container);
		FormatDurationSeconds(aSecBuf, sizeof(aSecBuf), g_Config.m_ClModMuteSec3);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_aSecondsPreset[2], aSecBuf, &Container, FontSize - 2.0f, TEXTALIGN_MC))
			pPopupContext->m_SecondsInput.SetInteger(g_Config.m_ClModMuteSec3);

		// Presets Row 2 (configured)
		View.HSplitTop(ItemSpacing, nullptr, &View);
		View.HSplitTop(ButtonSize - 1.0f, &Container, &View);

		Container.VSplitLeft(W3, &Btn, &Container);
		FormatDurationSeconds(aSecBuf, sizeof(aSecBuf), g_Config.m_ClModMuteSec4);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_aSecondsPreset[3], aSecBuf, &Btn, FontSize - 2.0f, TEXTALIGN_MC))
			pPopupContext->m_SecondsInput.SetInteger(g_Config.m_ClModMuteSec4);

		Container.VSplitLeft(ItemSpacing, nullptr, &Container);
		Container.VSplitLeft(W3, &Btn, &Container);
		FormatDurationSeconds(aSecBuf, sizeof(aSecBuf), g_Config.m_ClModMuteSec5);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_aSecondsPreset[4], aSecBuf, &Btn, FontSize - 2.0f, TEXTALIGN_MC))
			pPopupContext->m_SecondsInput.SetInteger(g_Config.m_ClModMuteSec5);

		Container.VSplitLeft(ItemSpacing, nullptr, &Container);
		FormatDurationSeconds(aSecBuf, sizeof(aSecBuf), g_Config.m_ClModMuteSec6);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_aSecondsPreset[5], aSecBuf, &Container, FontSize - 2.0f, TEXTALIGN_MC))
			pPopupContext->m_SecondsInput.SetInteger(g_Config.m_ClModMuteSec6);

		// Seconds Input Box
		View.HSplitTop(ItemSpacing * 2.0f, nullptr, &View);
		View.HSplitTop(ButtonSize, &Container, &View);
		CUIRect InputLabel, InputBox;
		Container.VSplitLeft(38.0f, &InputLabel, &InputBox);
		pUi->DoLabel(&InputLabel, TCLocalize("Seg:"), FontSize - 1.0f, TEXTALIGN_ML);
		pUi->DoEditBox(&pPopupContext->m_SecondsInput, &InputBox, FontSize - 1.0f);

		// Reason Presets (configured)
		View.HSplitTop(ItemSpacing, nullptr, &View);
		View.HSplitTop(ButtonSize - 1.0f, &Container, &View);
		Container.VSplitLeft(W3, &Btn, &Container);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_aReasonPreset[0], g_Config.m_ClModMuteReason1, &Btn, FontSize - 2.0f, TEXTALIGN_MC))
			pPopupContext->m_ReasonInput.Set(g_Config.m_ClModMuteReason1);
		Container.VSplitLeft(ItemSpacing, nullptr, &Container);
		Container.VSplitLeft(W3, &Btn, &Container);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_aReasonPreset[1], g_Config.m_ClModMuteReason2, &Btn, FontSize - 2.0f, TEXTALIGN_MC))
			pPopupContext->m_ReasonInput.Set(g_Config.m_ClModMuteReason2);
		Container.VSplitLeft(ItemSpacing, nullptr, &Container);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_aReasonPreset[2], g_Config.m_ClModMuteReason3, &Container, FontSize - 2.0f, TEXTALIGN_MC))
			pPopupContext->m_ReasonInput.Set(g_Config.m_ClModMuteReason3);

		// Reason Input Box
		View.HSplitTop(ItemSpacing, nullptr, &View);
		View.HSplitTop(ButtonSize, &Container, &View);
		Container.VSplitLeft(38.0f, &InputLabel, &InputBox);
		pUi->DoLabel(&InputLabel, TCLocalize("Razón:"), FontSize - 1.0f, TEXTALIGN_ML);
		pUi->DoEditBox(&pPopupContext->m_ReasonInput, &InputBox, FontSize - 1.0f);

		// Action Buttons: [Atrás] [Unmute] [Mutear]
		View.HSplitTop(ItemSpacing * 2.5f, nullptr, &View);
		View.HSplitTop(ButtonSize + 1.0f, &Container, &View);
		CUIRect BackBtn, UnmuteBtn, ConfirmBtn;
		Container.VSplitLeft(38.0f, &BackBtn, &Container);
		Container.VSplitLeft(ItemSpacing, nullptr, &Container);
		Container.VSplitLeft(52.0f, &UnmuteBtn, &ConfirmBtn);
		ConfirmBtn.VSplitLeft(ItemSpacing, nullptr, &ConfirmBtn);

		if(pUi->DoButton_PopupMenu(&pPopupContext->m_ModBack, TCLocalize("Atrás"), &BackBtn, FontSize - 2.0f, TEXTALIGN_MC))
		{
			pPopupContext->m_SubMenu = SUBMENU_MAIN;
		}

		ColorRGBA UnmuteColor = ColorRGBA(0.4f, 0.7f, 1.0f, 0.6f * pUi->ButtonColorMul(&pPopupContext->m_ModUnmute));
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_ModUnmute, TCLocalize("Unmute"), &UnmuteBtn, FontSize - 2.0f, TEXTALIGN_MC, 0.0f, false, true, UnmuteColor))
		{
			char aCmd[64];
			str_format(aCmd, sizeof(aCmd), "unmuteid %d", pPopupContext->m_ClientId);
			pScoreboard->Client()->Rcon(aCmd);
			if(!pScoreboard->m_Active)
				pScoreboard->LockMouse();
			return CUi::POPUP_CLOSE_CURRENT;
		}

		ColorRGBA ConfirmColor = ColorRGBA(1.0f, 0.6f, 0.2f, 0.7f * pUi->ButtonColorMul(&pPopupContext->m_ModConfirm));
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_ModConfirm, TCLocalize("Mutear"), &ConfirmBtn, FontSize - 1.0f, TEXTALIGN_MC, 0.0f, false, true, ConfirmColor))
		{
			int Secs = pPopupContext->m_SecondsInput.GetInteger();
			if(Secs <= 0)
				Secs = 3600;
			const char *pReason = pPopupContext->m_ReasonInput.GetString();
			char aCmd[256];
			if(pReason[0] != '\0')
				str_format(aCmd, sizeof(aCmd), "muteid %d %d %s", pPopupContext->m_ClientId, Secs, pReason);
			else
				str_format(aCmd, sizeof(aCmd), "muteid %d %d", pPopupContext->m_ClientId, Secs);
			pScoreboard->Client()->Rcon(aCmd);
			if(!pScoreboard->m_Active)
				pScoreboard->LockMouse();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}
	else if(pPopupContext->m_SubMenu == SUBMENU_KICK)
	{
		char aHeader[128];
		str_format(aHeader, sizeof(aHeader), "%s: %s (ID: %d)", TCLocalize("Kick"), Client.m_aName, pPopupContext->m_ClientId);
		View.HSplitTop(FontSize + 2.0f, &Label, &View);
		pUi->DoLabel(&Label, aHeader, FontSize - 1.0f, TEXTALIGN_ML);

		View.HSplitTop(ItemSpacing * 2.0f, nullptr, &View);

		// Reason Presets (configured)
		View.HSplitTop(ButtonSize - 1.0f, &Container, &View);
		const float W3 = (Container.w - ItemSpacing * 2.0f) / 3.0f;
		CUIRect Btn;
		Container.VSplitLeft(W3, &Btn, &Container);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_aReasonPreset[0], g_Config.m_ClModKickReason1, &Btn, FontSize - 2.0f, TEXTALIGN_MC))
			pPopupContext->m_ReasonInput.Set(g_Config.m_ClModKickReason1);
		Container.VSplitLeft(ItemSpacing, nullptr, &Container);
		Container.VSplitLeft(W3, &Btn, &Container);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_aReasonPreset[1], g_Config.m_ClModKickReason2, &Btn, FontSize - 2.0f, TEXTALIGN_MC))
			pPopupContext->m_ReasonInput.Set(g_Config.m_ClModKickReason2);
		Container.VSplitLeft(ItemSpacing, nullptr, &Container);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_aReasonPreset[2], g_Config.m_ClModKickReason3, &Container, FontSize - 2.0f, TEXTALIGN_MC))
			pPopupContext->m_ReasonInput.Set(g_Config.m_ClModKickReason3);

		// Reason Input Box
		View.HSplitTop(ItemSpacing * 2.0f, nullptr, &View);
		View.HSplitTop(ButtonSize, &Container, &View);
		CUIRect InputLabel, InputBox;
		Container.VSplitLeft(38.0f, &InputLabel, &InputBox);
		pUi->DoLabel(&InputLabel, TCLocalize("Razón:"), FontSize - 1.0f, TEXTALIGN_ML);
		pUi->DoEditBox(&pPopupContext->m_ReasonInput, &InputBox, FontSize - 1.0f);

		// Action buttons: [Atrás] [Continuar]
		View.HSplitTop(ItemSpacing * 3.0f, nullptr, &View);
		View.HSplitTop(ButtonSize + 2.0f, &Container, &View);
		CUIRect BackBtn, NextBtn;
		Container.VSplitLeft(50.0f, &BackBtn, &Container);
		Container.VSplitLeft(ItemSpacing * 2.0f, nullptr, &NextBtn);

		if(pUi->DoButton_PopupMenu(&pPopupContext->m_ModBack, TCLocalize("Atrás"), &BackBtn, FontSize - 1.0f, TEXTALIGN_MC))
		{
			pPopupContext->m_SubMenu = SUBMENU_MAIN;
		}

		ColorRGBA NextColor = ColorRGBA(0.95f, 0.45f, 0.2f, 0.7f * pUi->ButtonColorMul(&pPopupContext->m_ModConfirm));
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_ModConfirm, TCLocalize("Continuar"), &NextBtn, FontSize - 1.0f, TEXTALIGN_MC, 0.0f, false, true, NextColor))
		{
			pPopupContext->m_SubMenu = SUBMENU_CONFIRM_KICK;
		}
	}
	else if(pPopupContext->m_SubMenu == SUBMENU_CONFIRM_KICK)
	{
		View.HSplitTop(FontSize + 4.0f, &Label, &View);
		pScoreboard->TextRender()->TextColor(ColorRGBA(1.0f, 0.35f, 0.35f, 1.0f));
		pUi->DoLabel(&Label, TCLocalize("¿Seguro de kickear?"), FontSize, TEXTALIGN_MC);
		pScoreboard->TextRender()->TextColor(pScoreboard->TextRender()->DefaultTextColor());

		View.HSplitTop(ItemSpacing * 2.0f, nullptr, &View);

		char aTarget[128];
		str_format(aTarget, sizeof(aTarget), "%s (ID: %d)", Client.m_aName, pPopupContext->m_ClientId);
		View.HSplitTop(FontSize + 2.0f, &Label, &View);
		pUi->DoLabel(&Label, aTarget, FontSize - 1.0f, TEXTALIGN_MC);

		const char *pReason = pPopupContext->m_ReasonInput.GetString();
		char aReasonStr[128];
		str_format(aReasonStr, sizeof(aReasonStr), "%s: %s", TCLocalize("Razón"), pReason[0] != '\0' ? pReason : "(ninguna)");
		View.HSplitTop(FontSize + 4.0f, &Label, &View);
		pUi->DoLabel(&Label, aReasonStr, FontSize - 2.0f, TEXTALIGN_MC);

		View.HSplitTop(ItemSpacing * 3.0f, nullptr, &View);

		View.HSplitTop(ButtonSize + 4.0f, &Container, &View);
		CUIRect CancelBtn, KickBtn;
		Container.VSplitMid(&CancelBtn, &KickBtn, ItemSpacing * 2.0f);

		if(pUi->DoButton_PopupMenu(&pPopupContext->m_ModKickConfirmNo, TCLocalize("Cancelar"), &CancelBtn, FontSize - 1.0f, TEXTALIGN_MC))
		{
			pPopupContext->m_SubMenu = SUBMENU_KICK;
		}

		ColorRGBA KickColor = ColorRGBA(0.95f, 0.25f, 0.25f, 0.8f * pUi->ButtonColorMul(&pPopupContext->m_ModKickConfirmYes));
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_ModKickConfirmYes, TCLocalize("Sí, Kickear"), &KickBtn, FontSize - 1.0f, TEXTALIGN_MC, 0.0f, false, true, KickColor))
		{
			char aCmd[256];
			if(pReason[0] != '\0')
				str_format(aCmd, sizeof(aCmd), "kick %d %s", pPopupContext->m_ClientId, pReason);
			else
				str_format(aCmd, sizeof(aCmd), "kick %d", pPopupContext->m_ClientId);
			pScoreboard->Client()->Rcon(aCmd);
			if(g_Config.m_ClModRecordOnPunish)
				pScoreboard->SaveModReplay("kick", Client.m_aName);
			if(!pScoreboard->m_Active)
				pScoreboard->LockMouse();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}
	else if(pPopupContext->m_SubMenu == SUBMENU_BAN)
	{
		char aHeader[128];
		str_format(aHeader, sizeof(aHeader), "%s: %s (ID: %d)", TCLocalize("Ban"), Client.m_aName, pPopupContext->m_ClientId);
		View.HSplitTop(FontSize + 2.0f, &Label, &View);
		pUi->DoLabel(&Label, aHeader, FontSize - 1.0f, TEXTALIGN_ML);

		View.HSplitTop(ItemSpacing * 2.0f, nullptr, &View);

		// Minutes Presets (configured)
		View.HSplitTop(ButtonSize - 1.0f, &Container, &View);
		const float W4 = (Container.w - ItemSpacing * 3.0f) / 4.0f;
		CUIRect Btn;
		char aMinBuf[32];

		Container.VSplitLeft(W4, &Btn, &Container);
		FormatDurationMinutes(aMinBuf, sizeof(aMinBuf), g_Config.m_ClModBanMin1);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_aMinutesPreset[0], aMinBuf, &Btn, FontSize - 2.0f, TEXTALIGN_MC))
			pPopupContext->m_MinutesInput.SetInteger(g_Config.m_ClModBanMin1);

		Container.VSplitLeft(ItemSpacing, nullptr, &Container);
		Container.VSplitLeft(W4, &Btn, &Container);
		FormatDurationMinutes(aMinBuf, sizeof(aMinBuf), g_Config.m_ClModBanMin2);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_aMinutesPreset[1], aMinBuf, &Btn, FontSize - 2.0f, TEXTALIGN_MC))
			pPopupContext->m_MinutesInput.SetInteger(g_Config.m_ClModBanMin2);

		Container.VSplitLeft(ItemSpacing, nullptr, &Container);
		Container.VSplitLeft(W4, &Btn, &Container);
		FormatDurationMinutes(aMinBuf, sizeof(aMinBuf), g_Config.m_ClModBanMin3);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_aMinutesPreset[2], aMinBuf, &Btn, FontSize - 2.0f, TEXTALIGN_MC))
			pPopupContext->m_MinutesInput.SetInteger(g_Config.m_ClModBanMin3);

		Container.VSplitLeft(ItemSpacing, nullptr, &Container);
		FormatDurationMinutes(aMinBuf, sizeof(aMinBuf), g_Config.m_ClModBanMin4);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_aMinutesPreset[3], aMinBuf, &Container, FontSize - 2.0f, TEXTALIGN_MC))
			pPopupContext->m_MinutesInput.SetInteger(g_Config.m_ClModBanMin4);

		// Minutes Input Box
		View.HSplitTop(ItemSpacing, nullptr, &View);
		View.HSplitTop(ButtonSize, &Container, &View);
		CUIRect InputLabel, InputBox;
		Container.VSplitLeft(38.0f, &InputLabel, &InputBox);
		pUi->DoLabel(&InputLabel, TCLocalize("Min:"), FontSize - 1.0f, TEXTALIGN_ML);
		pUi->DoEditBox(&pPopupContext->m_MinutesInput, &InputBox, FontSize - 1.0f);

		// Reason Presets (configured)
		View.HSplitTop(ItemSpacing, nullptr, &View);
		View.HSplitTop(ButtonSize - 1.0f, &Container, &View);
		const float W3 = (Container.w - ItemSpacing * 2.0f) / 3.0f;
		Container.VSplitLeft(W3, &Btn, &Container);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_aReasonPreset[0], g_Config.m_ClModBanReason1, &Btn, FontSize - 2.0f, TEXTALIGN_MC))
			pPopupContext->m_ReasonInput.Set(g_Config.m_ClModBanReason1);
		Container.VSplitLeft(ItemSpacing, nullptr, &Container);
		Container.VSplitLeft(W3, &Btn, &Container);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_aReasonPreset[1], g_Config.m_ClModBanReason2, &Btn, FontSize - 2.0f, TEXTALIGN_MC))
			pPopupContext->m_ReasonInput.Set(g_Config.m_ClModBanReason2);
		Container.VSplitLeft(ItemSpacing, nullptr, &Container);
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_aReasonPreset[2], g_Config.m_ClModBanReason3, &Container, FontSize - 2.0f, TEXTALIGN_MC))
			pPopupContext->m_ReasonInput.Set(g_Config.m_ClModBanReason3);

		// Reason Input Box
		View.HSplitTop(ItemSpacing, nullptr, &View);
		View.HSplitTop(ButtonSize, &Container, &View);
		Container.VSplitLeft(38.0f, &InputLabel, &InputBox);
		pUi->DoLabel(&InputLabel, TCLocalize("Razón:"), FontSize - 1.0f, TEXTALIGN_ML);
		pUi->DoEditBox(&pPopupContext->m_ReasonInput, &InputBox, FontSize - 1.0f);

		// Action buttons: [Atrás] [Continuar]
		View.HSplitTop(ItemSpacing * 2.5f, nullptr, &View);
		View.HSplitTop(ButtonSize + 2.0f, &Container, &View);
		CUIRect BackBtn, NextBtn;
		Container.VSplitLeft(50.0f, &BackBtn, &Container);
		Container.VSplitLeft(ItemSpacing * 2.0f, nullptr, &NextBtn);

		if(pUi->DoButton_PopupMenu(&pPopupContext->m_ModBack, TCLocalize("Atrás"), &BackBtn, FontSize - 1.0f, TEXTALIGN_MC))
		{
			pPopupContext->m_SubMenu = SUBMENU_MAIN;
		}

		ColorRGBA NextColor = ColorRGBA(0.95f, 0.25f, 0.25f, 0.7f * pUi->ButtonColorMul(&pPopupContext->m_ModConfirm));
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_ModConfirm, TCLocalize("Continuar"), &NextBtn, FontSize - 1.0f, TEXTALIGN_MC, 0.0f, false, true, NextColor))
		{
			pPopupContext->m_SubMenu = SUBMENU_CONFIRM_BAN;
		}
	}
	else if(pPopupContext->m_SubMenu == SUBMENU_CONFIRM_BAN)
	{
		View.HSplitTop(FontSize + 4.0f, &Label, &View);
		pScoreboard->TextRender()->TextColor(ColorRGBA(1.0f, 0.25f, 0.25f, 1.0f));
		pUi->DoLabel(&Label, TCLocalize("¿Seguro de banear?"), FontSize, TEXTALIGN_MC);
		pScoreboard->TextRender()->TextColor(pScoreboard->TextRender()->DefaultTextColor());

		View.HSplitTop(ItemSpacing * 2.0f, nullptr, &View);

		char aTarget[128];
		str_format(aTarget, sizeof(aTarget), "%s (ID: %d)", Client.m_aName, pPopupContext->m_ClientId);
		View.HSplitTop(FontSize + 2.0f, &Label, &View);
		pUi->DoLabel(&Label, aTarget, FontSize - 1.0f, TEXTALIGN_MC);

		int Mins = pPopupContext->m_MinutesInput.GetInteger();
		if(Mins <= 0)
			Mins = 60;
		char aMinsStr[64];
		str_format(aMinsStr, sizeof(aMinsStr), "%s: %d min", TCLocalize("Tiempo"), Mins);
		View.HSplitTop(FontSize + 2.0f, &Label, &View);
		pUi->DoLabel(&Label, aMinsStr, FontSize - 2.0f, TEXTALIGN_MC);

		const char *pReason = pPopupContext->m_ReasonInput.GetString();
		char aReasonStr[128];
		str_format(aReasonStr, sizeof(aReasonStr), "%s: %s", TCLocalize("Razón"), pReason[0] != '\0' ? pReason : "(ninguna)");
		View.HSplitTop(FontSize + 4.0f, &Label, &View);
		pUi->DoLabel(&Label, aReasonStr, FontSize - 2.0f, TEXTALIGN_MC);

		View.HSplitTop(ItemSpacing * 3.0f, nullptr, &View);

		View.HSplitTop(ButtonSize + 4.0f, &Container, &View);
		CUIRect CancelBtn, BanBtn;
		Container.VSplitMid(&CancelBtn, &BanBtn, ItemSpacing * 2.0f);

		if(pUi->DoButton_PopupMenu(&pPopupContext->m_ModBanConfirmNo, TCLocalize("Cancelar"), &CancelBtn, FontSize - 1.0f, TEXTALIGN_MC))
		{
			pPopupContext->m_SubMenu = SUBMENU_BAN;
		}

		ColorRGBA BanColor = ColorRGBA(0.95f, 0.15f, 0.15f, 0.85f * pUi->ButtonColorMul(&pPopupContext->m_ModBanConfirmYes));
		if(pUi->DoButton_PopupMenu(&pPopupContext->m_ModBanConfirmYes, TCLocalize("Sí, Banear"), &BanBtn, FontSize - 1.0f, TEXTALIGN_MC, 0.0f, false, true, BanColor))
		{
			char aCmd[256];
			if(pReason[0] != '\0')
				str_format(aCmd, sizeof(aCmd), "ban %d %d %s", pPopupContext->m_ClientId, Mins, pReason);
			else
				str_format(aCmd, sizeof(aCmd), "ban %d %d", pPopupContext->m_ClientId, Mins);
			pScoreboard->Client()->Rcon(aCmd);
			if(g_Config.m_ClModRecordOnPunish)
				pScoreboard->SaveModReplay("ban", Client.m_aName);
			if(!pScoreboard->m_Active)
				pScoreboard->LockMouse();
			return CUi::POPUP_CLOSE_CURRENT;
		}
	}

	return CUi::POPUP_KEEP_OPEN;
}

CUi::EPopupMenuFunctionResult CScoreboard::CMapTitlePopupContext::Render(void *pContext, CUIRect View, bool Active)
{
	CMapTitlePopupContext *pPopupContext = static_cast<CMapTitlePopupContext *>(pContext);
	CScoreboard *pScoreboard = pPopupContext->m_pScoreboard;

	pScoreboard->TextRender()->Text(View.x, View.y, pPopupContext->m_FontSize, pScoreboard->GameClient()->m_aMapDescription, View.w);

	return CUi::POPUP_KEEP_OPEN;
}
