/* Copyright (C) 2026 AClient / BestProject Team */
#include "hud_editor.h"

#include <base/color.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

namespace
{
constexpr float SNAP_THRESHOLD = 6.0f;
constexpr float SETTINGS_POPUP_WIDTH = 210.0f;
constexpr float SETTINGS_POPUP_HEIGHT = 168.0f;

constexpr float POPUP_FRAME_MARGIN = 5.0f;
constexpr float POPUP_FRAME_ROUNDING = 3.0f;
constexpr float POPUP_REVEAL_DURATION_S = 0.16f;

constexpr int RESIZE_CORNER_NONE = -1;
constexpr int RESIZE_CORNER_TL = 0;
constexpr int RESIZE_CORNER_TR = 1;
constexpr int RESIZE_CORNER_BL = 2;
constexpr int RESIZE_CORNER_BR = 3;
constexpr float RESIZE_HANDLE_RADIUS = 4.2f;
constexpr float RESIZE_HANDLE_THICKNESS = 1.8f;
constexpr float RESIZE_HANDLE_OUTLINE = 0.7f;
constexpr int RESIZE_HANDLE_ARC_SEGMENTS = 6;
constexpr float RESIZE_HANDLE_HIT_RADIUS = 5.5f;
constexpr float MIN_RESIZE_DIAGONAL = 12.0f;

constexpr int RESET_KIND_POSITION = 1;
constexpr int RESET_KIND_SCALE = 2;
constexpr int RESET_KIND_ALL = 3;
constexpr float RESET_ANIM_DURATION_S = 0.26f;

struct SResetTargetRestore
{
	bool m_HadPosOverride = false;
};

SResetTargetRestore gs_aResetTargetRestore[HudLayout::MODULE_COUNT];

struct STrigTables
{
	vec2 m_aHandleArc[4][RESIZE_HANDLE_ARC_SEGMENTS + 1];
	vec2 m_aCircle[7];
	vec2 m_aOutlineArc[4][9];

	STrigTables()
	{
		static constexpr float aStart[4] = {pi, -pi / 2.0f, pi / 2.0f, 0.0f};
		static constexpr float aEnd[4] = {3.0f * pi / 2.0f, 0.0f, pi, pi / 2.0f};
		for(int c = 0; c < 4; ++c)
		{
			for(int i = 0; i <= RESIZE_HANDLE_ARC_SEGMENTS; ++i)
			{
				const float Angle = mix(aStart[c], aEnd[c], i / (float)RESIZE_HANDLE_ARC_SEGMENTS);
				m_aHandleArc[c][i] = vec2(std::cos(Angle), std::sin(Angle));
			}
			for(int i = 0; i <= 8; ++i)
			{
				const float Angle = mix(aStart[c], aEnd[c], i / 8.0f);
				m_aOutlineArc[c][i] = vec2(std::cos(Angle), std::sin(Angle));
			}
		}
		for(int i = 0; i <= 6; ++i)
		{
			const float Angle = 2.0f * pi * i / 6.0f;
			m_aCircle[i] = vec2(std::cos(Angle), std::sin(Angle));
		}
	}
};

const STrigTables &GetTrigTables()
{
	static const STrigTables s_Tables;
	return s_Tables;
}

float EaseOutCubic(float T)
{
	const float Inv = 1.0f - std::clamp(T, 0.0f, 1.0f);
	return 1.0f - Inv * Inv * Inv;
}

void UpdatePhase(float &Phase, float Target, float Dt, float DurationS)
{
	if(DurationS <= 0.0001f)
	{
		Phase = Target;
		return;
	}
	const float Step = std::max(0.0f, Dt) / DurationS;
	if(Phase < Target)
		Phase = std::min(Target, Phase + Step);
	else if(Phase > Target)
		Phase = std::max(Target, Phase - Step);
}

vec2 CornerPoint(const CUIRect &Rect, int Corner)
{
	switch(Corner)
	{
	case RESIZE_CORNER_TL: return vec2(Rect.x, Rect.y);
	case RESIZE_CORNER_TR: return vec2(Rect.x + Rect.w, Rect.y);
	case RESIZE_CORNER_BL: return vec2(Rect.x, Rect.y + Rect.h);
	case RESIZE_CORNER_BR:
	default: return vec2(Rect.x + Rect.w, Rect.y + Rect.h);
	}
}

void CornerArcCenter(const CUIRect &Rect, int Corner, float Radius, vec2 &OutCenter)
{
	switch(Corner)
	{
	case RESIZE_CORNER_TL:
		OutCenter = vec2(Rect.x + Radius, Rect.y + Radius);
		break;
	case RESIZE_CORNER_TR:
		OutCenter = vec2(Rect.x + Rect.w - Radius, Rect.y + Radius);
		break;
	case RESIZE_CORNER_BL:
		OutCenter = vec2(Rect.x + Radius, Rect.y + Rect.h - Radius);
		break;
	case RESIZE_CORNER_BR:
	default:
		OutCenter = vec2(Rect.x + Rect.w - Radius, Rect.y + Rect.h - Radius);
		break;
	}
}

vec2 HandleCenterPoint(const CUIRect &Rect, int Corner)
{
	constexpr float Diag = 0.70710678f;
	vec2 Center;
	CornerArcCenter(Rect, Corner, RESIZE_HANDLE_RADIUS, Center);
	switch(Corner)
	{
	case RESIZE_CORNER_TL: return Center + vec2(-Diag, -Diag) * RESIZE_HANDLE_RADIUS;
	case RESIZE_CORNER_TR: return Center + vec2(Diag, -Diag) * RESIZE_HANDLE_RADIUS;
	case RESIZE_CORNER_BL: return Center + vec2(-Diag, Diag) * RESIZE_HANDLE_RADIUS;
	case RESIZE_CORNER_BR:
	default: return Center + vec2(Diag, Diag) * RESIZE_HANDLE_RADIUS;
	}
}

int CornerBit(int Corner)
{
	switch(Corner)
	{
	case RESIZE_CORNER_TL: return IGraphics::CORNER_TL;
	case RESIZE_CORNER_TR: return IGraphics::CORNER_TR;
	case RESIZE_CORNER_BL: return IGraphics::CORNER_BL;
	case RESIZE_CORNER_BR:
	default: return IGraphics::CORNER_BR;
	}
}

void DrawArcBand(IGraphics *pGraphics, vec2 Center, int Corner, float InnerRadius, float OuterRadius, ColorRGBA Color)
{
	if(Color.a <= 0.0f || OuterRadius <= InnerRadius)
		return;
	const auto &Dirs = GetTrigTables().m_aHandleArc[Corner];
	IGraphics::CFreeformItem aItems[RESIZE_HANDLE_ARC_SEGMENTS];
	for(int i = 0; i < RESIZE_HANDLE_ARC_SEGMENTS; ++i)
	{
		const vec2 &D0 = Dirs[i];
		const vec2 &D1 = Dirs[i + 1];
		aItems[i] = IGraphics::CFreeformItem(
			Center.x + D0.x * InnerRadius, Center.y + D0.y * InnerRadius,
			Center.x + D1.x * InnerRadius, Center.y + D1.y * InnerRadius,
			Center.x + D0.x * OuterRadius, Center.y + D0.y * OuterRadius,
			Center.x + D1.x * OuterRadius, Center.y + D1.y * OuterRadius);
	}
	pGraphics->SetColor(Color);
	pGraphics->QuadsDrawFreeform(aItems, RESIZE_HANDLE_ARC_SEGMENTS);
}

void DrawFilledCircle(IGraphics *pGraphics, vec2 Center, float Radius, ColorRGBA Color)
{
	if(Color.a <= 0.0f || Radius <= 0.0f)
		return;
	const auto &Dirs = GetTrigTables().m_aCircle;
	IGraphics::CFreeformItem aItems[6];
	for(int i = 0; i < 6; ++i)
	{
		aItems[i] = IGraphics::CFreeformItem(
			Center.x, Center.y,
			Center.x, Center.y,
			Center.x + Dirs[i].x * Radius, Center.y + Dirs[i].y * Radius,
			Center.x + Dirs[i + 1].x * Radius, Center.y + Dirs[i + 1].y * Radius);
	}
	pGraphics->SetColor(Color);
	pGraphics->QuadsDrawFreeform(aItems, 6);
}

void DrawHandleBracket(IGraphics *pGraphics, vec2 Center, int Corner, float InnerRadius, float OuterRadius, ColorRGBA Color)
{
	DrawArcBand(pGraphics, Center, Corner, InnerRadius, OuterRadius, Color);
	const float MidRadius = (InnerRadius + OuterRadius) * 0.5f;
	const float CapRadius = (OuterRadius - InnerRadius) * 0.5f;
	const auto &Dirs = GetTrigTables().m_aHandleArc[Corner];
	DrawFilledCircle(pGraphics, Center + Dirs[0] * MidRadius, CapRadius, Color);
	DrawFilledCircle(pGraphics, Center + Dirs[RESIZE_HANDLE_ARC_SEGMENTS] * MidRadius, CapRadius, Color);
}

CUIRect ComputeAnimRect(const CUIRect &OuterRect, bool GrowFromRight, float Phase)
{
	CUIRect AnimRect;
	AnimRect.w = OuterRect.w * Phase;
	AnimRect.h = OuterRect.h * Phase;
	AnimRect.y = OuterRect.y;
	AnimRect.x = GrowFromRight ? (OuterRect.x + OuterRect.w - AnimRect.w) : OuterRect.x;
	return AnimRect;
}

void DrawPopupFrame(const CUIRect &AnimRect, float Phase)
{
	if(Phase <= 0.001f || AnimRect.w <= 0.0f || AnimRect.h <= 0.0f)
		return;
	AnimRect.Draw(ColorRGBA(0.5f, 0.5f, 0.5f, 0.75f * Phase), IGraphics::CORNER_ALL, POPUP_FRAME_ROUNDING);
	if(AnimRect.w > 2.0f && AnimRect.h > 2.0f)
	{
		CUIRect InnerAnimRect;
		AnimRect.Margin(1.0f, &InnerAnimRect);
		InnerAnimRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.75f * Phase), IGraphics::CORNER_ALL, POPUP_FRAME_ROUNDING);
	}
}

CUIRect LerpRect(const CUIRect &A, const CUIRect &B, float T)
{
	CUIRect Result;
	Result.x = mix(A.x, B.x, T);
	Result.y = mix(A.y, B.y, T);
	Result.w = mix(A.w, B.w, T);
	Result.h = mix(A.h, B.h, T);
	return Result;
}

bool PointInRect(vec2 Point, const CUIRect &Rect)
{
	return Rect.w > 0.0f && Rect.h > 0.0f &&
	       Point.x >= Rect.x && Point.x <= Rect.x + Rect.w &&
	       Point.y >= Rect.y && Point.y <= Rect.y + Rect.h;
}

CUIRect ClampToBounds(CUIRect Rect, float Width, float Height)
{
	Rect.x = std::clamp(Rect.x, 0.0f, std::max(0.0f, Width - Rect.w));
	Rect.y = std::clamp(Rect.y, 0.0f, std::max(0.0f, Height - Rect.h));
	return Rect;
}

void EmitRoundedRectOutline(IGraphics *pGraphics, const CUIRect &Rect, int Corners, float Rounding, ColorRGBA Color)
{
	if(Rect.w <= 0.0f || Rect.h <= 0.0f || Color.a <= 0.0f)
		return;

	const float Radius = std::clamp(Rounding, 0.0f, std::min(Rect.w, Rect.h) * 0.5f);
	const float Left = Rect.x;
	const float Right = Rect.x + Rect.w;
	const float Top = Rect.y;
	const float Bottom = Rect.y + Rect.h;

	if(Radius <= 0.01f || Corners == IGraphics::CORNER_NONE)
	{
		const IGraphics::CLineItem aBoxLines[4] = {
			IGraphics::CLineItem(Left, Top, Right, Top),
			IGraphics::CLineItem(Right, Top, Right, Bottom),
			IGraphics::CLineItem(Right, Bottom, Left, Bottom),
			IGraphics::CLineItem(Left, Bottom, Left, Top)};
		pGraphics->SetColor(Color);
		pGraphics->LinesDraw(aBoxLines, 4);
		return;
	}

	constexpr int SegmentsPerCorner = 8;
	IGraphics::CLineItem aLines[SegmentsPerCorner * 4 + 4];
	int NumLines = 0;

	const auto &OutlineArc = GetTrigTables().m_aOutlineArc;
	auto AddLine = [&](vec2 From, vec2 To) {
		aLines[NumLines++] = IGraphics::CLineItem(From, To);
	};
	auto AddArc = [&](vec2 Center, int CornerIdx) {
		const vec2 *pDirs = OutlineArc[CornerIdx];
		vec2 Prev = Center + pDirs[0] * Radius;
		for(int i = 1; i <= SegmentsPerCorner; ++i)
		{
			const vec2 Cur = Center + pDirs[i] * Radius;
			AddLine(Prev, Cur);
			Prev = Cur;
		}
	};

	const bool TopLeftRounded = (Corners & IGraphics::CORNER_TL) != 0;
	const bool TopRightRounded = (Corners & IGraphics::CORNER_TR) != 0;
	const bool BottomLeftRounded = (Corners & IGraphics::CORNER_BL) != 0;
	const bool BottomRightRounded = (Corners & IGraphics::CORNER_BR) != 0;

	AddLine(
		vec2(Left + (TopLeftRounded ? Radius : 0.0f), Top),
		vec2(Right - (TopRightRounded ? Radius : 0.0f), Top));
	if(TopRightRounded)
		AddArc(vec2(Right - Radius, Top + Radius), RESIZE_CORNER_TR);

	AddLine(
		vec2(Right, Top + (TopRightRounded ? Radius : 0.0f)),
		vec2(Right, Bottom - (BottomRightRounded ? Radius : 0.0f)));
	if(BottomRightRounded)
		AddArc(vec2(Right - Radius, Bottom - Radius), RESIZE_CORNER_BR);

	AddLine(
		vec2(Right - (BottomRightRounded ? Radius : 0.0f), Bottom),
		vec2(Left + (BottomLeftRounded ? Radius : 0.0f), Bottom));
	if(BottomLeftRounded)
		AddArc(vec2(Left + Radius, Bottom - Radius), RESIZE_CORNER_BL);

	AddLine(
		vec2(Left, Bottom - (BottomLeftRounded ? Radius : 0.0f)),
		vec2(Left, Top + (TopLeftRounded ? Radius : 0.0f)));
	if(TopLeftRounded)
		AddArc(vec2(Left + Radius, Top + Radius), RESIZE_CORNER_TL);

	pGraphics->SetColor(Color);
	pGraphics->LinesDraw(aLines, NumLines);
}

void DrawRoundedRectOutline(IGraphics *pGraphics, const CUIRect &Rect, int Corners, float Rounding, ColorRGBA Color)
{
	if(Rect.w <= 0.0f || Rect.h <= 0.0f || Color.a <= 0.0f)
		return;
	pGraphics->TextureClear();
	pGraphics->LinesBegin();
	EmitRoundedRectOutline(pGraphics, Rect, Corners, Rounding, Color);
	pGraphics->LinesEnd();
}

float ComputePopupHeightForModule(HudLayout::EModule Module)
{
	float Height = SETTINGS_POPUP_HEIGHT;
	if(!HudLayout::IsScaleSupported(Module))
		Height -= 49.0f;
	if(!HudLayout::IsAlphaSupported(Module))
		Height -= 30.0f;
	return Height;
}

} // namespace

void CHudEditor::OnConsoleInit()
{
	HudLayout::OnConsoleInit(Console(), ConfigManager());
	Console()->Register("toggle_hud_editor", "", CFGFLAG_CLIENT, ConToggleHudEditor, this, "Toggle interactive HUD Editor");
	Console()->Register("ac_hud_editor", "", CFGFLAG_CLIENT, ConToggleHudEditor, this, "Toggle interactive HUD Editor");
}

void CHudEditor::ConToggleHudEditor(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	CHudEditor *pSelf = (CHudEditor *)pUserData;
	pSelf->Toggle();
}

void CHudEditor::Toggle()
{
	if(m_Active)
		Deactivate();
	else
		Activate();
}

void CHudEditor::Activate()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(m_Active)
		return;

	if(GameClient()->m_Menus.IsActive())
		GameClient()->m_Menus.SetActive(false);
	if(GameClient()->m_Chat.IsActive())
		GameClient()->m_Chat.DisableMode();
	if(GameClient()->m_GameConsole.IsActive())
	{
		if(!Ui()->Enabled())
			GameClient()->m_GameConsole.Toggle(CGameConsole::CONSOLETYPE_LOCAL);
		if(!Ui()->Enabled())
			GameClient()->m_GameConsole.Toggle(CGameConsole::CONSOLETYPE_LOCAL);
	}

	GameClient()->OnRelease();
	Input()->MouseModeRelative();

	const float WindowW = std::max(1.0f, (float)Graphics()->WindowWidth());
	const float WindowH = std::max(1.0f, (float)Graphics()->WindowHeight());
	const vec2 CurMouse = Ui()->UpdatedMousePos();
	if(CurMouse.x <= 0.0f && CurMouse.y <= 0.0f)
		Ui()->OnCursorMove(WindowW * 0.5f - CurMouse.x, WindowH * 0.5f - CurMouse.y);

	m_Active = true;
	m_MouseDownLast = Input()->KeyIsPressed(KEY_MOUSE_1);
	m_RightMouseDownLast = Input()->KeyIsPressed(KEY_MOUSE_2);
	m_Dragging = false;
	m_PressedOnReset = false;
	m_PressedModule = HudLayout::MODULE_COUNT;
	m_HoveredModule = HudLayout::MODULE_COUNT;
	m_SelectedModule = HudLayout::MODULE_COUNT;
	m_ResizingModule = HudLayout::MODULE_COUNT;
	m_ResizeCorner = RESIZE_CORNER_NONE;
	m_PopupRevealPhase = 0.0f;
	m_SuppressCloseAnim = false;
	m_PopupClosing = false;
	m_PopupClosePhase = 0.0f;
	m_ResetAnimCount = 0;
	m_FrameVisualCount = 0;
	for(int i = 0; i < HudLayout::MODULE_COUNT; ++i)
		m_aVisualIndex[i] = -1;

	Ui()->SetEnabled(true);
	Ui()->SetMouseSlow(false);
	Ui()->DisableMouseLock();
	Ui()->SetHotItem(nullptr);
	Ui()->SetActiveItem(nullptr);
	Ui()->ClosePopupMenus();
	Ui()->ClearHotkeys();
}

void CHudEditor::Deactivate()
{
	if(!m_Active && m_ResetAnimCount == 0)
		return;

	for(int i = 0; i < m_ResetAnimCount; ++i)
	{
		const SResetAnim &Anim = m_aResetAnims[i];
		if(Anim.m_Kind == RESET_KIND_ALL)
		{
			HudLayout::ResetSettings(Anim.m_Module);
		}
		else if(Anim.m_Kind == RESET_KIND_POSITION)
		{
			HudLayout::ResetPosition(Anim.m_Module);
		}
		else if(Anim.m_Kind == RESET_KIND_SCALE)
		{
			HudLayout::ResetScale(Anim.m_Module);
			if(!gs_aResetTargetRestore[Anim.m_Module].m_HadPosOverride)
				HudLayout::ResetPosition(Anim.m_Module);
		}
	}

	m_Active = false;
	m_MouseDownLast = false;
	m_RightMouseDownLast = false;
	m_Dragging = false;
	m_PressedOnReset = false;
	m_PressedModule = HudLayout::MODULE_COUNT;
	m_HoveredModule = HudLayout::MODULE_COUNT;
	m_SelectedModule = HudLayout::MODULE_COUNT;
	m_ResizingModule = HudLayout::MODULE_COUNT;
	m_ResizeCorner = RESIZE_CORNER_NONE;
	m_PopupRevealPhase = 0.0f;
	m_SuppressCloseAnim = false;
	m_PopupClosing = false;
	m_PopupClosePhase = 0.0f;
	m_ResetAnimCount = 0;

	Ui()->ClosePopupMenus();
	Ui()->SetHotItem(nullptr);
	Ui()->SetActiveItem(nullptr);
	Ui()->DisableMouseLock();
	Ui()->SetMouseSlow(false);
	Ui()->ClearHotkeys();
	if(!GameClient()->m_GameConsole.IsActive())
	{
		Ui()->SetEnabled(true);
		Input()->MouseModeRelative();
	}
	GameClient()->OnRelease();
}

void CHudEditor::OnStateChange(int NewState, int OldState)
{
	(void)OldState;
	if(NewState != IClient::STATE_ONLINE && NewState != IClient::STATE_DEMOPLAYBACK)
		Deactivate();
}

bool CHudEditor::OnCursorMove(float x, float y, IInput::ECursorType CursorType)
{
	if(!m_Active)
		return false;

	Ui()->ConvertMouseMove(&x, &y, CursorType);
	Ui()->OnCursorMove(x, y);
	return true;
}

bool CHudEditor::OnInput(const IInput::CEvent &Event)
{
	if(!m_Active)
		return false;
	if((Event.m_Flags & IInput::FLAG_PRESS) != 0 && Event.m_Key == KEY_ESCAPE)
	{
		if(Ui()->IsPopupOpen(&m_SettingsPopupId))
		{
			Ui()->ClosePopupMenus();
			m_PopupClosing = true;
			m_PopupClosePhase = 1.0f;
			m_PopupRevealPhase = 0.0f;
		}
		else
		{
			Deactivate();
		}
		Ui()->ClearHotkeys();
		return true;
	}
	Ui()->OnInput(Event);
	return true;
}

float CHudEditor::HudWidth() const
{
	const float Aspect = Graphics()->ScreenAspect();
	if(!std::isfinite(Aspect) || Aspect <= 0.05f)
		return HudLayout::CANVAS_HEIGHT * (16.0f / 9.0f);
	return HudLayout::CANVAS_HEIGHT * std::clamp(Aspect, 0.25f, 8.0f);
}

float CHudEditor::HudHeight() const
{
	return HudLayout::CANVAS_HEIGHT;
}

bool CHudEditor::IsEditableModule(HudLayout::EModule Module) const
{
	return HudLayout::IsEditorModule(Module);
}

bool CHudEditor::IsModuleEnabled(HudLayout::EModule Module) const
{
	return HudLayout::IsEnabled(Module);
}

CHudEditor::SModuleVisual CHudEditor::GetModuleVisual(HudLayout::EModule Module) const
{
	SModuleVisual Visual;
	Visual.m_Module = Module;
	Visual.m_Editable = IsEditableModule(Module);
	Visual.m_Enabled = IsModuleEnabled(Module);

	const float Width = HudWidth();
	const float Height = HudHeight();
	const auto Layout = HudLayout::Get(Module, Width, Height);
	const bool ScaleSupported = HudLayout::IsScaleSupported(Module);
	const float S = ScaleSupported ? std::clamp(Layout.m_Scale / 100.0f, HudLayout::MinScale(Module) / 100.0f, HudLayout::MaxScale(Module) / 100.0f) : 1.0f;

	auto ComputeBaseMovementHeight = [&](bool &OutPosOnly) -> float {
		const int ClientId = GameClient()->m_Snap.m_SpecInfo.m_Active ? GameClient()->m_Snap.m_SpecInfo.m_SpectatorId : GameClient()->m_Snap.m_LocalClientId;
		const bool SpecCharPresent = in_range(ClientId, 0, MAX_CLIENTS - 1) && GameClient()->m_aClients[ClientId].m_SpecCharPresent;
		OutPosOnly = (ClientId == SPEC_FREEVIEW) || SpecCharPresent;
		constexpr float LineH = 8.0f;
		if(OutPosOnly)
			return g_Config.m_ClShowhudPlayerPosition ? (3.0f * LineH + 2.0f) : 0.0f;
		float BoxH = 3.0f * LineH * (float)(g_Config.m_ClShowhudPlayerPosition + g_Config.m_ClShowhudPlayerSpeed) +
			     2.0f * LineH * (float)g_Config.m_ClShowhudPlayerAngle;
		if(g_Config.m_ClShowhudPlayerPosition || g_Config.m_ClShowhudPlayerSpeed || g_Config.m_ClShowhudPlayerAngle)
			BoxH += 2.0f;
		return BoxH;
	};

	auto ComputeClassicHudMovementHeight = [&]() -> float {
		bool PosOnly = false;
		return ComputeBaseMovementHeight(PosOnly);
	};

	auto ComputeMovementBoxHeight = [&]() -> float {
		bool PosOnly = false;
		constexpr float LineH = 8.0f;
		float BoxH = ComputeBaseMovementHeight(PosOnly);
		if(!g_Config.m_ClShowhudPlayerPosition && (PosOnly || (!g_Config.m_ClShowhudPlayerSpeed && !g_Config.m_ClShowhudPlayerAngle)))
			return 56.0f;

		if(Client()->DummyConnected())
		{
			int DummyClientId = -1;
			if(GameClient()->m_Snap.m_SpecInfo.m_Active)
			{
				const int SpectId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;
				if(SpectId == GameClient()->m_aLocalIds[0])
					DummyClientId = GameClient()->m_aLocalIds[1];
				else if(SpectId == GameClient()->m_aLocalIds[1])
					DummyClientId = GameClient()->m_aLocalIds[0];
				else
					DummyClientId = GameClient()->m_aLocalIds[1 - (g_Config.m_ClDummy ? 1 : 0)];
			}
			else
			{
				DummyClientId = GameClient()->m_aLocalIds[1 - (g_Config.m_ClDummy ? 1 : 0)];
			}

			if(in_range(DummyClientId, 0, MAX_CLIENTS - 1) && GameClient()->m_aClients[DummyClientId].m_Active)
			{
				if(g_Config.m_ClShowhudPlayerPosition && g_Config.m_TcShowhudDummyPosition)
					BoxH += 2.0f * LineH;
				if(!PosOnly && g_Config.m_ClShowhudPlayerSpeed && g_Config.m_TcShowhudDummySpeed)
					BoxH += 2.0f * LineH;
				if(!PosOnly && g_Config.m_ClShowhudPlayerAngle && g_Config.m_TcShowhudDummyAngle)
					BoxH += 1.0f * LineH;
			}
		}
		return BoxH;
	};

	switch(Module)
	{
	case HudLayout::MODULE_SCORE:
	{
		static bool s_ScoreTextCached = false;
		static float s_TextWidth10 = 0.0f;
		static float s_TextWidth100 = 0.0f;
		if(!s_ScoreTextCached)
		{
			s_TextWidth10 = TextRender()->TextWidth(14.0f, "10", -1, -1.0f);
			s_TextWidth100 = TextRender()->TextWidth(14.0f, "100", -1, -1.0f);
			s_ScoreTextCached = true;
		}
		float BoxW = s_TextWidth10 + 16.0f + 6.0f + 16.0f;
		if(GameClient()->IsTeamPlay() && GameClient()->m_Snap.m_pGameDataObj)
		{
			const float ImageSize = (GameClient()->m_Snap.m_pGameInfoObj && (GameClient()->m_Snap.m_pGameInfoObj->m_GameFlags & GAMEFLAG_FLAGS)) ? 16.0f : 3.0f;
			BoxW = s_TextWidth100 + ImageSize + 6.0f;
		}
		const float BoxX = HudLayout::HasPositionOverride(Module) ? Layout.m_X : std::max(0.0f, Width - BoxW);
		const float BoxY = HudLayout::HasPositionOverride(Module) ? Layout.m_Y : 229.0f;
		Visual.m_Rect = CUIRect{BoxX, BoxY, BoxW, 46.0f};
		Visual.m_Rounding = 5.0f;
		break;
	}
	case HudLayout::MODULE_CHAT:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, std::clamp((float)g_Config.m_ClChatWidth, 80.0f, Width) * S, 58.0f * S};
		Visual.m_Rounding = 6.0f;
		break;
	case HudLayout::MODULE_VOTES:
	{
		const float VoteScale = HudLayout::HasRuntimeOverride(Module) ? std::clamp(Layout.m_Scale / 100.0f, 0.5f, 2.0f) : 1.0f;
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 120.0f * VoteScale, 38.0f * VoteScale};
		Visual.m_Rounding = 3.0f;
		break;
	}
	case HudLayout::MODULE_MINI_STATBOARD:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 130.0f * S, 55.0f * S};
		Visual.m_Rounding = 5.0f;
		break;
	case HudLayout::MODULE_KEYSTROKES_KEYBOARD:
		Visual.m_Rect = GameClient()->m_Keystrokes.GetKeyboardHudEditorRect();
		if(g_Config.m_AcKeystrokesStyle == 0 && Visual.m_Rect.w > 0.0f)
		{
			const float KsScale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f) * 0.14f;
			const float ExactH = (g_Config.m_AcKeystrokesKeyboardPreset == 2 ? 261.0f : 394.0f) * KsScale;
			Visual.m_Rect.y = std::clamp(Layout.m_Y, 0.0f, std::max(0.0f, Height - ExactH));
			Visual.m_Rect.h = ExactH;
		}
		Visual.m_Rounding = 4.0f;
		break;
	case HudLayout::MODULE_KEYSTROKES_MOUSE:
		Visual.m_Rect = GameClient()->m_Keystrokes.GetMouseHudEditorRect();
		if(Visual.m_Rect.w <= 0.0f || Visual.m_Rect.h <= 0.0f)
			Visual.m_Editable = false;
		Visual.m_Rounding = 4.0f;
		break;
	case HudLayout::MODULE_SPEEDOMETER:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 75.0f * S, 18.0f * S};
		Visual.m_Rounding = 4.0f;
		break;
	case HudLayout::MODULE_SPECTATOR_COUNT:
	{
		static int s_LastSpecCount = -1;
		static float s_CachedSpecDigitW = 0.0f;
		int SpecCount = 3;
		if(!Client()->IsSixup() && GameClient()->m_Snap.m_pSpectatorCount && GameClient()->m_Snap.m_pSpectatorCount->m_NumSpectators > 0)
			SpecCount = GameClient()->m_Snap.m_pSpectatorCount->m_NumSpectators;
		if(SpecCount != s_LastSpecCount)
		{
			char aBuf[16];
			str_format(aBuf, sizeof(aBuf), "%d", SpecCount);
			s_CachedSpecDigitW = TextRender()->TextWidth(6.0f, aBuf);
			s_LastSpecCount = SpecCount;
		}
		const float BoxW = (13.0f + s_CachedSpecDigitW) * S;
		const float BoxH = 14.0f * S;
		float BoxX = Layout.m_X;
		float BoxY = Layout.m_Y;
		if(!HudLayout::HasPositionOverride(Module))
		{
			BoxX = Width - BoxW;
			BoxY = 285.0f - BoxH - 4.0f;
			if(g_Config.m_ClShowhudPlayerPosition || g_Config.m_ClShowhudPlayerSpeed || g_Config.m_ClShowhudPlayerAngle)
				BoxY -= 4.0f;
			BoxY -= ComputeClassicHudMovementHeight();
			if(g_Config.m_ClShowhudScore)
				BoxY -= 56.0f;
			if(g_Config.m_ClShowhudDummyActions &&
				(!GameClient()->m_Snap.m_pGameInfoObj || !(GameClient()->m_Snap.m_pGameInfoObj->m_GameStateFlags & GAMESTATEFLAG_GAMEOVER)) &&
				Client()->DummyConnected())
			{
				BoxY -= 33.0f;
			}
		}
		Visual.m_Rect = CUIRect{BoxX, BoxY, BoxW, BoxH};
		Visual.m_Rounding = 5.0f;
		break;
	}
	case HudLayout::MODULE_DUMMY_ACTIONS:
	{
		const float BoxW = 16.0f * S;
		const float BoxH = 29.0f * S;
		float BoxX = Layout.m_X;
		float BoxY = Layout.m_Y;
		if(!HudLayout::HasPositionOverride(Module))
		{
			BoxX = Width - BoxW;
			BoxY = 285.0f - BoxH - 4.0f;
			if(g_Config.m_ClShowhudPlayerPosition || g_Config.m_ClShowhudPlayerSpeed || g_Config.m_ClShowhudPlayerAngle)
				BoxY -= 4.0f;
			BoxY -= ComputeClassicHudMovementHeight();
			if(g_Config.m_ClShowhudScore)
				BoxY -= 56.0f;
		}
		Visual.m_Rect = CUIRect{BoxX, BoxY, BoxW, BoxH};
		Visual.m_Rounding = 5.0f;
		break;
	}
	case HudLayout::MODULE_MOVEMENT_INFO:
	{
		const float BoxW = 62.0f;
		const float BoxH = std::max(8.0f, ComputeMovementBoxHeight());
		float BoxX = Layout.m_X;
		float BoxY = Layout.m_Y;
		if(!HudLayout::HasPositionOverride(Module))
		{
			BoxX = Width - BoxW;
			BoxY = 285.0f - BoxH - 4.0f - (g_Config.m_ClShowhudScore ? 56.0f : 0.0f);
		}
		Visual.m_Rect = CUIRect{BoxX, BoxY, BoxW, BoxH};
		Visual.m_Rounding = 5.0f;
		break;
	}
	case HudLayout::MODULE_GAME_TIMER:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 50.0f * S, 14.0f * S};
		Visual.m_Rounding = 3.0f;
		break;
	case HudLayout::MODULE_LOCAL_TIME:
	{
		const float LocalScale = HudLayout::HasRuntimeOverride(Module) ? std::clamp(Layout.m_Scale / 100.0f, 0.5f, 2.0f) : 1.0f;
		const bool Seconds = g_Config.m_TcShowLocalTimeSeconds != 0;
		static float s_LastLocalScale = -1.0f;
		static int s_LastLocalSeconds = -1;
		static float s_CachedLocalTimeW = 0.0f;
		if(std::abs(LocalScale - s_LastLocalScale) > 0.001f || (int)Seconds != s_LastLocalSeconds)
		{
			s_CachedLocalTimeW = std::round(TextRender()->TextBoundingBox(5.0f * LocalScale, Seconds ? "00:00.00" : "00:00").m_W);
			s_LastLocalScale = LocalScale;
			s_LastLocalSeconds = Seconds ? 1 : 0;
		}
		const float BoxW = s_CachedLocalTimeW + 10.0f * LocalScale;
		const float BoxH = 12.5f * LocalScale;
		const float BoxX = HudLayout::HasPositionOverride(Module) ? Layout.m_X : ((Width / 7.0f) * 3.0f - (s_CachedLocalTimeW + 15.0f));
		const float BoxY = HudLayout::HasPositionOverride(Module) ? Layout.m_Y : 0.0f;
		Visual.m_Rect = CUIRect{BoxX, BoxY, BoxW, BoxH};
		Visual.m_Rounding = 3.75f;
		break;
	}
	case HudLayout::MODULE_FROZEN_HUD:
	{
		const float TeeSize = g_Config.m_TcFrozenHudTeeSize * S;
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, TeeSize * 3.0f, TeeSize + 3.0f};
		Visual.m_Rounding = 5.0f;
		break;
	}
	case HudLayout::MODULE_NOTIFY_LAST:
	{
		const float LastScale = HudLayout::HasRuntimeOverride(Module) ? std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f) : 1.0f;
		const float FontSize = std::clamp(g_Config.m_TcNotifyWhenLastSize * LastScale, 1.0f, 50.0f * LastScale);
		const char *pText = g_Config.m_TcNotifyWhenLastText[0] != '\0' ? g_Config.m_TcNotifyWhenLastText : "Last!";
		static float s_LastNotifyFontSize = -1.0f;
		static char s_aLastNotifyText[64] = "";
		static float s_CachedNotifyTextW = 0.0f;
		if(std::abs(FontSize - s_LastNotifyFontSize) > 0.001f || str_comp(s_aLastNotifyText, pText) != 0)
		{
			s_CachedNotifyTextW = TextRender()->TextWidth(FontSize, pText);
			s_LastNotifyFontSize = FontSize;
			str_copy(s_aLastNotifyText, pText, sizeof(s_aLastNotifyText));
		}
		const float BoxW = std::max(s_CachedNotifyTextW, std::max(FontSize, 8.0f));
		const float BoxH = std::max(FontSize + 2.0f * LastScale, 4.0f);
		const float BoxX = std::clamp(Layout.m_X, 1.0f, std::max(1.0f, Width - BoxW));
		const float BoxY = std::clamp(Layout.m_Y, 1.0f, std::max(1.0f, Height - BoxH));
		Visual.m_Rect = CUIRect{BoxX, BoxY, BoxW, BoxH};
		Visual.m_Rounding = 3.0f;
		break;
	}
	case HudLayout::MODULE_MUSIC_PLAYER:
	{
		Visual.m_Rect = GameClient()->m_MusicPlayer.GetHudEditorRect(false);
		if(Visual.m_Rect.w <= 0.0f || Visual.m_Rect.h <= 0.0f)
			Visual.m_Rect = GameClient()->m_MusicPlayer.GetHudEditorRect(true);
		Visual.m_Rounding = std::min(5.0f * S, Visual.m_Rect.h * 0.24f);
		break;
	}
	default:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 40.0f * S, 20.0f * S};
		Visual.m_Rounding = 4.0f;
		break;
	}

	Visual.m_Rounding = std::clamp(Visual.m_Rounding, 2.0f, 12.0f);
	Visual.m_Rect = ClampToBounds(Visual.m_Rect, Width, Height);
	Visual.m_Corners = HudLayout::BackgroundCorners(IGraphics::CORNER_ALL, Visual.m_Rect.x, Visual.m_Rect.y, Visual.m_Rect.w, Visual.m_Rect.h, Width, Height);
	return Visual;
}

void CHudEditor::RebuildFrameVisuals()
{
	m_FrameVisualCount = 0;
	for(int i = 0; i < HudLayout::MODULE_COUNT; ++i)
		m_aVisualIndex[i] = -1;

	for(int i = 0; i < HudLayout::MODULE_COUNT && m_FrameVisualCount < MAX_MODULE_VISUALS; ++i)
	{
		const SModuleVisual Visual = GetModuleVisual((HudLayout::EModule)i);
		if(Visual.m_Rect.w <= 0.0f || Visual.m_Rect.h <= 0.0f)
			continue;
		m_aVisualIndex[i] = m_FrameVisualCount;
		m_aFrameVisuals[m_FrameVisualCount++] = Visual;
	}
}

void CHudEditor::RefreshFrameVisual(HudLayout::EModule Module)
{
	if(Module < 0 || Module >= HudLayout::MODULE_COUNT)
		return;
	const int Idx = m_aVisualIndex[Module];
	if(Idx >= 0 && Idx < m_FrameVisualCount)
		m_aFrameVisuals[Idx] = GetModuleVisual(Module);
}

const CHudEditor::SModuleVisual *CHudEditor::FindFrameVisual(HudLayout::EModule Module) const
{
	if(Module < 0 || Module >= HudLayout::MODULE_COUNT)
		return nullptr;
	const int Idx = m_aVisualIndex[Module];
	if(Idx >= 0 && Idx < m_FrameVisualCount)
		return &m_aFrameVisuals[Idx];
	return nullptr;
}

HudLayout::EModule CHudEditor::HitTestModule(vec2 MousePos) const
{
	if(m_SelectedModule != HudLayout::MODULE_COUNT)
	{
		const SModuleVisual *pSelected = FindFrameVisual(m_SelectedModule);
		if(pSelected && pSelected->m_Editable && PointInRect(MousePos, pSelected->m_Rect))
			return m_SelectedModule;
	}
	for(int i = m_FrameVisualCount - 1; i >= 0; --i)
	{
		if(!m_aFrameVisuals[i].m_Editable)
			continue;
		if(PointInRect(MousePos, m_aFrameVisuals[i].m_Rect))
			return m_aFrameVisuals[i].m_Module;
	}
	for(int i = m_FrameVisualCount - 1; i >= 0; --i)
	{
		if(PointInRect(MousePos, m_aFrameVisuals[i].m_Rect))
			return m_aFrameVisuals[i].m_Module;
	}
	return HudLayout::MODULE_COUNT;
}

HudLayout::EModule CHudEditor::HitTestResizeHandle(vec2 MousePos, int &OutCorner) const
{
	OutCorner = RESIZE_CORNER_NONE;

	auto TryModule = [&](HudLayout::EModule Module) {
		if(Module == HudLayout::MODULE_COUNT || !IsEditableModule(Module) || !HudLayout::IsScaleSupported(Module))
			return false;
		const SModuleVisual *pVisual = FindFrameVisual(Module);
		if(!pVisual || pVisual->m_Rect.w <= 0.0f || pVisual->m_Rect.h <= 0.0f)
			return false;
		static constexpr int aCorners[4] = {RESIZE_CORNER_TL, RESIZE_CORNER_TR, RESIZE_CORNER_BL, RESIZE_CORNER_BR};
		for(int Corner : aCorners)
		{
			if((pVisual->m_Corners & CornerBit(Corner)) == 0)
				continue;
			if(distance(MousePos, HandleCenterPoint(pVisual->m_Rect, Corner)) <= RESIZE_HANDLE_HIT_RADIUS)
			{
				OutCorner = Corner;
				return true;
			}
		}
		return false;
	};

	if(TryModule(m_SelectedModule))
		return m_SelectedModule;
	if(m_HoveredModule != m_SelectedModule && TryModule(m_HoveredModule))
		return m_HoveredModule;
	return HudLayout::MODULE_COUNT;
}

void CHudEditor::ApplyDraggedPosition(HudLayout::EModule Module, const CUIRect &Rect)
{
	if(!IsEditableModule(Module))
		return;

	const float Width = HudWidth();
	if(!std::isfinite(Width) || Width < 1.0f || !std::isfinite(Rect.x) || !std::isfinite(Rect.y))
		return;
	const float CanvasScale = HudLayout::CANVAS_WIDTH / Width;
	HudLayout::SetPosition(Module, Rect.x * CanvasScale, Rect.y);
}

CUIRect CHudEditor::SnapRect(const CUIRect &Rect, HudLayout::EModule DraggedModule) const
{
	CUIRect Result = Rect;
	float BestDeltaX = SNAP_THRESHOLD + 1.0f;
	float BestDeltaY = SNAP_THRESHOLD + 1.0f;
	const float Width = HudWidth();
	const float Height = HudHeight();

	auto TrySnap = [](float Candidate, float Target, float &BestDelta) {
		const float Delta = Target - Candidate;
		if(absolute(Delta) <= SNAP_THRESHOLD && absolute(Delta) < absolute(BestDelta))
			BestDelta = Delta;
	};

	TrySnap(Result.x, 0.0f, BestDeltaX);
	TrySnap(Result.x + Result.w, Width, BestDeltaX);
	TrySnap(Result.x + Result.w * 0.5f, Width * 0.5f, BestDeltaX);
	TrySnap(Result.y, 0.0f, BestDeltaY);
	TrySnap(Result.y + Result.h, Height, BestDeltaY);
	TrySnap(Result.y + Result.h * 0.5f, Height * 0.5f, BestDeltaY);

	for(int i = 0; i < m_FrameVisualCount; ++i)
	{
		if(m_aFrameVisuals[i].m_Module == DraggedModule)
			continue;
		const CUIRect &Other = m_aFrameVisuals[i].m_Rect;
		if(Other.w <= 0.0f || Other.h <= 0.0f)
			continue;
		TrySnap(Result.x, Other.x, BestDeltaX);
		TrySnap(Result.x + Result.w, Other.x + Other.w, BestDeltaX);
		TrySnap(Result.x, Other.x + Other.w, BestDeltaX);
		TrySnap(Result.x + Result.w, Other.x, BestDeltaX);
		TrySnap(Result.x + Result.w * 0.5f, Other.x + Other.w * 0.5f, BestDeltaX);

		TrySnap(Result.y, Other.y, BestDeltaY);
		TrySnap(Result.y + Result.h, Other.y + Other.h, BestDeltaY);
		TrySnap(Result.y, Other.y + Other.h, BestDeltaY);
		TrySnap(Result.y + Result.h, Other.y, BestDeltaY);
		TrySnap(Result.y + Result.h * 0.5f, Other.y + Other.h * 0.5f, BestDeltaY);
	}

	if(absolute(BestDeltaX) <= SNAP_THRESHOLD)
		Result.x += BestDeltaX;
	if(absolute(BestDeltaY) <= SNAP_THRESHOLD)
		Result.y += BestDeltaY;

	return ClampToBounds(Result, Width, Height);
}

void CHudEditor::UpdateDragging(vec2 MousePos)
{
	if(!m_Dragging || m_PressedModule == HudLayout::MODULE_COUNT || !IsEditableModule(m_PressedModule))
		return;

	const SModuleVisual *pVisual = FindFrameVisual(m_PressedModule);
	if(!pVisual)
		return;

	CUIRect NewRect = pVisual->m_Rect;
	NewRect.x = MousePos.x - m_DragMouseOffset.x;
	NewRect.y = MousePos.y - m_DragMouseOffset.y;

	if(!Input()->ShiftIsPressed())
		NewRect = SnapRect(NewRect, m_PressedModule);
	else
		NewRect = ClampToBounds(NewRect, HudWidth(), HudHeight());

	ApplyDraggedPosition(m_PressedModule, NewRect);
	RefreshFrameVisual(m_PressedModule);
}

void CHudEditor::UpdateResizing(vec2 MousePos)
{
	if(m_ResizingModule == HudLayout::MODULE_COUNT || m_ResizeCorner == RESIZE_CORNER_NONE || !IsEditableModule(m_ResizingModule) || !HudLayout::IsScaleSupported(m_ResizingModule))
		return;

	const CUIRect &Anchor = m_ResizeAnchorRect;
	vec2 FixedPoint;
	switch(m_ResizeCorner)
	{
	case RESIZE_CORNER_TL: FixedPoint = vec2(Anchor.x + Anchor.w, Anchor.y + Anchor.h); break;
	case RESIZE_CORNER_TR: FixedPoint = vec2(Anchor.x, Anchor.y + Anchor.h); break;
	case RESIZE_CORNER_BL: FixedPoint = vec2(Anchor.x + Anchor.w, Anchor.y); break;
	case RESIZE_CORNER_BR:
	default: FixedPoint = vec2(Anchor.x, Anchor.y); break;
	}

	const vec2 DiagVec = CornerPoint(Anchor, m_ResizeCorner) - FixedPoint;
	const float StartDiagonal = length(DiagVec);
	if(StartDiagonal < MIN_RESIZE_DIAGONAL)
		return;

	const float Width = HudWidth();
	const float Height = HudHeight();
	const vec2 ClampedMouse(
		std::clamp(MousePos.x, 0.0f, Width),
		std::clamp(MousePos.y, 0.0f, Height));
	const vec2 DiagDir = DiagVec / StartDiagonal;
	const float ProjectedDiagonal = std::max(0.0f, dot(ClampedMouse - FixedPoint, DiagDir));
	const int StartScale = std::max(1, m_ResizeStartScale);
	const int MinS = HudLayout::MinScale(m_ResizingModule);
	const int MaxS = HudLayout::MaxScale(m_ResizingModule);
	const int NewScale = std::clamp((int)std::lround(StartScale * (ProjectedDiagonal / StartDiagonal)), MinS, MaxS);
	const float Factor = (float)NewScale / (float)StartScale;

	CUIRect NewRect;
	NewRect.w = Anchor.w * Factor;
	NewRect.h = Anchor.h * Factor;
	switch(m_ResizeCorner)
	{
	case RESIZE_CORNER_TL:
		NewRect.x = FixedPoint.x - NewRect.w;
		NewRect.y = FixedPoint.y - NewRect.h;
		break;
	case RESIZE_CORNER_TR:
		NewRect.x = FixedPoint.x;
		NewRect.y = FixedPoint.y - NewRect.h;
		break;
	case RESIZE_CORNER_BL:
		NewRect.x = FixedPoint.x - NewRect.w;
		NewRect.y = FixedPoint.y;
		break;
	case RESIZE_CORNER_BR:
	default:
		NewRect.x = FixedPoint.x;
		NewRect.y = FixedPoint.y;
		break;
	}
	NewRect = ClampToBounds(NewRect, Width, Height);

	HudLayout::SetScale(m_ResizingModule, NewScale);
	ApplyDraggedPosition(m_ResizingModule, NewRect);
	RefreshFrameVisual(m_ResizingModule);
}

CHudEditor::SResetAnim *CHudEditor::FindOrAddResetAnim(HudLayout::EModule Module)
{
	for(int i = 0; i < m_ResetAnimCount; ++i)
	{
		if(m_aResetAnims[i].m_Module == Module)
			return &m_aResetAnims[i];
	}
	if(m_ResetAnimCount >= MAX_MODULE_VISUALS)
		return nullptr;
	SResetAnim &NewAnim = m_aResetAnims[m_ResetAnimCount++];
	NewAnim = SResetAnim{};
	NewAnim.m_Module = Module;
	return &NewAnim;
}

void CHudEditor::CancelResetAnimation(HudLayout::EModule Module)
{
	for(int i = 0; i < m_ResetAnimCount; ++i)
	{
		if(m_aResetAnims[i].m_Module == Module)
		{
			m_aResetAnims[i] = m_aResetAnims[m_ResetAnimCount - 1];
			--m_ResetAnimCount;
			return;
		}
	}
}

void CHudEditor::StartResetAnimation(HudLayout::EModule Module, int Kind)
{
	if(!IsEditableModule(Module))
		return;

	const float Width = HudWidth();
	const float Height = HudHeight();
	const CUIRect StartRect = GetModuleVisual(Module).m_Rect;
	const int StartScale = HudLayout::Get(Module, Width, Height).m_Scale;
	const bool HadPosOverride = HudLayout::HasPositionOverride(Module);

	if(Kind == RESET_KIND_SCALE)
		HudLayout::ResetScale(Module);
	else if(Kind == RESET_KIND_POSITION)
		HudLayout::ResetPosition(Module);
	else
		HudLayout::ResetSettings(Module);

	const CUIRect TargetRect = GetModuleVisual(Module).m_Rect;
	const int TargetScale = HudLayout::Get(Module, Width, Height).m_Scale;

	const bool VisuallyUnchanged =
		StartScale == TargetScale &&
		std::abs(StartRect.x - TargetRect.x) < 0.05f &&
		std::abs(StartRect.y - TargetRect.y) < 0.05f &&
		std::abs(StartRect.w - TargetRect.w) < 0.05f &&
		std::abs(StartRect.h - TargetRect.h) < 0.05f;
	if(VisuallyUnchanged)
	{
		CancelResetAnimation(Module);
		return;
	}

	SResetAnim *pAnim = FindOrAddResetAnim(Module);
	if(!pAnim)
		return;

	pAnim->m_StartRect = StartRect;
	pAnim->m_TargetRect = TargetRect;
	pAnim->m_StartScale = StartScale;
	pAnim->m_TargetScale = TargetScale;
	pAnim->m_Kind = Kind;
	pAnim->m_Phase = 0.0f;
	gs_aResetTargetRestore[Module].m_HadPosOverride = HadPosOverride;

	if(Kind != RESET_KIND_POSITION)
		HudLayout::SetScale(Module, StartScale);
	ApplyDraggedPosition(Module, StartRect);
}

void CHudEditor::UpdateResetAnimation()
{
	if(m_ResetAnimCount == 0)
		return;

	const float Dt = Client()->RenderFrameTime();
	for(int i = 0; i < m_ResetAnimCount; ++i)
	{
		SResetAnim &Anim = m_aResetAnims[i];
		UpdatePhase(Anim.m_Phase, 1.0f, Dt, RESET_ANIM_DURATION_S);

		if(Anim.m_Phase >= 1.0f)
		{
			if(Anim.m_Kind == RESET_KIND_ALL)
			{
				HudLayout::ResetSettings(Anim.m_Module);
			}
			else if(Anim.m_Kind == RESET_KIND_POSITION)
			{
				HudLayout::ResetPosition(Anim.m_Module);
			}
			else
			{
				HudLayout::ResetScale(Anim.m_Module);
				if(!gs_aResetTargetRestore[Anim.m_Module].m_HadPosOverride)
					HudLayout::ResetPosition(Anim.m_Module);
			}
		}
		else
		{
			const float Eased = EaseOutCubic(Anim.m_Phase);
			const CUIRect CurRect = LerpRect(Anim.m_StartRect, Anim.m_TargetRect, Eased);
			if(Anim.m_Kind != RESET_KIND_POSITION)
			{
				const int CurScale = (int)std::lround(mix((float)Anim.m_StartScale, (float)Anim.m_TargetScale, Eased));
				HudLayout::SetScale(Anim.m_Module, CurScale);
			}
			ApplyDraggedPosition(Anim.m_Module, CurRect);
		}
	}

	int Write = 0;
	for(int Read = 0; Read < m_ResetAnimCount; ++Read)
	{
		if(m_aResetAnims[Read].m_Phase < 1.0f)
		{
			if(Write != Read)
				m_aResetAnims[Write] = m_aResetAnims[Read];
			++Write;
		}
	}
	m_ResetAnimCount = Write;
}

CUi::EPopupMenuFunctionResult CHudEditor::PopupModuleSettings(void *pContext, CUIRect View, bool Active)
{
	(void)Active;
	CHudEditor *pThis = static_cast<CHudEditor *>(pContext);
	if(pThis->m_SelectedModule == HudLayout::MODULE_COUNT)
		return CUi::POPUP_CLOSE_CURRENT;

	UpdatePhase(pThis->m_PopupRevealPhase, 1.0f, pThis->Client()->RenderFrameTime(), POPUP_REVEAL_DURATION_S);
	const float Phase = EaseOutCubic(pThis->m_PopupRevealPhase);

	CUIRect OuterRect = View;
	OuterRect.x -= POPUP_FRAME_MARGIN;
	OuterRect.y -= POPUP_FRAME_MARGIN;
	OuterRect.w += POPUP_FRAME_MARGIN * 2.0f;
	OuterRect.h += POPUP_FRAME_MARGIN * 2.0f;
	pThis->m_LastPopupOuterRect = OuterRect;

	const CUIRect AnimRect = ComputeAnimRect(OuterRect, pThis->m_PopupGrowFromRight, Phase);
	DrawPopupFrame(AnimRect, Phase);
	pThis->Ui()->ClipEnable(&AnimRect);

	const HudLayout::EModule Module = pThis->m_SelectedModule;
	const bool ScaleSupported = HudLayout::IsScaleSupported(Module);
	const bool AlphaSupported = HudLayout::IsAlphaSupported(Module);
	const bool Enabled = HudLayout::IsEnabled(Module);
	const auto SelectedLayout = HudLayout::Get(Module, pThis->HudWidth(), pThis->HudHeight());
	CUIRect Title, ToggleButton, ScaleLabel, ScaleSlider, OpacityLabel, OpacitySlider, ResetScaleButton, ResetPositionButton, ResetAllButton;
	View.HSplitTop(16.0f, &Title, &View);
	pThis->Ui()->DoLabel(&Title, HudLayout::Name(Module), 10.0f, TEXTALIGN_MC);
	View.HSplitTop(4.0f, nullptr, &View);
	View.HSplitTop(16.0f, &ToggleButton, &View);
	if(pThis->GameClient()->m_Menus.DoButton_CheckBox(&pThis->m_ToggleModuleButton, Localize("Enabled"), Enabled ? 1 : 0, &ToggleButton))
		HudLayout::SetEnabled(Module, !Enabled);

	if(ScaleSupported)
	{
		const int MinS = HudLayout::MinScale(Module);
		const int MaxS = HudLayout::MaxScale(Module);
		View.HSplitTop(4.0f, nullptr, &View);
		View.HSplitTop(12.0f, &ScaleLabel, &View);
		const int Scale = std::clamp(SelectedLayout.m_Scale, MinS, MaxS);
		char aScale[32];
		str_format(aScale, sizeof(aScale), "%s %d%%", Localize("Scale"), Scale);
		pThis->Ui()->DoLabel(&ScaleLabel, aScale, 8.0f, TEXTALIGN_ML);

		View.HSplitTop(14.0f, &ScaleSlider, &View);
		const float Relative = CUi::ms_LinearScrollbarScale.ToRelative(Scale, MinS, MaxS);
		const float NewRelative = pThis->Ui()->DoScrollbarH(&pThis->m_SelectedModule, &ScaleSlider, Relative);
		const int NewScale = std::clamp(CUi::ms_LinearScrollbarScale.ToAbsolute(NewRelative, MinS, MaxS), MinS, MaxS);
		if(NewScale != Scale)
		{
			pThis->CancelResetAnimation(Module);
			HudLayout::SetScale(Module, NewScale);
		}
	}

	if(AlphaSupported)
	{
		const int MinA = HudLayout::MinAlpha(Module);
		const int MaxA = HudLayout::MaxAlpha(Module);
		View.HSplitTop(4.0f, nullptr, &View);
		View.HSplitTop(12.0f, &OpacityLabel, &View);
		const int Opacity = std::clamp(HudLayout::Alpha(Module), MinA, MaxA);
		char aOpacity[32];
		str_format(aOpacity, sizeof(aOpacity), "%s %d%%", Localize("Opacity"), Opacity);
		pThis->Ui()->DoLabel(&OpacityLabel, aOpacity, 8.0f, TEXTALIGN_ML);

		View.HSplitTop(14.0f, &OpacitySlider, &View);
		const float OpacityRelative = CUi::ms_LinearScrollbarScale.ToRelative(Opacity, MinA, MaxA);
		const float NewOpacityRelative = pThis->Ui()->DoScrollbarH(&pThis->m_OpacitySlider, &OpacitySlider, OpacityRelative);
		const int NewOpacity = std::clamp(CUi::ms_LinearScrollbarScale.ToAbsolute(NewOpacityRelative, MinA, MaxA), MinA, MaxA);
		if(NewOpacity != Opacity)
			HudLayout::SetAlpha(Module, NewOpacity);
	}

	View.HSplitTop(6.0f, nullptr, &View);
	if(ScaleSupported)
	{
		View.HSplitTop(16.0f, &ResetScaleButton, &View);
		if(pThis->Ui()->DoButton_PopupMenu(&pThis->m_ResetScaleButton, Localize("Reset scale"), &ResetScaleButton, 8.0f, TEXTALIGN_MC))
			pThis->StartResetAnimation(Module, RESET_KIND_SCALE);
		View.HSplitTop(3.0f, nullptr, &View);
	}

	View.HSplitTop(16.0f, &ResetPositionButton, &View);
	if(pThis->Ui()->DoButton_PopupMenu(&pThis->m_ResetPositionButton, Localize("Reset position"), &ResetPositionButton, 8.0f, TEXTALIGN_MC))
		pThis->StartResetAnimation(Module, RESET_KIND_POSITION);

	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(16.0f, &ResetAllButton, &View);
	const ColorRGBA ResetAllColor(1.0f, 0.32f, 0.32f, 0.85f * pThis->Ui()->ButtonColorMul(&pThis->m_ResetSettingsButton));
	if(pThis->Ui()->DoButton_PopupMenu(&pThis->m_ResetSettingsButton, Localize("Reset all"), &ResetAllButton, 8.0f, TEXTALIGN_MC, 0.0f, false, true, ResetAllColor))
		pThis->StartResetAnimation(Module, RESET_KIND_ALL);

	pThis->Ui()->ClipDisable();
	return CUi::POPUP_KEEP_OPEN;
}

void CHudEditor::OpenModuleSettings(const SModuleVisual &Visual)
{
	if(!IsEditableModule(Visual.m_Module))
		return;

	m_SelectedModule = Visual.m_Module;
	const float Width = HudWidth();
	const float Height = HudHeight();
	const float UiScaleX = Ui()->Screen()->w / std::max(Width, 1.0f);
	const float UiScaleY = Ui()->Screen()->h / std::max(Height, 1.0f);
	constexpr float PopupMargin = 5.0f;
	constexpr float PopupGap = 6.0f;
	const float PopupWidth = SETTINGS_POPUP_WIDTH;
	const float PopupHeight = ComputePopupHeightForModule(Visual.m_Module);
	const CUIRect ModuleRectUi = {
		Visual.m_Rect.x * UiScaleX,
		Visual.m_Rect.y * UiScaleY,
		Visual.m_Rect.w * UiScaleX,
		Visual.m_Rect.h * UiScaleY};
	float PopupX = ModuleRectUi.x + ModuleRectUi.w + PopupGap;
	m_PopupGrowFromRight = false;
	if(PopupX + PopupWidth > Ui()->Screen()->w - PopupMargin)
	{
		PopupX = ModuleRectUi.x - PopupWidth - PopupGap;
		m_PopupGrowFromRight = true;
	}
	PopupX = std::clamp(PopupX, PopupMargin, std::max(PopupMargin, Ui()->Screen()->w - PopupWidth - PopupMargin));
	const float PopupY = std::clamp(ModuleRectUi.y, PopupMargin, std::max(PopupMargin, Ui()->Screen()->h - PopupHeight - PopupMargin));
	m_PopupRevealPhase = 0.0f;
	m_PopupClosing = false;
	m_PopupClosePhase = 0.0f;
	Ui()->ClosePopupMenus();
	SPopupMenuProperties Props;
	Props.m_BorderColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	Props.m_BackgroundColor = ColorRGBA(0.0f, 0.0f, 0.0f, 0.0f);
	Ui()->DoPopupMenu(&m_SettingsPopupId, PopupX, PopupY, PopupWidth, PopupHeight, this, PopupModuleSettings, Props);
}

void CHudEditor::RenderModulePreview(const SModuleVisual &Visual) const
{
	const CUIRect &Rect = Visual.m_Rect;
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;

	ColorRGBA Fill = Visual.m_Editable ? ColorRGBA(0.22f, 0.37f, 0.56f, 0.10f) : ColorRGBA(0.25f, 0.25f, 0.25f, 0.08f);
	if(!Visual.m_Enabled)
		Fill = ColorRGBA(0.12f, 0.14f, 0.18f, 0.22f);
	Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, Fill, Visual.m_Corners, Visual.m_Rounding);
	if(!Visual.m_Enabled)
		Graphics()->DrawRect(Rect.x, Rect.y, Rect.w, Rect.h, ColorRGBA(0.02f, 0.03f, 0.04f, 0.30f), Visual.m_Corners, Visual.m_Rounding);
}

void CHudEditor::RenderModuleOutline(const SModuleVisual &Visual, bool Hovered, bool Selected) const
{
	const CUIRect &Rect = Visual.m_Rect;
	ColorRGBA Color = Selected ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.92f) : (Hovered ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.78f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.46f));
	if(!Visual.m_Editable)
		Color.a *= 0.72f;
	if(!Visual.m_Enabled)
		Color.a *= 0.82f;

	DrawRoundedRectOutline(Graphics(), Rect, Visual.m_Corners, Visual.m_Rounding, Color);
}

void CHudEditor::RenderResizeHandles(const SModuleVisual &Visual) const
{
	const CUIRect &Rect = Visual.m_Rect;
	if(Rect.w <= 0.0f || Rect.h <= 0.0f || !HudLayout::IsScaleSupported(Visual.m_Module))
		return;

	const bool Active = m_ResizingModule == Visual.m_Module;
	const ColorRGBA Color = Active ? ColorRGBA(1.0f, 0.82f, 0.35f, 1.0f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f);
	constexpr ColorRGBA Outline(0.0f, 0.0f, 0.0f, 0.5f);
	constexpr float InnerRadius = RESIZE_HANDLE_RADIUS - RESIZE_HANDLE_THICKNESS * 0.5f;
	constexpr float OuterRadius = RESIZE_HANDLE_RADIUS + RESIZE_HANDLE_THICKNESS * 0.5f;
	static constexpr int aCorners[4] = {RESIZE_CORNER_TL, RESIZE_CORNER_TR, RESIZE_CORNER_BL, RESIZE_CORNER_BR};

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	for(int Corner : aCorners)
	{
		if((Visual.m_Corners & CornerBit(Corner)) == 0)
			continue;

		vec2 Center;
		CornerArcCenter(Rect, Corner, RESIZE_HANDLE_RADIUS, Center);
		DrawHandleBracket(Graphics(), Center, Corner, InnerRadius - RESIZE_HANDLE_OUTLINE, OuterRadius + RESIZE_HANDLE_OUTLINE, Outline);
		DrawHandleBracket(Graphics(), Center, Corner, InnerRadius, OuterRadius, Color);
	}
	Graphics()->QuadsEnd();
}

void CHudEditor::RenderClosingPopupFrame() const
{
	const float Phase = EaseOutCubic(m_PopupClosePhase);
	if(Phase <= 0.0f)
		return;
	const CUIRect AnimRect = ComputeAnimRect(m_LastPopupOuterRect, m_PopupGrowFromRight, Phase);
	DrawPopupFrame(AnimRect, Phase);
}

void CHudEditor::RenderModuleLabel(const SModuleVisual &Visual) const
{
	if(Visual.m_Rect.w <= 0.0f || Visual.m_Rect.h <= 0.0f)
		return;

	const char *pName = HudLayout::Name(Visual.m_Module);
	const char *pStatus = nullptr;
	int StatusKind = 0;
	if(Visual.m_Editable && !Visual.m_Enabled)
	{
		pStatus = Localize("disabled");
		StatusKind = 1;
	}
	else if(!Visual.m_Editable)
	{
		pStatus = Localize("preview");
		StatusKind = 2;
	}

	const float Width = HudWidth();
	const float Height = HudHeight();
	const float FontSize = 6.6f;
	const float StatusFontSize = 5.6f;

	static HudLayout::EModule s_CachedLabelModule = HudLayout::MODULE_COUNT;
	static int s_CachedLabelStatusKind = -1;
	static const char *s_pCachedStatus = nullptr;
	static float s_CachedNameW = 0.0f;
	static float s_CachedStatusW = 0.0f;
	if(s_CachedLabelModule != Visual.m_Module || s_CachedLabelStatusKind != StatusKind || s_pCachedStatus != pStatus)
	{
		s_CachedLabelModule = Visual.m_Module;
		s_CachedLabelStatusKind = StatusKind;
		s_pCachedStatus = pStatus;
		s_CachedNameW = TextRender()->TextWidth(FontSize, pName);
		s_CachedStatusW = pStatus ? TextRender()->TextWidth(StatusFontSize, pStatus) + 6.0f : 0.0f;
	}

	const float LabelW = std::max(12.0f, 10.0f + s_CachedNameW + s_CachedStatusW);
	const float LabelH = 13.0f;
	constexpr float TailSize = 2.6f;
	const float MaxLabelX = std::max(2.0f, Width - LabelW - 2.0f);
	const float X = std::clamp(Visual.m_Rect.x + (Visual.m_Rect.w - LabelW) * 0.5f, 2.0f, MaxLabelX);
	float Y = Visual.m_Rect.y - LabelH - TailSize - 2.0f;
	const bool PointingDown = Y >= 2.0f;
	if(!PointingDown)
		Y = std::clamp(Visual.m_Rect.y + Visual.m_Rect.h + TailSize + 2.0f, 2.0f, std::max(2.0f, Height - LabelH - TailSize - 2.0f));

	CUIRect LabelRect = {X, Y, LabelW, LabelH};
	Graphics()->DrawRect(LabelRect.x - 0.6f, LabelRect.y - 0.6f, LabelRect.w + 1.2f, LabelRect.h + 1.2f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.14f), IGraphics::CORNER_ALL, 6.0f);
	Graphics()->DrawRect(LabelRect.x, LabelRect.y, LabelRect.w, LabelRect.h, ColorRGBA(0.04f, 0.04f, 0.05f, 0.88f), IGraphics::CORNER_ALL, 5.5f);

	const float TailMinX = LabelRect.x + TailSize * 2.0f;
	const float TailMaxX = std::max(TailMinX, LabelRect.x + LabelRect.w - TailSize * 2.0f);
	const float TailCenterX = std::clamp(Visual.m_Rect.x + Visual.m_Rect.w * 0.5f, TailMinX, TailMaxX);
	const float TailBaseY = PointingDown ? LabelRect.y + LabelRect.h : LabelRect.y;
	const float TailDir = PointingDown ? 1.0f : -1.0f;
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.04f, 0.04f, 0.05f, 0.88f);
	IGraphics::CFreeformItem Tail(
		TailCenterX - TailSize, TailBaseY,
		TailCenterX + TailSize, TailBaseY,
		TailCenterX, TailBaseY + TailDir * TailSize * 1.5f,
		TailCenterX, TailBaseY + TailDir * TailSize * 1.5f);
	Graphics()->QuadsDrawFreeform(&Tail, 1);
	Graphics()->QuadsEnd();

	CUIRect NameRect, StatusRect;
	if(pStatus)
		LabelRect.VSplitRight(s_CachedStatusW, &NameRect, &StatusRect);
	else
		NameRect = LabelRect;
	Ui()->DoLabel(&NameRect, pName, FontSize, TEXTALIGN_MC);
	if(pStatus)
	{
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.55f));
		Ui()->DoLabel(&StatusRect, pStatus, StatusFontSize, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
}

void CHudEditor::RenderOverlay(vec2 MousePos)
{
	const float Width = HudWidth();
	const float Height = HudHeight();
	Graphics()->MapScreenToSize(Width, Height);
	Graphics()->TextureClear();
	Graphics()->DrawRect(0.0f, 0.0f, Width, Height, ColorRGBA(0.0f, 0.0f, 0.0f, 0.38f), IGraphics::CORNER_ALL, 0.0f);

	GameClient()->m_Keystrokes.RenderKeyboardPreview();
	GameClient()->m_Keystrokes.RenderMousePreview();
	{
		const bool MusicPlayerHasLiveRect = g_Config.m_BcMusicPlayer != 0 && GameClient()->m_MusicPlayer.HudReservation().m_Visible;
		GameClient()->m_MusicPlayer.RenderHudEditor(!MusicPlayerHasLiveRect);
	}
	Graphics()->MapScreenToSize(Width, Height);

	bool HasNonEditable = false;
	for(int i = 0; i < m_FrameVisualCount; ++i)
	{
		if(!m_aFrameVisuals[i].m_Editable)
		{
			HasNonEditable = true;
			RenderModulePreview(m_aFrameVisuals[i]);
		}
	}
	for(int i = 0; i < m_FrameVisualCount; ++i)
	{
		if(m_aFrameVisuals[i].m_Editable)
			RenderModulePreview(m_aFrameVisuals[i]);
	}

	auto OutlineColorFor = [](const SModuleVisual &Visual, bool Hovered, bool Selected) {
		ColorRGBA Color = Selected ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.92f) : (Hovered ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.78f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.46f));
		if(!Visual.m_Editable)
			Color.a *= 0.72f;
		if(!Visual.m_Enabled)
			Color.a *= 0.82f;
		return Color;
	};

	const SModuleVisual *pHoveredVisual = nullptr;
	const SModuleVisual *pSelectedVisual = nullptr;
	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	if(HasNonEditable)
	{
		for(int i = 0; i < m_FrameVisualCount; ++i)
		{
			const SModuleVisual &V = m_aFrameVisuals[i];
			if(V.m_Editable)
				continue;
			const bool Hovered = V.m_Module == m_HoveredModule;
			const bool Selected = V.m_Module == m_SelectedModule || V.m_Module == m_PressedModule;
			if(Hovered)
				pHoveredVisual = &V;
			if(Selected)
				pSelectedVisual = &V;
			else
				EmitRoundedRectOutline(Graphics(), V.m_Rect, V.m_Corners, V.m_Rounding, OutlineColorFor(V, Hovered, false));
		}
	}
	for(int i = 0; i < m_FrameVisualCount; ++i)
	{
		const SModuleVisual &V = m_aFrameVisuals[i];
		if(!V.m_Editable)
			continue;
		const bool Hovered = V.m_Module == m_HoveredModule;
		const bool Selected = V.m_Module == m_SelectedModule || V.m_Module == m_PressedModule;
		if(Hovered)
			pHoveredVisual = &V;
		if(Selected)
			pSelectedVisual = &V;
		else
			EmitRoundedRectOutline(Graphics(), V.m_Rect, V.m_Corners, V.m_Rounding, OutlineColorFor(V, Hovered, false));
	}
	if(pSelectedVisual)
	{
		const bool SelectedHovered = pSelectedVisual->m_Module == m_HoveredModule;
		EmitRoundedRectOutline(Graphics(), pSelectedVisual->m_Rect, pSelectedVisual->m_Corners, pSelectedVisual->m_Rounding, OutlineColorFor(*pSelectedVisual, SelectedHovered, true));
	}
	Graphics()->LinesEnd();

	if(pHoveredVisual && pHoveredVisual != pSelectedVisual && pHoveredVisual->m_Editable)
		RenderResizeHandles(*pHoveredVisual);
	if(pSelectedVisual && pSelectedVisual->m_Editable)
		RenderResizeHandles(*pSelectedVisual);
	if(pHoveredVisual)
		RenderModuleLabel(*pHoveredVisual);

	const bool PopupOpen = Ui()->IsPopupOpen(&m_SettingsPopupId);
	const bool MouseOverPopup = PopupOpen && (Ui()->IsPopupHovered() || PointInRect(Ui()->MousePos(), m_LastPopupOuterRect));
	CUIRect ResetRect = {8.0f, 8.0f, 66.0f, 16.0f};
	const bool ResetHovered = !MouseOverPopup && PointInRect(MousePos, ResetRect);
	const ColorRGBA ResetColor = m_PressedOnReset ? ColorRGBA(0.95f, 0.48f, 0.48f, 0.90f) :
							(ResetHovered ? ColorRGBA(0.95f, 0.48f, 0.48f, 0.55f) : ColorRGBA(0.95f, 0.48f, 0.48f, 0.36f));
	Graphics()->DrawRect(ResetRect.x, ResetRect.y, ResetRect.w, ResetRect.h, ResetColor, IGraphics::CORNER_ALL, 4.0f);
	Ui()->DoLabel(&ResetRect, Localize("Reset All"), 6.5f, TEXTALIGN_MC);

	Ui()->MapScreen();
	Ui()->RenderPopupMenus();
	if(m_PopupClosing)
	{
		UpdatePhase(m_PopupClosePhase, 0.0f, Client()->RenderFrameTime(), POPUP_REVEAL_DURATION_S);
		RenderClosingPopupFrame();
		if(m_PopupClosePhase <= 0.001f)
			m_PopupClosing = false;
	}
	Graphics()->MapScreenToSize(Width, Height);
	RenderTools()->RenderCursor(MousePos, 12.0f);
}

void CHudEditor::OnRender()
{
	if(!m_Active)
		return;
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		Deactivate();
		return;
	}
	if(GameClient()->m_Menus.IsActive())
	{
		Deactivate();
		return;
	}

	Ui()->SetMouseSlow(false);
	Ui()->StartCheck();
	Ui()->Update();
	Ui()->MapScreen();

	UpdateResetAnimation();
	RebuildFrameVisuals();

	const float Width = HudWidth();
	const float Height = HudHeight();
	const vec2 WindowSize(std::max(1.0f, (float)Graphics()->WindowWidth()), std::max(1.0f, (float)Graphics()->WindowHeight()));
	const vec2 UiMousePos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
	const vec2 UiToHudScale(Width / std::max(Ui()->Screen()->w, 1.0f), Height / std::max(Ui()->Screen()->h, 1.0f));
	const vec2 MousePos(
		std::clamp(UiMousePos.x * UiToHudScale.x, 0.0f, Width),
		std::clamp(UiMousePos.y * UiToHudScale.y, 0.0f, Height));
	const bool LeftDown = Input()->KeyIsPressed(KEY_MOUSE_1);
	const bool RightDown = Input()->KeyIsPressed(KEY_MOUSE_2);
	const bool LeftClicked = LeftDown && !m_MouseDownLast;
	const bool RightClicked = RightDown && !m_RightMouseDownLast;
	const bool WasPopupOpen = Ui()->IsPopupOpen(&m_SettingsPopupId);
	const bool MouseOverPopup = WasPopupOpen && (Ui()->IsPopupHovered() || PointInRect(UiMousePos, m_LastPopupOuterRect));

	m_HoveredModule = MouseOverPopup ? HudLayout::MODULE_COUNT : HitTestModule(MousePos);
	int ResizeHitCorner = RESIZE_CORNER_NONE;
	const HudLayout::EModule ResizeHitModule = MouseOverPopup ? HudLayout::MODULE_COUNT : HitTestResizeHandle(MousePos, ResizeHitCorner);

	CUIRect ResetRect = {8.0f, 8.0f, 66.0f, 16.0f};
	const bool ResetHovered = !MouseOverPopup && PointInRect(MousePos, ResetRect);

	if(RightClicked && !m_Dragging && m_ResizingModule == HudLayout::MODULE_COUNT && !MouseOverPopup)
	{
		if(m_HoveredModule != HudLayout::MODULE_COUNT && IsEditableModule(m_HoveredModule))
		{
			CancelResetAnimation(m_HoveredModule);
			m_SelectedModule = m_HoveredModule;
			if(const SModuleVisual *pVisual = FindFrameVisual(m_HoveredModule))
				OpenModuleSettings(*pVisual);
		}
		else if(WasPopupOpen)
		{
			Ui()->ClosePopupMenus();
		}
	}

	const bool PopupOpenNow = Ui()->IsPopupOpen(&m_SettingsPopupId);
	if(PopupOpenNow)
	{
		m_Dragging = false;
		m_PressedModule = HudLayout::MODULE_COUNT;
		m_ResizingModule = HudLayout::MODULE_COUNT;
		m_ResizeCorner = RESIZE_CORNER_NONE;
		m_PressedOnReset = false;
	}
	else if(LeftClicked && ResetHovered)
	{
		for(int i = 0; i < HudLayout::MODULE_COUNT; ++i)
		{
			const auto Mod = (HudLayout::EModule)i;
			if(IsEditableModule(Mod))
				StartResetAnimation(Mod, RESET_KIND_ALL);
		}
		m_Dragging = false;
		m_PressedModule = HudLayout::MODULE_COUNT;
		m_ResizingModule = HudLayout::MODULE_COUNT;
		m_ResizeCorner = RESIZE_CORNER_NONE;
		m_SelectedModule = HudLayout::MODULE_COUNT;
		m_PressedOnReset = true;
		m_SuppressCloseAnim = true;
		Ui()->ClosePopupMenus();
	}
	else if(LeftClicked && ResizeHitModule != HudLayout::MODULE_COUNT)
	{
		m_PressedOnReset = false;
		m_PressMousePos = MousePos;
		m_SelectedModule = ResizeHitModule;
		m_ResizingModule = ResizeHitModule;
		m_ResizeCorner = ResizeHitCorner;
		CancelResetAnimation(ResizeHitModule);
		if(const SModuleVisual *pVisual = FindFrameVisual(ResizeHitModule))
			m_ResizeAnchorRect = pVisual->m_Rect;
		m_ResizeStartScale = std::max(1, HudLayout::Get(ResizeHitModule, Width, Height).m_Scale);
	}
	else if(LeftClicked)
	{
		m_PressedOnReset = false;
		m_PressMousePos = MousePos;
		m_SelectedModule = m_HoveredModule;
		m_PressedModule = (m_HoveredModule != HudLayout::MODULE_COUNT && IsEditableModule(m_HoveredModule)) ? m_HoveredModule : HudLayout::MODULE_COUNT;
		if(m_PressedModule != HudLayout::MODULE_COUNT)
		{
			CancelResetAnimation(m_PressedModule);
			if(const SModuleVisual *pVisual = FindFrameVisual(m_PressedModule))
				m_DragMouseOffset = MousePos - vec2(pVisual->m_Rect.x, pVisual->m_Rect.y);
		}
	}
	else if(LeftDown && m_MouseDownLast && m_ResizingModule != HudLayout::MODULE_COUNT)
	{
		UpdateResizing(MousePos);
	}
	else if(LeftDown && m_MouseDownLast && m_PressedModule != HudLayout::MODULE_COUNT)
	{
		if(!m_Dragging && distance(m_PressMousePos, MousePos) > 2.0f)
			m_Dragging = true;
		UpdateDragging(MousePos);
	}
	else if(!LeftDown && m_MouseDownLast)
	{
		m_Dragging = false;
		m_PressedModule = HudLayout::MODULE_COUNT;
		m_ResizingModule = HudLayout::MODULE_COUNT;
		m_ResizeCorner = RESIZE_CORNER_NONE;
		m_PressedOnReset = false;
	}

	m_MouseDownLast = LeftDown;
	m_RightMouseDownLast = RightDown;

	RenderOverlay(MousePos);

	if(WasPopupOpen && !Ui()->IsPopupOpen(&m_SettingsPopupId) && !m_SuppressCloseAnim)
	{
		m_PopupClosing = true;
		m_PopupClosePhase = 1.0f;
	}
	m_SuppressCloseAnim = false;

	Ui()->FinishCheck();
	Ui()->ClearHotkeys();
}
