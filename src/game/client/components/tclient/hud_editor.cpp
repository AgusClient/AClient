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

void CornerArcParams(const CUIRect &Rect, int Corner, float Radius, vec2 &OutCenter, float &OutStartAngle, float &OutEndAngle)
{
	switch(Corner)
	{
	case RESIZE_CORNER_TL:
		OutCenter = vec2(Rect.x + Radius, Rect.y + Radius);
		OutStartAngle = pi;
		OutEndAngle = 3.0f * pi / 2.0f;
		break;
	case RESIZE_CORNER_TR:
		OutCenter = vec2(Rect.x + Rect.w - Radius, Rect.y + Radius);
		OutStartAngle = -pi / 2.0f;
		OutEndAngle = 0.0f;
		break;
	case RESIZE_CORNER_BL:
		OutCenter = vec2(Rect.x + Radius, Rect.y + Rect.h - Radius);
		OutStartAngle = pi / 2.0f;
		OutEndAngle = pi;
		break;
	case RESIZE_CORNER_BR:
	default:
		OutCenter = vec2(Rect.x + Rect.w - Radius, Rect.y + Rect.h - Radius);
		OutStartAngle = 0.0f;
		OutEndAngle = pi / 2.0f;
		break;
	}
}

vec2 HandleCenterPoint(const CUIRect &Rect, int Corner)
{
	vec2 Center;
	float StartAngle, EndAngle;
	CornerArcParams(Rect, Corner, RESIZE_HANDLE_RADIUS, Center, StartAngle, EndAngle);
	const float Mid = (StartAngle + EndAngle) * 0.5f;
	return Center + vec2(std::cos(Mid), std::sin(Mid)) * RESIZE_HANDLE_RADIUS;
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

void DrawArcBand(IGraphics *pGraphics, vec2 Center, float StartAngle, float EndAngle, float InnerRadius, float OuterRadius, ColorRGBA Color)
{
	if(Color.a <= 0.0f || OuterRadius <= InnerRadius)
		return;
	pGraphics->SetColor(Color);
	for(int i = 0; i < RESIZE_HANDLE_ARC_SEGMENTS; ++i)
	{
		const float Angle0 = mix(StartAngle, EndAngle, i / (float)RESIZE_HANDLE_ARC_SEGMENTS);
		const float Angle1 = mix(StartAngle, EndAngle, (i + 1) / (float)RESIZE_HANDLE_ARC_SEGMENTS);
		IGraphics::CFreeformItem Item(
			Center.x + std::cos(Angle0) * InnerRadius, Center.y + std::sin(Angle0) * InnerRadius,
			Center.x + std::cos(Angle1) * InnerRadius, Center.y + std::sin(Angle1) * InnerRadius,
			Center.x + std::cos(Angle0) * OuterRadius, Center.y + std::sin(Angle0) * OuterRadius,
			Center.x + std::cos(Angle1) * OuterRadius, Center.y + std::sin(Angle1) * OuterRadius);
		pGraphics->QuadsDrawFreeform(&Item, 1);
	}
}

void DrawFilledCircle(IGraphics *pGraphics, vec2 Center, float Radius, ColorRGBA Color)
{
	if(Color.a <= 0.0f || Radius <= 0.0f)
		return;
	pGraphics->SetColor(Color);
	constexpr int Segments = 6;
	for(int i = 0; i < Segments; ++i)
	{
		const float Angle0 = 2.0f * pi * i / (float)Segments;
		const float Angle1 = 2.0f * pi * (i + 1) / (float)Segments;
		IGraphics::CFreeformItem Item(
			Center.x, Center.y,
			Center.x, Center.y,
			Center.x + std::cos(Angle0) * Radius, Center.y + std::sin(Angle0) * Radius,
			Center.x + std::cos(Angle1) * Radius, Center.y + std::sin(Angle1) * Radius);
		pGraphics->QuadsDrawFreeform(&Item, 1);
	}
}

void DrawHandleBracket(IGraphics *pGraphics, vec2 Center, float StartAngle, float EndAngle, float InnerRadius, float OuterRadius, ColorRGBA Color)
{
	DrawArcBand(pGraphics, Center, StartAngle, EndAngle, InnerRadius, OuterRadius, Color);
	const float MidRadius = (InnerRadius + OuterRadius) * 0.5f;
	const float CapRadius = (OuterRadius - InnerRadius) * 0.5f;
	DrawFilledCircle(pGraphics, Center + vec2(std::cos(StartAngle), std::sin(StartAngle)) * MidRadius, CapRadius, Color);
	DrawFilledCircle(pGraphics, Center + vec2(std::cos(EndAngle), std::sin(EndAngle)) * MidRadius, CapRadius, Color);
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
	AnimRect.Draw(ColorRGBA(0.5f, 0.5f, 0.5f, 0.75f * Phase), IGraphics::CORNER_ALL, POPUP_FRAME_ROUNDING);
	CUIRect InnerAnimRect;
	AnimRect.Margin(1.0f, &InnerAnimRect);
	InnerAnimRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.75f * Phase), IGraphics::CORNER_ALL, POPUP_FRAME_ROUNDING);
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
	return Point.x >= Rect.x && Point.x <= Rect.x + Rect.w &&
	       Point.y >= Rect.y && Point.y <= Rect.y + Rect.h;
}

CUIRect ClampToBounds(CUIRect Rect, float Width, float Height)
{
	Rect.x = std::clamp(Rect.x, 0.0f, std::max(0.0f, Width - Rect.w));
	Rect.y = std::clamp(Rect.y, 0.0f, std::max(0.0f, Height - Rect.h));
	return Rect;
}

void DrawRoundedRectOutline(IGraphics *pGraphics, const CUIRect &Rect, int Corners, float Rounding, ColorRGBA Color)
{
	if(Rect.w <= 0.0f || Rect.h <= 0.0f || Color.a <= 0.0f)
		return;

	const float Radius = std::clamp(Rounding, 0.0f, std::min(Rect.w, Rect.h) * 0.5f);
	if(Radius <= 0.01f || Corners == IGraphics::CORNER_NONE)
	{
		Rect.DrawOutline(Color);
		return;
	}

	constexpr int SegmentsPerCorner = 8;
	IGraphics::CLineItem aLines[SegmentsPerCorner * 4 + 4];
	int NumLines = 0;

	auto AddLine = [&](vec2 From, vec2 To) {
		aLines[NumLines++] = IGraphics::CLineItem(From, To);
	};

	auto AddArc = [&](vec2 Center, float StartAngle, float EndAngle) {
		vec2 Prev = vec2(
			Center.x + std::cos(StartAngle) * Radius,
			Center.y + std::sin(StartAngle) * Radius);
		for(int i = 1; i <= SegmentsPerCorner; ++i)
		{
			const float T = i / (float)SegmentsPerCorner;
			const float Angle = mix(StartAngle, EndAngle, T);
			const vec2 Cur(
				Center.x + std::cos(Angle) * Radius,
				Center.y + std::sin(Angle) * Radius);
			AddLine(Prev, Cur);
			Prev = Cur;
		}
	};

	const bool TopLeftRounded = (Corners & IGraphics::CORNER_TL) != 0;
	const bool TopRightRounded = (Corners & IGraphics::CORNER_TR) != 0;
	const bool BottomLeftRounded = (Corners & IGraphics::CORNER_BL) != 0;
	const bool BottomRightRounded = (Corners & IGraphics::CORNER_BR) != 0;
	const float Left = Rect.x;
	const float Right = Rect.x + Rect.w;
	const float Top = Rect.y;
	const float Bottom = Rect.y + Rect.h;

	AddLine(
		vec2(Left + (TopLeftRounded ? Radius : 0.0f), Top),
		vec2(Right - (TopRightRounded ? Radius : 0.0f), Top));
	if(TopRightRounded)
		AddArc(vec2(Right - Radius, Top + Radius), -pi / 2.0f, 0.0f);

	AddLine(
		vec2(Right, Top + (TopRightRounded ? Radius : 0.0f)),
		vec2(Right, Bottom - (BottomRightRounded ? Radius : 0.0f)));
	if(BottomRightRounded)
		AddArc(vec2(Right - Radius, Bottom - Radius), 0.0f, pi / 2.0f);

	AddLine(
		vec2(Right - (BottomRightRounded ? Radius : 0.0f), Bottom),
		vec2(Left + (BottomLeftRounded ? Radius : 0.0f), Bottom));
	if(BottomLeftRounded)
		AddArc(vec2(Left + Radius, Bottom - Radius), pi / 2.0f, pi);

	AddLine(
		vec2(Left, Bottom - (BottomLeftRounded ? Radius : 0.0f)),
		vec2(Left, Top + (TopLeftRounded ? Radius : 0.0f)));
	if(TopLeftRounded)
		AddArc(vec2(Left + Radius, Top + Radius), pi, 3.0f * pi / 2.0f);

	pGraphics->TextureClear();
	pGraphics->LinesBegin();
	pGraphics->SetColor(Color);
	pGraphics->LinesDraw(aLines, NumLines);
	pGraphics->LinesEnd();
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

	if(GameClient()->m_Menus.IsActive())
		GameClient()->m_Menus.SetActive(false);
	if(GameClient()->m_GameConsole.IsActive())
	{
		if(!Ui()->Enabled())
			GameClient()->m_GameConsole.Toggle(CGameConsole::CONSOLETYPE_LOCAL);
		if(!Ui()->Enabled())
			GameClient()->m_GameConsole.Toggle(CGameConsole::CONSOLETYPE_LOCAL);
	}

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
	Ui()->ClosePopupMenus();
	Ui()->ClearHotkeys();
}

void CHudEditor::Deactivate()
{
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
			Ui()->ClosePopupMenus();
		else
			Deactivate();
		Ui()->ClearHotkeys();
		return true;
	}
	Ui()->OnInput(Event);
	return true;
}

float CHudEditor::HudWidth() const
{
	return HudLayout::CANVAS_HEIGHT * Graphics()->ScreenAspect();
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
	const float S = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);

	switch(Module)
	{
	case HudLayout::MODULE_SCORE:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 65.0f * S, 46.0f * S};
		Visual.m_Rounding = 5.0f;
		break;
	case HudLayout::MODULE_CHAT:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 180.0f * S, 58.0f * S};
		Visual.m_Rounding = 6.0f;
		break;
	case HudLayout::MODULE_VOTES:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 120.0f * S, 38.0f * S};
		Visual.m_Rounding = 3.0f;
		break;
	case HudLayout::MODULE_MINI_STATBOARD:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 130.0f * S, 55.0f * S};
		Visual.m_Rounding = 5.0f;
		break;
	case HudLayout::MODULE_KEYSTROKES_KEYBOARD:
		Visual.m_Rect = GameClient()->m_Keystrokes.GetKeyboardHudEditorRect();
		Visual.m_Rounding = 4.0f;
		break;
	case HudLayout::MODULE_KEYSTROKES_MOUSE:
		Visual.m_Rect = GameClient()->m_Keystrokes.GetMouseHudEditorRect();
		Visual.m_Rounding = 4.0f;
		break;
	case HudLayout::MODULE_SPEEDOMETER:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 75.0f * S, 18.0f * S};
		Visual.m_Rounding = 4.0f;
		break;
	case HudLayout::MODULE_SPECTATOR_COUNT:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 32.0f * S, 14.0f * S};
		Visual.m_Rounding = 5.0f;
		break;
	case HudLayout::MODULE_DUMMY_ACTIONS:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 16.0f * S, 29.0f * S};
		Visual.m_Rounding = 5.0f;
		break;
	case HudLayout::MODULE_MOVEMENT_INFO:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 62.0f * S, 50.0f * S};
		Visual.m_Rounding = 5.0f;
		break;
	case HudLayout::MODULE_GAME_TIMER:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 50.0f * S, 14.0f * S};
		Visual.m_Rounding = 3.0f;
		break;
	case HudLayout::MODULE_LOCAL_TIME:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 38.0f * S, 12.5f * S};
		Visual.m_Rounding = 3.75f;
		break;
	case HudLayout::MODULE_FROZEN_HUD:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 78.0f * S, 18.0f * S};
		Visual.m_Rounding = 5.0f;
		break;
	case HudLayout::MODULE_NOTIFY_LAST:
	{
		const float FontSize = std::clamp(g_Config.m_TcNotifyWhenLastSize * S, 1.0f, 50.0f * S);
		const char *pText = g_Config.m_TcNotifyWhenLastText[0] != '\0' ? g_Config.m_TcNotifyWhenLastText : "Last!";
		const float TextW = TextRender()->TextWidth(FontSize, pText);
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, std::max(TextW, 8.0f), FontSize + 2.0f * S};
		Visual.m_Rounding = 3.0f;
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
		m_aVisualIndex[i] = m_FrameVisualCount;
		m_aFrameVisuals[m_FrameVisualCount++] = GetModuleVisual((HudLayout::EModule)i);
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
		if(Module == HudLayout::MODULE_COUNT || !IsEditableModule(Module))
			return false;
		const SModuleVisual *pVisual = FindFrameVisual(Module);
		if(!pVisual)
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

	const float CanvasScale = HudLayout::CANVAS_WIDTH / std::max(HudWidth(), 1.0f);
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
	if(m_ResizingModule == HudLayout::MODULE_COUNT || m_ResizeCorner == RESIZE_CORNER_NONE || !IsEditableModule(m_ResizingModule))
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

	const float StartDiagonal = distance(FixedPoint, CornerPoint(Anchor, m_ResizeCorner));
	if(StartDiagonal < MIN_RESIZE_DIAGONAL)
		return;

	const vec2 ClampedMouse(
		std::clamp(MousePos.x, 0.0f, HudWidth()),
		std::clamp(MousePos.y, 0.0f, HudHeight()));
	const float NewDiagonal = distance(FixedPoint, ClampedMouse);
	const int NewScale = std::clamp((int)std::lround(m_ResizeStartScale * (NewDiagonal / StartDiagonal)), 25, 300);
	const float Factor = (float)NewScale / (float)m_ResizeStartScale;

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
	NewRect = ClampToBounds(NewRect, HudWidth(), HudHeight());

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

	SResetAnim *pAnim = FindOrAddResetAnim(Module);
	if(!pAnim)
		return;

	pAnim->m_StartRect = GetModuleVisual(Module).m_Rect;
	pAnim->m_StartScale = HudLayout::Get(Module, HudWidth(), HudHeight()).m_Scale;
	pAnim->m_Kind = Kind;

	if(Kind == RESET_KIND_SCALE)
		HudLayout::ResetScale(Module);
	else if(Kind == RESET_KIND_POSITION)
		HudLayout::ResetPosition(Module);
	else
		HudLayout::ResetSettings(Module);

	pAnim->m_TargetRect = GetModuleVisual(Module).m_Rect;
	pAnim->m_TargetScale = HudLayout::Get(Module, HudWidth(), HudHeight()).m_Scale;

	HudLayout::SetScale(Module, pAnim->m_StartScale);
	ApplyDraggedPosition(Module, pAnim->m_StartRect);

	pAnim->m_Phase = 0.0f;
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

		const float Eased = EaseOutCubic(Anim.m_Phase);
		const CUIRect CurRect = LerpRect(Anim.m_StartRect, Anim.m_TargetRect, Eased);
		const int CurScale = (int)std::lround(mix((float)Anim.m_StartScale, (float)Anim.m_TargetScale, Eased));

		HudLayout::SetScale(Anim.m_Module, CurScale);
		if(Anim.m_Phase >= 1.0f && Anim.m_Kind != RESET_KIND_SCALE)
		{
			HudLayout::ResetPosition(Anim.m_Module);
			HudLayout::SetScale(Anim.m_Module, Anim.m_TargetScale);
		}
		else
		{
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

	const bool Enabled = HudLayout::IsEnabled(pThis->m_SelectedModule);
	const auto SelectedLayout = HudLayout::Get(pThis->m_SelectedModule, pThis->HudWidth(), pThis->HudHeight());
	CUIRect Title, ToggleButton, ScaleLabel, ScaleSlider, OpacityLabel, OpacitySlider, ResetScaleButton, ResetPositionButton, ResetAllButton;
	View.HSplitTop(16.0f, &Title, &View);
	pThis->Ui()->DoLabel(&Title, HudLayout::Name(pThis->m_SelectedModule), 10.0f, TEXTALIGN_MC);
	View.HSplitTop(4.0f, nullptr, &View);
	View.HSplitTop(16.0f, &ToggleButton, &View);
	if(pThis->GameClient()->m_Menus.DoButton_CheckBox(&pThis->m_ToggleModuleButton, Localize("Enabled"), Enabled ? 1 : 0, &ToggleButton))
		HudLayout::SetEnabled(pThis->m_SelectedModule, !Enabled);

	View.HSplitTop(4.0f, nullptr, &View);
	View.HSplitTop(12.0f, &ScaleLabel, &View);
	const int Scale = SelectedLayout.m_Scale;
	char aScale[32];
	str_format(aScale, sizeof(aScale), "%s %d%%", Localize("Scale"), Scale);
	pThis->Ui()->DoLabel(&ScaleLabel, aScale, 8.0f, TEXTALIGN_ML);

	View.HSplitTop(14.0f, &ScaleSlider, &View);
	const float Relative = CUi::ms_LinearScrollbarScale.ToRelative(Scale, 25, 300);
	const float NewRelative = pThis->Ui()->DoScrollbarH(&pThis->m_SelectedModule, &ScaleSlider, Relative);
	const int NewScale = CUi::ms_LinearScrollbarScale.ToAbsolute(NewRelative, 25, 300);
	if(NewScale != Scale)
		HudLayout::SetScale(pThis->m_SelectedModule, NewScale);

	View.HSplitTop(4.0f, nullptr, &View);
	View.HSplitTop(12.0f, &OpacityLabel, &View);
	const int Opacity = SelectedLayout.m_Alpha;
	char aOpacity[32];
	str_format(aOpacity, sizeof(aOpacity), "%s %d%%", Localize("Opacity"), Opacity);
	pThis->Ui()->DoLabel(&OpacityLabel, aOpacity, 8.0f, TEXTALIGN_ML);

	View.HSplitTop(14.0f, &OpacitySlider, &View);
	const float OpacityRelative = CUi::ms_LinearScrollbarScale.ToRelative(Opacity, 0, 100);
	const float NewOpacityRelative = pThis->Ui()->DoScrollbarH(&pThis->m_OpacitySlider, &OpacitySlider, OpacityRelative);
	const int NewOpacity = CUi::ms_LinearScrollbarScale.ToAbsolute(NewOpacityRelative, 0, 100);
	if(NewOpacity != Opacity)
		HudLayout::SetAlpha(pThis->m_SelectedModule, NewOpacity);

	View.HSplitTop(6.0f, nullptr, &View);
	View.HSplitTop(16.0f, &ResetScaleButton, &View);
	if(pThis->Ui()->DoButton_PopupMenu(&pThis->m_ResetScaleButton, Localize("Reset scale"), &ResetScaleButton, 8.0f, TEXTALIGN_MC))
		pThis->StartResetAnimation(pThis->m_SelectedModule, RESET_KIND_SCALE);

	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(16.0f, &ResetPositionButton, &View);
	if(pThis->Ui()->DoButton_PopupMenu(&pThis->m_ResetPositionButton, Localize("Reset position"), &ResetPositionButton, 8.0f, TEXTALIGN_MC))
		pThis->StartResetAnimation(pThis->m_SelectedModule, RESET_KIND_POSITION);

	View.HSplitTop(3.0f, nullptr, &View);
	View.HSplitTop(16.0f, &ResetAllButton, &View);
	const ColorRGBA ResetAllColor(1.0f, 0.32f, 0.32f, 0.85f * pThis->Ui()->ButtonColorMul(&pThis->m_ResetSettingsButton));
	if(pThis->Ui()->DoButton_PopupMenu(&pThis->m_ResetSettingsButton, Localize("Reset all"), &ResetAllButton, 8.0f, TEXTALIGN_MC, 0.0f, false, true, ResetAllColor))
		pThis->StartResetAnimation(pThis->m_SelectedModule, RESET_KIND_ALL);

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
	const float PopupHeight = SETTINGS_POPUP_HEIGHT;
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
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
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
		float StartAngle, EndAngle;
		CornerArcParams(Rect, Corner, RESIZE_HANDLE_RADIUS, Center, StartAngle, EndAngle);
		DrawHandleBracket(Graphics(), Center, StartAngle, EndAngle, InnerRadius - RESIZE_HANDLE_OUTLINE, OuterRadius + RESIZE_HANDLE_OUTLINE, Outline);
		DrawHandleBracket(Graphics(), Center, StartAngle, EndAngle, InnerRadius, OuterRadius, Color);
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
	char aName[64];
	str_format(aName, sizeof(aName), "%s", HudLayout::Name(Visual.m_Module));
	const char *pStatus = nullptr;
	if(Visual.m_Editable && !Visual.m_Enabled)
		pStatus = Localize("disabled");
	else if(!Visual.m_Editable)
		pStatus = Localize("preview");

	const float Width = HudWidth();
	const float Height = HudHeight();
	const float FontSize = 6.6f;
	const float StatusFontSize = 5.6f;
	const float NameWidth = TextRender()->TextWidth(FontSize, aName);
	const float StatusWidth = pStatus ? TextRender()->TextWidth(StatusFontSize, pStatus) + 6.0f : 0.0f;
	const float LabelW = 10.0f + NameWidth + StatusWidth;
	const float LabelH = 13.0f;
	constexpr float TailSize = 2.6f;
	const float X = std::clamp(Visual.m_Rect.x + (Visual.m_Rect.w - LabelW) * 0.5f, 2.0f, Width - LabelW - 2.0f);
	float Y = Visual.m_Rect.y - LabelH - TailSize - 2.0f;
	const bool PointingDown = Y >= 2.0f;
	if(!PointingDown)
		Y = std::min(Height - LabelH - TailSize - 2.0f, Visual.m_Rect.y + Visual.m_Rect.h + TailSize + 2.0f);

	CUIRect LabelRect = {X, Y, LabelW, LabelH};
	Graphics()->DrawRect(LabelRect.x - 0.6f, LabelRect.y - 0.6f, LabelRect.w + 1.2f, LabelRect.h + 1.2f, ColorRGBA(1.0f, 1.0f, 1.0f, 0.14f), IGraphics::CORNER_ALL, 6.0f);
	Graphics()->DrawRect(LabelRect.x, LabelRect.y, LabelRect.w, LabelRect.h, ColorRGBA(0.04f, 0.04f, 0.05f, 0.88f), IGraphics::CORNER_ALL, 5.5f);

	const float TailCenterX = std::clamp(Visual.m_Rect.x + Visual.m_Rect.w * 0.5f, LabelRect.x + TailSize * 2.0f, LabelRect.x + LabelRect.w - TailSize * 2.0f);
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
		LabelRect.VSplitRight(StatusWidth, &NameRect, &StatusRect);
	else
		NameRect = LabelRect;
	Ui()->DoLabel(&NameRect, aName, FontSize, TEXTALIGN_MC);
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
	Graphics()->MapScreenToSize(Width, Height);

	for(int Pass = 0; Pass < 2; ++Pass)
	{
		const bool RenderEditable = Pass == 1;
		for(int i = 0; i < m_FrameVisualCount; ++i)
		{
			if(m_aFrameVisuals[i].m_Editable != RenderEditable)
				continue;
			RenderModulePreview(m_aFrameVisuals[i]);
		}
	}

	for(int Pass = 0; Pass < 2; ++Pass)
	{
		const bool RenderEditable = Pass == 1;
		for(int i = 0; i < m_FrameVisualCount; ++i)
		{
			if(m_aFrameVisuals[i].m_Editable != RenderEditable)
				continue;
			const bool Hovered = m_aFrameVisuals[i].m_Module == m_HoveredModule;
			const bool Selected = m_aFrameVisuals[i].m_Module == m_SelectedModule || m_aFrameVisuals[i].m_Module == m_PressedModule;
			RenderModuleOutline(m_aFrameVisuals[i], Hovered, Selected);
			if(Hovered)
				RenderModuleLabel(m_aFrameVisuals[i]);
			if(m_aFrameVisuals[i].m_Editable && (Hovered || Selected))
				RenderResizeHandles(m_aFrameVisuals[i]);
		}
	}

	CUIRect ResetRect = {8.0f, 8.0f, 66.0f, 16.0f};
	const bool ResetHovered = PointInRect(MousePos, ResetRect);
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

	Ui()->StartCheck();
	Ui()->Update();
	Ui()->MapScreen();

	UpdateResetAnimation();
	RebuildFrameVisuals();

	const vec2 WindowSize(std::max(1.0f, (float)Graphics()->WindowWidth()), std::max(1.0f, (float)Graphics()->WindowHeight()));
	const vec2 UiMousePos = Ui()->UpdatedMousePos() * vec2(Ui()->Screen()->w, Ui()->Screen()->h) / WindowSize;
	const vec2 UiToHudScale(HudWidth() / std::max(Ui()->Screen()->w, 1.0f), HudHeight() / std::max(Ui()->Screen()->h, 1.0f));
	const vec2 MousePos = UiMousePos * UiToHudScale;
	const bool LeftDown = Input()->KeyIsPressed(KEY_MOUSE_1);
	const bool RightDown = Input()->KeyIsPressed(KEY_MOUSE_2);
	const bool LeftClicked = LeftDown && !m_MouseDownLast;
	const bool RightClicked = RightDown && !m_RightMouseDownLast;
	const bool PopupOpen = Ui()->IsPopupOpen(&m_SettingsPopupId);

	m_HoveredModule = HitTestModule(MousePos);
	int ResizeHitCorner = RESIZE_CORNER_NONE;
	const HudLayout::EModule ResizeHitModule = HitTestResizeHandle(MousePos, ResizeHitCorner);

	CUIRect ResetRect = {8.0f, 8.0f, 66.0f, 16.0f};
	const bool ResetHovered = PointInRect(MousePos, ResetRect);

	if(RightClicked && m_HoveredModule != HudLayout::MODULE_COUNT && IsEditableModule(m_HoveredModule))
	{
		m_SelectedModule = m_HoveredModule;
		if(const SModuleVisual *pVisual = FindFrameVisual(m_HoveredModule))
			OpenModuleSettings(*pVisual);
	}

	if(PopupOpen)
	{
		m_Dragging = false;
		m_PressedModule = HudLayout::MODULE_COUNT;
		m_ResizingModule = HudLayout::MODULE_COUNT;
		m_ResizeCorner = RESIZE_CORNER_NONE;
		m_PressedOnReset = false;
	}
	else if(LeftClicked && ResetHovered)
	{
		for(int i = 0; i < m_FrameVisualCount; ++i)
		{
			if(IsEditableModule(m_aFrameVisuals[i].m_Module))
				StartResetAnimation(m_aFrameVisuals[i].m_Module, RESET_KIND_ALL);
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
		m_ResizeStartScale = HudLayout::Get(ResizeHitModule, HudWidth(), HudHeight()).m_Scale;
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

	if(PopupOpen && !Ui()->IsPopupOpen(&m_SettingsPopupId) && !m_SuppressCloseAnim)
	{
		m_PopupClosing = true;
		m_PopupClosePhase = 1.0f;
	}
	m_SuppressCloseAnim = false;

	Ui()->FinishCheck();
	Ui()->ClearHotkeys();
}
