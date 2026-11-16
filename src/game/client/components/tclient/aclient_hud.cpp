#include "aclient_hud.h"

#include "hud_layout.h"

#include <engine/keys.h>
#include <engine/textrender.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cmath>

void CAClientHud::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

#if defined(CONF_VIDEORECORDER)
	if(!((IVideo::Current() && g_Config.m_ClVideoShowhud) || (!IVideo::Current() && g_Config.m_ClShowhud)))
		return;
#else
	if(!g_Config.m_ClShowhud)
		return;
#endif

	const bool HudEditorActive = GameClient()->m_HudEditor.IsActive();
	if((g_Config.m_AcHudSpeedometer && HudLayout::IsEnabled(HudLayout::MODULE_SPEEDOMETER)) || HudEditorActive)
		RenderSpeedometer();
}

void CAClientHud::RenderSpeedometer()
{
	if(Graphics()->WindowWidth() <= 0 || Graphics()->WindowHeight() <= 0)
		return;

	const float Aspect = Graphics()->ScreenAspect();
	if(!std::isfinite(Aspect) || Aspect <= 0.001f)
		return;

	const bool HudEditorActive = GameClient()->m_HudEditor.IsActive();
	if(!GameClient()->m_Snap.m_pGameInfoObj && !HudEditorActive)
		return;

	int TrackedId = -1;
	if(GameClient()->m_Snap.m_SpecInfo.m_Active)
	{
		if(in_range(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId, 0, MAX_CLIENTS - 1))
			TrackedId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;
	}
	else if(!(GameClient()->m_Snap.m_pGameInfoObj && (GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER)))
	{
		TrackedId = GameClient()->m_Snap.m_LocalClientId;
	}

	const bool HasChar = in_range(TrackedId, 0, MAX_CLIENTS - 1) &&
			     GameClient()->m_Snap.m_aCharacters[TrackedId].m_Active &&
			     !GameClient()->m_aClients[TrackedId].m_SpecCharPresent;
	if(!HasChar && !HudEditorActive)
		return;

	const CScreenRect PrevScreen = Graphics()->GetScreen();
	const float HudHeight = 300.0f;
	const float HudWidth = HudHeight * Aspect;
	Graphics()->MapScreenToSize(HudWidth, HudHeight);

	const HudLayout::SModuleLayout Layout = HudLayout::Get(HudLayout::MODULE_SPEEDOMETER, HudWidth, HudHeight);
	const bool HasOverride = HudLayout::HasRuntimeOverride(HudLayout::MODULE_SPEEDOMETER);
	const float Scale = (HasOverride || HudEditorActive) ? std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f) : std::clamp(g_Config.m_AcHudSpeedometerScale / 100.0f, 0.5f, 2.0f);
	const float Alpha = (HasOverride || HudEditorActive) ? std::clamp(HudLayout::AlphaFactor(HudLayout::MODULE_SPEEDOMETER), 0.0f, 1.0f) : std::clamp(g_Config.m_AcHudSpeedometerAlpha / 100.0f, 0.2f, 1.0f);
	if(Alpha <= 0.001f)
	{
		Graphics()->MapScreen(PrevScreen);
		return;
	}

	float SpeedUps = 0.0f;
	if(HasChar)
	{
		const int Dummy = (Client()->State() == IClient::STATE_DEMOPLAYBACK) ? 0 : std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
		const float IntraTick = std::clamp(Client()->IntraGameTick(Dummy), 0.0f, 1.0f);
		const CNetObj_Character *pPrev = &GameClient()->m_Snap.m_aCharacters[TrackedId].m_Prev;
		const CNetObj_Character *pCur = &GameClient()->m_Snap.m_aCharacters[TrackedId].m_Cur;
		vec2 Vel = mix(vec2((float)pPrev->m_VelX, (float)pPrev->m_VelY), vec2((float)pCur->m_VelX, (float)pCur->m_VelY), IntraTick);
		if(std::abs(Vel.x) <= 1.0f)
			Vel.x = 0.0f;
		if(std::abs(Vel.y) <= 1.0f)
			Vel.y = 0.0f;
		const float Computed = length(Vel / 256.0f) * Client()->GameTickSpeed() / 32.0f;
		if(std::isfinite(Computed) && Computed >= 0.0f)
			SpeedUps = Computed;
	}

	const float BoxW = 75.0f * Scale;
	const float BoxH = 18.0f * Scale;
	const bool PosOverride = HudLayout::HasPositionOverride(HudLayout::MODULE_SPEEDOMETER);
	const bool UseLayoutPos = HudEditorActive || PosOverride || (g_Config.m_AcHudSpeedometerX == 50 && g_Config.m_AcHudSpeedometerY == 82);
	const float PosX = UseLayoutPos ?
				   std::clamp(Layout.m_X, 0.0f, std::max(0.0f, HudWidth - BoxW)) :
				   (std::clamp(g_Config.m_AcHudSpeedometerX, 0, 100) / 100.0f) * std::max(0.0f, HudWidth - BoxW);
	const float PosY = UseLayoutPos ?
				   std::clamp(Layout.m_Y, 0.0f, std::max(0.0f, HudHeight - BoxH)) :
				   (std::clamp(g_Config.m_AcHudSpeedometerY, 0, 100) / 100.0f) * std::max(0.0f, HudHeight - BoxH);

	const int Corners = HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, PosX, PosY, BoxW, BoxH, HudWidth, HudHeight);
	const float Rounding = std::min(4.0f * Scale, std::min(BoxW, BoxH) * 0.5f);
	CUIRect Rect(PosX, PosY, BoxW, BoxH);
	Rect.Draw(ColorRGBA(0.08f, 0.10f, 0.14f, Alpha * 0.70f), Corners, Rounding);

	char aBuf[32];
	str_format(aBuf, sizeof(aBuf), "%.1f ups", SpeedUps);

	const ColorRGBA SpeedColor = SpeedUps > 28.0f ? ColorRGBA(0.0f, 0.75f, 1.0f, Alpha * 0.95f) : ColorRGBA(0.90f, 0.94f, 0.98f, Alpha * 0.95f);
	TextRender()->TextColor(SpeedColor);
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor().WithMultipliedAlpha(Alpha));
	Ui()->DoLabel(&Rect, aBuf, BoxH * 0.58f, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	Graphics()->MapScreen(PrevScreen);
}
