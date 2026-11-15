/* Copyright (C) 2026 AClient / BestProject Team */
#include "hud_layout.h"

#include <base/math.h>
#include <base/str.h>
#include <engine/config.h>
#include <engine/console.h>
#include <engine/shared/config.h>

#include <algorithm>
#include <cstring>

namespace HudLayout
{

namespace
{

struct SModuleMeta
{
	const char *m_pId;
	const char *m_pName;
	bool m_IsEditor;
	bool m_IsPersisted;
};

static const SModuleMeta gs_aModuleMeta[MODULE_COUNT] = {
	{"score", "Score", true, true},
	{"chat", "Chat", true, true},
	{"votes", "Votes", true, true},
	{"mini_statboard", "RaceStats Overlay", true, true},
	{"keystrokes", "Keystrokes", true, true},
	{"speedometer", "Speedometer", true, true},
	{"spectator_count", "Spectator Count", true, true},
	{"dummy_actions", "Dummy Actions", true, true},
	{"movement_info", "Movement Info", true, true},
	{"game_timer", "Game Timer", true, true},
	{"local_time", "Local Time", true, true},
	{"frozen_hud", "Frozen HUD", true, true},
	{"notify_last", "Notify Last", true, true},
	{"keystrokes_keyboard", "Keystrokes Keyboard", true, true},
	{"keystrokes_mouse", "Keystrokes Mouse", true, true},
};

static const SModuleLayout gs_aDefaultLayouts[MODULE_COUNT] = {
	{435.0f, 229.0f, 100, 0, true, true, 0x40000000U, 100}, // MODULE_SCORE
	{5.0f, 220.0f, 100, 0, true, false, 0x66000000U, 100},   // MODULE_CHAT
	{0.0f, 60.0f, 100, 0, true, true, 0x66000000U, 100},     // MODULE_VOTES
	{370.0f, 120.0f, 70, 0, true, true, 0x99000000U, 100},   // MODULE_MINI_STATBOARD
	{15.0f, 210.0f, 100, 0, true, true, 0x88000000U, 85},    // MODULE_KEYSTROKES
	{212.0f, 260.0f, 100, 0, true, true, 0x88000000U, 85},   // MODULE_SPEEDOMETER
	{460.0f, 141.0f, 100, 0, true, true, 0x66000000U, 100},  // MODULE_SPECTATOR_COUNT
	{484.0f, 172.0f, 100, 0, true, true, 0x66000000U, 100},  // MODULE_DUMMY_ACTIONS
	{438.0f, 185.0f, 100, 0, true, true, 0x66000000U, 100},  // MODULE_MOVEMENT_INFO
	{225.0f, 2.0f, 100, 0, true, false, 0x66000000U, 100},   // MODULE_GAME_TIMER
	{180.0f, 0.0f, 100, 0, true, true, 0x66000000U, 100},    // MODULE_LOCAL_TIME
	{288.0f, 0.0f, 100, 0, true, true, 0x66000000U, 100},    // MODULE_FROZEN_HUD
	{100.0f, 3.0f, 100, 0, true, false, 0x66000000U, 100},   // MODULE_NOTIFY_LAST
	{2.0f, 152.0f, 100, 0, true, false, 0x66000000U, 100},   // MODULE_KEYSTROKES_KEYBOARD
	{80.0f, 152.0f, 100, 0, true, false, 0x66000000U, 100},  // MODULE_KEYSTROKES_MOUSE
};

static SModuleLayout gs_aRuntimeLayouts[MODULE_COUNT];
static bool gs_aHasRuntimeOverride[MODULE_COUNT] = {false};
static bool gs_aHasPositionOverride[MODULE_COUNT] = {false};
static bool gs_Initialized = false;

void SyncModuleToConfig(EModule Module)
{
	const SModuleLayout &Layout = gs_aRuntimeLayouts[Module];
	switch(Module)
	{
	case MODULE_MINI_STATBOARD:
		g_Config.m_AcMiniStatboard = Layout.m_Enabled ? 1 : 0;
		g_Config.m_AcMiniStatboardScale = std::clamp(Layout.m_Scale, 20, 200);
		g_Config.m_AcMiniStatboardOpacity = std::clamp(Layout.m_Alpha, 0, 100);
		break;
	case MODULE_KEYSTROKES:
		g_Config.m_AcHudKeystrokes = Layout.m_Enabled ? 1 : 0;
		g_Config.m_AcHudKeystrokesScale = std::clamp(Layout.m_Scale, 50, 200);
		g_Config.m_AcHudKeystrokesAlpha = std::clamp(Layout.m_Alpha, 20, 100);
		break;
	case MODULE_SPEEDOMETER:
		g_Config.m_AcHudSpeedometer = Layout.m_Enabled ? 1 : 0;
		g_Config.m_AcHudSpeedometerScale = std::clamp(Layout.m_Scale, 50, 200);
		g_Config.m_AcHudSpeedometerAlpha = std::clamp(Layout.m_Alpha, 20, 100);
		break;
	case MODULE_KEYSTROKES_KEYBOARD:
		g_Config.m_AcKeystrokesKeyboard = Layout.m_Enabled ? 1 : 0;
		break;
	case MODULE_KEYSTROKES_MOUSE:
		g_Config.m_AcKeystrokesMouse = Layout.m_Enabled ? 1 : 0;
		break;
	default:
		break;
	}
}

void EnsureInitialized()
{
	if(gs_Initialized)
		return;
	for(int i = 0; i < MODULE_COUNT; ++i)
	{
		gs_aRuntimeLayouts[i] = gs_aDefaultLayouts[i];
		gs_aHasRuntimeOverride[i] = false;
		gs_aHasPositionOverride[i] = false;
	}
	gs_Initialized = true;
}

void ConHudLayoutSet(IConsole::IResult *pResult, void *pUserData)
{
	(void)pUserData;
	EnsureInitialized();
	const char *pId = pResult->GetString(0);
	EModule Module = ModuleFromId(pId);
	if(Module == MODULE_COUNT)
		return;

	SModuleLayout &Layout = gs_aRuntimeLayouts[Module];
	Layout.m_X = pResult->GetFloat(1);
	Layout.m_Y = pResult->GetFloat(2);
	Layout.m_Scale = std::clamp(pResult->GetInteger(3), 20, 300);
	Layout.m_Mode = pResult->GetInteger(4);
	Layout.m_BackgroundEnabled = pResult->GetInteger(5) != 0;
	if(pResult->NumArguments() > 6)
		Layout.m_BackgroundColor = (unsigned)pResult->GetInteger(6);
	if(pResult->NumArguments() > 7)
		Layout.m_Enabled = pResult->GetInteger(7) != 0;
	if(pResult->NumArguments() > 8)
		Layout.m_Alpha = std::clamp(pResult->GetInteger(8), 0, 100);

	gs_aHasRuntimeOverride[Module] = true;
	gs_aHasPositionOverride[Module] = true;
	SyncModuleToConfig(Module);
}

void ConHudLayoutReset(IConsole::IResult *pResult, void *pUserData)
{
	(void)pUserData;
	EnsureInitialized();
	if(pResult->NumArguments() > 0 && pResult->GetString(0)[0] != '\0')
	{
		EModule Module = ModuleFromId(pResult->GetString(0));
		if(Module != MODULE_COUNT)
			ResetSettings(Module);
		return;
	}
	ResetEditableModules();
}

void ConfigSaveCallback(IConfigManager *pConfig, void *pUserData)
{
	(void)pUserData;
	EnsureInitialized();
	for(int i = 0; i < MODULE_COUNT; ++i)
	{
		const EModule Module = (EModule)i;
		if(!IsPersistedModule(Module) || !gs_aHasRuntimeOverride[Module])
			continue;

		const SModuleLayout &Layout = gs_aRuntimeLayouts[Module];
		char aBuf[256];
		str_format(
			aBuf,
			sizeof(aBuf),
			"hud_layout_set \"%s\" %.3f %.3f %d %d %d %d %d %d",
			Id(Module),
			Layout.m_X,
			Layout.m_Y,
			Layout.m_Scale,
			Layout.m_Mode,
			Layout.m_BackgroundEnabled ? 1 : 0,
			(int)Layout.m_BackgroundColor,
			Layout.m_Enabled ? 1 : 0,
			Layout.m_Alpha);
		pConfig->WriteLine(aBuf);
	}
}

} // namespace

bool IsEditorModule(EModule Module)
{
	if(Module < 0 || Module >= MODULE_COUNT)
		return false;
	return gs_aModuleMeta[Module].m_IsEditor;
}

bool IsPersistedModule(EModule Module)
{
	if(Module < 0 || Module >= MODULE_COUNT)
		return false;
	return gs_aModuleMeta[Module].m_IsPersisted;
}

const char *Id(EModule Module)
{
	if(Module < 0 || Module >= MODULE_COUNT)
		return "";
	return gs_aModuleMeta[Module].m_pId;
}

const char *Name(EModule Module)
{
	if(Module < 0 || Module >= MODULE_COUNT)
		return "";
	return gs_aModuleMeta[Module].m_pName;
}

EModule ModuleFromId(const char *pId)
{
	if(!pId || !pId[0])
		return MODULE_COUNT;
	for(int i = 0; i < MODULE_COUNT; ++i)
	{
		if(str_comp_nocase(gs_aModuleMeta[i].m_pId, pId) == 0)
			return (EModule)i;
	}
	return MODULE_COUNT;
}

float CanvasXToHud(float CanvasX, float HudWidth)
{
	return CanvasX * (HudWidth / CANVAS_WIDTH);
}

SModuleLayout Get(EModule Module, float HudWidth, float HudHeight)
{
	EnsureInitialized();
	if(Module < 0 || Module >= MODULE_COUNT)
		return SModuleLayout{};

	SModuleLayout Layout = gs_aRuntimeLayouts[Module];
	if(Module == MODULE_NOTIFY_LAST && !gs_aHasPositionOverride[MODULE_NOTIFY_LAST])
	{
		Layout.m_X = (g_Config.m_TcNotifyWhenLastX / 100.0f) * HudWidth;
		Layout.m_Y = (g_Config.m_TcNotifyWhenLastY / 100.0f) * HudHeight;
		return Layout;
	}
	// Si tiene posición específica calculada en lienzo, mapear X proporcionalmente al ancho de pantalla
	Layout.m_X = CanvasXToHud(Layout.m_X, HudWidth);
	return Layout;
}

SModuleLayout GetDefault(EModule Module, float HudWidth, float HudHeight)
{
	if(Module < 0 || Module >= MODULE_COUNT)
		return SModuleLayout{};

	SModuleLayout Layout = gs_aDefaultLayouts[Module];
	Layout.m_X = CanvasXToHud(Layout.m_X, HudWidth);
	return Layout;
}

bool HasRuntimeOverride(EModule Module)
{
	EnsureInitialized();
	if(Module < 0 || Module >= MODULE_COUNT)
		return false;
	return gs_aHasRuntimeOverride[Module];
}

bool HasPositionOverride(EModule Module)
{
	EnsureInitialized();
	if(Module < 0 || Module >= MODULE_COUNT)
		return false;
	return gs_aHasPositionOverride[Module];
}

void SetPosition(EModule Module, float X, float Y)
{
	SetPosition(Module, X, Y, POSITION_MODE_TOP_LEFT);
}

void SetPosition(EModule Module, float X, float Y, EPositionMode PositionMode)
{
	EnsureInitialized();
	if(Module < 0 || Module >= MODULE_COUNT)
		return;

	gs_aRuntimeLayouts[Module].m_X = X;
	gs_aRuntimeLayouts[Module].m_Y = Y;
	gs_aRuntimeLayouts[Module].m_Mode = PositionMode;
	gs_aHasRuntimeOverride[Module] = true;
	gs_aHasPositionOverride[Module] = true;
}

void SetScale(EModule Module, int Scale)
{
	EnsureInitialized();
	if(Module < 0 || Module >= MODULE_COUNT)
		return;

	gs_aRuntimeLayouts[Module].m_Scale = std::clamp(Scale, 20, 300);
	gs_aHasRuntimeOverride[Module] = true;
	SyncModuleToConfig(Module);
}

void SetEnabled(EModule Module, bool Enabled)
{
	EnsureInitialized();
	if(Module < 0 || Module >= MODULE_COUNT)
		return;

	gs_aRuntimeLayouts[Module].m_Enabled = Enabled;
	gs_aHasRuntimeOverride[Module] = true;
	SyncModuleToConfig(Module);
}

void SetAlpha(EModule Module, int Alpha)
{
	EnsureInitialized();
	if(Module < 0 || Module >= MODULE_COUNT)
		return;

	gs_aRuntimeLayouts[Module].m_Alpha = std::clamp(Alpha, 0, 100);
	gs_aHasRuntimeOverride[Module] = true;
	SyncModuleToConfig(Module);
}

bool IsEnabled(EModule Module)
{
	EnsureInitialized();
	if(Module < 0 || Module >= MODULE_COUNT)
		return false;
	return gs_aRuntimeLayouts[Module].m_Enabled;
}

int Alpha(EModule Module)
{
	EnsureInitialized();
	if(Module < 0 || Module >= MODULE_COUNT)
		return 100;
	return gs_aRuntimeLayouts[Module].m_Alpha;
}

float AlphaFactor(EModule Module)
{
	return (float)Alpha(Module) / 100.0f;
}

void ResetPosition(EModule Module)
{
	EnsureInitialized();
	if(Module < 0 || Module >= MODULE_COUNT)
		return;

	gs_aRuntimeLayouts[Module].m_X = gs_aDefaultLayouts[Module].m_X;
	gs_aRuntimeLayouts[Module].m_Y = gs_aDefaultLayouts[Module].m_Y;
	gs_aRuntimeLayouts[Module].m_Mode = gs_aDefaultLayouts[Module].m_Mode;
	gs_aHasPositionOverride[Module] = false;
}

void ResetScale(EModule Module)
{
	EnsureInitialized();
	if(Module < 0 || Module >= MODULE_COUNT)
		return;

	gs_aRuntimeLayouts[Module].m_Scale = gs_aDefaultLayouts[Module].m_Scale;
	SyncModuleToConfig(Module);
}

void ResetSettings(EModule Module)
{
	EnsureInitialized();
	if(Module < 0 || Module >= MODULE_COUNT)
		return;

	gs_aRuntimeLayouts[Module] = gs_aDefaultLayouts[Module];
	gs_aHasRuntimeOverride[Module] = false;
	gs_aHasPositionOverride[Module] = false;
	SyncModuleToConfig(Module);
}

void ResetEditableModules()
{
	EnsureInitialized();
	for(int i = 0; i < MODULE_COUNT; ++i)
	{
		if(gs_aModuleMeta[i].m_IsEditor)
			ResetSettings((EModule)i);
	}
}

SModuleRect ClampRectToScreen(const SModuleRect &Rect, float HudWidth, float HudHeight)
{
	SModuleRect Result = Rect;
	if(Result.m_W <= 0.0f || Result.m_H <= 0.0f)
		return Result;

	Result.m_X = std::clamp(Result.m_X, 0.0f, std::max(0.0f, HudWidth - Result.m_W));
	Result.m_Y = std::clamp(Result.m_Y, 0.0f, std::max(0.0f, HudHeight - Result.m_H));
	return Result;
}

int BackgroundCorners(int DefaultCorners, float RectX, float RectY, float RectW, float RectH, float CanvasWidth, float CanvasHeight)
{
	int Corners = DefaultCorners;
	const float Eps = 0.01f;
	if(RectW <= 0.0f || RectH <= 0.0f)
		return Corners;
	if(RectX <= Eps)
		Corners &= ~IGraphics::CORNER_L;
	if(RectX + RectW >= CanvasWidth - Eps)
		Corners &= ~IGraphics::CORNER_R;
	if(RectY <= Eps)
		Corners &= ~IGraphics::CORNER_T;
	if(RectY + RectH >= CanvasHeight - Eps)
		Corners &= ~IGraphics::CORNER_B;
	return Corners;
}

void OnConsoleInit(IConsole *pConsole, IConfigManager *pConfigManager)
{
	EnsureInitialized();
	if(pConsole)
	{
		pConsole->Register(
			"hud_layout_set",
			"s[id] f[x] f[y] i[scale] i[mode] i[bg] ?i[bgcolor] ?i[enabled] ?i[alpha]",
			CFGFLAG_CLIENT | CFGFLAG_SAVE,
			ConHudLayoutSet,
			nullptr,
			"Set HUD module layout entry");
		pConsole->Register(
			"hud_layout_reset",
			"?s[id]",
			CFGFLAG_CLIENT,
			ConHudLayoutReset,
			nullptr,
			"Reset HUD module layout to default");
	}
	if(pConfigManager)
	{
		pConfigManager->RegisterCallback(ConfigSaveCallback, nullptr);
	}
}

} // namespace HudLayout
