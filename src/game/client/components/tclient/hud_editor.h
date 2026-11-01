#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_HUD_EDITOR_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_HUD_EDITOR_H

#include <game/client/component.h>
#include <engine/console.h>
#include <game/client/components/tclient/hud_layout.h>
#include <game/client/ui.h>

class CHudEditor : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnRender() override;
	bool OnInput(const IInput::CEvent &Event) override;
	bool OnCursorMove(float x, float y, IInput::ECursorType CursorType) override;
	void OnStateChange(int NewState, int OldState) override;

	void Activate();
	void Deactivate();
	void Toggle();
	bool IsActive() const { return m_Active; }

	static void ConToggleHudEdit(IConsole::IResult *pResult, void *pUserData);

private:
	static constexpr int MAX_MODULE_VISUALS = 16;

	struct SModuleVisual
	{
		HudLayout::EModule m_Module = HudLayout::MODULE_FPS;
		CUIRect m_Rect{};
		int m_Corners = IGraphics::CORNER_ALL;
		float m_Rounding = 4.0f;
		bool m_Editable = true;
		bool m_Enabled = true;
	};

	bool m_Active = false;
	bool m_MouseDownLast = false;
	bool m_RightMouseDownLast = false;
	bool m_Dragging = false;
	HudLayout::EModule m_PressedModule = HudLayout::MODULE_COUNT;
	HudLayout::EModule m_HoveredModule = HudLayout::MODULE_COUNT;
	HudLayout::EModule m_SelectedModule = HudLayout::MODULE_COUNT;
	vec2 m_DragMouseOffset = vec2(0.0f, 0.0f);

	bool m_ShowPopup = false;
	vec2 m_PopupPos = vec2(0.0f, 0.0f);
	HudLayout::EModule m_PopupModule = HudLayout::MODULE_COUNT;

	CButtonContainer m_ToggleModuleButton;
	CButtonContainer m_ResetPositionButton;
	CButtonContainer m_ResetScaleButton;
	CButtonContainer m_ResetAllButton;
	CButtonContainer m_CloseEditorButton;

	SModuleVisual GetModuleVisual(HudLayout::EModule Module) const;
	float HudWidth() const;
	float HudHeight() const;
	void CollectModuleVisuals(SModuleVisual *pOut, int &Count) const;
	HudLayout::EModule HitTestModule(vec2 MousePos) const;
	void UpdateDragging(vec2 MousePos);
	void RenderOverlay(vec2 MousePos);
	void RenderModuleOutline(const SModuleVisual &Visual, bool Hovered, bool Selected) const;
	void RenderPopupMenu();
};

#endif // GAME_CLIENT_COMPONENTS_TCLIENT_HUD_EDITOR_H
