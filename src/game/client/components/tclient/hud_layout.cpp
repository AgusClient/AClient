#include "hud_layout.h"

#include <base/math.h>
#include <base/str.h>

#include <engine/config.h>
#include <engine/console.h>
#include <engine/shared/config.h>

#include <algorithm>
#include <cstdlib>

namespace HudLayout
{

namespace
{

static const SModuleLayout gs_aModuleLayouts[MODULE_COUNT] = {
	{-1.0f, -1.0f, 100, 0, true, false, 0x66000000U, 100}, // MODULE_FPS
	{-1.0f, -1.0f, 100, 0, true, false, 0x66000000U, 100}, // MODULE_PING
	{-1.0f, -1.0f, 100, 0, true, false, 0x66000000U, 100}, // MODULE_FRAMETIME
	{-1.0f, -1.0f, 100, 0, true, false, 0x66000000U, 100}, // MODULE_KEYSTROKES
	{-1.0f, -1.0f, 100, 0, true, false, 0x66000000U, 100}, // MODULE_SPEEDOMETER
	{-1.0f, -1.0f, 100, 0, true, false, 0x66000000U, 100}, // MODULE_MUSIC_PLAYER
	{-1.0f, -1.0f, 100, 0, true, false, 0x66000000U, 100}, // MODULE_FROZEN_HUD
	{-1.0f, -1.0f, 100, 0, true, false, 0x66000000U, 100}, // MODULE_CHAT
	{-1.0f, -1.0f, 100, 0, true, false, 0x66000000U, 100}, // MODULE_VOTES
	{-1.0f, -1.0f, 100, 0, true, false, 0x66000000U, 100}, // MODULE_SCORE
	{-1.0f, -1.0f, 100, 0, true, false, 0x66000000U, 100}, // MODULE_NOTIFY_LAST
};

static const char *gs_apModuleIds[MODULE_COUNT] = {
	"fps",
	"ping",
	"frametime",
	"keystrokes",
	"speedometer",
	"music_player",
	"frozen_hud",
	"chat",
	"votes",
	"score",
	"notify_last",
};

static const char *gs_apModuleNames[MODULE_COUNT] = {
	"FPS",
	"Ping",
	"Frametime",
	"Keystrokes",
	"Speedometer",
	"Music Player",
	"Frozen HUD",
	"Chat",
	"Votes",
	"Score",
	"Notify Last",
};

static SModuleLayout gs_aRuntimeModuleLayouts[MODULE_COUNT];
static bool gs_RuntimeLayoutsInitialized = false;
static bool gs_ConfigCallbackRegistered = false;
static bool gs_ConsoleCommandRegistered = false;

void EnsureRuntimeLayouts()
{
	if(gs_RuntimeLayoutsInitialized)
		return;
	for(int i = 0; i < MODULE_COUNT; ++i)
		gs_aRuntimeModuleLayouts[i] = gs_aModuleLayouts[i];
	gs_RuntimeLayoutsInitialized = true;
}

bool HasRuntimeOverrideInternal(EModule Module)
{
	EnsureRuntimeLayouts();
	const SModuleLayout &Runtime = gs_aRuntimeModuleLayouts[Module];
	const SModuleLayout &Default = gs_aModuleLayouts[Module];
	return Runtime.m_X != Default.m_X ||
	       Runtime.m_Y != Default.m_Y ||
	       Runtime.m_Scale != Default.m_Scale ||
	       Runtime.m_Mode != Default.m_Mode ||
	       Runtime.m_Enabled != Default.m_Enabled ||
	       Runtime.m_BackgroundEnabled != Default.m_BackgroundEnabled ||
	       Runtime.m_BackgroundColor != Default.m_BackgroundColor ||
	       Runtime.m_Alpha != Default.m_Alpha;
}

SModuleLayout DynamicDefaultLayout(EModule Module, float HudWidth, float HudHeight)
{
	SModuleLayout Layout = gs_aModuleLayouts[Module];
	switch(Module)
	{
	case MODULE_FPS:
		Layout.m_X = (float)round_to_int(HudWidth - 36.0f);
		Layout.m_Y = 5.0f;
		break;
	case MODULE_PING:
		Layout.m_X = (float)round_to_int(HudWidth - 36.0f);
		Layout.m_Y = 20.0f;
		break;
	case MODULE_FRAMETIME:
		Layout.m_X = (float)round_to_int(HudWidth - 36.0f);
		Layout.m_Y = 35.0f;
		break;
	case MODULE_KEYSTROKES:
		Layout.m_X = 10.0f;
		Layout.m_Y = 160.0f;
		break;
	case MODULE_SPEEDOMETER:
		Layout.m_X = (float)round_to_int(HudWidth * 0.5f - 38.0f);
		Layout.m_Y = 240.0f;
		break;
	case MODULE_MUSIC_PLAYER:
		Layout.m_X = (float)round_to_int(HudWidth * 0.5f - 110.0f);
		Layout.m_Y = 5.0f;
		break;
	case MODULE_FROZEN_HUD:
		Layout.m_X = HudWidth * 0.5f + 60.0f;
		Layout.m_Y = 0.0f;
		break;
	case MODULE_CHAT:
		Layout.m_X = 5.0f;
		Layout.m_Y = 278.0f;
		break;
	case MODULE_VOTES:
		Layout.m_X = 0.0f;
		Layout.m_Y = 60.0f;
		break;
	case MODULE_SCORE:
		Layout.m_X = (float)round_to_int(HudWidth - 52.0f);
		Layout.m_Y = 220.0f;
		break;
	case MODULE_NOTIFY_LAST:
		Layout.m_X = (float)round_to_int(HudWidth * 0.2f);
		Layout.m_Y = 3.0f;
		break;
	default:
		Layout.m_X = 10.0f;
		Layout.m_Y = 10.0f;
		break;
	}
	return Layout;
}

SModuleLayout ResolveBaseLayout(EModule Module, float HudWidth, float HudHeight)
{
	EnsureRuntimeLayouts();
	SModuleLayout Layout = gs_aRuntimeModuleLayouts[Module];
	if(!HasPositionOverride(Module))
	{
		SModuleLayout Def = DynamicDefaultLayout(Module, HudWidth, HudHeight);
		Layout.m_X = Def.m_X;
		Layout.m_Y = Def.m_Y;
	}
	return Layout;
}

void ConHudLayoutSet(IConsole::IResult *pResult, void *pUserData)
{
	const char *pModuleId = pResult->GetString(0);
	EModule Module = ModuleFromId(pModuleId);
	if(Module >= MODULE_COUNT)
		return;

	EnsureRuntimeLayouts();
	SModuleLayout &Layout = gs_aRuntimeModuleLayouts[Module];
	Layout.m_X = pResult->GetFloat(1);
	Layout.m_Y = pResult->GetFloat(2);
	Layout.m_Scale = std::clamp(pResult->GetInteger(3), 25, 300);
	Layout.m_Mode = pResult->GetInteger(4);
	Layout.m_BackgroundEnabled = pResult->GetInteger(5) != 0;

	const char *pBgColorHex = pResult->GetString(6);
	if(pBgColorHex && pBgColorHex[0] != '\0')
	{
		char *pEnd = nullptr;
		unsigned long Val = std::strtoul(pBgColorHex, &pEnd, 16);
		if(pEnd && pEnd != pBgColorHex)
			Layout.m_BackgroundColor = (unsigned)Val;
	}

	if(pResult->NumArguments() > 7)
		Layout.m_Enabled = pResult->GetInteger(7) != 0;
	if(pResult->NumArguments() > 8)
		Layout.m_Alpha = std::clamp(pResult->GetInteger(8), 0, 100);
}

void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	EnsureRuntimeLayouts();
	for(int i = 0; i < MODULE_COUNT; ++i)
	{
		if(!HasRuntimeOverrideInternal((EModule)i))
			continue;

		const SModuleLayout &Layout = gs_aRuntimeModuleLayouts[i];
		char aLine[256];
		str_format(aLine, sizeof(aLine), "hud_layout_set \"%s\" %.2f %.2f %d %d %d %08X %d %d",
			gs_apModuleIds[i],
			Layout.m_X,
			Layout.m_Y,
			Layout.m_Scale,
			Layout.m_Mode,
			Layout.m_BackgroundEnabled ? 1 : 0,
			Layout.m_BackgroundColor,
			Layout.m_Enabled ? 1 : 0,
			Layout.m_Alpha);
		pConfigManager->WriteLine(aLine, ConfigDomain::ACLIENT);
	}
}

} // namespace

bool IsEditorModule(EModule Module)
{
	return Module >= 0 && Module < MODULE_COUNT;
}

bool IsPersistedModule(EModule Module)
{
	return Module >= 0 && Module < MODULE_COUNT;
}

const char *Id(EModule Module)
{
	return Module >= 0 && Module < MODULE_COUNT ? gs_apModuleIds[Module] : "unknown";
}

const char *Name(EModule Module)
{
	return Module >= 0 && Module < MODULE_COUNT ? gs_apModuleNames[Module] : "HUD Module";
}

EModule ModuleFromId(const char *pId)
{
	if(!pId || pId[0] == '\0')
		return MODULE_COUNT;
	for(int i = 0; i < MODULE_COUNT; ++i)
	{
		if(str_comp(gs_apModuleIds[i], pId) == 0)
			return (EModule)i;
	}
	return MODULE_COUNT;
}

SModuleLayout Get(EModule Module, float HudWidth, float HudHeight)
{
	return ResolveBaseLayout(Module, HudWidth, HudHeight);
}

SModuleLayout GetDefault(EModule Module, float HudWidth, float HudHeight)
{
	return DynamicDefaultLayout(Module, HudWidth, HudHeight);
}

bool HasRuntimeOverride(EModule Module)
{
	return Module >= 0 && Module < MODULE_COUNT && HasRuntimeOverrideInternal(Module);
}

bool HasPositionOverride(EModule Module)
{
	if(Module < 0 || Module >= MODULE_COUNT)
		return false;
	EnsureRuntimeLayouts();
	const SModuleLayout &Layout = gs_aRuntimeModuleLayouts[Module];
	return Layout.m_X >= 0.0f && Layout.m_Y >= 0.0f;
}

void SetPosition(EModule Module, float X, float Y)
{
	EnsureRuntimeLayouts();
	gs_aRuntimeModuleLayouts[Module].m_X = X;
	gs_aRuntimeModuleLayouts[Module].m_Y = Y;
}

void SetPosition(EModule Module, float X, float Y, EPositionMode PositionMode)
{
	EnsureRuntimeLayouts();
	gs_aRuntimeModuleLayouts[Module].m_X = X;
	gs_aRuntimeModuleLayouts[Module].m_Y = Y;
	gs_aRuntimeModuleLayouts[Module].m_Mode = PositionMode;
}

void SetScale(EModule Module, int Scale)
{
	EnsureRuntimeLayouts();
	gs_aRuntimeModuleLayouts[Module].m_Scale = std::clamp(Scale, 25, 300);
}

void SetEnabled(EModule Module, bool Enabled)
{
	EnsureRuntimeLayouts();
	gs_aRuntimeModuleLayouts[Module].m_Enabled = Enabled;
}

void SetAlpha(EModule Module, int Alpha)
{
	EnsureRuntimeLayouts();
	gs_aRuntimeModuleLayouts[Module].m_Alpha = std::clamp(Alpha, 0, 100);
}

bool IsEnabled(EModule Module)
{
	EnsureRuntimeLayouts();
	return gs_aRuntimeModuleLayouts[Module].m_Enabled;
}

int Alpha(EModule Module)
{
	EnsureRuntimeLayouts();
	return gs_aRuntimeModuleLayouts[Module].m_Alpha;
}

float AlphaFactor(EModule Module)
{
	return std::clamp(Alpha(Module) / 100.0f, 0.0f, 1.0f);
}

void ResetPosition(EModule Module)
{
	EnsureRuntimeLayouts();
	gs_aRuntimeModuleLayouts[Module].m_X = gs_aModuleLayouts[Module].m_X;
	gs_aRuntimeModuleLayouts[Module].m_Y = gs_aModuleLayouts[Module].m_Y;
	gs_aRuntimeModuleLayouts[Module].m_Mode = gs_aModuleLayouts[Module].m_Mode;
}

void ResetScale(EModule Module)
{
	EnsureRuntimeLayouts();
	gs_aRuntimeModuleLayouts[Module].m_Scale = gs_aModuleLayouts[Module].m_Scale;
}

void ResetSettings(EModule Module)
{
	EnsureRuntimeLayouts();
	gs_aRuntimeModuleLayouts[Module] = gs_aModuleLayouts[Module];
}

void ResetEditableModules()
{
	for(int Module = 0; Module < MODULE_COUNT; ++Module)
	{
		if(IsEditorModule((EModule)Module))
			ResetSettings((EModule)Module);
	}
}

SModuleRect ClampRectToScreen(const SModuleRect &Rect, float HudWidth, float HudHeight)
{
	SModuleRect Result = Rect;
	Result.m_X = std::clamp(Result.m_X, 0.0f, std::max(0.0f, HudWidth - Result.m_W));
	Result.m_Y = std::clamp(Result.m_Y, 0.0f, std::max(0.0f, HudHeight - Result.m_H));
	return Result;
}

float CanvasXToHud(float CanvasX, float HudWidth)
{
	return CanvasX * (HudWidth / CANVAS_WIDTH);
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
	if(!gs_ConsoleCommandRegistered && pConsole)
	{
		pConsole->Register(
			"hud_layout_set",
			"s[module] f[x] f[y] i[scale] i[mode] i[background_enabled] s[background_color] ?i[enabled] ?i[alpha]",
			CFGFLAG_CLIENT,
			ConHudLayoutSet,
			nullptr,
			"Set HUD module layout entry");
		gs_ConsoleCommandRegistered = true;
	}

	if(!gs_ConfigCallbackRegistered && pConfigManager)
	{
		pConfigManager->RegisterCallback(ConfigSaveCallback, nullptr, ConfigDomain::ACLIENT);
		gs_ConfigCallbackRegistered = true;
	}
}

} // namespace HudLayout
