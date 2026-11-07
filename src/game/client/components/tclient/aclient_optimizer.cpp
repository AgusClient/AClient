#include "aclient_optimizer.h"

#include <engine/shared/config.h>
#include <game/client/gameclient.h>

#if defined(CONF_FAMILY_WINDOWS)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

void CAcOptimizer::OnInit()
{
	m_HadHighDetail = false;
	m_LastHighPriority = false;
}

void CAcOptimizer::OnReset()
{
	if(m_HadHighDetail)
	{
		g_Config.m_GfxHighDetail = 1;
		m_HadHighDetail = false;
	}
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
	// 1. High Detail Auto Toggle
	if(g_Config.m_AcOptimizer && g_Config.m_AcOptimizerDisableHighDetail)
	{
		if(g_Config.m_GfxHighDetail != 0)
		{
			g_Config.m_GfxHighDetail = 0;
			m_HadHighDetail = true;
		}
	}
	else if(m_HadHighDetail)
	{
		g_Config.m_GfxHighDetail = 1;
		m_HadHighDetail = false;
	}

	// 2. Windows Process Priority
#if defined(CONF_FAMILY_WINDOWS)
	bool HighPrio = (g_Config.m_AcOptimizer && g_Config.m_AcOptimizerHighPriority);
	if(HighPrio != m_LastHighPriority)
	{
		m_LastHighPriority = HighPrio;
		SetPriorityClass(GetCurrentProcess(), HighPrio ? HIGH_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS);
	}
#endif

	// 3. Render FPS Fog Boundary Debug Box
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(g_Config.m_AcOptimizer && g_Config.m_AcOptimizerFpsFog && g_Config.m_AcOptimizerFpsFogRenderRect)
	{
		float Radius = (float)std::clamp((int)g_Config.m_AcOptimizerFpsFogRadiusTiles, 5, 120) * 32.0f;
		vec2 Center = GameClient()->m_Camera.m_Center;

		float X0 = Center.x - Radius;
		float Y0 = Center.y - Radius;
		float X1 = Center.x + Radius;
		float Y1 = Center.y + Radius;

		IGraphics::CLineItem aLines[4] = {
			{X0, Y0, X1, Y0},
			{X1, Y0, X1, Y1},
			{X1, Y1, X0, Y1},
			{X0, Y1, X0, Y0}};

		Graphics()->TextureClear();
		Graphics()->LinesBegin();
		Graphics()->SetColor(0.2f, 0.8f, 1.0f, 0.75f);
		Graphics()->LinesDraw(aLines, 4);
		Graphics()->LinesEnd();
	}
}

