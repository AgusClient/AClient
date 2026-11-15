#include "aclient_optimizer.h"

#include <base/time.h>

#include <engine/shared/config.h>
#include <game/aclient_fog.h>
#include <game/client/gameclient.h>

#include <algorithm>
#include <vector>

#if defined(CONF_FAMILY_WINDOWS)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <wchar.h>

static void NormalizeProcessPriority(DWORD Pid, bool &Alive)
{
	Alive = false;
	HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION, FALSE, Pid);
	if(hProcess == nullptr)
		return;

	DWORD ExitCode = 0;
	if(GetExitCodeProcess(hProcess, &ExitCode) && ExitCode == STILL_ACTIVE)
	{
		Alive = true;
		const DWORD Priority = GetPriorityClass(hProcess);
		if(Priority != 0 && Priority != NORMAL_PRIORITY_CLASS)
			SetPriorityClass(hProcess, NORMAL_PRIORITY_CLASS);
	}
	CloseHandle(hProcess);
}

static void EnforceDiscordNormalPriority(int64_t Now)
{
	static std::vector<DWORD> s_vDiscordPids;
	static int64_t s_LastFullScan = 0;

	const int64_t RescanInterval = 30 * time_freq();
	if(s_vDiscordPids.empty() || Now - s_LastFullScan >= RescanInterval)
	{
		s_LastFullScan = Now;
		s_vDiscordPids.clear();

		HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if(hSnapshot == INVALID_HANDLE_VALUE)
			return;

		PROCESSENTRY32W Entry{};
		Entry.dwSize = sizeof(Entry);
		if(Process32FirstW(hSnapshot, &Entry))
		{
			do
			{
				if(_wcsicmp(Entry.szExeFile, L"Discord.exe") != 0 &&
					_wcsicmp(Entry.szExeFile, L"DiscordPTB.exe") != 0 &&
					_wcsicmp(Entry.szExeFile, L"DiscordCanary.exe") != 0)
				{
					continue;
				}

				bool Alive = false;
				NormalizeProcessPriority(Entry.th32ProcessID, Alive);
				if(Alive)
					s_vDiscordPids.push_back(Entry.th32ProcessID);
			} while(Process32NextW(hSnapshot, &Entry));
		}

		CloseHandle(hSnapshot);
		return;
	}

	size_t WriteIdx = 0;
	for(size_t i = 0; i < s_vDiscordPids.size(); ++i)
	{
		bool Alive = false;
		NormalizeProcessPriority(s_vDiscordPids[i], Alive);
		if(Alive)
			s_vDiscordPids[WriteIdx++] = s_vDiscordPids[i];
	}
	s_vDiscordPids.resize(WriteIdx);
}
#endif

void CAcOptimizer::OnInit()
{
	m_LastHighPriority = false;
	m_LastDiscordNormalPriority = false;
	m_LastDiscordPriorityCheck = 0;
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

	const bool DiscordNormalPrio = (g_Config.m_AcOptimizer && g_Config.m_AcOptimizerDiscordNormalPriority);
	if(DiscordNormalPrio)
	{
		const int64_t Now = time_get();
		if(!m_LastDiscordNormalPriority || Now - m_LastDiscordPriorityCheck >= 5 * time_freq())
		{
			m_LastDiscordPriorityCheck = Now;
			EnforceDiscordNormalPriority(Now);
		}
	}
	m_LastDiscordNormalPriority = DiscordNormalPrio;
#endif

	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(GameClient()->m_Menus.IsActive())
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
		const float W = FogRect.Width();
		const float H = FogRect.Height();
		const vec2 ViewportSize = Graphics()->ActiveViewportSize();
		const float ViewportW = std::max(1.0f, ViewportSize.x);
		const float ViewportH = std::max(1.0f, ViewportSize.y);
		const float ThicknessX = std::min(2.0f * (WorldScreen.Width() / ViewportW), W * 0.5f);
		const float ThicknessY = std::min(2.0f * (WorldScreen.Height() / ViewportH), H * 0.5f);

		if(W > 0.0f && H > 0.0f && ThicknessX > 0.0f && ThicknessY > 0.0f)
		{
			const float InnerH = std::max(0.0f, H - 2.0f * ThicknessY);
			const IGraphics::CQuadItem aBorderQuads[4] = {
				IGraphics::CQuadItem(X0, Y0, W, ThicknessY),
				IGraphics::CQuadItem(X0, Y0 + H - ThicknessY, W, ThicknessY),
				IGraphics::CQuadItem(X0, Y0 + ThicknessY, ThicknessX, InnerH),
				IGraphics::CQuadItem(X0 + W - ThicknessX, Y0 + ThicknessY, ThicknessX, InnerH),
			};

			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(0.2f, 0.8f, 1.0f, 0.75f);
			Graphics()->QuadsDrawTL(aBorderQuads, 4);
			Graphics()->QuadsEnd();
		}

		Graphics()->MapScreen(PrevScreen);
	}
}
