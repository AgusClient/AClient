#include "hud_editor.h"

#include <base/color.h>
#include <base/math.h>
#include <base/str.h>

#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/localization.h>

#include <algorithm>
#include <cmath>

void CHudEditor::OnConsoleInit()
{
	Console()->Register("hud_edit", "", CFGFLAG_CLIENT, ConToggleHudEdit, this, "Toggle Drag-and-Drop HUD Editor");
	HudLayout::OnConsoleInit(Console(), ConfigManager());
}

void CHudEditor::ConToggleHudEdit(IConsole::IResult *pResult, void *pUserData)
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

	m_Active = true;
	m_MouseDownLast = false;
	m_RightMouseDownLast = false;
	m_Dragging = false;
	m_PressedModule = HudLayout::MODULE_COUNT;
	m_HoveredModule = HudLayout::MODULE_COUNT;
	m_SelectedModule = HudLayout::MODULE_COUNT;
	m_ShowPopup = false;
}

void CHudEditor::Deactivate()
{
	m_Active = false;
	m_Dragging = false;
	m_ShowPopup = false;
	m_PressedModule = HudLayout::MODULE_COUNT;
	m_HoveredModule = HudLayout::MODULE_COUNT;
	m_SelectedModule = HudLayout::MODULE_COUNT;

	if(ConfigManager())
		ConfigManager()->Save();
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

	if((Event.m_Flags & IInput::FLAG_PRESS) != 0)
	{
		if(Event.m_Key == KEY_ESCAPE)
		{
			if(m_ShowPopup)
			{
				m_ShowPopup = false;
				return true;
			}
			Deactivate();
			return true;
		}

		if(Event.m_Key == KEY_MOUSE_WHEEL_UP || Event.m_Key == KEY_MOUSE_WHEEL_DOWN)
		{
			vec2 MousePos = Ui()->MousePos();
			HudLayout::EModule Hovered = HitTestModule(MousePos);
			if(Hovered < HudLayout::MODULE_COUNT)
			{
				auto Layout = HudLayout::Get(Hovered, HudWidth(), HudHeight());
				int Delta = (Event.m_Key == KEY_MOUSE_WHEEL_UP) ? 5 : -5;
				HudLayout::SetScale(Hovered, Layout.m_Scale + Delta);
				return true;
			}
		}
	}

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

CHudEditor::SModuleVisual CHudEditor::GetModuleVisual(HudLayout::EModule Module) const
{
	SModuleVisual Visual;
	Visual.m_Module = Module;
	Visual.m_Editable = HudLayout::IsEditorModule(Module);
	Visual.m_Enabled = HudLayout::IsEnabled(Module);

	const float Width = HudWidth();
	const float Height = HudHeight();
	const auto Layout = HudLayout::Get(Module, Width, Height);
	const float Scale = std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f);

	float BoxW = 40.0f * Scale;
	float BoxH = 16.0f * Scale;

	switch(Module)
	{
	case HudLayout::MODULE_FPS:
		BoxW = 34.0f * Scale;
		BoxH = 14.0f * Scale;
		break;
	case HudLayout::MODULE_PING:
		BoxW = 34.0f * Scale;
		BoxH = 14.0f * Scale;
		break;
	case HudLayout::MODULE_FRAMETIME:
		BoxW = 34.0f * Scale;
		BoxH = 14.0f * Scale;
		break;
	case HudLayout::MODULE_KEYSTROKES:
		BoxW = 64.0f * Scale;
		BoxH = 64.0f * Scale;
		break;
	case HudLayout::MODULE_SPEEDOMETER:
		BoxW = 75.0f * Scale;
		BoxH = 18.0f * Scale;
		break;
	case HudLayout::MODULE_MUSIC_PLAYER:
		BoxW = 210.0f * Scale;
		BoxH = 38.0f * Scale;
		break;
	case HudLayout::MODULE_FROZEN_HUD:
		BoxW = 100.0f * Scale;
		BoxH = 22.0f * Scale;
		break;
	case HudLayout::MODULE_CHAT:
		BoxW = 180.0f * Scale;
		BoxH = 80.0f * Scale;
		break;
	case HudLayout::MODULE_VOTES:
		BoxW = 110.0f * Scale;
		BoxH = 46.0f * Scale;
		break;
	case HudLayout::MODULE_SCORE:
		BoxW = 60.0f * Scale;
		BoxH = 18.0f * Scale;
		break;
	case HudLayout::MODULE_NOTIFY_LAST:
		BoxW = 120.0f * Scale;
		BoxH = 16.0f * Scale;
		break;
	default:
		break;
	}

	Visual.m_Rect = CUIRect(Layout.m_X, Layout.m_Y, BoxW, BoxH);
	Visual.m_Rounding = 4.0f * Scale;
	return Visual;
}

void CHudEditor::CollectModuleVisuals(SModuleVisual *pOut, int &Count) const
{
	Count = 0;
	for(int i = 0; i < HudLayout::MODULE_COUNT; ++i)
	{
		if(Count < MAX_MODULE_VISUALS)
		{
			pOut[Count++] = GetModuleVisual((HudLayout::EModule)i);
		}
	}
}

HudLayout::EModule CHudEditor::HitTestModule(vec2 MousePos) const
{
	SModuleVisual aVisuals[MAX_MODULE_VISUALS];
	int Count = 0;
	CollectModuleVisuals(aVisuals, Count);

	for(int i = Count - 1; i >= 0; --i)
	{
		if(aVisuals[i].m_Rect.Inside(MousePos))
			return aVisuals[i].m_Module;
	}
	return HudLayout::MODULE_COUNT;
}

void CHudEditor::UpdateDragging(vec2 MousePos)
{
	if(!m_Dragging || m_PressedModule >= HudLayout::MODULE_COUNT)
		return;

	const float Width = HudWidth();
	const float Height = HudHeight();
	SModuleVisual Visual = GetModuleVisual(m_PressedModule);

	float NewX = MousePos.x - m_DragMouseOffset.x;
	float NewY = MousePos.y - m_DragMouseOffset.y;

	// Clamp to screen bounds
	NewX = std::clamp(NewX, 0.0f, std::max(0.0f, Width - Visual.m_Rect.w));
	NewY = std::clamp(NewY, 0.0f, std::max(0.0f, Height - Visual.m_Rect.h));

	// Snap to edges (within 6 units)
	constexpr float SnapDist = 6.0f;
	if(NewX < SnapDist)
		NewX = 0.0f;
	else if(Width - (NewX + Visual.m_Rect.w) < SnapDist)
		NewX = Width - Visual.m_Rect.w;

	if(NewY < SnapDist)
		NewY = 0.0f;
	else if(Height - (NewY + Visual.m_Rect.h) < SnapDist)
		NewY = Height - Visual.m_Rect.h;

	HudLayout::SetPosition(m_PressedModule, NewX, NewY);
}

void CHudEditor::OnRender()
{
	if(!m_Active)
		return;

	const float Width = HudWidth();
	const float Height = HudHeight();
	Graphics()->MapScreenToSize(Width, Height);

	vec2 MousePos = Ui()->MousePos();
	bool MouseDown = (Ui()->MouseButton(0) != 0);
	bool RightMouseDown = (Ui()->MouseButton(1) != 0);

	m_HoveredModule = HitTestModule(MousePos);

	// Handle Mouse Left Click / Dragging
	if(MouseDown && !m_MouseDownLast)
	{
		if(m_ShowPopup)
		{
			CUIRect PopupRect(m_PopupPos.x, m_PopupPos.y, 160.0f, 130.0f);
			if(!PopupRect.Inside(MousePos))
				m_ShowPopup = false;
		}
		else
		{
			if(m_HoveredModule < HudLayout::MODULE_COUNT)
			{
				m_Dragging = true;
				m_PressedModule = m_HoveredModule;
				m_SelectedModule = m_HoveredModule;
				SModuleVisual Visual = GetModuleVisual(m_PressedModule);
				m_DragMouseOffset = MousePos - vec2(Visual.m_Rect.x, Visual.m_Rect.y);
			}
		}
	}
	else if(!MouseDown && m_MouseDownLast)
	{
		m_Dragging = false;
		m_PressedModule = HudLayout::MODULE_COUNT;
	}

	if(m_Dragging)
	{
		UpdateDragging(MousePos);
	}

	// Handle Mouse Right Click for popup
	if(RightMouseDown && !m_RightMouseDownLast && !m_Dragging)
	{
		if(m_HoveredModule < HudLayout::MODULE_COUNT)
		{
			m_ShowPopup = true;
			m_PopupModule = m_HoveredModule;
			m_PopupPos = MousePos;
			if(m_PopupPos.x + 160.0f > Width)
				m_PopupPos.x = Width - 165.0f;
			if(m_PopupPos.y + 130.0f > Height)
				m_PopupPos.y = Height - 135.0f;
		}
		else
		{
			m_ShowPopup = false;
		}
	}

	m_MouseDownLast = MouseDown;
	m_RightMouseDownLast = RightMouseDown;

	RenderOverlay(MousePos);

	if(m_ShowPopup)
		RenderPopupMenu();

	// Render Cursor
	Graphics()->TextureClear();
	RenderTools()->RenderCursor(Ui()->MousePos(), 24.0f);
}

void CHudEditor::RenderModuleOutline(const SModuleVisual &Visual, bool Hovered, bool Selected) const
{
	CUIRect Rect = Visual.m_Rect;
	ColorRGBA BgColor;
	ColorRGBA BorderColor;

	if(Selected)
	{
		BgColor = ColorRGBA(0.0f, 0.65f, 1.0f, 0.25f);
		BorderColor = ColorRGBA(0.0f, 0.85f, 1.0f, 0.95f);
	}
	else if(Hovered)
	{
		BgColor = ColorRGBA(1.0f, 0.85f, 0.2f, 0.20f);
		BorderColor = ColorRGBA(1.0f, 0.85f, 0.2f, 0.90f);
	}
	else if(Visual.m_Enabled)
	{
		BgColor = ColorRGBA(0.1f, 0.5f, 0.8f, 0.10f);
		BorderColor = ColorRGBA(0.2f, 0.7f, 1.0f, 0.55f);
	}
	else
	{
		BgColor = ColorRGBA(0.3f, 0.3f, 0.3f, 0.10f);
		BorderColor = ColorRGBA(0.6f, 0.6f, 0.6f, 0.35f);
	}

	Rect.Draw(BgColor, Visual.m_Corners, Visual.m_Rounding);

	// Draw border
	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	Graphics()->SetColor(BorderColor.r, BorderColor.g, BorderColor.b, BorderColor.a);
	IGraphics::CLineItem aLines[4] = {
		IGraphics::CLineItem(Rect.x, Rect.y, Rect.x + Rect.w, Rect.y),
		IGraphics::CLineItem(Rect.x + Rect.w, Rect.y, Rect.x + Rect.w, Rect.y + Rect.h),
		IGraphics::CLineItem(Rect.x + Rect.w, Rect.y + Rect.h, Rect.x, Rect.y + Rect.h),
		IGraphics::CLineItem(Rect.x, Rect.y + Rect.h, Rect.x, Rect.y),
	};
	Graphics()->LinesDraw(aLines, 4);
	Graphics()->LinesEnd();

	// Label
	char aLabel[64];
	auto Layout = HudLayout::Get(Visual.m_Module, HudWidth(), HudHeight());
	str_format(aLabel, sizeof(aLabel), "%s (%d%%)", HudLayout::Name(Visual.m_Module), Layout.m_Scale);

	const float FontSize = std::clamp(Rect.h * 0.45f, 6.0f, 10.0f);
	TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, Hovered ? 1.0f : 0.8f));
	Ui()->DoLabel(&Rect, aLabel, FontSize, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}

void CHudEditor::RenderOverlay(vec2 MousePos)
{
	const float Width = HudWidth();
	const float Height = HudHeight();

	// Semi-transparent background overlay
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.20f);
	IGraphics::CQuadItem DimQuad(0.0f, 0.0f, Width, Height);
	Graphics()->QuadsDrawTL(&DimQuad, 1);
	Graphics()->QuadsEnd();

	// Top control bar
	CUIRect TopBar(0.0f, 0.0f, Width, 22.0f);
	TopBar.Draw(ColorRGBA(0.06f, 0.08f, 0.12f, 0.88f), IGraphics::CORNER_B, 4.0f);

	CUIRect TitleRect, ButtonRect;
	TopBar.VSplitRight(90.0f, &TitleRect, &ButtonRect);
	TitleRect.Margin(3.0f, &TitleRect);
	ButtonRect.Margin(2.0f, &ButtonRect);

	TextRender()->TextColor(ColorRGBA(0.0f, 0.8f, 1.0f, 1.0f));
	Ui()->DoLabel(&TitleRect, "AClient HUD Editor  |  Arrastra para mover  |  Rueda del raton: escalar  |  Clic derecho: opciones", 8.0f, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	if(GameClient()->m_Menus.DoButton_Menu(&m_CloseEditorButton, "Guardar y Salir", 0, &ButtonRect))
	{
		Deactivate();
		return;
	}

	// Render all modules
	SModuleVisual aVisuals[MAX_MODULE_VISUALS];
	int Count = 0;
	CollectModuleVisuals(aVisuals, Count);

	for(int i = 0; i < Count; ++i)
	{
		bool Hovered = (aVisuals[i].m_Module == m_HoveredModule);
		bool Selected = (aVisuals[i].m_Module == m_SelectedModule || aVisuals[i].m_Module == m_PressedModule);
		RenderModuleOutline(aVisuals[i], Hovered, Selected);
	}
}

void CHudEditor::RenderPopupMenu()
{
	if(!m_ShowPopup || m_PopupModule >= HudLayout::MODULE_COUNT)
		return;

	CUIRect PopupRect(m_PopupPos.x, m_PopupPos.y, 160.0f, 126.0f);
	PopupRect.Draw(ColorRGBA(0.08f, 0.10f, 0.15f, 0.95f), IGraphics::CORNER_ALL, 6.0f);

	CUIRect Header, Content;
	PopupRect.HSplitTop(22.0f, &Header, &Content);
	Header.Draw(ColorRGBA(0.0f, 0.45f, 0.75f, 0.50f), IGraphics::CORNER_T, 6.0f);

	char aTitle[64];
	str_format(aTitle, sizeof(aTitle), "%s", HudLayout::Name(m_PopupModule));
	TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	Ui()->DoLabel(&Header, aTitle, 9.0f, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	Content.Margin(4.0f, &Content);

	CUIRect Row;
	// Row 1: Toggle Visibility
	Content.HSplitTop(22.0f, &Row, &Content);
	bool Enabled = HudLayout::IsEnabled(m_PopupModule);
	char aToggleLabel[32];
	str_format(aToggleLabel, sizeof(aToggleLabel), "Visible: %s", Enabled ? "SI" : "NO");
	if(GameClient()->m_Menus.DoButton_Menu(&m_ToggleModuleButton, aToggleLabel, 0, &Row))
	{
		HudLayout::SetEnabled(m_PopupModule, !Enabled);
	}

	Content.HSplitTop(4.0f, nullptr, &Content);

	// Row 2: Reset Position
	Content.HSplitTop(22.0f, &Row, &Content);
	if(GameClient()->m_Menus.DoButton_Menu(&m_ResetPositionButton, "Restablecer Posicion", 0, &Row))
	{
		HudLayout::ResetPosition(m_PopupModule);
	}

	Content.HSplitTop(4.0f, nullptr, &Content);

	// Row 3: Reset Scale
	Content.HSplitTop(22.0f, &Row, &Content);
	if(GameClient()->m_Menus.DoButton_Menu(&m_ResetScaleButton, "Restablecer Escala (100%)", 0, &Row))
	{
		HudLayout::ResetScale(m_PopupModule);
	}

	Content.HSplitTop(4.0f, nullptr, &Content);

	// Row 4: Reset All
	Content.HSplitTop(22.0f, &Row, &Content);
	if(GameClient()->m_Menus.DoButton_Menu(&m_ResetAllButton, "Restablecer Todo", 0, &Row))
	{
		HudLayout::ResetEditableModules();
	}
}
