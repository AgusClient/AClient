/* Copyright (C) 2026 AClient / BestProject Team */
#include "hud_layout.h"

#include <base/math.h>
#include <base/str.h>
#include <engine/config.h>
#include <engine/console.h>
#include <engine/shared/config.h>

#include <algorithm>
#include <cmath>
#include <cstring>

float GetKeystrokesKeyboardPresetWidthHudPx(int Preset);

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
	bool m_ScaleSupported;
	bool m_AlphaSupported;
	int m_MinScale;
	int m_MaxScale;
	int m_MinAlpha;
	int m_MaxAlpha;
};

static const SModuleMeta gs_aModuleMeta[MODULE_COUNT] = {
	{"score", "Score", true, true, false, true, 100, 100, 0, 100},
	{"chat", "Chat", true, true, false, true, 100, 100, 0, 100},
	{"votes", "Votes", true, true, true, true, 50, 200, 0, 100},
	{"mini_statboard", "RaceStats Overlay", true, true, true, true, 20, 200, 0, 100},
	{"speedometer", "Speedometer", true, true, true, true, 25, 300, 0, 100},
	{"spectator_count", "Spectator Count", true, true, true, true, 25, 300, 0, 100},
	{"dummy_actions", "Dummy Actions", true, true, true, true, 25, 300, 0, 100},
	{"movement_info", "Movement Info", true, true, false, true, 100, 100, 0, 100},
	{"game_timer", "Game Timer", true, true, true, true, 25, 300, 0, 100},
	{"local_time", "Local Time", true, true, true, true, 50, 200, 0, 100},
	{"frozen_hud", "Frozen HUD", true, true, true, true, 25, 300, 0, 100},
	{"notify_last", "Notify Last", true, true, true, true, 25, 300, 0, 100},
	{"keystrokes_keyboard", "Keystrokes Keyboard", true, true, true, false, 25, 300, 100, 100},
	{"keystrokes_mouse", "Keystrokes Mouse", true, true, true, false, 25, 300, 100, 100},
};

static const SModuleLayout gs_aDefaultLayouts[MODULE_COUNT] = {
	{435.0f, 229.0f, 100, 0, true, true, 0x40000000U, 100}, // MODULE_SCORE
	{5.0f, 220.0f, 100, 0, true, false, 0x66000000U, 100},   // MODULE_CHAT
	{0.0f, 60.0f, 100, 0, true, true, 0x66000000U, 100},     // MODULE_VOTES
	{370.0f, 120.0f, 70, 0, true, true, 0x99000000U, 60},    // MODULE_MINI_STATBOARD
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

void SanitizeHudDims(float &HudWidth, float &HudHeight)
{
	if(!std::isfinite(HudHeight) || HudHeight < 1.0f)
		HudHeight = CANVAS_HEIGHT;
	if(!std::isfinite(HudWidth) || HudWidth < 1.0f)
		HudWidth = HudHeight * (16.0f / 9.0f);
}

void RefreshRuntimeOverrideFlag(EModule Module)
{
	const SModuleLayout &Cur = gs_aRuntimeLayouts[Module];
	const SModuleLayout &Def = gs_aDefaultLayouts[Module];
	if(!gs_aHasPositionOverride[Module] &&
		Cur.m_Scale == Def.m_Scale &&
		Cur.m_Mode == Def.m_Mode &&
		Cur.m_Enabled == Def.m_Enabled &&
		Cur.m_BackgroundEnabled == Def.m_BackgroundEnabled &&
		Cur.m_BackgroundColor == Def.m_BackgroundColor &&
		Cur.m_Alpha == Def.m_Alpha)
	{
		gs_aHasRuntimeOverride[Module] = false;
	}
}

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

float EstimateMovementInfoBoxHeight()
{
	constexpr float LineH = 8.0f;
	float BoxH = 3.0f * LineH * (float)(g_Config.m_ClShowhudPlayerPosition + g_Config.m_ClShowhudPlayerSpeed) +
		     2.0f * LineH * (float)g_Config.m_ClShowhudPlayerAngle;
	if(g_Config.m_ClShowhudPlayerPosition || g_Config.m_ClShowhudPlayerSpeed || g_Config.m_ClShowhudPlayerAngle)
		BoxH += 2.0f;
	return BoxH;
}

void EstimateModuleBounds(EModule Module, const SModuleLayout &Layout, float HudWidth, float HudHeight, float &OutW, float &OutH)
{
	(void)HudHeight;
	const SModuleMeta &Meta = gs_aModuleMeta[Module];
	const float S = Meta.m_ScaleSupported ? std::clamp(Layout.m_Scale / 100.0f, Meta.m_MinScale / 100.0f, Meta.m_MaxScale / 100.0f) : 1.0f;

	switch(Module)
	{
	case MODULE_SCORE:
		OutW = 58.0f;
		OutH = 46.0f;
		break;
	case MODULE_CHAT:
		OutW = 180.0f;
		OutH = 58.0f;
		break;
	case MODULE_VOTES:
		OutW = 120.0f * S;
		OutH = 38.0f * S;
		break;
	case MODULE_MINI_STATBOARD:
		OutW = 60.0f * S;
		OutH = 24.0f * S;
		break;
	case MODULE_SPEEDOMETER:
		OutW = 75.0f * S;
		OutH = 18.0f * S;
		break;
	case MODULE_SPECTATOR_COUNT:
		OutW = 17.0f * S;
		OutH = 14.0f * S;
		break;
	case MODULE_DUMMY_ACTIONS:
		OutW = 16.0f * S;
		OutH = 29.0f * S;
		break;
	case MODULE_MOVEMENT_INFO:
		OutW = 62.0f;
		OutH = std::max(26.0f, EstimateMovementInfoBoxHeight());
		break;
	case MODULE_GAME_TIMER:
		OutW = 50.0f * S;
		OutH = 14.0f * S;
		break;
	case MODULE_LOCAL_TIME:
		OutW = 26.0f * S;
		OutH = 12.5f * S;
		break;
	case MODULE_FROZEN_HUD:
	{
		const float TeeSize = g_Config.m_TcFrozenHudTeeSize * S;
		OutW = std::min(HudWidth, TeeSize * 3.0f);
		OutH = TeeSize + 3.0f;
		break;
	}
	case MODULE_NOTIFY_LAST:
	{
		const float FontSize = std::clamp(g_Config.m_TcNotifyWhenLastSize * S, 1.0f, 50.0f * S);
		OutW = std::max(FontSize, 8.0f);
		OutH = FontSize + 2.0f * S;
		break;
	}
	case MODULE_KEYSTROKES_KEYBOARD:
	{
		const float KbScale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);
		OutW = GetKeystrokesKeyboardPresetWidthHudPx(g_Config.m_AcKeystrokesKeyboardPreset) * KbScale;
		if(g_Config.m_AcKeystrokesStyle == 1)
		{
			const bool OnlyAd = g_Config.m_AcKeystrokesMcLayout == 1;
			int Rows = OnlyAd ? 1 : 2;
			if(!OnlyAd || g_Config.m_AcKeystrokesMcShowLmb != 0 || g_Config.m_AcKeystrokesMcShowRmb != 0)
				++Rows;
			if(!OnlyAd || g_Config.m_AcKeystrokesMcShowSpace != 0)
				++Rows;
			const float McH = (float)Rows * 150.0f + (float)(Rows - 1) * 8.0f;
			OutH = McH * 0.14f * KbScale;
		}
		else
		{
			OutH = (g_Config.m_AcKeystrokesKeyboardPreset == 2 ? 261.0f : 394.0f) * 0.14f * KbScale;
		}
		break;
	}
	case MODULE_KEYSTROKES_MOUSE:
	{
		const float MsScale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f) * 0.14f;
		OutW = 285.0f * MsScale;
		OutH = 421.0f * MsScale;
		break;
	}
	default:
		OutW = 40.0f * S;
		OutH = 20.0f * S;
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

	const SModuleMeta &Meta = gs_aModuleMeta[Module];
	SModuleLayout &Layout = gs_aRuntimeLayouts[Module];
	float X = pResult->GetFloat(1);
	float Y = pResult->GetFloat(2);
	if(!std::isfinite(X))
		X = gs_aDefaultLayouts[Module].m_X;
	if(!std::isfinite(Y))
		Y = gs_aDefaultLayouts[Module].m_Y;
	Layout.m_X = std::clamp(X, 0.0f, CANVAS_WIDTH);
	Layout.m_Y = std::clamp(Y, 0.0f, CANVAS_HEIGHT);
	Layout.m_Scale = Meta.m_ScaleSupported ? std::clamp(pResult->GetInteger(3), Meta.m_MinScale, Meta.m_MaxScale) : 100;
	Layout.m_Mode = pResult->GetInteger(4);
	Layout.m_BackgroundEnabled = pResult->GetInteger(5) != 0;
	if(pResult->NumArguments() > 6)
		Layout.m_BackgroundColor = (unsigned)pResult->GetInteger(6);
	if(pResult->NumArguments() > 7)
		Layout.m_Enabled = pResult->GetInteger(7) != 0;
	if(pResult->NumArguments() > 8)
		Layout.m_Alpha = Meta.m_AlphaSupported ? std::clamp(pResult->GetInteger(8), Meta.m_MinAlpha, Meta.m_MaxAlpha) : 100;

	bool HasPosOverride = true;
	if(pResult->NumArguments() > 9)
		HasPosOverride = pResult->GetInteger(9) != 0;
	if(!HasPosOverride)
	{
		Layout.m_X = gs_aDefaultLayouts[Module].m_X;
		Layout.m_Y = gs_aDefaultLayouts[Module].m_Y;
		Layout.m_Mode = gs_aDefaultLayouts[Module].m_Mode;
	}

	gs_aHasPositionOverride[Module] = HasPosOverride;
	gs_aHasRuntimeOverride[Module] = true;
	RefreshRuntimeOverrideFlag(Module);
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
			"hud_layout_set \"%s\" %.3f %.3f %d %d %d %d %d %d %d",
			Id(Module),
			Layout.m_X,
			Layout.m_Y,
			Layout.m_Scale,
			Layout.m_Mode,
			Layout.m_BackgroundEnabled ? 1 : 0,
			(int)Layout.m_BackgroundColor,
			Layout.m_Enabled ? 1 : 0,
			Layout.m_Alpha,
			gs_aHasPositionOverride[Module] ? 1 : 0);
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

bool IsScaleSupported(EModule Module)
{
	if(Module < 0 || Module >= MODULE_COUNT)
		return false;
	return gs_aModuleMeta[Module].m_ScaleSupported;
}

bool IsAlphaSupported(EModule Module)
{
	if(Module < 0 || Module >= MODULE_COUNT)
		return false;
	return gs_aModuleMeta[Module].m_AlphaSupported;
}

int MinScale(EModule Module)
{
	if(Module < 0 || Module >= MODULE_COUNT)
		return 100;
	return gs_aModuleMeta[Module].m_MinScale;
}

int MaxScale(EModule Module)
{
	if(Module < 0 || Module >= MODULE_COUNT)
		return 100;
	return gs_aModuleMeta[Module].m_MaxScale;
}

int MinAlpha(EModule Module)
{
	if(Module < 0 || Module >= MODULE_COUNT)
		return 100;
	return gs_aModuleMeta[Module].m_MinAlpha;
}

int MaxAlpha(EModule Module)
{
	if(Module < 0 || Module >= MODULE_COUNT)
		return 100;
	return gs_aModuleMeta[Module].m_MaxAlpha;
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
	if(!std::isfinite(CanvasX))
		return 0.0f;
	if(!std::isfinite(HudWidth) || HudWidth <= 0.0f)
		return CanvasX;
	return CanvasX * (HudWidth / CANVAS_WIDTH);
}

SModuleLayout Get(EModule Module, float HudWidth, float HudHeight)
{
	EnsureInitialized();
	if(Module < 0 || Module >= MODULE_COUNT)
		return SModuleLayout{};

	SanitizeHudDims(HudWidth, HudHeight);
	SModuleLayout Layout = gs_aRuntimeLayouts[Module];
	const SModuleMeta &Meta = gs_aModuleMeta[Module];

	if(Module == MODULE_MINI_STATBOARD)
	{
		Layout.m_Enabled = Layout.m_Enabled && (g_Config.m_AcMiniStatboard != 0);
		Layout.m_Scale = std::clamp(g_Config.m_AcMiniStatboardScale, Meta.m_MinScale, Meta.m_MaxScale);
		Layout.m_Alpha = std::clamp(g_Config.m_AcMiniStatboardOpacity, Meta.m_MinAlpha, Meta.m_MaxAlpha);
	}
	else if(Module == MODULE_SPEEDOMETER)
	{
		Layout.m_Enabled = Layout.m_Enabled && (g_Config.m_AcHudSpeedometer != 0);
		if(!gs_aHasRuntimeOverride[MODULE_SPEEDOMETER])
		{
			Layout.m_Scale = std::clamp(g_Config.m_AcHudSpeedometerScale, 50, 200);
			Layout.m_Alpha = std::clamp(g_Config.m_AcHudSpeedometerAlpha, 20, 100);
		}
	}
	else if(Module == MODULE_KEYSTROKES_KEYBOARD)
	{
		Layout.m_Enabled = Layout.m_Enabled && (g_Config.m_AcKeystrokesKeyboard != 0);
	}
	else if(Module == MODULE_KEYSTROKES_MOUSE)
	{
		Layout.m_Enabled = Layout.m_Enabled && (g_Config.m_AcKeystrokesMouse != 0);
	}

	Layout.m_Scale = Meta.m_ScaleSupported ? std::clamp(Layout.m_Scale, Meta.m_MinScale, Meta.m_MaxScale) : 100;
	Layout.m_Alpha = Meta.m_AlphaSupported ? std::clamp(Layout.m_Alpha, Meta.m_MinAlpha, Meta.m_MaxAlpha) : 100;

	float EstW = 0.0f;
	float EstH = 0.0f;
	EstimateModuleBounds(Module, Layout, HudWidth, HudHeight, EstW, EstH);

	if(!gs_aHasPositionOverride[Module])
	{
		const float S = Meta.m_ScaleSupported ? (Layout.m_Scale / 100.0f) : 1.0f;
		switch(Module)
		{
		case MODULE_SCORE:
			Layout.m_X = std::max(0.0f, HudWidth - 65.0f);
			Layout.m_Y = 229.0f;
			break;
		case MODULE_CHAT:
		{
			const float ChatFont = g_Config.m_ClChatFontSize / 10.0f;
			const float StatusH = g_Config.m_TcStatusBar ? (float)g_Config.m_TcStatusBarHeight : 0.0f;
			const float PreviewBaseY = 300.0f - (20.0f * ChatFont / 6.0f + StatusH);
			Layout.m_X = 5.0f;
			Layout.m_Y = std::max(0.0f, PreviewBaseY - 58.0f);
			break;
		}
		case MODULE_VOTES:
			Layout.m_X = 0.0f;
			Layout.m_Y = 60.0f;
			break;
		case MODULE_MINI_STATBOARD:
		{
			const float StatW = 130.0f * S;
			const float StatH = 55.0f * S;
			const float PctX = std::clamp((float)g_Config.m_AcMiniStatboardX / 100.0f, 0.0f, 1.0f);
			const float PctY = std::clamp((float)g_Config.m_AcMiniStatboardY / 100.0f, 0.0f, 1.0f);
			Layout.m_X = std::max(0.0f, HudWidth - StatW) * PctX;
			Layout.m_Y = std::max(0.0f, HudHeight - StatH) * PctY;
			break;
		}
		case MODULE_SPEEDOMETER:
		{
			const float PctX = std::clamp((float)g_Config.m_AcHudSpeedometerX / 100.0f, 0.0f, 1.0f);
			const float PctY = std::clamp((float)g_Config.m_AcHudSpeedometerY / 100.0f, 0.0f, 1.0f);
			Layout.m_X = std::max(0.0f, HudWidth - EstW) * PctX;
			Layout.m_Y = std::max(0.0f, HudHeight - EstH) * PctY;
			break;
		}
		case MODULE_SPECTATOR_COUNT:
		{
			const float MoveH = EstimateMovementInfoBoxHeight();
			const float MoveGap = (g_Config.m_ClShowhudPlayerPosition || g_Config.m_ClShowhudPlayerSpeed || g_Config.m_ClShowhudPlayerAngle) ? 4.0f : 0.0f;
			Layout.m_X = std::max(0.0f, HudWidth - EstW);
			Layout.m_Y = 285.0f - EstH - 4.0f - MoveGap - MoveH - (g_Config.m_ClShowhudScore ? 56.0f : 0.0f);
			break;
		}
		case MODULE_DUMMY_ACTIONS:
		{
			const float MoveH = EstimateMovementInfoBoxHeight();
			const float MoveGap = (g_Config.m_ClShowhudPlayerPosition || g_Config.m_ClShowhudPlayerSpeed || g_Config.m_ClShowhudPlayerAngle) ? 4.0f : 0.0f;
			Layout.m_X = std::max(0.0f, HudWidth - EstW);
			Layout.m_Y = 285.0f - EstH - 4.0f - MoveGap - MoveH - (g_Config.m_ClShowhudScore ? 56.0f : 0.0f);
			break;
		}
		case MODULE_MOVEMENT_INFO:
		{
			const float MoveH = EstimateMovementInfoBoxHeight();
			Layout.m_X = std::max(0.0f, HudWidth - 62.0f);
			Layout.m_Y = 285.0f - std::max(26.0f, MoveH) - 4.0f - (g_Config.m_ClShowhudScore ? 56.0f : 0.0f);
			break;
		}
		case MODULE_GAME_TIMER:
			Layout.m_X = std::max(0.0f, HudWidth * 0.5f - 25.0f * S);
			Layout.m_Y = 2.0f;
			break;
		case MODULE_LOCAL_TIME:
		{
			const float EstTextW = (g_Config.m_TcShowLocalTimeSeconds ? 24.0f : 15.0f) * S;
			Layout.m_X = std::max(0.0f, (HudWidth / 7.0f) * 3.0f - (EstTextW + 15.0f));
			Layout.m_Y = 0.0f;
			break;
		}
		case MODULE_FROZEN_HUD:
		{
			const float TeeSize = g_Config.m_TcFrozenHudTeeSize * S;
			const float Aspect = HudWidth / HudHeight;
			Layout.m_X = std::max(0.0f, HudWidth * 0.5f + 38.0f * Aspect / 1.78f - TeeSize * 0.5f);
			Layout.m_Y = 0.0f;
			break;
		}
		case MODULE_NOTIFY_LAST:
			Layout.m_X = (g_Config.m_TcNotifyWhenLastX / 100.0f) * HudWidth;
			Layout.m_Y = (g_Config.m_TcNotifyWhenLastY / 100.0f) * HudHeight;
			break;
		case MODULE_KEYSTROKES_MOUSE:
			if(!gs_aHasPositionOverride[MODULE_KEYSTROKES_KEYBOARD])
			{
				const float KbScale = std::clamp(gs_aRuntimeLayouts[MODULE_KEYSTROKES_KEYBOARD].m_Scale / 100.0f, 0.25f, 3.0f);
				const float KbX = CanvasXToHud(gs_aDefaultLayouts[MODULE_KEYSTROKES_KEYBOARD].m_X, HudWidth);
				Layout.m_X = KbX + GetKeystrokesKeyboardPresetWidthHudPx(g_Config.m_AcKeystrokesKeyboardPreset) * KbScale + 5.0f;
			}
			else
			{
				Layout.m_X = CanvasXToHud(Layout.m_X, HudWidth);
			}
			break;
		default:
			Layout.m_X = CanvasXToHud(Layout.m_X, HudWidth);
			break;
		}
	}
	else
	{
		Layout.m_X = CanvasXToHud(Layout.m_X, HudWidth);
	}

	Layout.m_X = std::clamp(Layout.m_X, 0.0f, std::max(0.0f, HudWidth - EstW));
	Layout.m_Y = std::clamp(Layout.m_Y, 0.0f, std::max(0.0f, HudHeight - EstH));
	return Layout;
}

SModuleLayout GetDefault(EModule Module, float HudWidth, float HudHeight)
{
	if(Module < 0 || Module >= MODULE_COUNT)
		return SModuleLayout{};

	SanitizeHudDims(HudWidth, HudHeight);
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
	if(!std::isfinite(X) || !std::isfinite(Y))
		return;

	gs_aRuntimeLayouts[Module].m_X = std::clamp(X, 0.0f, CANVAS_WIDTH);
	gs_aRuntimeLayouts[Module].m_Y = std::clamp(Y, 0.0f, CANVAS_HEIGHT);
	gs_aRuntimeLayouts[Module].m_Mode = PositionMode;
	gs_aHasRuntimeOverride[Module] = true;
	gs_aHasPositionOverride[Module] = true;
}

void SetScale(EModule Module, int Scale)
{
	EnsureInitialized();
	if(Module < 0 || Module >= MODULE_COUNT)
		return;

	const SModuleMeta &Meta = gs_aModuleMeta[Module];
	gs_aRuntimeLayouts[Module].m_Scale = Meta.m_ScaleSupported ? std::clamp(Scale, Meta.m_MinScale, Meta.m_MaxScale) : 100;
	gs_aHasRuntimeOverride[Module] = true;
	RefreshRuntimeOverrideFlag(Module);
	SyncModuleToConfig(Module);
}

void SetEnabled(EModule Module, bool Enabled)
{
	EnsureInitialized();
	if(Module < 0 || Module >= MODULE_COUNT)
		return;

	gs_aRuntimeLayouts[Module].m_Enabled = Enabled;
	gs_aHasRuntimeOverride[Module] = true;
	RefreshRuntimeOverrideFlag(Module);
	SyncModuleToConfig(Module);
}

void SetAlpha(EModule Module, int Alpha)
{
	EnsureInitialized();
	if(Module < 0 || Module >= MODULE_COUNT)
		return;

	const SModuleMeta &Meta = gs_aModuleMeta[Module];
	gs_aRuntimeLayouts[Module].m_Alpha = Meta.m_AlphaSupported ? std::clamp(Alpha, Meta.m_MinAlpha, Meta.m_MaxAlpha) : 100;
	gs_aHasRuntimeOverride[Module] = true;
	RefreshRuntimeOverrideFlag(Module);
	SyncModuleToConfig(Module);
}

bool IsEnabled(EModule Module)
{
	EnsureInitialized();
	if(Module < 0 || Module >= MODULE_COUNT)
		return false;
	switch(Module)
	{
	case MODULE_MINI_STATBOARD:
		return gs_aRuntimeLayouts[Module].m_Enabled && (g_Config.m_AcMiniStatboard != 0);
	case MODULE_SPEEDOMETER:
		return gs_aRuntimeLayouts[Module].m_Enabled && (g_Config.m_AcHudSpeedometer != 0);
	case MODULE_KEYSTROKES_KEYBOARD:
		return gs_aRuntimeLayouts[Module].m_Enabled && (g_Config.m_AcKeystrokesKeyboard != 0);
	case MODULE_KEYSTROKES_MOUSE:
		return gs_aRuntimeLayouts[Module].m_Enabled && (g_Config.m_AcKeystrokesMouse != 0);
	default:
		return gs_aRuntimeLayouts[Module].m_Enabled;
	}
}

int Alpha(EModule Module)
{
	EnsureInitialized();
	if(Module < 0 || Module >= MODULE_COUNT)
		return 100;
	if(Module == MODULE_MINI_STATBOARD)
		return std::clamp(g_Config.m_AcMiniStatboardOpacity, 0, 100);
	if(Module == MODULE_SPEEDOMETER && !gs_aHasRuntimeOverride[MODULE_SPEEDOMETER])
		return std::clamp(g_Config.m_AcHudSpeedometerAlpha, 20, 100);
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
	RefreshRuntimeOverrideFlag(Module);
}

void ResetScale(EModule Module)
{
	EnsureInitialized();
	if(Module < 0 || Module >= MODULE_COUNT)
		return;

	gs_aRuntimeLayouts[Module].m_Scale = gs_aDefaultLayouts[Module].m_Scale;
	RefreshRuntimeOverrideFlag(Module);
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
	if(!std::isfinite(Result.m_X))
		Result.m_X = 0.0f;
	if(!std::isfinite(Result.m_Y))
		Result.m_Y = 0.0f;
	if(!std::isfinite(Result.m_W) || !std::isfinite(Result.m_H) || Result.m_W <= 0.0f || Result.m_H <= 0.0f)
		return Result;

	SanitizeHudDims(HudWidth, HudHeight);
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
			"s[id] f[x] f[y] i[scale] i[mode] i[bg] ?i[bgcolor] ?i[enabled] ?i[alpha] ?i[pos_override]",
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
