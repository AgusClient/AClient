/* Copyright Â© 2026 BestProject Team */
#include "keystrokes.h"
#include "keystrokes_assets.inl"

#include <base/log.h>
#include <base/math.h>
#include <base/str.h>
#include <base/time.h>
#include <base/vmath.h>

#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#if defined(CONF_VIDEORECORDER)
#include <engine/shared/video.h>
#endif

#include <game/client/components/binds.h>
#include <game/client/components/controls.h>
#include <game/client/components/tclient/hud_layout.h>
#include <game/client/gameclient.h>
#include <game/client/prediction/entities/character.h>
#include <game/gamecore.h>

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float KEYSTROKES_ATLAS_SCALE = 0.14f;
	constexpr int KEYSTROKES_WHEEL_HIGHLIGHT_MS = 150;
	constexpr int KEYSTROKES_PRESSED_TEXTURE_SPACE = 3;
	constexpr int KEYSTROKES_KEYBOARD_ATLAS_WIDTH = 1725;
	constexpr int KEYSTROKES_KEYBOARD_ATLAS_HEIGHT = 1050;
	constexpr int KEYSTROKES_MOUSE_ATLAS_WIDTH = 715;
	constexpr int KEYSTROKES_MOUSE_ATLAS_HEIGHT = 353;
	constexpr float KEYSTROKES_MC_KEY = 150.0f;
	constexpr float KEYSTROKES_MC_GAP = 8.0f;
	constexpr int KEYSTROKES_MC_MAX_KEYS = 8;
	constexpr int KEYSTROKES_STYLE_MINECRAFT = 1;
	constexpr int KEYSTROKES_STYLE_DDNET = 2;
	constexpr float KEYSTROKES_DDNET_KEY_W = 175.0f;
	constexpr float KEYSTROKES_DDNET_KEY_H = 115.0f;
	constexpr float KEYSTROKES_DDNET_GAP = 16.0f;
	constexpr float KEYSTROKES_DDNET_WIDTH = 2.0f * KEYSTROKES_DDNET_KEY_W + KEYSTROKES_DDNET_GAP;
	constexpr float KEYSTROKES_DDNET_HEIGHT = 3.0f * KEYSTROKES_DDNET_KEY_H + 2.0f * KEYSTROKES_DDNET_GAP;
	constexpr float KEYSTROKES_DDNET_ROUNDING = 20.0f;
	constexpr int KEYSTROKES_DDNET_NUM_KEYS = 5;

	enum class EKeystrokesInputKind
	{
		NONE,
		KEY,
		MOUSE_BUTTON,
		WHEEL,
		MOUSE_MOVE,
	};

	struct SKeystrokesElement
	{
		EKeystrokesInputKind m_InputKind;
		int m_KeyPrimary;
		int m_KeySecondary;
		int m_MouseButton;
		int m_WheelDir;
		int m_MapX;
		int m_MapY;
		int m_MapW;
		int m_MapH;
		int m_PosX;
		int m_PosY;
		int m_MouseType;
		int m_MouseRadius;
		bool m_ActiveOnly;
	};

	struct SKeystrokesOverlayPreset
	{
		int m_OverlayWidth;
		int m_OverlayHeight;
		int m_PressedOffsetY;
		int m_AtlasWidth;
		int m_AtlasHeight;
		const SKeystrokesElement *m_pElements;
		int m_NumElements;
	};

	template<typename T, size_t N>
	constexpr int ArrayCount(const T (&)[N])
	{
		return (int)N;
	}

	constexpr SKeystrokesElement KeyboardElement(int PrimaryKey, int SecondaryKey, int MapX, int MapY, int MapW, int MapH, int PosX, int PosY)
	{
		return {EKeystrokesInputKind::KEY, PrimaryKey, SecondaryKey, 0, 0, MapX, MapY, MapW, MapH, PosX, PosY, 0, 0, false};
	}

	constexpr SKeystrokesElement StaticElement(int MapX, int MapY, int MapW, int MapH, int PosX, int PosY)
	{
		return {EKeystrokesInputKind::NONE, 0, 0, 0, 0, MapX, MapY, MapW, MapH, PosX, PosY, 0, 0, false};
	}

	constexpr SKeystrokesElement MouseButtonElement(int MouseButton, int MapX, int MapY, int MapW, int MapH, int PosX, int PosY, bool ActiveOnly = false)
	{
		return {EKeystrokesInputKind::MOUSE_BUTTON, 0, 0, MouseButton, 0, MapX, MapY, MapW, MapH, PosX, PosY, 0, 0, ActiveOnly};
	}

	constexpr SKeystrokesElement WheelElement(int WheelDir, int MapX, int MapY, int MapW, int MapH, int PosX, int PosY, bool ActiveOnly = false)
	{
		return {EKeystrokesInputKind::WHEEL, 0, 0, 0, WheelDir, MapX, MapY, MapW, MapH, PosX, PosY, 0, 0, ActiveOnly};
	}

	constexpr SKeystrokesElement MouseMoveElement(int MouseType, int MapX, int MapY, int MapW, int MapH, int PosX, int PosY, int MouseRadius)
	{
		return {EKeystrokesInputKind::MOUSE_MOVE, 0, 0, 0, 0, MapX, MapY, MapW, MapH, PosX, PosY, MouseType, MouseRadius, false};
	}

	const SKeystrokesElement gs_aKeyboardMinimalElements[] = {
		KeyboardElement(KEY_Q, 0, 1, 1, 157, 128, 137, 0),
		KeyboardElement(KEY_W, 0, 161, 1, 157, 128, 274, 0),
		KeyboardElement(KEY_E, 0, 321, 1, 157, 128, 411, 0),
		KeyboardElement(KEY_LSHIFT, KEY_RSHIFT, 481, 1, 157, 128, 0, 133),
		KeyboardElement(KEY_A, 0, 641, 1, 157, 128, 137, 133),
		KeyboardElement(KEY_S, 0, 801, 1, 157, 128, 274, 133),
		KeyboardElement(KEY_D, 0, 961, 1, 157, 128, 411, 133),
		KeyboardElement(KEY_LCTRL, KEY_RCTRL, 1121, 1, 157, 128, 0, 266),
		KeyboardElement(KEY_SPACE, 0, 1301, 1, 421, 128, 137, 266),
	};

	const SKeystrokesElement gs_aKeyboardFullElements[] = {
		KeyboardElement(KEY_Q, 0, 1, 1, 157, 128, 137, 0),
		KeyboardElement(KEY_W, 0, 161, 1, 157, 128, 274, 0),
		KeyboardElement(KEY_E, 0, 321, 1, 157, 128, 411, 0),
		KeyboardElement(KEY_TAB, 0, 1, 263, 157, 128, 0, 0),
		KeyboardElement(KEY_LSHIFT, KEY_RSHIFT, 481, 1, 157, 128, 0, 133),
		KeyboardElement(KEY_A, 0, 641, 1, 157, 128, 137, 133),
		KeyboardElement(KEY_S, 0, 801, 1, 157, 128, 274, 133),
		KeyboardElement(KEY_D, 0, 961, 1, 157, 128, 411, 133),
		KeyboardElement(KEY_R, 0, 161, 263, 157, 128, 548, 0),
		KeyboardElement(KEY_F, 0, 321, 263, 157, 128, 548, 133),
		KeyboardElement(KEY_LCTRL, KEY_RCTRL, 1121, 1, 157, 128, 0, 266),
		KeyboardElement(KEY_SPACE, 0, 1301, 1, 421, 128, 137, 266),
	};

	const SKeystrokesElement gs_aKeyboardMicroElements[] = {
		KeyboardElement(KEY_A, 0, 641, 1, 157, 128, 30, 0),
		KeyboardElement(KEY_S, 0, 801, 1, 157, 128, 167, 0),
		KeyboardElement(KEY_D, 0, 961, 1, 157, 128, 304, 0),
		KeyboardElement(KEY_SPACE, 0, 1301, 1, 421, 128, 5, 133),
	};

	const SKeystrokesOverlayPreset gs_aKeyboardPresets[] = {
		{568, 394, 130, KEYSTROKES_KEYBOARD_ATLAS_WIDTH, KEYSTROKES_KEYBOARD_ATLAS_HEIGHT, gs_aKeyboardMinimalElements, ArrayCount(gs_aKeyboardMinimalElements)},
		{705, 394, 130, KEYSTROKES_KEYBOARD_ATLAS_WIDTH, KEYSTROKES_KEYBOARD_ATLAS_HEIGHT, gs_aKeyboardFullElements, ArrayCount(gs_aKeyboardFullElements)},
		{461, 261, 130, KEYSTROKES_KEYBOARD_ATLAS_WIDTH, KEYSTROKES_KEYBOARD_ATLAS_HEIGHT, gs_aKeyboardMicroElements, ArrayCount(gs_aKeyboardMicroElements)},
	};

	const SKeystrokesElement gs_aMouseArrowElements[] = {
		StaticElement(328, 1, 283, 242, 2, 179),
		StaticElement(1, 1, 139, 174, 2, 0),
		StaticElement(143, 1, 139, 174, 146, 0),
		StaticElement(285, 246, 48, 95, 117, 79),
		StaticElement(285, 1, 40, 62, 0, 210),
		StaticElement(284, 1, 41, 62, 11, 273),
		MouseMoveElement(1, 614, 1, 100, 100, 95, 238, 50),
		MouseButtonElement(1, 1, 178, 139, 174, 2, 0, true),
		MouseButtonElement(2, 143, 178, 139, 174, 146, 0, true),
		MouseButtonElement(3, 336, 246, 48, 95, 117, 79, true),
		WheelElement(1, 387, 246, 48, 95, 117, 79, true),
		WheelElement(2, 438, 246, 48, 95, 117, 79, true),
		MouseButtonElement(5, 285, 66, 40, 62, 0, 210, true),
		MouseButtonElement(4, 285, 66, 40, 62, 11, 273, true),
	};

	const SKeystrokesElement gs_aMouseDotDotElements[] = {
		StaticElement(328, 1, 283, 242, 1, 179),
		StaticElement(1, 1, 139, 174, 2, 0),
		StaticElement(143, 1, 139, 174, 146, 0),
		StaticElement(285, 246, 48, 95, 117, 79),
		StaticElement(285, 1, 40, 62, 0, 210),
		StaticElement(284, 1, 41, 62, 11, 273),
		StaticElement(493, 245, 100, 100, 91, 245),
		MouseMoveElement(0, 614, 207, 20, 20, 132, 284, 50),
		MouseButtonElement(1, 1, 178, 139, 174, 2, 0, true),
		MouseButtonElement(2, 143, 178, 139, 174, 146, 0, true),
		MouseButtonElement(3, 336, 246, 48, 95, 117, 79, true),
		WheelElement(1, 387, 246, 48, 95, 117, 79, true),
		WheelElement(2, 438, 246, 48, 95, 117, 79, true),
		MouseButtonElement(5, 285, 66, 40, 62, 0, 210, true),
		MouseButtonElement(4, 285, 66, 40, 62, 11, 273, true),
	};

	const SKeystrokesElement gs_aMouseNothingElements[] = {
		StaticElement(328, 1, 283, 242, 2, 179),
		StaticElement(1, 1, 139, 174, 2, 0),
		StaticElement(143, 1, 139, 174, 146, 0),
		StaticElement(285, 246, 48, 95, 117, 79),
		StaticElement(285, 1, 40, 62, 0, 210),
		StaticElement(284, 1, 41, 62, 11, 273),
		MouseButtonElement(1, 1, 178, 139, 174, 2, 0, true),
		MouseButtonElement(2, 143, 178, 139, 174, 146, 0, true),
		MouseButtonElement(3, 336, 246, 48, 95, 117, 79, true),
		WheelElement(1, 387, 246, 48, 95, 117, 79, true),
		WheelElement(2, 438, 246, 48, 95, 117, 79, true),
		MouseButtonElement(5, 285, 66, 40, 62, 0, 210, true),
		MouseButtonElement(4, 285, 66, 40, 62, 11, 273, true),
	};

	const SKeystrokesOverlayPreset gs_aMousePresets[] = {
		{285, 421, 0, KEYSTROKES_MOUSE_ATLAS_WIDTH, KEYSTROKES_MOUSE_ATLAS_HEIGHT, gs_aMouseArrowElements, ArrayCount(gs_aMouseArrowElements)},
		{285, 421, 0, KEYSTROKES_MOUSE_ATLAS_WIDTH, KEYSTROKES_MOUSE_ATLAS_HEIGHT, gs_aMouseDotDotElements, ArrayCount(gs_aMouseDotDotElements)},
		{285, 421, 0, KEYSTROKES_MOUSE_ATLAS_WIDTH, KEYSTROKES_MOUSE_ATLAS_HEIGHT, gs_aMouseNothingElements, ArrayCount(gs_aMouseNothingElements)},
	};

	const SKeystrokesOverlayPreset &GetKeystrokesKeyboardPreset(int Preset)
	{
		return gs_aKeyboardPresets[std::clamp(Preset, 0, ArrayCount(gs_aKeyboardPresets) - 1)];
	}

	const SKeystrokesOverlayPreset &GetKeystrokesMousePreset(int Preset)
	{
		return gs_aMousePresets[std::clamp(Preset - 1, 0, ArrayCount(gs_aMousePresets) - 1)];
	}

	bool IsKeystrokesPressed(IInput *pInput, int PrimaryKey, int SecondaryKey = 0)
	{
		return (PrimaryKey > 0 && pInput->KeyIsPressed(PrimaryKey)) ||
		       (SecondaryKey > 0 && pInput->KeyIsPressed(SecondaryKey));
	}

	bool IsKeystrokesPressed(const CNetObj_PlayerInput *pInput, int PrimaryKey, int SecondaryKey = 0)
	{
		if(pInput == nullptr)
			return false;

		auto IsPressed = [pInput](int Key) {
			switch(Key)
			{
			case KEY_A:
				return pInput->m_Direction < 0;
			case KEY_D:
				return pInput->m_Direction > 0;
			case KEY_W:
			case KEY_SPACE:
				return pInput->m_Jump != 0;
			case KEY_Q:
				return pInput->m_PrevWeapon != 0;
			case KEY_E:
				return pInput->m_NextWeapon != 0;
			default:
				return false;
			}
		};

		return IsPressed(PrimaryKey) || IsPressed(SecondaryKey);
	}

	bool IsKeystrokesPressed(const CNetObj_Character *pCharacter, int PrimaryKey, int SecondaryKey = 0)
	{
		if(pCharacter == nullptr)
			return false;

		auto IsPressed = [pCharacter](int Key) {
			switch(Key)
			{
			case KEY_A:
				return pCharacter->m_Direction < 0;
			case KEY_D:
				return pCharacter->m_Direction > 0;
			case KEY_W:
			case KEY_SPACE:
				return (pCharacter->m_Jumped & 1) != 0;
			default:
				return false;
			}
		};

		return IsPressed(PrimaryKey) || IsPressed(SecondaryKey);
	}

	bool IsKeystrokesMouseButtonPressed(IInput *pInput, int MouseButton)
	{
		if(MouseButton < 1 || MouseButton > 9)
			return false;
		return pInput->KeyIsPressed(KEY_MOUSE_1 + MouseButton - 1);
	}

	bool IsKeystrokesMouseButtonPressed(const CNetObj_PlayerInput *pInput, int MouseButton)
	{
		if(pInput == nullptr)
			return false;

		switch(MouseButton)
		{
		case 1:
			return (pInput->m_Fire & 1) != 0;
		case 2:
			return pInput->m_Hook != 0;
		default:
			return false;
		}
	}

	bool IsKeystrokesWheelActive(int WheelDir, int64_t Now, int64_t WheelUpEndTime, int64_t WheelDownEndTime)
	{
		switch(WheelDir)
		{
		case 1:
			return WheelUpEndTime > Now;
		case 2:
			return WheelDownEndTime > Now;
		default:
			return WheelUpEndTime > Now || WheelDownEndTime > Now;
		}
	}

	bool GetKeystrokesTrackedAim(const CGameClient *pGameClient, int TrackedClientId, float Intra, vec2 &OutAim)
	{
		if(pGameClient == nullptr || !in_range(TrackedClientId, 0, MAX_CLIENTS - 1))
			return false;

		const auto &Character = pGameClient->m_Snap.m_aCharacters[TrackedClientId];
		if(!Character.m_Active)
			return false;

		if(Character.m_HasExtendedDisplayInfo)
		{
			const CNetObj_DDNetCharacter *pExtendedData = &Character.m_ExtendedData;
			const CNetObj_DDNetCharacter *pPrevExtendedData = Character.m_pPrevExtendedData;
			if(pPrevExtendedData != nullptr)
			{
				OutAim = vec2(
					mix((float)pPrevExtendedData->m_TargetX, (float)pExtendedData->m_TargetX, Intra),
					mix((float)pPrevExtendedData->m_TargetY, (float)pExtendedData->m_TargetY, Intra));
			}
			else
			{
				OutAim = vec2((float)pExtendedData->m_TargetX, (float)pExtendedData->m_TargetY);
			}
			return length(OutAim) > 0.001f;
		}

		float Angle = 0.0f;
		if(Character.m_Cur.m_Angle > (256.0f * pi) && Character.m_Prev.m_Angle < 0)
			Angle = mix((float)Character.m_Prev.m_Angle, (float)(Character.m_Cur.m_Angle - 256.0f * 2 * pi), Intra) / 256.0f;
		else if(Character.m_Cur.m_Angle < 0 && Character.m_Prev.m_Angle > (256.0f * pi))
			Angle = mix((float)Character.m_Prev.m_Angle, (float)(Character.m_Cur.m_Angle + 256.0f * 2 * pi), Intra) / 256.0f;
		else
			Angle = mix((float)Character.m_Prev.m_Angle, (float)Character.m_Cur.m_Angle, Intra) / 256.0f;

		OutAim = direction(Angle) * 256.0f;
		return true;
	}

	bool IsKeystrokesMouseButtonPressedFromCharacter(const CNetObj_Character *pPrevCharacter, const CNetObj_Character *pCharacter, int MouseButton, int64_t Now, int64_t Mouse1EndTime)
	{
		if(pCharacter == nullptr)
			return false;

		switch(MouseButton)
		{
		case 1:
			return Mouse1EndTime > Now || (pPrevCharacter != nullptr && pPrevCharacter->m_AttackTick != pCharacter->m_AttackTick);
		case 2:
			return pCharacter->m_HookState != HOOK_IDLE;
		default:
			return false;
		}
	}

	float GetKeystrokesScale(const HudLayout::SModuleLayout &Layout)
	{
		return std::clamp(Layout.m_Scale / 100.0f, 0.25f, 3.0f) * KEYSTROKES_ATLAS_SCALE;
	}

	bool IsKeystrokesMinecraftStyle()
	{
		return g_Config.m_AcKeystrokesStyle == KEYSTROKES_STYLE_MINECRAFT;
	}

	bool IsKeystrokesDdnetStyle()
	{
		return g_Config.m_AcKeystrokesStyle == KEYSTROKES_STYLE_DDNET;
	}

	bool IsKeystrokesCombinedStyle()
	{
		return IsKeystrokesMinecraftStyle() || IsKeystrokesDdnetStyle();
	}

	struct SKeystrokesMcKey
	{
		enum class EKind
		{
			KEY,
			MOUSE,
			SPACE,
		};

		enum class EAction
		{
			LEFT,
			RIGHT,
			UP,
			DOWN,
			JUMP,
			FIRE,
			HOOK,
		};

		EKind m_Kind = EKind::KEY;
		EAction m_Action = EAction::LEFT;
		char m_aLabel[32] = "";
		bool m_ShowLabel = true;
		float m_X = 0.0f;
		float m_Y = 0.0f;
		float m_W = 0.0f;
		float m_H = 0.0f;
	};

	struct SKeystrokesMcLayout
	{
		float m_OverlayWidth = 0.0f;
		float m_OverlayHeight = 0.0f;
		SKeystrokesMcKey m_aKeys[KEYSTROKES_MC_MAX_KEYS] = {};
		int m_NumKeys = 0;
	};

	void ResolveKeystrokesMcLabel(const CBinds *pBinds, const char *pCommand, const char *pFallback, char *pOut, size_t OutSize)
	{
		pOut[0] = '\0';
		if(pBinds != nullptr && pCommand != nullptr)
			pBinds->GetKey(pCommand, pOut, OutSize);
		if(pOut[0] == '\0')
		{
			str_copy(pOut, pFallback, OutSize);
			return;
		}

		if(str_comp_nocase(pOut, "mouse1") == 0)
			str_copy(pOut, "LMB", OutSize);
		else if(str_comp_nocase(pOut, "mouse2") == 0)
			str_copy(pOut, "RMB", OutSize);
		else if(str_comp_nocase(pOut, "mouse3") == 0)
			str_copy(pOut, "MMB", OutSize);
		else
		{
			for(size_t i = 0; pOut[i] != '\0'; ++i)
				pOut[i] = str_uppercase(pOut[i]);
		}
	}

	bool IsKeystrokesMcActionActiveLocal(const CGameClient *pGameClient, SKeystrokesMcKey::EAction Action)
	{
		if(pGameClient == nullptr)
			return false;

		const CNetObj_PlayerInput &Input = pGameClient->m_Controls.m_aInputData[g_Config.m_ClDummy];
		switch(Action)
		{
		case SKeystrokesMcKey::EAction::LEFT:
			return Input.m_Direction < 0;
		case SKeystrokesMcKey::EAction::RIGHT:
			return Input.m_Direction > 0;
		case SKeystrokesMcKey::EAction::UP:
		case SKeystrokesMcKey::EAction::JUMP:
			return Input.m_Jump != 0;
		case SKeystrokesMcKey::EAction::DOWN:
			return pGameClient->Input()->KeyIsPressed(KEY_S);
		case SKeystrokesMcKey::EAction::FIRE:
			return (Input.m_Fire & 1) != 0;
		case SKeystrokesMcKey::EAction::HOOK:
			return Input.m_Hook != 0;
		}
		return false;
	}

	bool IsKeystrokesMcActionActiveTracked(
		SKeystrokesMcKey::EAction Action,
		const CNetObj_PlayerInput *pTrackedInput,
		const CNetObj_Character *pPrevTrackedCharacter,
		const CNetObj_Character *pTrackedCharacter,
		int64_t Now,
		int64_t Mouse1EndTime)
	{
		switch(Action)
		{
		case SKeystrokesMcKey::EAction::LEFT:
			return pTrackedInput != nullptr ?
				       IsKeystrokesPressed(pTrackedInput, KEY_A) :
				       IsKeystrokesPressed(pTrackedCharacter, KEY_A);
		case SKeystrokesMcKey::EAction::RIGHT:
			return pTrackedInput != nullptr ?
				       IsKeystrokesPressed(pTrackedInput, KEY_D) :
				       IsKeystrokesPressed(pTrackedCharacter, KEY_D);
		case SKeystrokesMcKey::EAction::UP:
		case SKeystrokesMcKey::EAction::JUMP:
			return pTrackedInput != nullptr ?
				       IsKeystrokesPressed(pTrackedInput, KEY_SPACE) :
				       IsKeystrokesPressed(pTrackedCharacter, KEY_SPACE);
		case SKeystrokesMcKey::EAction::DOWN:
			return false;
		case SKeystrokesMcKey::EAction::FIRE:
			return pTrackedInput != nullptr ?
				       IsKeystrokesMouseButtonPressed(pTrackedInput, 1) :
				       IsKeystrokesMouseButtonPressedFromCharacter(pPrevTrackedCharacter, pTrackedCharacter, 1, Now, Mouse1EndTime);
		case SKeystrokesMcKey::EAction::HOOK:
			return pTrackedInput != nullptr ?
				       IsKeystrokesMouseButtonPressed(pTrackedInput, 2) :
				       IsKeystrokesMouseButtonPressedFromCharacter(pPrevTrackedCharacter, pTrackedCharacter, 2, Now, Mouse1EndTime);
		}
		return false;
	}

	SKeystrokesMcLayout BuildKeystrokesMcLayout(const CBinds *pBinds = nullptr)
	{
		SKeystrokesMcLayout Layout;
		const float K = KEYSTROKES_MC_KEY;
		const float G = KEYSTROKES_MC_GAP;
		const bool OnlyAd = g_Config.m_AcKeystrokesMcLayout == 1;
		const bool ShowWs = !OnlyAd;
		const bool ShowLmb = !OnlyAd || g_Config.m_AcKeystrokesMcShowLmb != 0;
		const bool ShowRmb = !OnlyAd || g_Config.m_AcKeystrokesMcShowRmb != 0;
		const bool ShowSpace = !OnlyAd || g_Config.m_AcKeystrokesMcShowSpace != 0;
		const float Width = ShowWs ? (3.0f * K + 2.0f * G) : (2.0f * K + G);

		float Y = 0.0f;
		bool HasRow = false;
		auto StartRow = [&]() {
			if(HasRow)
				Y += K + G;
			HasRow = true;
		};
		auto AddKey = [&](SKeystrokesMcKey::EKind Kind, SKeystrokesMcKey::EAction Action, const char *pCommand, const char *pFallback, float X, float W, bool ForceLine = false) {
			if(Layout.m_NumKeys >= KEYSTROKES_MC_MAX_KEYS)
				return;
			SKeystrokesMcKey &Key = Layout.m_aKeys[Layout.m_NumKeys++];
			Key.m_Kind = Kind;
			Key.m_Action = Action;
			Key.m_X = X;
			Key.m_Y = Y;
			Key.m_W = W;
			Key.m_H = K;
			if(ForceLine)
			{
				Key.m_aLabel[0] = '\0';
				Key.m_ShowLabel = false;
			}
			else
			{
				ResolveKeystrokesMcLabel(pBinds, pCommand, pFallback, Key.m_aLabel, sizeof(Key.m_aLabel));
				Key.m_ShowLabel = true;
			}
		};

		if(ShowWs)
		{
			StartRow();
			AddKey(SKeystrokesMcKey::EKind::KEY, SKeystrokesMcKey::EAction::UP, nullptr, "W", K + G, K);
			StartRow();
			AddKey(SKeystrokesMcKey::EKind::KEY, SKeystrokesMcKey::EAction::LEFT, "+left", "A", 0.0f, K);
			AddKey(SKeystrokesMcKey::EKind::KEY, SKeystrokesMcKey::EAction::DOWN, nullptr, "S", K + G, K);
			AddKey(SKeystrokesMcKey::EKind::KEY, SKeystrokesMcKey::EAction::RIGHT, "+right", "D", 2.0f * (K + G), K);
		}
		else
		{
			StartRow();
			AddKey(SKeystrokesMcKey::EKind::KEY, SKeystrokesMcKey::EAction::LEFT, "+left", "A", 0.0f, K);
			AddKey(SKeystrokesMcKey::EKind::KEY, SKeystrokesMcKey::EAction::RIGHT, "+right", "D", K + G, K);
		}

		if(ShowLmb || ShowRmb)
		{
			StartRow();
			if(ShowLmb && ShowRmb)
			{
				const float Half = (Width - G) * 0.5f;
				AddKey(SKeystrokesMcKey::EKind::MOUSE, SKeystrokesMcKey::EAction::FIRE, "+fire", "LMB", 0.0f, Half);
				AddKey(SKeystrokesMcKey::EKind::MOUSE, SKeystrokesMcKey::EAction::HOOK, "+hook", "RMB", Half + G, Half);
			}
			else if(ShowLmb)
			{
				AddKey(SKeystrokesMcKey::EKind::MOUSE, SKeystrokesMcKey::EAction::FIRE, "+fire", "LMB", 0.0f, Width);
			}
			else
			{
				AddKey(SKeystrokesMcKey::EKind::MOUSE, SKeystrokesMcKey::EAction::HOOK, "+hook", "RMB", 0.0f, Width);
			}
		}

		if(ShowSpace)
		{
			StartRow();
			char aJumpKey[64] = "";
			if(pBinds != nullptr)
				pBinds->GetKey("+jump", aJumpKey, sizeof(aJumpKey));
			const bool JumpIsSpace = aJumpKey[0] == '\0' || str_comp_nocase(aJumpKey, "space") == 0;
			AddKey(SKeystrokesMcKey::EKind::SPACE, SKeystrokesMcKey::EAction::JUMP, "+jump", "SPACE", 0.0f, Width, JumpIsSpace);
		}

		Layout.m_OverlayWidth = Width;
		Layout.m_OverlayHeight = HasRow ? (Y + K) : 0.0f;
		return Layout;
	}

	const SKeystrokesMcLayout &GetCachedKeystrokesMcLayout(const CBinds *pBinds)
	{
		static SKeystrokesMcLayout s_CachedLayout;
		static int s_LastMcLayout = -1;
		static int s_LastShowLmb = -1;
		static int s_LastShowRmb = -1;
		static int s_LastShowSpace = -1;
		static bool s_HadBinds = false;
		static int64_t s_LastRefreshTick = 0;

		const int64_t Now = time_get();
		const bool HasBindsNow = pBinds != nullptr;
		const bool ConfigChanged = s_LastMcLayout != g_Config.m_AcKeystrokesMcLayout ||
					   s_LastShowLmb != g_Config.m_AcKeystrokesMcShowLmb ||
					   s_LastShowRmb != g_Config.m_AcKeystrokesMcShowRmb ||
					   s_LastShowSpace != g_Config.m_AcKeystrokesMcShowSpace ||
					   (!s_HadBinds && HasBindsNow);
		if(ConfigChanged || s_LastRefreshTick == 0 || Now - s_LastRefreshTick > time_freq() / 2)
		{
			s_CachedLayout = BuildKeystrokesMcLayout(pBinds);
			s_LastMcLayout = g_Config.m_AcKeystrokesMcLayout;
			s_LastShowLmb = g_Config.m_AcKeystrokesMcShowLmb;
			s_LastShowRmb = g_Config.m_AcKeystrokesMcShowRmb;
			s_LastShowSpace = g_Config.m_AcKeystrokesMcShowSpace;
			s_HadBinds = HasBindsNow;
			s_LastRefreshTick = Now;
		}
		return s_CachedLayout;
	}

	void DrawKeystrokesMcKey(IGraphics *pGraphics, ITextRender *pTextRender, float X, float Y, float W, float H, bool Active, const char *pLabel)
	{
		if(W <= 0.0f || H <= 0.0f)
			return;

		const float PressedOpacity = std::clamp(g_Config.m_AcKeystrokesMcPressedOpacity * 0.01f, 0.0f, 1.0f);
		const ColorRGBA Bg = Active ? ColorRGBA(1.0f, 1.0f, 1.0f, PressedOpacity) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.55f);
		pGraphics->DrawRect(X, Y, W, H, Bg, IGraphics::CORNER_NONE, 0.0f);

		if(pLabel == nullptr)
		{
			const ColorRGBA LineColor = Active ? ColorRGBA(0.0f, 0.0f, 0.0f, 0.85f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.9f);
			const float LineW = W * 0.35f;
			const float LineH = std::max(2.0f, H * 0.08f);
			pGraphics->DrawRect(X + (W - LineW) * 0.5f, Y + (H - LineH) * 0.5f, LineW, LineH, LineColor, IGraphics::CORNER_NONE, 0.0f);
			return;
		}

		const ColorRGBA TextColor = Active ? ColorRGBA(0.0f, 0.0f, 0.0f, 1.0f) : ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		const float FontSize = H * (str_length(pLabel) > 1 ? 0.32f : 0.45f);
		pTextRender->TextColor(TextColor);
		const float TextW = pTextRender->TextWidth(FontSize, pLabel, -1, -1.0f);
		pTextRender->Text(X + (W - TextW) * 0.5f, Y + (H - FontSize) * 0.5f, FontSize, pLabel);
		pTextRender->TextColor(pTextRender->DefaultTextColor());
	}

	struct SKeystrokesDdnetKeyDef
	{
		SKeystrokesMcKey::EAction m_Action;
		const char *m_pLabel;
		float m_X;
		float m_Y;
		float m_W;
		float m_H;
		bool m_MultiChar;
	};

	constexpr SKeystrokesDdnetKeyDef gs_aKeystrokesDdnetKeys[KEYSTROKES_DDNET_NUM_KEYS] = {
		{SKeystrokesMcKey::EAction::LEFT, "A", 0.0f, 0.0f, KEYSTROKES_DDNET_KEY_W, KEYSTROKES_DDNET_KEY_H, false},
		{SKeystrokesMcKey::EAction::RIGHT, "D", KEYSTROKES_DDNET_KEY_W + KEYSTROKES_DDNET_GAP, 0.0f, KEYSTROKES_DDNET_KEY_W, KEYSTROKES_DDNET_KEY_H, false},
		{SKeystrokesMcKey::EAction::FIRE, "FIRE", 0.0f, KEYSTROKES_DDNET_KEY_H + KEYSTROKES_DDNET_GAP, KEYSTROKES_DDNET_KEY_W, KEYSTROKES_DDNET_KEY_H, true},
		{SKeystrokesMcKey::EAction::HOOK, "HOOK", KEYSTROKES_DDNET_KEY_W + KEYSTROKES_DDNET_GAP, KEYSTROKES_DDNET_KEY_H + KEYSTROKES_DDNET_GAP, KEYSTROKES_DDNET_KEY_W, KEYSTROKES_DDNET_KEY_H, true},
		{SKeystrokesMcKey::EAction::JUMP, "JUMP", 0.0f, 2.0f * (KEYSTROKES_DDNET_KEY_H + KEYSTROKES_DDNET_GAP), KEYSTROKES_DDNET_WIDTH, KEYSTROKES_DDNET_KEY_H, true},
	};

	struct SDdnetKeystrokesMetricsCache
	{
		float m_Scale = -1.0f;
		float m_aFontSize[KEYSTROKES_DDNET_NUM_KEYS] = {};
		float m_aTextW[KEYSTROKES_DDNET_NUM_KEYS] = {};
	};

	const SDdnetKeystrokesMetricsCache &GetCachedDdnetMetrics(ITextRender *pTextRender, float Scale)
	{
		static SDdnetKeystrokesMetricsCache s_Cache;
		if(std::abs(s_Cache.m_Scale - Scale) > 0.0001f)
		{
			s_Cache.m_Scale = Scale;
			const float KeyH = KEYSTROKES_DDNET_KEY_H * Scale;
			for(int i = 0; i < KEYSTROKES_DDNET_NUM_KEYS; ++i)
			{
				const auto &Def = gs_aKeystrokesDdnetKeys[i];
				const float MaxW = Def.m_W * Scale * 0.86f;
				float FontSize = KeyH * (Def.m_MultiChar ? 0.42f : 0.56f);
				float TextW = pTextRender->TextWidth(FontSize, Def.m_pLabel, -1, -1.0f);
				if(TextW > MaxW && TextW > 0.001f)
				{
					FontSize *= MaxW / TextW;
					TextW = MaxW;
				}
				s_Cache.m_aFontSize[i] = FontSize;
				s_Cache.m_aTextW[i] = TextW;
			}
		}
		return s_Cache;
	}

	void DrawKeystrokesDdnetOverlay(
		IGraphics *pGraphics,
		ITextRender *pTextRender,
		const CUIRect &Rect,
		float Scale,
		const bool aActive[KEYSTROKES_DDNET_NUM_KEYS])
	{
		const float PressedOpacity = std::clamp(g_Config.m_AcKeystrokesMcPressedOpacity * 0.01f, 0.0f, 1.0f);
		const float Rounding = KEYSTROKES_DDNET_ROUNDING * Scale;
		const ColorRGBA BgInactive(0.11f, 0.12f, 0.15f, 0.74f);
		const ColorRGBA BgActive(0.08f, 0.56f, 0.76f, PressedOpacity);
		const ColorRGBA TextInactive(0.55f, 0.57f, 0.62f, 0.92f);
		const ColorRGBA TextActive(1.0f, 1.0f, 1.0f, 1.0f);
		const ColorRGBA OutlineInactive(0.0f, 0.0f, 0.0f, 0.25f);
		const ColorRGBA OutlineActive(0.0f, 0.0f, 0.0f, 0.65f);

		const auto &Metrics = GetCachedDdnetMetrics(pTextRender, Scale);
		const ColorRGBA PrevTextColor = pTextRender->GetTextColor();
		const ColorRGBA PrevOutlineColor = pTextRender->GetTextOutlineColor();

		pGraphics->TextureClear();
		pGraphics->QuadsBegin();
		for(int i = 0; i < KEYSTROKES_DDNET_NUM_KEYS; ++i)
		{
			const auto &Def = gs_aKeystrokesDdnetKeys[i];
			const float X = Rect.x + Def.m_X * Scale;
			const float Y = Rect.y + Def.m_Y * Scale;
			const float W = Def.m_W * Scale;
			const float H = Def.m_H * Scale;
			pGraphics->SetColor(aActive[i] ? BgActive : BgInactive);
			pGraphics->DrawRectExt(X, Y, W, H, Rounding, IGraphics::CORNER_ALL);
		}
		pGraphics->QuadsEnd();

		for(int i = 0; i < KEYSTROKES_DDNET_NUM_KEYS; ++i)
		{
			const auto &Def = gs_aKeystrokesDdnetKeys[i];
			const float X = Rect.x + Def.m_X * Scale;
			const float Y = Rect.y + Def.m_Y * Scale;
			const float W = Def.m_W * Scale;
			const float H = Def.m_H * Scale;
			const bool Active = aActive[i];
			const float FontSize = Metrics.m_aFontSize[i];
			const float TextW = Metrics.m_aTextW[i];
			pTextRender->TextColor(Active ? TextActive : TextInactive);
			pTextRender->TextOutlineColor(Active ? OutlineActive : OutlineInactive);
			pTextRender->Text(X + (W - TextW) * 0.5f, Y + (H - FontSize) * 0.5f, FontSize, Def.m_pLabel);
		}

		pTextRender->TextColor(PrevTextColor);
		pTextRender->TextOutlineColor(PrevOutlineColor);
	}

	void DrawKeystrokesSpriteQuad(
		IGraphics *pGraphics,
		int AtlasWidth,
		int AtlasHeight,
		int MapX,
		int MapY,
		int MapW,
		int MapH,
		float X,
		float Y,
		float W,
		float H,
		ColorRGBA Color = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f),
		float Rotation = 0.0f)
	{
		if(W <= 0.0f || H <= 0.0f)
			return;

		pGraphics->SetColor(Color);
		pGraphics->QuadsSetSubset(
			MapX / (float)AtlasWidth,
			MapY / (float)AtlasHeight,
			(MapX + MapW) / (float)AtlasWidth,
			(MapY + MapH) / (float)AtlasHeight);
		pGraphics->QuadsSetRotation(Rotation);
		IGraphics::CQuadItem Quad(X + W * 0.5f, Y + H * 0.5f, W, H);
		pGraphics->QuadsDraw(&Quad, 1);
	}
} // namespace

float GetKeystrokesKeyboardPresetWidthHudPx(int Preset)
{
	if(g_Config.m_AcKeystrokesStyle == KEYSTROKES_STYLE_DDNET)
		return KEYSTROKES_DDNET_WIDTH * KEYSTROKES_ATLAS_SCALE;
	if(g_Config.m_AcKeystrokesStyle == KEYSTROKES_STYLE_MINECRAFT)
		return GetCachedKeystrokesMcLayout(nullptr).m_OverlayWidth * KEYSTROKES_ATLAS_SCALE;
	return GetKeystrokesKeyboardPreset(Preset).m_OverlayWidth * KEYSTROKES_ATLAS_SCALE;
}

void CKeystrokes::OnInit()
{
	OnReset();
	auto LoadEmbeddedPng = [&](const uint8_t *pData, size_t DataSize, const char *pName) -> IGraphics::CTextureHandle {
		CImageInfo Image;
		if(Graphics()->LoadPng(Image, pData, DataSize, pName))
		{
			return Graphics()->LoadTextureRawMove(Image, 0, pName);
		}
		return IGraphics::CTextureHandle();
	};

	m_KeyboardTexture = Graphics()->LoadTexture("AClient/keystrokes/wasd.png", IStorage::TYPE_ALL);
	if(m_KeyboardTexture.IsNullTexture())
		m_KeyboardTexture = Graphics()->LoadTexture("BestClient/keystrokes/wasd.png", IStorage::TYPE_ALL);
	if(m_KeyboardTexture.IsNullTexture())
		m_KeyboardTexture = LoadEmbeddedPng(s_aDefaultWasdPng, sizeof(s_aDefaultWasdPng), "AClient/keystrokes/wasd.png");

	m_MouseTexture = Graphics()->LoadTexture("AClient/keystrokes/mouse.png", IStorage::TYPE_ALL);
	if(m_MouseTexture.IsNullTexture())
		m_MouseTexture = Graphics()->LoadTexture("BestClient/keystrokes/mouse.png", IStorage::TYPE_ALL);
	if(m_MouseTexture.IsNullTexture())
		m_MouseTexture = LoadEmbeddedPng(s_aDefaultMousePng, sizeof(s_aDefaultMousePng), "AClient/keystrokes/mouse.png");

	if(m_KeyboardTexture.IsNullTexture())
		log_warn("keystrokes", "Failed to load keyboard keystrokes texture");
	if(m_MouseTexture.IsNullTexture())
		log_warn("keystrokes", "Failed to load mouse keystrokes texture");
}

void CKeystrokes::OnReset()
{
	m_Mouse1EndTime = 0;
	m_WheelUpEndTime = 0;
	m_WheelDownEndTime = 0;
}

void CKeystrokes::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;
	if(GameClient()->m_HudEditor.IsActive())
		return;
#if defined(CONF_VIDEORECORDER)
	if(!((IVideo::Current() && g_Config.m_ClVideoShowhud) || (!IVideo::Current() && g_Config.m_ClShowhud)))
		return;
#else
	if(!g_Config.m_ClShowhud)
		return;
#endif

	const bool KeyboardEnabled = HudLayout::IsEnabled(HudLayout::MODULE_KEYSTROKES_KEYBOARD) && g_Config.m_AcKeystrokesKeyboard != 0;
	const bool MouseEnabled = HudLayout::IsEnabled(HudLayout::MODULE_KEYSTROKES_MOUSE) && g_Config.m_AcKeystrokesMouse != 0;
	if(!KeyboardEnabled && !MouseEnabled)
		return;

	const float HudHeight = HudLayout::CANVAS_HEIGHT;
	const float HudWidth = HudHeight * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(HudWidth, HudHeight);

	RenderKeyboardInternal(false, false);
	RenderMouseInternal(false, false);
}

CUIRect CKeystrokes::GetKeyboardRectInternal(bool IgnoreModuleEnabled) const
{
	if(!IgnoreModuleEnabled && (!HudLayout::IsEnabled(HudLayout::MODULE_KEYSTROKES_KEYBOARD) || g_Config.m_AcKeystrokesKeyboard == 0))
		return {};

	const float HudHeight = HudLayout::CANVAS_HEIGHT;
	const float HudWidth = HudHeight * Graphics()->ScreenAspect();
	const auto Layout = HudLayout::Get(HudLayout::MODULE_KEYSTROKES_KEYBOARD, HudWidth, HudHeight);
	const float Scale = GetKeystrokesScale(Layout);
	float OverlayWidth;
	float OverlayHeight;
	if(IsKeystrokesDdnetStyle())
	{
		OverlayWidth = KEYSTROKES_DDNET_WIDTH;
		OverlayHeight = KEYSTROKES_DDNET_HEIGHT;
	}
	else if(IsKeystrokesMinecraftStyle())
	{
		const auto &McLayout = GetCachedKeystrokesMcLayout(&GameClient()->m_Binds);
		OverlayWidth = McLayout.m_OverlayWidth;
		OverlayHeight = McLayout.m_OverlayHeight;
	}
	else
	{
		const auto &Preset = GetKeystrokesKeyboardPreset(g_Config.m_AcKeystrokesKeyboardPreset);
		OverlayWidth = (float)Preset.m_OverlayWidth;
		OverlayHeight = (float)Preset.m_OverlayHeight;
	}
	const HudLayout::SModuleRect RawRect = {Layout.m_X, Layout.m_Y, OverlayWidth * Scale, OverlayHeight * Scale, 0.0f};
	const auto Rect = HudLayout::ClampRectToScreen(RawRect, HudWidth, HudHeight);
	return {Rect.m_X, Rect.m_Y, Rect.m_W, Rect.m_H};
}

CUIRect CKeystrokes::GetMouseRectInternal(bool IgnoreModuleEnabled) const
{
	if(IsKeystrokesCombinedStyle())
		return {};
	if(!IgnoreModuleEnabled && (!HudLayout::IsEnabled(HudLayout::MODULE_KEYSTROKES_MOUSE) || g_Config.m_AcKeystrokesMouse == 0))
		return {};

	const float HudHeight = HudLayout::CANVAS_HEIGHT;
	const float HudWidth = HudHeight * Graphics()->ScreenAspect();
	const auto Layout = HudLayout::Get(HudLayout::MODULE_KEYSTROKES_MOUSE, HudWidth, HudHeight);
	const auto &Preset = GetKeystrokesMousePreset(g_Config.m_AcKeystrokesMousePreset);
	const float Scale = GetKeystrokesScale(Layout);
	const float Width = Preset.m_OverlayWidth * Scale;
	const float Height = Preset.m_OverlayHeight * Scale;

	const HudLayout::SModuleRect RawRect = {Layout.m_X, Layout.m_Y, Width, Height, 0.0f};
	const auto Rect = HudLayout::ClampRectToScreen(RawRect, HudWidth, HudHeight);
	return {Rect.m_X, Rect.m_Y, Rect.m_W, Rect.m_H};
}

CUIRect CKeystrokes::GetKeyboardHudEditorRect() const
{
	CUIRect Rect = GetKeyboardRectInternal(true);
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return Rect;

	if(!IsKeystrokesCombinedStyle())
	{
		const auto &DefaultMousePreset = GetKeystrokesMousePreset(1);
		Rect.h = std::max(Rect.h, DefaultMousePreset.m_OverlayHeight * KEYSTROKES_ATLAS_SCALE);
	}
	return Rect;
}

int CKeystrokes::GetTrackedClientId() const
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		if(!GameClient()->m_Snap.m_SpecInfo.m_Active &&
			in_range(GameClient()->m_Snap.m_LocalClientId, 0, MAX_CLIENTS - 1) &&
			GameClient()->m_Snap.m_aCharacters[GameClient()->m_Snap.m_LocalClientId].m_Active)
			return GameClient()->m_Snap.m_LocalClientId;
		if(GameClient()->m_DemoSpecId > SPEC_FREEVIEW && GameClient()->m_DemoSpecId < MAX_CLIENTS)
			return GameClient()->m_DemoSpecId;
	}

	if(!GameClient()->m_Snap.m_SpecInfo.m_Active)
		return -1;

	const int SpectatorId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;
	if(SpectatorId <= SPEC_FREEVIEW || SpectatorId >= MAX_CLIENTS)
		return -1;

	return SpectatorId;
}

const CNetObj_PlayerInput *CKeystrokes::GetTrackedInput() const
{
	const int SpectatorId = GetTrackedClientId();
	if(SpectatorId < 0)
		return nullptr;

	if(CCharacter *pCharacter = GameClient()->m_GameWorld.GetCharacterById(SpectatorId))
		return pCharacter->LatestInput();

	return nullptr;
}

void CKeystrokes::RenderKeyboardInternal(bool ForcePreview, bool IgnoreModuleEnabled)
{
	const CUIRect Rect = GetKeyboardRectInternal(IgnoreModuleEnabled);
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;

	const float HudHeight = HudLayout::CANVAS_HEIGHT;
	const float HudWidth = HudHeight * Graphics()->ScreenAspect();
	const auto Layout = HudLayout::Get(HudLayout::MODULE_KEYSTROKES_KEYBOARD, HudWidth, HudHeight);
	const float Scale = GetKeystrokesScale(Layout);
	const int TrackedClientId = ForcePreview ? -1 : GetTrackedClientId();
	const bool HasTrackedPlayer = TrackedClientId >= 0;
	const CNetObj_PlayerInput *pTrackedInput = ForcePreview ? nullptr : GetTrackedInput();
	const CNetObj_Character *pTrackedCharacter = HasTrackedPlayer && GameClient()->m_Snap.m_aCharacters[TrackedClientId].m_Active ?
							     &GameClient()->m_Snap.m_aCharacters[TrackedClientId].m_Cur :
							     nullptr;
	const CNetObj_Character *pPrevTrackedCharacter = HasTrackedPlayer && GameClient()->m_Snap.m_aCharacters[TrackedClientId].m_Active ?
								 &GameClient()->m_Snap.m_aCharacters[TrackedClientId].m_Prev :
								 nullptr;

	if(IsKeystrokesDdnetStyle())
	{
		const int64_t Now = time_get();
		const int64_t HighlightDuration = time_freq() * KEYSTROKES_WHEEL_HIGHLIGHT_MS / 1000;
		if(!ForcePreview && HasTrackedPlayer && pTrackedInput == nullptr && pTrackedCharacter != nullptr && pPrevTrackedCharacter != nullptr &&
			pPrevTrackedCharacter->m_AttackTick != pTrackedCharacter->m_AttackTick)
		{
			m_Mouse1EndTime = Now + HighlightDuration;
		}

		bool aActive[KEYSTROKES_DDNET_NUM_KEYS] = {};
		if(ForcePreview)
		{
			aActive[2] = true; // FIRE
			aActive[3] = true; // HOOK
		}
		else
		{
			for(int i = 0; i < KEYSTROKES_DDNET_NUM_KEYS; ++i)
			{
				const auto Action = gs_aKeystrokesDdnetKeys[i].m_Action;
				aActive[i] = HasTrackedPlayer ?
						     IsKeystrokesMcActionActiveTracked(Action, pTrackedInput, pPrevTrackedCharacter, pTrackedCharacter, Now, m_Mouse1EndTime) :
						     IsKeystrokesMcActionActiveLocal(GameClient(), Action);
			}
		}

		DrawKeystrokesDdnetOverlay(Graphics(), TextRender(), Rect, Scale, aActive);
		return;
	}

	if(IsKeystrokesMinecraftStyle())
	{
		const int64_t Now = time_get();
		const int64_t HighlightDuration = time_freq() * KEYSTROKES_WHEEL_HIGHLIGHT_MS / 1000;
		if(!ForcePreview && HasTrackedPlayer && pTrackedInput == nullptr && pTrackedCharacter != nullptr && pPrevTrackedCharacter != nullptr &&
			pPrevTrackedCharacter->m_AttackTick != pTrackedCharacter->m_AttackTick)
		{
			m_Mouse1EndTime = Now + HighlightDuration;
		}

		const auto &McLayout = GetCachedKeystrokesMcLayout(&GameClient()->m_Binds);
		for(int i = 0; i < McLayout.m_NumKeys; ++i)
		{
			const auto &Key = McLayout.m_aKeys[i];
			bool Active = false;
			if(!ForcePreview)
			{
				Active = HasTrackedPlayer ?
						 IsKeystrokesMcActionActiveTracked(Key.m_Action, pTrackedInput, pPrevTrackedCharacter, pTrackedCharacter, Now, m_Mouse1EndTime) :
						 IsKeystrokesMcActionActiveLocal(GameClient(), Key.m_Action);
			}

			DrawKeystrokesMcKey(
				Graphics(),
				TextRender(),
				Rect.x + Key.m_X * Scale,
				Rect.y + Key.m_Y * Scale,
				Key.m_W * Scale,
				Key.m_H * Scale,
				Active,
				Key.m_ShowLabel ? Key.m_aLabel : nullptr);
		}
		return;
	}

	if(!m_KeyboardTexture.IsValid() || m_KeyboardTexture.IsNullTexture())
		return;

	const auto &Preset = GetKeystrokesKeyboardPreset(g_Config.m_AcKeystrokesKeyboardPreset);
	Graphics()->TextureSet(m_KeyboardTexture);
	Graphics()->QuadsBegin();
	for(int i = 0; i < Preset.m_NumElements; ++i)
	{
		const auto &Element = Preset.m_pElements[i];
		const bool Active = !ForcePreview && (pTrackedCharacter != nullptr ?
								     (pTrackedInput != nullptr ?
										     IsKeystrokesPressed(pTrackedInput, Element.m_KeyPrimary, Element.m_KeySecondary) :
										     IsKeystrokesPressed(pTrackedCharacter, Element.m_KeyPrimary, Element.m_KeySecondary)) :
								     IsKeystrokesPressed(Input(), Element.m_KeyPrimary, Element.m_KeySecondary));
		int MapY = Element.m_MapY;
		if(Active && Preset.m_PressedOffsetY > 0)
		{
			const int Candidate = MapY + Element.m_MapH + KEYSTROKES_PRESSED_TEXTURE_SPACE;
			if(Candidate + Element.m_MapH <= Preset.m_AtlasHeight)
				MapY = Candidate;
		}

		DrawKeystrokesSpriteQuad(
			Graphics(),
			Preset.m_AtlasWidth,
			Preset.m_AtlasHeight,
			Element.m_MapX,
			MapY,
			Element.m_MapW,
			Element.m_MapH,
			Rect.x + Element.m_PosX * Scale,
			Rect.y + Element.m_PosY * Scale,
			Element.m_MapW * Scale,
			Element.m_MapH * Scale);
	}
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->QuadsEnd();
}

void CKeystrokes::RenderMouseInternal(bool ForcePreview, bool IgnoreModuleEnabled)
{
	if(IsKeystrokesCombinedStyle())
		return;

	const CUIRect Rect = GetMouseRectInternal(IgnoreModuleEnabled);
	if(Rect.w <= 0.0f || Rect.h <= 0.0f)
		return;

	const float HudHeight = HudLayout::CANVAS_HEIGHT;
	const float HudWidth = HudHeight * Graphics()->ScreenAspect();
	const auto Layout = HudLayout::Get(HudLayout::MODULE_KEYSTROKES_MOUSE, HudWidth, HudHeight);
	const auto &Preset = GetKeystrokesMousePreset(g_Config.m_AcKeystrokesMousePreset);
	const float Scale = GetKeystrokesScale(Layout);
	const int64_t Now = time_get();
	const int64_t HighlightDuration = time_freq() * KEYSTROKES_WHEEL_HIGHLIGHT_MS / 1000;
	const int TrackedClientId = ForcePreview ? -1 : GetTrackedClientId();
	const bool HasTrackedPlayer = TrackedClientId >= 0;
	const CNetObj_PlayerInput *pTrackedInput = ForcePreview ? nullptr : GetTrackedInput();
	const CNetObj_Character *pTrackedCharacter = HasTrackedPlayer && GameClient()->m_Snap.m_aCharacters[TrackedClientId].m_Active ?
							     &GameClient()->m_Snap.m_aCharacters[TrackedClientId].m_Cur :
							     nullptr;
	const CNetObj_Character *pPrevTrackedCharacter = HasTrackedPlayer && GameClient()->m_Snap.m_aCharacters[TrackedClientId].m_Active ?
								 &GameClient()->m_Snap.m_aCharacters[TrackedClientId].m_Prev :
								 nullptr;
	if(!ForcePreview)
	{
		if(!HasTrackedPlayer && pTrackedInput == nullptr && Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
			m_WheelUpEndTime = Now + HighlightDuration;
		if(!HasTrackedPlayer && pTrackedInput == nullptr && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
			m_WheelDownEndTime = Now + HighlightDuration;
		if(HasTrackedPlayer && pTrackedInput == nullptr && pTrackedCharacter != nullptr && pPrevTrackedCharacter != nullptr &&
			pPrevTrackedCharacter->m_AttackTick != pTrackedCharacter->m_AttackTick)
		{
			m_Mouse1EndTime = Now + HighlightDuration;
		}
	}

	vec2 AimOffset(0.0f, 0.0f);
	float AimRotation = 0.0f;
	bool MouseMoved = false;
	if(!ForcePreview)
	{
		vec2 Aim(0.0f, 0.0f);
		if(pTrackedInput != nullptr)
			Aim = vec2((float)pTrackedInput->m_TargetX, (float)pTrackedInput->m_TargetY);
		else if(HasTrackedPlayer)
			GetKeystrokesTrackedAim(GameClient(), TrackedClientId, Client()->IntraGameTick(g_Config.m_ClDummy), Aim);
		else
			Aim = GameClient()->m_Controls.m_aMousePos[g_Config.m_ClDummy];
		const float MaxDistance = std::max(GameClient()->m_Controls.GetMaxMouseDistance(), 0.001f);
		float Length = length(Aim);
		if(Length > 0.001f)
		{
			MouseMoved = true;
			AimRotation = std::atan2(Aim.y, Aim.x) + pi / 2.0f;
			Aim /= MaxDistance;
			Length = length(Aim);
			if(Length > 1.0f)
				Aim /= Length;
			AimOffset = Aim;
		}
	}

	if(!m_MouseTexture.IsValid() || m_MouseTexture.IsNullTexture())
		return;

	Graphics()->TextureSet(m_MouseTexture);
	Graphics()->QuadsBegin();
	for(int i = 0; i < Preset.m_NumElements; ++i)
	{
		const auto &Element = Preset.m_pElements[i];
		bool Active = false;
		switch(Element.m_InputKind)
		{
		case EKeystrokesInputKind::NONE:
			Active = true;
			break;
		case EKeystrokesInputKind::KEY:
			Active = !ForcePreview && (HasTrackedPlayer ?
								  (pTrackedInput != nullptr ?
										  IsKeystrokesPressed(pTrackedInput, Element.m_KeyPrimary, Element.m_KeySecondary) :
										  false) :
								  IsKeystrokesPressed(Input(), Element.m_KeyPrimary, Element.m_KeySecondary));
			break;
		case EKeystrokesInputKind::MOUSE_BUTTON:
			Active = !ForcePreview && (HasTrackedPlayer ?
								  (pTrackedInput != nullptr ?
										  IsKeystrokesMouseButtonPressed(pTrackedInput, Element.m_MouseButton) :
										  IsKeystrokesMouseButtonPressedFromCharacter(pPrevTrackedCharacter, pTrackedCharacter, Element.m_MouseButton, Now, m_Mouse1EndTime)) :
								  IsKeystrokesMouseButtonPressed(Input(), Element.m_MouseButton));
			break;
		case EKeystrokesInputKind::WHEEL:
			Active = !ForcePreview && IsKeystrokesWheelActive(Element.m_WheelDir, Now, m_WheelUpEndTime, m_WheelDownEndTime);
			break;
		case EKeystrokesInputKind::MOUSE_MOVE:
			Active = !ForcePreview && MouseMoved;
			break;
		}

		if(Element.m_ActiveOnly && !Active)
			continue;
		if(Element.m_InputKind == EKeystrokesInputKind::WHEEL && !Active)
			continue;

		int MapY = Element.m_MapY;
		if(Active && !Element.m_ActiveOnly &&
			((Element.m_InputKind == EKeystrokesInputKind::KEY && Preset.m_PressedOffsetY > 0) || Element.m_InputKind == EKeystrokesInputKind::MOUSE_BUTTON))
		{
			const int Candidate = MapY + Element.m_MapH + KEYSTROKES_PRESSED_TEXTURE_SPACE;
			if(Candidate + Element.m_MapH <= Preset.m_AtlasHeight)
				MapY = Candidate;
		}

		const float X = Rect.x + Element.m_PosX * Scale;
		const float Y = Rect.y + Element.m_PosY * Scale;
		vec2 Offset(0.0f, 0.0f);
		float Rotation = 0.0f;
		if(Element.m_InputKind == EKeystrokesInputKind::MOUSE_MOVE)
		{
			Offset = AimOffset * (Element.m_MouseRadius * Scale);
			if(Element.m_MouseType == 1)
				Rotation = AimRotation;
		}

		DrawKeystrokesSpriteQuad(
			Graphics(),
			Preset.m_AtlasWidth,
			Preset.m_AtlasHeight,
			Element.m_MapX,
			MapY,
			Element.m_MapW,
			Element.m_MapH,
			X + Offset.x,
			Y + Offset.y,
			Element.m_MapW * Scale,
			Element.m_MapH * Scale,
			ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f),
			Rotation);
	}
	Graphics()->QuadsSetRotation(0.0f);
	Graphics()->QuadsEnd();
}
