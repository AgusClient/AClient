#include "gradient.h"

#include <base/math.h>

#include <engine/shared/config.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>

#include <algorithm>
#include <cmath>

void (*ACGradient_ApplyEverythingHook)(CTextCursor *pCursor, const char *pText, int Length) = nullptr;

namespace
{
	static CAcGradient *s_pActiveGradient = nullptr;
	static CGameClient *s_pGameClient = nullptr;

	static ColorRGBA ConfigColor(unsigned ConfigValue)
	{
		return color_cast<ColorRGBA>(ColorHSLA(ConfigValue, true));
	}

	static int CollectCustomColors(ColorRGBA (&aColors)[4])
	{
		const int Count = std::clamp(g_Config.m_AcNameplateGradientColorCount, 2, 4);
		aColors[0] = ConfigColor(g_Config.m_AcNameplateGradientColor1);
		aColors[1] = ConfigColor(g_Config.m_AcNameplateGradientColor2);
		if(Count >= 3)
			aColors[2] = ConfigColor(g_Config.m_AcNameplateGradientColor3);
		if(Count >= 4)
			aColors[3] = ConfigColor(g_Config.m_AcNameplateGradientColor4);
		return Count;
	}

	static ColorRGBA SampleColorStops(const ColorRGBA *pColors, int Count, float Position)
	{
		if(Count <= 0)
			return ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		if(Count == 1)
			return pColors[0];

		float Wrapped = std::fmod(Position, 1.0f);
		if(Wrapped < 0.0f)
			Wrapped += 1.0f;

		const float Scaled = Wrapped * Count;
		const int Index = (int)Scaled % Count;
		const float LocalT = Scaled - std::floor(Scaled);
		const ColorRGBA &A = pColors[Index];
		const ColorRGBA &B = pColors[(Index + 1) % Count];
		return ColorRGBA(
			A.r + LocalT * (B.r - A.r),
			A.g + LocalT * (B.g - A.g),
			A.b + LocalT * (B.b - A.b),
			1.0f);
	}

	template<typename FSample>
	static void AppendMultiColorSplits(const char *pText, std::vector<STextColorSplit> &vOut, int CharOffset, bool Clear, FSample &&SampleColor)
	{
		if(Clear)
			vOut.clear();

		size_t Size = 0;
		size_t Count = 0;
		str_utf8_stats(pText, str_length(pText) + 1, SIZE_MAX, &Size, &Count);
		if(Count == 0)
			return;

		vOut.reserve(vOut.size() + Count);
		const char *pStr = pText;
		const float InvCountMinusOne = Count > 1 ? 1.0f / (float)(Count - 1) : 0.0f;
		for(size_t i = 0; i < Count; i++)
		{
			const int ByteOffset = (int)(pStr - pText);
			const char *pPrev = pStr;
			str_utf8_decode(&pStr);
			const int ByteLen = (int)(pStr - pPrev);
			const float t = Count == 1 ? 0.5f : (float)i * InvCountMinusOne;
			vOut.emplace_back(CharOffset + ByteOffset, ByteLen, SampleColor(t));
		}
	}
} // namespace

bool CAcGradient::AppliesTo(int ClientId, const CGameClient *pGameClient)
{
	const int Target = g_Config.m_AcNameplateGradientTarget;
	if(Target == AC_GRADIENT_TARGET_ALL || ClientId < 0 || ClientId >= MAX_CLIENTS)
		return true;

	bool Own = false;
	if(pGameClient->Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		Own = ClientId == (pGameClient->m_Snap.m_SpecInfo.m_Active ? pGameClient->m_Snap.m_SpecInfo.m_SpectatorId : pGameClient->m_Snap.m_LocalClientId);
	}
	else
	{
		for(const int LocalId : pGameClient->m_aLocalIds)
		{
			if(ClientId == LocalId)
				Own = true;
		}
	}

	return Own == (Target == AC_GRADIENT_TARGET_OWN);
}

float CAcGradient::AnimatePhase(double GlobalTime)
{
	return (float)std::fmod(GlobalTime * (g_Config.m_AcNameplateGradientAnimateSpeed / 100.0), 1.0);
}

ColorRGBA CAcGradient::SampleRainbow(float Hue)
{
	return color_cast<ColorRGBA>(ColorHSLA(std::fmod(Hue, 1.0f), 1.0f, 0.5f));
}

void CAcGradient::GetSkinToneColors(int ClientId, const CGameClient *pGameClient, ColorRGBA &Body, ColorRGBA &Feet)
{
	Body = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	Feet = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	if(ClientId < 0 || ClientId >= MAX_CLIENTS)
		return;

	const CAcGradient *pGradient = &pGameClient->m_AcGradient;
	if(g_Config.m_AcNameplateGradientSkin && pGradient->m_aHasOriginal[ClientId])
	{
		Body = pGradient->m_aOriginalBody[ClientId];
		Feet = pGradient->m_aOriginalFeet[ClientId];
		return;
	}

	const auto &RenderInfo = pGameClient->m_aClients[ClientId].m_RenderInfo;
	if(RenderInfo.m_CustomColoredSkin)
	{
		Body = RenderInfo.m_ColorBody;
		Feet = RenderInfo.m_ColorFeet;
	}
	else
	{
		Body = RenderInfo.m_BloodColor;
		Feet = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
	}
}

void CAcGradient::GetAnimatedEndpointColors(int ClientId, const CGameClient *pGameClient, float Phase, ColorRGBA &Color1, ColorRGBA &Color2)
{
	switch(g_Config.m_AcNameplateGradientMode)
	{
	case AC_GRADIENT_MODE_CUSTOM:
	{
		ColorRGBA aColors[4];
		const int Count = CollectCustomColors(aColors);
		Color1 = SampleColorStops(aColors, Count, Phase);
		Color2 = SampleColorStops(aColors, Count, std::fmod(Phase + 0.5f, 1.0f));
		break;
	}
	case AC_GRADIENT_MODE_RAINBOW:
		Color1 = SampleRainbow(Phase);
		Color2 = SampleRainbow(Phase + 0.33f);
		break;
	default:
	{
		ColorRGBA Body, Feet;
		GetSkinToneColors(ClientId, pGameClient, Body, Feet);
		const float Wave = 0.5f + 0.5f * std::cos(2.0f * pi * Phase);
		Color1 = ColorRGBA(
			Body.r + Wave * (Feet.r - Body.r),
			Body.g + Wave * (Feet.g - Body.g),
			Body.b + Wave * (Feet.b - Body.b),
			1.0f);
		Color2 = Feet;
		break;
	}
	}
}

void CAcGradient::AppendAnimatedColorSplits(const char *pText, const ColorRGBA &Color1, const ColorRGBA &Color2, float Phase, std::vector<STextColorSplit> &vOut, int CharOffset, bool Clear)
{
	AppendMultiColorSplits(pText, vOut, CharOffset, Clear, [&](float t) {
		const float Wave = 0.5f + 0.5f * std::cos(2.0f * pi * (t - Phase));
		return ColorRGBA(
			Color1.r + Wave * (Color2.r - Color1.r),
			Color1.g + Wave * (Color2.g - Color1.g),
			Color1.b + Wave * (Color2.b - Color1.b),
			1.0f);
	});
}

void CAcGradient::AppendAnimatedTextSplits(const char *pText, int ClientId, CGameClient *pGameClient, float Phase, std::vector<STextColorSplit> &vOut, int CharOffset, bool Clear)
{
	switch(g_Config.m_AcNameplateGradientMode)
	{
	case AC_GRADIENT_MODE_CUSTOM:
	{
		ColorRGBA aColors[4];
		const int Count = CollectCustomColors(aColors);
		AppendMultiColorSplits(pText, vOut, CharOffset, Clear, [&](float t) {
			const float Shifted = std::fmod(t - Phase + 1.0f, 1.0f);
			return SampleColorStops(aColors, Count, Shifted);
		});
		break;
	}
	case AC_GRADIENT_MODE_RAINBOW:
		AppendMultiColorSplits(pText, vOut, CharOffset, Clear, [&](float t) {
			return SampleRainbow(t * 0.75f + Phase);
		});
		break;
	default:
	{
		ColorRGBA Body, Feet;
		GetSkinToneColors(ClientId, pGameClient, Body, Feet);
		AppendAnimatedColorSplits(pText, Body, Feet, Phase, vOut, CharOffset, Clear);
		break;
	}
	}
}

void CAcGradient::ApplyEverythingGradient(CTextCursor *pCursor, const char *pText, int Length, CGameClient *pGameClient)
{
	if(!g_Config.m_AcNameplateGradientEverything || pCursor == nullptr || pText == nullptr || !pCursor->m_vColorSplits.empty())
		return;

	char aText[512];
	if(Length < 0)
		Length = str_length(pText);
	Length = std::min(Length, (int)sizeof(aText) - 1);
	str_copy(aText, pText, Length + 1);

	const int LocalId = pGameClient->m_Snap.m_LocalClientId;
	const float Phase = AnimatePhase(pGameClient->Client()->GlobalTime());
	const int CharOffset = pCursor->m_CharCount;
	AppendAnimatedTextSplits(aText, LocalId, pGameClient, Phase, pCursor->m_vColorSplits, CharOffset, true);
}

static void GradientEverythingHook(CTextCursor *pCursor, const char *pText, int Length)
{
	if(s_pGameClient == nullptr)
		return;
	CAcGradient::ApplyEverythingGradient(pCursor, pText, Length, s_pGameClient);
}

void CAcGradient::OnInit()
{
	s_pActiveGradient = this;
	s_pGameClient = GameClient();
	ACGradient_ApplyEverythingHook = GradientEverythingHook;
}

void CAcGradient::OnShutdown()
{
	if(s_pActiveGradient == this)
	{
		s_pActiveGradient = nullptr;
		s_pGameClient = nullptr;
		ACGradient_ApplyEverythingHook = nullptr;
	}
}

void CAcGradient::RefreshCachedText()
{
	Ui()->OnElementsReset();
	GameClient()->m_Chat.RebuildChat();
	GameClient()->m_NamePlates.ResetNamePlates();
	GameClient()->m_Hud.ResetHudContainers();
	GameClient()->m_Scoreboard.ResetTexts();
	GameClient()->m_InfoMessages.OnWindowResize();
	GameClient()->m_Broadcast.OnWindowResize();
}

void CAcGradient::OnRender()
{
	const int Everything = g_Config.m_AcNameplateGradientEverything;
	const int Nick = g_Config.m_AcNameplateGradient;
	const int Clan = g_Config.m_AcNameplateGradientClan;
	const int Target = g_Config.m_AcNameplateGradientTarget;
	const int Mode = g_Config.m_AcNameplateGradientMode;
	const int ColorCount = g_Config.m_AcNameplateGradientColorCount;
	const int AnimateSpeed = g_Config.m_AcNameplateGradientAnimateSpeed;
	const unsigned CfgColor1 = g_Config.m_AcNameplateGradientColor1;
	const unsigned CfgColor2 = g_Config.m_AcNameplateGradientColor2;
	const unsigned CfgColor3 = g_Config.m_AcNameplateGradientColor3;
	const unsigned CfgColor4 = g_Config.m_AcNameplateGradientColor4;

	bool NeedRefresh = false;
	if(m_LastEverything >= 0)
	{
		if(Everything != m_LastEverything || Nick != m_LastNick || Clan != m_LastClan || Target != m_LastTarget)
			NeedRefresh = true;
		else if(Everything && (Mode != m_LastMode || ColorCount != m_LastColorCount || AnimateSpeed != m_LastAnimateSpeed || CfgColor1 != m_LastColor1 || CfgColor2 != m_LastColor2 || CfgColor3 != m_LastColor3 || CfgColor4 != m_LastColor4))
			NeedRefresh = true;
	}

	m_LastEverything = Everything;
	m_LastNick = Nick;
	m_LastClan = Clan;
	m_LastTarget = Target;
	m_LastMode = Mode;
	m_LastColorCount = ColorCount;
	m_LastAnimateSpeed = AnimateSpeed;
	m_LastColor1 = CfgColor1;
	m_LastColor2 = CfgColor2;
	m_LastColor3 = CfgColor3;
	m_LastColor4 = CfgColor4;

	if(NeedRefresh)
		RefreshCachedText();

	if(!g_Config.m_AcNameplateGradientSkin)
	{
		if(m_HadSkinGradient)
		{
			std::fill(std::begin(m_aHasOriginal), std::end(m_aHasOriginal), false);
			m_HadSkinGradient = false;
		}
		return;
	}

	m_HadSkinGradient = true;
	std::fill(std::begin(m_aHasOriginal), std::end(m_aHasOriginal), false);

	const float Phase = AnimatePhase(Client()->GlobalTime());
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameClient()->m_Snap.m_aCharacters[i].m_Active || !AppliesTo(i, GameClient()))
			continue;

		GameClient()->m_aClients[i].UpdateRenderInfo();
		const auto &BaseInfo = GameClient()->m_aClients[i].m_RenderInfo;
		if(BaseInfo.m_CustomColoredSkin)
		{
			m_aOriginalBody[i] = BaseInfo.m_ColorBody;
			m_aOriginalFeet[i] = BaseInfo.m_ColorFeet;
		}
		else
		{
			m_aOriginalBody[i] = BaseInfo.m_BloodColor;
			m_aOriginalFeet[i] = ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f);
		}
		m_aHasOriginal[i] = true;

		CTeeRenderInfo *pRenderInfo = &GameClient()->m_aClients[i].m_RenderInfo;
		ColorRGBA Endpoint1, Endpoint2;
		GetAnimatedEndpointColors(i, GameClient(), Phase, Endpoint1, Endpoint2);
		pRenderInfo->m_ColorBody = Endpoint1;
		pRenderInfo->m_ColorFeet = Endpoint2;
		pRenderInfo->m_CustomColoredSkin = true;
	}
}
