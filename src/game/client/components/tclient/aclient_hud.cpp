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
	const bool HudEditorActive = GameClient()->m_HudEditor.IsActive();
	int TrackedId = GameClient()->m_Snap.m_LocalClientId;
	if(GameClient()->m_Snap.m_SpecInfo.m_Active && in_range(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId, 0, MAX_CLIENTS - 1))
		TrackedId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;

	const bool HasChar = in_range(TrackedId, 0, MAX_CLIENTS - 1) && GameClient()->m_Snap.m_aCharacters[TrackedId].m_Active;
	if(!HasChar && !HudEditorActive)
		return;

	const float HudHeight = 300.0f;
	const float HudWidth = HudHeight * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(HudWidth, HudHeight);

	const HudLayout::SModuleLayout Layout = HudLayout::Get(HudLayout::MODULE_SPEEDOMETER, HudWidth, HudHeight);
	const bool HasOverride = HudLayout::HasRuntimeOverride(HudLayout::MODULE_SPEEDOMETER);
	const float Scale = HasOverride ? std::clamp(Layout.m_Scale / 100.0f, 0.5f, 2.0f) : std::clamp(g_Config.m_AcHudSpeedometerScale / 100.0f, 0.5f, 2.0f);
	const float Alpha = HasOverride ? HudLayout::AlphaFactor(HudLayout::MODULE_SPEEDOMETER) : std::clamp(g_Config.m_AcHudSpeedometerAlpha / 100.0f, 0.2f, 1.0f);

	float SpeedUps = 0.0f;
	if(HasChar)
	{
		const CNetObj_Character *pPrev = &GameClient()->m_Snap.m_aCharacters[TrackedId].m_Prev;
		const CNetObj_Character *pCur = &GameClient()->m_Snap.m_aCharacters[TrackedId].m_Cur;
		const float IntraTick = Client()->IntraGameTick(g_Config.m_ClDummy);
		const vec2 Vel = mix(vec2(pPrev->m_VelX, pPrev->m_VelY), vec2(pCur->m_VelX, pCur->m_VelY), IntraTick);
		SpeedUps = length(Vel / 256.0f) * Client()->GameTickSpeed() / 32.0f;
	}

	const float BoxW = 75.0f * Scale;
	const float BoxH = 18.0f * Scale;
	const bool PosOverride = HudLayout::HasPositionOverride(HudLayout::MODULE_SPEEDOMETER);
	const float PosX = PosOverride ? std::clamp(Layout.m_X, 0.0f, std::max(0.0f, HudWidth - BoxW)) : (g_Config.m_AcHudSpeedometerX / 100.0f) * (HudWidth - BoxW);
	const float PosY = PosOverride ? std::clamp(Layout.m_Y, 0.0f, std::max(0.0f, HudHeight - BoxH)) : (g_Config.m_AcHudSpeedometerY / 100.0f) * (HudHeight - BoxH);

	CUIRect Rect(PosX, PosY, BoxW, BoxH);
	Rect.Draw(ColorRGBA(0.08f, 0.10f, 0.14f, Alpha * 0.70f), IGraphics::CORNER_ALL, 4.0f * Scale);

	char aBuf[32];
	str_format(aBuf, sizeof(aBuf), "%.1f ups", SpeedUps);

	const ColorRGBA SpeedColor = SpeedUps > 28.0f ? ColorRGBA(0.0f, 0.75f, 1.0f, 1.0f) : ColorRGBA(0.90f, 0.94f, 0.98f, Alpha * 0.95f);
	TextRender()->TextColor(SpeedColor);
	Ui()->DoLabel(&Rect, aBuf, BoxH * 0.58f, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}
