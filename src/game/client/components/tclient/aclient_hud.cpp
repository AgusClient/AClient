#include "aclient_hud.h"

#include <engine/keys.h>
#include <engine/textrender.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cmath>

void CAClientHud::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

#if defined(CONF_VIDEORECORDER)
	if(!((IVideo::Current() && g_Config.m_ClVideoShowhud) || (!IVideo::Current() && g_Config.m_ClShowhud)))
		return;
#else
	if(!g_Config.m_ClShowhud)
		return;
#endif

	if(g_Config.m_AcHudKeystrokes)
		RenderKeystrokes();

	if(g_Config.m_AcHudSpeedometer)
		RenderSpeedometer();
}

void CAClientHud::DrawKey(float X, float Y, float W, float H, float Rounding, bool Active, const char *pLabel, float Alpha)
{
	if(W <= 0.0f || H <= 0.0f)
		return;

	CUIRect Rect(X, Y, W, H);
	ColorRGBA BgColor;
	ColorRGBA TextColor;
	const ColorRGBA Accent = ColorRGBA(0.0f, 0.75f, 1.0f, 1.0f);

	if(Active)
	{
		BgColor = Accent.WithAlpha(Alpha * 0.85f);
		TextColor = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	}
	else
	{
		BgColor = ColorRGBA(0.08f, 0.10f, 0.14f, Alpha * 0.65f);
		TextColor = ColorRGBA(0.85f, 0.88f, 0.92f, Alpha * 0.75f);
	}

	Rect.Draw(BgColor, IGraphics::CORNER_ALL, Rounding);

	if(pLabel && pLabel[0])
	{
		const float TextSize = std::max(6.0f, H * 0.52f);
		TextRender()->TextColor(TextColor);
		Ui()->DoLabel(&Rect, pLabel, TextSize, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
}

void CAClientHud::RenderKeystrokes()
{
	const float HudHeight = 300.0f;
	const float HudWidth = HudHeight * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(HudWidth, HudHeight);

	const float Scale = std::clamp(g_Config.m_AcHudKeystrokesScale / 100.0f, 0.5f, 2.0f);
	const float Alpha = std::clamp(g_Config.m_AcHudKeystrokesAlpha / 100.0f, 0.2f, 1.0f);

	bool PressLeft = false;
	bool PressRight = false;
	bool PressUp = false;
	bool PressDown = false;
	bool PressJump = false;
	bool PressFire = false;
	bool PressHook = false;

	int TrackedId = GameClient()->m_Snap.m_LocalClientId;
	if(GameClient()->m_Snap.m_SpecInfo.m_Active && in_range(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId, 0, MAX_CLIENTS - 1))
		TrackedId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;

	if(TrackedId == GameClient()->m_Snap.m_LocalClientId)
	{
		const CNetObj_PlayerInput &Input = GameClient()->m_Controls.m_aInputData[g_Config.m_ClDummy];
		PressLeft = Input.m_Direction < 0 || GameClient()->Input()->KeyIsPressed(KEY_A) || GameClient()->Input()->KeyIsPressed(KEY_LEFT);
		PressRight = Input.m_Direction > 0 || GameClient()->Input()->KeyIsPressed(KEY_D) || GameClient()->Input()->KeyIsPressed(KEY_RIGHT);
		PressUp = GameClient()->Input()->KeyIsPressed(KEY_W) || GameClient()->Input()->KeyIsPressed(KEY_UP);
		PressDown = GameClient()->Input()->KeyIsPressed(KEY_S) || GameClient()->Input()->KeyIsPressed(KEY_DOWN);
		PressJump = Input.m_Jump != 0 || GameClient()->Input()->KeyIsPressed(KEY_SPACE);
		PressFire = (Input.m_Fire & 1) != 0 || GameClient()->Input()->KeyIsPressed(KEY_MOUSE_1);
		PressHook = Input.m_Hook != 0 || GameClient()->Input()->KeyIsPressed(KEY_MOUSE_2);
	}
	else if(in_range(TrackedId, 0, MAX_CLIENTS - 1) && GameClient()->m_Snap.m_aCharacters[TrackedId].m_Active)
	{
		const auto &Char = GameClient()->m_Snap.m_aCharacters[TrackedId].m_Cur;
		const auto &PrevChar = GameClient()->m_Snap.m_aCharacters[TrackedId].m_Prev;
		PressHook = Char.m_HookState != HOOK_IDLE;
		PressJump = (Char.m_Jumped & 1) != 0;
		PressFire = PrevChar.m_AttackTick != Char.m_AttackTick;
		if(Char.m_VelX < -80)
			PressLeft = true;
		else if(Char.m_VelX > 80)
			PressRight = true;
	}

	const float Rounding = 3.0f * Scale;

	if(g_Config.m_AcHudKeystrokesLayout == 1)
	{
		// Compact Layout: A/D + Hook/Fire/Jump
		const float KeyW = 28.0f * Scale;
		const float KeyH = 18.0f * Scale;
		const float Gap = 3.0f * Scale;
		const float TotalW = KeyW * 2.0f + Gap;
		const float TotalH = KeyH * 3.0f + Gap * 2.0f;

		const float PosX = (g_Config.m_AcHudKeystrokesX / 100.0f) * (HudWidth - TotalW);
		const float PosY = (g_Config.m_AcHudKeystrokesY / 100.0f) * (HudHeight - TotalH);

		// Row 0: A, D
		DrawKey(PosX, PosY, KeyW, KeyH, Rounding, PressLeft, "A", Alpha);
		DrawKey(PosX + KeyW + Gap, PosY, KeyW, KeyH, Rounding, PressRight, "D", Alpha);

		// Row 1: LMB, RMB
		DrawKey(PosX, PosY + KeyH + Gap, KeyW, KeyH, Rounding, PressFire, "FIRE", Alpha);
		DrawKey(PosX + KeyW + Gap, PosY + KeyH + Gap, KeyW, KeyH, Rounding, PressHook, "HOOK", Alpha);

		// Row 2: SPACE (Jump)
		DrawKey(PosX, PosY + (KeyH + Gap) * 2.0f, TotalW, KeyH * 0.85f, Rounding, PressJump, "JUMP", Alpha);
	}
	else
	{
		// Standard Layout: W, A, S, D + LMB, RMB + SPACE
		const float KeySize = 18.0f * Scale;
		const float Gap = 2.5f * Scale;
		const float TotalW = KeySize * 3.0f + Gap * 2.0f;
		const float MouseH = 16.0f * Scale;
		const float SpaceH = 12.0f * Scale;
		const float TotalH = KeySize * 2.0f + MouseH + SpaceH + Gap * 3.0f;

		const float PosX = (g_Config.m_AcHudKeystrokesX / 100.0f) * (HudWidth - TotalW);
		const float PosY = (g_Config.m_AcHudKeystrokesY / 100.0f) * (HudHeight - TotalH);

		// Row 0: W (Center)
		DrawKey(PosX + KeySize + Gap, PosY, KeySize, KeySize, Rounding, PressUp, "W", Alpha);

		// Row 1: A, S, D
		DrawKey(PosX, PosY + KeySize + Gap, KeySize, KeySize, Rounding, PressLeft, "A", Alpha);
		DrawKey(PosX + KeySize + Gap, PosY + KeySize + Gap, KeySize, KeySize, Rounding, PressDown, "S", Alpha);
		DrawKey(PosX + (KeySize + Gap) * 2.0f, PosY + KeySize + Gap, KeySize, KeySize, Rounding, PressRight, "D", Alpha);

		// Row 2: LMB, RMB
		const float HalfMouseW = (TotalW - Gap) * 0.5f;
		const float MouseY = PosY + (KeySize + Gap) * 2.0f;
		DrawKey(PosX, MouseY, HalfMouseW, MouseH, Rounding, PressFire, "LMB", Alpha);
		DrawKey(PosX + HalfMouseW + Gap, MouseY, HalfMouseW, MouseH, Rounding, PressHook, "RMB", Alpha);

		// Row 3: SPACE
		const float SpaceY = MouseY + MouseH + Gap;
		DrawKey(PosX, SpaceY, TotalW, SpaceH, Rounding, PressJump, "SPACE", Alpha);
	}
}

void CAClientHud::RenderSpeedometer()
{
	int TrackedId = GameClient()->m_Snap.m_LocalClientId;
	if(GameClient()->m_Snap.m_SpecInfo.m_Active && in_range(GameClient()->m_Snap.m_SpecInfo.m_SpectatorId, 0, MAX_CLIENTS - 1))
		TrackedId = GameClient()->m_Snap.m_SpecInfo.m_SpectatorId;

	if(!in_range(TrackedId, 0, MAX_CLIENTS - 1) || !GameClient()->m_Snap.m_aCharacters[TrackedId].m_Active)
		return;

	const float HudHeight = 300.0f;
	const float HudWidth = HudHeight * Graphics()->ScreenAspect();
	Graphics()->MapScreenToSize(HudWidth, HudHeight);

	const float Scale = std::clamp(g_Config.m_AcHudSpeedometerScale / 100.0f, 0.5f, 2.0f);
	const float Alpha = std::clamp(g_Config.m_AcHudSpeedometerAlpha / 100.0f, 0.2f, 1.0f);

	const CNetObj_Character *pPrev = &GameClient()->m_Snap.m_aCharacters[TrackedId].m_Prev;
	const CNetObj_Character *pCur = &GameClient()->m_Snap.m_aCharacters[TrackedId].m_Cur;
	const float IntraTick = Client()->IntraGameTick(g_Config.m_ClDummy);
	const vec2 Vel = mix(vec2(pPrev->m_VelX, pPrev->m_VelY), vec2(pCur->m_VelX, pCur->m_VelY), IntraTick);
	const float SpeedUps = length(Vel / 256.0f) * Client()->GameTickSpeed() / 32.0f;

	const float BoxW = 75.0f * Scale;
	const float BoxH = 18.0f * Scale;
	const float PosX = (g_Config.m_AcHudSpeedometerX / 100.0f) * (HudWidth - BoxW);
	const float PosY = (g_Config.m_AcHudSpeedometerY / 100.0f) * (HudHeight - BoxH);

	CUIRect Rect(PosX, PosY, BoxW, BoxH);
	Rect.Draw(ColorRGBA(0.08f, 0.10f, 0.14f, Alpha * 0.70f), IGraphics::CORNER_ALL, 4.0f * Scale);

	char aBuf[32];
	str_format(aBuf, sizeof(aBuf), "%.1f ups", SpeedUps);

	const ColorRGBA SpeedColor = SpeedUps > 28.0f ? ColorRGBA(0.0f, 0.75f, 1.0f, 1.0f) : ColorRGBA(0.90f, 0.94f, 0.98f, Alpha * 0.95f);
	TextRender()->TextColor(SpeedColor);
	Ui()->DoLabel(&Rect, aBuf, BoxH * 0.58f, TEXTALIGN_MC);
	TextRender()->TextColor(TextRender()->DefaultTextColor());
}
