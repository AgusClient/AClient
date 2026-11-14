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
constexpr float SETTINGS_POPUP_WIDTH = 190.0f;
constexpr float SETTINGS_POPUP_HEIGHT = 150.0f;

constexpr int RESIZE_CORNER_NONE = -1;
constexpr int RESIZE_CORNER_TL = 0;
constexpr int RESIZE_CORNER_TR = 1;
constexpr int RESIZE_CORNER_BL = 2;
constexpr int RESIZE_CORNER_BR = 3;

constexpr float RESIZE_HANDLE_RADIUS = 4.2f;
constexpr float RESIZE_HANDLE_HIT_RADIUS = 6.0f;
constexpr float MIN_RESIZE_DIAGONAL = 12.0f;

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

bool PointInRect(vec2 P, const CUIRect &R)
{
	return P.x >= R.x && P.x <= R.x + R.w && P.y >= R.y && P.y <= R.y + R.h;
}

CUIRect ClampToBounds(const CUIRect &Rect, float BoundsW, float BoundsH)
{
	CUIRect Out = Rect;
	Out.x = std::clamp(Out.x, 0.0f, std::max(0.0f, BoundsW - Out.w));
	Out.y = std::clamp(Out.y, 0.0f, std::max(0.0f, BoundsH - Out.h));
	return Out;
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

	constexpr int Segments = 6;
	IGraphics::CLineItem aLines[Segments * 4 + 4];
	int NumLines = 0;

	auto AddLine = [&](vec2 From, vec2 To) {
		aLines[NumLines++] = IGraphics::CLineItem(From, To);
	};

	auto AddArc = [&](vec2 Center, float StartAngle, float EndAngle) {
		vec2 Prev = vec2(Center.x + std::cos(StartAngle) * Radius, Center.y + std::sin(StartAngle) * Radius);
		for(int i = 1; i <= Segments; ++i)
		{
			const float T = i / (float)Segments;
			const float Angle = mix(StartAngle, EndAngle, T);
			const vec2 Cur(Center.x + std::cos(Angle) * Radius, Center.y + std::sin(Angle) * Radius);
			AddLine(Prev, Cur);
			Prev = Cur;
		}
	};

	// 4 esquinas
	AddArc(vec2(Rect.x + Radius, Rect.y + Radius), pi, pi * 1.5f);
	AddLine(vec2(Rect.x + Radius, Rect.y), vec2(Rect.x + Rect.w - Radius, Rect.y));
	AddArc(vec2(Rect.x + Rect.w - Radius, Rect.y + Radius), pi * 1.5f, pi * 2.0f);
	AddLine(vec2(Rect.x + Rect.w, Rect.y + Radius), vec2(Rect.x + Rect.w, Rect.y + Rect.h - Radius));
	AddArc(vec2(Rect.x + Rect.w - Radius, Rect.y + Rect.h - Radius), 0.0f, pi * 0.5f);
	AddLine(vec2(Rect.x + Rect.w - Radius, Rect.y + Rect.h), vec2(Rect.x + Radius, Rect.y + Rect.h));
	AddArc(vec2(Rect.x + Radius, Rect.y + Rect.h - Radius), pi * 0.5f, pi);
	AddLine(vec2(Rect.x, Rect.y + Rect.h - Radius), vec2(Rect.x, Rect.y + Radius));

	pGraphics->TextureClear();
	pGraphics->SetColor(Color);
	pGraphics->LinesBegin();
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
	Ui()->SetEnabled(true);
	Ui()->ClearHotkeys();

	m_Active = true;
	m_MouseDownLast = Input()->KeyIsPressed(KEY_MOUSE_1);
	m_RightMouseDownLast = Input()->KeyIsPressed(KEY_MOUSE_2);
	m_Dragging = false;
	m_PressedModule = HudLayout::MODULE_COUNT;
	m_HoveredModule = HudLayout::MODULE_COUNT;
	m_SelectedModule = HudLayout::MODULE_COUNT;
	m_ResizingModule = HudLayout::MODULE_COUNT;
	m_ResizeCorner = RESIZE_CORNER_NONE;
	m_PressedOnReset = false;
	Ui()->ClosePopupMenus();
}

void CHudEditor::Deactivate()
{
	m_Active = false;
	m_MouseDownLast = false;
	m_RightMouseDownLast = false;
	m_Dragging = false;
	m_PressedModule = HudLayout::MODULE_COUNT;
	m_HoveredModule = HudLayout::MODULE_COUNT;
	m_SelectedModule = HudLayout::MODULE_COUNT;
	m_ResizingModule = HudLayout::MODULE_COUNT;
	m_ResizeCorner = RESIZE_CORNER_NONE;
	m_PressedOnReset = false;
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
	const float S = Layout.m_Scale / 100.0f;

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
		Visual.m_Rounding = 4.0f;
		break;
	case HudLayout::MODULE_MINI_STATBOARD:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 130.0f * S, 55.0f * S};
		Visual.m_Rounding = 5.0f;
		break;
	case HudLayout::MODULE_KEYSTROKES:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 59.0f * S, 72.0f * S};
		Visual.m_Rounding = 4.0f;
		break;
	case HudLayout::MODULE_SPEEDOMETER:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 75.0f * S, 18.0f * S};
		Visual.m_Rounding = 4.0f;
		break;
	case HudLayout::MODULE_SPECTATOR_COUNT:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 32.0f * S, 14.0f * S};
		Visual.m_Rounding = 3.0f;
		break;
	case HudLayout::MODULE_DUMMY_ACTIONS:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 16.0f * S, 29.0f * S};
		Visual.m_Rounding = 3.0f;
		break;
	case HudLayout::MODULE_MOVEMENT_INFO:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 62.0f * S, 50.0f * S};
		Visual.m_Rounding = 4.0f;
		break;
	case HudLayout::MODULE_GAME_TIMER:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 50.0f * S, 14.0f * S};
		Visual.m_Rounding = 3.0f;
		break;
	case HudLayout::MODULE_LOCAL_TIME:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 38.0f * S, 12.5f * S};
		Visual.m_Rounding = 3.0f;
		break;
	case HudLayout::MODULE_FROZEN_HUD:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 78.0f * S, 18.0f * S};
		Visual.m_Rounding = 4.0f;
		break;
	default:
		Visual.m_Rect = CUIRect{Layout.m_X, Layout.m_Y, 40.0f * S, 20.0f * S};
		break;
	}

	return Visual;
}

void CHudEditor::CollectModuleVisuals(SModuleVisual *pOut, int &Count) const
{
	Count = 0;
	for(int i = 0; i < HudLayout::MODULE_COUNT && Count < MAX_MODULE_VISUALS; ++i)
	{
		pOut[Count++] = GetModuleVisual((HudLayout::EModule)i);
	}
}

HudLayout::EModule CHudEditor::HitTestModule(vec2 MousePos) const
{
	SModuleVisual aVisuals[MAX_MODULE_VISUALS];
	int Count = 0;
	CollectModuleVisuals(aVisuals, Count);

	for(int i = Count - 1; i >= 0; --i)
	{
		if(PointInRect(MousePos, aVisuals[i].m_Rect))
			return aVisuals[i].m_Module;
	}
	return HudLayout::MODULE_COUNT;
}

HudLayout::EModule CHudEditor::HitTestResizeHandle(vec2 MousePos, int &OutCorner) const
{
	OutCorner = RESIZE_CORNER_NONE;
	if(m_SelectedModule == HudLayout::MODULE_COUNT)
		return HudLayout::MODULE_COUNT;

	const SModuleVisual Visual = GetModuleVisual(m_SelectedModule);
	const int aCorners[4] = {RESIZE_CORNER_TL, RESIZE_CORNER_TR, RESIZE_CORNER_BL, RESIZE_CORNER_BR};
	for(int c : aCorners)
	{
		vec2 P = CornerPoint(Visual.m_Rect, c);
		if(distance(MousePos, P) <= RESIZE_HANDLE_HIT_RADIUS)
		{
			OutCorner = c;
			return m_SelectedModule;
		}
	}
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
	SModuleVisual aVisuals[MAX_MODULE_VISUALS];
	int Count = 0;
	CollectModuleVisuals(aVisuals, Count);

	float BestDeltaX = 999.0f;
	float BestDeltaY = 999.0f;

	auto TrySnap = [](float Candidate, float Target, float &BestDelta) {
		const float Delta = Target - Candidate;
		if(absolute(Delta) <= SNAP_THRESHOLD && absolute(Delta) < absolute(BestDelta))
			BestDelta = Delta;
	};

	// Snap a bordes de pantalla
	TrySnap(Result.x, 0.0f, BestDeltaX);
	TrySnap(Result.x + Result.w, HudWidth(), BestDeltaX);
	TrySnap(Result.y, 0.0f, BestDeltaY);
	TrySnap(Result.y + Result.h, HudHeight(), BestDeltaY);

	// Snap con otros módulos
	for(int i = 0; i < Count; ++i)
	{
		if(aVisuals[i].m_Module == DraggedModule)
			continue;
		const CUIRect &Other = aVisuals[i].m_Rect;
		TrySnap(Result.x, Other.x, BestDeltaX);
		TrySnap(Result.x, Other.x + Other.w, BestDeltaX);
		TrySnap(Result.x + Result.w, Other.x, BestDeltaX);
		TrySnap(Result.x + Result.w, Other.x + Other.w, BestDeltaX);

		TrySnap(Result.y, Other.y, BestDeltaY);
		TrySnap(Result.y, Other.y + Other.h, BestDeltaY);
		TrySnap(Result.y + Result.h, Other.y, BestDeltaY);
		TrySnap(Result.y + Result.h, Other.y + Other.h, BestDeltaY);
	}

	if(absolute(BestDeltaX) <= SNAP_THRESHOLD)
		Result.x += BestDeltaX;
	if(absolute(BestDeltaY) <= SNAP_THRESHOLD)
		Result.y += BestDeltaY;

	return ClampToBounds(Result, HudWidth(), HudHeight());
}

void CHudEditor::UpdateDragging(vec2 MousePos)
{
	if(!m_Dragging || m_PressedModule == HudLayout::MODULE_COUNT || !IsEditableModule(m_PressedModule))
		return;

	SModuleVisual Visual = GetModuleVisual(m_PressedModule);
	CUIRect NewRect = Visual.m_Rect;
	NewRect.x = MousePos.x - m_DragMouseOffset.x;
	NewRect.y = MousePos.y - m_DragMouseOffset.y;

	if(!Input()->ShiftIsPressed())
		NewRect = SnapRect(NewRect, m_PressedModule);
	else
		NewRect = ClampToBounds(NewRect, HudWidth(), HudHeight());

	ApplyDraggedPosition(m_PressedModule, NewRect);
}

void CHudEditor::UpdateResizing(vec2 MousePos)
{
	if(m_ResizingModule == HudLayout::MODULE_COUNT || m_ResizeCorner == RESIZE_CORNER_NONE)
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

	const vec2 ClampedMouse(std::clamp(MousePos.x, 0.0f, HudWidth()), std::clamp(MousePos.y, 0.0f, HudHeight()));
	const float NewDiagonal = distance(FixedPoint, ClampedMouse);
	const int NewScale = std::clamp((int)std::lround(m_ResizeStartScale * (NewDiagonal / StartDiagonal)), 25, 300);

	HudLayout::SetScale(m_ResizingModule, NewScale);
}

void CHudEditor::OpenModuleSettings(const SModuleVisual &Visual)
{
	const float Width = HudWidth();
	const float Height = HudHeight();
	CUIRect PopupRect;
	PopupRect.w = SETTINGS_POPUP_WIDTH;
	PopupRect.h = SETTINGS_POPUP_HEIGHT;
	PopupRect.x = std::clamp(Visual.m_Rect.x + Visual.m_Rect.w + 6.0f, 10.0f, Width - PopupRect.w - 10.0f);
	PopupRect.y = std::clamp(Visual.m_Rect.y, 10.0f, Height - PopupRect.h - 10.0f);

	Ui()->DoPopupMenu(&m_SettingsPopupId, PopupRect.x, PopupRect.y, PopupRect.w, PopupRect.h, this, PopupModuleSettings);
}

CUi::EPopupMenuFunctionResult CHudEditor::PopupModuleSettings(void *pContext, CUIRect View, bool Active)
{
	(void)Active;
	CHudEditor *pThis = static_cast<CHudEditor *>(pContext);
	if(pThis->m_SelectedModule == HudLayout::MODULE_COUNT)
		return CUi::POPUP_CLOSE_CURRENT;

	const bool Enabled = HudLayout::IsEnabled(pThis->m_SelectedModule);
	const auto SelectedLayout = HudLayout::Get(pThis->m_SelectedModule, pThis->HudWidth(), pThis->HudHeight());

	CUIRect Title, ToggleButton, ScaleLabel, ScaleSlider, OpacityLabel, OpacitySlider, ResetScaleButton, ResetPositionButton;
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
	str_format(aScale, sizeof(aScale), "%s: %d%%", Localize("Scale"), Scale);
	pThis->Ui()->DoLabel(&ScaleLabel, aScale, 8.0f, TEXTALIGN_ML);

	View.HSplitTop(14.0f, &ScaleSlider, &View);
	const float RelScale = CUi::ms_LinearScrollbarScale.ToRelative(Scale, 25, 300);
	const float NewRelScale = pThis->Ui()->DoScrollbarH(&pThis->m_SelectedModule, &ScaleSlider, RelScale);
	const int NewScale = CUi::ms_LinearScrollbarScale.ToAbsolute(NewRelScale, 25, 300);
	if(NewScale != Scale)
		HudLayout::SetScale(pThis->m_SelectedModule, NewScale);

	View.HSplitTop(4.0f, nullptr, &View);
	View.HSplitTop(12.0f, &OpacityLabel, &View);
	const int Opacity = SelectedLayout.m_Alpha;
	char aOpacity[32];
	str_format(aOpacity, sizeof(aOpacity), "%s: %d%%", Localize("Opacity"), Opacity);
	pThis->Ui()->DoLabel(&OpacityLabel, aOpacity, 8.0f, TEXTALIGN_ML);

	View.HSplitTop(14.0f, &OpacitySlider, &View);
	const float RelOp = CUi::ms_LinearScrollbarScale.ToRelative(Opacity, 10, 100);
	const float NewRelOp = pThis->Ui()->DoScrollbarH(&pThis->m_OpacitySlider, &OpacitySlider, RelOp);
	const int NewOp = CUi::ms_LinearScrollbarScale.ToAbsolute(NewRelOp, 10, 100);
	if(NewOp != Opacity)
		HudLayout::SetAlpha(pThis->m_SelectedModule, NewOp);

	View.HSplitTop(6.0f, nullptr, &View);
	CUIRect ButtonRow;
	View.HSplitTop(16.0f, &ButtonRow, &View);
	ButtonRow.VSplitMid(&ResetPositionButton, &ResetScaleButton, 4.0f);

	if(pThis->GameClient()->m_Menus.DoButton_Menu(&pThis->m_ResetPositionButton, Localize("Reset Pos"), 0, &ResetPositionButton))
		HudLayout::ResetPosition(pThis->m_SelectedModule);

	if(pThis->GameClient()->m_Menus.DoButton_Menu(&pThis->m_ResetScaleButton, Localize("Reset Scale"), 0, &ResetScaleButton))
		HudLayout::ResetScale(pThis->m_SelectedModule);

	return CUi::POPUP_KEEP_OPEN;
}

void CHudEditor::RenderModuleOutline(const SModuleVisual &Visual, bool Hovered, bool Selected) const
{
	const CUIRect &Rect = Visual.m_Rect;
	ColorRGBA Color = Selected ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.92f) : (Hovered ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.78f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.46f));
	if(!Visual.m_Editable)
		Color.a *= 0.6f;
	if(!Visual.m_Enabled)
		Color.a *= 0.6f;

	DrawRoundedRectOutline(Graphics(), Rect, Visual.m_Corners, Visual.m_Rounding, Color);
}

void CHudEditor::RenderResizeHandles(const SModuleVisual &Visual) const
{
	const CUIRect &Rect = Visual.m_Rect;
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;

	const int aCorners[4] = {RESIZE_CORNER_TL, RESIZE_CORNER_TR, RESIZE_CORNER_BL, RESIZE_CORNER_BR};
	Graphics()->TextureClear();
	for(int c : aCorners)
	{
		vec2 P = CornerPoint(Rect, c);
		Graphics()->DrawRect(P.x - RESIZE_HANDLE_RADIUS, P.y - RESIZE_HANDLE_RADIUS, RESIZE_HANDLE_RADIUS * 2.0f, RESIZE_HANDLE_RADIUS * 2.0f, ColorRGBA(1.0f, 0.85f, 0.35f, 0.95f), IGraphics::CORNER_ALL, 2.0f);
	}
}

void CHudEditor::RenderModuleLabel(const SModuleVisual &Visual) const
{
	const CUIRect &Rect = Visual.m_Rect;
	CUIRect LabelRect = {Rect.x, Rect.y - 10.0f, Rect.w, 10.0f};
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.9f);
	Ui()->DoLabel(&LabelRect, HudLayout::Name(Visual.m_Module), 6.5f, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}

void CHudEditor::RenderOverlay(vec2 MousePos)
{
	const float Width = HudWidth();
	const float Height = HudHeight();
	Graphics()->MapScreenToSize(Width, Height);
	Graphics()->TextureClear();
	Graphics()->DrawRect(0.0f, 0.0f, Width, Height, ColorRGBA(0.0f, 0.0f, 0.0f, 0.38f), IGraphics::CORNER_ALL, 0.0f);

	SModuleVisual aVisuals[MAX_MODULE_VISUALS];
	int Count = 0;
	CollectModuleVisuals(aVisuals, Count);

	for(int i = 0; i < Count; ++i)
	{
		const bool Hovered = aVisuals[i].m_Module == m_HoveredModule;
		const bool Selected = aVisuals[i].m_Module == m_SelectedModule || aVisuals[i].m_Module == m_PressedModule;
		RenderModuleOutline(aVisuals[i], Hovered, Selected);
		if(Hovered)
			RenderModuleLabel(aVisuals[i]);
		if(aVisuals[i].m_Editable && (Hovered || Selected))
			RenderResizeHandles(aVisuals[i]);
	}

	// Botón Reset All
	CUIRect ResetRect = {8.0f, 8.0f, 66.0f, 16.0f};
	const bool ResetHovered = PointInRect(MousePos, ResetRect);
	const ColorRGBA ResetColor = m_PressedOnReset ? ColorRGBA(0.95f, 0.48f, 0.48f, 0.90f) :
							(ResetHovered ? ColorRGBA(0.95f, 0.48f, 0.48f, 0.55f) : ColorRGBA(0.95f, 0.48f, 0.48f, 0.36f));
	Graphics()->DrawRect(ResetRect.x, ResetRect.y, ResetRect.w, ResetRect.h, ResetColor, IGraphics::CORNER_ALL, 4.0f);
	Ui()->DoLabel(&ResetRect, Localize("Reset All"), 6.5f, TEXTALIGN_MC);

	Ui()->MapScreen();
	Ui()->RenderPopupMenus();
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
	HitTestResizeHandle(MousePos, ResizeHitCorner);

	CUIRect ResetRect = {8.0f, 8.0f, 66.0f, 16.0f};
	const bool ResetHovered = PointInRect(MousePos, ResetRect);

	if(RightClicked && m_HoveredModule != HudLayout::MODULE_COUNT && IsEditableModule(m_HoveredModule))
	{
		m_SelectedModule = m_HoveredModule;
		OpenModuleSettings(GetModuleVisual(m_HoveredModule));
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
		HudLayout::ResetEditableModules();
		m_Dragging = false;
		m_PressedModule = HudLayout::MODULE_COUNT;
		m_ResizingModule = HudLayout::MODULE_COUNT;
		m_ResizeCorner = RESIZE_CORNER_NONE;
		m_PressedOnReset = true;
	}
	else if(LeftClicked)
	{
		if(ResizeHitCorner != RESIZE_CORNER_NONE)
		{
			m_ResizingModule = m_SelectedModule;
			m_ResizeCorner = ResizeHitCorner;
			m_ResizeAnchorRect = GetModuleVisual(m_SelectedModule).m_Rect;
			const auto Layout = HudLayout::Get(m_SelectedModule, HudWidth(), HudHeight());
			m_ResizeStartScale = Layout.m_Scale;
		}
		else if(m_HoveredModule != HudLayout::MODULE_COUNT && IsEditableModule(m_HoveredModule))
		{
			m_PressedModule = m_HoveredModule;
			m_SelectedModule = m_HoveredModule;
			m_Dragging = true;
			const SModuleVisual Visual = GetModuleVisual(m_HoveredModule);
			m_DragMouseOffset = MousePos - vec2(Visual.m_Rect.x, Visual.m_Rect.y);
		}
		else
		{
			m_SelectedModule = HudLayout::MODULE_COUNT;
		}
	}

	if(LeftDown)
	{
		if(m_Dragging)
			UpdateDragging(MousePos);
		else if(m_ResizingModule != HudLayout::MODULE_COUNT)
			UpdateResizing(MousePos);
	}
	else
	{
		m_Dragging = false;
		m_PressedModule = HudLayout::MODULE_COUNT;
		m_ResizingModule = HudLayout::MODULE_COUNT;
		m_ResizeCorner = RESIZE_CORNER_NONE;
		m_PressedOnReset = false;
	}

	RenderOverlay(MousePos);

	m_MouseDownLast = LeftDown;
	m_RightMouseDownLast = RightDown;
	Ui()->FinishCheck();
}
