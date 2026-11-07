#include "aclient_optimizer.h"

#include <engine/shared/config.h>
#include <game/aclient_fog.h>
#include <game/client/gameclient.h>

#if defined(CONF_FAMILY_WINDOWS)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

void CAcOptimizer::OnInit()
{
	m_LastHighPriority = false;
}

void CAcOptimizer::OnConsoleInit()
{
	Console()->Register("ac_optimizer_toggle", "", CFGFLAG_CLIENT, ConToggleOptimizer, this, "Toggle AClient FPS Booster / Optimizer mode");
}

void CAcOptimizer::ConToggleOptimizer(IConsole::IResult *pResult, void *pUserData)
{
	g_Config.m_AcOptimizer ^= 1;
}

void CAcOptimizer::OnRender()
{
#if defined(CONF_FAMILY_WINDOWS)
	const bool HighPrio = (g_Config.m_AcOptimizer && g_Config.m_AcOptimizerHighPriority);
	if(HighPrio != m_LastHighPriority)
	{
		m_LastHighPriority = HighPrio;
		SetPriorityClass(GetCurrentProcess(), HighPrio ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS);
	}
#endif

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(IsAcFpsFogActive() && g_Config.m_AcOptimizerFpsFogRenderRect)
	{
		const CScreenRect PrevScreen = Graphics()->GetScreen();
		const vec2 Center = GameClient()->m_Camera.m_Center;
		const CScreenRect WorldScreen = Graphics()->MapScreenToWorld(
			Center.x, Center.y,
			100.0f, 100.0f, 100.0f,
			0.0f, 0.0f,
			Graphics()->ScreenAspect(), GameClient()->m_Camera.m_Zoom);
		const CScreenRect FogRect = ComputeAcFogScreenRect(WorldScreen, 0.0f);

		Graphics()->MapScreen(WorldScreen);

		const float X0 = FogRect.m_TopLeft.x;
		const float Y0 = FogRect.m_TopLeft.y;
		const float X1 = FogRect.m_BottomRight.x;
		const float Y1 = FogRect.m_BottomRight.y;

		const IGraphics::CLineItem aLines[4] = {
			{X0, Y0, X1, Y0},
			{X1, Y0, X1, Y1},
			{X1, Y1, X0, Y1},
			{X0, Y1, X0, Y0}};

		Graphics()->TextureClear();
		Graphics()->LinesBegin();
		Graphics()->SetColor(0.2f, 0.8f, 1.0f, 0.75f);
		Graphics()->LinesDraw(aLines, 4);
		Graphics()->LinesEnd();

		Graphics()->MapScreen(PrevScreen);
	}
}
