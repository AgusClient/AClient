#include "player_indicator.h"

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>

static vec2 NormalizedDirection(vec2 Src, vec2 Dst)
{
	return normalize(vec2(Dst.x - Src.x, Dst.y - Src.y));
}

static float DistanceBetweenTwoPoints(vec2 Src, vec2 Dst)
{
	return distance(Src, Dst);
}

void CPlayerIndicator::OnRender()
{
	// Don't render if we can't find our own tee
	const int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalClientId == -1 || !GameClient()->m_Snap.m_aCharacters[LocalClientId].m_Active)
		return;

	// Don't render if not race gamemode or in demo
	if(!GameClient()->m_GameInfo.m_Race || Client()->State() == IClient::STATE_DEMOPLAYBACK || !GameClient()->m_Camera.ZoomAllowed())
		return;

	if(g_Config.m_TcPlayerIndicator != 1)
		return;

	if(GameClient()->m_Teams.Team(LocalClientId) == 0 && g_Config.m_TcIndicatorTeamOnly)
		return;

	const vec2 Position = GameClient()->m_aClients[LocalClientId].m_RenderPos;
	const int LocalTeam = GameClient()->m_aClients[LocalClientId].m_Team;
	const CScreenRect ScreenRect = Graphics()->GetScreen();
	const ColorRGBA SavedCol = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcIndicatorSaved));
	const ColorRGBA FreezeCol = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcIndicatorFreeze));
	const ColorRGBA AliveCol = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcIndicatorAlive));
	const bool DrawTees = g_Config.m_TcIndicatorTees != 0;

	Graphics()->TextureClear();
	if(!DrawTees)
		Graphics()->QuadsBegin();

	ColorRGBA Col = ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f);
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(!GameClient()->m_Snap.m_apPlayerInfos[i] || i == LocalClientId)
			continue;

		const CGameClient::CClientData &OtherTee = GameClient()->m_aClients[i];
		const CCharacterCore *pOtherCharacter = &OtherTee.m_Predicted;
		if(
			OtherTee.m_Team == LocalTeam &&
			!OtherTee.m_Spec &&
			GameClient()->m_Snap.m_aCharacters[i].m_Active)
		{
			if(g_Config.m_TcPlayerIndicatorFreeze && !(OtherTee.m_FreezeEnd > 0 || OtherTee.m_DeepFrozen))
				continue;

			// Hide tees on our screen if the config is set to do so
			if(g_Config.m_TcIndicatorHideVisible && ScreenRect.Inside(OtherTee.m_RenderPos))
				continue;

			vec2 Norm = NormalizedDirection(OtherTee.m_RenderPos, Position) * (-1);

			float Offset = g_Config.m_TcIndicatorOffset;
			if(g_Config.m_TcIndicatorVariableDistance)
			{
				Offset = mix((float)g_Config.m_TcIndicatorOffset, (float)g_Config.m_TcIndicatorOffsetMax,
					std::min(DistanceBetweenTwoPoints(Position, OtherTee.m_RenderPos) / g_Config.m_TcIndicatorMaxDistance, 1.0f));
			}

			vec2 IndicatorPos(Norm.x * Offset + Position.x, Norm.y * Offset + Position.y);
			CTeeRenderInfo TeeInfo = OtherTee.m_RenderInfo;
			float Alpha = g_Config.m_TcIndicatorOpacity / 100.0f;
			if(OtherTee.m_FreezeEnd > 0 || OtherTee.m_DeepFrozen)
			{
				// check if player is frozen or is getting saved
				if(pOtherCharacter->m_IsInFreeze == 0)
				{
					// player is on the way to get free again
					Col = SavedCol;
				}
				else
				{
					// player is frozen
					Col = FreezeCol;
				}
				if(DrawTees)
				{
					TeeInfo.m_ColorBody.r *= 0.4f;
					TeeInfo.m_ColorBody.g *= 0.4f;
					TeeInfo.m_ColorBody.b *= 0.4f;
					TeeInfo.m_ColorFeet.r *= 0.4f;
					TeeInfo.m_ColorFeet.g *= 0.4f;
					TeeInfo.m_ColorFeet.b *= 0.4f;
					Alpha *= 0.8f;
				}
			}
			else
			{
				Col = AliveCol;
			}
			bool HideIfNotWar = false;
			ColorRGBA PrevCol = Col;
			if(g_Config.m_TcWarListIndicator)
			{
				HideIfNotWar = true;
				if(g_Config.m_TcWarListIndicatorAll)
				{
					if(GameClient()->m_WarList.GetAnyWar(i))
					{
						Col = GameClient()->m_WarList.GetPriorityColor(i);
						HideIfNotWar = false;
					}
				}
				if(g_Config.m_TcWarListIndicatorTeam)
				{
					if(GameClient()->m_WarList.GetWarData(i).m_WarGroupMatches[2])
					{
						Col = GameClient()->m_WarList.m_WarTypes[2]->m_Color;
						HideIfNotWar = false;
					}
				}
				if(g_Config.m_TcWarListIndicatorEnemy)
				{
					if(GameClient()->m_WarList.GetWarData(i).m_WarGroupMatches[1])
					{
						Col = GameClient()->m_WarList.m_WarTypes[1]->m_Color;
						HideIfNotWar = false;
					}
				}
			}

			if(HideIfNotWar)
				continue;
			if(!g_Config.m_TcWarListIndicatorColors)
				Col = PrevCol;

			Col.a = Alpha;

			TeeInfo.m_Size = g_Config.m_TcIndicatorRadius * 4.0f;

			if(DrawTees)
			{
				RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, OtherTee.m_RenderCur.m_Emote, vec2(1.0f, 0.0f), IndicatorPos, Col.a);
			}
			else
			{
				Graphics()->SetColor(Col);
				Graphics()->DrawCircle(IndicatorPos.x, IndicatorPos.y, g_Config.m_TcIndicatorRadius, 16);
			}
		}
	}

	if(!DrawTees)
		Graphics()->QuadsEnd();

	// reset texture color
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
}
