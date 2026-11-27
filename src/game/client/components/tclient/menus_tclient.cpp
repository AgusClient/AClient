#include <base/log.h>
#include <base/math.h>
#include <base/io.h>
#include <base/time.h>
#include <base/types.h>

#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/linereader.h>
#include <engine/shared/localization.h>
#include <engine/shared/protocol7.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <engine/updater.h>
#include <engine/image.h>
#include <engine/gfx/image_loader.h>

#include <game/version.h>
#include <game/client/animstate.h>
#include <game/client/components/binds.h>
#include <game/client/components/chat.h>
#include <game/client/components/countryflags.h>
#include <game/client/components/menu_background.h>
#include <game/client/components/menus.h>
#include <game/client/components/skins.h>
#include <game/client/components/sounds.h>
#include <game/client/components/tclient/bindchat.h>
#include <game/client/components/tclient/bindwheel.h>
#include <game/client/components/tclient/hud_layout.h>
#include <game/client/components/tclient/media_decoder.h>
#include <game/client/components/tclient/race_stats.h>
#include <game/client/components/tclient/trails.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/client/skin.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/client/ui_scrollregion.h>
#include <game/localization.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

#include "chess_assets.inl"

enum
{
	TCLIENT_TAB_SETTINGS = 0,
	TCLIENT_TAB_BINDWHEEL,
	TCLIENT_TAB_WARLIST,
	TCLIENT_TAB_BINDCHAT,
	TCLIENT_TAB_STATUSBAR,
	TCLIENT_TAB_RACESTATS,
	TCLIENT_TAB_INFO,
	NUMBER_OF_TCLIENT_TABS
};

typedef struct
{
	const char *m_pName;
	const char *m_pCommand;
	int m_KeyId;
	int m_ModifierCombination;
} CKeyInfo;

static float s_Time = 0.0f;
static bool s_StartedTime = false;

const float FontSize = 14.0f;
const float EditBoxFontSize = 12.0f;
const float LineSize = 20.0f;
const float ColorPickerLineSize = 25.0f;
const float HeadlineFontSize = 20.0f;
const float StandardFontSize = 14.0f;

const float HeadlineHeight = HeadlineFontSize + 0.0f;
const float Margin = 10.0f;
const float MarginSmall = 5.0f;
const float MarginExtraSmall = 2.5f;
const float MarginBetweenSections = 30.0f;
const float MarginBetweenViews = 30.0f;

const float ColorPickerLabelSize = 13.0f;
const float ColorPickerLineSpacing = 5.0f;

static void SetFlag(int32_t &Flags, int n, bool Value)
{
	if(Value)
		Flags |= (1 << n);
	else
		Flags &= ~(1 << n);
}

static bool IsFlagSet(int32_t Flags, int n)
{
	return (Flags & (1 << n)) != 0;
}

bool CMenus::DoLine_KeyReader(CUIRect &View, CButtonContainer &ReaderButton, CButtonContainer &ClearButton, const char *pName, const char *pCommand)
{
	CBindSlot Bind(0, 0);
	for(int Mod = 0; Mod < KeyModifier::COMBINATION_COUNT; Mod++)
	{
		for(int KeyId = 0; KeyId < KEY_LAST; KeyId++)
		{
			const char *pBind = GameClient()->m_Binds.Get(KeyId, Mod);
			if(!pBind[0])
				continue;

			if(str_comp(pBind, pCommand) == 0)
			{
				Bind.m_Key = KeyId;
				Bind.m_ModifierMask = Mod;
				break;
			}
		}
	}

	CUIRect KeyButton, KeyLabel;
	View.HSplitTop(LineSize, &KeyButton, &View);
	KeyButton.VSplitMid(&KeyLabel, &KeyButton);

	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "%s:", pName);
	Ui()->DoLabel(&KeyLabel, aBuf, FontSize, TEXTALIGN_ML);

	View.HSplitTop(MarginExtraSmall, nullptr, &View);

	const auto Result = GameClient()->m_KeyBinder.DoKeyReader(&ReaderButton, &ClearButton, &KeyButton, Bind, false);
	if(Result.m_Bind != Bind)
	{
		if(Bind.m_Key != KEY_UNKNOWN)
			GameClient()->m_Binds.Bind(Bind.m_Key, "", false, Bind.m_ModifierMask);
		if(Result.m_Bind.m_Key != KEY_UNKNOWN)
			GameClient()->m_Binds.Bind(Result.m_Bind.m_Key, pCommand, false, Result.m_Bind.m_ModifierMask);
		return true;
	}
	return false;
}

void CMenus::DoTickAmountSlider(int *pValue, const CUIRect *pRect, const char *pLabel, int Min, int Max, int Scale)
{
	CUIRect Button = *pRect;
	int Value = std::clamp(*pValue, Min, Max);

	const int Increment = std::max(1, (Max - Min) / 35);
	if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_UP) && Ui()->MouseInside(&Button))
		Value = std::clamp(Value + Increment, Min, Max);
	if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) && Ui()->MouseInside(&Button))
		Value = std::clamp(Value - Increment, Min, Max);

	char aBuf[256];
	if(Scale == 1000)
		str_format(aBuf, sizeof(aBuf), "%s: %.3f %s", pLabel, Value / (float)Scale, TCLocalize("ticks"));
	else
		str_format(aBuf, sizeof(aBuf), "%s: %.2f %s", pLabel, Value / (float)Scale, TCLocalize("ticks"));

	CUIRect Label, ScrollBar;
	Button.VSplitMid(&Label, &ScrollBar, std::min(10.0f, Button.w * 0.05f));
	Ui()->DoLabel(&Label, aBuf, Label.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);

	const float Rel = (Value - Min) / (float)(Max - Min);
	const float NewRel = Ui()->DoScrollbarH(pValue, &ScrollBar, Rel);
	*pValue = (int)(Min + NewRel * (Max - Min) + 0.5f);
}

bool CMenus::DoSliderWithScaledValue(const void *pId, int *pOption, const CUIRect *pRect, const char *pStr, int Min, int Max, int Scale, const IScrollbarScale *pScale, unsigned Flags, const char *pSuffix)
{
	const bool NoClampValue = Flags & CUi::SCROLLBAR_OPTION_NOCLAMPVALUE;

	int Value = *pOption;
	Min /= Scale;
	Max /= Scale;
	// Allow adjustment of slider options when ctrl is pressed (to avoid scrolling, or accidentally adjusting the value)
	int Increment = std::max(1, (Max - Min) / 35);
	if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_UP) && Ui()->MouseInside(pRect))
	{
		Value += Increment;
		Value = std::clamp(Value, Min, Max);
	}
	if(Input()->ModifierIsPressed() && Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN) && Ui()->MouseInside(pRect))
	{
		Value -= Increment;
		Value = std::clamp(Value, Min, Max);
	}

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s: %i%s", pStr, Value * Scale, pSuffix);

	if(NoClampValue)
	{
		// Clamp the value internally for the scrollbar
		Value = std::clamp(Value, Min, Max);
	}

	CUIRect Label, ScrollBar;
	pRect->VSplitMid(&Label, &ScrollBar, std::min(10.0f, pRect->w * 0.05f));

	const float LabelFontSize = Label.h * CUi::ms_FontmodHeight * 0.8f;
	Ui()->DoLabel(&Label, aBuf, LabelFontSize, TEXTALIGN_ML);

	Value = pScale->ToAbsolute(Ui()->DoScrollbarH(pId, &ScrollBar, pScale->ToRelative(Value, Min, Max)), Min, Max);
	if(NoClampValue && ((Value == Min && *pOption < Min) || (Value == Max && *pOption > Max)))
	{
		Value = *pOption;
	}

	if(*pOption != Value)
	{
		*pOption = Value;
		return true;
	}
	return false;
}

bool CMenus::DoEditBoxWithLabel(CLineInput *LineInput, const CUIRect *pRect, const char *pLabel, const char *pDefault, char *pBuf, size_t BufSize)
{
	CUIRect Button, Label;
	pRect->VSplitLeft(210.0f, &Label, &Button);
	Ui()->DoLabel(&Label, pLabel, FontSize, TEXTALIGN_ML);
	LineInput->SetBuffer(pBuf, BufSize);
	LineInput->SetEmptyText(pDefault);
	return Ui()->DoEditBox(LineInput, &Button, EditBoxFontSize);
}

bool CMenus::DoEditBoxNumberWithLabel(CLineInputNumber *pLineInput, const CUIRect *pRect, const char *pLabel, int *pVal, int MinVal, int MaxVal)
{
	CUIRect Button, Label;
	pRect->VSplitLeft(210.0f, &Label, &Button);
	Ui()->DoLabel(&Label, pLabel, FontSize, TEXTALIGN_ML);
	if(pLineInput->IsEmpty() && *pVal > 0)
		pLineInput->SetInteger(*pVal);
	if(Ui()->DoEditBox(pLineInput, &Button, EditBoxFontSize))
	{
		int Val = pLineInput->GetInteger();
		if(Val >= MinVal && Val <= MaxVal)
			*pVal = Val;
		return true;
	}
	return false;
}

int CMenus::DoButtonLineSize_Menu(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, float ButtonLineSize, bool Fake, const char *pImageName, int Corners, float Rounding, float FontFactor, ColorRGBA Color)
{
	CUIRect Text = *pRect;

	if(Checked)
		Color = ColorRGBA(0.6f, 0.6f, 0.6f, 0.5f);
	Color.a *= Ui()->ButtonColorMul(pButtonContainer);

	if(Fake)
		Color.a *= 0.5f;

	pRect->Draw(Color, Corners, Rounding);

	Text.HMargin((Text.h - ButtonLineSize) / 2.0f, &Text);
	Text.HMargin(pRect->h >= 20.0f ? 2.0f : 1.0f, &Text);
	Text.HMargin((Text.h * FontFactor) / 2.0f, &Text);
	Ui()->DoLabel(&Text, pText, Text.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);

	if(Fake)
		return 0;

	return Ui()->DoButtonLogic(pButtonContainer, Checked, pRect, BUTTONFLAG_LEFT);
}

void CMenus::RenderDevSkin(vec2 RenderPos, float Size, const char *pSkinName, const char *pBackupSkin, bool CustomColors, int FeetColor, int BodyColor, int Emote, bool Rainbow, bool Cute, ColorRGBA ColorFeet, ColorRGBA ColorBody)
{
	bool WhiteFeetTemp = g_Config.m_TcWhiteFeet;
	g_Config.m_TcWhiteFeet = false;

	float DefTick = std::fmod(s_Time, 1.0f);

	CTeeRenderInfo SkinInfo;
	const CSkin *pSkin = GameClient()->m_Skins.Find(pSkinName);
	if(str_comp(pSkin->GetName(), pSkinName) != 0)
		pSkin = GameClient()->m_Skins.Find(pBackupSkin);

	SkinInfo.m_OriginalRenderSkin = pSkin->m_OriginalSkin;
	SkinInfo.m_ColorableRenderSkin = pSkin->m_ColorableSkin;
	SkinInfo.m_SkinMetrics = pSkin->m_Metrics;
	SkinInfo.m_CustomColoredSkin = CustomColors;
	if(SkinInfo.m_CustomColoredSkin)
	{
		SkinInfo.m_ColorBody = color_cast<ColorRGBA>(ColorHSLA(BodyColor).UnclampLighting(ColorHSLA::DARKEST_LGT));
		SkinInfo.m_ColorFeet = color_cast<ColorRGBA>(ColorHSLA(FeetColor).UnclampLighting(ColorHSLA::DARKEST_LGT));
		if(ColorFeet.a != 0.0f)
		{
			SkinInfo.m_ColorBody = ColorBody;
			SkinInfo.m_ColorFeet = ColorFeet;
		}
	}
	else
	{
		SkinInfo.m_ColorBody = ColorRGBA(1.0f, 1.0f, 1.0f);
		SkinInfo.m_ColorFeet = ColorRGBA(1.0f, 1.0f, 1.0f);
	}
	if(Rainbow)
	{
		ColorRGBA Col = color_cast<ColorRGBA>(ColorHSLA(DefTick, 1.0f, 0.5f));
		SkinInfo.m_ColorBody = Col;
		SkinInfo.m_ColorFeet = Col;
	}
	SkinInfo.m_Size = Size;
	const CAnimState *pIdleState = CAnimState::GetIdle();
	vec2 OffsetToMid;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &SkinInfo, OffsetToMid);
	vec2 TeeRenderPos(RenderPos.x, RenderPos.y + OffsetToMid.y);
	if(Cute)
		RenderTeeCute(pIdleState, &SkinInfo, Emote, vec2(1.0f, 0.0f), TeeRenderPos, true);
	else
		RenderTools()->RenderTee(pIdleState, &SkinInfo, Emote, vec2(1.0f, 0.0f), TeeRenderPos);
	g_Config.m_TcWhiteFeet = WhiteFeetTemp;
}

void CMenus::RenderTeeCute(const CAnimState *pAnim, const CTeeRenderInfo *pInfo, int Emote, vec2 Dir, vec2 Pos, bool CuteEyes, float Alpha)
{
	Dir = Ui()->MousePos() - Pos;
	if(pInfo->m_Size > 0.0f)
		Dir /= pInfo->m_Size;
	const float Length = length(Dir);
	if(Length > 1.0f)
		Dir /= Length;
	if(CuteEyes && Length < 0.4f)
		Emote = 2;
	RenderTools()->RenderTee(pAnim, pInfo, Emote, Dir, Pos, Alpha);
}

void CMenus::RenderFontIcon(const CUIRect Rect, const char *pText, float Size, int Align)
{
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
	Ui()->DoLabel(&Rect, pText, Size, Align);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
}

int CMenus::DoButtonNoRect_FontIcon(CButtonContainer *pButtonContainer, const char *pText, int Checked, const CUIRect *pRect, int Corners)
{
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
	TextRender()->TextOutlineColor(TextRender()->DefaultTextOutlineColor());
	TextRender()->TextColor(TextRender()->DefaultTextSelectionColor());
	if(Ui()->HotItem() == pButtonContainer)
	{
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	CUIRect Temp;
	pRect->HMargin(0.0f, &Temp);
	Ui()->DoLabel(&Temp, pText, Temp.h * CUi::ms_FontmodHeight, TEXTALIGN_MC);
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	return Ui()->DoButtonLogic(pButtonContainer, Checked, pRect, BUTTONFLAG_LEFT);
}

void CMenus::PopupConfirmRemoveWarType()
{
	GameClient()->m_WarList.RemoveWarType(m_pRemoveWarType->m_aWarName);
	m_pRemoveWarType = nullptr;
}

void CMenus::RenderSettingsTClient(CUIRect MainView)
{
	s_Time += Client()->RenderFrameTime() * (1.0f / 100.0f);
	if(!s_StartedTime)
	{
		s_StartedTime = true;
		s_Time = (float)rand() / (float)RAND_MAX;
	}

	static int s_CurCustomTab = 0;

	CUIRect TabBar, Button;
	int TabCount = NUMBER_OF_TCLIENT_TABS;
	for(int Tab = 0; Tab < NUMBER_OF_TCLIENT_TABS; ++Tab)
	{
		if(IsFlagSet(g_Config.m_TcTClientSettingsTabs, Tab))
		{
			TabCount--;
			if(s_CurCustomTab == Tab)
				s_CurCustomTab++;
		}
	}

	MainView.HSplitTop(LineSize, &TabBar, &MainView);
	const float TabWidth = TabBar.w / TabCount;
	static CButtonContainer s_aPageTabs[NUMBER_OF_TCLIENT_TABS] = {};
	const char *apTabNames[] = {
		TCLocalize("Settings"),
		TCLocalize("Bind Wheel"),
		TCLocalize("War List"),
		TCLocalize("Chat Binds"),
		TCLocalize("Status Bar"),
		TCLocalize("Race Stats"),
		TCLocalize("Info")};

	for(int Tab = 0; Tab < NUMBER_OF_TCLIENT_TABS; ++Tab)
	{
		if(IsFlagSet(g_Config.m_TcTClientSettingsTabs, Tab))
			continue;

		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = Tab == 0 ? IGraphics::CORNER_L : Tab == NUMBER_OF_TCLIENT_TABS - 1 ? IGraphics::CORNER_R :
													 IGraphics::CORNER_NONE;
		if(DoButton_MenuTab(&s_aPageTabs[Tab], apTabNames[Tab], s_CurCustomTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			s_CurCustomTab = Tab;
	}

	MainView.HSplitTop(Margin, nullptr, &MainView);

	if(s_CurCustomTab == TCLIENT_TAB_SETTINGS)
		RenderSettingsTClientSettings(MainView);
	if(s_CurCustomTab == TCLIENT_TAB_BINDCHAT)
		RenderSettingsTClientChatBinds(MainView);
	if(s_CurCustomTab == TCLIENT_TAB_BINDWHEEL)
		RenderSettingsTClientBindWheel(MainView);
	if(s_CurCustomTab == TCLIENT_TAB_WARLIST)
		RenderSettingsTClientWarList(MainView);
	if(s_CurCustomTab == TCLIENT_TAB_STATUSBAR)
		RenderSettingsTClientStatusBar(MainView);
	if(s_CurCustomTab == TCLIENT_TAB_RACESTATS)
		RenderSettingsTClientRaceStats(MainView);
	if(s_CurCustomTab == TCLIENT_TAB_INFO)
		RenderSettingsTClientInfo(MainView);
}

void CMenus::RenderSettingsTClientSettings(CUIRect MainView)
{
	CUIRect Column, LeftView, RightView, Button, Label;

	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_ForceShowScrollbar = true;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);

	static std::vector<CUIRect> s_SectionBoxes;
	const float ScrollOffset = MainView.y;
	static float s_PrevScrollOffset = 0.0f;

	MainView.VSplitRight(5.0f, &MainView, nullptr); // Padding for scrollbar
	MainView.VSplitLeft(5.0f, nullptr, &MainView); // Padding for scrollbar

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	for(CUIRect &Section : s_SectionBoxes)
	{
		float Padding = MarginBetweenViews * 0.6666f;
		Section.w += Padding;
		Section.h += Padding;
		Section.x -= Padding * 0.5f;
		Section.y -= Padding * 0.5f;
		Section.y -= s_PrevScrollOffset - ScrollOffset;
		if(Section.x < MainView.x + 1.0f)
		{
			float Diff = (MainView.x + 1.0f) - Section.x;
			Section.x += Diff;
			Section.w -= Diff;
		}
		if(Section.x + Section.w > MainView.x + MainView.w - 1.0f)
		{
			Section.w = (MainView.x + MainView.w - 1.0f) - Section.x;
		}
		if(g_Config.m_AcModernUi)
		{
			Section.Draw(GetColorBorder(0.40f), IGraphics::CORNER_ALL, 10.0f);
			CUIRect Inner = Section;
			Inner.Margin(1.0f, &Inner);
			Inner.Draw(GetColorBg(), IGraphics::CORNER_ALL, 9.0f);
		}
		else
		{
			Section.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
		}
	}
	s_PrevScrollOffset = ScrollOffset;
	s_SectionBoxes.clear();

	// ***** LeftView ***** //
	Column = LeftView;

	// ***** Visual Miscellaneous ***** //
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Visual"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static std::vector<const char *> s_FontDropDownNames = {};
	static CUi::SDropDownState s_FontDropDownState;
	static CScrollRegion s_FontDropDownScrollRegion;
	s_FontDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_FontDropDownScrollRegion;
	s_FontDropDownState.m_SelectionPopupContext.m_SpecialFontRenderMode = true;
	int FontSelectedOld = -1;
	for(size_t i = 0; i < TextRender()->GetCustomFaces()->size(); ++i)
	{
		if(s_FontDropDownNames.size() != TextRender()->GetCustomFaces()->size())
			s_FontDropDownNames.push_back(TextRender()->GetCustomFaces()->at(i).c_str());

		if(str_find_nocase(g_Config.m_TcCustomFont, TextRender()->GetCustomFaces()->at(i).c_str()))
			FontSelectedOld = i;
	}
	CUIRect FontDropDownRect, FontDirectory;
	Column.HSplitTop(LineSize, &FontDropDownRect, &Column);
	FontDropDownRect.VSplitLeft(100.0f, &Label, &FontDropDownRect);
	FontDropDownRect.VSplitRight(20.0f, &FontDropDownRect, &FontDirectory);
	FontDropDownRect.VSplitRight(MarginSmall, &FontDropDownRect, nullptr);

	Ui()->DoLabel(&Label, TCLocalize("Custom Font: "), FontSize, TEXTALIGN_ML);
	const int FontSelectedNew = Ui()->DoDropDown(&FontDropDownRect, FontSelectedOld, s_FontDropDownNames.data(), s_FontDropDownNames.size(), s_FontDropDownState);
	if(FontSelectedOld != FontSelectedNew)
	{
		str_copy(g_Config.m_TcCustomFont, s_FontDropDownNames[FontSelectedNew]);
		TextRender()->SetCustomFace(g_Config.m_TcCustomFont);

		// Attempt to reset all the containers
		TextRender()->OnPreWindowResize();
		GameClient()->OnWindowResize();
		GameClient()->Editor()->OnWindowResize();
		TextRender()->OnWindowResize();
		GameClient()->m_MapImages.SetTextureScale(101);
		GameClient()->m_MapImages.SetTextureScale(g_Config.m_ClTextEntitiesSize);
	}

	static CButtonContainer s_FontDirectoryId;
	if(Ui()->DoButton_FontIcon(&s_FontDirectoryId, FontIcon::FOLDER, 0, &FontDirectory, IGraphics::CORNER_ALL))
	{
		Storage()->CreateFolder("tclient", IStorage::TYPE_SAVE);
		Storage()->CreateFolder("tclient/fonts", IStorage::TYPE_SAVE);
		char aBuf[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "tclient/fonts", aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}

	CUIRect TinyTeeConfig;
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);

	{
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
		static std::vector<const char *> s_DropDownNames;
		s_DropDownNames = {TCLocalize("Normal", "Hammer Mode"), TCLocalize("Rotate with cursor", "Hammer Mode"), TCLocalize("Rotate with cursor like gun", "Hammer Mode")};
		static CUi::SDropDownState s_DropDownState;
		static CScrollRegion s_DropDownScrollRegion;
		s_DropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_DropDownScrollRegion;
		CUIRect DropDownRect;
		Column.HSplitTop(LineSize, &DropDownRect, &Column);
		DropDownRect.VSplitLeft(120.0f, &Label, &DropDownRect);
		Ui()->DoLabel(&Label, TCLocalize("Hammer Mode: "), FontSize, TEXTALIGN_ML);
		g_Config.m_TcHammerRotatesWithCursor = Ui()->DoDropDown(&DropDownRect, g_Config.m_TcHammerRotatesWithCursor, s_DropDownNames.data(), s_DropDownNames.size(), s_DropDownState);
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	}

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcCursorScale, &g_Config.m_TcCursorScale, &Button, TCLocalize("Ingame cursor scale"), 0, 500, &CUi::ms_LinearScrollbarScale, 0, "%");

	Column.HSplitTop(LineSize, &Button, &Column);
	if(g_Config.m_TcAnimateWheelTime > 0)
		Ui()->DoScrollbarOption(&g_Config.m_TcAnimateWheelTime, &g_Config.m_TcAnimateWheelTime, &Button, TCLocalize("Wheel animate"), 0, 1000, &CUi::ms_LinearScrollbarScale, 0, "ms");
	else
		Ui()->DoScrollbarOption(&g_Config.m_TcAnimateWheelTime, &g_Config.m_TcAnimateWheelTime, &Button, TCLocalize("Wheel animate"), 0, 1000, &CUi::ms_LinearScrollbarScale, 0, "ms (off)");

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNameplatePingCircle, TCLocalize("Show ping colored circle in nameplates"), &g_Config.m_TcNameplatePingCircle, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNameplateCountry, TCLocalize("Show country flags in nameplates"), &g_Config.m_TcNameplateCountry, &Column, LineSize);
	// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRenderNameplateSpec, TCLocalize("Hide nameplates in spec"), &g_Config.m_TcRenderNameplateSpec, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNameplateSkins, TCLocalize("Show skin names in nameplate"), &g_Config.m_TcNameplateSkins, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClFreezeStars, TCLocalize("Freeze stars"), &g_Config.m_ClFreezeStars, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcColorFreeze, TCLocalize("Colored frozen tee skins"), &g_Config.m_TcColorFreeze, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFrozenKatana, TCLocalize("Show katan on frozen players"), &g_Config.m_TcFrozenKatana, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRenderWeaponsAsGun, TCLocalize("Render weapons as the gun sprite"), &g_Config.m_TcRenderWeaponsAsGun, &Column, LineSize);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWhiteFeet, TCLocalize("Render all custom colored feet as white feet skin"), &g_Config.m_TcWhiteFeet, &Column, LineSize);
	CUIRect FeetBox;
	Column.HSplitTop(LineSize + MarginExtraSmall, &FeetBox, &Column);
	if(g_Config.m_TcWhiteFeet)
	{
		FeetBox.HSplitTop(MarginExtraSmall, nullptr, &FeetBox);
		FeetBox.VSplitMid(&FeetBox, nullptr);
		static CLineInput s_WhiteFeet(g_Config.m_TcWhiteFeetSkin, sizeof(g_Config.m_TcWhiteFeetSkin));
		s_WhiteFeet.SetEmptyText("x_ninja");
		Ui()->DoEditBox(&s_WhiteFeet, &FeetBox, EditBoxFontSize);
	}

	{
		static std::vector<CButtonContainer> s_vButtonContainers = {{}, {}, {}};
		int Value = g_Config.m_TcTinyTees ? (g_Config.m_TcTinyTeesOthers ? 2 : 1) : 0;
		if(DoLine_RadioMenu(Column, TCLocalize("Tiny Tees"),
			   s_vButtonContainers,
			   {Localize("None"), Localize("Own"), Localize("All")},
			   {0, 1, 2},
			   Value))
		{
			g_Config.m_TcTinyTees = Value > 0 ? 1 : 0;
			g_Config.m_TcTinyTeesOthers = Value > 1 ? 1 : 0;
		}
		Column.HSplitTop(LineSize, &TinyTeeConfig, &Column);
		if(g_Config.m_TcTinyTees > 0)
			Ui()->DoScrollbarOption(&g_Config.m_TcTinyTeeSize, &g_Config.m_TcTinyTeeSize, &TinyTeeConfig, TCLocalize("Tiny Tee Size"), 85, 115);
	}

	{
		static std::vector<CButtonContainer> s_vButtonContainers = {{}, {}, {}};
		int Value = g_Config.m_TcFakeCtfFlags;
		if(DoLine_RadioMenu(Column, TCLocalize("Fake CTF flags"),
			   s_vButtonContainers,
			   {Localize("None"), Localize("Red"), Localize("Blue")},
			   {0, 1, 2},
			   Value))
		{
			g_Config.m_TcFakeCtfFlags = Value;
		}
	}
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMovingTilesEntities, TCLocalize("Show moving tiles in entities"), &g_Config.m_TcMovingTilesEntities, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Anti Latency Tools ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Anti Latency Tools"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_ClPredictionMargin, &g_Config.m_ClPredictionMargin, &Button, TCLocalize("Prediction Margin"), 10, 75, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRemoveAnti, TCLocalize("Remove prediction & antiping in freeze"), &g_Config.m_TcRemoveAnti, &Column, LineSize);
	if(g_Config.m_TcRemoveAnti)
	{
		if(g_Config.m_TcUnfreezeLagDelayTicks < g_Config.m_TcUnfreezeLagTicks)
			g_Config.m_TcUnfreezeLagDelayTicks = g_Config.m_TcUnfreezeLagTicks;
		Column.HSplitTop(LineSize, &Button, &Column);
		DoSliderWithScaledValue(&g_Config.m_TcUnfreezeLagTicks, &g_Config.m_TcUnfreezeLagTicks, &Button, TCLocalize("Amount"), 100, 300, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
		Column.HSplitTop(LineSize, &Button, &Column);
		DoSliderWithScaledValue(&g_Config.m_TcUnfreezeLagDelayTicks, &g_Config.m_TcUnfreezeLagDelayTicks, &Button, TCLocalize("Delay"), 100, 3000, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
	}
	else
		Column.HSplitTop(LineSize * 2, nullptr, &Column);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcUnpredOthersInFreeze, TCLocalize("Dont predict other players if you are frozen"), &g_Config.m_TcUnpredOthersInFreeze, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPredMarginInFreeze, TCLocalize("Adjust your prediction margin while frozen"), &g_Config.m_TcPredMarginInFreeze, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	if(g_Config.m_TcPredMarginInFreeze)
		Ui()->DoScrollbarOption(&g_Config.m_TcPredMarginInFreezeAmount, &g_Config.m_TcPredMarginInFreezeAmount, &Button, TCLocalize("Frozen Margin"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "ms");
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Improved Anti Ping ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Anti Ping Smoothing"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAntiPingImproved, TCLocalize("Use new smoothing algorithm"), &g_Config.m_TcAntiPingImproved, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAntiPingStableDirection, TCLocalize("Optimistic prediction in stable direction"), &g_Config.m_TcAntiPingStableDirection, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAntiPingNegativeBuffer, TCLocalize("Remember instability for longer"), &g_Config.m_TcAntiPingNegativeBuffer, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcAntiPingUncertaintyScale, &g_Config.m_TcAntiPingUncertaintyScale, &Button, TCLocalize("Uncertainty duration"), 50, 400, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "%");
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Execute on join ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);

	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Auto execute"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	{
		CUIRect Box;
		Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
		Box.VSplitMid(&Label, &Button);
		Ui()->DoLabel(&Label, Localize("Execute before connect"), FontSize, TEXTALIGN_ML);
		static CLineInput s_LineInput(g_Config.m_TcExecuteOnConnect, sizeof(g_Config.m_TcExecuteOnConnect));
		s_LineInput.SetEmptyText(TCLocalize("Run a console command before connect"));

		Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize);
	}
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	{
		CUIRect Box;
		Column.HSplitTop(LineSize + MarginExtraSmall, &Box, &Column);
		Box.VSplitMid(&Label, &Button);
		Ui()->DoLabel(&Label, Localize("Execute on join"), FontSize, TEXTALIGN_ML);
		static CLineInput s_LineInput(g_Config.m_TcExecuteOnJoin, sizeof(g_Config.m_TcExecuteOnJoin));
		s_LineInput.SetEmptyText(TCLocalize("Run a console command on join"));

		Ui()->DoEditBox(&s_LineInput, &Button, EditBoxFontSize);
	}

	Column.HSplitTop(LineSize, &Button, &Column);
	DoSliderWithScaledValue(&g_Config.m_TcExecuteOnJoinDelay, &g_Config.m_TcExecuteOnJoinDelay, &Button, TCLocalize("Delay"), 140, 2000, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Voting ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Voting"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoVoteWhenFar, TCLocalize("Auto vote no to map changes when far"), &g_Config.m_TcAutoVoteWhenFar, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcAutoVoteWhenFarTime, &g_Config.m_TcAutoVoteWhenFarTime, &Button, TCLocalize("Minimum Time"), 1, 20, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, " minutes");

	CUIRect VoteMessage;
	Column.HSplitTop(LineSize + MarginExtraSmall, &VoteMessage, &Column);
	VoteMessage.HSplitTop(MarginExtraSmall, nullptr, &VoteMessage);
	VoteMessage.VSplitMid(&Label, &VoteMessage);
	Ui()->DoLabel(&Label, TCLocalize("Message to send in chat:"), FontSize, TEXTALIGN_ML);
	static CLineInput s_VoteMessage(g_Config.m_TcAutoVoteWhenFarMessage, sizeof(g_Config.m_TcAutoVoteWhenFarMessage));
	s_VoteMessage.SetEmptyText(TCLocalize("Leave empty to disable"));
	Ui()->DoEditBox(&s_VoteMessage, &VoteMessage, EditBoxFontSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Auto Reply ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Auto Reply"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoReplyMuted, TCLocalize("Auto reply to muted players"), &g_Config.m_TcAutoReplyMuted, &Column, LineSize);
	CUIRect MutedReply;
	Column.HSplitTop(LineSize + MarginExtraSmall, &MutedReply, &Column);
	if(g_Config.m_TcAutoReplyMuted)
	{
		MutedReply.HSplitTop(MarginExtraSmall, nullptr, &MutedReply);
		static CLineInput s_MutedReply(g_Config.m_TcAutoReplyMutedMessage, sizeof(g_Config.m_TcAutoReplyMutedMessage));
		s_MutedReply.SetEmptyText("I have muted you");
		Ui()->DoEditBox(&s_MutedReply, &MutedReply, EditBoxFontSize);
	}
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcAutoReplyMinimized, TCLocalize("Auto reply when tabbed out"), &g_Config.m_TcAutoReplyMinimized, &Column, LineSize);
	CUIRect MinimizedReply;
	Column.HSplitTop(LineSize + MarginExtraSmall, &MinimizedReply, &Column);
	if(g_Config.m_TcAutoReplyMinimized)
	{
		MinimizedReply.HSplitTop(MarginExtraSmall, nullptr, &MinimizedReply);
		static CLineInput s_MinimizedReply(g_Config.m_TcAutoReplyMinimizedMessage, sizeof(g_Config.m_TcAutoReplyMinimizedMessage));
		s_MinimizedReply.SetEmptyText("I am not tabbed in");
		Ui()->DoEditBox(&s_MinimizedReply, &MinimizedReply, EditBoxFontSize);
	}
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Player Indicator ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Player Indicator"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPlayerIndicator, TCLocalize("Show any enabled Indicators"), &g_Config.m_TcPlayerIndicator, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcIndicatorHideVisible, TCLocalize("Hide indicator for tees on your screen"), &g_Config.m_TcIndicatorHideVisible, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPlayerIndicatorFreeze, TCLocalize("Show only freeze Players"), &g_Config.m_TcPlayerIndicatorFreeze, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcIndicatorTeamOnly, TCLocalize("Only show after joining a team"), &g_Config.m_TcIndicatorTeamOnly, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcIndicatorTees, TCLocalize("Render tiny tees instead of circles"), &g_Config.m_TcIndicatorTees, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicator, TCLocalize("Use warlist groups for indicator"), &g_Config.m_TcWarListIndicator, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorRadius, &g_Config.m_TcIndicatorRadius, &Button, TCLocalize("Indicator size"), 1, 16);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorOpacity, &g_Config.m_TcIndicatorOpacity, &Button, TCLocalize("Indicator opacity"), 0, 100);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcIndicatorVariableDistance, TCLocalize("Change indicator offset based on distance to other tees"), &g_Config.m_TcIndicatorVariableDistance, &Column, LineSize);
	if(g_Config.m_TcIndicatorVariableDistance)
	{
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorOffset, &g_Config.m_TcIndicatorOffset, &Button, TCLocalize("Indicator min offset"), 16, 200);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorOffsetMax, &g_Config.m_TcIndicatorOffsetMax, &Button, TCLocalize("Indicator max offset"), 16, 200);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorMaxDistance, &g_Config.m_TcIndicatorMaxDistance, &Button, TCLocalize("Indicator max distance"), 500, 7000);
	}
	else
	{
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcIndicatorOffset, &g_Config.m_TcIndicatorOffset, &Button, TCLocalize("Indicator offset"), 16, 200);
		Column.HSplitTop(LineSize * 2, nullptr, &Column);
	}
	if(g_Config.m_TcWarListIndicator)
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorColors, TCLocalize("Use warlist colors instead of regular colors"), &g_Config.m_TcWarListIndicatorColors, &Column, LineSize);
		char aBuf[128];
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorAll, TCLocalize("Show all warlist groups"), &g_Config.m_TcWarListIndicatorAll, &Column, LineSize);
		str_format(aBuf, sizeof(aBuf), "Show %s group", GameClient()->m_WarList.m_WarTypes.at(1)->m_aWarName);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorEnemy, aBuf, &g_Config.m_TcWarListIndicatorEnemy, &Column, LineSize);
		str_format(aBuf, sizeof(aBuf), "Show %s group", GameClient()->m_WarList.m_WarTypes.at(2)->m_aWarName);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListIndicatorTeam, aBuf, &g_Config.m_TcWarListIndicatorTeam, &Column, LineSize);
	}
	if(!g_Config.m_TcWarListIndicatorColors || !g_Config.m_TcWarListIndicator)
	{
		static CButtonContainer s_IndicatorAliveColorId, s_IndicatorDeadColorId, s_IndicatorSavedColorId;
		DoLine_ColorPicker(&s_IndicatorAliveColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Indicator alive color"), &g_Config.m_TcIndicatorAlive, ColorRGBA(0.0f, 0.0f, 0.0f), false);
		DoLine_ColorPicker(&s_IndicatorDeadColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Indicator in freeze color"), &g_Config.m_TcIndicatorFreeze, ColorRGBA(0.0f, 0.0f, 0.0f), false);
		DoLine_ColorPicker(&s_IndicatorSavedColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Indicator safe color"), &g_Config.m_TcIndicatorSaved, ColorRGBA(0.0f, 0.0f, 0.0f), false);
	}
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Pet ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Pet"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcPetShow, TCLocalize("Show the pet"), &g_Config.m_TcPetShow, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcPetSize, &g_Config.m_TcPetSize, &Button, TCLocalize("Pet size"), 10, 500, &CUi::ms_LinearScrollbarScale, 0, "%");
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcPetAlpha, &g_Config.m_TcPetAlpha, &Button, TCLocalize("Pet alpha"), 10, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	Column.HSplitTop(LineSize + MarginExtraSmall, &Button, &Column);
	Button.VSplitMid(&Label, &Button);
	Ui()->DoLabel(&Label, TCLocalize("Pet Skin:"), FontSize, TEXTALIGN_ML);
	static CLineInput s_PetSkin(g_Config.m_TcPetSkin, sizeof(g_Config.m_TcPetSkin));
	Ui()->DoEditBox(&s_PetSkin, &Button, EditBoxFontSize);

	// Pet Preview
	Column.HSplitTop(MarginSmall, nullptr, &Column);
	CUIRect Preview;
	Column.HSplitTop(64.0f, &Preview, &Column);

	CTeeRenderInfo TeeInfo;
	const CSkin *pSkin = GameClient()->m_Skins.Find(g_Config.m_TcPetSkin);
	if(!pSkin || str_comp(pSkin->GetName(), g_Config.m_TcPetSkin) != 0)
		pSkin = GameClient()->m_Skins.Find("default");

	TeeInfo.m_OriginalRenderSkin = pSkin->m_OriginalSkin;
	TeeInfo.m_ColorableRenderSkin = pSkin->m_ColorableSkin;
	TeeInfo.m_SkinMetrics = pSkin->m_Metrics;
	TeeInfo.m_CustomColoredSkin = false;
	TeeInfo.m_ColorBody = ColorRGBA(1.0f, 1.0f, 1.0f);
	TeeInfo.m_ColorFeet = ColorRGBA(1.0f, 1.0f, 1.0f);
	TeeInfo.m_Size = 64.0f;

	const CAnimState *pIdleState = CAnimState::GetIdle();
	vec2 OffsetToMid;
	CRenderTools::GetRenderTeeOffsetToRenderedTee(pIdleState, &TeeInfo, OffsetToMid);
	vec2 TeeRenderPos = Preview.Center();
	TeeRenderPos.y += OffsetToMid.y;

	vec2 Dir = Ui()->MousePos() - TeeRenderPos;
	const float Length = length(Dir);
	if(Length > 0.0f)
		Dir /= Length;
	if(Length < 0.4f * 64.0f)
	{
		Dir = vec2(1.0f, 0.0f);
	}

	int PetEmote = g_Config.m_ClPlayerDefaultEyes;
	RenderTools()->RenderTee(pIdleState, &TeeInfo, PetEmote, Dir, TeeRenderPos);

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** RightView ***** //
	LeftView = Column;
	Column = RightView;

	// ***** Race Stats (moved to AClient tab) ***** //
	if(false)
	{
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("AClient"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcMiniStatboard, TCLocalize("Enable Mini Statboard HUD"), &g_Config.m_AcMiniStatboard, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcStatboardKeepOnDeath, TCLocalize("Keep stats on death (do not reset on /kill)"), &g_Config.m_AcStatboardKeepOnDeath, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcStatboardResetOnTeamKill, TCLocalize("Reset stats on /kill or /team change"), &g_Config.m_AcStatboardResetOnTeamKill, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcStatboardAutoSave, TCLocalize("Auto-save stats before reset (/kill or /team)"), &g_Config.m_AcStatboardAutoSave, &Column, LineSize);

	static CButtonContainer s_ReaderButtonSaveStats, s_ClearButtonSaveStats;
	static CButtonContainer s_ReaderButtonToggleStats, s_ClearButtonToggleStats;
	DoLine_KeyReader(Column, s_ReaderButtonToggleStats, s_ClearButtonToggleStats, TCLocalize("Toggle stats overlay key"), "toggle ac_mini_statboard 0 1");
	static CButtonContainer s_ReaderButtonSaveStats2, s_ClearButtonSaveStats2;
	(void)s_ReaderButtonSaveStats;
	(void)s_ClearButtonSaveStats;
	DoLine_KeyReader(Column, s_ReaderButtonSaveStats2, s_ClearButtonSaveStats2, TCLocalize("Save stats key"), "ac_save_racestats");
	static CButtonContainer s_ReaderButtonFastPractice, s_ClearButtonFastPractice;
	DoLine_KeyReader(Column, s_ReaderButtonFastPractice, s_ClearButtonFastPractice, TCLocalize("Fast Practice key (Ghost mode)"), "fast_practice_toggle");

	static int s_ColFreezes = (g_Config.m_AcMiniStatboardColumns & 1) ? 1 : 0;
	s_ColFreezes = (g_Config.m_AcMiniStatboardColumns & 1) ? 1 : 0;
	if(DoButton_CheckBoxAutoVMarginAndSet(&s_ColFreezes, TCLocalize("Column: Freezes"), &s_ColFreezes, &Column, LineSize))
		g_Config.m_AcMiniStatboardColumns ^= 1;

	static int s_ColSaves = (g_Config.m_AcMiniStatboardColumns & 2) ? 1 : 0;
	s_ColSaves = (g_Config.m_AcMiniStatboardColumns & 2) ? 1 : 0;
	if(DoButton_CheckBoxAutoVMarginAndSet(&s_ColSaves, TCLocalize("Column: Saves (Hammer unfreeze)"), &s_ColSaves, &Column, LineSize))
		g_Config.m_AcMiniStatboardColumns ^= 2;

	static int s_ColLasts = (g_Config.m_AcMiniStatboardColumns & 4) ? 1 : 0;
	s_ColLasts = (g_Config.m_AcMiniStatboardColumns & 4) ? 1 : 0;
	if(DoButton_CheckBoxAutoVMarginAndSet(&s_ColLasts, TCLocalize("Column: Lasts (Sole survivor)"), &s_ColLasts, &Column, LineSize))
		g_Config.m_AcMiniStatboardColumns ^= 4;

	static int s_ColEff = (g_Config.m_AcMiniStatboardColumns & 8) ? 1 : 0;
	s_ColEff = (g_Config.m_AcMiniStatboardColumns & 8) ? 1 : 0;
	if(DoButton_CheckBoxAutoVMarginAndSet(&s_ColEff, TCLocalize("Column: Efficiency ratio"), &s_ColEff, &Column, LineSize))
		g_Config.m_AcMiniStatboardColumns ^= 8;

	static int s_ColDeathsAc = 0;
	s_ColDeathsAc = (g_Config.m_AcMiniStatboardColumns & 16) ? 1 : 0;
	if(DoButton_CheckBoxAutoVMarginAndSet(&s_ColDeathsAc, TCLocalize("Column: Deaths"), &s_ColDeathsAc, &Column, LineSize))
		g_Config.m_AcMiniStatboardColumns ^= 16;

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_AcMiniStatboardX, &g_Config.m_AcMiniStatboardX, &Button, TCLocalize("Horizontal Position (X)"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_AcMiniStatboardY, &g_Config.m_AcMiniStatboardY, &Button, TCLocalize("Vertical Position (Y)"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_AcMiniStatboardScale, &g_Config.m_AcMiniStatboardScale, &Button, TCLocalize("HUD Scale (Size / Z)"), 30, 200, &CUi::ms_LinearScrollbarScale, 0, "%");

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_AcMiniStatboardOpacity, &g_Config.m_AcMiniStatboardOpacity, &Button, TCLocalize("Background Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_AcMiniStatboardMaxPlayers, &g_Config.m_AcMiniStatboardMaxPlayers, &Button, TCLocalize("Max visible players"), 0, MAX_CLIENTS);

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	}

	// ***** HUD ***** //
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("HUD"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMiniVoteHud, TCLocalize("Show mini vote HUD"), &g_Config.m_TcMiniVoteHud, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcMiniDebug, TCLocalize("Show position and angle (mini debug)"), &g_Config.m_TcMiniDebug, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRenderCursorSpec, TCLocalize("Show your cursor when in free spectate"), &g_Config.m_TcRenderCursorSpec, &Column, LineSize);

	Column.HSplitTop(LineSize, &Button, &Column);
	if(g_Config.m_TcRenderCursorSpec)
	{
		Ui()->DoScrollbarOption(&g_Config.m_TcRenderCursorSpecAlpha, &g_Config.m_TcRenderCursorSpecAlpha, &Button, TCLocalize("Spectate cursor alpha"), 0, 100);
	}

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcNotifyWhenLast, TCLocalize("Show when you are the last alive"), &g_Config.m_TcNotifyWhenLast, &Column, LineSize);
	CUIRect NotificationConfig;
	Column.HSplitTop(LineSize + MarginSmall, &NotificationConfig, &Column);
	if(g_Config.m_TcNotifyWhenLast)
	{
		NotificationConfig.VSplitMid(&Button, &NotificationConfig);
		static CLineInput s_LastInput(g_Config.m_TcNotifyWhenLastText, sizeof(g_Config.m_TcNotifyWhenLastText));
		s_LastInput.SetEmptyText(TCLocalize("Last!"));
		Button.HSplitTop(MarginSmall, nullptr, &Button);
		Ui()->DoEditBox(&s_LastInput, &Button, EditBoxFontSize);
		static CButtonContainer s_ClientNotifyWhenLastColor;
		DoLine_ColorPicker(&s_ClientNotifyWhenLastColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &NotificationConfig, "", &g_Config.m_TcNotifyWhenLastColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcNotifyWhenLastX, &g_Config.m_TcNotifyWhenLastX, &Button, TCLocalize("Horizontal Position"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcNotifyWhenLastY, &g_Config.m_TcNotifyWhenLastY, &Button, TCLocalize("Vertical Position"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcNotifyWhenLastSize, &g_Config.m_TcNotifyWhenLastSize, &Button, TCLocalize("Font Size"), 1, 50);
	}
	else
	{
		Column.HSplitTop(LineSize * 3.0f, nullptr, &Column);
	}

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowCenter, TCLocalize("Show screen center lines"), &g_Config.m_TcShowCenter, &Column, LineSize);
	Column.HSplitTop(LineSize + MarginSmall, &Button, &Column);
	if(g_Config.m_TcShowCenter)
	{
		static CButtonContainer s_ShowCenterLineColor;
		DoLine_ColorPicker(&s_ShowCenterLineColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Button, TCLocalize("Screen center line color"), &g_Config.m_TcShowCenterColor, DefaultConfig::TcShowCenterColor, false, nullptr, true);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_TcShowCenterWidth, &g_Config.m_TcShowCenterWidth, &Button, TCLocalize("Screen center line width"), 0, 20);
	}
	else
	{
		Column.HSplitTop(LineSize, nullptr, &Column);
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Frozen Tee Display ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Frozen Tee Display"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowFrozenHud, TCLocalize("Show frozen tee display"), &g_Config.m_TcShowFrozenHud, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowFrozenHudSkins, TCLocalize("Use skins instead of ninja tees"), &g_Config.m_TcShowFrozenHudSkins, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFrozenHudTeamOnly, TCLocalize("Only show after joining a team"), &g_Config.m_TcFrozenHudTeamOnly, &Column, LineSize);

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcFrozenMaxRows, &g_Config.m_TcFrozenMaxRows, &Button, TCLocalize("Max Rows"), 1, 6);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcFrozenHudTeeSize, &g_Config.m_TcFrozenHudTeeSize, &Button, TCLocalize("Tee Size"), 8, 27);

	{
		CUIRect CheckBoxRect, CheckBoxRect2;
		Column.HSplitTop(LineSize, &CheckBoxRect, &Column);
		Column.HSplitTop(LineSize, &CheckBoxRect2, &Column);
		if(DoButton_CheckBox(&g_Config.m_TcShowFrozenText, TCLocalize("Tees left alive text"), g_Config.m_TcShowFrozenText >= 1, &CheckBoxRect))
			g_Config.m_TcShowFrozenText = g_Config.m_TcShowFrozenText >= 1 ? 0 : 1;

		if(g_Config.m_TcShowFrozenText)
		{
			static int s_CountFrozenText = 0;
			if(DoButton_CheckBox(&s_CountFrozenText, TCLocalize("Count frozen tees"), g_Config.m_TcShowFrozenText == 2, &CheckBoxRect2))
				g_Config.m_TcShowFrozenText = g_Config.m_TcShowFrozenText != 2 ? 2 : 1;
		}
	}
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Tile Outlines ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Tile Outlines"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutline, TCLocalize("Show any enabled outlines"), &g_Config.m_TcOutline, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineEntities, TCLocalize("Only show outlines in entities"), &g_Config.m_TcOutlineEntities, &Column, LineSize);

	auto DoOutlineType = [&](CButtonContainer &ButtonContainer, const char *pName, int &Enable, int &Width, unsigned int &Color, const unsigned int &ColorDefault) {
		// Checkbox & Color
		DoLine_ColorPicker(&ButtonContainer, ColorPickerLineSize, ColorPickerLabelSize, 0, &Column, pName, &Color, ColorDefault, true, &Enable, true);
		// Width
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&Width, &Width, &Button, TCLocalize("Width", "Outlines"), 1, 16);
		//
		Column.HSplitTop(ColorPickerLineSpacing, nullptr, &Column);
	};
	Column.HSplitTop(ColorPickerLineSpacing, nullptr, &Column);
	static CButtonContainer s_aOutlineButtonContainers[5];
	DoOutlineType(s_aOutlineButtonContainers[0], TCLocalize("Unhook & hook"), g_Config.m_TcOutlineSolid, g_Config.m_TcOutlineWidthSolid, g_Config.m_TcOutlineColorSolid, DefaultConfig::TcOutlineColorSolid);
	DoOutlineType(s_aOutlineButtonContainers[1], TCLocalize("Freeze & deep"), g_Config.m_TcOutlineFreeze, g_Config.m_TcOutlineWidthFreeze, g_Config.m_TcOutlineColorFreeze, DefaultConfig::TcOutlineColorFreeze);
	DoOutlineType(s_aOutlineButtonContainers[2], TCLocalize("Unfreeze & undeep"), g_Config.m_TcOutlineUnfreeze, g_Config.m_TcOutlineWidthUnfreeze, g_Config.m_TcOutlineColorUnfreeze, DefaultConfig::TcOutlineColorUnfreeze);
	DoOutlineType(s_aOutlineButtonContainers[3], TCLocalize("Kill"), g_Config.m_TcOutlineKill, g_Config.m_TcOutlineWidthKill, g_Config.m_TcOutlineColorKill, DefaultConfig::TcOutlineColorKill);
	DoOutlineType(s_aOutlineButtonContainers[4], TCLocalize("Tele"), g_Config.m_TcOutlineTele, g_Config.m_TcOutlineWidthTele, g_Config.m_TcOutlineColorTele, DefaultConfig::TcOutlineColorTele);
	Column.h -= ColorPickerLineSpacing;

	// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineFreeze, TCLocalize("Outline freeze & deep"), &g_Config.m_TcOutlineFreeze, &Column, LineSize);
	// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineSolid, TCLocalize("Outline walls"), &g_Config.m_TcOutlineSolid, &Column, LineSize);
	// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineTele, TCLocalize("Outline teleporter"), &g_Config.m_TcOutlineTele, &Column, LineSize);
	// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineUnfreeze, TCLocalize("Outline unfreeze & undeep"), &g_Config.m_TcOutlineUnfreeze, &Column, LineSize);
	// DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcOutlineKill, TCLocalize("Outline kill"), &g_Config.m_TcOutlineKill, &Column, LineSize);
	// Column.HSplitTop(LineSize, &Button, &Column);
	// Ui()->DoScrollbarOption(&g_Config.m_TcOutlineWidth, &g_Config.m_TcOutlineWidth, &Button, TCLocalize("Outline width"), 1, 16);
	// Column.HSplitTop(LineSize, &Button, &Column);
	// Ui()->DoScrollbarOption(&g_Config.m_TcOutlineAlpha, &g_Config.m_TcOutlineAlpha, &Button, TCLocalize("Outline alpha"), 0, 100);
	// Column.HSplitTop(LineSize, &Button, &Column);
	// Ui()->DoScrollbarOption(&g_Config.m_TcOutlineAlphaSolid, &g_Config.m_TcOutlineAlphaSolid, &Button, TCLocalize("Outline Alpha (walls)"), 0, 100);
	// static CButtonContainer s_OutlineColorFreezeId, s_OutlineColorSolidId, s_OutlineColorTeleId, s_OutlineColorUnfreezeId, s_OutlineColorKillId;
	// DoLine_ColorPicker(&s_OutlineColorFreezeId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Freeze outline color"), &g_Config.m_TcOutlineColorFreeze, ColorRGBA(0.0f, 0.0f, 0.0f), false);
	// DoLine_ColorPicker(&s_OutlineColorSolidId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Walls outline color"), &g_Config.m_TcOutlineColorSolid, ColorRGBA(0.0f, 0.0f, 0.0f), false);
	// DoLine_ColorPicker(&s_OutlineColorTeleId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Teleporter outline color"), &g_Config.m_TcOutlineColorTele, ColorRGBA(0.0f, 0.0f, 0.0f), false);
	// DoLine_ColorPicker(&s_OutlineColorUnfreezeId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Unfreeze outline color"), &g_Config.m_TcOutlineColorUnfreeze, ColorRGBA(0.0f, 0.0f, 0.0f), false);
	// DoLine_ColorPicker(&s_OutlineColorKillId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Kill outline color"), &g_Config.m_TcOutlineColorKill, ColorRGBA(0.0f, 0.0f, 0.0f), false);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Ghost Tools ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Ghost Tools"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcShowOthersGhosts, TCLocalize("Show unpredicted ghosts for other players"), &g_Config.m_TcShowOthersGhosts, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcSwapGhosts, TCLocalize("Swap ghosts and normal players"), &g_Config.m_TcSwapGhosts, &Column, LineSize);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcPredGhostsAlpha, &g_Config.m_TcPredGhostsAlpha, &Button, TCLocalize("Predicted alpha"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcUnpredGhostsAlpha, &g_Config.m_TcUnpredGhostsAlpha, &Button, TCLocalize("Unpredicted alpha"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcHideFrozenGhosts, TCLocalize("Hide ghosts of frozen players"), &g_Config.m_TcHideFrozenGhosts, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRenderGhostAsCircle, TCLocalize("Render ghosts as circles"), &g_Config.m_TcRenderGhostAsCircle, &Column, LineSize);

	static CButtonContainer s_ReaderButtonGhost, s_ClearButtonGhost;
	DoLine_KeyReader(Column, s_ReaderButtonGhost, s_ClearButtonGhost, TCLocalize("Toggle ghosts key"), "toggle tc_show_others_ghosts 0 1");

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Rainbow ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Rainbow"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRainbowTees, TCLocalize("Rainbow Tees"), &g_Config.m_TcRainbowTees, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRainbowWeapon, TCLocalize("Rainbow weapons"), &g_Config.m_TcRainbowWeapon, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRainbowHook, TCLocalize("Rainbow hook"), &g_Config.m_TcRainbowHook, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcRainbowOthers, TCLocalize("Rainbow others"), &g_Config.m_TcRainbowOthers, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	static std::vector<const char *> s_RainbowDropDownNames;
	s_RainbowDropDownNames = {TCLocalize("Rainbow"), TCLocalize("Pulse"), TCLocalize("Black"), TCLocalize("Random")};
	static CUi::SDropDownState s_RainbowDropDownState;
	static CScrollRegion s_RainbowDropDownScrollRegion;
	s_RainbowDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_RainbowDropDownScrollRegion;
	int RainbowSelectedOld = g_Config.m_TcRainbowMode - 1;
	CUIRect RainbowDropDownRect;
	Column.HSplitTop(LineSize, &RainbowDropDownRect, &Column);
	const int RainbowSelectedNew = Ui()->DoDropDown(&RainbowDropDownRect, RainbowSelectedOld, s_RainbowDropDownNames.data(), s_RainbowDropDownNames.size(), s_RainbowDropDownState);
	if(RainbowSelectedOld != RainbowSelectedNew)
	{
		g_Config.m_TcRainbowMode = RainbowSelectedNew + 1;
	}
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcRainbowSpeed, &g_Config.m_TcRainbowSpeed, &Button, TCLocalize("Rainbow speed"), 0, 5000, &CUi::ms_LogarithmicScrollbarScale, 0, "%");
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	// ***** Tee Trails ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Tee Trails"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTeeTrail, TCLocalize("Enable tee trails"), &g_Config.m_TcTeeTrail, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTeeTrailOthers, TCLocalize("Show other tees' trails"), &g_Config.m_TcTeeTrailOthers, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTeeTrailFade, TCLocalize("Fade trail alpha"), &g_Config.m_TcTeeTrailFade, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTeeTrailTaper, TCLocalize("Taper trail width"), &g_Config.m_TcTeeTrailTaper, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	std::vector<const char *> vTrailDropDownNames;
	vTrailDropDownNames = {TCLocalize("Solid"), TCLocalize("Tee"), TCLocalize("Rainbow"), TCLocalize("Speed")};
	static CUi::SDropDownState s_TrailDropDownState;
	static CScrollRegion s_TrailDropDownScrollRegion;
	s_TrailDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_TrailDropDownScrollRegion;
	int TrailSelectedOld = g_Config.m_TcTeeTrailColorMode - 1;
	CUIRect TrailDropDownRect;
	Column.HSplitTop(LineSize, &TrailDropDownRect, &Column);
	const int TrailSelectedNew = Ui()->DoDropDown(&TrailDropDownRect, TrailSelectedOld, vTrailDropDownNames.data(), vTrailDropDownNames.size(), s_TrailDropDownState);
	if(TrailSelectedOld != TrailSelectedNew)
	{
		g_Config.m_TcTeeTrailColorMode = TrailSelectedNew + 1;
	}
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static CButtonContainer s_TeeTrailColor;
	if(g_Config.m_TcTeeTrailColorMode == CTrails::COLORMODE_SOLID)
		DoLine_ColorPicker(&s_TeeTrailColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Tee trail color"), &g_Config.m_TcTeeTrailColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
	else
		Column.HSplitTop(ColorPickerLineSize + ColorPickerLineSpacing, &Button, &Column);

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcTeeTrailWidth, &g_Config.m_TcTeeTrailWidth, &Button, TCLocalize("Trail width"), 0, 20);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcTeeTrailLength, &g_Config.m_TcTeeTrailLength, &Button, TCLocalize("Trail length"), 0, 200);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcTeeTrailAlpha, &g_Config.m_TcTeeTrailAlpha, &Button, TCLocalize("Trail alpha"), 0, 100);

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	// ***** BG Draw ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Background Draw"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static CButtonContainer s_BgDrawColor;
	DoLine_ColorPicker(&s_BgDrawColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Color"), &g_Config.m_TcBgDrawColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);

	Column.HSplitTop(LineSize * 2.0f, &Button, &Column);
	if(g_Config.m_TcBgDrawFadeTime == 0)
		Ui()->DoScrollbarOption(&g_Config.m_TcBgDrawFadeTime, &g_Config.m_TcBgDrawFadeTime, &Button, TCLocalize("Time until strokes disappear"), 0, 600, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, TCLocalize(" seconds (never)"));
	else
		Ui()->DoScrollbarOption(&g_Config.m_TcBgDrawFadeTime, &g_Config.m_TcBgDrawFadeTime, &Button, TCLocalize("Time until strokes disappear"), 0, 600, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE, TCLocalize(" seconds"));

	Column.HSplitTop(LineSize * 2.0f, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_TcBgDrawWidth, &g_Config.m_TcBgDrawWidth, &Button, TCLocalize("Width"), 1, 50, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_MULTILINE);

	static CButtonContainer s_ReaderButtonDraw, s_ClearButtonDraw;
	DoLine_KeyReader(Column, s_ReaderButtonDraw, s_ClearButtonDraw, TCLocalize("Draw where mouse is"), "+bg_draw");

	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	// ***** Finish Name ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Finish Name"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcChangeNameNearFinish, TCLocalize("Attempt to change your name when near finish"), &g_Config.m_TcChangeNameNearFinish, &Column, LineSize);
	// Column.HSplitTop(LineSize, &Button, &Column); // TODO finish scan radius
	// Ui()->DoScrollbarOption(&g_Config.m_TcPetSize, &g_Config.m_TcPetSize, &Button, TCLocalize("Pet size"), 10, 500, &CUi::ms_LinearScrollbarScale, 0, "%");
	Column.HSplitTop(LineSize + MarginExtraSmall, &Button, &Column);
	Button.VSplitMid(&Label, &Button);
	Ui()->DoLabel(&Label, TCLocalize("Finish Name:"), FontSize, TEXTALIGN_ML);
	static CLineInput s_FinishName(g_Config.m_TcFinishName, sizeof(g_Config.m_TcFinishName));
	Ui()->DoEditBox(&s_FinishName, &Button, EditBoxFontSize);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** Translate ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Translate"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	// Current backend's language table -- Google and LibreTranslate genuinely
	// support different language sets (see translate.h/.cpp's
	// g_aTranslateBackends), so the dropdowns below are backend-scoped
	// rather than one shared list.
	int CurrentBackendIndex = 0;
	for(size_t i = 0; i < g_aTranslateBackends.size(); i++)
	{
		if(str_comp_nocase(g_Config.m_TcTranslateBackend, g_aTranslateBackends[i].m_pValue) == 0)
			CurrentBackendIndex = (int)i;
	}
	const STranslateBackendInfo &CurrentBackend = g_aTranslateBackends[CurrentBackendIndex];

	{
		static std::vector<const char *> s_TranslateBackendNames;
		s_TranslateBackendNames.clear();
		for(const STranslateBackendInfo &Backend : g_aTranslateBackends)
			s_TranslateBackendNames.push_back(Backend.m_pName);

		static CUi::SDropDownState s_TranslateBackendDropDownState;
		static CScrollRegion s_TranslateBackendDropDownScrollRegion;
		s_TranslateBackendDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_TranslateBackendDropDownScrollRegion;
		CUIRect BackendDropDownRect;
		Column.HSplitTop(LineSize, &BackendDropDownRect, &Column);
		BackendDropDownRect.VSplitLeft(120.0f, &Label, &BackendDropDownRect);
		Ui()->DoLabel(&Label, TCLocalize("Backend: "), FontSize, TEXTALIGN_ML);
		const int BackendSelectedNew = Ui()->DoDropDown(&BackendDropDownRect, CurrentBackendIndex, s_TranslateBackendNames.data(), s_TranslateBackendNames.size(), s_TranslateBackendDropDownState);
		if(BackendSelectedNew != CurrentBackendIndex)
			str_copy(g_Config.m_TcTranslateBackend, g_aTranslateBackends[BackendSelectedNew].m_pValue);
	}
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTranslateAuto, TCLocalize("Automatically translate incoming messages"), &g_Config.m_TcTranslateAuto, &Column, LineSize);
	CUIRect IncomingTargetBox;
	Column.HSplitTop(LineSize + MarginExtraSmall, &IncomingTargetBox, &Column);
	if(g_Config.m_TcTranslateAuto)
	{
		IncomingTargetBox.HSplitTop(MarginExtraSmall, nullptr, &IncomingTargetBox);
		IncomingTargetBox.VSplitMid(&Label, &IncomingTargetBox);
		Ui()->DoLabel(&Label, TCLocalize("Translate incoming to:"), FontSize, TEXTALIGN_ML);
		static CUi::SDropDownState s_IncomingLangDropDownState;
		static CScrollRegion s_IncomingLangDropDownScrollRegion;
		s_IncomingLangDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_IncomingLangDropDownScrollRegion;
		static std::vector<const char *> s_IncomingLangNames;
		s_IncomingLangNames.clear();
		for(const STranslateLanguage &Language : CurrentBackend.m_vLanguages)
			s_IncomingLangNames.push_back(Language.m_pName);
		const int IncomingLangOld = TranslateLanguageIndex(CurrentBackend.m_vLanguages, g_Config.m_TcTranslateTarget);
		const int IncomingLangNew = Ui()->DoDropDown(&IncomingTargetBox, IncomingLangOld, s_IncomingLangNames.data(), s_IncomingLangNames.size(), s_IncomingLangDropDownState);
		if(IncomingLangOld != IncomingLangNew)
			str_copy(g_Config.m_TcTranslateTarget, CurrentBackend.m_vLanguages[IncomingLangNew].m_pCode);
	}
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcTranslateOutgoing, TCLocalize("Translate your outgoing messages"), &g_Config.m_TcTranslateOutgoing, &Column, LineSize);
	CUIRect OutgoingTargetBox;
	Column.HSplitTop(LineSize + MarginExtraSmall, &OutgoingTargetBox, &Column);
	if(g_Config.m_TcTranslateOutgoing)
	{
		OutgoingTargetBox.HSplitTop(MarginExtraSmall, nullptr, &OutgoingTargetBox);
		OutgoingTargetBox.VSplitMid(&Label, &OutgoingTargetBox);
		Ui()->DoLabel(&Label, TCLocalize("Translate outgoing to:"), FontSize, TEXTALIGN_ML);
		static CUi::SDropDownState s_OutgoingLangDropDownState;
		static CScrollRegion s_OutgoingLangDropDownScrollRegion;
		s_OutgoingLangDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_OutgoingLangDropDownScrollRegion;
		static std::vector<const char *> s_OutgoingLangNames;
		s_OutgoingLangNames.clear();
		for(const STranslateLanguage &Language : CurrentBackend.m_vLanguages)
			s_OutgoingLangNames.push_back(Language.m_pName);
		const int OutgoingLangOld = TranslateLanguageIndex(CurrentBackend.m_vLanguages, g_Config.m_TcTranslateOutgoingTarget);
		const int OutgoingLangNew = Ui()->DoDropDown(&OutgoingTargetBox, OutgoingLangOld, s_OutgoingLangNames.data(), s_OutgoingLangNames.size(), s_OutgoingLangDropDownState);
		if(OutgoingLangOld != OutgoingLangNew)
			str_copy(g_Config.m_TcTranslateOutgoingTarget, CurrentBackend.m_vLanguages[OutgoingLangNew].m_pCode);
	}
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	if(CurrentBackend.m_NeedsEndpointConfig)
	{
		CUIRect EndpointBox;
		Column.HSplitTop(LineSize + MarginExtraSmall, &EndpointBox, &Column);
		EndpointBox.VSplitMid(&Label, &EndpointBox);
		Ui()->DoLabel(&Label, TCLocalize("LibreTranslate endpoint:"), FontSize, TEXTALIGN_ML);
		static CLineInput s_TranslateEndpoint(g_Config.m_TcTranslateEndpoint, sizeof(g_Config.m_TcTranslateEndpoint));
		s_TranslateEndpoint.SetEmptyText("localhost:5000/translate");
		Ui()->DoEditBox(&s_TranslateEndpoint, &EndpointBox, EditBoxFontSize);
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);

		CUIRect KeyBox;
		Column.HSplitTop(LineSize + MarginExtraSmall, &KeyBox, &Column);
		KeyBox.VSplitMid(&Label, &KeyBox);
		Ui()->DoLabel(&Label, TCLocalize("LibreTranslate API key:"), FontSize, TEXTALIGN_ML);
		static CLineInput s_TranslateKey(g_Config.m_TcTranslateKey, sizeof(g_Config.m_TcTranslateKey));
		s_TranslateKey.SetEmptyText(TCLocalize("Optional"));
		Ui()->DoEditBox(&s_TranslateKey, &KeyBox, EditBoxFontSize);
	}
	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;

	// ***** END OF PAGE 1 SETTINGS ***** //
	RightView = Column;
#if 0
	RightView = Column;

	// ***** Bloque Inferior: Novedades de AClient ***** //
	CUIRect BottomWhatsNew;
	BottomWhatsNew.x = LeftView.x;
	BottomWhatsNew.y = std::max(LeftView.y, RightView.y) + MarginBetweenSections;
	BottomWhatsNew.w = (RightView.x + RightView.w) - LeftView.x;
	BottomWhatsNew.h = 360.0f;

	s_SectionBoxesAc.push_back(BottomWhatsNew);

	BottomWhatsNew.HSplitTop(HeadlineHeight, &Label, &BottomWhatsNew);
	TextRender()->TextColor(1.0f, 0.88f, 0.35f, 1.0f);
	Ui()->DoLabel(&Label, TCLocalize("What's New in AClient (Quick Summary)"), HeadlineFontSize, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	BottomWhatsNew.HSplitTop(MarginSmall, nullptr, &BottomWhatsNew);

	auto AddNewsLine = [&](const char *pTitle, const char *pDetail) {
		CUIRect LineRect;
		BottomWhatsNew.HSplitTop(18.0f, &LineRect, &BottomWhatsNew);
		TextRender()->TextColor(0.45f, 0.92f, 1.0f, 1.0f);
		float TitleW = TextRender()->TextWidth(12.5f, pTitle) + 6.0f;
		CUIRect TitleRect, DetailRect;
		LineRect.VSplitLeft(TitleW, &TitleRect, &DetailRect);
		Ui()->DoLabel(&TitleRect, pTitle, 12.5f, TEXTALIGN_ML);
		TextRender()->TextColor(0.88f, 0.90f, 0.95f, 0.95f);
		Ui()->DoLabel(&DetailRect, pDetail, 12.0f, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	};

	AddNewsLine(TCLocalize("* Race Stats HUD & History:"), TCLocalize("Live stats (Freezes, Saves, Deaths, Dynamic % Eff, Net +/-, Frozen time, Hook accuracy) & auto-saved log."));
	AddNewsLine(TCLocalize("* Background Alerts (Tabbed out):"), TCLocalize("Granular alerts (sound, taskbar flash, desktop banner) on unfreeze or when moved/hooked."));
	AddNewsLine(TCLocalize("* Skin Stealer:"), TCLocalize("Instantly copy skins and colors by aiming with cursor bind, via Scoreboard (Tab) or /stealskin [name]."));
	AddNewsLine(TCLocalize("* Fast Practice (Ghost Mode):"), TCLocalize("Practice any map part with /tp, /tc, /invincible, /weapons, /save & /load without losing your real run."));
	AddNewsLine(TCLocalize("* Client Echo Control:"), TCLocalize("Enable or silence local client notices ([AClient], copied skin notices, invites, command echoes)."));
	AddNewsLine(TCLocalize("* Auto Team Lock (up to 5m):"), TCLocalize("Automatically locks team (/lock 1) after configured delay (up to 5 minutes) when joining."));
	AddNewsLine(TCLocalize("* Auto-Invite Friends:"), TCLocalize("Invite all online friends to your team with /invite friends, with anti-flood queue and Tab auto-complete."));

	BottomWhatsNew.HSplitTop(MarginExtraSmall, nullptr, &BottomWhatsNew);
	s_SectionBoxesAc.back().h = BottomWhatsNew.y - s_SectionBoxesAc.back().y;

	LeftView.y = BottomWhatsNew.y;
	RightView.y = BottomWhatsNew.y;

#endif
	// Scroll
	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = std::max(LeftView.y, RightView.y) + MarginSmall * 2.0f;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRegion);
	s_ScrollRegion.End();
}

void CMenus::RenderSettingsTClientBindWheel(CUIRect MainView)
{
	CUIRect LeftView, RightView, Label, Button;
	MainView.VSplitLeft(MainView.w / 2.1f, &LeftView, &RightView);

	const float Radius = std::min(RightView.w, RightView.h) / 2.0f;
	vec2 Center = RightView.Center();
	// Draw Circle
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(0.0f, 0.0f, 0.0f, 0.3f);
	Graphics()->DrawCircle(Center.x, Center.y, Radius, 64);
	Graphics()->QuadsEnd();

	static char s_aBindName[BINDWHEEL_MAX_NAME];
	static char s_aBindCommand[BINDWHEEL_MAX_CMD];

	static int s_SelectedBindIndex = -1;
	int HoveringIndex = -1;

	float MouseDist = distance(Center, Ui()->MousePos());
	const int SegmentCount = GameClient()->m_BindWheel.m_vBinds.size();
	if(MouseDist < Radius && MouseDist > Radius * 0.25f && SegmentCount > 0)
	{
		float SegmentAngle = 2.0f * pi / SegmentCount;

		float HoveringAngle = angle(Ui()->MousePos() - Center) + SegmentAngle / 2.0f;
		if(HoveringAngle < 0.0f)
			HoveringAngle += 2.0f * pi;

		HoveringIndex = (int)(HoveringAngle / (2.0f * pi) * SegmentCount);
		HoveringIndex = std::clamp(HoveringIndex, 0, SegmentCount - 1);
		if(Ui()->MouseButtonClicked(0))
		{
			s_SelectedBindIndex = HoveringIndex;
			str_copy(s_aBindName, GameClient()->m_BindWheel.m_vBinds[HoveringIndex].m_aName);
			str_copy(s_aBindCommand, GameClient()->m_BindWheel.m_vBinds[HoveringIndex].m_aCommand);
		}
		else if(Ui()->MouseButtonClicked(1) && s_SelectedBindIndex >= 0 && HoveringIndex >= 0 && HoveringIndex != s_SelectedBindIndex)
		{
			CBindWheel::CBind BindA = GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex];
			CBindWheel::CBind BindB = GameClient()->m_BindWheel.m_vBinds[HoveringIndex];
			str_copy(GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex].m_aName, BindB.m_aName);
			str_copy(GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex].m_aCommand, BindB.m_aCommand);
			str_copy(GameClient()->m_BindWheel.m_vBinds[HoveringIndex].m_aName, BindA.m_aName);
			str_copy(GameClient()->m_BindWheel.m_vBinds[HoveringIndex].m_aCommand, BindA.m_aCommand);
		}
		else if(Ui()->MouseButtonClicked(2))
		{
			s_SelectedBindIndex = HoveringIndex;
		}
	}
	else if(MouseDist < Radius && Ui()->MouseButtonClicked(0))
	{
		s_SelectedBindIndex = -1;
		str_copy(s_aBindName, "");
		str_copy(s_aBindCommand, "");
	}

	const float Theta = pi * 2.0f / std::max<float>(1.0f, GameClient()->m_BindWheel.m_vBinds.size());
	for(int i = 0; i < static_cast<int>(GameClient()->m_BindWheel.m_vBinds.size()); i++)
	{
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

		float SegmentFontSize = FontSize * 1.1f;
		if(i == s_SelectedBindIndex)
		{
			SegmentFontSize = FontSize * 1.7f;
			TextRender()->TextColor(ColorRGBA(0.5f, 1.0f, 0.75f, 1.0f));
		}
		else if(i == HoveringIndex)
		{
			SegmentFontSize = FontSize * 1.35f;
		}

		const CBindWheel::CBind Bind = GameClient()->m_BindWheel.m_vBinds[i];
		const float Angle = Theta * i;

		const vec2 Pos = direction(Angle) * (Radius * 0.75f) + Center;
		const CUIRect Rect = CUIRect{Pos.x - 50.0f, Pos.y - 50.0f, 100.0f, 100.0f};
		Ui()->DoLabel(&Rect, Bind.m_aName, SegmentFontSize, TEXTALIGN_MC);
	}

	TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));

	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Button.VSplitLeft(100.0f, &Label, &Button);
	Ui()->DoLabel(&Label, TCLocalize("Name:"), FontSize, TEXTALIGN_ML);
	static CLineInput s_NameInput;
	s_NameInput.SetBuffer(s_aBindName, sizeof(s_aBindName));
	s_NameInput.SetEmptyText(TCLocalize("Name"));
	Ui()->DoEditBox(&s_NameInput, &Button, EditBoxFontSize);

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Button.VSplitLeft(100.0f, &Label, &Button);
	Ui()->DoLabel(&Label, TCLocalize("Command:"), FontSize, TEXTALIGN_ML);
	static CLineInput s_BindInput;
	s_BindInput.SetBuffer(s_aBindCommand, sizeof(s_aBindCommand));
	s_BindInput.SetEmptyText(TCLocalize("Command"));
	Ui()->DoEditBox(&s_BindInput, &Button, EditBoxFontSize);

	static CButtonContainer s_AddButton, s_RemoveButton, s_OverrideButton;

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	if(DoButton_Menu(&s_OverrideButton, TCLocalize("Override Selected"), 0, &Button) && s_SelectedBindIndex >= 0 && s_SelectedBindIndex < static_cast<int>(GameClient()->m_BindWheel.m_vBinds.size()))
	{
		CBindWheel::CBind TempBind;
		if(str_length(s_aBindName) == 0)
			str_copy(TempBind.m_aName, "*");
		else
			str_copy(TempBind.m_aName, s_aBindName);

		str_copy(GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex].m_aName, TempBind.m_aName);
		str_copy(GameClient()->m_BindWheel.m_vBinds[s_SelectedBindIndex].m_aCommand, s_aBindCommand);
	}
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	CUIRect ButtonAdd, ButtonRemove;
	Button.VSplitMid(&ButtonRemove, &ButtonAdd, MarginSmall);
	if(DoButton_Menu(&s_AddButton, TCLocalize("Add Bind"), 0, &ButtonAdd))
	{
		CBindWheel::CBind TempBind;
		if(str_length(s_aBindName) == 0)
			str_copy(TempBind.m_aName, "*");
		else
			str_copy(TempBind.m_aName, s_aBindName);

		GameClient()->m_BindWheel.AddBind(TempBind.m_aName, s_aBindCommand);
		s_SelectedBindIndex = static_cast<int>(GameClient()->m_BindWheel.m_vBinds.size()) - 1;
	}
	if(DoButton_Menu(&s_RemoveButton, TCLocalize("Remove Bind"), 0, &ButtonRemove) && s_SelectedBindIndex >= 0)
	{
		GameClient()->m_BindWheel.RemoveBind(s_SelectedBindIndex);
		s_SelectedBindIndex = -1;
	}

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("The command is ran in console not chat"), FontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Use left mouse to select"), FontSize * 0.8f, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Use right mouse to swap with selected"), FontSize * 0.8f, TEXTALIGN_ML);
	LeftView.HSplitTop(LineSize * 0.8f, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Use middle mouse select without copy"), FontSize * 0.8f, TEXTALIGN_ML);

	LeftView.HSplitBottom(LineSize, &LeftView, &Label);
	static CButtonContainer s_ReaderButtonWheel, s_ClearButtonWheel;
	DoLine_KeyReader(Label, s_ReaderButtonWheel, s_ClearButtonWheel, TCLocalize("Bind Wheel Key"), "+bindwheel");

	LeftView.HSplitBottom(LineSize, &LeftView, &Label);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcResetBindWheelMouse, TCLocalize("Reset position of mouse when opening bindwheel"), &g_Config.m_TcResetBindWheelMouse, &Label, LineSize);
}

void CMenus::RenderSettingsTClientChatBinds(CUIRect MainView)
{
	CUIRect LeftView, RightView, Button, Label;

	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_ForceShowScrollbar = true;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegion.Begin(&MainView, &ScrollParams);

	static std::vector<CUIRect> s_SectionBoxes;
	const float ScrollOffset = MainView.y;
	static float s_PrevScrollOffset = 0.0f;

	MainView.HSplitTop(Margin, nullptr, &MainView);
	MainView.VSplitRight(5.0f, &MainView, nullptr); // Padding for scrollbar
	MainView.VSplitLeft(5.0f, nullptr, &MainView); // Padding for scrollbar

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	for(CUIRect &Section : s_SectionBoxes)
	{
		float Padding = MarginBetweenViews * 0.6666f;
		Section.w += Padding;
		Section.h += Padding;
		Section.x -= Padding * 0.5f;
		Section.y -= Padding * 0.5f;
		Section.y -= s_PrevScrollOffset - ScrollOffset;
		if(Section.x < MainView.x + 1.0f)
		{
			float Diff = (MainView.x + 1.0f) - Section.x;
			Section.x += Diff;
			Section.w -= Diff;
		}
		if(Section.x + Section.w > MainView.x + MainView.w - 1.0f)
		{
			Section.w = (MainView.x + MainView.w - 1.0f) - Section.x;
		}
		if(g_Config.m_AcModernUi)
		{
			Section.Draw(GetColorBorder(0.40f), IGraphics::CORNER_ALL, 10.0f);
			CUIRect Inner = Section;
			Inner.Margin(1.0f, &Inner);
			Inner.Draw(GetColorBg(), IGraphics::CORNER_ALL, 9.0f);
		}
		else
		{
			Section.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
		}
	}
	s_PrevScrollOffset = ScrollOffset;
	s_SectionBoxes.clear();

	// ***** All the stuff ***** //

	auto DoBindchatDefault = [&](CUIRect &Column, CBindChat::CBindDefault &BindDefault) {
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		CBindChat::CBind *pOldBind = GameClient()->m_BindChat.GetBind(BindDefault.m_Bind.m_aCommand);
		static char s_aTempName[BINDCHAT_MAX_NAME] = "";
		char *pName;
		if(pOldBind == nullptr)
			pName = s_aTempName;
		else
			pName = pOldBind->m_aName;
		if(DoEditBoxWithLabel(&BindDefault.m_LineInput, &Button, TCLocalize(BindDefault.m_pTitle), BindDefault.m_Bind.m_aName, pName, BINDCHAT_MAX_NAME) && BindDefault.m_LineInput.IsActive())
		{
			if(!pOldBind && pName[0] != '\0')
			{
				auto BindNew = BindDefault.m_Bind;
				str_copy(BindNew.m_aName, pName);
				GameClient()->m_BindChat.RemoveBind(pName); // Prevent duplicates
				GameClient()->m_BindChat.AddBind(BindNew);
				s_aTempName[0] = '\0';
			}
			if(pOldBind && pName[0] == '\0')
			{
				GameClient()->m_BindChat.RemoveBind(pName);
			}
		}
	};

	auto DoBindchatDefaults = [&](CUIRect &Column, const char *pTitle, std::vector<CBindChat::CBindDefault> &vBindchatDefaults) {
		s_SectionBoxes.push_back(Column);
		Column.HSplitTop(HeadlineHeight, &Label, &Column);
		Ui()->DoLabel(&Label, pTitle, HeadlineFontSize, TEXTALIGN_ML);
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		for(CBindChat::CBindDefault &BindchatDefault : vBindchatDefaults)
			DoBindchatDefault(Column, BindchatDefault);
		s_SectionBoxes.back().h = Column.y - s_SectionBoxes.back().y;
		Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	};

#if 1
	float SizeL = 0.0f, SizeR = 0.0f;
	for(auto &[pTitle, vBindDefaults] : CBindChat::BIND_DEFAULTS)
	{
		float &Size = SizeL > SizeR ? SizeR : SizeL;
		CUIRect &Column = SizeL > SizeR ? RightView : LeftView;
		DoBindchatDefaults(Column, TCLocalize(pTitle), vBindDefaults);
		Size += vBindDefaults.size() * (MarginSmall + LineSize) + HeadlineHeight + HeadlineFontSize + MarginSmall * 2.0f;
	}

#endif
	// Scroll
	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = std::max(LeftView.y, RightView.y) + MarginSmall * 2.0f;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegion.AddRect(ScrollRegion);
	s_ScrollRegion.End();
}

void CMenus::RenderSettingsTClientWarList(CUIRect MainView)
{
	CUIRect RightView, LeftView, Column1, Column2, Column3, Column4, Button, ButtonL, ButtonR, Label;

	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.VSplitMid(&LeftView, &RightView, Margin);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	// WAR LIST will have 4 columns
	//  [War entries] - [Entry Editing] - [Group Types] - [Recent Players]
	//									 [Group Editing]

	// putting this here so it can be updated by the entry list
	static char s_aEntryName[MAX_NAME_LENGTH];
	static char s_aEntryClan[MAX_CLAN_LENGTH];
	static char s_aEntryReason[MAX_WARLIST_REASON_LENGTH];
	static bool s_IsClan = false;
	static bool s_IsName = true;

	LeftView.VSplitMid(&Column1, &Column2, Margin);
	RightView.VSplitMid(&Column3, &Column4, Margin);

	// ======WAR ENTRIES======
	static CWarEntry *s_pSelectedEntry = nullptr;
	static CWarType *s_pSelectedType = GameClient()->m_WarList.m_WarTypes[0];
	{
		Column1.HSplitTop(HeadlineHeight, &Label, &Column1);
		Label.VSplitRight(25.0f, &Label, &Button);
		Ui()->DoLabel(&Label, TCLocalize("War Entries"), HeadlineFontSize, TEXTALIGN_ML);
		Column1.HSplitTop(MarginSmall, nullptr, &Column1);

		static CButtonContainer s_ReverseEntries;
		static bool s_Reversed = true;
		if(Ui()->DoButton_FontIcon(&s_ReverseEntries, s_Reversed ? FontIcon::CHEVRON_UP : FontIcon::CHEVRON_DOWN, 0, &Button, IGraphics::CORNER_ALL))
		{
			s_Reversed = !s_Reversed;
		}

		CUIRect EntriesSearch;
		Column1.HSplitBottom(25.0f, &Column1, &EntriesSearch);
		EntriesSearch.HSplitTop(MarginSmall, nullptr, &EntriesSearch);

		// Filter the list
		static CLineInputBuffered<128> s_EntriesFilterInput;
		std::vector<CWarEntry *> vpFilteredEntries;
		for(CWarEntry &Entry : GameClient()->m_WarList.m_vWarEntries)
		{
			if(str_find_nocase(Entry.m_aName, s_EntriesFilterInput.GetString()))
				vpFilteredEntries.push_back(&Entry);
			else if(str_find_nocase(Entry.m_aClan, s_EntriesFilterInput.GetString()))
				vpFilteredEntries.push_back(&Entry);
			else if(str_find_nocase(Entry.m_pWarType->m_aWarName, s_EntriesFilterInput.GetString()))
				vpFilteredEntries.push_back(&Entry);
		}
		if(s_Reversed)
			std::reverse(vpFilteredEntries.begin(), vpFilteredEntries.end());

		int SelectedOldEntry = -1;
		static CListBox s_EntriesListBox;
		s_EntriesListBox.DoStart(35.0f, vpFilteredEntries.size(), 1, 2, SelectedOldEntry, &Column1);

		static std::vector<unsigned char> s_vItemIds;
		static std::vector<CButtonContainer> s_vDeleteButtons;

		const int MaxEntries = GameClient()->m_WarList.m_vWarEntries.size();
		s_vItemIds.resize(MaxEntries);
		s_vDeleteButtons.resize(MaxEntries);

		for(size_t i = 0; i < vpFilteredEntries.size(); i++)
		{
			CWarEntry *pEntry = vpFilteredEntries[i];

			if(s_pSelectedEntry && pEntry == s_pSelectedEntry)
				SelectedOldEntry = i;

			const CListboxItem Item = s_EntriesListBox.DoNextItem(&s_vItemIds[i], SelectedOldEntry >= 0 && (size_t)SelectedOldEntry == i);
			if(!Item.m_Visible)
				continue;

			CUIRect EntryRect, DeleteButton, EntryTypeRect, WarType, ToolTip;
			Item.m_Rect.Margin(0.0f, &EntryRect);
			EntryRect.VSplitLeft(26.0f, &DeleteButton, &EntryRect);
			DeleteButton.HMargin(7.5f, &DeleteButton);
			DeleteButton.VSplitLeft(MarginSmall, nullptr, &DeleteButton);
			DeleteButton.VSplitRight(MarginExtraSmall, &DeleteButton, nullptr);

			if(Ui()->DoButton_FontIcon(&s_vDeleteButtons[i], FontIcon::TRASH, 0, &DeleteButton, IGraphics::CORNER_ALL))
				GameClient()->m_WarList.RemoveWarEntry(pEntry);

			bool IsClan = false;
			char aBuf[32];
			if(str_comp(pEntry->m_aClan, "") != 0)
			{
				str_copy(aBuf, pEntry->m_aClan);
				IsClan = true;
			}
			else
			{
				str_copy(aBuf, pEntry->m_aName);
			}
			EntryRect.VSplitLeft(35.0f, &EntryTypeRect, &EntryRect);

			if(IsClan)
			{
				RenderFontIcon(EntryTypeRect, FontIcon::ICON_USERS, 18.0f, TEXTALIGN_MC);
			}
			else
			{
				// TODO: stop misusing this function
				// TODO: render the real skin with skin remembering component (to be added)
				RenderDevSkin(EntryTypeRect.Center(), 35.0f, "default", "default", false, 0, 0, 0, false, false);
			}

			if(str_comp(pEntry->m_aReason, "") != 0)
			{
				EntryRect.VSplitRight(20.0f, &EntryRect, &ToolTip);
				RenderFontIcon(ToolTip, FontIcon::COMMENT, 18.0f, TEXTALIGN_MC);
				GameClient()->m_Tooltips.DoToolTip(&s_vItemIds[i], &ToolTip, pEntry->m_aReason);
				GameClient()->m_Tooltips.SetFadeTime(&s_vItemIds[i], 0.0f);
			}

			EntryRect.HMargin(MarginExtraSmall, &EntryRect);
			EntryRect.HSplitMid(&EntryRect, &WarType, MarginSmall);

			Ui()->DoLabel(&EntryRect, aBuf, StandardFontSize, TEXTALIGN_ML);
			TextRender()->TextColor(pEntry->m_pWarType->m_Color);
			Ui()->DoLabel(&WarType, pEntry->m_pWarType->m_aWarName, StandardFontSize, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		const int NewSelectedEntry = s_EntriesListBox.DoEnd();
		if(SelectedOldEntry != NewSelectedEntry || (SelectedOldEntry >= 0 && Ui()->HotItem() == &s_vItemIds[NewSelectedEntry] && Ui()->MouseButtonClicked(0)))
		{
			s_pSelectedEntry = vpFilteredEntries[NewSelectedEntry];
			if(!Ui()->LastMouseButton(1) && !Ui()->LastMouseButton(2))
			{
				str_copy(s_aEntryName, s_pSelectedEntry->m_aName);
				str_copy(s_aEntryClan, s_pSelectedEntry->m_aClan);
				str_copy(s_aEntryReason, s_pSelectedEntry->m_aReason);
				if(str_comp(s_pSelectedEntry->m_aClan, "") != 0)
				{
					s_IsName = false;
					s_IsClan = true;
				}
				else
				{
					s_IsName = true;
					s_IsClan = false;
				}
				s_pSelectedType = s_pSelectedEntry->m_pWarType;
			}
		}

		Ui()->DoEditBox_Search(&s_EntriesFilterInput, &EntriesSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
	}

	// ======WAR ENTRY EDITING======
	Column2.HSplitTop(HeadlineHeight, &Label, &Column2);
	Label.VSplitRight(25.0f, &Label, &Button);
	Ui()->DoLabel(&Label, TCLocalize("Edit Entry"), HeadlineFontSize, TEXTALIGN_ML);
	Column2.HSplitTop(MarginSmall, nullptr, &Column2);
	Column2.HSplitTop(HeadlineFontSize, &Button, &Column2);

	Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
	static CLineInput s_NameInput;
	s_NameInput.SetBuffer(s_aEntryName, sizeof(s_aEntryName));
	s_NameInput.SetEmptyText(TCLocalize("Name"));
	if(s_IsName)
	{
		Ui()->DoEditBox(&s_NameInput, &ButtonL, 12.0f);
	}
	else
	{
		ButtonL.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), 15, 3.0f);
		Ui()->ClipEnable(&ButtonL);
		ButtonL.VMargin(2.0f, &ButtonL);
		s_NameInput.Render(&ButtonL, 12.0f, TEXTALIGN_ML, false, -1.0f, 0.0f);
		Ui()->ClipDisable();
	}

	static CLineInput s_ClanInput;
	s_ClanInput.SetBuffer(s_aEntryClan, sizeof(s_aEntryClan));
	s_ClanInput.SetEmptyText(TCLocalize("Clan"));
	if(s_IsClan)
		Ui()->DoEditBox(&s_ClanInput, &ButtonR, 12.0f);
	else
	{
		ButtonR.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.25f), 15, 3.0f);
		Ui()->ClipEnable(&ButtonR);
		ButtonR.VMargin(2.0f, &ButtonR);
		s_ClanInput.Render(&ButtonR, 12.0f, TEXTALIGN_ML, false, -1.0f, 0.0f);
		Ui()->ClipDisable();
	}

	Column2.HSplitTop(MarginSmall, nullptr, &Column2);
	Column2.HSplitTop(LineSize, &Button, &Column2);
	Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
	static unsigned char s_NameRadio, s_ClanRadio;
	if(DoButton_CheckBox_Common(&s_NameRadio, TCLocalize("Name"), s_IsName ? "X" : "", &ButtonL, BUTTONFLAG_LEFT))
	{
		s_IsName = true;
		s_IsClan = false;
	}
	if(DoButton_CheckBox_Common(&s_ClanRadio, TCLocalize("Clan"), s_IsClan ? "X" : "", &ButtonR, BUTTONFLAG_LEFT))
	{
		s_IsName = false;
		s_IsClan = true;
	}
	if(!s_IsName)
		str_copy(s_aEntryName, "");
	if(!s_IsClan)
		str_copy(s_aEntryClan, "");

	Column2.HSplitTop(MarginSmall, nullptr, &Column2);
	Column2.HSplitTop(HeadlineFontSize, &Button, &Column2);
	static CLineInput s_ReasonInput;
	s_ReasonInput.SetBuffer(s_aEntryReason, sizeof(s_aEntryReason));
	s_ReasonInput.SetEmptyText(TCLocalize("Reason"));
	Ui()->DoEditBox(&s_ReasonInput, &Button, 12.0f);

	static CButtonContainer s_AddButton, s_OverrideButton;

	Column2.HSplitTop(MarginSmall, nullptr, &Column2);
	Column2.HSplitTop(LineSize * 2.0f, &Button, &Column2);
	Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);

	if(DoButtonLineSize_Menu(&s_OverrideButton, TCLocalize("Override Entry"), 0, &ButtonL, LineSize) && s_pSelectedEntry)
	{
		if(s_pSelectedEntry && s_pSelectedType && (str_comp(s_aEntryName, "") != 0 || str_comp(s_aEntryClan, "") != 0))
		{
			str_copy(s_pSelectedEntry->m_aName, s_aEntryName);
			str_copy(s_pSelectedEntry->m_aClan, s_aEntryClan);
			str_copy(s_pSelectedEntry->m_aReason, s_aEntryReason);
			s_pSelectedEntry->m_pWarType = s_pSelectedType;
			GameClient()->m_WarList.MarkDirty();
		}
	}
	if(DoButtonLineSize_Menu(&s_AddButton, TCLocalize("Add Entry"), 0, &ButtonR, LineSize))
	{
		if(s_pSelectedType)
			GameClient()->m_WarList.AddWarEntry(s_aEntryName, s_aEntryClan, s_aEntryReason, s_pSelectedType->m_aWarName);
	}
	Column2.HSplitTop(MarginSmall, nullptr, &Column2);
	Column2.HSplitTop(HeadlineFontSize + MarginSmall, &Button, &Column2);
	if(s_pSelectedType)
	{
		float Shade = 0.0f;
		Button.Draw(ColorRGBA(Shade, Shade, Shade, 0.25f), 15, 3.0f);
		TextRender()->TextColor(s_pSelectedType->m_Color);
		Ui()->DoLabel(&Button, s_pSelectedType->m_aWarName, HeadlineFontSize, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	Column2.HSplitBottom(150.0f, nullptr, &Column2);

	Column2.HSplitTop(HeadlineHeight, &Label, &Column2);
	Ui()->DoLabel(&Label, TCLocalize("Settings"), HeadlineFontSize, TEXTALIGN_ML);
	Column2.HSplitTop(MarginSmall, nullptr, &Column2);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListAllowDuplicates, TCLocalize("Allow Duplicate Entries"), &g_Config.m_TcWarListAllowDuplicates, &Column2, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarList, TCLocalize("Enable warlist"), &g_Config.m_TcWarList, &Column2, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListChat, TCLocalize("Colors in chat"), &g_Config.m_TcWarListChat, &Column2, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListScoreboard, TCLocalize("Colors in scoreboard"), &g_Config.m_TcWarListScoreboard, &Column2, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListSpectate, TCLocalize("Colors in spectate select"), &g_Config.m_TcWarListSpectate, &Column2, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcWarListShowClan, TCLocalize("Show clan if war"), &g_Config.m_TcWarListShowClan, &Column2, LineSize);

	// ======WAR TYPE EDITING======

	Column3.HSplitTop(HeadlineHeight, &Label, &Column3);
	Ui()->DoLabel(&Label, TCLocalize("War Groups"), HeadlineFontSize, TEXTALIGN_ML);
	Column3.HSplitTop(MarginSmall, nullptr, &Column3);

	static char s_aTypeName[MAX_WARLIST_TYPE_LENGTH];
	static ColorRGBA s_GroupColor = ColorRGBA(1, 1, 1, 1);

	CUIRect WarTypeList;
	Column3.HSplitBottom(180.0f, &WarTypeList, &Column3);
	m_pRemoveWarType = nullptr;
	int SelectedOldType = -1;
	static CListBox s_WarTypeListBox;
	s_WarTypeListBox.DoStart(25.0f, GameClient()->m_WarList.m_WarTypes.size(), 1, 2, SelectedOldType, &WarTypeList, true, IGraphics::CORNER_ALL, true);

	static std::vector<unsigned char> s_vTypeItemIds;
	static std::vector<CButtonContainer> s_vTypeDeleteButtons;

	const int MaxTypes = GameClient()->m_WarList.m_WarTypes.size();
	s_vTypeItemIds.resize(MaxTypes);
	s_vTypeDeleteButtons.resize(MaxTypes);

	for(int i = 0; i < (int)GameClient()->m_WarList.m_WarTypes.size(); i++)
	{
		CWarType *pType = GameClient()->m_WarList.m_WarTypes[i];

		if(!pType)
			continue;

		if(s_pSelectedType && pType == s_pSelectedType)
			SelectedOldType = i;

		const CListboxItem Item = s_WarTypeListBox.DoNextItem(&s_vTypeItemIds[i], SelectedOldType >= 0 && SelectedOldType == i);
		if(!Item.m_Visible)
			continue;

		CUIRect TypeRect, DeleteButton;
		Item.m_Rect.Margin(0.0f, &TypeRect);

		if(pType->m_Removable)
		{
			TypeRect.VSplitRight(20.0f, &TypeRect, &DeleteButton);
			DeleteButton.HSplitTop(20.0f, &DeleteButton, nullptr);
			DeleteButton.Margin(2.0f, &DeleteButton);
			if(DoButtonNoRect_FontIcon(&s_vTypeDeleteButtons[i], FontIcon::TRASH, 0, &DeleteButton, IGraphics::CORNER_ALL))
				m_pRemoveWarType = pType;
		}
		TextRender()->TextColor(pType->m_Color);
		Ui()->DoLabel(&TypeRect, pType->m_aWarName, StandardFontSize, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	const int NewSelectedType = s_WarTypeListBox.DoEnd();
	if((SelectedOldType != NewSelectedType && NewSelectedType >= 0) || (NewSelectedType >= 0 && Ui()->HotItem() == &s_vTypeItemIds[NewSelectedType] && Ui()->MouseButtonClicked(0)))
	{
		s_pSelectedType = GameClient()->m_WarList.m_WarTypes[NewSelectedType];
		if(!Ui()->LastMouseButton(1) && !Ui()->LastMouseButton(2))
		{
			str_copy(s_aTypeName, s_pSelectedType->m_aWarName);
			s_GroupColor = s_pSelectedType->m_Color;
		}
	}
	if(m_pRemoveWarType != nullptr)
	{
		char aMessage[256];
		str_format(aMessage, sizeof(aMessage),
			TCLocalize("Are you sure that you want to remove '%s' from your war groups?"),
			m_pRemoveWarType->m_aWarName);
		PopupConfirm(TCLocalize("Remove War Group"), aMessage, TCLocalize("Yes"), TCLocalize("No"), &CMenus::PopupConfirmRemoveWarType);
	}

	static CLineInput s_TypeNameInput;
	Column3.HSplitTop(MarginSmall, nullptr, &Column3);
	Column3.HSplitTop(HeadlineFontSize + MarginSmall, &Button, &Column3);
	s_TypeNameInput.SetBuffer(s_aTypeName, sizeof(s_aTypeName));
	s_TypeNameInput.SetEmptyText("Group Name");
	Ui()->DoEditBox(&s_TypeNameInput, &Button, 12.0f);
	static CButtonContainer s_AddGroupButton, s_OverrideGroupButton, s_GroupColorPicker;

	Column3.HSplitTop(MarginSmall, nullptr, &Column3);
	static unsigned int s_ColorValue = 0;
	s_ColorValue = color_cast<ColorHSLA>(s_GroupColor).Pack(false);
	ColorHSLA PickedColor = DoLine_ColorPicker(&s_GroupColorPicker, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column3, TCLocalize("Color"), &s_ColorValue, ColorRGBA(1.0f, 1.0f, 1.0f), true);
	s_GroupColor = color_cast<ColorRGBA>(PickedColor);

	Column3.HSplitTop(LineSize * 2.0f, &Button, &Column3);
	Button.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
	bool OverrideDisabled = NewSelectedType == 0;
	if(DoButtonLineSize_Menu(&s_OverrideGroupButton, TCLocalize("Override Group"), 0, &ButtonL, LineSize, OverrideDisabled) && s_pSelectedType)
	{
		if(s_pSelectedType && str_comp(s_aTypeName, "") != 0)
		{
			str_copy(s_pSelectedType->m_aWarName, s_aTypeName);
			s_pSelectedType->m_Color = s_GroupColor;
			GameClient()->m_WarList.MarkDirty();
		}
	}
	bool AddDisabled = str_comp(GameClient()->m_WarList.FindWarType(s_aTypeName)->m_aWarName, "none") != 0 || str_comp(s_aTypeName, "none") == 0;
	if(DoButtonLineSize_Menu(&s_AddGroupButton, TCLocalize("Add Group"), 0, &ButtonR, LineSize, AddDisabled))
	{
		GameClient()->m_WarList.AddWarType(s_aTypeName, s_GroupColor);
	}

	// ======ONLINE PLAYER LIST======

	Column4.HSplitTop(HeadlineHeight, &Label, &Column4);
	Ui()->DoLabel(&Label, TCLocalize("Online Players"), HeadlineFontSize, TEXTALIGN_ML);
	Column4.HSplitTop(MarginSmall, nullptr, &Column4);

	CUIRect PlayerSearch;
	Column4.HSplitBottom(25.0f, &Column4, &PlayerSearch);
	PlayerSearch.HSplitTop(MarginSmall, nullptr, &PlayerSearch);
	static CLineInputBuffered<128> s_PlayerSearchInput;
	Ui()->DoEditBox_Search(&s_PlayerSearchInput, &PlayerSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());

	CUIRect PlayerList;
	Column4.HSplitBottom(0.0f, &PlayerList, &Column4);
	static CListBox s_PlayerListBox;
	s_PlayerListBox.DoStart(30.0f, MAX_CLIENTS, 1, 2, -1, &PlayerList, true, IGraphics::CORNER_ALL, true);

	static std::vector<unsigned char> s_vPlayerItemIds;
	static std::vector<CButtonContainer> s_vNameButtons;
	static std::vector<CButtonContainer> s_vClanButtons;

	s_vPlayerItemIds.resize(MAX_CLIENTS);
	s_vNameButtons.resize(MAX_CLIENTS);
	s_vClanButtons.resize(MAX_CLIENTS);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameClient()->m_Snap.m_apPlayerInfos[i])
			continue;

		const auto &Client = GameClient()->m_aClients[i];

		if(!str_find_nocase(Client.m_aName, s_PlayerSearchInput.GetString()) &&
			!str_find_nocase(Client.m_aClan, s_PlayerSearchInput.GetString()))
			continue;

		const CListboxItem Item = s_PlayerListBox.DoNextItem(&s_vPlayerItemIds[i], false);
		if(!Item.m_Visible)
			continue;

		CUIRect PlayerRect, TeeRect, NameRect, ClanRect;
		Item.m_Rect.Margin(0.0f, &PlayerRect);
		PlayerRect.VSplitLeft(25.0f, &TeeRect, &PlayerRect);

		PlayerRect.VSplitMid(&NameRect, &ClanRect);
		PlayerRect = NameRect;
		PlayerRect.x = TeeRect.x;
		PlayerRect.w += TeeRect.w;
		TextRender()->TextColor(GameClient()->m_WarList.GetWarData(i).m_NameColor);
		ColorRGBA NameButtonColor = Ui()->CheckActiveItem(&s_vNameButtons[i]) ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f) :
											(Ui()->HotItem() == &s_vNameButtons[i] ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.33f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
		PlayerRect.Draw(NameButtonColor, IGraphics::CORNER_L, 5.0f);
		Ui()->DoLabel(&NameRect, Client.m_aName, StandardFontSize, TEXTALIGN_ML);
		if(Ui()->DoButtonLogic(&s_vNameButtons[i], false, &PlayerRect, BUTTONFLAG_LEFT))
		{
			s_IsName = true;
			s_IsClan = false;
			str_copy(s_aEntryName, Client.m_aName);
		}

		TextRender()->TextColor(GameClient()->m_WarList.GetWarData(i).m_ClanColor);
		ColorRGBA ClanButtonColor = Ui()->CheckActiveItem(&s_vClanButtons[i]) ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.75f) :
											(Ui()->HotItem() == &s_vClanButtons[i] ? ColorRGBA(1.0f, 1.0f, 1.0f, 0.33f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.0f));
		ClanRect.Draw(ClanButtonColor, IGraphics::CORNER_R, 5.0f);
		Ui()->DoLabel(&ClanRect, Client.m_aClan, StandardFontSize, TEXTALIGN_ML);
		if(Ui()->DoButtonLogic(&s_vClanButtons[i], false, &ClanRect, BUTTONFLAG_LEFT))
		{
			s_IsName = false;
			s_IsClan = true;
			str_copy(s_aEntryClan, Client.m_aClan);
		}
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		CTeeRenderInfo TeeInfo = Client.m_RenderInfo;
		TeeInfo.m_Size = 25.0f;
		RenderTeeCute(CAnimState::GetIdle(), &TeeInfo, 0, vec2(1.0f, 0.0f), TeeRect.Center() + vec2(-1.0f, 2.5f), true);
	}
	s_PlayerListBox.DoEnd();
}

void CMenus::RenderSettingsTClientStatusBar(CUIRect MainView)
{
	CUIRect LeftView, RightView, Button, Label, StatusBar;
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.HSplitBottom(100.0f, &MainView, &StatusBar);

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Status Bar"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcStatusBar, TCLocalize("Show status bar"), &g_Config.m_TcStatusBar, &LeftView, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcStatusBarLabels, TCLocalize("Show labels on status bar items"), &g_Config.m_TcStatusBarLabels, &LeftView, LineSize);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Ui()->DoScrollbarOption(&g_Config.m_TcStatusBarHeight, &g_Config.m_TcStatusBarHeight, &Button, TCLocalize("Status bar height"), 1, 16);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Local Time"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcStatusBar12HourClock, TCLocalize("Use 12 hour clock"), &g_Config.m_TcStatusBar12HourClock, &LeftView, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcStatusBarLocalTimeSeconds, TCLocalize("Show seconds on clock"), &g_Config.m_TcStatusBarLocalTimeSeconds, &LeftView, LineSize);
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Colors"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	static CButtonContainer s_StatusbarColor, s_StatusbarTextColor;

	DoLine_ColorPicker(&s_StatusbarColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, TCLocalize("Status bar color"), &g_Config.m_TcStatusBarColor, ColorRGBA(0.0f, 0.0f, 0.0f), false);
	DoLine_ColorPicker(&s_StatusbarTextColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &LeftView, TCLocalize("Text color"), &g_Config.m_TcStatusBarTextColor, ColorRGBA(1.0f, 1.0f, 1.0f), false);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Ui()->DoScrollbarOption(&g_Config.m_TcStatusBarAlpha, &g_Config.m_TcStatusBarAlpha, &Button, TCLocalize("Status bar alpha"), 0, 100);
	LeftView.HSplitTop(LineSize, &Button, &LeftView);
	Ui()->DoScrollbarOption(&g_Config.m_TcStatusBarTextAlpha, &g_Config.m_TcStatusBarTextAlpha, &Button, TCLocalize("Text alpha"), 0, 100);

	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("Status Bar Codes:"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("a = Angle"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("p = Ping"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("d = Prediction"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("c = Position"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("l = Local Time"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("r = Race Time"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("f = FPS"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("v = Velocity"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("z = Zoom"), FontSize, TEXTALIGN_ML);
	RightView.HSplitTop(LineSize, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("_ or ' ' = Space"), FontSize, TEXTALIGN_ML);
	static int s_SelectedItem = -1;
	static int s_TypeSelectedOld = -1;

	CUIRect StatusScheme, StatusButtons, ItemLabel;
	static CButtonContainer s_ApplyButton, s_AddButton, s_RemoveButton;
	StatusBar.HSplitBottom(LineSize + MarginSmall, &StatusBar, &StatusScheme);
	StatusBar.HSplitTop(LineSize + MarginSmall, &ItemLabel, &StatusBar);
	StatusScheme.HSplitTop(MarginSmall, nullptr, &StatusScheme);

	if(s_TypeSelectedOld >= 0)
		Ui()->DoLabel(&ItemLabel, GameClient()->m_StatusBar.m_StatusItemTypes[s_TypeSelectedOld].m_aDesc, FontSize, TEXTALIGN_ML);

	StatusScheme.VSplitMid(&StatusButtons, &StatusScheme, MarginSmall);
	StatusScheme.VSplitMid(&Label, &StatusScheme, MarginSmall);
	StatusScheme.VSplitMid(&StatusScheme, &Button, MarginSmall);
	if(DoButton_Menu(&s_ApplyButton, TCLocalize("Apply"), 0, &Button))
	{
		GameClient()->m_StatusBar.ApplyStatusBarScheme(g_Config.m_TcStatusBarScheme);
		GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
		s_SelectedItem = -1;
	}
	Ui()->DoLabel(&Label, TCLocalize("Status Scheme:"), FontSize, TEXTALIGN_MR);
	static CLineInput s_StatusScheme(g_Config.m_TcStatusBarScheme, sizeof(g_Config.m_TcStatusBarScheme));
	s_StatusScheme.SetEmptyText("");
	Ui()->DoEditBox(&s_StatusScheme, &StatusScheme, EditBoxFontSize);

	static std::vector<const char *> s_DropDownNames = {};
	for(const CStatusItem &StatusItemType : GameClient()->m_StatusBar.m_StatusItemTypes)
		if(s_DropDownNames.size() != GameClient()->m_StatusBar.m_StatusItemTypes.size())
			s_DropDownNames.push_back(StatusItemType.m_aName);

	static CUi::SDropDownState s_DropDownState;
	static CScrollRegion s_DropDownScrollRegion;
	s_DropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_DropDownScrollRegion;
	CUIRect DropDownRect;

	StatusButtons.VSplitMid(&DropDownRect, &StatusButtons, MarginSmall);
	const int TypeSelectedNew = Ui()->DoDropDown(&DropDownRect, s_TypeSelectedOld, s_DropDownNames.data(), s_DropDownNames.size(), s_DropDownState);
	if(s_TypeSelectedOld != TypeSelectedNew)
	{
		s_TypeSelectedOld = TypeSelectedNew;
		if(s_SelectedItem >= 0)
		{
			GameClient()->m_StatusBar.m_StatusBarItems[s_SelectedItem] = &GameClient()->m_StatusBar.m_StatusItemTypes[s_TypeSelectedOld];
			GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
		}
	}
	CUIRect ButtonL, ButtonR;
	StatusButtons.VSplitMid(&ButtonL, &ButtonR, MarginSmall);
	size_t NumItems = GameClient()->m_StatusBar.m_StatusBarItems.size();
	if(DoButton_Menu(&s_AddButton, TCLocalize("Add Item"), 0, &ButtonL) && s_TypeSelectedOld >= 0 && NumItems < 128)
	{
		GameClient()->m_StatusBar.m_StatusBarItems.push_back(&GameClient()->m_StatusBar.m_StatusItemTypes[s_TypeSelectedOld]);
		GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
		s_SelectedItem = (int)GameClient()->m_StatusBar.m_StatusBarItems.size() - 1;
	}
	if(DoButton_Menu(&s_RemoveButton, TCLocalize("Remove Item"), 0, &ButtonR) && s_SelectedItem >= 0)
	{
		GameClient()->m_StatusBar.m_StatusBarItems.erase(GameClient()->m_StatusBar.m_StatusBarItems.begin() + s_SelectedItem);
		GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
		s_SelectedItem = -1;
	}

	// color_cast<ColorRGBA>(ColorHSLA(g_Config.m_TcStatusBarColor)).WithAlpha(0.5f)
	StatusBar.Draw(ColorRGBA(0, 0, 0, 0.5f), IGraphics::CORNER_ALL, 5.0f);
	int ItemCount = GameClient()->m_StatusBar.m_StatusBarItems.size();
	float AvailableWidth = StatusBar.w;
	// AvailableWidth -= (ItemCount - 1) * MarginSmall;
	AvailableWidth -= MarginSmall;
	StatusBar.VSplitLeft(MarginExtraSmall, nullptr, &StatusBar);
	float ItemWidth = AvailableWidth / (float)ItemCount;
	CUIRect StatusItemButton;
	static std::vector<CButtonContainer *> s_pItemButtons;
	static std::vector<CButtonContainer> s_ItemButtons;
	static vec2 s_ActivePos = vec2(0.0f, 0.0f);
	class CSwapItem
	{
	public:
		vec2 m_InitialPosition = vec2(0.0f, 0.0f);
		float m_Duration = 0.0f;
	};

	static std::vector<CSwapItem> s_ItemSwaps;

	if((int)s_ItemButtons.size() != ItemCount)
	{
		s_ItemSwaps.resize(ItemCount);
		s_pItemButtons.resize(ItemCount);
		s_ItemButtons.resize(ItemCount);
		for(int i = 0; i < ItemCount; ++i)
		{
			s_pItemButtons[i] = &s_ItemButtons[i];
		}
	}
	bool StatusItemActive = false;
	int HotStatusIndex = 0;
	for(int i = 0; i < ItemCount; ++i)
	{
		if(Ui()->ActiveItem() == s_pItemButtons[i])
		{
			StatusItemActive = true;
			HotStatusIndex = i;
		}
	}

	for(int i = 0; i < ItemCount; ++i)
	{
		// if(i > 0)
		//	StatusBar.VSplitLeft(MarginSmall, nullptr, &StatusBar);
		StatusBar.VSplitLeft(ItemWidth, &StatusItemButton, &StatusBar);
		StatusItemButton.HMargin(MarginSmall, &StatusItemButton);
		StatusItemButton.VMargin(MarginExtraSmall, &StatusItemButton);
		CStatusItem *StatusItem = GameClient()->m_StatusBar.m_StatusBarItems[i];
		ColorRGBA Col = ColorRGBA(1.0f, 1.0f, 1.0f, 0.5f);
		if(s_SelectedItem == i)
			Col = ColorRGBA(1.0f, 0.35f, 0.35f, 0.75f);
		CUIRect TempItemButton = StatusItemButton;
		TempItemButton.y = 0, TempItemButton.h = 10000.0f;
		if(StatusItemActive && Ui()->ActiveItem() != s_pItemButtons[i] && Ui()->MouseInside(&TempItemButton))
		{
			std::swap(s_pItemButtons[i], s_pItemButtons[HotStatusIndex]);
			std::swap(GameClient()->m_StatusBar.m_StatusBarItems[i], GameClient()->m_StatusBar.m_StatusBarItems[HotStatusIndex]);
			s_SelectedItem = -2;
			s_ItemSwaps[HotStatusIndex].m_InitialPosition = vec2(StatusItemButton.x, StatusItemButton.y);
			s_ItemSwaps[HotStatusIndex].m_Duration = 0.15f;
			s_ItemSwaps[i].m_InitialPosition = vec2(s_ActivePos.x, s_ActivePos.y);
			s_ItemSwaps[i].m_Duration = 0.15f;
			GameClient()->m_StatusBar.UpdateStatusBarScheme(g_Config.m_TcStatusBarScheme);
		}
		TempItemButton = StatusItemButton;
		s_ItemSwaps[i].m_Duration = std::max(0.0f, s_ItemSwaps[i].m_Duration - Client()->RenderFrameTime());
		if(s_ItemSwaps[i].m_Duration > 0.0f)
		{
			float Progress = std::pow(2.0, -5.0 * (1.0 - s_ItemSwaps[i].m_Duration / 0.15f));
			TempItemButton.x = mix(TempItemButton.x, s_ItemSwaps[i].m_InitialPosition.x, Progress);
		}
		if(DoButtonLineSize_Menu(s_pItemButtons[i], StatusItem->m_aDisplayName, 0, &TempItemButton, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, Col))
		{
			if(s_SelectedItem == -2)
				s_SelectedItem++;
			else if(s_SelectedItem != i)
			{
				s_SelectedItem = i;
				for(int t = 0; t < (int)GameClient()->m_StatusBar.m_StatusItemTypes.size(); ++t)
					if(str_comp(GameClient()->m_StatusBar.m_StatusItemTypes[t].m_aName, StatusItem->m_aName) == 0)
						s_TypeSelectedOld = t;
			}
			else
			{
				s_SelectedItem = -1;
				s_TypeSelectedOld = -1;
			}
		}
		if(Ui()->ActiveItem() == s_pItemButtons[i])
			s_ActivePos = vec2(StatusItemButton.x, StatusItemButton.y);
	}
	if(!StatusItemActive)
		s_SelectedItem = std::max(-1, s_SelectedItem);
}

void CMenus::RenderSettingsTClientInfo(CUIRect MainView)
{
	CUIRect LeftView, RightView, Button, Label, LowerLeftView;
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);
	LeftView.HSplitMid(&LeftView, &LowerLeftView, 0.0f);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("TClient Links"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	static CButtonContainer s_DiscordButton, s_WebsiteButton, s_GithubButton, s_SupportButton;
	CUIRect ButtonLeft, ButtonRight;

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);
	if(DoButtonLineSize_Menu(&s_DiscordButton, TCLocalize("Discord"), 0, &ButtonLeft, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://discord.gg/fBvhH93Bt6");
	if(DoButtonLineSize_Menu(&s_WebsiteButton, TCLocalize("Website"), 0, &ButtonRight, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://tclient.app/");

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&ButtonLeft, &ButtonRight, MarginSmall);

	if(DoButtonLineSize_Menu(&s_GithubButton, TCLocalize("Github"), 0, &ButtonLeft, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://github.com/sjrc6/TaterClient-ddnet");
	if(DoButtonLineSize_Menu(&s_SupportButton, TCLocalize("Support ♥"), 0, &ButtonRight, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		Client()->ViewLink("https://ko-fi.com/Totar");

	LeftView = LowerLeftView;
	LeftView.HSplitBottom(LineSize * 4.0f + MarginSmall * 2.0f + HeadlineFontSize, nullptr, &LeftView);
	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Config Files"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	char aBuf[128 + IO_MAX_PATH_LENGTH];
	CUIRect TClientConfig, ProfilesFile, WarlistFile, ChatbindsFile;

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&TClientConfig, &ProfilesFile, MarginSmall);

	static CButtonContainer s_Config, s_Profiles, s_Warlist, s_Chatbinds;
	if(DoButtonLineSize_Menu(&s_Config, TCLocalize("TClient Settings"), 0, &TClientConfig, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::TCLIENT].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}
	if(DoButtonLineSize_Menu(&s_Profiles, TCLocalize("Profiles"), 0, &ProfilesFile, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::TCLIENTPROFILES].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&WarlistFile, &ChatbindsFile, MarginSmall);

	if(DoButtonLineSize_Menu(&s_Warlist, TCLocalize("War List"), 0, &WarlistFile, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::TCLIENTWARLIST].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}
	if(DoButtonLineSize_Menu(&s_Chatbinds, TCLocalize("Chat Binds"), 0, &ChatbindsFile, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::TCLIENTCHATBINDS].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	CUIRect AClientConfig, RaceStatsHistory;
	LeftView.HSplitTop(LineSize * 2.0f, &Button, &LeftView);
	Button.VSplitMid(&AClientConfig, &RaceStatsHistory, MarginSmall);

	static CButtonContainer s_AClientConfig, s_RaceStatsHistory;
	if(DoButtonLineSize_Menu(&s_AClientConfig, TCLocalize("AClient Settings"), 0, &AClientConfig, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::ACLIENT].m_aConfigPath, aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}
	if(DoButtonLineSize_Menu(&s_RaceStatsHistory, TCLocalize("RaceStats History"), 0, &RaceStatsHistory, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "aclient_racestats_history.txt", aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}

	// =======RIGHT VIEW========

	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("TClient Developers"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);

	const float TeeSize = 50.0f;
	const float CardSize = TeeSize + MarginSmall;
	CUIRect TeeRect, DevCardRect;
	static CButtonContainer s_LinkButton1, s_LinkButton2, s_LinkButton3, s_LinkButton4, s_LinkButton5;
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(LineSize, "Tater"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = LineSize, Button.h = LineSize, Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "Tater", LineSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton1, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/sjrc6");
		RenderDevSkin(TeeRect.Center(), 50.0f, "glow_mermyfox", "mermyfox", true, 0, 0, 0, false, true, ColorRGBA(0.92f, 0.29f, 0.48f, 1.0f), ColorRGBA(0.55f, 0.64f, 0.76f, 1.0f));
	}
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(LineSize, "SollyBunny / bun bun"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = LineSize, Button.h = LineSize, Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "SollyBunny / bun bun", LineSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton3, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/SollyBunny");
		RenderDevSkin(TeeRect.Center(), 50.0f, "tuzi", "tuzi", false, 0, 0, 2, true, true, true);
	}
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(LineSize, "PeBox"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = LineSize, Button.h = LineSize, Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "PeBox", LineSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton2, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/danielkempf");
		RenderDevSkin(TeeRect.Center(), 50.0f, "greyfox", "greyfox", true, 0, 0, 2, false, true, ColorRGBA(0.00f, 0.09f, 1.00f, 1.00f), ColorRGBA(1.00f, 0.92f, 0.00f, 1.00f));
	}
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(LineSize, "Teero"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = LineSize, Button.h = LineSize, Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "Teero", LineSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton4, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/Teero888");
		RenderDevSkin(TeeRect.Center(), 50.0f, "glow_mermyfox", "mermyfox", true, 0, 0, 0, false, true, ColorRGBA(1.00f, 1.00f, 1.00f, 1.00f), ColorRGBA(1.00f, 0.02f, 0.13f, 1.00f));
	}
	{
		RightView.HSplitTop(CardSize, &DevCardRect, &RightView);
		DevCardRect.VSplitLeft(CardSize, &TeeRect, &Label);
		Label.VSplitLeft(TextRender()->TextWidth(LineSize, "ChillerDragon"), &Label, &Button);
		Button.VSplitLeft(MarginSmall, nullptr, &Button);
		Button.w = LineSize, Button.h = LineSize, Button.y = Label.y + (Label.h / 2.0f - Button.h / 2.0f);
		Ui()->DoLabel(&Label, "ChillerDragon", LineSize, TEXTALIGN_ML);
		if(Ui()->DoButton_FontIcon(&s_LinkButton5, FontIcon::ARROW_UP_RIGHT_FROM_SQUARE, 0, &Button, IGraphics::CORNER_ALL))
			Client()->ViewLink("https://github.com/ChillerDragon");
		RenderDevSkin(TeeRect.Center(), 50.0f, "glow_greensward", "greensward", false, 0, 0, 0, false, true, ColorRGBA(1.00f, 1.00f, 1.00f, 1.00f), ColorRGBA(1.00f, 0.02f, 0.13f, 1.00f));
	}

	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("Hide Settings Tabs"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	CUIRect LeftSettings, RightSettings;

	RightView.VSplitMid(&LeftSettings, &RightSettings, MarginSmall);
	RightView.HSplitTop(LineSize * 3.5f, nullptr, &RightView);

	const char *apTabNames[] = {
		TCLocalize("Settings"),
		TCLocalize("Bind Wheel"),
		TCLocalize("War List"),
		TCLocalize("Chat Binds"),
		TCLocalize("Status Bar"),
		TCLocalize("Race Stats"),
		TCLocalize("Info")};
	static int s_aShowTabs[NUMBER_OF_TCLIENT_TABS] = {};
	for(int i = 0; i < NUMBER_OF_TCLIENT_TABS - 1; ++i)
	{
		DoButton_CheckBoxAutoVMarginAndSet(&s_aShowTabs[i], apTabNames[i], &s_aShowTabs[i], i % 2 == 0 ? &LeftSettings : &RightSettings, LineSize);
		SetFlag(g_Config.m_TcTClientSettingsTabs, i, s_aShowTabs[i]);
	}

	RightView.HSplitTop(HeadlineHeight, &Label, &RightView);
	Ui()->DoLabel(&Label, TCLocalize("Integration"), HeadlineFontSize, TEXTALIGN_ML);
	RightView.HSplitTop(MarginSmall, nullptr, &RightView);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcDiscordRPC, TCLocalize("Enable Discord Integration"), &g_Config.m_TcDiscordRPC, &RightView, LineSize);
}

void CMenus::RenderSettingsTClientProfiles(CUIRect MainView)
{
	int *pCurrentUseCustomColor = m_Dummy ? &g_Config.m_ClDummyUseCustomColor : &g_Config.m_ClPlayerUseCustomColor;

	const char *pCurrentSkinName = m_Dummy ? g_Config.m_ClDummySkin : g_Config.m_ClPlayerSkin;
	const unsigned CurrentColorBody = *pCurrentUseCustomColor == 1 ? (m_Dummy ? g_Config.m_ClDummyColorBody : g_Config.m_ClPlayerColorBody) : -1;
	const unsigned CurrentColorFeet = *pCurrentUseCustomColor == 1 ? (m_Dummy ? g_Config.m_ClDummyColorFeet : g_Config.m_ClPlayerColorFeet) : -1;
	const int CurrentFlag = m_Dummy ? g_Config.m_ClDummyCountry : g_Config.m_PlayerCountry;
	const int Emote = m_Dummy ? g_Config.m_ClDummyDefaultEyes : g_Config.m_ClPlayerDefaultEyes;
	const char *pCurrentName = m_Dummy ? g_Config.m_ClDummyName : g_Config.m_PlayerName;
	const char *pCurrentClan = m_Dummy ? g_Config.m_ClDummyClan : g_Config.m_PlayerClan;

	const CProfile CurrentProfile(
		CurrentColorBody,
		CurrentColorFeet,
		CurrentFlag,
		Emote,
		pCurrentSkinName,
		pCurrentName,
		pCurrentClan);

	static int s_SelectedProfile = -1;

	CUIRect Label, Button;

	auto RenderProfile = [&](CUIRect Rect, const CProfile &Profile, bool Main) {
		auto RenderCross = [&](CUIRect Cross, float MaxSize = 0.0f) {
			float MaxExtent = std::max(Cross.w, Cross.h);
			if(MaxSize > 0.0f && MaxExtent > MaxSize)
				MaxExtent = MaxSize;
			TextRender()->TextColor(ColorRGBA(1.0f, 0.0f, 0.0f));
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			const auto TextBoundingBox = TextRender()->TextBoundingBox(MaxExtent * 0.8f, FontIcon::XMARK);
			TextRender()->Text(Cross.x + (Cross.w - TextBoundingBox.m_W) / 2.0f, Cross.y + (Cross.h - TextBoundingBox.m_H) / 2.0f, MaxExtent * 0.8f, FontIcon::XMARK);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		};
		{
			CUIRect Skin;
			Rect.VSplitLeft(50.0f, &Skin, &Rect);
			if(!Main && Profile.m_SkinName[0] == '\0')
			{
				RenderCross(Skin, 20.0f);
			}
			else
			{
				CTeeRenderInfo TeeRenderInfo;
				TeeRenderInfo.Apply(GameClient()->m_Skins.Find(Profile.m_SkinName));
				TeeRenderInfo.ApplyColors(Profile.m_BodyColor >= 0 && Profile.m_FeetColor > 0, Profile.m_BodyColor, Profile.m_FeetColor);
				TeeRenderInfo.m_Size = 50.0f;
				const vec2 Pos = Skin.Center() + vec2(0.0f, TeeRenderInfo.m_Size / 10.0f); // Prevent overflow from hats
				vec2 Dir = vec2(1.0f, 0.0f);
				if(Main)
					RenderTeeCute(CAnimState::GetIdle(), &TeeRenderInfo, std::max(0, Profile.m_Emote), Dir, Pos, false);
				else
					RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeRenderInfo, std::max(0, Profile.m_Emote), Dir, Pos);
			}
		}
		Rect.VSplitLeft(5.0f, nullptr, &Rect);
		{
			CUIRect Colors;
			Rect.VSplitLeft(10.0f, &Colors, &Rect);
			CUIRect BodyColor{Colors.Center().x - 5.0f, Colors.Center().y - 11.0f, 10.0f, 10.0f};
			CUIRect FeetColor{Colors.Center().x - 5.0f, Colors.Center().y + 1.0f, 10.0f, 10.0f};
			if(Profile.m_BodyColor >= 0 && Profile.m_FeetColor > 0)
			{
				// Body Color
				Graphics()->DrawRect(BodyColor.x, BodyColor.y, BodyColor.w, BodyColor.h,
					color_cast<ColorRGBA>(ColorHSLA(Profile.m_BodyColor).UnclampLighting(ColorHSLA::DARKEST_LGT)).WithAlpha(1.0f),
					IGraphics::CORNER_ALL, 2.0f);
				// Feet Color;
				Graphics()->DrawRect(FeetColor.x, FeetColor.y, FeetColor.w, FeetColor.h,
					color_cast<ColorRGBA>(ColorHSLA(Profile.m_FeetColor).UnclampLighting(ColorHSLA::DARKEST_LGT)).WithAlpha(1.0f),
					IGraphics::CORNER_ALL, 2.0f);
			}
			else
			{
				RenderCross(BodyColor);
				RenderCross(FeetColor);
			}
		}
		Rect.VSplitLeft(5.0f, nullptr, &Rect);
		{
			CUIRect Flag;
			Rect.VSplitRight(50.0f, &Rect, &Flag);
			Flag = {Flag.x, Flag.y + (Flag.h - 25.0f) / 2.0f, Flag.w, 25.0f};
			if(Profile.m_CountryFlag == -2)
				RenderCross(Flag, 20.0f);
			else
				GameClient()->m_CountryFlags.Render(Profile.m_CountryFlag, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), Flag.x, Flag.y, Flag.w, Flag.h);
		}
		Rect.VSplitRight(5.0f, &Rect, nullptr);
		{
			const float Height = Rect.h / 3.0f;
			if(Main)
			{
				char aBuf[256];
				Rect.HSplitTop(Height, &Label, &Rect);
				str_format(aBuf, sizeof(aBuf), TCLocalize("Name: %s"), Profile.m_Name);
				Ui()->DoLabel(&Label, aBuf, Height / LineSize * FontSize, TEXTALIGN_ML);
				Rect.HSplitTop(Height, &Label, &Rect);
				str_format(aBuf, sizeof(aBuf), TCLocalize("Clan: %s"), Profile.m_Clan);
				Ui()->DoLabel(&Label, aBuf, Height / LineSize * FontSize, TEXTALIGN_ML);
				Rect.HSplitTop(Height, &Label, &Rect);
				str_format(aBuf, sizeof(aBuf), TCLocalize("Skin: %s"), Profile.m_SkinName);
				Ui()->DoLabel(&Label, aBuf, Height / LineSize * FontSize, TEXTALIGN_ML);
			}
			else
			{
				Rect.HSplitTop(Height, &Label, &Rect);
				Ui()->DoLabel(&Label, Profile.m_Name, Height / LineSize * FontSize, TEXTALIGN_ML);
				Rect.HSplitTop(Height, &Label, &Rect);
				Ui()->DoLabel(&Label, Profile.m_Clan, Height / LineSize * FontSize, TEXTALIGN_ML);
			}
		}
	};

	{
		CUIRect Top;
		MainView.HSplitTop(160.0f, &Top, &MainView);
		CUIRect Profiles, Settings, Actions;
		Top.VSplitLeft(300.0f, &Profiles, &Top);
		{
			CUIRect Skin;
			Profiles.HSplitTop(LineSize, &Label, &Profiles);
			Ui()->DoLabel(&Label, TCLocalize("Your profile"), FontSize, TEXTALIGN_ML);
			Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
			Profiles.HSplitTop(50.0f, &Skin, &Profiles);
			RenderProfile(Skin, CurrentProfile, true);

			// After load
			if(s_SelectedProfile != -1 && s_SelectedProfile < (int)GameClient()->m_SkinProfiles.m_Profiles.size())
			{
				Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
				Profiles.HSplitTop(LineSize, &Label, &Profiles);
				Ui()->DoLabel(&Label, TCLocalize("After Load"), FontSize, TEXTALIGN_ML);
				Profiles.HSplitTop(MarginSmall, nullptr, &Profiles);
				Profiles.HSplitTop(50.0f, &Skin, &Profiles);

				CProfile LoadProfile = CurrentProfile;
				const CProfile &Profile = GameClient()->m_SkinProfiles.m_Profiles[s_SelectedProfile];
				if(g_Config.m_TcProfileSkin && strlen(Profile.m_SkinName) != 0)
					str_copy(LoadProfile.m_SkinName, Profile.m_SkinName);
				if(g_Config.m_TcProfileColors && Profile.m_BodyColor != -1 && Profile.m_FeetColor != -1)
				{
					LoadProfile.m_BodyColor = Profile.m_BodyColor;
					LoadProfile.m_FeetColor = Profile.m_FeetColor;
				}
				if(g_Config.m_TcProfileEmote && Profile.m_Emote != -1)
					LoadProfile.m_Emote = Profile.m_Emote;
				if(g_Config.m_TcProfileName && strlen(Profile.m_Name) != 0)
					str_copy(LoadProfile.m_Name, Profile.m_Name);
				if(g_Config.m_TcProfileClan && (strlen(Profile.m_Clan) != 0 || g_Config.m_TcProfileOverwriteClanWithEmpty))
					str_copy(LoadProfile.m_Clan, Profile.m_Clan);
				if(g_Config.m_TcProfileFlag && Profile.m_CountryFlag != -2)
					LoadProfile.m_CountryFlag = Profile.m_CountryFlag;

				RenderProfile(Skin, LoadProfile, true);
			}
		}
		Top.VSplitLeft(20.0f, nullptr, &Top);
		Top.VSplitMid(&Settings, &Actions, 20.0f);
		{
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileSkin, TCLocalize("Save/Load Skin"), &g_Config.m_TcProfileSkin, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileColors, TCLocalize("Save/Load Colors"), &g_Config.m_TcProfileColors, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileEmote, TCLocalize("Save/Load Emote"), &g_Config.m_TcProfileEmote, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileName, TCLocalize("Save/Load Name"), &g_Config.m_TcProfileName, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileClan, TCLocalize("Save/Load Clan"), &g_Config.m_TcProfileClan, &Settings, LineSize);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcProfileFlag, TCLocalize("Save/Load Flag"), &g_Config.m_TcProfileFlag, &Settings, LineSize);
		}
		{
			Actions.HSplitTop(30.0f, &Button, &Actions);
			static CButtonContainer s_LoadButton;
			if(DoButton_Menu(&s_LoadButton, TCLocalize("Load"), 0, &Button))
			{
				if(s_SelectedProfile != -1 && s_SelectedProfile < (int)GameClient()->m_SkinProfiles.m_Profiles.size())
				{
					CProfile LoadProfile = GameClient()->m_SkinProfiles.m_Profiles[s_SelectedProfile];
					GameClient()->m_SkinProfiles.ApplyProfile(m_Dummy, LoadProfile);
				}
			}
			Actions.HSplitTop(5.0f, nullptr, &Actions);

			Actions.HSplitTop(30.0f, &Button, &Actions);
			static CButtonContainer s_SaveButton;
			if(DoButton_Menu(&s_SaveButton, TCLocalize("Save"), 0, &Button))
			{
				GameClient()->m_SkinProfiles.AddProfile(
					g_Config.m_TcProfileColors ? CurrentColorBody : -1,
					g_Config.m_TcProfileColors ? CurrentColorFeet : -1,
					g_Config.m_TcProfileFlag ? CurrentFlag : -2,
					g_Config.m_TcProfileEmote ? Emote : -1,
					g_Config.m_TcProfileSkin ? pCurrentSkinName : "",
					g_Config.m_TcProfileName ? pCurrentName : "",
					g_Config.m_TcProfileClan ? pCurrentClan : "");
			}
			Actions.HSplitTop(5.0f, nullptr, &Actions);

			static int s_AllowDelete;
			DoButton_CheckBoxAutoVMarginAndSet(&s_AllowDelete, TCLocalize("Enable Deleting"), &s_AllowDelete, &Actions, LineSize);
			Actions.HSplitTop(5.0f, nullptr, &Actions);

			if(s_AllowDelete)
			{
				Actions.HSplitTop(30.0f, &Button, &Actions);
				static CButtonContainer s_DeleteButton;
				if(DoButton_Menu(&s_DeleteButton, TCLocalize("Delete"), 0, &Button))
					if(s_SelectedProfile != -1 && s_SelectedProfile < (int)GameClient()->m_SkinProfiles.m_Profiles.size())
						GameClient()->m_SkinProfiles.m_Profiles.erase(GameClient()->m_SkinProfiles.m_Profiles.begin() + s_SelectedProfile);
				Actions.HSplitTop(5.0f, nullptr, &Actions);

				Actions.HSplitTop(30.0f, &Button, &Actions);
				static CButtonContainer s_OverrideButton;
				if(DoButton_Menu(&s_OverrideButton, TCLocalize("Override"), 0, &Button))
				{
					if(s_SelectedProfile != -1 && s_SelectedProfile < (int)GameClient()->m_SkinProfiles.m_Profiles.size())
					{
						GameClient()->m_SkinProfiles.m_Profiles[s_SelectedProfile] = CProfile(
							g_Config.m_TcProfileColors ? CurrentColorBody : -1,
							g_Config.m_TcProfileColors ? CurrentColorFeet : -1,
							g_Config.m_TcProfileFlag ? CurrentFlag : -2,
							g_Config.m_TcProfileEmote ? Emote : -1,
							g_Config.m_TcProfileSkin ? pCurrentSkinName : "",
							g_Config.m_TcProfileName ? pCurrentName : "",
							g_Config.m_TcProfileClan ? pCurrentClan : "");
					}
				}
			}
		}
	}
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	{
		CUIRect Options;
		MainView.HSplitTop(LineSize, &Options, &MainView);

		Options.VSplitLeft(150.0f, &Button, &Options);
		if(DoButton_CheckBox(&m_Dummy, TCLocalize("Dummy"), m_Dummy, &Button))
			m_Dummy = 1 - m_Dummy;

		Options.VSplitLeft(150.0f, &Button, &Options);
		static int s_CustomColorId = 0;
		if(DoButton_CheckBox(&s_CustomColorId, TCLocalize("Custom colors"), *pCurrentUseCustomColor, &Button))
		{
			*pCurrentUseCustomColor = *pCurrentUseCustomColor ? 0 : 1;
			SetNeedSendInfo();
		}

		Button = Options;
		if(DoButton_CheckBox(&g_Config.m_TcProfileOverwriteClanWithEmpty, TCLocalize("Overwrite clan even if empty"), g_Config.m_TcProfileOverwriteClanWithEmpty, &Button))
			g_Config.m_TcProfileOverwriteClanWithEmpty = 1 - g_Config.m_TcProfileOverwriteClanWithEmpty;
	}
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	{
		CUIRect SelectorRect;
		MainView.HSplitBottom(LineSize + MarginSmall, &MainView, &SelectorRect);
		SelectorRect.HSplitTop(MarginSmall, nullptr, &SelectorRect);

		static CButtonContainer s_ProfilesFile;
		SelectorRect.VSplitLeft(130.0f, &Button, &SelectorRect);
		if(DoButton_Menu(&s_ProfilesFile, TCLocalize("Profiles file"), 0, &Button))
		{
			char aBuf[IO_MAX_PATH_LENGTH];
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, s_aConfigDomains[ConfigDomain::TCLIENTPROFILES].m_aConfigPath, aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
	}

	const std::vector<CProfile> &ProfileList = GameClient()->m_SkinProfiles.m_Profiles;
	static CListBox s_ListBox;
	s_ListBox.DoStart(50.0f, ProfileList.size(), MainView.w / 200.0f, 3, s_SelectedProfile, &MainView, true, IGraphics::CORNER_ALL, true);

	static bool s_Indexes[1024];

	for(size_t i = 0; i < ProfileList.size(); ++i)
	{
		CListboxItem Item = s_ListBox.DoNextItem(&s_Indexes[i], s_SelectedProfile >= 0 && (size_t)s_SelectedProfile == i);
		if(!Item.m_Visible)
			continue;

		RenderProfile(Item.m_Rect, ProfileList[i], false);
	}

	s_SelectedProfile = s_ListBox.DoEnd();
}

void CMenus::RenderSettingsTClientConfigs(CUIRect MainView)
{
	// hi hello, this is a relatively self contained mess, sorry if you're forking or need to modify this -Tater

	struct SIntStage
	{
		int m_Value;
	};
	struct SStrStage
	{
		std::string m_Value;
	};
	struct SColStage
	{
		unsigned m_Value;
	};
	static std::unordered_map<const SConfigVariable *, SIntStage> s_StagedInts;
	static std::unordered_map<const SConfigVariable *, SStrStage> s_StagedStrs;
	static std::unordered_map<const SConfigVariable *, SColStage> s_StagedCols;

	struct SIntState
	{
		CLineInputNumber m_Input;
		int m_LastValue = 0;
		bool m_Inited = false;
	};
	struct SStrState
	{
		CLineInputBuffered<512> m_Input;
		bool m_Inited = false;
	};
	struct SColState
	{
		unsigned m_LastValue = 0;
		unsigned m_Working = 0;
		bool m_Inited = false;
	};
	static std::unordered_map<const SConfigVariable *, SIntState> s_IntInputs;
	static std::unordered_map<const SConfigVariable *, SStrState> s_StrInputs;
	static std::unordered_map<const SConfigVariable *, SColState> s_ColInputs;

	auto ClearStagedAndCaches = [&]() {
		s_StagedInts.clear();
		s_StagedStrs.clear();
		s_StagedCols.clear();
		s_IntInputs.clear();
		s_StrInputs.clear();
		s_ColInputs.clear();
	};

	size_t ChangesCount = 0;

	CUIRect ApplyBar, TopBar, ListArea;
	MainView.VSplitRight(5.0f, &MainView, nullptr); // padding for scrollbar
	MainView.VSplitLeft(5.0f, nullptr, &MainView);
	MainView.HSplitTop(LineSize + MarginSmall, &ApplyBar, &MainView);
	MainView.HSplitTop(LineSize + MarginSmall, &TopBar, &ListArea);
	ListArea.HSplitTop(MarginSmall, nullptr, &ListArea);

	static CLineInputBuffered<128> s_SearchInput;

	ChangesCount = s_StagedInts.size() + s_StagedStrs.size() + s_StagedCols.size();
	{
		CUIRect LeftHalf, RightHalf;
		ApplyBar.VSplitMid(&LeftHalf, &RightHalf, 0.0f);
		CUIRect Row = LeftHalf;
		Row.HMargin(MarginSmall, &Row);
		Row.h = LineSize;
		Row.y = ApplyBar.y + (ApplyBar.h - LineSize) / 2.0f;

		const float BtnWidth = 120.0f;
		CUIRect ApplyBtn, ClearBtn, Counter;
		Row.VSplitLeft(BtnWidth, &ApplyBtn, &Row);
		Row.VSplitLeft(MarginSmall, nullptr, &Row);
		Row.VSplitLeft(BtnWidth, &ClearBtn, &Row);
		Row.VSplitLeft(MarginSmall, nullptr, &Counter);

		static CButtonContainer s_ApplyBtn, s_ClearBtn;
		int DisabledStyle = ChangesCount > 0 ? 0 : -1;
		const bool ApplyClicked = DoButton_Menu(&s_ApplyBtn, Localize("Apply Changes"), DisabledStyle, &ApplyBtn);
		if(ChangesCount > 0 && ApplyClicked)
		{
			// NOLINTBEGIN(bugprone-nondeterministic-pointer-iteration-order)
			for(const auto &It : s_StagedInts)
			{
				const SConfigVariable *pVar = It.first;
				char aCmd[256];
				str_format(aCmd, sizeof(aCmd), "%s %d", pVar->m_pScriptName, It.second.m_Value);
				Console()->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
			}
			for(const auto &It : s_StagedStrs)
			{
				const SConfigVariable *pVar = It.first;
				char aEsc[1024];
				aEsc[0] = '\0';
				char *pDst = aEsc;
				str_escape(&pDst, It.second.m_Value.c_str(), aEsc + sizeof(aEsc));
				char aCmd[1200];
				str_format(aCmd, sizeof(aCmd), "%s \"%s\"", pVar->m_pScriptName, aEsc);
				Console()->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
			}
			for(const auto &It : s_StagedCols)
			{
				const SConfigVariable *pVar = It.first;
				char aCmd[256];
				str_format(aCmd, sizeof(aCmd), "%s %u", pVar->m_pScriptName, It.second.m_Value);
				Console()->ExecuteLine(aCmd, IConsole::CLIENT_ID_UNSPECIFIED);
			}
			// NOLINTEND(bugprone-nondeterministic-pointer-iteration-order)
			ClearStagedAndCaches();
			ConfigManager()->Save();
		}
		const bool ClearClicked = DoButton_Menu(&s_ClearBtn, Localize("Clear Changes"), DisabledStyle, &ClearBtn);
		if(ChangesCount > 0 && ClearClicked)
		{
			ClearStagedAndCaches();
		}

		char aBuf[64];
		str_format(aBuf, sizeof(aBuf), Localize("Changes: %d"), (int)ChangesCount);
		Ui()->DoLabel(&Counter, aBuf, FontSize, TEXTALIGN_ML);

		CUIRect RightRow = RightHalf;
		RightRow.h = LineSize;
		RightRow.y = ApplyBar.y + (ApplyBar.h - LineSize) / 2.0f;
		const float RightInset = 24.0f;
		RightRow.VSplitLeft(RightInset, nullptr, &RightRow);
		CUIRect TopCol1, TopCol2;
		RightRow.VSplitMid(&TopCol1, &TopCol2, 0.0f);
		if(DoButton_CheckBox(&g_Config.m_TcUiShowTClient, Localize("TClient"), g_Config.m_TcUiShowTClient, &TopCol1))
			g_Config.m_TcUiShowTClient ^= 1;
		if(DoButton_CheckBox(&g_Config.m_TcUiCompactList, Localize("Compact List"), g_Config.m_TcUiCompactList, &TopCol2))
			g_Config.m_TcUiCompactList ^= 1;
	}

	const float SearchLabelW = 60.0f;
	{
		CUIRect SearchRow = TopBar;
		SearchRow.h = LineSize;
		SearchRow.y = TopBar.y + (TopBar.h - LineSize) / 2.0f;

		CUIRect LeftHalf, RightHalf;
		SearchRow.VSplitMid(&LeftHalf, &RightHalf, 0.0f);

		CUIRect SearchLabel, SearchEdit;
		LeftHalf.VSplitLeft(SearchLabelW, &SearchLabel, &SearchEdit);
		Ui()->DoLabel(&SearchLabel, Localize("Search"), FontSize, TEXTALIGN_ML);
		Ui()->DoClearableEditBox(&s_SearchInput, &SearchEdit, EditBoxFontSize);

		CUIRect RightCol1, RightCol2;
		const float RightInset2 = 24.0f;
		RightHalf.VSplitLeft(RightInset2, nullptr, &RightHalf);
		RightHalf.VSplitMid(&RightCol1, &RightCol2, 0.0f);
		if(DoButton_CheckBox(&g_Config.m_TcUiShowDDNet, Localize("DDNet"), g_Config.m_TcUiShowDDNet, &RightCol1))
			g_Config.m_TcUiShowDDNet ^= 1;
		if(DoButton_CheckBox(&g_Config.m_TcUiOnlyModified, Localize("Only modified"), g_Config.m_TcUiOnlyModified, &RightCol2))
			g_Config.m_TcUiOnlyModified ^= 1;
	}

	const int FlagMask = CFGFLAG_CLIENT;

	struct SEntry
	{
		const SConfigVariable *m_pVar;
	};
	std::vector<SEntry> vEntries;
	vEntries.reserve(256);

	auto Collector = [](const SConfigVariable *pVar, void *pUserData) {
		auto *pVec = static_cast<std::vector<SEntry> *>(pUserData);
		pVec->push_back({pVar});
	};
	ConfigManager()->PossibleConfigVariables("", FlagMask, Collector, &vEntries);

	auto DomainEnabled = [&](ConfigDomain Domain) {
		if(Domain == ConfigDomain::DDNET)
			return g_Config.m_TcUiShowDDNet != 0;
		if(Domain == ConfigDomain::TCLIENT)
			return g_Config.m_TcUiShowTClient != 0;
		if(Domain == ConfigDomain::ACLIENT)
			return true;
		// only show DDNet and TClient domains
		return false;
	};

	const char *pSearch = s_SearchInput.GetString();

	auto IsEffectiveDefaultVar = [&](const SConfigVariable *p) -> bool {
		if(p->m_Type == SConfigVariable::VAR_INT)
		{
			const SIntConfigVariable *pInt = static_cast<const SIntConfigVariable *>(p);
			auto It = s_StagedInts.find(p);
			int Value = It != s_StagedInts.end() ? It->second.m_Value : *pInt->m_pVariable;
			return Value == pInt->m_Default;
		}
		if(p->m_Type == SConfigVariable::VAR_STRING)
		{
			const SStringConfigVariable *pStr = static_cast<const SStringConfigVariable *>(p);
			auto It = s_StagedStrs.find(p);
			const char *pValue = It != s_StagedStrs.end() ? It->second.m_Value.c_str() : pStr->m_pStr;
			return str_comp(pValue, pStr->m_pDefault) == 0;
		}
		if(p->m_Type == SConfigVariable::VAR_COLOR)
		{
			const SColorConfigVariable *pColor = static_cast<const SColorConfigVariable *>(p);
			auto It = s_StagedCols.find(p);
			unsigned Value = It != s_StagedCols.end() ? It->second.m_Value : *pColor->m_pVariable;
			return Value == pColor->m_Default;
		}
		return true;
	};

	std::vector<const SConfigVariable *> vpFiltered;
	vpFiltered.reserve(vEntries.size());
	for(const auto &E : vEntries)
	{
		const SConfigVariable *pVar = E.m_pVar;
		if(!DomainEnabled(pVar->m_ConfigDomain))
			continue;
		if(g_Config.m_TcUiOnlyModified && IsEffectiveDefaultVar(pVar))
			continue;
		if(pSearch && pSearch[0])
		{
			const char *pName = pVar->m_pScriptName ? pVar->m_pScriptName : "";
			const char *pHelp = pVar->m_pHelp ? pVar->m_pHelp : "";
			if(!str_find_nocase(pName, pSearch) && !str_find_nocase(pHelp, pSearch))
				continue;
		}
		vpFiltered.push_back(pVar);
	}

	std::sort(vpFiltered.begin(), vpFiltered.end(), [](const SConfigVariable *a, const SConfigVariable *b) {
		if(a->m_ConfigDomain != b->m_ConfigDomain)
			return a->m_ConfigDomain < b->m_ConfigDomain;
		return str_comp(a->m_pScriptName, b->m_pScriptName) < 0;
	});

	static CScrollRegion s_ScrollRegion;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_ForceShowScrollbar = true;
	s_ScrollRegion.Begin(&ListArea, &ScrollParams);

	ListArea.VSplitRight(5.0f, &ListArea, nullptr);
	CUIRect Content = ListArea;

	auto DomainName = [](ConfigDomain Domain) {
		switch(Domain)
		{
		case ConfigDomain::DDNET: return "DDNet";
		case ConfigDomain::TCLIENT: return "TClient";
		case ConfigDomain::ACLIENT: return "AClient";
		default: return "Other";
		}
	};

	ConfigDomain CurrentDomain = ConfigDomain::NUM;
	for(const SConfigVariable *pVar : vpFiltered)
	{
		if(pVar->m_ConfigDomain != CurrentDomain)
		{
			CurrentDomain = pVar->m_ConfigDomain;
			CUIRect Header;
			Content.HSplitTop(HeadlineHeight, &Header, &Content);
			if(s_ScrollRegion.AddRect(Header))
				Ui()->DoLabel(&Header, DomainName(CurrentDomain), HeadlineFontSize, TEXTALIGN_ML);
			Content.HSplitTop(MarginSmall, nullptr, &Content);
		}

		CUIRect RowItem;
		const float RowHeight = g_Config.m_TcUiCompactList ? (std::max(LineSize, ColorPickerLineSize) + 5.0f) : 55.0f;
		Content.HSplitTop(RowHeight, &RowItem, &Content);
		Content.HSplitTop(MarginExtraSmall, nullptr, &Content);
		const bool Visible = s_ScrollRegion.AddRect(RowItem);
		if(!Visible)
			continue;

		const bool Modified = !IsEffectiveDefaultVar(pVar);
		const ColorRGBA BgModified = ColorRGBA(1.0f, 0.8f, 0.0f, 0.15f);
		const ColorRGBA BgNormal = ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f);
		RowItem.Draw(Modified ? BgModified : BgNormal, IGraphics::CORNER_ALL, 6.0f);

		CUIRect RowContent;
		RowItem.Margin(5.0f, &RowContent);

		CUIRect TopLine, Below;
		if(g_Config.m_TcUiCompactList)
		{
			const float UsedHeight = (pVar->m_Type == SConfigVariable::VAR_COLOR) ? ColorPickerLineSize : LineSize;
			TopLine = RowContent;
			TopLine.h = UsedHeight;
			TopLine.y = round_to_int(RowContent.y + (RowContent.h - UsedHeight) / 2.0f);
			Below = RowContent;
		}
		else
		{
			RowContent.HSplitTop(LineSize, &TopLine, &Below);
		}
		CUIRect NameLine, Right;
		TopLine.VSplitRight(320.0f, &NameLine, &Right);
		NameLine.VSplitLeft(10.0f, nullptr, &NameLine);

		Ui()->DoLabel(&NameLine, pVar->m_pScriptName, FontSize, TEXTALIGN_ML);

		CUIRect Controls, ResetRect;
		Right.VSplitRight(120.0f, &Controls, &ResetRect);
		Controls.h = LineSize;
		Controls.y = TopLine.y + (TopLine.h - LineSize) / 2.0f;
		ResetRect.h = LineSize;
		ResetRect.y = Controls.y;
		Controls.VSplitRight(MarginSmall, &Controls, nullptr);

		if(!g_Config.m_TcUiCompactList)
		{
			CUIRect Help;
			Below.HSplitTop(2.0f, nullptr, &Below);
			Help = Below;
			Help.VSplitLeft(10.0f, nullptr, &Help);
			Ui()->DoLabel(&Help, pVar->m_pHelp ? pVar->m_pHelp : "", 11.0f, TEXTALIGN_ML);
		}

		static std::unordered_map<const SConfigVariable *, CButtonContainer> s_ResetBtns;
		if(Modified && pVar->m_Type != SConfigVariable::VAR_COLOR)
		{
			CButtonContainer &ResetBtn = s_ResetBtns[pVar];
			if(DoButton_Menu(&ResetBtn, Localize("Reset"), 0, &ResetRect))
			{
				if(pVar->m_Type == SConfigVariable::VAR_INT)
				{
					const SIntConfigVariable *pInt = static_cast<const SIntConfigVariable *>(pVar);
					s_StagedInts[pVar] = {pInt->m_Default};
				}
				else if(pVar->m_Type == SConfigVariable::VAR_STRING)
				{
					const SStringConfigVariable *pStr = static_cast<const SStringConfigVariable *>(pVar);
					s_StagedStrs[pVar] = {std::string(pStr->m_pDefault)};
				}
			}
		}

		if(pVar->m_Type == SConfigVariable::VAR_INT)
		{
			const SIntConfigVariable *pInt = static_cast<const SIntConfigVariable *>(pVar);
			// treat 0 1 ints as checkboxes
			if(pInt->m_Min == 0 && pInt->m_Max == 1)
			{
				const int Effective = s_StagedInts.contains(pVar) ? s_StagedInts[pVar].m_Value : *pInt->m_pVariable;
				if(DoButton_CheckBox(pVar, "", Effective, &Controls))
				{
					const int NewVal = Effective ? 0 : 1;
					if(NewVal == *pInt->m_pVariable)
						s_StagedInts.erase(pVar);
					else
						s_StagedInts[pVar] = {NewVal};
				}
			}
			else
			{
				SIntState &State = s_IntInputs[pVar];
				const int Effective = s_StagedInts.contains(pVar) ? s_StagedInts[pVar].m_Value : *pInt->m_pVariable;
				if(!State.m_Inited)
				{
					State.m_Input.SetInteger(Effective);
					State.m_LastValue = Effective;
					State.m_Inited = true;
				}
				else if(!State.m_Input.IsActive() && State.m_LastValue != Effective)
				{
					State.m_Input.SetInteger(Effective);
					State.m_LastValue = Effective;
				}

				CUIRect InputBox, Dummy;
				Controls.VSplitLeft(60.0f, &InputBox, &Dummy);

				if(Ui()->DoEditBox(&State.m_Input, &InputBox, EditBoxFontSize))
				{
					int NewVal = State.m_Input.GetInteger();
					bool InRange = true;
					if(pInt->m_Min != pInt->m_Max)
					{
						if(NewVal < pInt->m_Min)
							InRange = false;
						if(pInt->m_Max != 0 && NewVal > pInt->m_Max)
							InRange = false;
					}
					if(InRange && NewVal != State.m_LastValue)
					{
						if(NewVal == *pInt->m_pVariable)
							s_StagedInts.erase(pVar);
						else
							s_StagedInts[pVar] = {NewVal};
						State.m_LastValue = NewVal;
					}
				}
			}
		}
		else if(pVar->m_Type == SConfigVariable::VAR_STRING)
		{
			const SStringConfigVariable *pStr = static_cast<const SStringConfigVariable *>(pVar);
			SStrState &State = s_StrInputs[pVar];
			const char *Effective = s_StagedStrs.contains(pVar) ? s_StagedStrs[pVar].m_Value.c_str() : pStr->m_pStr;
			if(!State.m_Inited)
			{
				State.m_Input.Set(Effective);
				State.m_Inited = true;
			}
			else if(!State.m_Input.IsActive())
			{
				if(str_comp(State.m_Input.GetString(), Effective) != 0)
					State.m_Input.Set(Effective);
			}

			if(Ui()->DoEditBox(&State.m_Input, &Controls, EditBoxFontSize))
			{
				const char *NewVal = State.m_Input.GetString();
				if(str_comp(NewVal, pStr->m_pStr) == 0)
					s_StagedStrs.erase(pVar);
				else
					s_StagedStrs[pVar] = {std::string(NewVal)};
			}
		}
		else if(pVar->m_Type == SConfigVariable::VAR_COLOR)
		{
			const SColorConfigVariable *pCol = static_cast<const SColorConfigVariable *>(pVar);
			CUIRect ColorRect;
			ColorRect.x = Controls.x;
			ColorRect.h = ColorPickerLineSize;
			ColorRect.y = TopLine.y + (TopLine.h - ColorPickerLineSize) / 2.0f;
			ColorRect.w = ColorPickerLineSize + 8.0f + 60.0f;
			const ColorRGBA DefaultColor = color_cast<ColorRGBA>(ColorHSLA(pCol->m_Default, true).UnclampLighting(pCol->m_DarkestLighting));
			static std::unordered_map<const SConfigVariable *, CButtonContainer> s_ColorResetIds;
			CButtonContainer &ResetId = s_ColorResetIds[pVar];

			SColState &ColState = s_ColInputs[pVar];
			unsigned Effective = s_StagedCols.contains(pVar) ? s_StagedCols[pVar].m_Value : *pCol->m_pVariable;
			if(!ColState.m_Inited)
			{
				ColState.m_Working = Effective;
				ColState.m_LastValue = Effective;
				ColState.m_Inited = true;
			}
			else
			{
				const bool EditingThis = Ui()->IsPopupOpen(&m_ColorPickerPopupContext) && m_ColorPickerPopupContext.m_pHslaColor == &ColState.m_Working;
				if(!EditingThis && ColState.m_Working != Effective)
				{
					ColState.m_Working = Effective;
					ColState.m_LastValue = Effective;
				}
			}

			DoLine_ColorPicker(&ResetId, ColorPickerLineSize, ColorPickerLabelSize, 0.0f, &ColorRect, "", &ColState.m_Working, DefaultColor, false, nullptr, pCol->m_Alpha);
			if(ColState.m_Working != Effective)
			{
				if(ColState.m_Working == *pCol->m_pVariable)
					s_StagedCols.erase(pVar);
				else
					s_StagedCols[pVar] = {ColState.m_Working};
				ColState.m_LastValue = ColState.m_Working;
			}
		}
	}

	CUIRect EndPad{Content.x, Content.y, Content.w, 5.0f};
	s_ScrollRegion.AddRect(EndPad);
	s_ScrollRegion.End();
}


void CMenus::RenderSettingsTClientRaceStats(CUIRect MainView)
{
	CUIRect LeftView, RightView, Button, Label;
	MainView.HSplitTop(MarginSmall, nullptr, &MainView);
	MainView.VSplitLeft(MainView.w * 0.44f, &LeftView, &RightView);
	LeftView.VSplitRight(MarginSmall, &LeftView, nullptr);
	RightView.VSplitLeft(MarginSmall, nullptr, &RightView);

	auto &RaceStats = GameClient()->m_RaceStats;
	static int s_SelectedEntry = 0;
	if(s_SelectedEntry >= (int)RaceStats.m_vSavedEntries.size())
		s_SelectedEntry = std::max(0, (int)RaceStats.m_vSavedEntries.size() - 1);

	// ===== PANEL IZQUIERDO: CONTROLES E HISTORIAL =====
	LeftView.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
	LeftView.Margin(Margin, &LeftView);

	LeftView.HSplitTop(HeadlineHeight, &Label, &LeftView);
	Ui()->DoLabel(&Label, TCLocalize("Saved Race Stats Log"), HeadlineFontSize, TEXTALIGN_ML);
	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	static CButtonContainer s_ReaderBtnTabSave, s_ClearBtnTabSave;
	DoLine_KeyReader(LeftView, s_ReaderBtnTabSave, s_ClearBtnTabSave, TCLocalize("Save stats key"), "ac_save_racestats");

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcStatboardAutoSave, TCLocalize("Auto-save before reset (/kill or /team)"), &g_Config.m_AcStatboardAutoSave, &LeftView, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcRaceStatsSaveOnFinish, TCLocalize("Auto-save on map finish"), &g_Config.m_AcRaceStatsSaveOnFinish, &LeftView, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcRaceStatsSaveOnlyOnFinish, TCLocalize("Save ONLY on map finish"), &g_Config.m_AcRaceStatsSaveOnlyOnFinish, &LeftView, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcStatboardBackgroundCalc, TCLocalize("Calculate stats in background"), &g_Config.m_AcStatboardBackgroundCalc, &LeftView, LineSize);

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	CUIRect DurationSlider, MaxLogsSlider;
	LeftView.HSplitTop(LineSize, &DurationSlider, &LeftView);
	Ui()->DoScrollbarOption(&g_Config.m_AcRaceStatsMinDuration, &g_Config.m_AcRaceStatsMinDuration, &DurationSlider, TCLocalize("Min run duration"), 0, 120, &CUi::ms_LinearScrollbarScale, 0u, "s");

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);
	LeftView.HSplitTop(LineSize, &MaxLogsSlider, &LeftView);
	Ui()->DoScrollbarOption(&g_Config.m_AcRaceStatsMaxLogs, &g_Config.m_AcRaceStatsMaxLogs, &MaxLogsSlider, TCLocalize("Max saved logs (0=unlimited)"), 0, 200, &CUi::ms_LinearScrollbarScale, 0u);

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	// Botones de acción: Guardar Actual / Borrar / Limpiar Todo
	CUIRect BtnRow, BtnSaveNow, BtnDelete, BtnClearAll;
	LeftView.HSplitTop(LineSize * 1.15f, &BtnRow, &LeftView);
	BtnRow.VSplitLeft(BtnRow.w * 0.42f, &BtnSaveNow, &BtnRow);
	BtnRow.VSplitLeft(MarginSmall, nullptr, &BtnRow);
	BtnRow.VSplitMid(&BtnDelete, &BtnClearAll, MarginSmall);

	static CButtonContainer s_BtnSaveNowId, s_BtnDeleteId, s_BtnClearAllId;
	if(DoButton_Menu(&s_BtnSaveNowId, TCLocalize("Save Now"), 0, &BtnSaveNow))
	{
		if(RaceStats.SaveCurrentStats("Manual", false, true))
			s_SelectedEntry = 0;
	}
	if(DoButton_Menu(&s_BtnDeleteId, TCLocalize("Delete"), 0, &BtnDelete))
	{
		if(!RaceStats.m_vSavedEntries.empty())
		{
			RaceStats.DeleteSavedEntry(s_SelectedEntry);
			if(s_SelectedEntry >= (int)RaceStats.m_vSavedEntries.size())
				s_SelectedEntry = std::max(0, (int)RaceStats.m_vSavedEntries.size() - 1);
		}
	}
	if(DoButton_Menu(&s_BtnClearAllId, TCLocalize("Clear All"), 0, &BtnClearAll))
	{
		RaceStats.ClearSavedHistory();
		s_SelectedEntry = 0;
	}

	LeftView.HSplitTop(MarginSmall, nullptr, &LeftView);

	// Lista desplazable de partidas guardadas
	static CScrollRegion s_HistoryScroll;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 40.0f;
	ScrollParams.m_ScrollbarMargin = 3.0f;
	s_HistoryScroll.Begin(&LeftView, &ScrollParams);

	static std::vector<CButtonContainer> s_vEntryButtons;
	if(s_vEntryButtons.size() < RaceStats.m_vSavedEntries.size())
		s_vEntryButtons.resize(RaceStats.m_vSavedEntries.size());

	for(int i = 0; i < (int)RaceStats.m_vSavedEntries.size(); i++)
	{
		const auto &Entry = RaceStats.m_vSavedEntries[i];
		CUIRect ItemRect;
		LeftView.HSplitTop(38.0f, &ItemRect, &LeftView);
		LeftView.HSplitTop(4.0f, nullptr, &LeftView);

		if(!s_HistoryScroll.AddRect(ItemRect))
			continue;

		bool IsSelected = (i == s_SelectedEntry);
		ColorRGBA ItemBg = IsSelected ? ColorRGBA(0.25f, 0.55f, 0.90f, 0.45f) : ColorRGBA(1.0f, 1.0f, 1.0f, 0.08f);
		if(DoButtonLineSize_Menu(&s_vEntryButtons[i], "", IsSelected ? 1 : 0, &ItemRect, LineSize, false, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ItemBg))
		{
			s_SelectedEntry = i;
		}

		CUIRect TopLine, BottomLine, MapRect, TimeRect, FlagRect;
		ItemRect.Margin(4.0f, &ItemRect);
		ItemRect.HSplitMid(&TopLine, &BottomLine);
		TopLine.VSplitRight(95.0f, &MapRect, &TimeRect);

		if(Entry.m_Completed)
		{
			MapRect.VSplitLeft(16.0f, &FlagRect, &MapRect);
			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->TextColor(1.0f, 0.85f, 0.2f, 1.0f);
			Ui()->DoLabel(&FlagRect, FontIcon::FLAG_CHECKERED, 11.5f, TEXTALIGN_ML);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
		}

		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		Ui()->DoLabel(&MapRect, Entry.m_aMapName, 12.5f, TEXTALIGN_ML);

		char aRunBuf[48];
		str_format(aRunBuf, sizeof(aRunBuf), "Run: %s", Entry.m_aRunTime);
		TextRender()->TextColor(0.4f, 0.95f, 0.55f, 1.0f);
		Ui()->DoLabel(&TimeRect, aRunBuf, 12.0f, TEXTALIGN_MR);

		char aSubBuf[128];
		str_format(aSubBuf, sizeof(aSubBuf), "%s  |  Team %d  |  %s (%d)", Entry.m_aTimestamp, Entry.m_Team, Entry.m_aReason, (int)Entry.m_vPlayers.size());
		TextRender()->TextColor(0.75f, 0.78f, 0.85f, 0.9f);
		Ui()->DoLabel(&BottomLine, aSubBuf, 10.5f, TEXTALIGN_ML);
	}

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	s_HistoryScroll.End();

	// ===== PANEL DERECHO: VISUALIZADOR DETALLADO =====
	CUIRect ExportCardRect = RightView;
	RightView.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 8.0f);
	RightView.Margin(Margin, &RightView);

	if(RaceStats.m_vSavedEntries.empty())
	{
		CUIRect EmptyMsg;
		RightView.HSplitTop(120.0f, nullptr, &EmptyMsg);
		TextRender()->TextColor(0.8f, 0.8f, 0.8f, 0.85f);
		Ui()->DoLabel(&EmptyMsg, TCLocalize("No saved Race Stats yet.\nStats are saved automatically on /kill or /team change,\nor manually using 'Save Now' or your bound key."), 14.0f, TEXTALIGN_TC);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}

	const auto &Sel = RaceStats.m_vSavedEntries[s_SelectedEntry];

	// Tarjeta superior de metadatos de la Run
	CUIRect MetaCard, MetaTop, MetaBottom;
	RightView.HSplitTop(54.0f, &MetaCard, &RightView);
	MetaCard.Draw(ColorRGBA(0.10f, 0.12f, 0.18f, 0.65f), IGraphics::CORNER_ALL, 6.0f);
	MetaCard.Margin(8.0f, &MetaCard);
	MetaCard.HSplitMid(&MetaTop, &MetaBottom);

	CUIRect MetaMap, MetaRunTime, BtnExportPng;
	MetaTop.VSplitRight(95.0f, &MetaTop, &BtnExportPng);
	MetaTop.VSplitRight(8.0f, &MetaTop, nullptr);
	MetaTop.VSplitRight(140.0f, &MetaMap, &MetaRunTime);

	static CButtonContainer s_BtnExportPng;
	static bool s_ExportPngPending = false;
	if(DoButton_Menu(&s_BtnExportPng, "Export PNG", 0, &BtnExportPng))
	{
		s_ExportPngPending = true;
	}

	char aTitleBuf[128];
	str_format(aTitleBuf, sizeof(aTitleBuf), "Map: %s", Sel.m_aMapName);
	TextRender()->TextColor(1.0f, 0.9f, 0.35f, 1.0f);
	Ui()->DoLabel(&MetaMap, aTitleBuf, 15.0f, TEXTALIGN_ML);

	char aRunTimeLabel[64];
	str_format(aRunTimeLabel, sizeof(aRunTimeLabel), "%s: %s", TCLocalize("Run Time"), Sel.m_aRunTime);
	TextRender()->TextColor(0.4f, 1.0f, 0.55f, 1.0f);
	Ui()->DoLabel(&MetaRunTime, aRunTimeLabel, 14.0f, TEXTALIGN_MR);

	char aDetailsBuf[160];
	str_format(aDetailsBuf, sizeof(aDetailsBuf), "%s: %s   |   %s: %d   |   %s: %s   |   %s: %d",
		TCLocalize("Date"), Sel.m_aTimestamp, TCLocalize("Team"), Sel.m_Team, TCLocalize("Saved"), Sel.m_aReason, TCLocalize("Players"), (int)Sel.m_vPlayers.size());
	TextRender()->TextColor(0.8f, 0.85f, 0.92f, 0.95f);
	Ui()->DoLabel(&MetaBottom, aDetailsBuf, 11.5f, TEXTALIGN_ML);

	RightView.HSplitTop(Margin, nullptr, &RightView);

	// Tabla estilo Overlay con los iconos embebidos y skins de cada jugador
	CUIRect TableHeader;
	const float RowH = 26.0f;
	const float IconSz = 16.0f;

	bool ShowFreeze = (g_Config.m_AcMiniStatboardColumns & 1) != 0;
	bool ShowSave   = (g_Config.m_AcMiniStatboardColumns & 2) != 0;
	bool ShowLast   = (g_Config.m_AcMiniStatboardColumns & 4) != 0;
	bool ShowEff    = (g_Config.m_AcMiniStatboardColumns & 8) != 0;
	bool ShowDeath  = (g_Config.m_AcMiniStatboardColumns & 16) != 0;
	bool ShowNet    = (g_Config.m_AcMiniStatboardColumns & 32) != 0;
	bool ShowTime   = (g_Config.m_AcMiniStatboardColumns & 64) != 0;
	bool ShowHook   = (g_Config.m_AcMiniStatboardColumns & 128) != 0;

	int NumActiveCols = (ShowFreeze ? 1 : 0) + (ShowSave ? 1 : 0) + (ShowDeath ? 1 : 0) +
	                    (ShowEff ? 1 : 0) + (ShowNet ? 1 : 0) + (ShowTime ? 1 : 0) +
	                    (ShowHook ? 1 : 0) + (ShowLast ? 1 : 0);
	if(NumActiveCols == 0)
	{
		ShowFreeze = ShowSave = ShowDeath = ShowEff = ShowNet = ShowTime = ShowHook = ShowLast = true;
		NumActiveCols = 8;
	}

	const float ColStatW = (NumActiveCols > 5) ? 36.0f : 44.0f;
	const float ColNameW = std::max(70.0f, RightView.w - (ColStatW * (float)NumActiveCols) - 16.0f);

	RightView.HSplitTop(RowH + 4.0f, &TableHeader, &RightView);
	TableHeader.Draw(ColorRGBA(0.14f, 0.15f, 0.22f, 0.75f), IGraphics::CORNER_T, 6.0f);

	auto DrawTexIcon = [&](IGraphics::CTextureHandle Tex, float x, float y, float size, ColorRGBA col) {
		if(!Tex.IsValid())
			return;
		Graphics()->TextureSet(Tex);
		Graphics()->QuadsBegin();
		Graphics()->SetColor(col);
		IGraphics::CQuadItem Quad(x, y, size, size);
		Graphics()->QuadsDrawTL(&Quad, 1);
		Graphics()->QuadsEnd();
	};

	float HeadX = TableHeader.x + 8.0f;
	float HeadY = TableHeader.y + (TableHeader.h - IconSz) * 0.5f;

	DrawTexIcon(RaceStats.m_aTextures[CRaceStats::ICON_USER], HeadX + 4.0f, HeadY, IconSz, ColorRGBA(1.0f, 1.0f, 1.0f, 0.95f));
	HeadX += ColNameW;

	if(ShowFreeze) { DrawTexIcon(RaceStats.m_aTextures[CRaceStats::ICON_FREEZE], HeadX + (ColStatW - IconSz) * 0.5f, HeadY, IconSz, ColorRGBA(0.5f, 0.85f, 1.0f, 1.0f)); HeadX += ColStatW; }
	if(ShowSave)   { DrawTexIcon(RaceStats.m_aTextures[CRaceStats::ICON_SAVE], HeadX + (ColStatW - IconSz) * 0.5f, HeadY, IconSz, ColorRGBA(0.4f, 1.0f, 0.5f, 1.0f)); HeadX += ColStatW; }
	if(ShowDeath)  { DrawTexIcon(RaceStats.m_aTextures[CRaceStats::ICON_SKULL], HeadX + (ColStatW - IconSz) * 0.5f, HeadY, IconSz, ColorRGBA(0.95f, 0.35f, 0.35f, 1.0f)); HeadX += ColStatW; }
	if(ShowEff)    { DrawTexIcon(RaceStats.m_aTextures[CRaceStats::ICON_EFF], HeadX + (ColStatW - IconSz) * 0.5f, HeadY, IconSz, ColorRGBA(0.95f, 0.75f, 0.3f, 1.0f)); HeadX += ColStatW; }
	if(ShowNet)    { DrawTexIcon(RaceStats.m_aTextures[CRaceStats::ICON_NET], HeadX + (ColStatW - IconSz) * 0.5f, HeadY, IconSz, ColorRGBA(0.45f, 0.90f, 0.95f, 1.0f)); HeadX += ColStatW; }
	if(ShowTime)   { DrawTexIcon(RaceStats.m_aTextures[CRaceStats::ICON_TIME], HeadX + (ColStatW - IconSz) * 0.5f, HeadY, IconSz, ColorRGBA(0.55f, 0.80f, 1.0f, 1.0f)); HeadX += ColStatW; }
	if(ShowHook)   { DrawTexIcon(RaceStats.m_aTextures[CRaceStats::ICON_HOOK], HeadX + (ColStatW - IconSz) * 0.5f, HeadY, IconSz, ColorRGBA(1.0f, 0.85f, 0.35f, 1.0f)); HeadX += ColStatW; }
	if(ShowLast)   { DrawTexIcon(RaceStats.m_aTextures[CRaceStats::ICON_LAST], HeadX + (ColStatW - IconSz) * 0.5f, HeadY, IconSz, ColorRGBA(1.0f, 0.75f, 0.3f, 1.0f)); HeadX += ColStatW; }

	static CScrollRegion s_PlayersScroll;
	CScrollRegionParams PlScrollParams;
	PlScrollParams.m_ScrollUnit = 30.0f;
	PlScrollParams.m_ScrollbarMargin = 2.0f;
	s_PlayersScroll.Begin(&RightView, &PlScrollParams);

	for(int idx = 0; idx < (int)Sel.m_vPlayers.size(); idx++)
	{
		const auto &Pl = Sel.m_vPlayers[idx];
		CUIRect RowRect;
		RightView.HSplitTop(RowH, &RowRect, &RightView);

		if(!s_PlayersScroll.AddRect(RowRect))
			continue;

		if(idx % 2 == 1)
			RowRect.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.04f), IGraphics::CORNER_NONE, 0.0f);
		if(Pl.m_IsLocal)
			RowRect.Draw(ColorRGBA(1.0f, 0.85f, 0.2f, 0.12f), IGraphics::CORNER_ALL, 3.0f);

		float CurrX = RowRect.x + 8.0f;
		const float RowFontSize = 13.0f;
		float TextY = RowRect.y + (RowRect.h - RowFontSize) * 0.5f;

		// Renderizar Tee con la skin guardada
		CTeeRenderInfo TeeInfo;
		TeeInfo.Apply(GameClient()->m_Skins.Find(Pl.m_aSkinName));
		TeeInfo.ApplyColors(Pl.m_CustomColors, Pl.m_ColorBody, Pl.m_ColorFeet);
		TeeInfo.m_Size = 21.0f;
		RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), vec2(CurrX + 10.0f, RowRect.y + RowRect.h * 0.5f + 1.5f));

		if(Pl.m_IsLocal)
			TextRender()->TextColor(1.0f, 0.9f, 0.3f, 1.0f);
		else
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f);

		TextRender()->Text(CurrX + 24.0f, TextY, RowFontSize, Pl.m_aName, -1.0f);
		CurrX += ColNameW;

		auto DrawSavedStat = [&](const char *pText, ColorRGBA col) {
			float W = TextRender()->TextWidth(RowFontSize, pText, -1, -1.0f);
			TextRender()->TextColor(col);
			TextRender()->Text(CurrX + (ColStatW - W) * 0.5f, TextY, RowFontSize, pText, -1.0f);
			CurrX += ColStatW;
		};

		char aBuf[32];
		if(ShowFreeze)
		{
			str_format(aBuf, sizeof(aBuf), "%d", Pl.m_Freezes);
			DrawSavedStat(aBuf, ColorRGBA(0.7f, 0.9f, 1.0f, 0.95f));
		}
		if(ShowSave)
		{
			str_format(aBuf, sizeof(aBuf), "%d", Pl.m_Saves);
			DrawSavedStat(aBuf, ColorRGBA(0.45f, 1.0f, 0.55f, 0.95f));
		}
		if(ShowDeath)
		{
			str_format(aBuf, sizeof(aBuf), "%d", Pl.m_Deaths);
			DrawSavedStat(aBuf, ColorRGBA(1.0f, 0.45f, 0.45f, 0.95f));
		}
		if(ShowEff)
		{
			if(Pl.m_EfficiencyPercent < 0)
				str_copy(aBuf, "-", sizeof(aBuf));
			else
				str_format(aBuf, sizeof(aBuf), "%d%%", Pl.m_EfficiencyPercent);
			DrawSavedStat(aBuf, CRaceStats::GetEfficiencyColor(Pl.m_EfficiencyPercent));
		}
		if(ShowNet)
		{
			if(Pl.m_NetBalance > 0)
			{
				str_format(aBuf, sizeof(aBuf), "+%d", Pl.m_NetBalance);
				DrawSavedStat(aBuf, ColorRGBA(0.35f, 1.0f, 0.45f, 0.95f));
			}
			else if(Pl.m_NetBalance == 0)
			{
				DrawSavedStat("0", ColorRGBA(0.80f, 0.80f, 0.85f, 0.90f));
			}
			else
			{
				str_format(aBuf, sizeof(aBuf), "%d", Pl.m_NetBalance);
				DrawSavedStat(aBuf, ColorRGBA(1.0f, 0.35f, 0.35f, 0.95f));
			}
		}
		if(ShowTime)
		{
			if(Pl.m_FrozenSeconds < 60)
				str_format(aBuf, sizeof(aBuf), "%ds", Pl.m_FrozenSeconds);
			else
				str_format(aBuf, sizeof(aBuf), "%dm %02ds", Pl.m_FrozenSeconds / 60, Pl.m_FrozenSeconds % 60);
			DrawSavedStat(aBuf, ColorRGBA(0.5f, 0.85f, 1.0f, 0.95f));
		}
		if(ShowHook)
		{
			if(Pl.m_HookAccuracyPercent < 0)
				str_copy(aBuf, "-", sizeof(aBuf));
			else
				str_format(aBuf, sizeof(aBuf), "%d%%", Pl.m_HookAccuracyPercent);
			DrawSavedStat(aBuf, ColorRGBA(1.0f, 0.85f, 0.35f, 0.95f));
		}
		if(ShowLast)
		{
			str_format(aBuf, sizeof(aBuf), "%d", Pl.m_Lasts);
			DrawSavedStat(aBuf, ColorRGBA(1.0f, 0.85f, 0.4f, 0.95f));
		}
	}

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	s_PlayersScroll.End();

	if(s_ExportPngPending)
	{
		s_ExportPngPending = false;

		CImageInfo FullImage;
		Graphics()->ReadFramebuffer(FullImage);

		if(FullImage.m_pData && FullImage.m_Width > 0 && FullImage.m_Height > 0)
		{
			float ScreenW = Ui()->Screen()->w;
			float ScreenH = Ui()->Screen()->h;
			float FbW = (float)FullImage.m_Width;
			float FbH = (float)FullImage.m_Height;

			float ScaleX = FbW / ScreenW;
			float ScaleY = FbH / ScreenH;

			int PixX = std::clamp((int)(ExportCardRect.x * ScaleX), 0, (int)FullImage.m_Width - 1);
			int PixY = std::clamp((int)(ExportCardRect.y * ScaleY), 0, (int)FullImage.m_Height - 1);
			int PixW = std::clamp((int)(ExportCardRect.w * ScaleX), 1, (int)FullImage.m_Width - PixX);
			int PixH = std::clamp((int)(ExportCardRect.h * ScaleY), 1, (int)FullImage.m_Height - PixY);

			CImageInfo CroppedImage;
			CroppedImage.m_Width = PixW;
			CroppedImage.m_Height = PixH;
			CroppedImage.m_Format = FullImage.m_Format;
			CroppedImage.Allocate();
			CroppedImage.CopyRectFrom(FullImage, PixX, PixY, PixW, PixH, 0, 0);

			Storage()->CreateFolder("screenshots", IStorage::TYPE_SAVE);
			Storage()->CreateFolder("screenshots/racestats", IStorage::TYPE_SAVE);

			char aDate[32];
			str_timestamp_format(aDate, sizeof(aDate), "%Y-%m-%d_%H-%M-%S");
			char aSafeMap[64];
			str_copy(aSafeMap, Sel.m_aMapName, sizeof(aSafeMap));
			for(char *p = aSafeMap; *p; ++p)
			{
				if(*p == '/' || *p == '\\' || *p == ':' || *p == '*' || *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|')
					*p = '_';
			}

			char aFilename[IO_MAX_PATH_LENGTH];
			str_format(aFilename, sizeof(aFilename), "screenshots/racestats/%s_%s.png", aSafeMap, aDate);

			char aWholePath[IO_MAX_PATH_LENGTH];
			IOHANDLE File = Storage()->OpenFile(aFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE, aWholePath, sizeof(aWholePath));
			if(File)
			{
				if(CImageLoader::SavePng(File, aFilename, CroppedImage))
				{
					char aEcho[256];
					str_format(aEcho, sizeof(aEcho), "RaceStats PNG guardado en: %s", aWholePath);
					GameClient()->m_Chat.Echo(aEcho);
				}
				else
				{
					GameClient()->m_Chat.Echo("Error al guardar PNG de RaceStats.");
				}
			}
			CroppedImage.Free();
		}
		FullImage.Free();
	}
}

void CMenus::RenderSettingsAClient(CUIRect MainView)
{
	CUIRect TabBar, Button;
	MainView.HSplitTop(LineSize, &TabBar, &MainView);
	const float TabWidth = TabBar.w / 6.0f;
	static CButtonContainer s_aAClientTabs[6] = {};
	const char *apTabNames[6] = {
		TCLocalize("Settings"),
		TCLocalize("Visual & HUD"),
		TCLocalize("Race Stats"),
		TCLocalize("Perfiles"),
		TCLocalize("Games"),
		TCLocalize("Mod")};
	const int aTabIds[6] = {
		ACLIENT_TAB_SETTINGS,
		ACLIENT_TAB_HUD,
		ACLIENT_TAB_RACESTATS,
		ACLIENT_TAB_PROFILES,
		ACLIENT_TAB_GAMES,
		ACLIENT_TAB_MOD};

	for(int Tab = 0; Tab < 6; ++Tab)
	{
		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = Tab == 0 ? IGraphics::CORNER_L : (Tab == 5 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aAClientTabs[Tab], apTabNames[Tab], m_CurAClientTab == aTabIds[Tab], &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			m_CurAClientTab = aTabIds[Tab];
	}

	MainView.HSplitTop(Margin, nullptr, &MainView);

	// Banner global de invitacion a sala de Voice Chat (visible desde cualquier pestaña de AClient)
	{
		CAClientVoice &Voice = GameClient()->m_AClientVoice;
		if(Voice.HasPendingInvite())
		{
			CUIRect InvBanner;
			MainView.HSplitTop(LineSize + 8.0f, &InvBanner, &MainView);
			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			InvBanner.Draw(ColorRGBA(0.12f, 0.28f, 0.48f, 0.90f), IGraphics::CORNER_ALL, 5.0f);
			InvBanner.Margin(4.0f, &InvBanner);

			CUIRect AcceptBtn, DeclineBtn;
			InvBanner.VSplitRight(72.0f, &InvBanner, &DeclineBtn);
			InvBanner.VSplitRight(5.0f, &InvBanner, nullptr);
			InvBanner.VSplitRight(72.0f, &InvBanner, &AcceptBtn);
			InvBanner.VSplitRight(8.0f, &InvBanner, nullptr);

			char aInvMsg[160];
			str_format(aInvMsg, sizeof(aInvMsg), "Voice Chat: '%s' te invito a la sala '%s'%s",
				Voice.GetPendingInviteFrom(), Voice.GetPendingInviteRoom(),
				Voice.HasPendingInvitePassword() ? " (con clave)" : "");
			TextRender()->TextColor(0.95f, 0.98f, 1.0f, 1.0f);
			Ui()->DoLabel(&InvBanner, aInvMsg, FontSize * 0.95f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			auto DrawInvBtn = [&](CButtonContainer *pId, const char *pText, const CUIRect &Rect, ColorRGBA Bg) -> bool {
				const bool Hovered = Ui()->MouseInside(&Rect);
				Rect.Draw(Hovered ? ColorRGBA(std::min(Bg.r + 0.12f, 1.0f), std::min(Bg.g + 0.12f, 1.0f), std::min(Bg.b + 0.12f, 1.0f), 0.95f) : Bg, IGraphics::CORNER_ALL, 4.0f);
				TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
				Ui()->DoLabel(&Rect, pText, FontSize * 0.85f, TEXTALIGN_MC);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				return Ui()->DoButtonLogic(pId, 0, &Rect, BUTTONFLAG_LEFT) != 0;
			};

			static CButtonContainer s_TopVoiceAcceptBtn, s_TopVoiceDeclineBtn;
			if(DrawInvBtn(&s_TopVoiceAcceptBtn, TCLocalize("Aceptar"), AcceptBtn, ColorRGBA(0.20f, 0.72f, 0.36f, 0.92f)))
				Voice.AcceptPendingInvite();
			if(DrawInvBtn(&s_TopVoiceDeclineBtn, TCLocalize("Rechazar"), DeclineBtn, ColorRGBA(0.80f, 0.24f, 0.24f, 0.90f)))
				Voice.DeclinePendingInvite();
		}
	}

	// Banner global de invitacion a Compartir Musica (visible desde cualquier pestaña de AClient)
	{
		CAClientMusicShare &MusicShare = GameClient()->m_AClientMusicShare;
		if(MusicShare.HasPendingInvite())
		{
			CUIRect InvBanner;
			MainView.HSplitTop(LineSize + 8.0f, &InvBanner, &MainView);
			MainView.HSplitTop(MarginSmall, nullptr, &MainView);
			InvBanner.Draw(ColorRGBA(0.14f, 0.22f, 0.44f, 0.92f), IGraphics::CORNER_ALL, 5.0f);
			InvBanner.Margin(4.0f, &InvBanner);

			CUIRect AcceptBtn, DeclineBtn;
			InvBanner.VSplitRight(72.0f, &InvBanner, &DeclineBtn);
			InvBanner.VSplitRight(5.0f, &InvBanner, nullptr);
			InvBanner.VSplitRight(72.0f, &InvBanner, &AcceptBtn);
			InvBanner.VSplitRight(8.0f, &InvBanner, nullptr);

			char aInvMsg[180];
			str_format(aInvMsg, sizeof(aInvMsg), "Musica: '%s' quiere compartirte '%s'",
				MusicShare.GetPendingInviteFrom(), MusicShare.GetPendingInviteTrack());
			TextRender()->TextColor(0.95f, 0.98f, 1.0f, 1.0f);
			SLabelProperties InvProps;
			InvProps.m_MaxWidth = InvBanner.w;
			InvProps.m_EllipsisAtEnd = true;
			Ui()->DoLabel(&InvBanner, aInvMsg, FontSize * 0.92f, TEXTALIGN_ML, InvProps);
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			auto DrawInvBtn = [&](CButtonContainer *pId, const char *pText, const CUIRect &Rect, ColorRGBA Bg) -> bool {
				const bool Hovered = Ui()->MouseInside(&Rect);
				Rect.Draw(Hovered ? ColorRGBA(std::min(Bg.r + 0.12f, 1.0f), std::min(Bg.g + 0.12f, 1.0f), std::min(Bg.b + 0.12f, 1.0f), 0.95f) : Bg, IGraphics::CORNER_ALL, 4.0f);
				TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
				Ui()->DoLabel(&Rect, pText, FontSize * 0.85f, TEXTALIGN_MC);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
				return Ui()->DoButtonLogic(pId, 0, &Rect, BUTTONFLAG_LEFT) != 0;
			};

			static CButtonContainer s_TopMusicAcceptBtn, s_TopMusicDeclineBtn;
			if(DrawInvBtn(&s_TopMusicAcceptBtn, TCLocalize("Aceptar"), AcceptBtn, ColorRGBA(0.20f, 0.72f, 0.36f, 0.92f)))
				MusicShare.AcceptPendingInvite();
			if(DrawInvBtn(&s_TopMusicDeclineBtn, TCLocalize("Rechazar"), DeclineBtn, ColorRGBA(0.80f, 0.24f, 0.24f, 0.90f)))
				MusicShare.DeclinePendingInvite();
		}
	}

	if(m_CurAClientTab == ACLIENT_TAB_SETTINGS)
		RenderSettingsAClientSettings(MainView);
	else if(m_CurAClientTab == ACLIENT_TAB_HUD)
		RenderSettingsAClientHud(MainView);
	else if(m_CurAClientTab == ACLIENT_TAB_RACESTATS)
		RenderSettingsTClientRaceStats(MainView);
	else if(m_CurAClientTab == ACLIENT_TAB_PROFILES)
		RenderSettingsAClientProfiles(MainView);
	else if(m_CurAClientTab == ACLIENT_TAB_GAMES)
		RenderSettingsAClientGames(MainView);
	else
		RenderSettingsAClientMod(MainView);
}

void CMenus::RenderSettingsAClientHud(CUIRect MainView)
{
	RenderSettingsAClientSettings(MainView);
}

static const char *GetChessPieceIcon(char Piece)
{
	switch((char)toupper((unsigned char)Piece))
	{
	case 'P': return FontIcon::CIRCLE;
	case 'N': return FontIcon::ARROWS_LEFT_RIGHT;
	case 'B': return FontIcon::BRUSH;
	case 'R': return FontIcon::SQUARE_PLUS;
	case 'Q': return FontIcon::STAR;
	case 'K': return FontIcon::USER;
	default: return nullptr;
	}
}

static int GetChessPieceAtlasCol(char Piece)
{
	switch((char)toupper((unsigned char)Piece))
	{
	case 'P': return 0;
	case 'N': return 1;
	case 'B': return 2;
	case 'R': return 3;
	case 'Q': return 4;
	case 'K': return 5;
	default: return -1;
	}
}

struct SChessMove
{
	int m_FromX = 0;
	int m_FromY = 0;
	int m_ToX = 0;
	int m_ToY = 0;
	char m_Promo = 0; // 'Q', 'R', 'B', 'N' or 0
};

struct SChessOpeningStep
{
	int m_FromX;
	int m_FromY;
	int m_ToX;
	int m_ToY;
	const char *m_pNote;
};

struct SChessOpening
{
	const char *m_pName;
	bool m_WhitePerspective;
	const char *m_pSummary;
	int m_Count;
	SChessOpeningStep m_aSteps[10];
};

static const SChessOpening gs_aChessOpenings[10] = {
	{
		"[B] Italiana",
		true,
		"Control rapido del centro y ataque directo sobre f7.",
		9,
		{
			{4, 6, 4, 4, "1. e4 - Ocupa el centro y abre al alfil"},
			{4, 1, 4, 3, "1... e5 - Rival simetrico en el centro"},
			{6, 7, 5, 5, "2. Cf3 - Desarrolla y ataca e5"},
			{1, 0, 2, 2, "2... Cc6 - Defiende e5 desarrollando"},
			{5, 7, 2, 4, "3. Ac4 - Clave de la Italiana: apunta a f7"},
			{5, 0, 2, 3, "3... Ac5 - Desarrollo clasico (Giuoco Piano)"},
			{2, 6, 2, 5, "4. c3 - Prepara d4 para dominar el centro"},
			{6, 0, 5, 2, "4... Cf6 - Contraataca el peon e4"},
			{3, 6, 3, 5, "5. d3 - Sostiene e4 y abre el alfil de c1"},
		},
	},
	{
		"[B] Espanola (Ruy Lopez)",
		true,
		"Presiona al defensor de e5 y busca ventaja a largo plazo.",
		9,
		{
			{4, 6, 4, 4, "1. e4 - Apertura de peon de rey"},
			{4, 1, 4, 3, "1... e5 - Respuesta simetrica"},
			{6, 7, 5, 5, "2. Cf3 - Ataca e5"},
			{1, 0, 2, 2, "2... Cc6 - Defiende e5"},
			{5, 7, 1, 3, "3. Ab5 - Espanola: presiona al caballo de c6"},
			{0, 1, 0, 2, "3... a6 - Variante Morphy, expulsa el alfil"},
			{1, 3, 0, 4, "4. Aa4 - Mantiene la clavada y presion"},
			{6, 0, 5, 2, "4... Cf6 - Desarrolla y presiona e4"},
			{4, 7, 6, 7, "5. O-O - Enroque rapido para proteger al rey"},
		},
	},
	{
		"[B] Gambito de Dama",
		true,
		"Presiona el centro con c4 para debilitar d5.",
		9,
		{
			{3, 6, 3, 4, "1. d4 - Toma espacio central con la dama"},
			{3, 1, 3, 3, "1... d5 - Impide e4 directo"},
			{2, 6, 2, 4, "2. c4 - Gambito de Dama: desafia d5"},
			{4, 1, 4, 2, "2... e6 - Gambito declinado, sostiene d5"},
			{1, 7, 2, 5, "3. Cc3 - Desarrolla y refuerza d5/e4"},
			{6, 0, 5, 2, "3... Cf6 - Desarrollo natural del flanco rey"},
			{2, 7, 6, 3, "4. Ag5 - Clava al caballo de f6"},
			{5, 0, 4, 1, "4... Ae7 - Desclava y prepara enroque"},
			{4, 6, 4, 5, "5. e3 - Consolida d4 y libera el alfil de f1"},
		},
	},
	{
		"[B] Sistema Londres",
		true,
		"Esquema solido con alfil en f4 y piramide de peones.",
		9,
		{
			{3, 6, 3, 4, "1. d4 - Control del centro"},
			{3, 1, 3, 3, "1... d5 - Respuesta cerrada"},
			{2, 7, 5, 4, "2. Af4 - Firma del Sistema Londres antes de e3"},
			{6, 0, 5, 2, "2... Cf6 - Desarrollo del rival"},
			{4, 6, 4, 5, "3. e3 - Refuerza d4 y abre f1"},
			{2, 1, 2, 3, "3... c5 - Las negras golpean el centro"},
			{2, 6, 2, 5, "4. c3 - Crea el triangulo indestructible c3-d4-e3"},
			{1, 0, 2, 2, "4... Cc6 - Presion adicional sobre d4"},
			{1, 7, 3, 6, "5. Cd2 - Desarrolla apoyando e4"},
		},
	},
	{
		"[B] Gambito de Rey",
		true,
		"Sacrificio agresivo para abrir la columna f y atacar.",
		9,
		{
			{4, 6, 4, 4, "1. e4 - Centro abierto"},
			{4, 1, 4, 3, "1... e5 - Respuesta clasica"},
			{5, 6, 5, 4, "2. f4 - Gambito de Rey: desvia el peon e5"},
			{4, 3, 5, 4, "2... exf4 - Gambito aceptado"},
			{6, 7, 5, 5, "3. Cf3 - Evita Dh4+ y desarrolla"},
			{6, 1, 6, 3, "3... g5 - Intenta sostener el peon de mas"},
			{5, 7, 2, 4, "4. Ac4 - Apunta directo a f7"},
			{5, 0, 6, 1, "4... Ag7 - Defiende f6/g5 en fianchetto"},
			{4, 7, 6, 7, "5. O-O - Enroque listo para abrir la columna f"},
		},
	},
	{
		"[N] Siciliana Najdorf",
		false,
		"Defensa asimetrica muy combativa contra 1.e4.",
		10,
		{
			{4, 6, 4, 4, "1. e4 - Blancas abren con peon de rey"},
			{2, 1, 2, 3, "1... c5 - Defensa Siciliana: lucha por d4"},
			{6, 7, 5, 5, "2. Cf3 - Prepara d4"},
			{3, 1, 3, 2, "2... d6 - Controla e5 y prepara Cf6"},
			{3, 6, 3, 4, "3. d4 - Siciliana Abierta"},
			{2, 3, 3, 4, "3... cxd4 - Cambia peon lateral por central"},
			{5, 5, 3, 4, "4. Cxd4 - Recaptura en el centro"},
			{6, 0, 5, 2, "4... Cf6 - Ataca e4 de inmediato"},
			{1, 7, 2, 5, "5. Cc3 - Defiende e4"},
			{0, 1, 0, 2, "5... a6 - Najdorf: evita Ab5/Cb5 y prepara b5"},
		},
	},
	{
		"[N] Defensa Francesa",
		false,
		"Estructura solida que contraataca la cadena blanca en d4.",
		10,
		{
			{4, 6, 4, 4, "1. e4 - Blancas toman el centro"},
			{4, 1, 4, 2, "1... e6 - Defensa Francesa: prepara d5"},
			{3, 6, 3, 4, "2. d4 - Blancas ocupan el centro completo"},
			{3, 1, 3, 3, "2... d5 - Golpea inmediatamente e4"},
			{1, 7, 2, 5, "3. Cc3 - Desarrolla y defiende e4"},
			{6, 0, 5, 2, "3... Cf6 - Variante Clasica: presiona e4"},
			{4, 4, 4, 3, "4. e5 - Gana espacio y expulsa al caballo"},
			{5, 2, 3, 1, "4... Cfd7 - Retirada clave para preparar c5"},
			{5, 6, 5, 4, "5. f4 - Sostiene la cadena de peones"},
			{2, 1, 2, 3, "5... c5 - Ruptura principal contra la base d4"},
		},
	},
	{
		"[N] Defensa Caro-Kann",
		false,
		"Solidez extrema sin encerrar al alfil de c8.",
		10,
		{
			{4, 6, 4, 4, "1. e4 - Apertura de rey"},
			{2, 1, 2, 2, "1... c6 - Caro-Kann: prepara d5 con apoyo"},
			{3, 6, 3, 4, "2. d4 - Centro clasico"},
			{3, 1, 3, 3, "2... d5 - Disputa e4"},
			{1, 7, 2, 5, "3. Cc3 - Defiende e4"},
			{3, 3, 4, 4, "3... dxe4 - Simplifica el centro"},
			{2, 5, 4, 4, "4. Cxe4 - Caballo centralizado"},
			{2, 0, 5, 3, "4... Af5 - Desarrolla el alfil fuera de la cadena"},
			{4, 4, 6, 5, "5. Cg3 - Gana tiempo atacando al alfil"},
			{5, 3, 6, 2, "5... Ag6 - Alfil seguro en la gran diagonal"},
		},
	},
	{
		"[N] India de Rey",
		false,
		"Hipermoderna: cede el centro para contraatacarlo luego.",
		10,
		{
			{3, 6, 3, 4, "1. d4 - Apertura de dama"},
			{6, 0, 5, 2, "1... Cf6 - Controla e4 y d5 con pieza"},
			{2, 6, 2, 4, "2. c4 - Gana espacio en el flanco dama"},
			{6, 1, 6, 2, "2... g6 - Prepara el fianchetto de alfil"},
			{1, 7, 2, 5, "3. Cc3 - Prepara e4"},
			{5, 0, 6, 1, "3... Ag7 - Alfil fianchettado domina la diagonal"},
			{4, 6, 4, 4, "4. e4 - Centro amplio blanco"},
			{3, 1, 3, 2, "4... d6 - Frena e5 y prepara e5/c5"},
			{6, 7, 5, 5, "5. Cf3 - Desarrollo natural"},
			{4, 0, 6, 0, "5... O-O - Enroque listo para romper con e5"},
		},
	},
	{
		"[N] Defensa Escandinava",
		false,
		"Desafia e4 desde la primera jugada con desarrollo activo.",
		10,
		{
			{4, 6, 4, 4, "1. e4 - Apertura de rey"},
			{3, 1, 3, 3, "1... d5 - Escandinava: rompe el centro ya"},
			{4, 4, 3, 3, "2. exd5 - Captura obligada para mantener iniciativa"},
			{3, 0, 3, 3, "2... Dxd5 - Recaptura con la dama"},
			{1, 7, 2, 5, "3. Cc3 - Desarrolla ganando tiempo sobre la dama"},
			{3, 3, 0, 3, "3... Da5 - Casilla segura clavando hacia e1"},
			{3, 6, 3, 4, "4. d4 - Toma el centro"},
			{6, 0, 5, 2, "4... Cf6 - Desarrollo activo sobre d5/e4"},
			{6, 7, 5, 5, "5. Cf3 - Desarrollo normal"},
			{2, 0, 5, 3, "5... Af5 - Alfil activo fuera de la cadena de peones"},
		},
	},
};

struct SChessOnlineContext
{
	bool m_InOnlineMatch = false;
	bool m_PendingStartOnlineMatch = false;
	int m_OpponentClientId = -1;
	char m_aOpponentName[MAX_NAME_LENGTH] = "";
	bool m_LocalIsWhite = true;
	bool m_OpponentResigned = false;

	int m_SelectedTargetId = -1;
	int m_WaitingInviteTargetId = -1;
	char m_aWaitingInviteName[MAX_NAME_LENGTH] = "";
	bool m_WaitingLocalIsWhite = true;

	int m_IncomingInviteClientId = -1;
	char m_aIncomingInviteName[MAX_NAME_LENGTH] = "";
	bool m_IncomingLocalIsWhite = false;

	static constexpr int MAX_PENDING_MOVES = 64;
	SChessMove m_aPendingRemoteMoves[MAX_PENDING_MOVES] = {};
	int m_PendingHead = 0;
	int m_PendingTail = 0;
	int m_PendingCount = 0;
	int64_t m_LastInviteSentTime = 0;

	void ClearPendingMoves()
	{
		m_PendingHead = 0;
		m_PendingTail = 0;
		m_PendingCount = 0;
	}

	void PushPendingMove(const SChessMove &Move)
	{
		if(m_PendingCount >= MAX_PENDING_MOVES)
			return;
		m_aPendingRemoteMoves[m_PendingTail] = Move;
		m_PendingTail = (m_PendingTail + 1) % MAX_PENDING_MOVES;
		++m_PendingCount;
	}

	bool PopPendingMove(SChessMove &Out)
	{
		if(m_PendingCount <= 0)
			return false;
		Out = m_aPendingRemoteMoves[m_PendingHead];
		m_PendingHead = (m_PendingHead + 1) % MAX_PENDING_MOVES;
		--m_PendingCount;
		return true;
	}
};

static SChessOnlineContext gs_ChessOnline;

static void SendChessWhisper(CGameClient *pGameClient, int TargetId, const char *pTargetName, const char *pPayload)
{
	if(!pGameClient || !pTargetName || pTargetName[0] == '\0' || !pPayload)
		return;
	if(pGameClient->Client()->IsSixup() && TargetId >= 0 && TargetId < MAX_CLIENTS)
	{
		char aMsg[MAX_CHAT_LENGTH];
		str_format(aMsg, sizeof(aMsg), "[AC-CHESS] %s", pPayload);
		protocol7::CNetMsg_Cl_Say Msg7;
		Msg7.m_Mode = protocol7::CHAT_WHISPER;
		Msg7.m_Target = TargetId;
		Msg7.m_pMessage = aMsg;
		pGameClient->Client()->SendPackMsgActive(&Msg7, MSGFLAG_VITAL, true);
		return;
	}
	char aSafeTarget[MAX_NAME_LENGTH];
	str_copy(aSafeTarget, pTargetName);
	for(char *p = aSafeTarget; *p; ++p)
	{
		if(*p == '"')
			*p = '\'';
	}
	char aCmd[MAX_CHAT_LENGTH];
	str_format(aCmd, sizeof(aCmd), "/w \"%s\" [AC-CHESS] %s", aSafeTarget, pPayload);
	pGameClient->m_Chat.SendChat(0, aCmd);
}

bool CMenus::OnChessNetworkMessage(int ClientId, int Team, const char *pLine)
{
	if(!pLine)
		return false;
	const char *pTag = str_find(pLine, "[AC-CHESS]");
	if(!pTag)
		return false;

	// Suppress whisper send echo (Team == 2) or messages from local client IDs
	if(Team == 2 || ClientId == GameClient()->m_aLocalIds[0] || ClientId == GameClient()->m_aLocalIds[1])
		return true;

	// Fallback for servers without VERSION_DDNET_WHISPER that format as "[← Name] [AC-CHESS] ..."
	if(ClientId < 0)
	{
		const char *pArrow = str_find(pLine, "← ");
		const char *pBracket = pArrow ? str_find(pArrow + 4, "]") : nullptr;
		if(pArrow && pBracket && pBracket > pArrow + 4)
		{
			char aNameBuf[MAX_NAME_LENGTH];
			str_truncate(aNameBuf, sizeof(aNameBuf), pArrow + 4, (int)(pBracket - (pArrow + 4)));
			const int OppId = gs_ChessOnline.m_OpponentClientId;
			if(gs_ChessOnline.m_InOnlineMatch && OppId >= 0 && OppId < MAX_CLIENTS &&
				GameClient()->m_aClients[OppId].m_Active && str_comp(GameClient()->m_aClients[OppId].m_aName, aNameBuf) == 0)
			{
				ClientId = OppId;
			}
			else
			{
				for(int i = 0; i < MAX_CLIENTS; ++i)
				{
					if(GameClient()->m_aClients[i].m_Active && str_comp(GameClient()->m_aClients[i].m_aName, aNameBuf) == 0)
					{
						ClientId = i;
						break;
					}
				}
			}
		}
	}

	if(ClientId < 0 || ClientId >= MAX_CLIENTS || !GameClient()->m_aClients[ClientId].m_Active)
		return true;

	const char *pCmd = pTag + 10; // past "[AC-CHESS]"
	if(*pCmd == ' ')
		pCmd++;
	const char *pSenderName = GameClient()->m_aClients[ClientId].m_aName;

	if(str_startswith(pCmd, "INV "))
	{
		const char ChallengerColor = pCmd[4];
		gs_ChessOnline.m_IncomingInviteClientId = ClientId;
		str_copy(gs_ChessOnline.m_aIncomingInviteName, pSenderName);
		gs_ChessOnline.m_IncomingLocalIsWhite = (ChallengerColor != 'W');
		char aEcho[256];
		str_format(aEcho, sizeof(aEcho), "Ajedrez: '%s' te reto 1v1 (juegas %s). Acepta en AClient -> Games.",
			pSenderName, gs_ChessOnline.m_IncomingLocalIsWhite ? "Blancas" : "Negras");
		GameClient()->m_Chat.Echo(aEcho);
	}
	else if(str_startswith(pCmd, "ACC"))
	{
		if(gs_ChessOnline.m_WaitingInviteTargetId == ClientId)
		{
			gs_ChessOnline.m_InOnlineMatch = true;
			gs_ChessOnline.m_PendingStartOnlineMatch = true;
			gs_ChessOnline.m_OpponentClientId = ClientId;
			str_copy(gs_ChessOnline.m_aOpponentName, pSenderName);
			gs_ChessOnline.m_LocalIsWhite = gs_ChessOnline.m_WaitingLocalIsWhite;
			gs_ChessOnline.m_OpponentResigned = false;
			gs_ChessOnline.m_WaitingInviteTargetId = -1;
			gs_ChessOnline.m_IncomingInviteClientId = -1;
			gs_ChessOnline.ClearPendingMoves();
			char aEcho[256];
			str_format(aEcho, sizeof(aEcho), "Ajedrez: '%s' acepto el reto! Ve a AClient -> Games.", pSenderName);
			GameClient()->m_Chat.Echo(aEcho);
		}
	}
	else if(str_startswith(pCmd, "DEC"))
	{
		if(gs_ChessOnline.m_WaitingInviteTargetId == ClientId)
		{
			gs_ChessOnline.m_WaitingInviteTargetId = -1;
			char aEcho[256];
			str_format(aEcho, sizeof(aEcho), "Ajedrez: '%s' rechazo tu reto.", pSenderName);
			GameClient()->m_Chat.Echo(aEcho);
		}
	}
	else if(str_startswith(pCmd, "MOV "))
	{
		const char *pMoveStr = pCmd + 4;
		if(gs_ChessOnline.m_InOnlineMatch && ClientId == gs_ChessOnline.m_OpponentClientId && str_length(pMoveStr) >= 4)
		{
			const int FX = pMoveStr[0] - '0';
			const int FY = pMoveStr[1] - '0';
			const int TX = pMoveStr[2] - '0';
			const int TY = pMoveStr[3] - '0';
			const char Promo = (pMoveStr[4] == 'Q' || pMoveStr[4] == 'R' || pMoveStr[4] == 'B' || pMoveStr[4] == 'N') ? pMoveStr[4] : 0;
			if(FX >= 0 && FX < 8 && FY >= 0 && FY < 8 && TX >= 0 && TX < 8 && TY >= 0 && TY < 8)
				gs_ChessOnline.PushPendingMove({FX, FY, TX, TY, Promo});
		}
	}
	else if(str_startswith(pCmd, "RES"))
	{
		if(gs_ChessOnline.m_InOnlineMatch && ClientId == gs_ChessOnline.m_OpponentClientId)
			gs_ChessOnline.m_OpponentResigned = true;
	}

	return true;
}

void CMenus::RenderSettingsAClientGames(CUIRect MainView)
{
	const float AnimTime = time_get() / (float)time_freq();
	auto RenderIconLabel = [&](const CUIRect &Rect, const char *pIcon, float Size, int Align, const ColorRGBA *pColor = nullptr) {
		const ColorRGBA Color = pColor ? *pColor : TextRender()->DefaultTextColor();
		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING);
		TextRender()->TextColor(Color);
		Ui()->DoLabel(&Rect, pIcon, Size, Align);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	};

	static IGraphics::CTextureHandle s_ChessPiecesTex;
	static bool s_ChessPiecesLoaded = false;
	if(!s_ChessPiecesLoaded)
	{
		s_ChessPiecesLoaded = true;
		CImageInfo Image;
		if(Graphics()->LoadPng(Image, gs_aChessPiecesPng, sizeof(gs_aChessPiecesPng), "chess_pieces"))
		{
			s_ChessPiecesTex = Graphics()->LoadTextureRawMove(Image, 0, "chess_pieces");
		}
	}

	using TChessBoard = std::array<std::array<char, 8>, 8>;

	struct SChessPosState
	{
		TChessBoard m_Board{};
		bool m_WhiteTurn = true;
		bool m_WhiteKingSideCastle = true;
		bool m_WhiteQueenSideCastle = true;
		bool m_BlackKingSideCastle = true;
		bool m_BlackQueenSideCastle = true;
		int m_EnPassantColumn = -1;
		int m_HalfMoveClock = 0;
	};

	struct SChessSnapshot
	{
		SChessPosState m_Pos;
		bool m_HasLastMove = false;
		int m_LastFromX = -1;
		int m_LastFromY = -1;
		int m_LastToX = -1;
		int m_LastToY = -1;
		std::vector<char> m_vCapturedByWhite;
		std::vector<char> m_vCapturedByBlack;
	};

	struct SChessState
	{
		bool m_Initialized = false;
		SChessPosState m_Pos;
		bool m_GameOver = false;
		bool m_WhiteWon = false;
		bool m_Stalemate = false;
		bool m_FiftyMove = false;
		bool m_Threefold = false;
		bool m_Resigned = false;
		bool m_GameLogged = false;

		int m_SelectedX = -1;
		int m_SelectedY = -1;
		bool m_Dragging = false;
		int m_DragFromX = -1;
		int m_DragFromY = -1;
		vec2 m_DragMouse = vec2(0.0f, 0.0f);

		bool m_HasMoveAnim = false;
		char m_AnimPiece = '.';
		int m_AnimFromX = -1;
		int m_AnimFromY = -1;
		int m_AnimToX = -1;
		int m_AnimToY = -1;
		bool m_HasRookAnim = false;
		char m_AnimRookPiece = '.';
		int m_AnimRookFromX = -1;
		int m_AnimRookToX = -1;
		int m_AnimRookY = -1;
		float m_AnimStart = 0.0f;
		float m_AnimDuration = 0.16f;

		bool m_HasLastMove = false;
		int m_LastFromX = -1;
		int m_LastFromY = -1;
		int m_LastToX = -1;
		int m_LastToY = -1;

		// Settings: Board Theme (0=Verde Foto, 1=Madera, 2=Azul, 3=Oscuro)
		int m_BoardTheme = 0;
		// Piece Model (0=Clasico Foto, 1=Iconos)
		int m_PieceStyle = 0;
		// Color Mode (0=Blancas, 1=Random, 2=Negras)
		int m_ColorChoice = 0;
		bool m_PlayerIsWhite = true;
		bool m_ManualFlip = false;
		// Difficulty (0=Facil, 1=Medio, 2=Dificil, 3=Experto, 4=2 Jugadores, 5=1v1 Servidor, 6=Aperturas)
		int m_Difficulty = 2;
		float m_BotReadyTime = 0.0f;

		// Opening Trainer state
		int m_OpeningIndex = 0;
		int m_OpeningStep = 0;
		bool m_OpeningShowHint = true;
		bool m_OpeningRevealHint = false;
		char m_aOpeningFeedback[96] = "";

		// Pending promotion for human
		bool m_PendingPromotion = false;
		SChessMove m_PromoMove;

		std::unordered_map<uint64_t, int> m_RepetitionTable;
		std::vector<SChessSnapshot> m_vHistory;
		std::vector<std::string> m_vMoveNotation;
		std::vector<char> m_vCapturedByWhite; // lowercase black pieces captured by white
		std::vector<char> m_vCapturedByBlack; // uppercase white pieces captured by black

		int m_CachedCheckPly = -1;
		bool m_CachedSideInCheck = false;
		int m_CachedCheckKingX = -1;
		int m_CachedCheckKingY = -1;

		struct SChessArrow
		{
			int m_FromX = 0;
			int m_FromY = 0;
			int m_ToX = 0;
			int m_ToY = 0;
			bool operator==(const SChessArrow &O) const { return m_FromX == O.m_FromX && m_FromY == O.m_FromY && m_ToX == O.m_ToX && m_ToY == O.m_ToY; }
		};
		std::vector<SChessArrow> m_vArrows;
		bool m_aMarkedSquares[8][8] = {};
		bool m_aValidTargets[8][8] = {};
		int m_CachedSelX = -2;
		int m_CachedSelY = -2;
		int m_CachedSelPly = -1;
		bool m_RightDragging = false;
		int m_RightFromX = -1;
		int m_RightFromY = -1;
	};

	static SChessState s_Chess;

	auto IsWhitePiece = [](char Piece) { return Piece >= 'A' && Piece <= 'Z'; };
	auto IsBlackPiece = [](char Piece) { return Piece >= 'a' && Piece <= 'z'; };

	auto MakeStateKey = [&](const SChessPosState &Pos) -> uint64_t {
		uint64_t Hash = 14695981039346656037ULL;
		auto MixByte = [&](uint8_t B) {
			Hash ^= (uint64_t)B;
			Hash *= 1099511628211ULL;
		};
		for(int y = 0; y < 8; ++y)
			for(int x = 0; x < 8; ++x)
				MixByte((uint8_t)Pos.m_Board[y][x]);
		MixByte((uint8_t)(Pos.m_WhiteTurn ? 1 : 0));
		uint8_t CastleMask = 0;
		if(Pos.m_WhiteKingSideCastle)
			CastleMask |= 1;
		if(Pos.m_WhiteQueenSideCastle)
			CastleMask |= 2;
		if(Pos.m_BlackKingSideCastle)
			CastleMask |= 4;
		if(Pos.m_BlackQueenSideCastle)
			CastleMask |= 8;
		MixByte(CastleMask);
		MixByte((uint8_t)(Pos.m_EnPassantColumn + 1));
		return Hash;
	};

	auto ResetChess = [&]() {
		static const char *s_apSetup[8] = {
			"rnbqkbnr",
			"pppppppp",
			"........",
			"........",
			"........",
			"........",
			"PPPPPPPP",
			"RNBQKBNR"};
		s_Chess.m_Initialized = true;
		s_Chess.m_Pos = SChessPosState{};
		for(int y = 0; y < 8; ++y)
			for(int x = 0; x < 8; ++x)
				s_Chess.m_Pos.m_Board[y][x] = s_apSetup[y][x];

		if(s_Chess.m_Difficulty == 6)
			s_Chess.m_PlayerIsWhite = gs_aChessOpenings[std::clamp(s_Chess.m_OpeningIndex, 0, 9)].m_WhitePerspective;
		else if(s_Chess.m_ColorChoice == 0)
			s_Chess.m_PlayerIsWhite = true;
		else if(s_Chess.m_ColorChoice == 2)
			s_Chess.m_PlayerIsWhite = false;
		else
			s_Chess.m_PlayerIsWhite = (rand() % 2) == 0;

		s_Chess.m_ManualFlip = false;
		s_Chess.m_GameOver = false;
		s_Chess.m_WhiteWon = false;
		s_Chess.m_CachedCheckPly = -1;
		s_Chess.m_Stalemate = false;
		s_Chess.m_FiftyMove = false;
		s_Chess.m_Threefold = false;
		s_Chess.m_Resigned = false;
		s_Chess.m_GameLogged = false;
		s_Chess.m_OpeningStep = 0;
		s_Chess.m_OpeningRevealHint = false;
		s_Chess.m_aOpeningFeedback[0] = '\0';
		s_Chess.m_SelectedX = -1;
		s_Chess.m_SelectedY = -1;
		s_Chess.m_Dragging = false;
		s_Chess.m_DragFromX = -1;
		s_Chess.m_DragFromY = -1;
		s_Chess.m_HasMoveAnim = false;
		s_Chess.m_HasRookAnim = false;
		s_Chess.m_AnimPiece = '.';
		s_Chess.m_HasLastMove = false;
		s_Chess.m_PendingPromotion = false;
		s_Chess.m_BotReadyTime = AnimTime + 0.16f;

		s_Chess.m_RepetitionTable.clear();
		s_Chess.m_RepetitionTable[MakeStateKey(s_Chess.m_Pos)] = 1;
		s_Chess.m_CachedCheckPly = -1;
		s_Chess.m_CachedSideInCheck = false;
		s_Chess.m_CachedCheckKingX = -1;
		s_Chess.m_CachedCheckKingY = -1;
		s_Chess.m_vHistory.clear();
		s_Chess.m_vMoveNotation.clear();
		s_Chess.m_vCapturedByWhite.clear();
		s_Chess.m_vCapturedByBlack.clear();
		s_Chess.m_vArrows.clear();
		memset(s_Chess.m_aMarkedSquares, 0, sizeof(s_Chess.m_aMarkedSquares));
		s_Chess.m_RightDragging = false;
	};

	auto IsPathClearOnBoard = [](const TChessBoard &Board, int FromX, int FromY, int ToX, int ToY) {
		const int StepX = (ToX > FromX) - (ToX < FromX);
		const int StepY = (ToY > FromY) - (ToY < FromY);
		int x = FromX + StepX;
		int y = FromY + StepY;
		while(x != ToX || y != ToY)
		{
			if(Board[y][x] != '.')
				return false;
			x += StepX;
			y += StepY;
		}
		return true;
	};

	auto IsUnderAttack = [&](const TChessBoard &Board, int ToX, int ToY, bool TargetIsWhite) {
		for(int FromY = 0; FromY < 8; ++FromY)
		{
			for(int FromX = 0; FromX < 8; ++FromX)
			{
				if(FromX == ToX && FromY == ToY)
					continue;
				const char Piece = Board[FromY][FromX];
				if(Piece == '.' || IsWhitePiece(Piece) == TargetIsWhite)
					continue;

				const int Dx = ToX - FromX;
				const int Dy = ToY - FromY;
				const int AbsDx = abs(Dx);
				const int AbsDy = abs(Dy);
				const char UpperPiece = (char)toupper((unsigned char)Piece);

				if(UpperPiece == 'P')
				{
					const int Backward = TargetIsWhite ? 1 : -1;
					if(Dy == Backward && AbsDx == 1)
						return true;
				}
				else if(UpperPiece == 'N')
				{
					if((AbsDx == 1 && AbsDy == 2) || (AbsDx == 2 && AbsDy == 1))
						return true;
				}
				else if(UpperPiece == 'B' || UpperPiece == 'R' || UpperPiece == 'Q')
				{
					bool IsValidLine = false;
					if(UpperPiece == 'B')
						IsValidLine = (AbsDx == AbsDy);
					else if(UpperPiece == 'R')
						IsValidLine = (Dx == 0 || Dy == 0);
					else
						IsValidLine = ((AbsDx == AbsDy) || (Dx == 0 || Dy == 0));
					if(IsValidLine && IsPathClearOnBoard(Board, FromX, FromY, ToX, ToY))
						return true;
				}
				else if(UpperPiece == 'K')
				{
					if(AbsDx <= 1 && AbsDy <= 1)
						return true;
				}
			}
		}
		return false;
	};

	auto FindKingPos = [&](const TChessBoard &Board, bool WhiteKing, int &OutX, int &OutY) {
		const char TargetKing = WhiteKing ? 'K' : 'k';
		for(int y = 0; y < 8; ++y)
		{
			for(int x = 0; x < 8; ++x)
			{
				if(Board[y][x] == TargetKing)
				{
					OutX = x;
					OutY = y;
					return true;
				}
			}
		}
		return false;
	};

	auto IsCheckOnBoard = [&](const TChessBoard &Board, bool WhiteTurn) {
		int Kx = -1, Ky = -1;
		if(!FindKingPos(Board, WhiteTurn, Kx, Ky))
			return false;
		return IsUnderAttack(Board, Kx, Ky, WhiteTurn);
	};

	auto IsValidMoveInState = [&](const SChessPosState &Pos, int FromX, int FromY, int ToX, int ToY) {
		if(FromX == ToX && FromY == ToY)
			return false;
		if(ToX < 0 || ToY < 0 || ToX >= 8 || ToY >= 8)
			return false;

		const TChessBoard &Board = Pos.m_Board;
		const char Piece = Board[FromY][FromX];
		const char Target = Board[ToY][ToX];
		if(Piece == '.')
			return false;
		if(Target != '.' && IsWhitePiece(Piece) == IsWhitePiece(Target))
			return false;
		if(Target != '.' && (char)toupper((unsigned char)Target) == 'K')
			return false;

		const int Dx = ToX - FromX;
		const int Dy = ToY - FromY;
		const int AbsDx = abs(Dx);
		const int AbsDy = abs(Dy);
		const char UpperPiece = (char)toupper((unsigned char)Piece);

		bool IsBasicMoveValid = false;
		if(UpperPiece == 'P')
		{
			const int Forward = IsWhitePiece(Piece) ? -1 : 1;
			const int StartRow = IsWhitePiece(Piece) ? 6 : 1;
			if(Dx == 0 && Target == '.')
			{
				if(Dy == Forward)
					IsBasicMoveValid = true;
				else if(FromY == StartRow && Dy == Forward * 2 && Board[FromY + Forward][FromX] == '.')
					IsBasicMoveValid = true;
			}
			else if(AbsDx == 1 && Dy == Forward)
			{
				if(Target != '.' && IsWhitePiece(Target) != IsWhitePiece(Piece))
					IsBasicMoveValid = true;
				else if(Target == '.' && ToX == Pos.m_EnPassantColumn)
				{
					const int EnPassantRow = IsWhitePiece(Piece) ? 3 : 4;
					const char EnPassantVictim = Board[FromY][ToX];
					if(FromY == EnPassantRow && (char)toupper((unsigned char)EnPassantVictim) == 'P' && IsWhitePiece(EnPassantVictim) != IsWhitePiece(Piece))
						IsBasicMoveValid = true;
				}
			}
		}
		else if(UpperPiece == 'N')
			IsBasicMoveValid = (AbsDx == 1 && AbsDy == 2) || (AbsDx == 2 && AbsDy == 1);
		else if(UpperPiece == 'B')
			IsBasicMoveValid = AbsDx == AbsDy && IsPathClearOnBoard(Board, FromX, FromY, ToX, ToY);
		else if(UpperPiece == 'R')
			IsBasicMoveValid = (Dx == 0 || Dy == 0) && IsPathClearOnBoard(Board, FromX, FromY, ToX, ToY);
		else if(UpperPiece == 'Q')
			IsBasicMoveValid = ((AbsDx == AbsDy) || (Dx == 0 || Dy == 0)) && IsPathClearOnBoard(Board, FromX, FromY, ToX, ToY);
		else if(UpperPiece == 'K')
		{
			if(IsUnderAttack(Board, ToX, ToY, IsWhitePiece(Piece)))
				return false;

			if(AbsDx == 2 && AbsDy == 0)
			{
				const int KingStartY = IsWhitePiece(Piece) ? 7 : 0;
				if(FromX != 4 || FromY != KingStartY || ToY != KingStartY)
					return false;

				const int RookPos = Dx == 2 ? 7 : 0;
				const char ExpectedRook = IsWhitePiece(Piece) ? 'R' : 'r';
				if(Board[ToY][RookPos] != ExpectedRook)
					return false;
				if(!IsPathClearOnBoard(Board, FromX, FromY, RookPos, ToY))
					return false;
				if(IsUnderAttack(Board, FromX, FromY, IsWhitePiece(Piece)))
					return false;
				if(IsUnderAttack(Board, FromX + Dx / 2, FromY, IsWhitePiece(Piece)))
					return false;
				if(IsUnderAttack(Board, FromX + Dx, FromY, IsWhitePiece(Piece)))
					return false;

				IsBasicMoveValid = (Dx == 2) ? (IsWhitePiece(Piece) ? Pos.m_WhiteKingSideCastle : Pos.m_BlackKingSideCastle) : (IsWhitePiece(Piece) ? Pos.m_WhiteQueenSideCastle : Pos.m_BlackQueenSideCastle);
			}
			else
			{
				IsBasicMoveValid = AbsDx <= 1 && AbsDy <= 1;
			}
		}

		if(!IsBasicMoveValid)
			return false;

		TChessBoard TempBoard = Board;
		TempBoard[ToY][ToX] = Piece;
		TempBoard[FromY][FromX] = '.';

		if(UpperPiece == 'P' && FromX != ToX && Target == '.')
		{
			const char EnPassantVictim = TempBoard[FromY][ToX];
			if((char)toupper((unsigned char)EnPassantVictim) == 'P' && IsWhitePiece(EnPassantVictim) != IsWhitePiece(Piece))
				TempBoard[FromY][ToX] = '.';
		}
		else if(UpperPiece == 'K' && AbsDx == 2)
		{
			const bool KingSide = ToX > FromX;
			const int RookFromX = KingSide ? 7 : 0;
			const int RookToX = KingSide ? 5 : 3;
			TempBoard[FromY][RookToX] = TempBoard[FromY][RookFromX];
			TempBoard[FromY][RookFromX] = '.';
		}

		if(IsCheckOnBoard(TempBoard, IsWhitePiece(Piece)))
			return false;

		return true;
	};

	auto CollectLegalMovesInState = [&](const SChessPosState &Pos, bool WhiteTurn) {
		std::vector<SChessMove> vMoves;
		vMoves.reserve(40);
		for(int y = 0; y < 8; ++y)
		{
			for(int x = 0; x < 8; ++x)
			{
				const char Piece = Pos.m_Board[y][x];
				if(Piece == '.' || (WhiteTurn != IsWhitePiece(Piece)))
					continue;
				for(int ty = 0; ty < 8; ++ty)
				{
					for(int tx = 0; tx < 8; ++tx)
					{
						if(IsValidMoveInState(Pos, x, y, tx, ty))
							vMoves.push_back({x, y, tx, ty, 0});
					}
				}
			}
		}
		return vMoves;
	};

	auto HasAnyLegalMoveInState = [&](const SChessPosState &Pos, bool WhiteTurn) {
		for(int y = 0; y < 8; ++y)
		{
			for(int x = 0; x < 8; ++x)
			{
				const char Piece = Pos.m_Board[y][x];
				if(Piece == '.' || (WhiteTurn != IsWhitePiece(Piece)))
					continue;
				for(int ty = 0; ty < 8; ++ty)
				{
					for(int tx = 0; tx < 8; ++tx)
					{
						if(IsValidMoveInState(Pos, x, y, tx, ty))
							return true;
					}
				}
			}
		}
		return false;
	};

	auto PieceValue = [](char Piece) {
		switch((char)toupper((unsigned char)Piece))
		{
		case 'P': return 100;
		case 'N': return 320;
		case 'B': return 330;
		case 'R': return 500;
		case 'Q': return 900;
		case 'K': return 20000;
		default: return 0;
		}
	};

	auto ApplyMoveInState = [&](SChessPosState &Pos, const SChessMove &Move, char *pOutRealCaptured = nullptr) {
		const char MovingPiece = Pos.m_Board[Move.m_FromY][Move.m_FromX];
		char RealCaptured = Pos.m_Board[Move.m_ToY][Move.m_ToX];
		const char Upper = (char)toupper((unsigned char)MovingPiece);
		const bool MovingWhite = IsWhitePiece(MovingPiece);

		Pos.m_Board[Move.m_ToY][Move.m_ToX] = MovingPiece;
		Pos.m_Board[Move.m_FromY][Move.m_FromX] = '.';

		if(Upper == 'P' && (Move.m_ToY == 0 || Move.m_ToY == 7))
		{
			const char PromoUpper = (Move.m_Promo == 'R' || Move.m_Promo == 'B' || Move.m_Promo == 'N') ? Move.m_Promo : 'Q';
			Pos.m_Board[Move.m_ToY][Move.m_ToX] = MovingWhite ? PromoUpper : (char)tolower((unsigned char)PromoUpper);
		}

		if(Upper == 'K' && abs(Move.m_ToX - Move.m_FromX) == 2)
		{
			const bool KingSide = Move.m_ToX > Move.m_FromX;
			const int RookFromX = KingSide ? 7 : 0;
			const int RookToX = KingSide ? 5 : 3;
			Pos.m_Board[Move.m_FromY][RookToX] = Pos.m_Board[Move.m_FromY][RookFromX];
			Pos.m_Board[Move.m_FromY][RookFromX] = '.';
		}

		if(Upper == 'P' && Move.m_FromX != Move.m_ToX && RealCaptured == '.')
		{
			const char EnPassantVictim = Pos.m_Board[Move.m_FromY][Move.m_ToX];
			if((char)toupper((unsigned char)EnPassantVictim) == 'P' && IsWhitePiece(EnPassantVictim) != MovingWhite)
			{
				RealCaptured = EnPassantVictim;
				Pos.m_Board[Move.m_FromY][Move.m_ToX] = '.';
			}
		}

		if(MovingPiece == 'K')
		{
			Pos.m_WhiteKingSideCastle = false;
			Pos.m_WhiteQueenSideCastle = false;
		}
		else if(MovingPiece == 'k')
		{
			Pos.m_BlackKingSideCastle = false;
			Pos.m_BlackQueenSideCastle = false;
		}
		else if(MovingPiece == 'R')
		{
			if(Move.m_FromX == 0 && Move.m_FromY == 7)
				Pos.m_WhiteQueenSideCastle = false;
			else if(Move.m_FromX == 7 && Move.m_FromY == 7)
				Pos.m_WhiteKingSideCastle = false;
		}
		else if(MovingPiece == 'r')
		{
			if(Move.m_FromX == 0 && Move.m_FromY == 0)
				Pos.m_BlackQueenSideCastle = false;
			else if(Move.m_FromX == 7 && Move.m_FromY == 0)
				Pos.m_BlackKingSideCastle = false;
		}

		if(RealCaptured == 'R' && Move.m_ToX == 0 && Move.m_ToY == 7)
			Pos.m_WhiteQueenSideCastle = false;
		else if(RealCaptured == 'R' && Move.m_ToX == 7 && Move.m_ToY == 7)
			Pos.m_WhiteKingSideCastle = false;
		else if(RealCaptured == 'r' && Move.m_ToX == 0 && Move.m_ToY == 0)
			Pos.m_BlackQueenSideCastle = false;
		else if(RealCaptured == 'r' && Move.m_ToX == 7 && Move.m_ToY == 0)
			Pos.m_BlackKingSideCastle = false;

		if(Upper == 'P' && abs(Move.m_FromY - Move.m_ToY) == 2)
			Pos.m_EnPassantColumn = Move.m_ToX;
		else
			Pos.m_EnPassantColumn = -1;

		if(RealCaptured != '.' || Upper == 'P')
			Pos.m_HalfMoveClock = 0;
		else
			Pos.m_HalfMoveClock++;

		Pos.m_WhiteTurn = !Pos.m_WhiteTurn;
		if(pOutRealCaptured)
			*pOutRealCaptured = RealCaptured;
		return RealCaptured;
	};

	auto EvaluateBoard = [&](const TChessBoard &Board) {
		static const int s_aPstP[64] = {
			0, 0, 0, 0, 0, 0, 0, 0,
			50, 50, 50, 50, 50, 50, 50, 50,
			10, 10, 20, 30, 30, 20, 10, 10,
			5, 5, 10, 25, 25, 10, 5, 5,
			0, 0, 0, 20, 20, 0, 0, 0,
			5, -5, -10, 0, 0, -10, -5, 5,
			5, 10, 10, -20, -20, 10, 10, 5,
			0, 0, 0, 0, 0, 0, 0, 0};
		static const int s_aPstN[64] = {
			-50, -40, -30, -30, -30, -30, -40, -50,
			-40, -20, 0, 0, 0, 0, -20, -40,
			-30, 0, 10, 15, 15, 10, 0, -30,
			-30, 5, 15, 20, 20, 15, 5, -30,
			-30, 0, 15, 20, 20, 15, 0, -30,
			-30, 5, 10, 15, 15, 10, 5, -30,
			-40, -20, 0, 5, 5, 0, -20, -40,
			-50, -40, -30, -30, -30, -30, -40, -50};
		static const int s_aPstB[64] = {
			-20, -10, -10, -10, -10, -10, -10, -20,
			-10, 0, 0, 0, 0, 0, 0, -10,
			-10, 0, 5, 10, 10, 5, 0, -10,
			-10, 5, 5, 10, 10, 5, 5, -10,
			-10, 0, 10, 10, 10, 10, 0, -10,
			-10, 10, 10, 10, 10, 10, 10, -10,
			-10, 5, 0, 0, 0, 0, 5, -10,
			-20, -10, -10, -10, -10, -10, -10, -20};
		static const int s_aPstR[64] = {
			0, 0, 0, 0, 0, 0, 0, 0,
			5, 10, 10, 10, 10, 10, 10, 5,
			-5, 0, 0, 0, 0, 0, 0, -5,
			-5, 0, 0, 0, 0, 0, 0, -5,
			-5, 0, 0, 0, 0, 0, 0, -5,
			-5, 0, 0, 0, 0, 0, 0, -5,
			-5, 0, 0, 0, 0, 0, 0, -5,
			0, 0, 0, 5, 5, 0, 0, 0};
		static const int s_aPstQ[64] = {
			-20, -10, -10, -5, -5, -10, -10, -20,
			-10, 0, 0, 0, 0, 0, 0, -10,
			-10, 0, 5, 5, 5, 5, 0, -10,
			-5, 0, 5, 5, 5, 5, 0, -5,
			0, 0, 5, 5, 5, 5, 0, -5,
			-10, 5, 5, 5, 5, 5, 0, -10,
			-10, 0, 5, 0, 0, 0, 0, -10,
			-20, -10, -10, -5, -5, -10, -10, -20};
		static const int s_aPstKMid[64] = {
			-30, -40, -40, -50, -50, -40, -40, -30,
			-30, -40, -40, -50, -50, -40, -40, -30,
			-30, -40, -40, -50, -50, -40, -40, -30,
			-30, -40, -40, -50, -50, -40, -40, -30,
			-20, -30, -30, -40, -40, -30, -30, -20,
			-10, -20, -20, -20, -20, -20, -20, -10,
			20, 20, 0, 0, 0, 0, 20, 20,
			20, 30, 10, 0, 0, 10, 30, 20};

		bool WhiteKingAlive = false;
		bool BlackKingAlive = false;
		int Score = 0;
		for(int y = 0; y < 8; ++y)
		{
			for(int x = 0; x < 8; ++x)
			{
				const char Piece = Board[y][x];
				if(Piece == '.')
					continue;
				const bool White = IsWhitePiece(Piece);
				const char Upper = (char)toupper((unsigned char)Piece);
				if(Upper == 'K')
				{
					if(White)
						WhiteKingAlive = true;
					else
						BlackKingAlive = true;
				}
				const int Material = PieceValue(Piece);
				const int Sq = White ? (y * 8 + x) : ((7 - y) * 8 + x);
				int Pst = 0;
				switch(Upper)
				{
				case 'P': Pst = s_aPstP[Sq]; break;
				case 'N': Pst = s_aPstN[Sq]; break;
				case 'B': Pst = s_aPstB[Sq]; break;
				case 'R': Pst = s_aPstR[Sq]; break;
				case 'Q': Pst = s_aPstQ[Sq]; break;
				case 'K': Pst = s_aPstKMid[Sq]; break;
				default: break;
				}
				int Local = Material + Pst;
				if(Upper == 'P')
					Local += White ? (6 - y) * 4 : (y - 1) * 4;
				Score += White ? -Local : Local;
			}
		}

		if(!BlackKingAlive)
			return -1000000;
		if(!WhiteKingAlive)
			return 1000000;
		return Score;
	};

	auto IsValidMove = [&](int FromX, int FromY, int ToX, int ToY) {
		return IsValidMoveInState(s_Chess.m_Pos, FromX, FromY, ToX, ToY);
	};

	auto ResolveEffectiveTarget = [&](int FromX, int FromY, int RawToX, int RawToY, int &OutToX, int &OutToY) {
		OutToX = RawToX;
		OutToY = RawToY;
		if(FromX < 0 || FromY < 0 || RawToX < 0 || RawToY < 0)
			return;
		const char MovingPiece = s_Chess.m_Pos.m_Board[FromY][FromX];
		const char TargetPiece = s_Chess.m_Pos.m_Board[RawToY][RawToX];
		if((char)toupper((unsigned char)MovingPiece) == 'K' && (char)toupper((unsigned char)TargetPiece) == 'R' &&
			IsWhitePiece(MovingPiece) == IsWhitePiece(TargetPiece) && FromX == 4 && FromY == RawToY && (RawToX == 0 || RawToX == 7))
		{
			const int CastleToX = (RawToX == 7) ? 6 : 2;
			if(IsValidMove(FromX, FromY, CastleToX, FromY))
			{
				OutToX = CastleToX;
				OutToY = FromY;
			}
		}
	};

	auto OrderMovesForSearch = [&](const TChessBoard &Board, std::vector<SChessMove> &vMoves) {
		std::sort(vMoves.begin(), vMoves.end(), [&](const SChessMove &A, const SChessMove &B) {
			const char MovA = Board[A.m_FromY][A.m_FromX];
			const char MovB = Board[B.m_FromY][B.m_FromX];
			const char CapA = Board[A.m_ToY][A.m_ToX];
			const char CapB = Board[B.m_ToY][B.m_ToX];
			int ScoreA = PieceValue(CapA) * 16 - PieceValue(MovA) / 10;
			int ScoreB = PieceValue(CapB) * 16 - PieceValue(MovB) / 10;
			if((char)toupper((unsigned char)MovA) == 'P' && (A.m_ToY == 0 || A.m_ToY == 7))
				ScoreA += 8000;
			if((char)toupper((unsigned char)MovB) == 'P' && (B.m_ToY == 0 || B.m_ToY == 7))
				ScoreB += 8000;
			if((char)toupper((unsigned char)MovA) == 'K' && abs(A.m_ToX - A.m_FromX) == 2)
				ScoreA += 400;
			if((char)toupper((unsigned char)MovB) == 'K' && abs(B.m_ToX - B.m_FromX) == 2)
				ScoreB += 400;
			return ScoreA > ScoreB;
		});
	};

	auto PickBotMove = [&](SChessMove &OutMove, bool BotIsWhite) {
		const SChessPosState RootPos = s_Chess.m_Pos;
		std::vector<SChessMove> vRootMoves = CollectLegalMovesInState(RootPos, BotIsWhite);
		if(vRootMoves.empty())
			return false;

		if(s_Chess.m_Difficulty == 0 && (rand() % 100) < 30)
		{
			OutMove = vRootMoves[rand() % vRootMoves.size()];
			return true;
		}

		OrderMovesForSearch(RootPos.m_Board, vRootMoves);

		constexpr int INF = 10000000;
		int SearchDepth = 3;
		int RandomMargin = 8;
		if(s_Chess.m_Difficulty == 0)
		{
			SearchDepth = 1;
			RandomMargin = 65;
		}
		else if(s_Chess.m_Difficulty == 1)
		{
			SearchDepth = 2;
			RandomMargin = 25;
		}
		else if(s_Chess.m_Difficulty == 2)
		{
			SearchDepth = 3;
			RandomMargin = 8;
		}
		else
		{
			SearchDepth = 4;
			RandomMargin = 0;
		}

		const int Perspective = BotIsWhite ? -1 : 1;

		std::function<int(const SChessPosState &, int, int, int, bool)> Minimax =
			[&](const SChessPosState &Pos, int Depth, int Alpha, int Beta, bool MaximizingBot) -> int {
			if(Depth <= 0)
				return EvaluateBoard(Pos.m_Board) * Perspective;

			const bool SideWhite = MaximizingBot ? BotIsWhite : !BotIsWhite;
			std::vector<SChessMove> vMoves = CollectLegalMovesInState(Pos, SideWhite);
			if(vMoves.empty())
			{
				if(IsCheckOnBoard(Pos.m_Board, SideWhite))
					return MaximizingBot ? (-INF + (SearchDepth - Depth)) : (INF - (SearchDepth - Depth));
				return 0;
			}

			OrderMovesForSearch(Pos.m_Board, vMoves);

			if(MaximizingBot)
			{
				int Best = -INF;
				for(const SChessMove &Move : vMoves)
				{
					SChessPosState Next = Pos;
					ApplyMoveInState(Next, Move);
					Best = std::max(Best, Minimax(Next, Depth - 1, Alpha, Beta, false));
					Alpha = std::max(Alpha, Best);
					if(Beta <= Alpha)
						break;
				}
				return Best;
			}

			int Best = INF;
			for(const SChessMove &Move : vMoves)
			{
				SChessPosState Next = Pos;
				ApplyMoveInState(Next, Move);
				Best = std::min(Best, Minimax(Next, Depth - 1, Alpha, Beta, true));
				Beta = std::min(Beta, Best);
				if(Beta <= Alpha)
					break;
			}
			return Best;
		};

		int BestScore = -INF;
		std::vector<SChessMove> vBestMoves;
		for(const SChessMove &Move : vRootMoves)
		{
			SChessPosState AfterBot = RootPos;
			const char Captured = ApplyMoveInState(AfterBot, Move);
			int Score;
			if((char)toupper((unsigned char)Captured) == 'K')
				Score = INF;
			else
				Score = Minimax(AfterBot, SearchDepth - 1, -INF, INF, false);

			if(Score > BestScore + RandomMargin)
			{
				BestScore = Score;
				vBestMoves.clear();
				vBestMoves.push_back(Move);
			}
			else if(abs(Score - BestScore) <= RandomMargin)
			{
				vBestMoves.push_back(Move);
			}
		}

		if(vBestMoves.empty())
			return false;
		OutMove = vBestMoves[rand() % vBestMoves.size()];
		return true;
	};

	auto FormatMoveNotation = [&](const SChessPosState &BeforePos, const SChessMove &Move, char Captured, bool IsMate, bool IsCheck) {
		const char MovingPiece = BeforePos.m_Board[Move.m_FromY][Move.m_FromX];
		const char Upper = (char)toupper((unsigned char)MovingPiece);
		std::string Note;
		if(Upper == 'K' && abs(Move.m_ToX - Move.m_FromX) == 2)
		{
			Note = (Move.m_ToX > Move.m_FromX) ? "O-O" : "O-O-O";
		}
		else
		{
			if(Upper != 'P')
				Note += Upper;
			else if(Captured != '.')
				Note += (char)('a' + Move.m_FromX);
			if(Captured != '.')
				Note += 'x';
			Note += (char)('a' + Move.m_ToX);
			Note += (char)('8' - Move.m_ToY);
			if(Upper == 'P' && (Move.m_ToY == 0 || Move.m_ToY == 7))
			{
				const char PromoUpper = (Move.m_Promo == 'R' || Move.m_Promo == 'B' || Move.m_Promo == 'N') ? Move.m_Promo : 'Q';
				Note += '=';
				Note += PromoUpper;
			}
		}
		if(IsMate)
			Note += '#';
		else if(IsCheck)
			Note += '+';
		return Note;
	};

	auto ApplyChessMove = [&](const SChessMove &Move) {
		SChessSnapshot Snap;
		Snap.m_Pos = s_Chess.m_Pos;
		Snap.m_HasLastMove = s_Chess.m_HasLastMove;
		Snap.m_LastFromX = s_Chess.m_LastFromX;
		Snap.m_LastFromY = s_Chess.m_LastFromY;
		Snap.m_LastToX = s_Chess.m_LastToX;
		Snap.m_LastToY = s_Chess.m_LastToY;
		Snap.m_vCapturedByWhite = s_Chess.m_vCapturedByWhite;
		Snap.m_vCapturedByBlack = s_Chess.m_vCapturedByBlack;

		const char MovingPiece = s_Chess.m_Pos.m_Board[Move.m_FromY][Move.m_FromX];
		const char Upper = (char)toupper((unsigned char)MovingPiece);
		const bool WhiteMoved = IsWhitePiece(MovingPiece);
		char CapturedPiece = '.';
		ApplyMoveInState(s_Chess.m_Pos, Move, &CapturedPiece);

		if(CapturedPiece != '.')
		{
			if(WhiteMoved)
				s_Chess.m_vCapturedByWhite.push_back(CapturedPiece);
			else
				s_Chess.m_vCapturedByBlack.push_back(CapturedPiece);
		}

		s_Chess.m_HasLastMove = true;
		s_Chess.m_LastFromX = Move.m_FromX;
		s_Chess.m_LastFromY = Move.m_FromY;
		s_Chess.m_LastToX = Move.m_ToX;
		s_Chess.m_LastToY = Move.m_ToY;

		s_Chess.m_HasMoveAnim = true;
		s_Chess.m_AnimPiece = s_Chess.m_Pos.m_Board[Move.m_ToY][Move.m_ToX];
		s_Chess.m_AnimFromX = Move.m_FromX;
		s_Chess.m_AnimFromY = Move.m_FromY;
		s_Chess.m_AnimToX = Move.m_ToX;
		s_Chess.m_AnimToY = Move.m_ToY;
		s_Chess.m_AnimStart = AnimTime;

		if(Upper == 'K' && abs(Move.m_ToX - Move.m_FromX) == 2)
		{
			const bool KingSide = Move.m_ToX > Move.m_FromX;
			s_Chess.m_HasRookAnim = true;
			s_Chess.m_AnimRookPiece = WhiteMoved ? 'R' : 'r';
			s_Chess.m_AnimRookFromX = KingSide ? 7 : 0;
			s_Chess.m_AnimRookToX = KingSide ? 5 : 3;
			s_Chess.m_AnimRookY = Move.m_FromY;
		}
		else
		{
			s_Chess.m_HasRookAnim = false;
		}

		if(CapturedPiece != '.' || Upper == 'P')
			s_Chess.m_RepetitionTable.clear();

		const uint64_t Key = MakeStateKey(s_Chess.m_Pos);
		s_Chess.m_RepetitionTable[Key]++;
		s_Chess.m_BotReadyTime = AnimTime + 0.18f;

		const bool OpponentInCheck = IsCheckOnBoard(s_Chess.m_Pos.m_Board, s_Chess.m_Pos.m_WhiteTurn);
		const bool OpponentHasMove = HasAnyLegalMoveInState(s_Chess.m_Pos, s_Chess.m_Pos.m_WhiteTurn);
		const bool IsCheckmate = OpponentInCheck && !OpponentHasMove;
		const bool IsStalemate = !OpponentInCheck && !OpponentHasMove;

		s_Chess.m_vMoveNotation.push_back(FormatMoveNotation(Snap.m_Pos, Move, CapturedPiece, IsCheckmate, OpponentInCheck));
		s_Chess.m_vHistory.push_back(std::move(Snap));

		if(IsCheckmate)
		{
			s_Chess.m_GameOver = true;
			s_Chess.m_WhiteWon = !s_Chess.m_Pos.m_WhiteTurn;
			s_Chess.m_Stalemate = false;
		}
		else if(IsStalemate)
		{
			s_Chess.m_GameOver = true;
			s_Chess.m_WhiteWon = false;
			s_Chess.m_Stalemate = true;
		}
		else if(s_Chess.m_Pos.m_HalfMoveClock >= 100)
		{
			s_Chess.m_GameOver = true;
			s_Chess.m_WhiteWon = false;
			s_Chess.m_FiftyMove = true;
		}
		else if(s_Chess.m_RepetitionTable[Key] >= 3)
		{
			s_Chess.m_GameOver = true;
			s_Chess.m_WhiteWon = false;
			s_Chess.m_Threefold = true;
		}
	};

	auto UndoMove = [&]() {
		if(s_Chess.m_vHistory.empty() || s_Chess.m_PendingPromotion)
			return;
		auto PopOne = [&]() {
			if(s_Chess.m_vHistory.empty())
				return;
			const uint64_t CurKey = MakeStateKey(s_Chess.m_Pos);
			auto It = s_Chess.m_RepetitionTable.find(CurKey);
			if(It != s_Chess.m_RepetitionTable.end() && It->second > 0)
				It->second--;

			const SChessSnapshot &Snap = s_Chess.m_vHistory.back();
			s_Chess.m_Pos = Snap.m_Pos;
			s_Chess.m_HasLastMove = Snap.m_HasLastMove;
			s_Chess.m_LastFromX = Snap.m_LastFromX;
			s_Chess.m_LastFromY = Snap.m_LastFromY;
			s_Chess.m_LastToX = Snap.m_LastToX;
			s_Chess.m_LastToY = Snap.m_LastToY;
			s_Chess.m_vCapturedByWhite = Snap.m_vCapturedByWhite;
			s_Chess.m_vCapturedByBlack = Snap.m_vCapturedByBlack;
			s_Chess.m_vHistory.pop_back();
			if(!s_Chess.m_vMoveNotation.empty())
				s_Chess.m_vMoveNotation.pop_back();
		};

		PopOne();
		if((s_Chess.m_Difficulty < 4 || s_Chess.m_Difficulty == 6) && s_Chess.m_Pos.m_WhiteTurn != s_Chess.m_PlayerIsWhite && !s_Chess.m_vHistory.empty())
			PopOne();

		s_Chess.m_GameOver = false;
		s_Chess.m_WhiteWon = false;
		s_Chess.m_CachedCheckPly = -1;
		s_Chess.m_Stalemate = false;
		s_Chess.m_FiftyMove = false;
		s_Chess.m_Threefold = false;
		s_Chess.m_Resigned = false;
		s_Chess.m_GameLogged = false;
		s_Chess.m_OpeningStep = std::min(s_Chess.m_OpeningStep, (int)s_Chess.m_vMoveNotation.size());
		s_Chess.m_OpeningRevealHint = false;
		s_Chess.m_aOpeningFeedback[0] = '\0';
		s_Chess.m_SelectedX = -1;
		s_Chess.m_SelectedY = -1;
		s_Chess.m_Dragging = false;
		s_Chess.m_HasMoveAnim = false;
		s_Chess.m_HasRookAnim = false;
		s_Chess.m_BotReadyTime = AnimTime + 0.25f;
	};

	auto SaveChessGameLog = [&](bool ManualTrigger) {
		if(s_Chess.m_vMoveNotation.empty())
		{
			if(ManualTrigger)
				GameClient()->m_Chat.Echo("Ajedrez: No hay jugadas para guardar.");
			return;
		}
		char aTimestamp[32];
		str_timestamp_format(aTimestamp, sizeof(aTimestamp), "%Y-%m-%d %H:%M:%S");

		const char *pResult = "*";
		if(s_Chess.m_GameOver)
		{
			if(s_Chess.m_Stalemate || s_Chess.m_FiftyMove || s_Chess.m_Threefold)
				pResult = "1/2-1/2";
			else
				pResult = s_Chess.m_WhiteWon ? "1-0" : "0-1";
		}

		static const char *const s_apBotNames[4] = {"Bot Facil", "Bot Medio", "Bot Dificil", "Bot Experto"};
		const char *pModeName = "Ajedrez";
		char aWhite[64], aBlack[64];
		if(s_Chess.m_Difficulty >= 0 && s_Chess.m_Difficulty <= 3)
		{
			pModeName = s_apBotNames[s_Chess.m_Difficulty];
			str_copy(aWhite, s_Chess.m_PlayerIsWhite ? "Jugador" : pModeName);
			str_copy(aBlack, s_Chess.m_PlayerIsWhite ? pModeName : "Jugador");
		}
		else if(s_Chess.m_Difficulty == 4)
		{
			pModeName = "2P Local";
			str_copy(aWhite, "Jugador 1 (Blancas)");
			str_copy(aBlack, "Jugador 2 (Negras)");
		}
		else if(s_Chess.m_Difficulty == 5)
		{
			pModeName = "1v1 Servidor DDNet";
			str_copy(aWhite, s_Chess.m_PlayerIsWhite ? "Tu" : gs_ChessOnline.m_aOpponentName);
			str_copy(aBlack, s_Chess.m_PlayerIsWhite ? gs_ChessOnline.m_aOpponentName : "Tu");
		}
		else
		{
			pModeName = gs_aChessOpenings[std::clamp(s_Chess.m_OpeningIndex, 0, 9)].m_pName;
			str_copy(aWhite, "Blancas");
			str_copy(aBlack, "Negras");
		}

		std::string LogText;
		LogText.reserve(512);
		char aHdr[320];
		str_format(aHdr, sizeof(aHdr),
			"[Event \"AClient Chess (%s)\"]\n[Date \"%s\"]\n[White \"%s\"]\n[Black \"%s\"]\n[Result \"%s\"]\n\n",
			pModeName, aTimestamp, aWhite, aBlack, pResult);
		LogText += aHdr;

		for(size_t i = 0; i < s_Chess.m_vMoveNotation.size(); i += 2)
		{
			char aTurn[40];
			const int TurnNum = (int)(i / 2) + 1;
			if(i + 1 < s_Chess.m_vMoveNotation.size())
				str_format(aTurn, sizeof(aTurn), "%d. %s %s ", TurnNum, s_Chess.m_vMoveNotation[i].c_str(), s_Chess.m_vMoveNotation[i + 1].c_str());
			else
				str_format(aTurn, sizeof(aTurn), "%d. %s ", TurnNum, s_Chess.m_vMoveNotation[i].c_str());
			LogText += aTurn;
		}
		LogText += pResult;
		LogText += "\n\n";

		char aWholePath[IO_MAX_PATH_LENGTH] = "";
		IOHANDLE File = Storage()->OpenFile("chess_games.log", IOFLAG_APPEND, IStorage::TYPE_SAVE, aWholePath, sizeof(aWholePath));
		if(File)
		{
			io_write(File, LogText.c_str(), (unsigned)LogText.size());
			io_close(File);
			s_Chess.m_GameLogged = true;
		}

		if(ManualTrigger)
		{
			Input()->SetClipboardText(LogText.c_str());
			char aEcho[IO_MAX_PATH_LENGTH + 64];
			if(File)
				str_format(aEcho, sizeof(aEcho), "Ajedrez: Partida guardada en %s (y copiada al portapapeles)", aWholePath);
			else
				str_copy(aEcho, "Ajedrez: Partida copiada al portapapeles.");
			GameClient()->m_Chat.Echo(aEcho);
		}
	};

	if(!s_Chess.m_Initialized)
		ResetChess();

	if(gs_ChessOnline.m_InOnlineMatch)
	{
		const bool OpponentGone = (Client()->State() != IClient::STATE_ONLINE) ||
			gs_ChessOnline.m_OpponentClientId < 0 || gs_ChessOnline.m_OpponentClientId >= MAX_CLIENTS ||
			!GameClient()->m_aClients[gs_ChessOnline.m_OpponentClientId].m_Active;
		if(OpponentGone && !s_Chess.m_GameOver)
		{
			s_Chess.m_GameOver = true;
			s_Chess.m_WhiteWon = gs_ChessOnline.m_LocalIsWhite;
			s_Chess.m_Resigned = true;
			gs_ChessOnline.m_InOnlineMatch = false;
		}
	}
	if(gs_ChessOnline.m_PendingStartOnlineMatch)
	{
		gs_ChessOnline.m_PendingStartOnlineMatch = false;
		s_Chess.m_Difficulty = 5;
		ResetChess();
		s_Chess.m_PlayerIsWhite = gs_ChessOnline.m_LocalIsWhite;
	}
	if(gs_ChessOnline.m_OpponentResigned)
	{
		gs_ChessOnline.m_OpponentResigned = false;
		if(!s_Chess.m_GameOver)
		{
			s_Chess.m_GameOver = true;
			s_Chess.m_WhiteWon = gs_ChessOnline.m_LocalIsWhite;
			s_Chess.m_Resigned = true;
		}
		gs_ChessOnline.m_InOnlineMatch = false;
	}

	if(s_Chess.m_GameOver && !s_Chess.m_GameLogged && !s_Chess.m_vMoveNotation.empty() && s_Chess.m_Difficulty != 6)
		SaveChessGameLog(false);

	// Layout: Top status bar + Board on left + Settings/Info sidebar on right
	CUIRect TopBarChess, ContentArea, BoardArea, SidePanel;
	MainView.HSplitTop(LineSize * 1.2f, &TopBarChess, &ContentArea);
	ContentArea.HSplitTop(MarginSmall, nullptr, &ContentArea);
	ContentArea.VSplitRight(235.0f, &BoardArea, &SidePanel);
	BoardArea.VSplitRight(MarginSmall * 2.0f, &BoardArea, nullptr);

	const SChessOpening &CurOpening = gs_aChessOpenings[std::clamp(s_Chess.m_OpeningIndex, 0, 9)];

	// Status bar
	const int CurCheckPly = (int)s_Chess.m_vMoveNotation.size();
	if(s_Chess.m_CachedCheckPly != CurCheckPly)
	{
		s_Chess.m_CachedCheckPly = CurCheckPly;
		s_Chess.m_CachedCheckKingX = -1;
		s_Chess.m_CachedCheckKingY = -1;
		s_Chess.m_CachedSideInCheck = IsCheckOnBoard(s_Chess.m_Pos.m_Board, s_Chess.m_Pos.m_WhiteTurn);
		if(s_Chess.m_CachedSideInCheck)
			FindKingPos(s_Chess.m_Pos.m_Board, s_Chess.m_Pos.m_WhiteTurn, s_Chess.m_CachedCheckKingX, s_Chess.m_CachedCheckKingY);
	}
	const bool SideInCheck = !s_Chess.m_GameOver && s_Chess.m_CachedSideInCheck;
	char aStatusBuf[128];
	if(s_Chess.m_GameOver)
	{
		if(s_Chess.m_Resigned)
			str_copy(aStatusBuf, (s_Chess.m_WhiteWon == s_Chess.m_PlayerIsWhite) ? Localize("¡Victoria por abandono del rival!") : Localize("Derrota por abandono"));
		else if(s_Chess.m_Stalemate)
			str_copy(aStatusBuf, Localize("Tablas - Ahogado"));
		else if(s_Chess.m_FiftyMove)
			str_copy(aStatusBuf, Localize("Tablas - Regla 50 jugadas"));
		else if(s_Chess.m_Threefold)
			str_copy(aStatusBuf, Localize("Tablas - Triple repetición"));
		else
			str_copy(aStatusBuf, s_Chess.m_WhiteWon ? Localize("Jaque Mate - Ganan Blancas") : Localize("Jaque Mate - Ganan Negras"));
	}
	else if(s_Chess.m_Difficulty == 6)
	{
		if(s_Chess.m_OpeningStep >= CurOpening.m_Count)
			str_format(aStatusBuf, sizeof(aStatusBuf), "¡Apertura completada! (%s)", CurOpening.m_pName);
		else
			str_format(aStatusBuf, sizeof(aStatusBuf), "%s (%d/%d)", CurOpening.m_pName, s_Chess.m_OpeningStep, CurOpening.m_Count);
	}
	else if(s_Chess.m_Difficulty == 5 && gs_ChessOnline.m_InOnlineMatch)
	{
		const bool MyTurn = (s_Chess.m_Pos.m_WhiteTurn == s_Chess.m_PlayerIsWhite);
		str_format(aStatusBuf, sizeof(aStatusBuf), "%s vs %s%s",
			MyTurn ? Localize("Tu turno") : Localize("Turno rival"),
			gs_ChessOnline.m_aOpponentName,
			SideInCheck ? " (¡JAQUE!)" : "");
	}
	else if(s_Chess.m_Difficulty == 5)
	{
		str_copy(aStatusBuf, Localize("1v1 Servidor - Elige o espera rival"));
	}
	else if(SideInCheck)
	{
		str_copy(aStatusBuf, s_Chess.m_Pos.m_WhiteTurn ? Localize("Turno: Blancas (¡JAQUE!)") : Localize("Turno: Negras (¡JAQUE!)"));
	}
	else
	{
		str_copy(aStatusBuf, s_Chess.m_Pos.m_WhiteTurn ? Localize("Turno: Blancas") : Localize("Turno: Negras"));
	}
	const char *pStatus = aStatusBuf;

	CUIRect TurnLabel, TopActions, UndoBtn, FlipBtn, RestartBtn;
	TopBarChess.VSplitRight(285.0f, &TurnLabel, &TopActions);
	TopActions.VSplitLeft(92.0f, &UndoBtn, &TopActions);
	TopActions.VSplitLeft(MarginSmall, nullptr, &TopActions);
	TopActions.VSplitLeft(86.0f, &FlipBtn, &TopActions);
	TopActions.VSplitLeft(MarginSmall, nullptr, &TopActions);
	RestartBtn = TopActions;

	TextRender()->TextColor(SideInCheck ? ColorRGBA(1.0f, 0.42f, 0.38f, 1.0f) : TextRender()->DefaultTextColor());
	Ui()->DoLabel(&TurnLabel, pStatus, FontSize, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	static CButtonContainer s_UndoBtn, s_FlipBtn, s_RestartBtn;
	if(DoButton_Menu(&s_UndoBtn, Localize("Deshacer"), gs_ChessOnline.m_InOnlineMatch ? 1 : 0, &UndoBtn) && !gs_ChessOnline.m_InOnlineMatch)
		UndoMove();
	if(DoButton_Menu(&s_FlipBtn, Localize("Girar"), 0, &FlipBtn))
		s_Chess.m_ManualFlip = !s_Chess.m_ManualFlip;
	if(gs_ChessOnline.m_InOnlineMatch)
	{
		if(DoButton_Menu(&s_RestartBtn, Localize("Rendirse"), 0, &RestartBtn))
		{
			SendChessWhisper(GameClient(), gs_ChessOnline.m_OpponentClientId, gs_ChessOnline.m_aOpponentName, "RES");
			gs_ChessOnline.m_InOnlineMatch = false;
			s_Chess.m_GameOver = true;
			s_Chess.m_Resigned = true;
			s_Chess.m_WhiteWon = !s_Chess.m_PlayerIsWhite;
		}
	}
	else
	{
		if(DoButton_Menu(&s_RestartBtn, Localize("Reiniciar"), 0, &RestartBtn))
			ResetChess();
	}

	// Sidebar controls
	SidePanel.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 5.0f);
	SidePanel.Margin(6.0f, &SidePanel);

	// Incoming 1v1 challenge banner
	if(gs_ChessOnline.m_IncomingInviteClientId >= 0)
	{
		CUIRect InviteBox, InviteLabel, InviteBtns, AcceptBtn, DeclineBtn;
		SidePanel.HSplitTop(42.0f, &InviteBox, &SidePanel);
		SidePanel.HSplitTop(4.0f, nullptr, &SidePanel);
		InviteBox.Draw(ColorRGBA(0.18f, 0.36f, 0.20f, 0.85f), IGraphics::CORNER_ALL, 4.0f);
		InviteBox.Margin(4.0f, &InviteBox);
		InviteBox.HSplitTop(15.0f, &InviteLabel, &InviteBtns);
		char aInv[64];
		str_format(aInv, sizeof(aInv), "Reto de %s (%s)", gs_ChessOnline.m_aIncomingInviteName,
			gs_ChessOnline.m_IncomingLocalIsWhite ? "Blancas" : "Negras");
		Ui()->DoLabel(&InviteLabel, aInv, 10.5f, TEXTALIGN_ML);
		InviteBtns.VSplitMid(&AcceptBtn, &DeclineBtn, 4.0f);
		static CButtonContainer s_AcceptInvBtn, s_DeclineInvBtn;
		if(DoButton_Menu(&s_AcceptInvBtn, Localize("Aceptar"), 0, &AcceptBtn))
		{
			SendChessWhisper(GameClient(), gs_ChessOnline.m_IncomingInviteClientId, gs_ChessOnline.m_aIncomingInviteName, "ACC");
			gs_ChessOnline.m_InOnlineMatch = true;
			gs_ChessOnline.m_PendingStartOnlineMatch = true;
			gs_ChessOnline.m_OpponentClientId = gs_ChessOnline.m_IncomingInviteClientId;
			str_copy(gs_ChessOnline.m_aOpponentName, gs_ChessOnline.m_aIncomingInviteName);
			gs_ChessOnline.m_LocalIsWhite = gs_ChessOnline.m_IncomingLocalIsWhite;
			gs_ChessOnline.m_OpponentResigned = false;
			gs_ChessOnline.m_WaitingInviteTargetId = -1;
			gs_ChessOnline.m_IncomingInviteClientId = -1;
			gs_ChessOnline.ClearPendingMoves();
		}
		if(DoButton_Menu(&s_DeclineInvBtn, Localize("Rechazar"), 0, &DeclineBtn))
		{
			SendChessWhisper(GameClient(), gs_ChessOnline.m_IncomingInviteClientId, gs_ChessOnline.m_aIncomingInviteName, "DEC");
			gs_ChessOnline.m_IncomingInviteClientId = -1;
		}
	}

	auto DrawSegmentedRow = [&](const char *pTitle, int Count, const char *const *ppLabels, int &Selected, CButtonContainer *pBtns, bool ResetOnSelect) {
		CUIRect TitleRect, RowRect;
		SidePanel.HSplitTop(15.0f, &TitleRect, &SidePanel);
		Ui()->DoLabel(&TitleRect, pTitle, 11.0f, TEXTALIGN_ML);
		SidePanel.HSplitTop(20.0f, &RowRect, &SidePanel);
		SidePanel.HSplitTop(5.0f, nullptr, &SidePanel);
		const float Gap = 3.0f;
		const float BtnW = (RowRect.w - Gap * (Count - 1)) / (float)Count;
		for(int i = 0; i < Count; ++i)
		{
			CUIRect Btn;
			RowRect.VSplitLeft(BtnW, &Btn, &RowRect);
			if(i + 1 < Count)
				RowRect.VSplitLeft(Gap, nullptr, &RowRect);
			const int Corners = (i == 0) ? IGraphics::CORNER_L : (i == Count - 1 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
			if(DoButton_MenuTab(&pBtns[i], ppLabels[i], Selected == i, &Btn, Corners, nullptr, nullptr, nullptr, nullptr, 3.0f))
			{
				if(ResetOnSelect && (gs_ChessOnline.m_InOnlineMatch || s_Chess.m_Difficulty == 6))
					continue;
				if(Selected != i || (ResetOnSelect && i == 1))
				{
					Selected = i;
					if(ResetOnSelect)
						ResetChess();
				}
			}
		}
	};

	static CButtonContainer s_aColorBtns[3];
	const char *apColorLabels[3] = {Localize("Blancas"), Localize("Random"), Localize("Negras")};
	DrawSegmentedRow(Localize("Tu color"), 3, apColorLabels, s_Chess.m_ColorChoice, s_aColorBtns, true);

	{
		CUIRect TitleRect, Row1, Row2, LocalBtn, OnlineBtn, OpenBtn;
		SidePanel.HSplitTop(15.0f, &TitleRect, &SidePanel);
		Ui()->DoLabel(&TitleRect, Localize("Nivel / Modo"), 11.0f, TEXTALIGN_ML);
		SidePanel.HSplitTop(20.0f, &Row1, &SidePanel);
		SidePanel.HSplitTop(3.0f, nullptr, &SidePanel);
		SidePanel.HSplitTop(20.0f, &Row2, &SidePanel);
		SidePanel.HSplitTop(5.0f, nullptr, &SidePanel);
		static CButtonContainer s_aDiffBtns[7];
		const char *apDiffLabels[4] = {Localize("Fácil"), Localize("Medio"), Localize("Difícil"), Localize("Experto")};
		const float Gap = 3.0f;
		const float BtnW = (Row1.w - Gap * 3) / 4.0f;
		for(int i = 0; i < 4; ++i)
		{
			CUIRect Btn;
			Row1.VSplitLeft(BtnW, &Btn, &Row1);
			if(i < 3)
				Row1.VSplitLeft(Gap, nullptr, &Row1);
			const int Corners = (i == 0) ? IGraphics::CORNER_L : (i == 3 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
			if(DoButton_MenuTab(&s_aDiffBtns[i], apDiffLabels[i], s_Chess.m_Difficulty == i, &Btn, Corners, nullptr, nullptr, nullptr, nullptr, 3.0f) && !gs_ChessOnline.m_InOnlineMatch)
			{
				if(s_Chess.m_Difficulty == 6)
				{
					s_Chess.m_Difficulty = i;
					ResetChess();
				}
				else
					s_Chess.m_Difficulty = i;
			}
		}
		const float W2 = (Row2.w - 6.0f) / 3.0f;
		Row2.VSplitLeft(W2, &LocalBtn, &Row2);
		Row2.VSplitLeft(3.0f, nullptr, &Row2);
		Row2.VSplitLeft(W2, &OnlineBtn, &Row2);
		Row2.VSplitLeft(3.0f, nullptr, &OpenBtn);
		if(DoButton_MenuTab(&s_aDiffBtns[4], Localize("2P Local"), s_Chess.m_Difficulty == 4, &LocalBtn, IGraphics::CORNER_L, nullptr, nullptr, nullptr, nullptr, 3.0f) && !gs_ChessOnline.m_InOnlineMatch)
		{
			if(s_Chess.m_Difficulty == 6)
				ResetChess();
			s_Chess.m_Difficulty = 4;
		}
		if(DoButton_MenuTab(&s_aDiffBtns[5], Localize("1v1 Serv."), s_Chess.m_Difficulty == 5, &OnlineBtn, IGraphics::CORNER_NONE, nullptr, nullptr, nullptr, nullptr, 3.0f) && !gs_ChessOnline.m_InOnlineMatch)
		{
			if(s_Chess.m_Difficulty == 6)
				ResetChess();
			s_Chess.m_Difficulty = 5;
		}
		if(DoButton_MenuTab(&s_aDiffBtns[6], Localize("Aperturas"), s_Chess.m_Difficulty == 6, &OpenBtn, IGraphics::CORNER_R, nullptr, nullptr, nullptr, nullptr, 3.0f) && !gs_ChessOnline.m_InOnlineMatch)
		{
			s_Chess.m_Difficulty = 6;
			ResetChess();
			s_Chess.m_PlayerIsWhite = gs_aChessOpenings[std::clamp(s_Chess.m_OpeningIndex, 0, 9)].m_WhitePerspective;
		}
	}

	static CButtonContainer s_aThemeBtns[4];
	const char *apThemeLabels[4] = {Localize("Verde"), Localize("Madera"), Localize("Azul"), Localize("Oscuro")};
	DrawSegmentedRow(Localize("Modelo de Tablero"), 4, apThemeLabels, s_Chess.m_BoardTheme, s_aThemeBtns, false);

	static CButtonContainer s_aPieceBtns[2];
	const char *apPieceLabels[2] = {Localize("Clásico (Foto)"), Localize("Iconos")};
	DrawSegmentedRow(Localize("Modelo de Fichas"), 2, apPieceLabels, s_Chess.m_PieceStyle, s_aPieceBtns, false);

	auto DrawSpriteSingle = [&](const CUIRect &Rect, char Piece, float Alpha) {
		const int Col = GetChessPieceAtlasCol(Piece);
		if(Col < 0)
			return;
		const int Row = IsWhitePiece(Piece) ? 0 : 1;
		Graphics()->QuadsSetSubset(Col / 6.0f, Row / 2.0f, (Col + 1) / 6.0f, (Row + 1) / 2.0f);
		Graphics()->SetColor(1.0f, 1.0f, 1.0f, Alpha);
		const float Pad = Rect.w * 0.04f;
		IGraphics::CQuadItem Quad(Rect.x + Pad, Rect.y + Pad, Rect.w - Pad * 2.0f, Rect.h - Pad * 2.0f);
		Graphics()->QuadsDrawTL(&Quad, 1);
	};

	auto RenderIconFallbackPiece = [&](const CUIRect &Rect, char Piece, float Alpha) {
		const char *pIcon = GetChessPieceIcon(Piece);
		if(!pIcon)
			return;
		const bool White = IsWhitePiece(Piece);
		const ColorRGBA PieceColor = White ? ColorRGBA(0.97f, 0.97f, 0.97f, Alpha) : ColorRGBA(0.08f, 0.08f, 0.08f, Alpha);
		const float SizeScale = ((char)toupper((unsigned char)Piece) == 'P') ? 0.45f : 0.60f;
		RenderIconLabel(Rect, pIcon, Rect.h * SizeScale, TEXTALIGN_MC, &PieceColor);
		if((char)toupper((unsigned char)Piece) == 'K')
		{
			CUIRect Crown = Rect;
			Crown.h *= 0.4f;
			TextRender()->TextColor(ColorRGBA(1.0f, 0.85f, 0.25f, Alpha));
			Ui()->DoLabel(&Crown, "+", Rect.h * 0.34f, TEXTALIGN_MC);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
	};

	const bool UseSpriteAtlas = (s_Chess.m_PieceStyle == 0) && s_ChessPiecesTex.IsValid() && !s_ChessPiecesTex.IsNullTexture();

	if(s_Chess.m_Difficulty == 5 && !gs_ChessOnline.m_InOnlineMatch)
	{
		CUIRect LobbyTitle, ListBox, ActionBar;
		SidePanel.HSplitTop(15.0f, &LobbyTitle, &SidePanel);
		Ui()->DoLabel(&LobbyTitle, Localize("Jugadores en servidor"), 11.0f, TEXTALIGN_ML);
		SidePanel.HSplitBottom(22.0f, &ListBox, &ActionBar);
		ListBox.HSplitBottom(4.0f, &ListBox, nullptr);
		ListBox.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.22f), IGraphics::CORNER_ALL, 4.0f);

		if(Client()->State() != IClient::STATE_ONLINE)
		{
			Ui()->DoLabel(&ListBox, Localize("Conéctate a un servidor"), 11.0f, TEXTALIGN_MC);
		}
		else
		{
			static CScrollRegion s_ChessPlayersScroll;
			static CButtonContainer s_aPlayerRowBtns[MAX_CLIENTS];
			CScrollRegionParams Params;
			Params.m_ScrollUnit = 18.0f;
			Params.m_ScrollbarMargin = 2.0f;
			s_ChessPlayersScroll.Begin(&ListBox, &Params);
			int PlayerCount = 0;
			for(int i = 0; i < MAX_CLIENTS; ++i)
			{
				if(!GameClient()->m_aClients[i].m_Active)
					continue;
				if(i == GameClient()->m_aLocalIds[0] || i == GameClient()->m_aLocalIds[1])
					continue;
				if(gs_ChessOnline.m_SelectedTargetId < 0 || !GameClient()->m_aClients[gs_ChessOnline.m_SelectedTargetId].m_Active)
					gs_ChessOnline.m_SelectedTargetId = i;
				PlayerCount++;
				CUIRect Row;
				ListBox.HSplitTop(17.0f, &Row, &ListBox);
				ListBox.HSplitTop(2.0f, nullptr, &ListBox);
				s_ChessPlayersScroll.AddRect(Row);
				if(!s_ChessPlayersScroll.RectClipped(Row))
				{
					const bool Selected = (gs_ChessOnline.m_SelectedTargetId == i);
					if(DoButton_MenuTab(&s_aPlayerRowBtns[i], GameClient()->m_aClients[i].m_aName, Selected, &Row, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 3.0f))
						gs_ChessOnline.m_SelectedTargetId = i;
				}
			}
			if(PlayerCount == 0)
			{
				CUIRect EmptyRow;
				ListBox.HSplitTop(20.0f, &EmptyRow, &ListBox);
				s_ChessPlayersScroll.AddRect(EmptyRow);
				Ui()->DoLabel(&EmptyRow, Localize("No hay otros jugadores"), 10.5f, TEXTALIGN_MC);
			}
			s_ChessPlayersScroll.End();
		}

		static CButtonContainer s_ChallengeBtn;
		if(gs_ChessOnline.m_WaitingInviteTargetId >= 0)
		{
			char aWait[64];
			str_format(aWait, sizeof(aWait), "Cancelar (%s)", gs_ChessOnline.m_aWaitingInviteName);
			if(DoButton_Menu(&s_ChallengeBtn, aWait, 0, &ActionBar))
				gs_ChessOnline.m_WaitingInviteTargetId = -1;
		}
		else
		{
			const bool ValidTarget = (Client()->State() == IClient::STATE_ONLINE) &&
				gs_ChessOnline.m_SelectedTargetId >= 0 && gs_ChessOnline.m_SelectedTargetId < MAX_CLIENTS &&
				GameClient()->m_aClients[gs_ChessOnline.m_SelectedTargetId].m_Active;
			const int64_t NowInv = time_get();
			if(DoButton_Menu(&s_ChallengeBtn, Localize("Retar a 1v1"), 0, &ActionBar) && ValidTarget && (NowInv - gs_ChessOnline.m_LastInviteSentTime >= (time_freq() * 3) / 2))
			{
				gs_ChessOnline.m_LastInviteSentTime = NowInv;
				const int Id = gs_ChessOnline.m_SelectedTargetId;
				bool LocalW = true;
				if(s_Chess.m_ColorChoice == 2)
					LocalW = false;
				else if(s_Chess.m_ColorChoice == 1)
					LocalW = (rand() % 2) == 0;
				gs_ChessOnline.m_WaitingInviteTargetId = Id;
				str_copy(gs_ChessOnline.m_aWaitingInviteName, GameClient()->m_aClients[Id].m_aName);
				gs_ChessOnline.m_WaitingLocalIsWhite = LocalW;
				SendChessWhisper(GameClient(), Id, gs_ChessOnline.m_aWaitingInviteName, LocalW ? "INV W" : "INV B");
				char aEcho[128];
				str_format(aEcho, sizeof(aEcho), "Ajedrez: Reto enviado a '%s' (%s).", gs_ChessOnline.m_aWaitingInviteName, LocalW ? "Blancas" : "Negras");
				GameClient()->m_Chat.Echo(aEcho);
			}
		}
	}
	else if(s_Chess.m_Difficulty == 6)
	{
		CUIRect CtrlRow, HintBtn, PlayBotBtn, ListTitle, OpenList, NoteBox;
		SidePanel.HSplitTop(20.0f, &CtrlRow, &SidePanel);
		SidePanel.HSplitTop(4.0f, nullptr, &SidePanel);
		CtrlRow.VSplitMid(&HintBtn, &PlayBotBtn, 4.0f);
		static CButtonContainer s_HintBtn, s_ContinueBotBtn;
		if(DoButton_MenuTab(&s_HintBtn, s_Chess.m_OpeningShowHint ? Localize("Pista: ON") : Localize("Pista: OFF"), s_Chess.m_OpeningShowHint, &HintBtn, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 3.0f))
			s_Chess.m_OpeningShowHint = !s_Chess.m_OpeningShowHint;
		const bool Completed = (s_Chess.m_OpeningStep >= CurOpening.m_Count);
		if(DoButton_Menu(&s_ContinueBotBtn, Completed ? Localize("Seguir vs Bot") : Localize("Repetir"), 0, &PlayBotBtn))
		{
			if(Completed)
			{
				s_Chess.m_Difficulty = 2; // Continue from reached opening position vs Medio/Dificil Bot
				s_Chess.m_BotReadyTime = AnimTime + 0.25f;
			}
			else
			{
				ResetChess();
				s_Chess.m_PlayerIsWhite = CurOpening.m_WhitePerspective;
			}
		}

		SidePanel.HSplitTop(15.0f, &ListTitle, &SidePanel);
		Ui()->DoLabel(&ListTitle, Localize("Aperturas populares"), 11.0f, TEXTALIGN_ML);
		SidePanel.HSplitBottom(66.0f, &OpenList, &NoteBox);
		OpenList.HSplitBottom(4.0f, &OpenList, nullptr);
		OpenList.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.22f), IGraphics::CORNER_ALL, 4.0f);

		static CScrollRegion s_OpeningScroll;
		static CButtonContainer s_aOpeningBtns[10];
		CScrollRegionParams Params;
		Params.m_ScrollUnit = 18.0f;
		Params.m_ScrollbarMargin = 2.0f;
		s_OpeningScroll.Begin(&OpenList, &Params);
		for(int i = 0; i < 10; ++i)
		{
			CUIRect Row;
			OpenList.HSplitTop(17.0f, &Row, &OpenList);
			OpenList.HSplitTop(2.0f, nullptr, &OpenList);
			s_OpeningScroll.AddRect(Row);
			if(!s_OpeningScroll.RectClipped(Row))
			{
				if(DoButton_MenuTab(&s_aOpeningBtns[i], gs_aChessOpenings[i].m_pName, s_Chess.m_OpeningIndex == i, &Row, IGraphics::CORNER_ALL, nullptr, nullptr, nullptr, nullptr, 3.0f))
				{
					s_Chess.m_OpeningIndex = i;
					ResetChess();
					s_Chess.m_PlayerIsWhite = gs_aChessOpenings[i].m_WhitePerspective;
				}
			}
		}
		s_OpeningScroll.End();

		NoteBox.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.26f), IGraphics::CORNER_ALL, 4.0f);
		NoteBox.Margin(4.0f, &NoteBox);
		CUIRect NoteTitle, NoteBody;
		NoteBox.HSplitTop(15.0f, &NoteTitle, &NoteBody);
		if(s_Chess.m_aOpeningFeedback[0] != '\0')
		{
			TextRender()->TextColor(ColorRGBA(1.0f, 0.45f, 0.35f, 1.0f));
			Ui()->DoLabel(&NoteTitle, s_Chess.m_aOpeningFeedback, 10.5f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		else if(Completed)
		{
			TextRender()->TextColor(ColorRGBA(0.40f, 0.92f, 0.50f, 1.0f));
			Ui()->DoLabel(&NoteTitle, Localize("¡Apertura completada!"), 10.5f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}
		else
		{
			char aStepHdr[48];
			str_format(aStepHdr, sizeof(aStepHdr), "Paso %d de %d", s_Chess.m_OpeningStep + 1, CurOpening.m_Count);
			Ui()->DoLabel(&NoteTitle, aStepHdr, 10.5f, TEXTALIGN_ML);
		}
		const char *pExplain = Completed ? CurOpening.m_pSummary : CurOpening.m_aSteps[s_Chess.m_OpeningStep].m_pNote;
		SLabelProperties Props;
		Props.m_MaxWidth = NoteBody.w;
		Ui()->DoLabel(&NoteBody, pExplain, 10.0f, TEXTALIGN_TL, Props);
	}
	else
	{
		{
			int WhiteCapVal = 0;
			for(char P : s_Chess.m_vCapturedByWhite)
				WhiteCapVal += PieceValue(P) / 100;
			int BlackCapVal = 0;
			for(char P : s_Chess.m_vCapturedByBlack)
				BlackCapVal += PieceValue(P) / 100;
			const int Adv = WhiteCapVal - BlackCapVal;

			CUIRect CapTitle, CapWhiteRow, CapBlackRow;
			SidePanel.HSplitTop(15.0f, &CapTitle, &SidePanel);
			char aCapHeader[64];
			if(Adv > 0)
				str_format(aCapHeader, sizeof(aCapHeader), "Capturas (Blancas +%d)", Adv);
			else if(Adv < 0)
				str_format(aCapHeader, sizeof(aCapHeader), "Capturas (Negras +%d)", -Adv);
			else
				str_copy(aCapHeader, Localize("Capturas (Igualado)"));
			Ui()->DoLabel(&CapTitle, aCapHeader, 11.0f, TEXTALIGN_ML);

			auto RenderCapturedRow = [&](CUIRect RowRect, const std::vector<char> &vCaps) {
				RowRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.20f), IGraphics::CORNER_ALL, 3.0f);
				const float IconSz = std::min(15.0f, RowRect.h);
				const int MaxShow = std::min((int)vCaps.size(), (int)(RowRect.w / (IconSz * 0.85f)));
				if(UseSpriteAtlas)
				{
					Graphics()->TextureSet(s_ChessPiecesTex);
					Graphics()->QuadsBegin();
					for(int i = 0; i < MaxShow; ++i)
					{
						CUIRect R = {RowRect.x + 2.0f + i * (IconSz * 0.85f), RowRect.y + (RowRect.h - IconSz) * 0.5f, IconSz, IconSz};
						DrawSpriteSingle(R, vCaps[i], 0.95f);
					}
					Graphics()->QuadsEnd();
				}
				else
				{
					for(int i = 0; i < MaxShow; ++i)
					{
						CUIRect R = {RowRect.x + 2.0f + i * (IconSz * 0.85f), RowRect.y + (RowRect.h - IconSz) * 0.5f, IconSz, IconSz};
						RenderIconFallbackPiece(R, vCaps[i], 0.95f);
					}
				}
			};

			SidePanel.HSplitTop(17.0f, &CapWhiteRow, &SidePanel);
			SidePanel.HSplitTop(2.0f, nullptr, &SidePanel);
			RenderCapturedRow(CapWhiteRow, s_Chess.m_vCapturedByWhite);
			SidePanel.HSplitTop(17.0f, &CapBlackRow, &SidePanel);
			SidePanel.HSplitTop(6.0f, nullptr, &SidePanel);
			RenderCapturedRow(CapBlackRow, s_Chess.m_vCapturedByBlack);
		}

		{
			CUIRect HistHeader, HistLabel, SaveLogBtn, HistBox;
			SidePanel.HSplitTop(17.0f, &HistHeader, &SidePanel);
			HistHeader.VSplitRight(76.0f, &HistLabel, &SaveLogBtn);
			Ui()->DoLabel(&HistLabel, Localize("Historial"), 11.0f, TEXTALIGN_ML);
			static CButtonContainer s_SaveLogBtn;
			if(DoButton_Menu(&s_SaveLogBtn, Localize("Guardar Log"), 0, &SaveLogBtn))
				SaveChessGameLog(true);
			SidePanel.HSplitTop(2.0f, nullptr, &SidePanel);
			HistBox = SidePanel;
			HistBox.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.22f), IGraphics::CORNER_ALL, 4.0f);
			HistBox.Margin(4.0f, &HistBox);

			const float RowH = 14.0f;
			const int MaxRows = std::max(1, (int)(HistBox.h / RowH));
			const int TotalTurns = ((int)s_Chess.m_vMoveNotation.size() + 1) / 2;
			const int StartTurn = std::max(0, TotalTurns - MaxRows);
			for(int t = StartTurn; t < TotalTurns; ++t)
			{
				CUIRect Row;
				HistBox.HSplitTop(RowH, &Row, &HistBox);
				const int WIdx = t * 2;
				const int BIdx = t * 2 + 1;
				char aLine[48];
				if(BIdx < (int)s_Chess.m_vMoveNotation.size())
					str_format(aLine, sizeof(aLine), "%d. %-7s %s", t + 1, s_Chess.m_vMoveNotation[WIdx].c_str(), s_Chess.m_vMoveNotation[BIdx].c_str());
				else
					str_format(aLine, sizeof(aLine), "%d. %s", t + 1, s_Chess.m_vMoveNotation[WIdx].c_str());
				Ui()->DoLabel(&Row, aLine, 10.5f, TEXTALIGN_ML);
			}
		}
	}

	const bool FlipBoard = ((s_Chess.m_Difficulty < 4 || s_Chess.m_Difficulty == 5 || s_Chess.m_Difficulty == 6) ? !s_Chess.m_PlayerIsWhite : false) ^ s_Chess.m_ManualFlip;
	auto BoardToDisplayX = [&](int Bx) { return FlipBoard ? (7 - Bx) : Bx; };
	auto BoardToDisplayY = [&](int By) { return FlipBoard ? (7 - By) : By; };
	auto DisplayToBoardX = [&](int Dx) { return FlipBoard ? (7 - Dx) : Dx; };
	auto DisplayToBoardY = [&](int Dy) { return FlipBoard ? (7 - Dy) : Dy; };

	const float BoardSize = std::min(BoardArea.w, BoardArea.h);
	const float CellSize = BoardSize / 8.0f;
	CUIRect Board;
	Board.w = BoardSize;
	Board.h = BoardSize;
	Board.x = BoardArea.x + (BoardArea.w - Board.w) / 2.0f;
	Board.y = BoardArea.y + (BoardArea.h - Board.h) / 2.0f;
	Board.DrawOutline(ColorRGBA(0.0f, 0.0f, 0.0f, 0.55f));

	int HoverX = -1;
	int HoverY = -1;
	if(Ui()->MouseInside(&Board))
	{
		const vec2 Mouse = Ui()->MousePos();
		const int DispX = std::clamp((int)((Mouse.x - Board.x) / CellSize), 0, 7);
		const int DispY = std::clamp((int)((Mouse.y - Board.y) / CellSize), 0, 7);
		HoverX = DisplayToBoardX(DispX);
		HoverY = DisplayToBoardY(DispY);
	}

	if(s_Chess.m_HasMoveAnim && AnimTime - s_Chess.m_AnimStart >= s_Chess.m_AnimDuration)
	{
		s_Chess.m_HasMoveAnim = false;
		s_Chess.m_HasRookAnim = false;
	}

	auto ApplyLocalMoveAndSync = [&](const SChessMove &Move) {
		ApplyChessMove(Move);
		if(s_Chess.m_Difficulty == 5 && gs_ChessOnline.m_InOnlineMatch)
		{
			char aPayload[16];
			str_format(aPayload, sizeof(aPayload), "MOV %d%d%d%d%c", Move.m_FromX, Move.m_FromY, Move.m_ToX, Move.m_ToY, Move.m_Promo ? Move.m_Promo : '0');
			SendChessWhisper(GameClient(), gs_ChessOnline.m_OpponentClientId, gs_ChessOnline.m_aOpponentName, aPayload);
			if(s_Chess.m_GameOver)
				gs_ChessOnline.m_InOnlineMatch = false;
		}
	};

	auto ExecuteHumanMoveOrPromo = [&](const SChessMove &Move) {
		if(s_Chess.m_Difficulty == 6)
		{
			if(s_Chess.m_OpeningStep >= CurOpening.m_Count)
				return;
			const SChessOpeningStep &Step = CurOpening.m_aSteps[s_Chess.m_OpeningStep];
			if(Move.m_FromX != Step.m_FromX || Move.m_FromY != Step.m_FromY || Move.m_ToX != Step.m_ToX || Move.m_ToY != Step.m_ToY)
			{
				s_Chess.m_OpeningRevealHint = true;
				str_format(s_Chess.m_aOpeningFeedback, sizeof(s_Chess.m_aOpeningFeedback), "Incorrecto: juega %c%c -> %c%c",
					'a' + Step.m_FromX, '8' - Step.m_FromY, 'a' + Step.m_ToX, '8' - Step.m_ToY);
				s_Chess.m_SelectedX = -1;
				s_Chess.m_SelectedY = -1;
				return;
			}
			s_Chess.m_aOpeningFeedback[0] = '\0';
			s_Chess.m_OpeningRevealHint = false;
			s_Chess.m_OpeningStep++;
		}
		const char MovingPiece = s_Chess.m_Pos.m_Board[Move.m_FromY][Move.m_FromX];
		if((char)toupper((unsigned char)MovingPiece) == 'P' && (Move.m_ToY == 0 || Move.m_ToY == 7))
		{
			s_Chess.m_PendingPromotion = true;
			s_Chess.m_PromoMove = Move;
			s_Chess.m_SelectedX = -1;
			s_Chess.m_SelectedY = -1;
			return;
		}
		ApplyLocalMoveAndSync(Move);
		s_Chess.m_SelectedX = -1;
		s_Chess.m_SelectedY = -1;
	};

	if(!s_Chess.m_GameOver && !s_Chess.m_PendingPromotion)
	{
		const bool BotTurn = (s_Chess.m_Difficulty < 4) && (s_Chess.m_Pos.m_WhiteTurn != s_Chess.m_PlayerIsWhite);
		const bool OnlineOpponentTurn = (s_Chess.m_Difficulty == 5) && (!gs_ChessOnline.m_InOnlineMatch || s_Chess.m_Pos.m_WhiteTurn != s_Chess.m_PlayerIsWhite);
		const bool OpeningDone = (s_Chess.m_Difficulty == 6) && (s_Chess.m_OpeningStep >= CurOpening.m_Count);
		const bool OpeningAutoTurn = (s_Chess.m_Difficulty == 6) && !OpeningDone && (s_Chess.m_Pos.m_WhiteTurn != s_Chess.m_PlayerIsWhite);
		const bool MoveAnimBusy = s_Chess.m_HasMoveAnim && AnimTime - s_Chess.m_AnimStart < s_Chess.m_AnimDuration;
		if(OpeningAutoTurn && !s_Chess.m_Dragging && !MoveAnimBusy && AnimTime >= s_Chess.m_BotReadyTime)
		{
			const SChessOpeningStep &Step = CurOpening.m_aSteps[s_Chess.m_OpeningStep];
			if(IsValidMove(Step.m_FromX, Step.m_FromY, Step.m_ToX, Step.m_ToY))
			{
				ApplyChessMove({Step.m_FromX, Step.m_FromY, Step.m_ToX, Step.m_ToY, 0});
				s_Chess.m_OpeningStep++;
			}
		}
		else if(s_Chess.m_Difficulty == 5 && gs_ChessOnline.m_InOnlineMatch && OnlineOpponentTurn && gs_ChessOnline.m_PendingCount > 0)
		{
			SChessMove RemoteMove;
			if(gs_ChessOnline.PopPendingMove(RemoteMove))
			{
				const char RemotePiece = s_Chess.m_Pos.m_Board[RemoteMove.m_FromY][RemoteMove.m_FromX];
				if(RemotePiece != '.' && IsWhitePiece(RemotePiece) == s_Chess.m_Pos.m_WhiteTurn &&
					IsValidMove(RemoteMove.m_FromX, RemoteMove.m_FromY, RemoteMove.m_ToX, RemoteMove.m_ToY))
				{
					ApplyChessMove(RemoteMove);
					if(s_Chess.m_GameOver)
						gs_ChessOnline.m_InOnlineMatch = false;
				}
			}
		}
		else if(BotTurn && !s_Chess.m_Dragging && !MoveAnimBusy && AnimTime >= s_Chess.m_BotReadyTime)
		{
			SChessMove BotMove;
			if(!PickBotMove(BotMove, !s_Chess.m_PlayerIsWhite))
			{
				s_Chess.m_GameOver = true;
				if(IsCheckOnBoard(s_Chess.m_Pos.m_Board, s_Chess.m_Pos.m_WhiteTurn))
				{
					s_Chess.m_WhiteWon = !s_Chess.m_Pos.m_WhiteTurn;
					s_Chess.m_Stalemate = false;
				}
				else
				{
					s_Chess.m_WhiteWon = false;
					s_Chess.m_Stalemate = true;
				}
			}
			else
			{
				ApplyChessMove(BotMove);
			}
		}
		else if(!BotTurn && !OnlineOpponentTurn && !OpeningAutoTurn && !OpeningDone)
		{
			if(HoverX >= 0 && HoverY >= 0 && Ui()->MouseButtonClicked(0))
			{
				s_Chess.m_vArrows.clear();
				memset(s_Chess.m_aMarkedSquares, 0, sizeof(s_Chess.m_aMarkedSquares));
				bool HandledClickMove = false;
				if(s_Chess.m_SelectedX >= 0 && s_Chess.m_SelectedY >= 0)
				{
					int EffX = HoverX, EffY = HoverY;
					ResolveEffectiveTarget(s_Chess.m_SelectedX, s_Chess.m_SelectedY, HoverX, HoverY, EffX, EffY);
					if(!(EffX == s_Chess.m_SelectedX && EffY == s_Chess.m_SelectedY) && IsValidMove(s_Chess.m_SelectedX, s_Chess.m_SelectedY, EffX, EffY))
					{
						ExecuteHumanMoveOrPromo({s_Chess.m_SelectedX, s_Chess.m_SelectedY, EffX, EffY, 0});
						HandledClickMove = true;
					}
				}

				if(!HandledClickMove)
				{
					const char ClickedPiece = s_Chess.m_Pos.m_Board[HoverY][HoverX];
					const bool OwnPiece = ClickedPiece != '.' && (s_Chess.m_Pos.m_WhiteTurn ? IsWhitePiece(ClickedPiece) : IsBlackPiece(ClickedPiece));
					if(OwnPiece)
					{
						s_Chess.m_Dragging = true;
						s_Chess.m_DragFromX = HoverX;
						s_Chess.m_DragFromY = HoverY;
						s_Chess.m_DragMouse = Ui()->MousePos();
						s_Chess.m_SelectedX = HoverX;
						s_Chess.m_SelectedY = HoverY;
					}
					else
					{
						s_Chess.m_SelectedX = -1;
						s_Chess.m_SelectedY = -1;
					}
				}
			}

			if(s_Chess.m_Dragging)
			{
				s_Chess.m_DragMouse = Ui()->MousePos();
				if(!Ui()->MouseButton(0))
				{
					const int FromX = s_Chess.m_DragFromX;
					const int FromY = s_Chess.m_DragFromY;
					if(HoverX >= 0 && HoverY >= 0)
					{
						int EffX = HoverX, EffY = HoverY;
						ResolveEffectiveTarget(FromX, FromY, HoverX, HoverY, EffX, EffY);
						if(!(EffX == FromX && EffY == FromY) && IsValidMove(FromX, FromY, EffX, EffY))
						{
							ExecuteHumanMoveOrPromo({FromX, FromY, EffX, EffY, 0});
						}
						else
						{
							const char HoverPiece = s_Chess.m_Pos.m_Board[HoverY][HoverX];
							const bool OwnPiece = HoverPiece != '.' && (s_Chess.m_Pos.m_WhiteTurn ? IsWhitePiece(HoverPiece) : IsBlackPiece(HoverPiece));
							s_Chess.m_SelectedX = OwnPiece ? HoverX : FromX;
							s_Chess.m_SelectedY = OwnPiece ? HoverY : FromY;
						}
					}
					else
					{
						s_Chess.m_SelectedX = FromX;
						s_Chess.m_SelectedY = FromY;
					}
					s_Chess.m_Dragging = false;
					s_Chess.m_DragFromX = -1;
					s_Chess.m_DragFromY = -1;
				}
			}
		}
	}

	static const ColorRGBA s_aLightColors[4] = {
		ColorRGBA(235.0f / 255.0f, 236.0f / 255.0f, 208.0f / 255.0f, 1.0f),
		ColorRGBA(0.93f, 0.86f, 0.74f, 0.92f),
		ColorRGBA(0.88f, 0.92f, 0.96f, 0.96f),
		ColorRGBA(0.76f, 0.77f, 0.80f, 0.96f)};
	static const ColorRGBA s_aDarkColors[4] = {
		ColorRGBA(115.0f / 255.0f, 149.0f / 255.0f, 82.0f / 255.0f, 1.0f),
		ColorRGBA(0.53f, 0.39f, 0.27f, 0.92f),
		ColorRGBA(0.36f, 0.54f, 0.72f, 0.96f),
		ColorRGBA(0.28f, 0.30f, 0.34f, 0.96f)};
	const ColorRGBA LightSquareCol = s_aLightColors[std::clamp(s_Chess.m_BoardTheme, 0, 3)];
	const ColorRGBA DarkSquareCol = s_aDarkColors[std::clamp(s_Chess.m_BoardTheme, 0, 3)];

	const int CheckKingX = SideInCheck ? s_Chess.m_CachedCheckKingX : -1;
	const int CheckKingY = SideInCheck ? s_Chess.m_CachedCheckKingY : -1;

	const bool MoveAnimActive = s_Chess.m_HasMoveAnim && AnimTime - s_Chess.m_AnimStart < s_Chess.m_AnimDuration;

	const int CurPly = (int)s_Chess.m_vMoveNotation.size();
	if(s_Chess.m_CachedSelX != s_Chess.m_SelectedX || s_Chess.m_CachedSelY != s_Chess.m_SelectedY || s_Chess.m_CachedSelPly != CurPly)
	{
		s_Chess.m_CachedSelX = s_Chess.m_SelectedX;
		s_Chess.m_CachedSelY = s_Chess.m_SelectedY;
		s_Chess.m_CachedSelPly = CurPly;
		memset(s_Chess.m_aValidTargets, 0, sizeof(s_Chess.m_aValidTargets));
		if(s_Chess.m_SelectedX >= 0 && s_Chess.m_SelectedY >= 0)
		{
			for(int y = 0; y < 8; ++y)
			{
				for(int x = 0; x < 8; ++x)
				{
					int EffX = x, EffY = y;
					ResolveEffectiveTarget(s_Chess.m_SelectedX, s_Chess.m_SelectedY, x, y, EffX, EffY);
					if(!(EffX == s_Chess.m_SelectedX && EffY == s_Chess.m_SelectedY) && IsValidMove(s_Chess.m_SelectedX, s_Chess.m_SelectedY, EffX, EffY))
						s_Chess.m_aValidTargets[y][x] = true;
				}
			}
		}
	}

	Graphics()->TextureClear();
	Graphics()->QuadsBegin();
	Graphics()->QuadsSetRotation(0.0f);
	for(int dy = 0; dy < 8; ++dy)
	{
		for(int dx = 0; dx < 8; ++dx)
		{
			const int bx = DisplayToBoardX(dx);
			const int by = DisplayToBoardY(dy);
			const float CellX = Board.x + dx * CellSize;
			const float CellY = Board.y + dy * CellSize;
			const IGraphics::CQuadItem Quad(CellX, CellY, CellSize, CellSize);

			const bool LightSquare = ((bx + by) % 2) == 0;
			Graphics()->SetColor(LightSquare ? LightSquareCol : DarkSquareCol);
			Graphics()->QuadsDrawTL(&Quad, 1);

			if(s_Chess.m_HasLastMove && ((bx == s_Chess.m_LastFromX && by == s_Chess.m_LastFromY) || (bx == s_Chess.m_LastToX && by == s_Chess.m_LastToY)))
			{
				Graphics()->SetColor(0.96f, 0.92f, 0.28f, 0.38f);
				Graphics()->QuadsDrawTL(&Quad, 1);
			}

			if(bx == CheckKingX && by == CheckKingY)
			{
				Graphics()->SetColor(0.95f, 0.18f, 0.15f, 0.55f);
				Graphics()->QuadsDrawTL(&Quad, 1);
			}

			if(s_Chess.m_aMarkedSquares[by][bx])
			{
				Graphics()->SetColor(0.92f, 0.30f, 0.24f, 0.55f);
				Graphics()->QuadsDrawTL(&Quad, 1);
			}

			if(bx == s_Chess.m_SelectedX && by == s_Chess.m_SelectedY)
			{
				Graphics()->SetColor(0.30f, 0.80f, 1.0f, 0.35f);
				Graphics()->QuadsDrawTL(&Quad, 1);
			}
			else if(bx == HoverX && by == HoverY && !s_Chess.m_PendingPromotion)
			{
				Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.12f);
				Graphics()->QuadsDrawTL(&Quad, 1);
			}
		}
	}
	Graphics()->QuadsEnd();
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);

	for(int dy = 0; dy < 8; ++dy)
	{
		for(int dx = 0; dx < 8; ++dx)
		{
			const int bx = DisplayToBoardX(dx);
			const int by = DisplayToBoardY(dy);
			CUIRect Cell;
			Cell.x = Board.x + dx * CellSize;
			Cell.y = Board.y + dy * CellSize;
			Cell.w = CellSize;
			Cell.h = CellSize;
			const bool LightSquare = ((bx + by) % 2) == 0;

			if(!s_Chess.m_PendingPromotion && s_Chess.m_SelectedX >= 0 && s_Chess.m_SelectedY >= 0 && s_Chess.m_aValidTargets[by][bx])
			{
				const bool IsOccupiedOrCastle = s_Chess.m_Pos.m_Board[by][bx] != '.';
				if(IsOccupiedOrCastle)
				{
					CUIRect Ring = Cell;
					Ring.Margin(CellSize * 0.08f, &Ring);
					Ring.DrawOutline(ColorRGBA(0.15f, 0.15f, 0.15f, 0.35f));
					Ring.Margin(2.0f, &Ring);
					Ring.DrawOutline(ColorRGBA(0.15f, 0.15f, 0.15f, 0.35f));
				}
				else
				{
					const float DotSz = CellSize * 0.28f;
					CUIRect Dot = {Cell.x + (Cell.w - DotSz) * 0.5f, Cell.y + (Cell.h - DotSz) * 0.5f, DotSz, DotSz};
					Dot.Draw(ColorRGBA(0.10f, 0.10f, 0.10f, 0.24f), IGraphics::CORNER_ALL, DotSz * 0.5f);
				}
			}

			const ColorRGBA CoordCol = LightSquare ? DarkSquareCol : LightSquareCol;
			if(dx == 0)
			{
				char aRank[2] = {(char)('8' - by), 0};
				TextRender()->TextColor(CoordCol);
				TextRender()->Text(Cell.x + 2.0f, Cell.y + 1.5f, CellSize * 0.20f, aRank, -1.0f);
			}
			if(dy == 7)
			{
				char aFile[2] = {(char)('a' + bx), 0};
				TextRender()->TextColor(CoordCol);
				TextRender()->Text(Cell.x + Cell.w - CellSize * 0.22f, Cell.y + Cell.h - CellSize * 0.26f, CellSize * 0.20f, aFile, -1.0f);
			}
		}
	}
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	auto ShouldSkipBoardPiece = [&](int bx, int by) {
		if(s_Chess.m_Dragging && bx == s_Chess.m_DragFromX && by == s_Chess.m_DragFromY)
			return true;
		if(MoveAnimActive && bx == s_Chess.m_AnimToX && by == s_Chess.m_AnimToY)
			return true;
		if(MoveAnimActive && s_Chess.m_HasRookAnim && bx == s_Chess.m_AnimRookToX && by == s_Chess.m_AnimRookY)
			return true;
		return false;
	};

	const float AnimEase = MoveAnimActive ? (1.0f - powf(1.0f - std::clamp((AnimTime - s_Chess.m_AnimStart) / s_Chess.m_AnimDuration, 0.0f, 1.0f), 3.0f)) : 1.0f;

	if(UseSpriteAtlas)
	{
		Graphics()->TextureSet(s_ChessPiecesTex);
		Graphics()->QuadsBegin();
		for(int dy = 0; dy < 8; ++dy)
		{
			for(int dx = 0; dx < 8; ++dx)
			{
				const int bx = DisplayToBoardX(dx);
				const int by = DisplayToBoardY(dy);
				if(ShouldSkipBoardPiece(bx, by))
					continue;
				const char Piece = s_Chess.m_Pos.m_Board[by][bx];
				if(Piece != '.')
				{
					CUIRect Cell = {Board.x + dx * CellSize, Board.y + dy * CellSize, CellSize, CellSize};
					DrawSpriteSingle(Cell, Piece, 1.0f);
				}
			}
		}

		if(MoveAnimActive && s_Chess.m_AnimPiece != '.')
		{
			const float FX = (float)BoardToDisplayX(s_Chess.m_AnimFromX);
			const float FY = (float)BoardToDisplayY(s_Chess.m_AnimFromY);
			const float TX = (float)BoardToDisplayX(s_Chess.m_AnimToX);
			const float TY = (float)BoardToDisplayY(s_Chess.m_AnimToY);
			CUIRect AnimRect = {Board.x + (FX + (TX - FX) * AnimEase) * CellSize, Board.y + (FY + (TY - FY) * AnimEase) * CellSize, CellSize, CellSize};
			DrawSpriteSingle(AnimRect, s_Chess.m_AnimPiece, 0.98f);
		}
		if(MoveAnimActive && s_Chess.m_HasRookAnim && s_Chess.m_AnimRookPiece != '.')
		{
			const float FX = (float)BoardToDisplayX(s_Chess.m_AnimRookFromX);
			const float TX = (float)BoardToDisplayX(s_Chess.m_AnimRookToX);
			const float RY = (float)BoardToDisplayY(s_Chess.m_AnimRookY);
			CUIRect AnimRect = {Board.x + (FX + (TX - FX) * AnimEase) * CellSize, Board.y + RY * CellSize, CellSize, CellSize};
			DrawSpriteSingle(AnimRect, s_Chess.m_AnimRookPiece, 0.98f);
		}
		if(s_Chess.m_Dragging && s_Chess.m_DragFromX >= 0 && s_Chess.m_DragFromY >= 0)
		{
			const char DragPiece = s_Chess.m_Pos.m_Board[s_Chess.m_DragFromY][s_Chess.m_DragFromX];
			CUIRect DragRect = {s_Chess.m_DragMouse.x - CellSize * 0.54f, s_Chess.m_DragMouse.y - CellSize * 0.54f, CellSize * 1.08f, CellSize * 1.08f};
			DrawSpriteSingle(DragRect, DragPiece, 0.96f);
		}
		Graphics()->QuadsEnd();
	}
	else
	{
		for(int dy = 0; dy < 8; ++dy)
		{
			for(int dx = 0; dx < 8; ++dx)
			{
				const int bx = DisplayToBoardX(dx);
				const int by = DisplayToBoardY(dy);
				if(ShouldSkipBoardPiece(bx, by))
					continue;
				const char Piece = s_Chess.m_Pos.m_Board[by][bx];
				if(Piece != '.')
				{
					CUIRect Cell = {Board.x + dx * CellSize, Board.y + dy * CellSize, CellSize, CellSize};
					RenderIconFallbackPiece(Cell, Piece, 1.0f);
				}
			}
		}
		if(MoveAnimActive && s_Chess.m_AnimPiece != '.')
		{
			const float FX = (float)BoardToDisplayX(s_Chess.m_AnimFromX);
			const float FY = (float)BoardToDisplayY(s_Chess.m_AnimFromY);
			const float TX = (float)BoardToDisplayX(s_Chess.m_AnimToX);
			const float TY = (float)BoardToDisplayY(s_Chess.m_AnimToY);
			CUIRect AnimRect = {Board.x + (FX + (TX - FX) * AnimEase) * CellSize, Board.y + (FY + (TY - FY) * AnimEase) * CellSize, CellSize, CellSize};
			RenderIconFallbackPiece(AnimRect, s_Chess.m_AnimPiece, 0.98f);
		}
		if(MoveAnimActive && s_Chess.m_HasRookAnim && s_Chess.m_AnimRookPiece != '.')
		{
			const float FX = (float)BoardToDisplayX(s_Chess.m_AnimRookFromX);
			const float TX = (float)BoardToDisplayX(s_Chess.m_AnimRookToX);
			const float RY = (float)BoardToDisplayY(s_Chess.m_AnimRookY);
			CUIRect AnimRect = {Board.x + (FX + (TX - FX) * AnimEase) * CellSize, Board.y + RY * CellSize, CellSize, CellSize};
			RenderIconFallbackPiece(AnimRect, s_Chess.m_AnimRookPiece, 0.98f);
		}
		if(s_Chess.m_Dragging && s_Chess.m_DragFromX >= 0 && s_Chess.m_DragFromY >= 0)
		{
			const char DragPiece = s_Chess.m_Pos.m_Board[s_Chess.m_DragFromY][s_Chess.m_DragFromX];
			CUIRect DragRect = {s_Chess.m_DragMouse.x - CellSize * 0.5f, s_Chess.m_DragMouse.y - CellSize * 0.5f, CellSize, CellSize};
			RenderIconFallbackPiece(DragRect, DragPiece, 0.96f);
		}
	}

	// Right-click arrows & square marking input
	if(!s_Chess.m_PendingPromotion)
	{
		if(HoverX >= 0 && HoverY >= 0 && Ui()->MouseButtonClicked(1))
		{
			s_Chess.m_RightDragging = true;
			s_Chess.m_RightFromX = HoverX;
			s_Chess.m_RightFromY = HoverY;
		}
		if(s_Chess.m_RightDragging && !Ui()->MouseButton(1))
		{
			if(HoverX >= 0 && HoverY >= 0)
			{
				if(HoverX == s_Chess.m_RightFromX && HoverY == s_Chess.m_RightFromY)
				{
					s_Chess.m_aMarkedSquares[HoverY][HoverX] = !s_Chess.m_aMarkedSquares[HoverY][HoverX];
				}
				else
				{
					const SChessState::SChessArrow Arrow = {s_Chess.m_RightFromX, s_Chess.m_RightFromY, HoverX, HoverY};
					auto It = std::find(s_Chess.m_vArrows.begin(), s_Chess.m_vArrows.end(), Arrow);
					if(It != s_Chess.m_vArrows.end())
						s_Chess.m_vArrows.erase(It);
					else
						s_Chess.m_vArrows.push_back(Arrow);
				}
			}
			s_Chess.m_RightDragging = false;
			s_Chess.m_RightFromX = -1;
			s_Chess.m_RightFromY = -1;
		}
	}

	// Render right-click arrows and Opening Trainer hint arrow in a single O(1) batched draw call
	{
		const bool HasPreviewArrow = s_Chess.m_RightDragging && HoverX >= 0 && HoverY >= 0 && !(HoverX == s_Chess.m_RightFromX && HoverY == s_Chess.m_RightFromY);
		const bool HasOpeningHint = (s_Chess.m_Difficulty == 6) && !s_Chess.m_GameOver && (s_Chess.m_OpeningStep < CurOpening.m_Count) &&
			(s_Chess.m_Pos.m_WhiteTurn == s_Chess.m_PlayerIsWhite) && (s_Chess.m_OpeningShowHint || s_Chess.m_OpeningRevealHint);
		if(!s_Chess.m_vArrows.empty() || HasPreviewArrow || HasOpeningHint)
		{
			auto SquareCenter = [&](int bx, int by) {
				const int dx = BoardToDisplayX(bx);
				const int dy = BoardToDisplayY(by);
				return vec2(Board.x + (dx + 0.5f) * CellSize, Board.y + (dy + 0.5f) * CellSize);
			};

			auto EmitSegmentAndHead = [&](vec2 Start, vec2 End, bool DrawHead, float ShaftW, float HeadLen, float HeadHalfW) {
				const vec2 Delta = End - Start;
				const float Len = length(Delta);
				if(Len < 1.0f)
					return;
				const vec2 Dir = Delta / Len;
				const vec2 Perp(-Dir.y, Dir.x);
				const vec2 ShaftEnd = DrawHead ? (End - Dir * HeadLen) : End;
				if(length(ShaftEnd - Start) >= 0.5f || !DrawHead)
				{
					const vec2 Half = Perp * (ShaftW * 0.5f);
					const IGraphics::CFreeformItem Shaft(Start + Half, Start - Half, ShaftEnd - Half, ShaftEnd + Half);
					Graphics()->QuadsDrawFreeform(&Shaft, 1);
				}
				if(DrawHead)
				{
					const vec2 Base = End - Dir * HeadLen;
					const IGraphics::CFreeformItem Head(End, Base + Perp * HeadHalfW, Base - Perp * HeadHalfW, Base - Perp * HeadHalfW);
					Graphics()->QuadsDrawFreeform(&Head, 1);
				}
			};

			auto DrawChessArrow = [&](int FromX, int FromY, int ToX, int ToY) {
				const float ShaftW = CellSize * 0.22f;
				const float HeadLen = CellSize * 0.36f;
				const float HeadHalfW = CellSize * 0.30f;
				const vec2 P0 = SquareCenter(FromX, FromY);
				const vec2 P1 = SquareCenter(ToX, ToY);
				const int Dx = abs(ToX - FromX);
				const int Dy = abs(ToY - FromY);
				if((Dx == 1 && Dy == 2) || (Dx == 2 && Dy == 1))
				{
					// L-shaped Knight arrow
					const vec2 Corner = (Dy == 2) ? SquareCenter(FromX, ToY) : SquareCenter(ToX, FromY);
					const vec2 D1 = normalize(Corner - P0);
					const vec2 D2 = normalize(P1 - Corner);
					const vec2 Start = P0 + D1 * (CellSize * 0.26f);
					const vec2 End = P1 - D2 * (CellSize * 0.14f);
					EmitSegmentAndHead(Start, Corner + D1 * (ShaftW * 0.5f), false, ShaftW, HeadLen, HeadHalfW);
					EmitSegmentAndHead(Corner - D2 * (ShaftW * 0.5f), End, true, ShaftW, HeadLen, HeadHalfW);
				}
				else
				{
					const vec2 Dir = normalize(P1 - P0);
					const vec2 Start = P0 + Dir * (CellSize * 0.28f);
					const vec2 End = P1 - Dir * (CellSize * 0.14f);
					EmitSegmentAndHead(Start, End, true, ShaftW, HeadLen, HeadHalfW);
				}
			};

			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(1.0f, 0.67f, 0.10f, 0.80f);
			for(const auto &A : s_Chess.m_vArrows)
				DrawChessArrow(A.m_FromX, A.m_FromY, A.m_ToX, A.m_ToY);
			if(HasPreviewArrow)
			{
				Graphics()->SetColor(1.0f, 0.67f, 0.10f, 0.55f);
				DrawChessArrow(s_Chess.m_RightFromX, s_Chess.m_RightFromY, HoverX, HoverY);
			}
			if(HasOpeningHint)
			{
				const SChessOpeningStep &HintStep = CurOpening.m_aSteps[s_Chess.m_OpeningStep];
				Graphics()->SetColor(0.18f, 0.86f, 0.52f, 0.84f);
				DrawChessArrow(HintStep.m_FromX, HintStep.m_FromY, HintStep.m_ToX, HintStep.m_ToY);
			}
			Graphics()->QuadsEnd();
		}
	}

	if(s_Chess.m_PendingPromotion)
	{
		CUIRect Dim = Board;
		Dim.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.45f), IGraphics::CORNER_ALL, 4.0f);

		const float BoxW = CellSize * 4.4f;
		const float BoxH = CellSize * 1.45f;
		CUIRect PromoBox = {Board.x + (Board.w - BoxW) * 0.5f, Board.y + (Board.h - BoxH) * 0.5f, BoxW, BoxH};
		PromoBox.Draw(ColorRGBA(0.14f, 0.16f, 0.20f, 0.96f), IGraphics::CORNER_ALL, 6.0f);
		PromoBox.DrawOutline(ColorRGBA(1.0f, 1.0f, 1.0f, 0.35f));

		CUIRect LabelRect, PiecesRow;
		PromoBox.HSplitTop(18.0f, &LabelRect, &PiecesRow);
		Ui()->DoLabel(&LabelRect, Localize("Elige pieza de coronación"), 11.5f, TEXTALIGN_MC);
		PiecesRow.Margin(4.0f, &PiecesRow);

		static CButtonContainer s_aPromoBtns[4];
		const char aPromoUpper[4] = {'Q', 'R', 'B', 'N'};
		const bool PromoWhite = IsWhitePiece(s_Chess.m_Pos.m_Board[s_Chess.m_PromoMove.m_FromY][s_Chess.m_PromoMove.m_FromX]);
		const float SlotW = PiecesRow.w / 4.0f;
		for(int i = 0; i < 4; ++i)
		{
			CUIRect Slot;
			PiecesRow.VSplitLeft(SlotW, &Slot, &PiecesRow);
			Slot.Margin(2.0f, &Slot);
			if(DoButton_Menu(&s_aPromoBtns[i], "", 0, &Slot))
			{
				s_Chess.m_PromoMove.m_Promo = aPromoUpper[i];
				s_Chess.m_PendingPromotion = false;
				ApplyLocalMoveAndSync(s_Chess.m_PromoMove);
				break;
			}
			const char DrawChar = PromoWhite ? aPromoUpper[i] : (char)tolower((unsigned char)aPromoUpper[i]);
			const float IconSz = std::min(Slot.w, Slot.h);
			CUIRect IconR = {Slot.x + (Slot.w - IconSz) * 0.5f, Slot.y + (Slot.h - IconSz) * 0.5f, IconSz, IconSz};
			if(UseSpriteAtlas)
			{
				Graphics()->TextureSet(s_ChessPiecesTex);
				Graphics()->QuadsBegin();
				DrawSpriteSingle(IconR, DrawChar, 1.0f);
				Graphics()->QuadsEnd();
			}
			else
			{
				RenderIconFallbackPiece(IconR, DrawChar, 1.0f);
			}
		}
	}

	if(s_Chess.m_GameOver)
	{
		CUIRect Overlay = Board;
		Overlay.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.36f), IGraphics::CORNER_ALL, 4.0f);

		const char *pResult;
		if(s_Chess.m_Resigned)
			pResult = (s_Chess.m_WhiteWon == s_Chess.m_PlayerIsWhite) ? Localize("¡Victoria por abandono!") : Localize("Derrota por abandono");
		else if(s_Chess.m_Stalemate)
			pResult = Localize("Tablas - Ahogado");
		else if(s_Chess.m_FiftyMove)
			pResult = Localize("Tablas - Regla de 50 jugadas");
		else if(s_Chess.m_Threefold)
			pResult = Localize("Tablas - Triple repetición");
		else
			pResult = s_Chess.m_WhiteWon ? Localize("¡Ganan Blancas!") : Localize("¡Ganan Negras!");

		Ui()->DoLabel(&Overlay, pResult, HeadlineFontSize, TEXTALIGN_MC);
	}
}

void CMenus::RenderSettingsAClientSettings(CUIRect MainView)
{
	CUIRect Column, LeftView, RightView, Button, Label;

	static CScrollRegion s_ScrollRegionAc;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_ForceShowScrollbar = true;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegionAc.Begin(&MainView, &ScrollParams);

	static std::vector<CUIRect> s_SectionBoxesAc;
	const float ScrollOffset = MainView.y;
	static float s_PrevScrollOffsetAc = 0.0f;

	MainView.VSplitRight(5.0f, &MainView, nullptr);
	MainView.VSplitLeft(5.0f, nullptr, &MainView);

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	for(CUIRect &Section : s_SectionBoxesAc)
	{
		float Padding = MarginBetweenViews * 0.6666f;
		Section.w += Padding;
		Section.h += Padding;
		Section.x -= Padding * 0.5f;
		Section.y -= Padding * 0.5f;
		Section.y -= s_PrevScrollOffsetAc - ScrollOffset;
		if(Section.x < MainView.x + 1.0f)
		{
			float Diff = (MainView.x + 1.0f) - Section.x;
			Section.x += Diff;
			Section.w -= Diff;
		}
		if(Section.x + Section.w > MainView.x + MainView.w - 1.0f)
		{
			Section.w = (MainView.x + MainView.w - 1.0f) - Section.x;
		}
		if(g_Config.m_AcModernUi)
		{
			Section.Draw(GetColorBorder(0.40f), IGraphics::CORNER_ALL, 10.0f);
			CUIRect Inner = Section;
			Inner.Margin(1.0f, &Inner);
			Inner.Draw(GetColorBg(), IGraphics::CORNER_ALL, 9.0f);
		}
		else
		{
			Section.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
		}
	}
	s_PrevScrollOffsetAc = ScrollOffset;
	s_SectionBoxesAc.clear();

	// ***** Left Column: Gameplay, Network & Input ***** //
	Column = LeftView;

	const auto DoSplitHookMoveUI = [&](bool MsMode) {
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcInputSplitHookMove, TCLocalize("Decouple hook from A/D movement"), &g_Config.m_AcInputSplitHookMove, &Column, LineSize);
		if(g_Config.m_AcInputSplitHookMove)
		{
			Column.HSplitTop(LineSize, &Button, &Column);
			if(MsMode)
				DoSliderWithScaledValue(&g_Config.m_AcFastInputHookAmount, &g_Config.m_AcFastInputHookAmount, &Button, TCLocalize("Hook offset"), 1, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
			else
				DoTickAmountSlider(&g_Config.m_AcTickInputHookAmount, &Button, TCLocalize("Hook offset"), 0, 3000);
		}
	};

	const bool VisualTab = (m_CurAClientTab == ACLIENT_TAB_HUD);

	if(!VisualTab)
	{
	// ***** Network & Anti-Ping ***** //
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxesAc.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Network & Anti-Ping"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoCheckboxWithHelp(&g_Config.m_ClAntiPing, TCLocalize("Enable Anti-Ping"), TCLocalize("Tries to predict other entities and players to give a feel of low latency."), &g_Config.m_ClAntiPing, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_ClAntiPingEcho, TCLocalize("Anti-Ping Echo in chat (ON / OFF)"), TCLocalize("Shows a local notification in chat whenever you toggle anti-ping with your bound key or command."), &g_Config.m_ClAntiPingEcho, &Column, LineSize);

	static CButtonContainer s_ReaderButtonAntiPingAc, s_ClearButtonAntiPingAc;
	DoLine_KeyReader(Column, s_ReaderButtonAntiPingAc, s_ClearButtonAntiPingAc, TCLocalize("Toggle Anti-Ping key"), "toggle_antiping");

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	s_SectionBoxesAc.back().h = Column.y - s_SectionBoxesAc.back().y;

	// ***** Input (Fast Input & Prediction) ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxesAc.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Input"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	// Prediction Margin
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_ClPredictionMargin, &g_Config.m_ClPredictionMargin, &Button, TCLocalize("Prediction Margin"), 10, 75, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSubTickAiming, TCLocalize("Sub-Tick aiming"), &g_Config.m_ClSubTickAiming, &Column, LineSize);
	if(!g_Config.m_ClSubTickAiming)
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcFastInputRepredictHook, TCLocalize("Repredict on mid-tick hook direction change (may lag)"), &g_Config.m_AcFastInputRepredictHook, &Column, LineSize);
		g_Config.m_TcFastInputRepredictHook = g_Config.m_AcFastInputRepredictHook;
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(LineSize, &Label, &Column);
	CUIRect PencilBtn;
	Label.VSplitRight(Label.h, &Label, &PencilBtn);
	Ui()->DoLabel(&Label, TCLocalize("Fast Input Mode:"), Label.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);

	static bool s_ShowInputVisibilityEdit = false;
	static CButtonContainer s_PencilBtnContainer;
	if(Ui()->DoButton_FontIcon(&s_PencilBtnContainer, FontIcon::PENCIL, s_ShowInputVisibilityEdit ? 1 : 0, &PencilBtn, BUTTONFLAG_LEFT))
	{
		s_ShowInputVisibilityEdit = !s_ShowInputVisibilityEdit;
	}

	struct SInputDef
	{
		const char *m_pName;
		int m_Value;
	};
	static const SInputDef s_aAllDefs[] = {
		{"Off", AC_INPUTS_OFF},
		{"aX30", AC_INPUTS_AX30},
		{"aXv2", AC_INPUTS_AXV2},
		{"AClient", AC_INPUTS_ACLIENT},
		{"Fast", AC_INPUTS_FAST},
		{"Best", AC_INPUTS_BEST},
		{"Saiko", AC_INPUTS_SAIKO},
		{"Delta", AC_INPUTS_DELTA},
		{"F", AC_INPUTS_F},
		{"Cloud", AC_INPUTS_CLOUD},
	};

	if(s_ShowInputVisibilityEdit)
	{
		CUIRect EditPanel;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize * 3.6f, &EditPanel, &Column);
		EditPanel.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.40f), IGraphics::CORNER_ALL, 6.0f);
		CUIRect InnerPanel = EditPanel;
		InnerPanel.Margin(6.0f, &InnerPanel);

		CUIRect PanelTitle;
		InnerPanel.HSplitTop(LineSize * 0.8f, &PanelTitle, &InnerPanel);
		Ui()->DoLabel(&PanelTitle, TCLocalize("Select visible inputs:"), PanelTitle.h * 0.85f, TEXTALIGN_ML);

		InnerPanel.HSplitTop(4.0f, nullptr, &InnerPanel);

		static CButtonContainer s_aVisibilityToggleButtons[10];

		CUIRect Row1, Row2;
		InnerPanel.HSplitTop((InnerPanel.h - 3.0f) * 0.5f, &Row1, &InnerPanel);
		InnerPanel.HSplitTop(3.0f, nullptr, &Row2);

		const float Spacing = 3.0f;
		const float W = (Row1.w - Spacing * 4.0f) / 5.0f;
		for(int i = 0; i < 5; ++i)
		{
			CUIRect B;
			if(i < 4)
			{
				Row1.VSplitLeft(W, &B, &Row1);
				Row1.VSplitLeft(Spacing, nullptr, &Row1);
			}
			else
				B = Row1;

			const int ModeVal = s_aAllDefs[i].m_Value;
			const bool IsVisible = (g_Config.m_AcInputsVisibleMask & (1 << ModeVal)) != 0;
			if(DoButton_Menu(&s_aVisibilityToggleButtons[ModeVal], TCLocalize(s_aAllDefs[i].m_pName), IsVisible, &B, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL))
			{
				g_Config.m_AcInputsVisibleMask ^= (1 << ModeVal);
				if((g_Config.m_AcInputsVisibleMask & 0x3FF) == 0)
					g_Config.m_AcInputsVisibleMask = (1 << ModeVal);
			}
		}

		for(int i = 5; i < 10; ++i)
		{
			CUIRect B;
			if(i < 9)
			{
				Row2.VSplitLeft(W, &B, &Row2);
				Row2.VSplitLeft(Spacing, nullptr, &Row2);
			}
			else
				B = Row2;

			const int ModeVal = s_aAllDefs[i].m_Value;
			const bool IsVisible = (g_Config.m_AcInputsVisibleMask & (1 << ModeVal)) != 0;
			if(DoButton_Menu(&s_aVisibilityToggleButtons[ModeVal], TCLocalize(s_aAllDefs[i].m_pName), IsVisible, &B, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL))
			{
				g_Config.m_AcInputsVisibleMask ^= (1 << ModeVal);
				if((g_Config.m_AcInputsVisibleMask & 0x3FF) == 0)
					g_Config.m_AcInputsVisibleMask = (1 << ModeVal);
			}
		}
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(LineSize, &Button, &Column);
	{
		static CButtonContainer s_aInputButtons[10];

		SInputDef aShownDefs[10];
		int NumShown = 0;
		for(const auto &Def : s_aAllDefs)
		{
			if(g_Config.m_AcInputsVisibleMask & (1 << Def.m_Value))
			{
				aShownDefs[NumShown++] = Def;
			}
		}
		if(NumShown == 0)
		{
			aShownDefs[0] = s_aAllDefs[0];
			aShownDefs[1] = s_aAllDefs[1];
			aShownDefs[2] = s_aAllDefs[2];
			NumShown = 3;
		}

		CUIRect ButtonsRect = Button;
		const float Spacing = 2.0f;
		const float ModeButtonWidth = (ButtonsRect.w - Spacing * (NumShown - 1)) / (float)NumShown;
		for(int i = 0; i < NumShown; ++i)
		{
			CUIRect ModeButton;
			if(i < NumShown - 1)
			{
				ButtonsRect.VSplitLeft(ModeButtonWidth, &ModeButton, &ButtonsRect);
				ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
			}
			else
				ModeButton = ButtonsRect;

		int Corners = (NumShown == 1) ? IGraphics::CORNER_ALL : ((i == 0) ? IGraphics::CORNER_L : ((i == NumShown - 1) ? IGraphics::CORNER_R : IGraphics::CORNER_NONE));

			if(DoButton_Menu(&s_aInputButtons[aShownDefs[i].m_Value], TCLocalize(aShownDefs[i].m_pName), g_Config.m_AcInputs == aShownDefs[i].m_Value, &ModeButton, BUTTONFLAG_LEFT, nullptr, Corners))
			{
				g_Config.m_AcInputs = aShownDefs[i].m_Value;
				g_Config.m_TcFastInput = (g_Config.m_AcInputs != AC_INPUTS_OFF) ? 1 : 0;
			}
		}
	}
	if(g_Config.m_AcInputs == AC_INPUTS_AX30)
	{
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		DoTickAmountSlider(&g_Config.m_AcAX30InputAmount, &Button, TCLocalize("Prediction offset"), 0, 1000);

		DoSplitHookMoveUI(false);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcAX30AdaptiveJitter, TCLocalize("Smart adaptive compensation"), &g_Config.m_AcAX30AdaptiveJitter, &Column, LineSize);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcAX30CollisionClamp, TCLocalize("Boundary safety filter"), &g_Config.m_AcAX30CollisionClamp, &Column, LineSize);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Label, &Column);
		Ui()->DoLabel(&Label, TCLocalize("Interpolation"), Label.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		{
			static CButtonContainer s_aAX30InterpolationButtons[3];
			static const char *s_apInterpolationNames[] = {"HE", "Linear (1:1)", "Cubic"};
			static const int s_aInterpolationValues[] = {1, 2, 3};

			CUIRect ButtonsRect = Button;
			const float Spacing = 2.0f;
			const float InterpWidth = (ButtonsRect.w - Spacing * 2.0f) / 3.0f;
			for(int i = 0; i < 3; ++i)
			{
				CUIRect InterpButton;
				if(i < 2)
				{
					ButtonsRect.VSplitLeft(InterpWidth, &InterpButton, &ButtonsRect);
					ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
				}
				else
					InterpButton = ButtonsRect;

				int Corners = (i == 0) ? IGraphics::CORNER_L : ((i == 2) ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
				if(DoButton_Menu(&s_aAX30InterpolationButtons[i], TCLocalize(s_apInterpolationNames[i]), g_Config.m_AcAX30Interpolation == s_aInterpolationValues[i], &InterpButton, BUTTONFLAG_LEFT, nullptr, Corners))
					g_Config.m_AcAX30Interpolation = s_aInterpolationValues[i];
			}
		}

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcAX30Others, TCLocalize("aX30 input others"), &g_Config.m_AcAX30Others, &Column, LineSize);
		if(g_Config.m_AcAX30Others)
		{
			Column.HSplitTop(LineSize, &Button, &Column);
			DoTickAmountSlider(&g_Config.m_AcAX30OthersAmount, &Button, TCLocalize("Others offset"), 0, 1000);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcFastInputOthersOnlyFrozen, TCLocalize("Only when frozen"), &g_Config.m_AcFastInputOthersOnlyFrozen, &Column, LineSize);
		}
	}
	else if(g_Config.m_AcInputs == AC_INPUTS_AXV2)
	{
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		DoTickAmountSlider(&g_Config.m_AcAXv2InputAmount, &Button, TCLocalize("Prediction offset"), 0, 1000);

		DoSplitHookMoveUI(false);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcAXv2AdaptiveJitter, TCLocalize("Smart adaptive compensation"), &g_Config.m_AcAXv2AdaptiveJitter, &Column, LineSize);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcAXv2CollisionClamp, TCLocalize("Boundary safety filter"), &g_Config.m_AcAXv2CollisionClamp, &Column, LineSize);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Label, &Column);
		Ui()->DoLabel(&Label, TCLocalize("Interpolation"), Label.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		{
			static CButtonContainer s_aAXv2InterpolationButtons[3];
			static const char *s_apInterpolationNames[] = {"HE", "Linear (1:1)", "Cubic"};
			static const int s_aInterpolationValues[] = {1, 2, 3};

			CUIRect ButtonsRect = Button;
			const float Spacing = 2.0f;
			const float InterpWidth = (ButtonsRect.w - Spacing * 2.0f) / 3.0f;
			for(int i = 0; i < 3; ++i)
			{
				CUIRect InterpButton;
				if(i < 2)
				{
					ButtonsRect.VSplitLeft(InterpWidth, &InterpButton, &ButtonsRect);
					ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
				}
				else
					InterpButton = ButtonsRect;

				int Corners = (i == 0) ? IGraphics::CORNER_L : ((i == 2) ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
				if(DoButton_Menu(&s_aAXv2InterpolationButtons[i], TCLocalize(s_apInterpolationNames[i]), g_Config.m_AcAXv2Interpolation == s_aInterpolationValues[i], &InterpButton, BUTTONFLAG_LEFT, nullptr, Corners))
					g_Config.m_AcAXv2Interpolation = s_aInterpolationValues[i];
			}
		}

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcAXv2Others, TCLocalize("aXv2 input others"), &g_Config.m_AcAXv2Others, &Column, LineSize);
		if(g_Config.m_AcAXv2Others)
		{
			Column.HSplitTop(LineSize, &Button, &Column);
			DoTickAmountSlider(&g_Config.m_AcAXv2OthersAmount, &Button, TCLocalize("Others offset"), 0, 1000);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcFastInputOthersOnlyFrozen, TCLocalize("Only when frozen"), &g_Config.m_AcFastInputOthersOnlyFrozen, &Column, LineSize);
		}
	}
	else if(g_Config.m_AcInputs == AC_INPUTS_ACLIENT)
	{
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		DoTickAmountSlider(&g_Config.m_AcAClientInputAmount, &Button, TCLocalize("Prediction offset"), 0, 1000);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_AcAClientInputLatencyComp, &g_Config.m_AcAClientInputLatencyComp, &Button, TCLocalize("Latency compensation"), 0, 50, &CUi::ms_LinearScrollbarScale, 0, "%");

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Label, &Column);
		Ui()->DoLabel(&Label, TCLocalize("Interpolation"), Label.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		{
			static CButtonContainer s_aAClientInterpolationButtons[3];
			static const char *s_apInterpolationNames[] = {"Linear", "Cubic", "Smooth"};
			static const int s_aInterpolationValues[] = {1, 2, 3};

			CUIRect ButtonsRect = Button;
			const float Spacing = 2.0f;
			const float InterpWidth = (ButtonsRect.w - Spacing * 2.0f) / 3.0f;
			for(int i = 0; i < 3; ++i)
			{
				CUIRect InterpButton;
				if(i < 2)
				{
					ButtonsRect.VSplitLeft(InterpWidth, &InterpButton, &ButtonsRect);
					ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
				}
				else
					InterpButton = ButtonsRect;

				int Corners = (i == 0) ? IGraphics::CORNER_L : ((i == 2) ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
				if(DoButton_Menu(&s_aAClientInterpolationButtons[i], TCLocalize(s_apInterpolationNames[i]), g_Config.m_AcAClientInputInterpolation == s_aInterpolationValues[i], &InterpButton, BUTTONFLAG_LEFT, nullptr, Corners))
					g_Config.m_AcAClientInputInterpolation = s_aInterpolationValues[i];
			}
		}

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcAClientInputOthers, TCLocalize("AClient input others"), &g_Config.m_AcAClientInputOthers, &Column, LineSize);
		if(g_Config.m_AcAClientInputOthers)
		{
			Column.HSplitTop(LineSize, &Button, &Column);
			DoTickAmountSlider(&g_Config.m_AcAClientInputOthersAmount, &Button, TCLocalize("Others offset"), 0, 1000);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcFastInputOthersOnlyFrozen, TCLocalize("Only when frozen"), &g_Config.m_AcFastInputOthersOnlyFrozen, &Column, LineSize);
		}
	}
	else if(g_Config.m_AcInputs == AC_INPUTS_FAST)
	{
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		DoSliderWithScaledValue(&g_Config.m_AcFastInputAmount, &g_Config.m_AcFastInputAmount, &Button, TCLocalize("Prediction offset"), 1, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
		g_Config.m_TcFastInputAmount = g_Config.m_AcFastInputAmount;

		DoSplitHookMoveUI(true);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcFastInputOthers, TCLocalize("Fast Input others"), &g_Config.m_AcFastInputOthers, &Column, LineSize);
		g_Config.m_TcFastInputOthers = g_Config.m_AcFastInputOthers;
		if(g_Config.m_AcFastInputOthers)
		{
			Column.HSplitTop(LineSize, &Button, &Column);
			DoSliderWithScaledValue(&g_Config.m_AcFastInputOthersAmount, &g_Config.m_AcFastInputOthersAmount, &Button, TCLocalize("Others offset"), 1, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
			g_Config.m_TcFastInputOthersAmount = g_Config.m_AcFastInputOthersAmount;
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcFastInputOthersOnlyFrozen, TCLocalize("Only when frozen"), &g_Config.m_AcFastInputOthersOnlyFrozen, &Column, LineSize);
		}
	}
	else if(g_Config.m_AcInputs == AC_INPUTS_BEST)
	{
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		DoTickAmountSlider(&g_Config.m_AcBestInputAmount, &Button, TCLocalize("Prediction offset"), 0, 1000);

		DoSplitHookMoveUI(false);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_AcBestInputSmoothing, &g_Config.m_AcBestInputSmoothing, &Button, TCLocalize("Smoothing"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_AcBestInputLatencyComp, &g_Config.m_AcBestInputLatencyComp, &Button, TCLocalize("Latency compensation"), 0, 50, &CUi::ms_LinearScrollbarScale, 0, "%");

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Label, &Column);
		Ui()->DoLabel(&Label, TCLocalize("Interpolation"), Label.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		{
			static CButtonContainer s_aInterpolationButtons[3];
			static const char *s_apInterpolationNames[] = {"Linear", "Cubic", "Smooth"};
			static const int s_aInterpolationValues[] = {1, 2, 3};

			CUIRect ButtonsRect = Button;
			const float Spacing = 2.0f;
			const float InterpWidth = (ButtonsRect.w - Spacing * 2.0f) / 3.0f;
			for(int i = 0; i < 3; ++i)
			{
				CUIRect InterpButton;
				if(i < 2)
				{
					ButtonsRect.VSplitLeft(InterpWidth, &InterpButton, &ButtonsRect);
					ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
				}
				else
					InterpButton = ButtonsRect;

				int Corners = (i == 0) ? IGraphics::CORNER_L : ((i == 2) ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
				if(DoButton_Menu(&s_aInterpolationButtons[i], TCLocalize(s_apInterpolationNames[i]), g_Config.m_AcBestInputInterpolation == s_aInterpolationValues[i], &InterpButton, BUTTONFLAG_LEFT, nullptr, Corners))
					g_Config.m_AcBestInputInterpolation = s_aInterpolationValues[i];
			}
		}

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcBestInputOthers, TCLocalize("Best input others"), &g_Config.m_AcBestInputOthers, &Column, LineSize);
		if(g_Config.m_AcBestInputOthers)
		{
			Column.HSplitTop(LineSize, &Button, &Column);
			DoTickAmountSlider(&g_Config.m_AcBestInputOthersAmount, &Button, TCLocalize("Others offset"), 0, 1000);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcFastInputOthersOnlyFrozen, TCLocalize("Only when frozen"), &g_Config.m_AcFastInputOthersOnlyFrozen, &Column, LineSize);
		}
	}
	else if(g_Config.m_AcInputs == AC_INPUTS_SAIKO)
	{
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		DoTickAmountSlider(&g_Config.m_AcSaikoInputAmount, &Button, TCLocalize("Prediction offset"), 0, 500);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcSaikoInputOthers, TCLocalize("Saiko input others"), &g_Config.m_AcSaikoInputOthers, &Column, LineSize);
		if(g_Config.m_AcSaikoInputOthers)
		{
			Column.HSplitTop(LineSize, &Button, &Column);
			DoTickAmountSlider(&g_Config.m_AcSaikoInputOthersAmount, &Button, TCLocalize("Others offset"), 0, 500);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcFastInputOthersOnlyFrozen, TCLocalize("Only when frozen"), &g_Config.m_AcFastInputOthersOnlyFrozen, &Column, LineSize);
		}
	}
	else if(g_Config.m_AcInputs == AC_INPUTS_DELTA)
	{
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		DoTickAmountSlider(&g_Config.m_AcDeltaInputAmount, &Button, TCLocalize("Prediction offset"), 0, 500);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcDeltaInputOthers, TCLocalize("Delta input others"), &g_Config.m_AcDeltaInputOthers, &Column, LineSize);
		if(g_Config.m_AcDeltaInputOthers)
		{
			Column.HSplitTop(LineSize, &Button, &Column);
			DoTickAmountSlider(&g_Config.m_AcDeltaInputOthersAmount, &Button, TCLocalize("Others offset"), 0, 500);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcFastInputOthersOnlyFrozen, TCLocalize("Only when frozen"), &g_Config.m_AcFastInputOthersOnlyFrozen, &Column, LineSize);
		}
	}
	else if(g_Config.m_AcInputs == AC_INPUTS_F)
	{
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		DoTickAmountSlider(&g_Config.m_AcFInputAmount, &Button, TCLocalize("Prediction offset"), 0, 5000, 1000);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcFInputOthers, TCLocalize("F input others"), &g_Config.m_AcFInputOthers, &Column, LineSize);
		if(g_Config.m_AcFInputOthers)
		{
			Column.HSplitTop(LineSize, &Button, &Column);
			DoTickAmountSlider(&g_Config.m_AcFInputOthersAmount, &Button, TCLocalize("Others offset"), 0, 5000, 1000);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcFastInputOthersOnlyFrozen, TCLocalize("Only when frozen"), &g_Config.m_AcFastInputOthersOnlyFrozen, &Column, LineSize);
		}
	}
	else if(g_Config.m_AcInputs == AC_INPUTS_CLOUD)
	{
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		DoTickAmountSlider(&g_Config.m_AcCloudInputAmount, &Button, TCLocalize("Prediction offset"), 0, 500);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcCloudInputOthers, TCLocalize("Cloud input others"), &g_Config.m_AcCloudInputOthers, &Column, LineSize);
		if(g_Config.m_AcCloudInputOthers)
		{
			Column.HSplitTop(LineSize, &Button, &Column);
			DoTickAmountSlider(&g_Config.m_AcCloudInputOthersAmount, &Button, TCLocalize("Others offset"), 0, 500);
			DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcFastInputOthersOnlyFrozen, TCLocalize("Only when frozen"), &g_Config.m_AcFastInputOthersOnlyFrozen, &Column, LineSize);
		}
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	s_SectionBoxesAc.back().h = Column.y - s_SectionBoxesAc.back().y;

	// ***** Fast Practice ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxesAc.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Fast Practice"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static CButtonContainer s_ReaderButtonFastPracticeAc, s_ClearButtonFastPracticeAc;
	DoLine_KeyReader(Column, s_ReaderButtonFastPracticeAc, s_ClearButtonFastPracticeAc, TCLocalize("Fast Practice key (Ghost mode)"), "fast_practice_toggle");

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcFastPracticeShowText, TCLocalize("Show practice mode text (/tc /invincible)"), &g_Config.m_AcFastPracticeShowText, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcFastPracticeSpecOriginal, TCLocalize("Put original in spec on practice start (/spec)"), &g_Config.m_AcFastPracticeSpecOriginal, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcFastPracticeFastInputOverride, TCLocalize("Override Fast Input in Practice"), &g_Config.m_AcFastPracticeFastInputOverride, &Column, LineSize);
	if(g_Config.m_AcFastPracticeFastInputOverride)
	{
		Column.HSplitTop(LineSize, &Button, &Column);
		DoSliderWithScaledValue(&g_Config.m_AcFastPracticeFastInputAmount, &g_Config.m_AcFastPracticeFastInputAmount, &Button, TCLocalize("Practice Fast Input"), 0, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
	}

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_AcFastPracticeGhostAlpha, &g_Config.m_AcFastPracticeGhostAlpha, &Button, TCLocalize("Ghost transparency"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(16.0f, &Label, &Column);
	TextRender()->TextColor(0.55f, 0.9f, 1.0f, 1.0f);
	Ui()->DoLabel(&Label, TCLocalize("Practice commands: /tp  /tc  /invincible  /weapons  /save  /load  /fastinput"), 11.5f, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	if(Client()->State() == IClient::STATE_ONLINE)
	{
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize + 4.0f, &Button, &Column);
		static CButtonContainer s_BtnToggleFastPracticeNow;
		const char *pBtnLabel = GameClient()->m_FastPractice.Enabled() ? TCLocalize("Disable Fast Practice (Return to Run)") : TCLocalize("Enable Fast Practice (Ghost Mode)");
		if(DoButton_Menu(&s_BtnToggleFastPracticeNow, pBtnLabel, GameClient()->m_FastPractice.Enabled() ? 1 : 0, &Button))
			GameClient()->m_FastPractice.Toggle();
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxesAc.back().h = Column.y - s_SectionBoxesAc.back().y;
	}

	if(VisualTab)
	{
	// ***** AClient Theme & UI ***** //
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxesAc.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("AClient Theme & UI"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoCheckboxWithHelp(&g_Config.m_AcModernUi, TCLocalize("Modern Dark-Glass & Accent Hover"), TCLocalize("Estilo estetico moderno oscuro con transparencias y acentos personalizables."), &g_Config.m_AcModernUi, &Column, LineSize);

	static CButtonContainer s_ColorXReset;
	DoLine_ColorPicker(&s_ColorXReset, LineSize, 12.0f, MarginSmall, &Column, TCLocalize("Color de la X (ON / OFF)"), (unsigned int *)&g_Config.m_AcColorX, color_cast<ColorRGBA>(ColorHSLA(12050856)), false);

	static CButtonContainer s_ColorBorderReset;
	DoLine_ColorPicker(&s_ColorBorderReset, LineSize, 12.0f, MarginSmall, &Column, TCLocalize("Color del borde"), (unsigned int *)&g_Config.m_AcColorBorder, color_cast<ColorRGBA>(ColorHSLA(12050856)), false);

	static CButtonContainer s_ColorBgReset;
	DoLine_ColorPicker(&s_ColorBgReset, LineSize, 12.0f, MarginSmall, &Column, TCLocalize("Color del fondo"), (unsigned int *)&g_Config.m_AcColorBg, color_cast<ColorRGBA>(ColorHSLA(3215086858, true)), false, nullptr, true);

	static CButtonContainer s_ColorDetailsReset;
	DoLine_ColorPicker(&s_ColorDetailsReset, LineSize, 12.0f, MarginSmall, &Column, TCLocalize("Color detalles"), (unsigned int *)&g_Config.m_AcColorDetails, color_cast<ColorRGBA>(ColorHSLA(12050856)), false);

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	s_SectionBoxesAc.back().h = Column.y - s_SectionBoxesAc.back().y;
	}

	if(VisualTab)
	{
	// ***** Custom Aspect Ratio & Black Bars ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxesAc.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Relacion de Aspecto / Pantalla"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoCheckboxWithHelp(&g_Config.m_AcCustomAspectRatio, TCLocalize("Activar Relacion de Aspecto Personalizada"), TCLocalize("Permite jugar en 4:3, 16:10, 5:4, 21:9 o proporcion libre con Bandas Negras (Black Bars) o Estirado (Stretched) a resolucion nativa."), &g_Config.m_AcCustomAspectRatio, &Column, LineSize);

	if(g_Config.m_AcCustomAspectRatio)
	{
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		{
			static CButtonContainer s_aAspectPresetBtns[6];
			static const char *s_apAspectPresetNames[6] = {"4:3", "16:10", "5:4", "16:9", "21:9", "Custom"};
			CUIRect Row = Button;
			const float Spacing = 2.0f;
			const float BtnW = (Row.w - Spacing * 5.0f) / 6.0f;
			for(int i = 0; i < 6; ++i)
			{
				CUIRect ItemRect;
				if(i < 5)
				{
					Row.VSplitLeft(BtnW, &ItemRect, &Row);
					Row.VSplitLeft(Spacing, nullptr, &Row);
				}
				else
				{
					ItemRect = Row;
				}
				int Corners = (i == 0) ? IGraphics::CORNER_L : ((i == 5) ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
				if(DoButton_Menu(&s_aAspectPresetBtns[i], TCLocalize(s_apAspectPresetNames[i]), g_Config.m_AcCustomAspectRatioMode == i, &ItemRect, BUTTONFLAG_LEFT, nullptr, Corners))
					g_Config.m_AcCustomAspectRatioMode = i;
			}
		}

		if(g_Config.m_AcCustomAspectRatioMode == 5)
		{
			Column.HSplitTop(MarginSmall, nullptr, &Column);

			// Width (Numerator)
			Column.HSplitTop(LineSize, &Button, &Column);
			CUIRect NumMinus, NumPlus;
			Button.VSplitRight(20.0f, &Button, &NumPlus);
			Button.VSplitRight(2.0f, &Button, nullptr);
			Button.VSplitRight(20.0f, &Button, &NumMinus);
			Button.VSplitRight(4.0f, &Button, nullptr);
			Ui()->DoScrollbarOption(&g_Config.m_AcCustomAspectRatioNum, &g_Config.m_AcCustomAspectRatioNum, &Button, TCLocalize("Ancho (Numerador)"), 1, 256);
			static CButtonContainer s_BtnNumMinus, s_BtnNumPlus;
			if(DoButton_Menu(&s_BtnNumMinus, "-", 0, &NumMinus))
				g_Config.m_AcCustomAspectRatioNum = std::clamp(g_Config.m_AcCustomAspectRatioNum - 1, 1, 256);
			if(DoButton_Menu(&s_BtnNumPlus, "+", 0, &NumPlus))
				g_Config.m_AcCustomAspectRatioNum = std::clamp(g_Config.m_AcCustomAspectRatioNum + 1, 1, 256);

			// Height (Denominator)
			Column.HSplitTop(LineSize, &Button, &Column);
			CUIRect DenMinus, DenPlus;
			Button.VSplitRight(20.0f, &Button, &DenPlus);
			Button.VSplitRight(2.0f, &Button, nullptr);
			Button.VSplitRight(20.0f, &Button, &DenMinus);
			Button.VSplitRight(4.0f, &Button, nullptr);
			Ui()->DoScrollbarOption(&g_Config.m_AcCustomAspectRatioDen, &g_Config.m_AcCustomAspectRatioDen, &Button, TCLocalize("Alto (Denominador)"), 1, 256);
			static CButtonContainer s_BtnDenMinus, s_BtnDenPlus;
			if(DoButton_Menu(&s_BtnDenMinus, "-", 0, &DenMinus))
				g_Config.m_AcCustomAspectRatioDen = std::clamp(g_Config.m_AcCustomAspectRatioDen - 1, 1, 256);
			if(DoButton_Menu(&s_BtnDenPlus, "+", 0, &DenPlus))
				g_Config.m_AcCustomAspectRatioDen = std::clamp(g_Config.m_AcCustomAspectRatioDen + 1, 1, 256);

			// Info label showing exact aspect ratio
			Column.HSplitTop(LineSize, &Button, &Column);
			char aRatioStr[64];
			const float Ratio = (float)g_Config.m_AcCustomAspectRatioNum / (float)std::max(1, g_Config.m_AcCustomAspectRatioDen);
			str_format(aRatioStr, sizeof(aRatioStr), "%s: %d:%d (%.3f:1)", TCLocalize("Proporcion resultante"), g_Config.m_AcCustomAspectRatioNum, g_Config.m_AcCustomAspectRatioDen, Ratio);
			TextRender()->TextColor(ColorRGBA(0.7f, 0.7f, 0.7f, 1.0f));
			Ui()->DoLabel(&Button, aRatioStr, FontSize * 0.9f, TEXTALIGN_ML);
			TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
		}

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoCheckboxWithHelp(&g_Config.m_AcCustomAspectRatioBlackBars, TCLocalize("Bandas negras (Black Bars) en vez de estirado"), TCLocalize("Activa bandas negras laterales o superiores sin deformar la imagen. Desactivalo para estirar la imagen a pantalla completa (Stretched)."), &g_Config.m_AcCustomAspectRatioBlackBars, &Column, LineSize);
		DoCheckboxWithHelp(&g_Config.m_AcCustomAspectRatioIngameOnly, TCLocalize("Solo dentro de partida (menus en pantalla completa)"), TCLocalize("Mantiene los menus y el editor de mapas en la relacion nativa del monitor sin recargar texturas al tocar Escape."), &g_Config.m_AcCustomAspectRatioIngameOnly, &Column, LineSize);
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	s_SectionBoxesAc.back().h = Column.y - s_SectionBoxesAc.back().y;

	// ***** Hook Glow & Bloque Hookeado ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxesAc.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	CUIRect HeaderLabel = Label;
	CUIRect BetaBadge;
	HeaderLabel.VSplitRight(44.0f, &HeaderLabel, &BetaBadge);
	Ui()->DoLabel(&HeaderLabel, TCLocalize("Hook Glow / Bloque Hookeado"), HeadlineFontSize, TEXTALIGN_ML);

	const float BadgeHeight = std::min(16.0f, BetaBadge.h);
	BetaBadge.HMargin((BetaBadge.h - BadgeHeight) * 0.5f, &BetaBadge);
	BetaBadge.Draw(ColorRGBA(0.95f, 0.55f, 0.15f, 0.75f), IGraphics::CORNER_ALL, 4.0f);
	CUIRect BetaInner = BetaBadge;
	BetaInner.Margin(1.0f, &BetaInner);
	BetaInner.Draw(ColorRGBA(0.12f, 0.13f, 0.16f, 0.95f), IGraphics::CORNER_ALL, 3.0f);
	TextRender()->TextColor(ColorRGBA(1.0f, 0.68f, 0.22f, 1.0f));
	Ui()->DoLabel(&BetaInner, "BETA", 10.0f, TEXTALIGN_MC);
	TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoCheckboxWithHelp(&g_Config.m_AcHookedTile, TCLocalize("Iluminar bloque al hookear"), TCLocalize("Resalta e ilumina visualmente el bloque que estas hookeando con un efecto luminoso."), &g_Config.m_AcHookedTile, &Column, LineSize);
	if(g_Config.m_AcHookedTile)
	{
		DoCheckboxWithHelp(&g_Config.m_AcHookedTileGlow, TCLocalize("Resplandor luminoso (Glow halo)"), TCLocalize("Dibuja un halo de luz brillante y suave alrededor del bloque enganchado."), &g_Config.m_AcHookedTileGlow, &Column, LineSize);
		DoCheckboxWithHelp(&g_Config.m_AcHookedTileRgb, TCLocalize("Modo Rainbow RGB animado"), TCLocalize("Hace rotar los colores del bloque hookeado a traves del espectro arcoiris en tiempo real."), &g_Config.m_AcHookedTileRgb, &Column, LineSize);
		if(g_Config.m_AcHookedTileRgb)
		{
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_AcHookedTileSpeed, &g_Config.m_AcHookedTileSpeed, &Button, TCLocalize("Velocidad Rainbow RGB"), 1, 100);
		}
		else
		{
			static CButtonContainer s_HookedTileColorId;
			DoLine_ColorPicker(&s_HookedTileColorId, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Color del bloque hookeado"), &g_Config.m_AcHookedTileColor, ColorRGBA(0.0f, 0.0f, 0.0f), false);
		}

		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_AcHookedTileOpacity, &g_Config.m_AcHookedTileOpacity, &Button, TCLocalize("Opacidad del bloque (%)"), 5, 100);

		DoCheckboxWithHelp(&g_Config.m_AcHookedTileOthers, TCLocalize("Iluminar bloques de otros jugadores"), TCLocalize("Muestra tambien el efecto luminoso cuando otros jugadores enganchan un bloque."), &g_Config.m_AcHookedTileOthers, &Column, LineSize);
	}

	DoCheckboxWithHelp(&g_Config.m_AcShowRemoteEntitiesOnHook, TCLocalize("Mostrar entidades de otros al hookear (Supabase)"), TCLocalize("Cuando otro jugador hookea un bloque, ese bloque se renderiza temporalmente con el pack de entidades que dicho jugador tiene activo."), &g_Config.m_AcShowRemoteEntitiesOnHook, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_ClDownloadEntities, TCLocalize("Descargar entidades remotas automaticamente"), TCLocalize("Descarga e instala de fondo el pack de entidades de otros jugadores desde GitHub para mostrarlas de inmediato."), &g_Config.m_ClDownloadEntities, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_AcScoreboardGameSkinActions, TCLocalize("Opciones de Gameskin/Entities en Tab"), TCLocalize("Muestra botones en el menu contextual del Scoreboard (Tab) para copiar u ocultar gameskins y entities de otros jugadores."), &g_Config.m_AcScoreboardGameSkinActions, &Column, LineSize);

	static CLineInput s_DummyEntitiesInput;
	Column.HSplitTop(LineSize, &Button, &Column);
	DoEditBoxWithLabel(&s_DummyEntitiesInput, &Button, TCLocalize("Entities del Dummy"), "", g_Config.m_ClDummyAssetEntities, sizeof(g_Config.m_ClDummyAssetEntities));

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	s_SectionBoxesAc.back().h = Column.y - s_SectionBoxesAc.back().y;
	}

	if(VisualTab)
	{
	// ***** Bloque: Fondos Multimedia (Menu & In-Game) ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxesAc.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Fondos Multimedia"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	auto ReloadAcMediaFromConfig = [&]() {
		CMenuMediaBackground &MediaBackground = GameClient()->m_MenuMediaBackground;
		if(Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK)
			MediaBackground.ReloadFromConfig(g_Config.m_AcGameMediaBackground, g_Config.m_AcMenuMediaBackgroundPath);
		else
			MediaBackground.ReloadFromConfig(g_Config.m_AcMenuMediaBackground, g_Config.m_AcMenuMediaBackgroundPath);
	};

	const bool MenuMediaChanged = DoCheckboxWithHelp(&g_Config.m_AcMenuMediaBackground, TCLocalize("Activar en menu principal"), TCLocalize("Reemplaza el fondo del menu principal con una imagen, GIF o video (mp4/webm/etc.)."), &g_Config.m_AcMenuMediaBackground, &Column, LineSize);
	const bool GameMediaChanged = DoCheckboxWithHelp(&g_Config.m_AcGameMediaBackground, TCLocalize("Activar en fondo del juego"), TCLocalize("Muestra el fondo multimedia detras del mapa cuando Overlay Entities es mayor a 0."), &g_Config.m_AcGameMediaBackground, &Column, LineSize);
	if(MenuMediaChanged || GameMediaChanged)
		ReloadAcMediaFromConfig();

	{
		static std::vector<std::string> s_vMediaFileLabels;
		static std::vector<std::string> s_vMediaFilePaths;
		static std::vector<std::string> s_vMediaDropDownLabels;
		static std::vector<const char *> s_vMediaDropDownPtrs;
		static std::vector<int> s_vSortedIndices;
		static int64_t s_LastMediaScanTick = 0;

		auto ScanMediaFolder = [&]() {
			struct SScanCtx
			{
				std::vector<std::string> *m_pLabels;
				std::vector<std::string> *m_pPaths;
			};
			auto ScanCb = [](const char *pName, int IsDir, int StorageType, void *pUser) {
				(void)StorageType;
				if(IsDir)
					return 0;
				auto *pCtx = static_cast<SScanCtx *>(pUser);
				const std::string Ext = MediaDecoder::ExtractExtensionLower(pName);
				const bool SupportedImage = Ext == "png" || Ext == "jpg" || Ext == "jpeg" || Ext == "webp" || Ext == "bmp" || Ext == "avif" || Ext == "gif";
				const bool SupportedVideo = Ext == "mp4" || Ext == "webm" || Ext == "mov" || Ext == "m4v" || Ext == "mkv" || Ext == "avi";
				if(!SupportedImage && !SupportedVideo)
					return 0;
				pCtx->m_pLabels->emplace_back(pName);
				pCtx->m_pPaths->emplace_back(std::string("AClient/backgrounds/") + pName);
				return 0;
			};

			Storage()->CreateFolder("AClient", IStorage::TYPE_SAVE);
			Storage()->CreateFolder("AClient/backgrounds", IStorage::TYPE_SAVE);
			s_vMediaFileLabels.clear();
			s_vMediaFilePaths.clear();
			SScanCtx Ctx{&s_vMediaFileLabels, &s_vMediaFilePaths};
			Storage()->ListDirectory(IStorage::TYPE_SAVE, "AClient/backgrounds", ScanCb, &Ctx);

			s_vSortedIndices.resize(s_vMediaFileLabels.size());
			for(size_t i = 0; i < s_vSortedIndices.size(); ++i)
				s_vSortedIndices[i] = (int)i;
			std::sort(s_vSortedIndices.begin(), s_vSortedIndices.end(), [&](int L, int R) {
				return str_comp_nocase(s_vMediaFileLabels[L].c_str(), s_vMediaFileLabels[R].c_str()) < 0;
			});

			s_vMediaDropDownLabels.clear();
			s_vMediaDropDownPtrs.clear();
			s_vMediaDropDownLabels.reserve(s_vSortedIndices.size());
			s_vMediaDropDownPtrs.reserve(s_vSortedIndices.size());
			for(int Idx : s_vSortedIndices)
				s_vMediaDropDownLabels.push_back(s_vMediaFileLabels[Idx]);
			for(const std::string &Lbl : s_vMediaDropDownLabels)
				s_vMediaDropDownPtrs.push_back(Lbl.c_str());
			s_LastMediaScanTick = time_get();
		};

		const int64_t NowTick = time_get();
		if(s_LastMediaScanTick == 0 || NowTick - s_LastMediaScanTick > time_freq() * 2)
			ScanMediaFolder();

		int SelectedMediaFile = -1;
		for(size_t i = 0; i < s_vSortedIndices.size(); ++i)
		{
			if(str_comp(g_Config.m_AcMenuMediaBackgroundPath, s_vMediaFilePaths[s_vSortedIndices[i]].c_str()) == 0)
			{
				SelectedMediaFile = (int)i;
				break;
			}
		}

		CUIRect MediaRow, MediaDropDown, MediaReloadBtn, MediaFolderBtn;
		Column.HSplitTop(LineSize, &MediaRow, &Column);
		MediaRow.VSplitRight(20.0f, &MediaRow, &MediaFolderBtn);
		MediaRow.VSplitRight(MarginSmall, &MediaRow, nullptr);
		MediaRow.VSplitRight(20.0f, &MediaRow, &MediaReloadBtn);
		MediaRow.VSplitRight(MarginSmall, &MediaRow, nullptr);
		MediaDropDown = MediaRow;

		if(s_vMediaDropDownPtrs.empty())
		{
			static CButtonContainer s_MediaEmptyBtn;
			DoButton_Menu(&s_MediaEmptyBtn, TCLocalize("Sin archivos en AClient/backgrounds"), -1, &MediaDropDown);
		}
		else
		{
			static CUi::SDropDownState s_MediaDropDownState;
			static CScrollRegion s_MediaDropDownScroll;
			s_MediaDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_MediaDropDownScroll;
			const int NewSel = Ui()->DoDropDown(&MediaDropDown, SelectedMediaFile, s_vMediaDropDownPtrs.data(), s_vMediaDropDownPtrs.size(), s_MediaDropDownState);
			if(NewSel != SelectedMediaFile && NewSel >= 0 && NewSel < (int)s_vSortedIndices.size())
			{
				str_copy(g_Config.m_AcMenuMediaBackgroundPath, s_vMediaFilePaths[s_vSortedIndices[NewSel]].c_str(), sizeof(g_Config.m_AcMenuMediaBackgroundPath));
				ReloadAcMediaFromConfig();
			}
		}

		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT);
		static CButtonContainer s_MediaReloadBtnId;
		if(DoButton_Menu(&s_MediaReloadBtnId, FontIcon::ARROW_ROTATE_RIGHT, 0, &MediaReloadBtn))
		{
			ScanMediaFolder();
			ReloadAcMediaFromConfig();
		}
		GameClient()->m_Tooltips.DoToolTip(&s_MediaReloadBtnId, &MediaReloadBtn, TCLocalize("Recargar fondo multimedia"));

		static CButtonContainer s_MediaFolderBtnId;
		if(DoButton_Menu(&s_MediaFolderBtnId, FontIcon::FOLDER, 0, &MediaFolderBtn))
		{
			Storage()->CreateFolder("AClient", IStorage::TYPE_SAVE);
			Storage()->CreateFolder("AClient/backgrounds", IStorage::TYPE_SAVE);
			char aFolderPath[IO_MAX_PATH_LENGTH];
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, "AClient/backgrounds", aFolderPath, sizeof(aFolderPath));
			Client()->ViewFile(aFolderPath);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_MediaFolderBtnId, &MediaFolderBtn, TCLocalize("Abrir carpeta AClient/backgrounds"));
		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	{
		CUIRect OffsetRow;
		Column.HSplitTop(LineSize, &OffsetRow, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_AcGameMediaBackgroundOffset, &g_Config.m_AcGameMediaBackgroundOffset, &OffsetRow, TCLocalize("Parallax del mapa"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
		GameClient()->m_Tooltips.DoToolTip(&g_Config.m_AcGameMediaBackgroundOffset, &OffsetRow, TCLocalize("0 mantiene el fondo fijo. Valores mayores agregan parallax al mover la camara."));
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	{
		CUIRect StatusRow;
		Column.HSplitTop(LineSize, &StatusRow, &Column);
		const bool InGame = Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK;
		const CMenuMediaBackground &MediaSrc = GameClient()->m_MenuMediaBackground;
		const char *pStatusText = MediaSrc.StatusText();
		char aStatusBuf[256];
		if(InGame && MediaSrc.IsLoaded() && g_Config.m_AcGameMediaBackground && g_Config.m_ClOverlayEntities == 0)
		{
			str_format(aStatusBuf, sizeof(aStatusBuf), "%s %s", pStatusText, TCLocalize("(Oculto: Overlay Entities es 0)"));
			pStatusText = aStatusBuf;
			TextRender()->TextColor(ColorRGBA(1.0f, 0.85f, 0.45f, 1.0f));
		}
		else if(MediaSrc.HasError())
			TextRender()->TextColor(ColorRGBA(1.0f, 0.45f, 0.45f, 1.0f));
		else if(MediaSrc.IsLoaded())
			TextRender()->TextColor(ColorRGBA(0.55f, 1.0f, 0.55f, 1.0f));
		Ui()->DoLabel(&StatusRow, pStatusText, 11.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	s_SectionBoxesAc.back().h = Column.y - s_SectionBoxesAc.back().y;

	// ***** Bloque: Gradientes de Texto ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxesAc.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Gradientes de Texto"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoCheckboxWithHelp(&g_Config.m_AcNameplateGradient, TCLocalize("Gradiente en nicks"), TCLocalize("Aplica gradiente animado a los nombres en nameplates y scoreboard."), &g_Config.m_AcNameplateGradient, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_AcNameplateGradientClan, TCLocalize("Gradiente en clanes"), TCLocalize("Aplica gradiente animado a los clanes en nameplates y scoreboard."), &g_Config.m_AcNameplateGradientClan, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_AcNameplateGradientSkin, TCLocalize("Gradiente en skin (cuerpo y pies)"), TCLocalize("Anima el color del cuerpo y pies del tee con los colores del gradiente."), &g_Config.m_AcNameplateGradientSkin, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_AcNameplateGradientEverything, TCLocalize("Gradiente en todo el texto (UI/HUD)"), TCLocalize("Aplica el gradiente a todos los textos renderizados de la interfaz y HUD."), &g_Config.m_AcNameplateGradientEverything, &Column, LineSize);

	{
		CUIRect SpeedRow;
		Column.HSplitTop(LineSize, &SpeedRow, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_AcNameplateGradientAnimateSpeed, &g_Config.m_AcNameplateGradientAnimateSpeed, &SpeedRow, TCLocalize("Velocidad de animacion"), 1, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
	}
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	{
		CUIRect ModeRow, BtnSkin, BtnCustom, BtnRainbow;
		Column.HSplitTop(LineSize, &ModeRow, &Column);
		ModeRow.VSplitLeft(ModeRow.w / 3.0f, &BtnSkin, &ModeRow);
		ModeRow.VSplitMid(&BtnCustom, &BtnRainbow);
		g_Config.m_AcNameplateGradientMode = std::clamp(g_Config.m_AcNameplateGradientMode, 0, 2);
		static CButtonContainer s_GradModeSkin, s_GradModeCustom, s_GradModeRainbow;
		if(DoButton_Menu(&s_GradModeSkin, TCLocalize("Skin"), g_Config.m_AcNameplateGradientMode == AC_GRADIENT_MODE_SKIN, &BtnSkin, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
			g_Config.m_AcNameplateGradientMode = AC_GRADIENT_MODE_SKIN;
		if(DoButton_Menu(&s_GradModeCustom, TCLocalize("Custom"), g_Config.m_AcNameplateGradientMode == AC_GRADIENT_MODE_CUSTOM, &BtnCustom, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
			g_Config.m_AcNameplateGradientMode = AC_GRADIENT_MODE_CUSTOM;
		if(DoButton_Menu(&s_GradModeRainbow, TCLocalize("Rainbow"), g_Config.m_AcNameplateGradientMode == AC_GRADIENT_MODE_RAINBOW, &BtnRainbow, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
			g_Config.m_AcNameplateGradientMode = AC_GRADIENT_MODE_RAINBOW;
	}
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	{
		CUIRect TargetRow, BtnOwn, BtnOthers, BtnAll;
		Column.HSplitTop(LineSize, &TargetRow, &Column);
		TargetRow.VSplitLeft(TargetRow.w / 3.0f, &BtnOwn, &TargetRow);
		TargetRow.VSplitMid(&BtnOthers, &BtnAll);
		g_Config.m_AcNameplateGradientTarget = std::clamp(g_Config.m_AcNameplateGradientTarget, 0, 2);
		static CButtonContainer s_GradTargetOwn, s_GradTargetOthers, s_GradTargetAll;
		if(DoButton_Menu(&s_GradTargetOwn, TCLocalize("Solo yo"), g_Config.m_AcNameplateGradientTarget == AC_GRADIENT_TARGET_OWN, &BtnOwn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
			g_Config.m_AcNameplateGradientTarget = AC_GRADIENT_TARGET_OWN;
		if(DoButton_Menu(&s_GradTargetOthers, TCLocalize("Otros"), g_Config.m_AcNameplateGradientTarget == AC_GRADIENT_TARGET_OTHERS, &BtnOthers, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
			g_Config.m_AcNameplateGradientTarget = AC_GRADIENT_TARGET_OTHERS;
		if(DoButton_Menu(&s_GradTargetAll, TCLocalize("Todos"), g_Config.m_AcNameplateGradientTarget == AC_GRADIENT_TARGET_ALL, &BtnAll, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
			g_Config.m_AcNameplateGradientTarget = AC_GRADIENT_TARGET_ALL;
	}

	if(g_Config.m_AcNameplateGradientMode == AC_GRADIENT_MODE_CUSTOM)
	{
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		CUIRect CountRow, Btn2, Btn3, Btn4;
		Column.HSplitTop(LineSize, &CountRow, &Column);
		CountRow.VSplitLeft(CountRow.w / 3.0f, &Btn2, &CountRow);
		CountRow.VSplitMid(&Btn3, &Btn4);
		g_Config.m_AcNameplateGradientColorCount = std::clamp(g_Config.m_AcNameplateGradientColorCount, 2, 4);
		static CButtonContainer s_GradCnt2, s_GradCnt3, s_GradCnt4;
		if(DoButton_Menu(&s_GradCnt2, "2 colores", g_Config.m_AcNameplateGradientColorCount == 2, &Btn2, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
			g_Config.m_AcNameplateGradientColorCount = 2;
		if(DoButton_Menu(&s_GradCnt3, "3 colores", g_Config.m_AcNameplateGradientColorCount == 3, &Btn3, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
			g_Config.m_AcNameplateGradientColorCount = 3;
		if(DoButton_Menu(&s_GradCnt4, "4 colores", g_Config.m_AcNameplateGradientColorCount == 4, &Btn4, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
			g_Config.m_AcNameplateGradientColorCount = 4;

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		static CButtonContainer s_GradCol1, s_GradCol2, s_GradCol3, s_GradCol4;
		DoLine_ColorPicker(&s_GradCol1, LineSize, 13.0f, MarginSmall, &Column, TCLocalize("Color 1"), &g_Config.m_AcNameplateGradientColor1, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::AcNameplateGradientColor1, true)), false);
		DoLine_ColorPicker(&s_GradCol2, LineSize, 13.0f, MarginSmall, &Column, TCLocalize("Color 2"), &g_Config.m_AcNameplateGradientColor2, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::AcNameplateGradientColor2, true)), false);
		if(g_Config.m_AcNameplateGradientColorCount >= 3)
			DoLine_ColorPicker(&s_GradCol3, LineSize, 13.0f, MarginSmall, &Column, TCLocalize("Color 3"), &g_Config.m_AcNameplateGradientColor3, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::AcNameplateGradientColor3, true)), false);
		if(g_Config.m_AcNameplateGradientColorCount >= 4)
			DoLine_ColorPicker(&s_GradCol4, LineSize, 13.0f, MarginSmall, &Column, TCLocalize("Color 4"), &g_Config.m_AcNameplateGradientColor4, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::AcNameplateGradientColor4, true)), false);
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	s_SectionBoxesAc.back().h = Column.y - s_SectionBoxesAc.back().y;

	// ***** Bloque: Keystrokes (Teclado + Mouse) ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxesAc.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	{
		CUIRect HeaderLabel = Label;
		CUIRect BetaBadge;
		HeaderLabel.VSplitRight(44.0f, &HeaderLabel, &BetaBadge);
		Ui()->DoLabel(&HeaderLabel, TCLocalize("Keystrokes Separado (Teclado + Mouse)"), HeadlineFontSize, TEXTALIGN_ML);

		const float BadgeHeight = std::min(16.0f, BetaBadge.h);
		BetaBadge.HMargin((BetaBadge.h - BadgeHeight) * 0.5f, &BetaBadge);
		BetaBadge.Draw(ColorRGBA(0.95f, 0.55f, 0.15f, 0.75f), IGraphics::CORNER_ALL, 4.0f);
		CUIRect BetaInner = BetaBadge;
		BetaInner.Margin(1.0f, &BetaInner);
		BetaInner.Draw(ColorRGBA(0.12f, 0.13f, 0.16f, 0.95f), IGraphics::CORNER_ALL, 3.0f);
		TextRender()->TextColor(ColorRGBA(1.0f, 0.68f, 0.22f, 1.0f));
		Ui()->DoLabel(&BetaInner, "BETA", 10.0f, TEXTALIGN_MC);
		TextRender()->TextColor(ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f));
	}
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	{
		CUIRect StyleRow, BtnClassic, BtnMc, BtnDdnet;
		Column.HSplitTop(LineSize, &StyleRow, &Column);
		StyleRow.VSplitLeft(StyleRow.w / 3.0f, &BtnClassic, &StyleRow);
		StyleRow.VSplitMid(&BtnMc, &BtnDdnet);
		g_Config.m_AcKeystrokesStyle = std::clamp(g_Config.m_AcKeystrokesStyle, 0, 2);
		static CButtonContainer s_KsStyleClassic, s_KsStyleMc, s_KsStyleDdnet;
		if(DoButton_Menu(&s_KsStyleClassic, TCLocalize("Clasico"), g_Config.m_AcKeystrokesStyle == 0, &BtnClassic, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
			g_Config.m_AcKeystrokesStyle = 0;
		if(DoButton_Menu(&s_KsStyleMc, TCLocalize("Minecraft"), g_Config.m_AcKeystrokesStyle == 1, &BtnMc, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
			g_Config.m_AcKeystrokesStyle = 1;
		if(DoButton_Menu(&s_KsStyleDdnet, "DDNet", g_Config.m_AcKeystrokesStyle == 2, &BtnDdnet, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
			g_Config.m_AcKeystrokesStyle = 2;
	}
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	if(DoCheckboxWithHelp(&g_Config.m_AcKeystrokesKeyboard, TCLocalize("Mostrar Teclado (HUD)"), TCLocalize("Muestra el modulo independiente de teclado, movible en el Editor de HUD."), &g_Config.m_AcKeystrokesKeyboard, &Column, LineSize))
		HudLayout::SetEnabled(HudLayout::MODULE_KEYSTROKES_KEYBOARD, g_Config.m_AcKeystrokesKeyboard != 0);
	if(g_Config.m_AcKeystrokesStyle == 0 && g_Config.m_AcKeystrokesKeyboard)
	{
		CUIRect KbRow, BtnMin, BtnFull, BtnMicro;
		Column.HSplitTop(LineSize, &KbRow, &Column);
		KbRow.VSplitLeft(KbRow.w / 3.0f, &BtnMin, &KbRow);
		KbRow.VSplitMid(&BtnFull, &BtnMicro);
		g_Config.m_AcKeystrokesKeyboardPreset = std::clamp(g_Config.m_AcKeystrokesKeyboardPreset, 0, 2);
		static CButtonContainer s_KbPresetMin, s_KbPresetFull, s_KbPresetMicro;
		if(DoButton_Menu(&s_KbPresetMin, "Minimal", g_Config.m_AcKeystrokesKeyboardPreset == 0, &BtnMin, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
		{
			g_Config.m_AcKeystrokesKeyboardPreset = 0;
			HudLayout::ResetPosition(HudLayout::MODULE_KEYSTROKES_MOUSE);
		}
		if(DoButton_Menu(&s_KbPresetFull, "Full", g_Config.m_AcKeystrokesKeyboardPreset == 1, &BtnFull, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
		{
			g_Config.m_AcKeystrokesKeyboardPreset = 1;
			HudLayout::ResetPosition(HudLayout::MODULE_KEYSTROKES_MOUSE);
		}
		if(DoButton_Menu(&s_KbPresetMicro, "Micro", g_Config.m_AcKeystrokesKeyboardPreset == 2, &BtnMicro, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
		{
			g_Config.m_AcKeystrokesKeyboardPreset = 2;
			HudLayout::ResetPosition(HudLayout::MODULE_KEYSTROKES_MOUSE);
		}
		Column.HSplitTop(MarginSmall, nullptr, &Column);
	}

	if(g_Config.m_AcKeystrokesStyle == 0)
	{
		if(DoCheckboxWithHelp(&g_Config.m_AcKeystrokesMouse, TCLocalize("Mostrar Mouse (HUD)"), TCLocalize("Muestra el modulo independiente de mouse, movible en el Editor de HUD."), &g_Config.m_AcKeystrokesMouse, &Column, LineSize))
			HudLayout::SetEnabled(HudLayout::MODULE_KEYSTROKES_MOUSE, g_Config.m_AcKeystrokesMouse != 0);
		if(g_Config.m_AcKeystrokesMouse)
		{
			CUIRect MsRow, BtnArrow, BtnDot, BtnClean;
			Column.HSplitTop(LineSize, &MsRow, &Column);
			MsRow.VSplitLeft(MsRow.w / 3.0f, &BtnArrow, &MsRow);
			MsRow.VSplitMid(&BtnDot, &BtnClean);
			g_Config.m_AcKeystrokesMousePreset = std::clamp(g_Config.m_AcKeystrokesMousePreset, 1, 3);
			static CButtonContainer s_MsPresetArrow, s_MsPresetDot, s_MsPresetClean;
			if(DoButton_Menu(&s_MsPresetArrow, "Arrow", g_Config.m_AcKeystrokesMousePreset == 1, &BtnArrow, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_AcKeystrokesMousePreset = 1;
			if(DoButton_Menu(&s_MsPresetDot, "Dot", g_Config.m_AcKeystrokesMousePreset == 2, &BtnDot, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
				g_Config.m_AcKeystrokesMousePreset = 2;
			if(DoButton_Menu(&s_MsPresetClean, "Simple", g_Config.m_AcKeystrokesMousePreset == 3, &BtnClean, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_AcKeystrokesMousePreset = 3;
			Column.HSplitTop(MarginSmall, nullptr, &Column);
		}
	}

	if(g_Config.m_AcKeystrokesStyle == 1)
	{
		CUIRect McLayoutRow, BtnMcFull, BtnMcAd;
		Column.HSplitTop(LineSize, &McLayoutRow, &Column);
		McLayoutRow.VSplitMid(&BtnMcFull, &BtnMcAd);
		g_Config.m_AcKeystrokesMcLayout = std::clamp(g_Config.m_AcKeystrokesMcLayout, 0, 1);
		static CButtonContainer s_McFull, s_McAd;
		if(DoButton_Menu(&s_McFull, "MC: WASD", g_Config.m_AcKeystrokesMcLayout == 0, &BtnMcFull, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
			g_Config.m_AcKeystrokesMcLayout = 0;
		if(DoButton_Menu(&s_McAd, "MC: Solo A/D", g_Config.m_AcKeystrokesMcLayout == 1, &BtnMcAd, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
			g_Config.m_AcKeystrokesMcLayout = 1;
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		if(g_Config.m_AcKeystrokesMcLayout == 1)
		{
			DoCheckboxWithHelp(&g_Config.m_AcKeystrokesMcShowLmb, TCLocalize("Mostrar LMB (Click Izq)"), TCLocalize("Muestra el boton LMB en el layout A/D de Minecraft."), &g_Config.m_AcKeystrokesMcShowLmb, &Column, LineSize);
			DoCheckboxWithHelp(&g_Config.m_AcKeystrokesMcShowRmb, TCLocalize("Mostrar RMB (Click Der)"), TCLocalize("Muestra el boton RMB en el layout A/D de Minecraft."), &g_Config.m_AcKeystrokesMcShowRmb, &Column, LineSize);
			DoCheckboxWithHelp(&g_Config.m_AcKeystrokesMcShowSpace, TCLocalize("Mostrar Space"), TCLocalize("Muestra la barra Space en el layout A/D de Minecraft."), &g_Config.m_AcKeystrokesMcShowSpace, &Column, LineSize);
		}

	}

	if(g_Config.m_AcKeystrokesStyle == 1 || g_Config.m_AcKeystrokesStyle == 2)
	{
		CUIRect OpacityRow;
		Column.HSplitTop(LineSize, &OpacityRow, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_AcKeystrokesMcPressedOpacity, &g_Config.m_AcKeystrokesMcPressedOpacity, &OpacityRow, TCLocalize("Opacidad tecla presionada"), 0, 100, &CUi::ms_LinearScrollbarScale, 0u, "%");
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	s_SectionBoxesAc.back().h = Column.y - s_SectionBoxesAc.back().y;
	}

	// ***** Right Column: HUD, Social, Chat & Alerts ***** //
	LeftView = Column;
	Column = RightView;

	if(VisualTab)
	{
	// ***** Overlay RaceStats ***** //
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxesAc.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Overlay & HUD Editor"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	{
		CUIRect BtnEditorRect;
		Column.HSplitTop(LineSize + 4.0f, &BtnEditorRect, &Column);
		static CButtonContainer s_BtnOpenHudEditor;
		if(DoButton_Menu(&s_BtnOpenHudEditor, TCLocalize("Abrir Editor de HUD Interactivo (BestClient Style)"), 0, &BtnEditorRect))
		{
			SetActive(false);
			GameClient()->m_HudEditor.Activate();
		}
	}
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoCheckboxWithHelp(&g_Config.m_AcMiniStatboard, TCLocalize("Enable Mini Statboard HUD"), TCLocalize("Shows the compact live team statistics table on your screen during the game."), &g_Config.m_AcMiniStatboard, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_AcStatboardBackgroundCalc, TCLocalize("Calculate stats in background"), TCLocalize("Keep tracking freezes, hammer saves and deaths even when the overlay is hidden."), &g_Config.m_AcStatboardBackgroundCalc, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_AcStatboardKeepOnDeath, TCLocalize("Keep stats on death (do not reset on /kill)"), TCLocalize("Keeps your accumulated statistics intact even if you or a teammate dies or uses /kill."), &g_Config.m_AcStatboardKeepOnDeath, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_AcStatboardResetOnTeamKill, TCLocalize("Reset stats on /kill or /team change"), TCLocalize("Automatically resets statistics whenever the team dies or you switch rooms (/team)."), &g_Config.m_AcStatboardResetOnTeamKill, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_AcStatboardAutoSave, TCLocalize("Auto-save stats before reset (/kill or /team)"), TCLocalize("Automatically saves current run stats to RaceStats history file before resetting."), &g_Config.m_AcStatboardAutoSave, &Column, LineSize);

	static CButtonContainer s_ReaderButtonToggleStatsAc, s_ClearButtonToggleStatsAc;
	DoLine_KeyReader(Column, s_ReaderButtonToggleStatsAc, s_ClearButtonToggleStatsAc, TCLocalize("Toggle stats overlay key"), "toggle ac_mini_statboard 0 1");
	static CButtonContainer s_ReaderButtonSaveStatsAc, s_ClearButtonSaveStatsAc;
	DoLine_KeyReader(Column, s_ReaderButtonSaveStatsAc, s_ClearButtonSaveStatsAc, TCLocalize("Save stats key"), "ac_save_racestats");

	CUIRect PresetRow, BtnAllCols, BtnBasicCols;
	Column.HSplitTop(LineSize, &PresetRow, &Column);
	PresetRow.VSplitMid(&BtnAllCols, &BtnBasicCols, MarginSmall);
	static CButtonContainer s_BtnAllColsId, s_BtnBasicColsId;
	if(DoButton_Menu(&s_BtnAllColsId, TCLocalize("Show All Stats"), 0, &BtnAllCols))
		g_Config.m_AcMiniStatboardColumns = 255;
	if(DoButton_Menu(&s_BtnBasicColsId, TCLocalize("Basic Stats"), 0, &BtnBasicCols))
		g_Config.m_AcMiniStatboardColumns = 27;
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static int s_ColFreezesAc = 0;
	s_ColFreezesAc = (g_Config.m_AcMiniStatboardColumns & 1) ? 1 : 0;
	if(DoCheckboxWithHelp(&s_ColFreezesAc, TCLocalize("Column: Freezes"), TCLocalize("Shows how many times each player was frozen during the current run."), &s_ColFreezesAc, &Column, LineSize))
		g_Config.m_AcMiniStatboardColumns ^= 1;

	static int s_ColSavesAc = 0;
	s_ColSavesAc = (g_Config.m_AcMiniStatboardColumns & 2) ? 1 : 0;
	if(DoCheckboxWithHelp(&s_ColSavesAc, TCLocalize("Column: Saves (Hammer unfreeze)"), TCLocalize("Shows how many times each player unfroze a teammate using the hammer."), &s_ColSavesAc, &Column, LineSize))
		g_Config.m_AcMiniStatboardColumns ^= 2;

	static int s_ColDeathsAc = 0;
	s_ColDeathsAc = (g_Config.m_AcMiniStatboardColumns & 16) ? 1 : 0;
	if(DoCheckboxWithHelp(&s_ColDeathsAc, TCLocalize("Column: Deaths"), TCLocalize("Shows the number of deaths or /kill for each player during the current run."), &s_ColDeathsAc, &Column, LineSize))
		g_Config.m_AcMiniStatboardColumns ^= 16;

	static int s_ColEffAc = 0;
	s_ColEffAc = (g_Config.m_AcMiniStatboardColumns & 8) ? 1 : 0;
	if(DoCheckboxWithHelp(&s_ColEffAc, TCLocalize("Column: Efficiency ratio"), TCLocalize("Save success rate: Saves / (Saves + Freezes) %. Dynamically shifts colors from Red (0%) to Emerald Green (100%)."), &s_ColEffAc, &Column, LineSize))
		g_Config.m_AcMiniStatboardColumns ^= 8;

	static int s_ColNetAc = 0;
	s_ColNetAc = (g_Config.m_AcMiniStatboardColumns & 32) ? 1 : 0;
	if(DoCheckboxWithHelp(&s_ColNetAc, TCLocalize("Column: Net Balance (+/-)"), TCLocalize("Direct difference between Saves and Freezes (Saves - Freezes). Green if positive (+), Gray if zero, Red if negative (-)."), &s_ColNetAc, &Column, LineSize))
		g_Config.m_AcMiniStatboardColumns ^= 32;

	static int s_ColTimeAc = 0;
	s_ColTimeAc = (g_Config.m_AcMiniStatboardColumns & 64) ? 1 : 0;
	if(DoCheckboxWithHelp(&s_ColTimeAc, TCLocalize("Column: Frozen Time"), TCLocalize("Total accumulated time the player spent stuck inside freeze during the run (e.g. 15s or 1m 24s)."), &s_ColTimeAc, &Column, LineSize))
		g_Config.m_AcMiniStatboardColumns ^= 64;

	static int s_ColHookAc = 0;
	s_ColHookAc = (g_Config.m_AcMiniStatboardColumns & 128) ? 1 : 0;
	if(DoCheckboxWithHelp(&s_ColHookAc, TCLocalize("Column: Hook Accuracy"), TCLocalize("Hook hit accuracy: hooks that latched onto a player vs total hooks fired."), &s_ColHookAc, &Column, LineSize))
		g_Config.m_AcMiniStatboardColumns ^= 128;

	static int s_ColLastsAc = 0;
	s_ColLastsAc = (g_Config.m_AcMiniStatboardColumns & 4) ? 1 : 0;
	if(DoCheckboxWithHelp(&s_ColLastsAc, TCLocalize("Column: Lasts (Sole survivor)"), TCLocalize("Times the player was the sole unfrozen survivor alive in the team (requires 15+ ticks stabilization)."), &s_ColLastsAc, &Column, LineSize))
		g_Config.m_AcMiniStatboardColumns ^= 4;

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_AcMiniStatboardX, &g_Config.m_AcMiniStatboardX, &Button, TCLocalize("Horizontal Position (X)"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_AcMiniStatboardY, &g_Config.m_AcMiniStatboardY, &Button, TCLocalize("Vertical Position (Y)"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_AcMiniStatboardScale, &g_Config.m_AcMiniStatboardScale, &Button, TCLocalize("HUD Scale (Size / Z)"), 30, 200, &CUi::ms_LinearScrollbarScale, 0, "%");

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_AcMiniStatboardOpacity, &g_Config.m_AcMiniStatboardOpacity, &Button, TCLocalize("Background Opacity"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_AcMiniStatboardMaxPlayers, &g_Config.m_AcMiniStatboardMaxPlayers, &Button, TCLocalize("Max visible players"), 0, MAX_CLIENTS);

	// Ordenamiento en tiempo real & Animación F1
	Column.HSplitTop(MarginSmall, nullptr, &Column);
	{
		CUIRect SortLabel, StepperRect, BtnPrev, BtnNext, BtnText;
		Column.HSplitTop(LineSize, &SortLabel, &Column);
		SortLabel.VSplitLeft(110.0f, &SortLabel, &StepperRect);
		Ui()->DoLabel(&SortLabel, TCLocalize("Sort by:"), 13.0f, TEXTALIGN_ML);

		StepperRect.VSplitLeft(24.0f, &BtnPrev, &BtnText);
		BtnText.VSplitRight(24.0f, &BtnText, &BtnNext);

		static const char *s_apSortNames[9] = {
			"Default (Local first)",
			"Saves (Hammer)",
			"Deaths (Most deaths)",
			"Freezes (Most freezes)",
			"Efficiency %",
			"Net Balance (+/-)",
			"Frozen Time",
			"Hook Accuracy %",
			"Lasts (Sole survivor)"
		};

		static CButtonContainer s_BtnSortPrev, s_BtnSortNext, s_BtnSortText;
		if(DoButton_Menu(&s_BtnSortPrev, "<", 0, &BtnPrev, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
		{
			g_Config.m_AcMiniStatboardSort = (g_Config.m_AcMiniStatboardSort + 8) % 9;
		}
		if(DoButton_Menu(&s_BtnSortNext, ">", 0, &BtnNext, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
		{
			g_Config.m_AcMiniStatboardSort = (g_Config.m_AcMiniStatboardSort + 1) % 9;
		}
		int CurSort = std::clamp((int)g_Config.m_AcMiniStatboardSort, 0, 8);
		if(DoButton_Menu(&s_BtnSortText, TCLocalize(s_apSortNames[CurSort]), 0, &BtnText, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
		{
			g_Config.m_AcMiniStatboardSort = (g_Config.m_AcMiniStatboardSort + 1) % 9;
		}
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	DoCheckboxWithHelp(&g_Config.m_AcMiniStatboardAnim, TCLocalize("F1-style position animation"), TCLocalize("Smoothly animates players sliding up and down the leaderboard ladder when positions change in real time."), &g_Config.m_AcMiniStatboardAnim, &Column, LineSize);

	if(g_Config.m_AcMiniStatboardAnim)
	{
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_AcMiniStatboardAnimSpeed, &g_Config.m_AcMiniStatboardAnimSpeed, &Button, TCLocalize("Animation speed"), 3, 30, &CUi::ms_LinearScrollbarScale, 0);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoCheckboxWithHelp(&g_Config.m_AcMiniStatboardRankUpColor, TCLocalize("Green color on rank up"), TCLocalize("Show green flash when gaining a rank in mini statboard"), &g_Config.m_AcMiniStatboardRankUpColor, &Column, LineSize);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoCheckboxWithHelp(&g_Config.m_AcMiniStatboardRankDownColor, TCLocalize("Red color on rank down"), TCLocalize("Show red flash when losing a rank in mini statboard"), &g_Config.m_AcMiniStatboardRankDownColor, &Column, LineSize);
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	DoCheckboxWithHelp(&g_Config.m_AcMiniStatboardShowTees, TCLocalize("Show player skins (Mini Tees)"), TCLocalize("Shows mini tees next to each player name in the stats overlay."), &g_Config.m_AcMiniStatboardShowTees, &Column, LineSize);

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	{
		CUIRect ActionRow, BtnSave, BtnReset;
		Column.HSplitTop(LineSize + 4.0f, &ActionRow, &Column);
		ActionRow.VSplitMid(&BtnSave, &BtnReset, MarginSmall);
		static CButtonContainer s_BtnSaveOverlayNow, s_BtnResetOverlayNow;
		if(DoButton_Menu(&s_BtnSaveOverlayNow, TCLocalize("Save Stats Now"), 0, &BtnSave))
			GameClient()->m_RaceStats.SaveCurrentStats("Manual");
		if(DoButton_Menu(&s_BtnResetOverlayNow, TCLocalize("Reset Live Stats"), 0, &BtnReset))
			GameClient()->m_RaceStats.ResetTeamStats();
	}

	s_SectionBoxesAc.back().h = Column.y - s_SectionBoxesAc.back().y;

	// ***** Chat & Client Messages (Echo) ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxesAc.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Chat & Client Messages (Echo)"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	if(DoCheckboxWithHelp(&g_Config.m_AcShowChatClient, TCLocalize("Show client messages in chat (Echo / Local notices)"), TCLocalize("Controls whether local echo messages (like [AClient], command feedback and notices) appear in the chat."), &g_Config.m_AcShowChatClient, &Column, LineSize))
	{
		g_Config.m_AcShowChatClient = g_Config.m_AcShowChatClient;
	}

	DoCheckboxWithHelp(&g_Config.m_ClShowNotifications, TCLocalize("Desktop notifications (Chat mentions & Alerts)"), TCLocalize("Shows a Windows desktop toast notification banner when someone mentions your name in chat or on vote alerts while the game is minimized or inactive."), &g_Config.m_ClShowNotifications, &Column, LineSize);

	if(DoCheckboxWithHelp(&g_Config.m_AcSilentTyping, TCLocalize("Silent typing (Hide 3-dots bubble when typing)"), TCLocalize("Prevents the 3-dots bubble (...) from appearing above your Tee when the chat is open."), &g_Config.m_AcSilentTyping, &Column, LineSize))
	{
		g_Config.m_AcSilentTyping = g_Config.m_AcSilentTyping;
		g_Config.m_AcHideChatBubbles = g_Config.m_AcSilentTyping;
	}

	DoCheckboxWithHelp(&g_Config.m_AcHideOthersChatBubbles, TCLocalize("Hide typing bubbles of other players"), TCLocalize("Hides the 3-dots typing bubble of other players on your screen when they are typing."), &g_Config.m_AcHideOthersChatBubbles, &Column, LineSize);

	DoCheckboxWithHelp(&g_Config.m_AcShowInMenuIcon, TCLocalize("Mostrar logo arriba del Tee en Escape / Opciones"), TCLocalize("Muestra el logo de opciones (engranaje) arriba de tu Tee al tocar Escape o abrir el menu."), &g_Config.m_AcShowInMenuIcon, &Column, LineSize);

	DoCheckboxWithHelp(&g_Config.m_AcHideStartMessages, TCLocalize("Hide start line messages in chat"), TCLocalize("Hides team start warnings and team started notifications when touching the start line."), &g_Config.m_AcHideStartMessages, &Column, LineSize);

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_AcChatMaxLines, &g_Config.m_AcChatMaxLines, &Button, TCLocalize("Max lineas de historial de chat"), 64, 512, &CUi::ms_LinearScrollbarScale, 0, " lineas");

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	Column.HSplitTop(15.0f, &Label, &Column);
	TextRender()->TextColor(0.65f, 0.85f, 1.0f, 0.95f);
	Ui()->DoLabel(&Label, TCLocalize("Scroll en chat abierto (Enter / T / Y): Rueda del mouse, RePag/AvPag o Ctrl+Inicio"), 11.0f, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(LineSize + 4.0f, &Button, &Column);
	static CButtonContainer s_BtnTestEchoNow;
	if(DoButton_Menu(&s_BtnTestEchoNow, TCLocalize("Send Test Echo Message"), 0, &Button))
	{
		GameClient()->Echo(TCLocalize("[AClient] This is a test Echo message."));
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	s_SectionBoxesAc.back().h = Column.y - s_SectionBoxesAc.back().y;

	// ***** Skin Stealer ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxesAc.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Skin Stealer"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static CButtonContainer s_ReaderStealSkinAc, s_ClearStealSkinAc;
	DoLine_KeyReader(Column, s_ReaderStealSkinAc, s_ClearStealSkinAc, TCLocalize("Steal skin key (aiming at player)"), "steal_skin");

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(16.0f, &Label, &Column);
	TextRender()->TextColor(1.0f, 0.88f, 0.35f, 1.0f);
	Ui()->DoLabel(&Label, TCLocalize("Chat: /stealskin [name]  or  Console: steal_skin [name]"), 12.0f, TEXTALIGN_ML);
	Column.HSplitTop(16.0f, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("You can also right-click / click on any player in Scoreboard (Tab) to steal skin."), 11.5f, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	if(Client()->State() == IClient::STATE_ONLINE)
	{
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize + 4.0f, &Button, &Column);
		static CButtonContainer s_BtnStealSkinAimedNow;
		if(DoButton_Menu(&s_BtnStealSkinAimedNow, TCLocalize("Steal skin from aimed player now"), 0, &Button))
		{
			int TargetId = GameClient()->FindStealTarget(nullptr);
			if(TargetId >= 0)
				GameClient()->StealSkin(TargetId);
			else
				GameClient()->m_Chat.Echo(TCLocalize("[AClient] No target player found to steal skin from."));
		}
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	s_SectionBoxesAc.back().h = Column.y - s_SectionBoxesAc.back().y;

	// ***** Auto Team Lock ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxesAc.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Auto Team Lock & Invites"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcAutoTeamLock, TCLocalize("Auto lock team on join (/lock 1)"), &g_Config.m_AcAutoTeamLock, &Column, LineSize);

	Column.HSplitTop(LineSize, &Button, &Column);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_AcAutoTeamLockDelay, &g_Config.m_AcAutoTeamLockDelay, &Button, TCLocalize("Lock delay (max 5m)"), 0, 300, &CUi::ms_LinearScrollbarScale, 0, "s");

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(16.0f, &Label, &Column);
	TextRender()->TextColor(1.0f, 0.88f, 0.35f, 1.0f);
	Ui()->DoLabel(&Label, TCLocalize("Chat: /invite friends  (invites only friends to team)"), 12.0f, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	static CButtonContainer s_ReaderInviteFriendsAc, s_ClearInviteFriendsAc;
	DoLine_KeyReader(Column, s_ReaderInviteFriendsAc, s_ClearInviteFriendsAc, TCLocalize("Invite friends key (/invite friends)"), "say /invite friends");

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	static CButtonContainer s_ReaderJoinInviteAc, s_ClearJoinInviteAc;
	DoLine_KeyReader(Column, s_ReaderJoinInviteAc, s_ClearJoinInviteAc, TCLocalize("Unirse a invitacion (/team last)"), "team_invite");

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	static CButtonContainer s_ReaderGreetLastAc, s_ClearGreetLastAc;
	DoLine_KeyReader(Column, s_ReaderGreetLastAc, s_ClearGreetLastAc, TCLocalize("Saludar al ultimo que te taggeo (greet_last)"), "greet_last");

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(LineSize, &Button, &Column);
	static CLineInput s_GreetMsgInput(g_Config.m_AcGreetMessage, sizeof(g_Config.m_AcGreetMessage));
	DoEditBoxWithLabel(&s_GreetMsgInput, &Button, TCLocalize("Mensaje de saludo"), "wenas", g_Config.m_AcGreetMessage, sizeof(g_Config.m_AcGreetMessage));

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcGreetWhisperMode, TCLocalize("Responder por susurro si el tag fue susurro"), &g_Config.m_AcGreetWhisperMode, &Column, LineSize);

	if(Client()->State() == IClient::STATE_ONLINE)
	{
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		CUIRect BtnInvFriends;
		Column.HSplitTop(LineSize + 4.0f, &BtnInvFriends, &Column);
		static CButtonContainer s_BtnInviteFriendsNow;
		if(DoButton_Menu(&s_BtnInviteFriendsNow, TCLocalize("Invite Friends (/invite friends)"), 0, &BtnInvFriends))
			GameClient()->m_Chat.SendChat(0, "/invite friends");
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	s_SectionBoxesAc.back().h = Column.y - s_SectionBoxesAc.back().y;

	// ***** Music Player (HUD + ESC Controls) ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxesAc.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	{
		CUIRect MusicHeaderLabel, MusicResetBtn;
		Label.VSplitRight(90.0f, &MusicHeaderLabel, &MusicResetBtn);
		Ui()->DoLabel(&MusicHeaderLabel, TCLocalize("Music Player (HUD)"), HeadlineFontSize, TEXTALIGN_ML);
		static CButtonContainer s_MusicResetAc;
		if(DoButton_Menu(&s_MusicResetAc, TCLocalize("Reset"), 0, &MusicResetBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ColorRGBA(0.35f, 0.38f, 0.45f, 0.55f)))
		{
			g_Config.m_BcMusicPlayerColorMode = DefaultConfig::BcMusicPlayerColorMode;
			g_Config.m_BcMusicPlayerStaticColor = DefaultConfig::BcMusicPlayerStaticColor;
			g_Config.m_BcMusicPlayerTextScale = DefaultConfig::BcMusicPlayerTextScale;
			g_Config.m_BcMusicPlayerVisualizerMode = DefaultConfig::BcMusicPlayerVisualizerMode;
			g_Config.m_BcMusicPlayerVisualizerRounding = DefaultConfig::BcMusicPlayerVisualizerRounding;
			g_Config.m_BcMusicPlayerVisualizerColumns = DefaultConfig::BcMusicPlayerVisualizerColumns;
			g_Config.m_BcMusicPlayerShowLyrics = DefaultConfig::BcMusicPlayerShowLyrics;
			g_Config.m_BcMusicPlayerShowCurrentTime = DefaultConfig::BcMusicPlayerShowCurrentTime;
			HudLayout::ResetSettings(HudLayout::MODULE_MUSIC_PLAYER);
		}
	}
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	if(DoCheckboxWithHelp(&g_Config.m_BcMusicPlayer, TCLocalize("Activar Music Player (HUD)"), TCLocalize("Muestra la isla dinamica de musica arriba del todo en el centro y permite cambiar canciones al tocar ESC."), &g_Config.m_BcMusicPlayer, &Column, LineSize))
		HudLayout::SetEnabled(HudLayout::MODULE_MUSIC_PLAYER, g_Config.m_BcMusicPlayer != 0);

	if(g_Config.m_BcMusicPlayer)
	{
		DoCheckboxWithHelp(&g_Config.m_BcMusicPlayerShowLyrics, TCLocalize("Mostrar letras sincronizadas (Lyrics)"), TCLocalize("Descarga y muestra letras sincronizadas en vivo desde lrclib.net usando cache O(1) y limite de velocidad."), &g_Config.m_BcMusicPlayerShowLyrics, &Column, LineSize);
		if(g_Config.m_BcMusicPlayerShowLyrics)
			DoCheckboxWithHelp(&g_Config.m_BcMusicPlayerShowCurrentTime, TCLocalize("Mostrar tiempo debajo de letras"), TCLocalize("Muestra una pestana con el tiempo de partida/cancion debajo de las letras."), &g_Config.m_BcMusicPlayerShowCurrentTime, &Column, LineSize);

		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_BcMusicPlayerTextScale, &g_Config.m_BcMusicPlayerTextScale, &Button, TCLocalize("Tamano de texto"), 70, 150, &CUi::ms_LinearScrollbarScale, 0u, "%");

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		{
			CUIRect Row, ColorLabel, BtnStatic, BtnCover;
			Column.HSplitTop(LineSize, &Row, &Column);
			Row.VSplitLeft(120.0f, &ColorLabel, &Row);
			Ui()->DoLabel(&ColorLabel, TCLocalize("Color acento:"), FontSize, TEXTALIGN_ML);
			Row.VSplitMid(&BtnStatic, &BtnCover, 2.0f);
			g_Config.m_BcMusicPlayerColorMode = std::clamp(g_Config.m_BcMusicPlayerColorMode, 0, 1);
			static CButtonContainer s_MpColStatic, s_MpColCover;
			if(DoButton_Menu(&s_MpColStatic, TCLocalize("Estatico"), g_Config.m_BcMusicPlayerColorMode == 0, &BtnStatic, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_BcMusicPlayerColorMode = 0;
			if(DoButton_Menu(&s_MpColCover, TCLocalize("Portada"), g_Config.m_BcMusicPlayerColorMode == 1, &BtnCover, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_BcMusicPlayerColorMode = 1;
		}
		if(g_Config.m_BcMusicPlayerColorMode == 0)
		{
			Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
			static CButtonContainer s_MpStaticColor;
			DoLine_ColorPicker(&s_MpStaticColor, ColorPickerLineSize, ColorPickerLabelSize, ColorPickerLineSpacing, &Column, TCLocalize("Color estatico"), &g_Config.m_BcMusicPlayerStaticColor, color_cast<ColorRGBA>(ColorHSLA(DefaultConfig::BcMusicPlayerStaticColor)), false);
		}

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		{
			CUIRect Row, VisLabel, BtnBot, BtnMid, BtnTop;
			Column.HSplitTop(LineSize, &Row, &Column);
			Row.VSplitLeft(120.0f, &VisLabel, &Row);
			Ui()->DoLabel(&VisLabel, TCLocalize("Visualizador:"), FontSize, TEXTALIGN_ML);
			Row.VSplitLeft(Row.w / 3.0f, &BtnBot, &Row);
			Row.VSplitMid(&BtnMid, &BtnTop);
			g_Config.m_BcMusicPlayerVisualizerMode = std::clamp(g_Config.m_BcMusicPlayerVisualizerMode, 0, 2);
			static CButtonContainer s_MpVisBot, s_MpVisMid, s_MpVisTop;
			if(DoButton_Menu(&s_MpVisBot, TCLocalize("Abajo"), g_Config.m_BcMusicPlayerVisualizerMode == 0, &BtnBot, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_BcMusicPlayerVisualizerMode = 0;
			if(DoButton_Menu(&s_MpVisMid, TCLocalize("Centro"), g_Config.m_BcMusicPlayerVisualizerMode == 1, &BtnMid, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_NONE))
				g_Config.m_BcMusicPlayerVisualizerMode = 1;
			if(DoButton_Menu(&s_MpVisTop, TCLocalize("Arriba"), g_Config.m_BcMusicPlayerVisualizerMode == 2, &BtnTop, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_BcMusicPlayerVisualizerMode = 2;
		}

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		{
			CUIRect Row, RoundLabel, BtnCube, BtnSoft;
			Column.HSplitTop(LineSize, &Row, &Column);
			Row.VSplitLeft(120.0f, &RoundLabel, &Row);
			Ui()->DoLabel(&RoundLabel, TCLocalize("Forma barras:"), FontSize, TEXTALIGN_ML);
			Row.VSplitMid(&BtnCube, &BtnSoft, 2.0f);
			const int RoundPreset = g_Config.m_BcMusicPlayerVisualizerRounding < 100 ? 0 : 1;
			static CButtonContainer s_MpCube, s_MpSoft;
			if(DoButton_Menu(&s_MpCube, TCLocalize("Cuadrado"), RoundPreset == 0, &BtnCube, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_BcMusicPlayerVisualizerRounding = 0;
			if(DoButton_Menu(&s_MpSoft, TCLocalize("Suave"), RoundPreset == 1, &BtnSoft, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_BcMusicPlayerVisualizerRounding = 200;
		}

		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_BcMusicPlayerVisualizerColumns, &g_Config.m_BcMusicPlayerVisualizerColumns, &Button, TCLocalize("Columnas visualizador"), 5, 10);
	}

	// Teclas bindeables de control multimedia (siempre visibles, incluso con el HUD desactivado)
	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(LineSize, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Atajos de musica (sin tocar ESC):"), FontSize, TEXTALIGN_ML);
	{
		static CButtonContainer s_MusicPrevReader, s_MusicPrevClear;
		static CButtonContainer s_MusicPlayReader, s_MusicPlayClear;
		static CButtonContainer s_MusicNextReader, s_MusicNextClear;
		DoLine_KeyReader(Column, s_MusicPrevReader, s_MusicPrevClear, TCLocalize("Cancion anterior"), "ac_music_prev");
		DoLine_KeyReader(Column, s_MusicPlayReader, s_MusicPlayClear, TCLocalize("Pausar / Reanudar musica"), "ac_music_playpause");
		DoLine_KeyReader(Column, s_MusicNextReader, s_MusicNextClear, TCLocalize("Siguiente cancion"), "ac_music_next");
	}

	// Compartir Musica en vivo (1 a 1 entre usuarios con AClient)
	{
		CAClientMusicShare &MusicShare = GameClient()->m_AClientMusicShare;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Label, &Column);
		Ui()->DoLabel(&Label, TCLocalize("Compartir Musica (1 a 1 con AClient):"), FontSize, TEXTALIGN_ML);

		// Volumen musica compartida + Mute
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		CUIRect VolSlider, MuteBtn;
		Button.VSplitRight(52.0f, &VolSlider, &MuteBtn);
		VolSlider.VSplitRight(4.0f, &VolSlider, nullptr);
		Ui()->DoScrollbarOption(&g_Config.m_AcMusicShareVolume, &g_Config.m_AcMusicShareVolume, &VolSlider, TCLocalize("Volumen musica compartida"), 0, 200, &CUi::ms_LinearScrollbarScale, 0u, "%");
		static CButtonContainer s_BtnMusicShareMute;
		const bool Muted = MusicShare.IsMuted();
		if(DoButton_Menu(&s_BtnMusicShareMute, Muted ? "Muted" : "Mute", 0, &MuteBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, Muted ? ColorRGBA(0.82f, 0.25f, 0.25f, 0.85f) : ColorRGBA(0.24f, 0.28f, 0.35f, 0.65f)))
			MusicShare.ToggleMute();

		// Estado actual
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		char aShareStatus[144];
		if(MusicShare.IsListening())
		{
			str_format(aShareStatus, sizeof(aShareStatus), "Estado: Escuchando con %s (%s)", MusicShare.GetPeerName(), MusicShare.GetRemoteTrack().m_aTitle[0] ? MusicShare.GetRemoteTrack().m_aTitle : "En vivo");
			TextRender()->TextColor(0.40f, 0.95f, 0.65f, 0.98f);
		}
		else if(MusicShare.IsHosting())
		{
			if(MusicShare.GetCaptureStatus() == CAClientMusicShare::CAPTURE_SEARCHING)
			{
				str_format(aShareStatus, sizeof(aShareStatus), "Estado: Conectado con %s (abre Spotify/YouTube)", MusicShare.GetPeerName());
				TextRender()->TextColor(0.95f, 0.85f, 0.45f, 0.98f);
			}
			else
			{
				str_format(aShareStatus, sizeof(aShareStatus), "Estado: Compartiendo musica en estereo a %s", MusicShare.GetPeerName());
				TextRender()->TextColor(0.40f, 0.95f, 0.55f, 0.98f);
			}
		}
		else if(MusicShare.IsWaitingAccept())
		{
			str_format(aShareStatus, sizeof(aShareStatus), "Estado: Esperando que %s acepte...", MusicShare.GetPeerName());
			TextRender()->TextColor(0.95f, 0.85f, 0.45f, 0.98f);
		}
		else
		{
			str_copy(aShareStatus, TCLocalize("Estado: Inactivo (audio aislado solo de tu app de musica)"));
			TextRender()->TextColor(0.68f, 0.72f, 0.78f, 0.90f);
		}
		SLabelProperties StProps;
		StProps.m_MaxWidth = Button.w;
		StProps.m_EllipsisAtEnd = true;
		Ui()->DoLabel(&Button, aShareStatus, FontSize * 0.86f, TEXTALIGN_ML, StProps);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		// Banner de invitacion pendiente dentro del panel
		if(MusicShare.HasPendingInvite())
		{
			Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
			Column.HSplitTop(LineSize + 4.0f, &Button, &Column);
			Button.Draw(ColorRGBA(0.14f, 0.24f, 0.46f, 0.88f), IGraphics::CORNER_ALL, 4.0f);
			CUIRect Row = Button;
			Row.Margin(2.0f, &Row);
			CUIRect AccBtn, DecBtn;
			Row.VSplitRight(58.0f, &Row, &DecBtn);
			Row.VSplitRight(4.0f, &Row, nullptr);
			Row.VSplitRight(58.0f, &Row, &AccBtn);
			Row.VSplitRight(6.0f, &Row, nullptr);

			char aInvTxt[128];
			str_format(aInvTxt, sizeof(aInvTxt), "%s: %s", MusicShare.GetPendingInviteFrom(), MusicShare.GetPendingInviteTrack());
			TextRender()->TextColor(0.95f, 0.98f, 1.0f, 1.0f);
			SLabelProperties InvP;
			InvP.m_MaxWidth = Row.w;
			InvP.m_EllipsisAtEnd = true;
			Ui()->DoLabel(&Row, aInvTxt, FontSize * 0.84f, TEXTALIGN_ML, InvP);
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			static CButtonContainer s_PanelMusicAcc, s_PanelMusicDec;
			if(DoButton_Menu(&s_PanelMusicAcc, TCLocalize("Aceptar"), 0, &AccBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 3.0f, 0.0f, ColorRGBA(0.22f, 0.75f, 0.38f, 0.90f)))
				MusicShare.AcceptPendingInvite();
			if(DoButton_Menu(&s_PanelMusicDec, TCLocalize("Rechazar"), 0, &DecBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 3.0f, 0.0f, ColorRGBA(0.80f, 0.25f, 0.25f, 0.85f)))
				MusicShare.DeclinePendingInvite();
		}

		// Boton principal: Compartir musica (desplegable con usuarios de AClient) o Detener / Salir
		static bool s_MenuShareDropdownOpen = false;
		if(!MusicShare.IsIdle())
		{
			s_MenuShareDropdownOpen = false;
			Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			static CButtonContainer s_BtnStopMusicShare;
			const char *pStopLbl = MusicShare.IsListening()
				? TCLocalize("Dejar de escuchar")
				: (MusicShare.IsWaitingAccept() ? TCLocalize("Cancelar invitacion") : TCLocalize("Dejar de compartir"));
			if(DoButton_Menu(&s_BtnStopMusicShare, pStopLbl, 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ColorRGBA(0.80f, 0.26f, 0.26f, 0.85f)))
				MusicShare.StopSession(true);
		}
		else
		{
			Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			static CButtonContainer s_BtnToggleAClientList;
			if(DoButton_Menu(&s_BtnToggleAClientList, s_MenuShareDropdownOpen ? TCLocalize("Ocultar usuarios con AClient") : TCLocalize("Compartir musica (elegir usuario AClient)"), s_MenuShareDropdownOpen ? 1 : 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ColorRGBA(0.20f, 0.48f, 0.78f, 0.85f)))
				s_MenuShareDropdownOpen = !s_MenuShareDropdownOpen;

			if(s_MenuShareDropdownOpen)
			{
				if(Client()->State() != IClient::STATE_ONLINE)
				{
					Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
					Column.HSplitTop(LineSize, &Button, &Column);
					TextRender()->TextColor(0.70f, 0.72f, 0.78f, 0.88f);
					Ui()->DoLabel(&Button, TCLocalize("Entra a un servidor para ver usuarios con AClient."), FontSize * 0.84f, TEXTALIGN_ML);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
				}
				else
				{
					static CButtonContainer s_aMusicShareUserBtns[MAX_CLIENTS];
					const int Local0 = GameClient()->m_aLocalIds[0];
					const int Local1 = GameClient()->m_aLocalIds[1];
					int Found = 0;
					for(int i = 0; i < MAX_CLIENTS; ++i)
					{
						if(i == Local0 || i == Local1)
							continue;
						if(!GameClient()->m_aClients[i].m_Active || !GameClient()->m_aClients[i].m_IsAClient || GameClient()->m_aClients[i].m_aName[0] == '\0')
							continue;
						Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
						Column.HSplitTop(LineSize, &Button, &Column);
						CUIRect NameR, ShareBtn;
						Button.VSplitRight(76.0f, &NameR, &ShareBtn);
						NameR.VSplitRight(4.0f, &NameR, nullptr);
						Ui()->DoLabel(&NameR, GameClient()->m_aClients[i].m_aName, FontSize * 0.86f, TEXTALIGN_ML);
						if(DoButton_Menu(&s_aMusicShareUserBtns[i], TCLocalize("Compartir"), 0, &ShareBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ColorRGBA(0.24f, 0.65f, 0.42f, 0.85f)))
						{
							MusicShare.InviteFriend(GameClient()->m_aClients[i].m_aName);
							s_MenuShareDropdownOpen = false;
						}
						++Found;
					}
					if(Found == 0)
					{
						Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
						Column.HSplitTop(LineSize, &Button, &Column);
						TextRender()->TextColor(0.70f, 0.72f, 0.78f, 0.88f);
						Ui()->DoLabel(&Button, TCLocalize("No se detectaron otros usuarios con AClient en este servidor."), FontSize * 0.84f, TEXTALIGN_ML);
						TextRender()->TextColor(TextRender()->DefaultTextColor());
					}
				}
			}
		}
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	s_SectionBoxesAc.back().h = Column.y - s_SectionBoxesAc.back().y;

	// ***** Modo Foco (Focus Mode) ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxesAc.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Modo Foco (Focus Mode)"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoCheckboxWithHelp(&g_Config.m_ClFocusMode, TCLocalize("Activar Modo Foco"), TCLocalize("Oculta elementos de distraccion (HUD, Chat, etc.) segun las opciones tildadas abajo."), &g_Config.m_ClFocusMode, &Column, LineSize);

	static CButtonContainer s_FocusModeBindReaderAc, s_FocusModeBindClearAc;
	DoLine_KeyReader(Column, s_FocusModeBindReaderAc, s_FocusModeBindClearAc, TCLocalize("Tecla Modo Foco"), "toggle cl_focus_mode 0 1");

	DoCheckboxWithHelp(&g_Config.m_ClFocusModeHideHud, TCLocalize("Ocultar HUD completo"), TCLocalize("Oculta barra de vida, municion, timer, score, velocidad, frozen HUD y keystrokes."), &g_Config.m_ClFocusModeHideHud, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_ClFocusModeHideChat, TCLocalize("Ocultar Chat"), TCLocalize("Oculta mensajes y caja del chat mientras el Modo Foco esta activo."), &g_Config.m_ClFocusModeHideChat, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_ClFocusModeHideSongPlayer, TCLocalize("Ocultar Reproductor de Musica"), TCLocalize("Oculta tambien la isla del reproductor de musica superior cuando Modo Foco esta activo."), &g_Config.m_ClFocusModeHideSongPlayer, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_ClFocusModeHideUI, TCLocalize("Ocultar UI secundaria (Killfeed, Broadcasts, Overlays)"), TCLocalize("Oculta killfeed, broadcasts, votos, hora local, racestats, statusbar, voice HUD y keystrokes."), &g_Config.m_ClFocusModeHideUI, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_ClFocusModeHideNames, TCLocalize("Ocultar Nombres (Nameplates)"), TCLocalize("Oculta los nombres y clanes sobre los tees en pantalla."), &g_Config.m_ClFocusModeHideNames, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_ClFocusModeHideEffects, TCLocalize("Ocultar Efectos visuales (Particulas y Trails)"), TCLocalize("Oculta particulas y estelas (trails) mientras Modo Foco esta activo."), &g_Config.m_ClFocusModeHideEffects, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_ClFocusModeHideScoreboard, TCLocalize("Ocultar Scoreboard (Tab)"), TCLocalize("Evita mostrar la tabla de puntuacion al mantener Tab mientras Modo Foco esta activo."), &g_Config.m_ClFocusModeHideScoreboard, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_ClFocusModeAutoReply, TCLocalize("Auto-responder si te mencionan con chat oculto"), TCLocalize("Responde automaticamente cuando alguien te nombra o susurra y tenes el chat oculto."), &g_Config.m_ClFocusModeAutoReply, &Column, LineSize);

	if(g_Config.m_ClFocusModeAutoReply)
	{
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		static CLineInput s_FocusModeAutoReplyInputAc(g_Config.m_ClFocusModeAutoReplyMessage, sizeof(g_Config.m_ClFocusModeAutoReplyMessage));
		DoEditBoxWithLabel(&s_FocusModeAutoReplyInputAc, &Button, TCLocalize("Mensaje auto-respuesta"), "my chat is hidden", g_Config.m_ClFocusModeAutoReplyMessage, sizeof(g_Config.m_ClFocusModeAutoReplyMessage));
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	s_SectionBoxesAc.back().h = Column.y - s_SectionBoxesAc.back().y;
	}

	if(!VisualTab)
	{
	// ***** Background Alerts (Tabbed out) ***** //
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxesAc.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Background Alerts (Tabbed out)"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcNotifyUnfreeze, TCLocalize("Notify when unfreezing (tabbed out)"), &g_Config.m_AcNotifyUnfreeze, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcChangeTileNotification, TCLocalize("Notify when moved or tile changes (tabbed out)"), &g_Config.m_AcChangeTileNotification, &Column, LineSize);

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(16.0f, &Label, &Column);
	TextRender()->TextColor(0.55f, 0.9f, 1.0f, 1.0f);
	Ui()->DoLabel(&Label, TCLocalize("Alert methods (independent):"), 12.0f, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcNotifySound, TCLocalize("Sound alert"), &g_Config.m_AcNotifySound, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcNotifyFlash, TCLocalize("Taskbar flash"), &g_Config.m_AcNotifyFlash, &Column, LineSize);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcNotifyDesktop, TCLocalize("Desktop notification"), &g_Config.m_AcNotifyDesktop, &Column, LineSize);

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcAfkAutoReply, TCLocalize("Auto-responder si te taggean estando tabulado (AFK)"), &g_Config.m_AcAfkAutoReply, &Column, LineSize);
	if(g_Config.m_AcAfkAutoReply)
	{
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		static CLineInput s_AfkMsgInput(g_Config.m_AcAfkAutoReplyMsg, sizeof(g_Config.m_AcAfkAutoReplyMsg));
		DoEditBoxWithLabel(&s_AfkMsgInput, &Button, TCLocalize("Mensaje tabulado / AFK"), "Estoy tabulado / afk", g_Config.m_AcAfkAutoReplyMsg, sizeof(g_Config.m_AcAfkAutoReplyMsg));

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_AcAfkAutoReplyCooldown, &g_Config.m_AcAfkAutoReplyCooldown, &Button, TCLocalize("Cooldown respuesta por jugador"), 5, 120, &CUi::ms_LinearScrollbarScale, 0, "s");
	}

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcAfkNotifyOnReturn, TCLocalize("Avisar en chat al volver si alguien te taggeo"), &g_Config.m_AcAfkNotifyOnReturn, &Column, LineSize);

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	s_SectionBoxesAc.back().h = Column.y - s_SectionBoxesAc.back().y;

	// ***** Sound Packs (Temas de Sonido) ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxesAc.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Sound Packs (Temas de Sonido)"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	CUIRect PackRow, PackLabel, PrevBtn, PackBtn, NextBtn, DirBtn, ReloadBtn;
	Column.HSplitTop(LineSize, &PackRow, &Column);
	PackRow.VSplitLeft(100.0f, &PackLabel, &PackRow);
	Ui()->DoLabel(&PackLabel, TCLocalize("Sound pack"), 14.0f, TEXTALIGN_ML);

	PackRow.VSplitRight(24.0f, &PackRow, &ReloadBtn);
	PackRow.VSplitRight(4.0f, &PackRow, nullptr);
	PackRow.VSplitRight(24.0f, &PackRow, &DirBtn);
	PackRow.VSplitRight(4.0f, &PackRow, nullptr);

	PackRow.VSplitLeft(24.0f, &PrevBtn, &PackRow);
	PackRow.VSplitRight(24.0f, &PackRow, &NextBtn);
	PackRow.VSplitLeft(4.0f, nullptr, &PackBtn);
	PackBtn.VSplitRight(4.0f, &PackBtn, nullptr);

	const auto &vPacks = GameClient()->m_Sounds.GetSoundPacks();
	int NumPacks = (int)vPacks.size();
	int CurIdx = 0;
	for(int i = 0; i < NumPacks; i++)
	{
		if(str_comp(vPacks[i].c_str(), g_Config.m_AcSoundPack) == 0)
		{
			CurIdx = i;
			break;
		}
	}

	static CButtonContainer s_PrevPackAc;
	static CButtonContainer s_NextPackAc;
	static CButtonContainer s_CurrentPackAc;
	static CButtonContainer s_DirPackAc;
	static CButtonContainer s_ReloadPackAc;

	if(DoButton_Menu(&s_PrevPackAc, "<", 0, &PrevBtn))
	{
		int NextIdx = CurIdx - 1;
		if(NextIdx < 0)
			NextIdx = NumPacks - 1;
		if(NextIdx >= 0 && NextIdx < NumPacks)
			GameClient()->m_Sounds.SetSoundPack(vPacks[NextIdx].c_str());
	}

	const char *pCurrentName = (CurIdx >= 0 && CurIdx < NumPacks) ? vPacks[CurIdx].c_str() : g_Config.m_AcSoundPack;
	char aDisplayName[128];
	if(str_comp(pCurrentName, "default") == 0)
		str_copy(aDisplayName, TCLocalize("Default"));
	else
		str_copy(aDisplayName, pCurrentName);

	if(DoButton_Menu(&s_CurrentPackAc, aDisplayName, 0, &PackBtn))
	{
		int NextIdx = CurIdx + 1;
		if(NextIdx >= NumPacks)
			NextIdx = 0;
		if(NextIdx >= 0 && NextIdx < NumPacks)
			GameClient()->m_Sounds.SetSoundPack(vPacks[NextIdx].c_str());
	}

	if(DoButton_Menu(&s_NextPackAc, ">", 0, &NextBtn))
	{
		int NextIdx = CurIdx + 1;
		if(NextIdx >= NumPacks)
			NextIdx = 0;
		if(NextIdx >= 0 && NextIdx < NumPacks)
			GameClient()->m_Sounds.SetSoundPack(vPacks[NextIdx].c_str());
	}

	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

	if(DoButton_Menu(&s_DirPackAc, FontIcon::FOLDER, 0, &DirBtn))
	{
		Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
		Storage()->CreateFolder("assets/audio", IStorage::TYPE_SAVE);
		char aBuf[IO_MAX_PATH_LENGTH];
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "assets/audio", aBuf, sizeof(aBuf));
		Client()->ViewFile(aBuf);
	}
	GameClient()->m_Tooltips.DoToolTip(&s_DirPackAc, &DirBtn, TCLocalize("Open directory for sound packs"));

	if(DoButton_Menu(&s_ReloadPackAc, FontIcon::ARROW_ROTATE_RIGHT, 0, &ReloadBtn))
	{
		GameClient()->m_Sounds.ScanSoundPacks();
		GameClient()->m_Sounds.ReloadSoundPack();
	}
	GameClient()->m_Tooltips.DoToolTip(&s_ReloadPackAc, &ReloadBtn, TCLocalize("Reload sound packs"));

	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	s_SectionBoxesAc.back().h = Column.y - s_SectionBoxesAc.back().y;

	// ***** FPS Booster / Optimizer ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxesAc.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("FPS Booster & Optimizer"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoCheckboxWithHelp(&g_Config.m_AcOptimizer, TCLocalize("Enable FPS Booster / Optimizer"), TCLocalize("Master switch that boosts framerates and reduces stuttering by trimming decorative graphics and background overhead."), &g_Config.m_AcOptimizer, &Column, LineSize);

	static CButtonContainer s_ReaderButtonOptimizer, s_ClearButtonOptimizer;
	DoLine_KeyReader(Column, s_ReaderButtonOptimizer, s_ClearButtonOptimizer, TCLocalize("Toggle Optimizer key"), "ac_optimizer_toggle");

	DoCheckboxWithHelp(&g_Config.m_AcOptimizerDisableParticles, TCLocalize("Disable particles"), TCLocalize("Stops updating and drawing all particles (smoke, explosions, weapon trails, freeze stars) to maximize GPU/CPU efficiency."), &g_Config.m_AcOptimizerDisableParticles, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_AcOptimizerDisableQuads, TCLocalize("Disable map quads (clouds & decorations)"), TCLocalize("Skips heavy decorative map quads like animated background clouds, dynamic lights, and mapper visual effects."), &g_Config.m_AcOptimizerDisableQuads, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_AcOptimizerDisableHighDetail, TCLocalize("Disable high detail layers"), TCLocalize("Automatically forces off high detail layers while optimizer is active, restoring your setting when disabled."), &g_Config.m_AcOptimizerDisableHighDetail, &Column, LineSize);

	DoCheckboxWithHelp(&g_Config.m_AcOptimizerFpsFog, TCLocalize("Limit FOV rendering (FPS Fog)"), TCLocalize("Limits map tiles, player hooks, tees and nameplates to a focused radius around the camera, culling off-screen and distant elements."), &g_Config.m_AcOptimizerFpsFog, &Column, LineSize);

	if(g_Config.m_AcOptimizerFpsFog)
	{
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		{
			static CButtonContainer s_aFogModeButtons[2];
			static const char *s_apFogModeNames[2] = {"Radio Fijo (Tiles)", "Dinamico (% Zoom)"};
			CUIRect LeftBtn, RightBtn;
			Button.VSplitMid(&LeftBtn, &RightBtn, 2.0f);
			if(DoButton_Menu(&s_aFogModeButtons[0], TCLocalize(s_apFogModeNames[0]), g_Config.m_AcOptimizerFpsFogMode == 0, &LeftBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_AcOptimizerFpsFogMode = 0;
			if(DoButton_Menu(&s_aFogModeButtons[1], TCLocalize(s_apFogModeNames[1]), g_Config.m_AcOptimizerFpsFogMode == 1, &RightBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_AcOptimizerFpsFogMode = 1;
		}
		Column.HSplitTop(MarginSmall, nullptr, &Column);

		if(g_Config.m_AcOptimizerFpsFogMode == 1)
		{
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_AcOptimizerFpsFogZoomPercent, &g_Config.m_AcOptimizerFpsFogZoomPercent, &Button, TCLocalize("FPS Fog Zoom Area"), 20, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
		}
		else
		{
			Column.HSplitTop(LineSize, &Button, &Column);
			Ui()->DoScrollbarOption(&g_Config.m_AcOptimizerFpsFogRadiusTiles, &g_Config.m_AcOptimizerFpsFogRadiusTiles, &Button, TCLocalize("FPS Fog Radius"), 10, 120, &CUi::ms_LinearScrollbarScale, 0, " tiles");
		}

		DoCheckboxWithHelp(&g_Config.m_AcOptimizerFpsFogCullMapTiles, TCLocalize("Cull map tiles outside radius"), TCLocalize("Drastically reduces tile vertices and draw operations outside the focused view area."), &g_Config.m_AcOptimizerFpsFogCullMapTiles, &Column, LineSize);

		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		{
			CUIRect LabelRect, ToggleRect, OffBtn, OnBtn;
			Button.VSplitRight(120.0f, &LabelRect, &ToggleRect);
			Ui()->DoLabel(&LabelRect, TCLocalize("Mostrar bordes del FPS Fog"), FontSize, TEXTALIGN_ML);
			ToggleRect.VSplitMid(&OffBtn, &OnBtn, 2.0f);
			static CButtonContainer s_aFogBorderBtns[2];
			if(DoButton_Menu(&s_aFogBorderBtns[0], "OFF", g_Config.m_AcOptimizerFpsFogRenderRect == 0, &OffBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
				g_Config.m_AcOptimizerFpsFogRenderRect = 0;
			if(DoButton_Menu(&s_aFogBorderBtns[1], "ON", g_Config.m_AcOptimizerFpsFogRenderRect == 1, &OnBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
				g_Config.m_AcOptimizerFpsFogRenderRect = 1;
		}
	}

	DoCheckboxWithHelp(&g_Config.m_AcOptimizerHighPriority, TCLocalize("High process priority (Windows)"), TCLocalize("Automatically assigns High priority to DDNet in Windows to reduce background process micro-stutters."), &g_Config.m_AcOptimizerHighPriority, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_AcOptimizerDiscordNormalPriority, TCLocalize("Discord normal priority (Windows)"), TCLocalize("Keeps Discord processes at Normal priority in Windows so Discord does not compete with DDNet for CPU."), &g_Config.m_AcOptimizerDiscordNormalPriority, &Column, LineSize);

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	s_SectionBoxesAc.back().h = Column.y - s_SectionBoxesAc.back().y;

	// ***** Voice Chat (Team) ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxesAc.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	{
		CUIRect VoiceHeaderLabel = Label;
		CUIRect VoiceBetaBadge;
		VoiceHeaderLabel.VSplitRight(44.0f, &VoiceHeaderLabel, &VoiceBetaBadge);
		Ui()->DoLabel(&VoiceHeaderLabel, TCLocalize("Voice Chat (Team)"), HeadlineFontSize, TEXTALIGN_ML);

		const float BadgeHeight = std::min(16.0f, VoiceBetaBadge.h);
		VoiceBetaBadge.HMargin((VoiceBetaBadge.h - BadgeHeight) * 0.5f, &VoiceBetaBadge);
		VoiceBetaBadge.Draw(ColorRGBA(0.95f, 0.55f, 0.15f, 0.75f), IGraphics::CORNER_ALL, 4.0f);
		CUIRect BetaInner = VoiceBetaBadge;
		BetaInner.Margin(1.0f, &BetaInner);
		BetaInner.Draw(ColorRGBA(0.12f, 0.13f, 0.16f, 0.95f), IGraphics::CORNER_ALL, 3.0f);
		TextRender()->TextColor(ColorRGBA(1.0f, 0.68f, 0.22f, 1.0f));
		Ui()->DoLabel(&BetaInner, "BETA", 10.0f, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoCheckboxWithHelp(&g_Config.m_AcVoiceEnabled, TCLocalize("Activar Voice Chat por Team"), TCLocalize("Permite hablar por voz usando Opus y UDP con tus companeros del mismo team sin usar apps externas."), &g_Config.m_AcVoiceEnabled, &Column, LineSize);
	static CButtonContainer s_ReaderButtonVoiceToggle, s_ClearButtonVoiceToggle;
	DoLine_KeyReader(Column, s_ReaderButtonVoiceToggle, s_ClearButtonVoiceToggle, TCLocalize("Toggle Voice Chat key"), "ac_voice_toggle");
	DoCheckboxWithHelp(&g_Config.m_AcVoiceOnlyTeam, TCLocalize("Solo en Team privado (Team > 0)"), TCLocalize("Si esta activo, solo se conecta al chat de voz cuando entras a un team privado (> 0) y no en team 0."), &g_Config.m_AcVoiceOnlyTeam, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	Column.HSplitTop(LineSize, &Button, &Column);
	{
		static CButtonContainer s_aVoiceModeBtns[2];
		CUIRect LeftBtn, RightBtn;
		Button.VSplitMid(&LeftBtn, &RightBtn, 2.0f);
		if(DoButton_Menu(&s_aVoiceModeBtns[0], TCLocalize("Push-to-Talk"), g_Config.m_AcVoiceMode == 0, &LeftBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L))
			g_Config.m_AcVoiceMode = 0;
		if(DoButton_Menu(&s_aVoiceModeBtns[1], TCLocalize("Deteccion de Voz (VAD)"), g_Config.m_AcVoiceMode == 1, &RightBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R))
			g_Config.m_AcVoiceMode = 1;
	}
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	if(g_Config.m_AcVoiceMode == 0)
	{
		static CButtonContainer s_ReaderButtonVoicePtt, s_ClearButtonVoicePtt;
		DoLine_KeyReader(Column, s_ReaderButtonVoicePtt, s_ClearButtonVoicePtt, TCLocalize("Tecla Push-to-Talk"), "+ac_voice_ptt");
	}
	else
	{
		Column.HSplitTop(LineSize, &Button, &Column);
		Ui()->DoScrollbarOption(&g_Config.m_AcVoiceVadThreshold, &g_Config.m_AcVoiceVadThreshold, &Button, TCLocalize("Sensibilidad VAD"), 1, 100, &CUi::ms_LinearScrollbarScale, 0, "%");
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_AcVoiceVolume, &g_Config.m_AcVoiceVolume, &Button, TCLocalize("Volumen de voz recibida"), 0, 200, &CUi::ms_LinearScrollbarScale, 0, "%");

	{
		struct SAudioSelectorBtns
		{
			CButtonContainer m_Prev, m_Cur, m_Next, m_Refresh;
		};
		auto RenderAudioDeviceRow = [&](const char *pLabelText, const char *pRefreshTooltip, const std::vector<std::string> &vDevices, const char *pCurDeviceName, SAudioSelectorBtns &Btns, auto &&SelectFn, auto &&RefreshFn) {
			Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);

			const int NumDevs = (int)vDevices.size();
			int CurIdx = -1; // -1 = system default
			if(pCurDeviceName[0] != '\0')
			{
				for(int i = 0; i < NumDevs; ++i)
				{
					if(str_comp(vDevices[i].c_str(), pCurDeviceName) == 0)
					{
						CurIdx = i;
						break;
					}
				}
			}

			CUIRect LabelDev, ControlsDev, PrevBtn, CurBtn, NextBtn, RefreshBtn;
			Button.VSplitLeft(95.0f, &LabelDev, &ControlsDev);
			Ui()->DoLabel(&LabelDev, pLabelText, FontSize, TEXTALIGN_ML);

			ControlsDev.VSplitRight(LineSize + 4.0f, &ControlsDev, &RefreshBtn);
			ControlsDev.VSplitRight(4.0f, &ControlsDev, nullptr);
			ControlsDev.VSplitLeft(22.0f, &PrevBtn, &ControlsDev);
			ControlsDev.VSplitRight(22.0f, &ControlsDev, &NextBtn);
			ControlsDev.VSplitLeft(2.0f, nullptr, &ControlsDev);
			ControlsDev.VSplitRight(2.0f, &ControlsDev, nullptr);
			CurBtn = ControlsDev;

			auto CycleDev = [&](int Delta) {
				if(NumDevs <= 0)
				{
					SelectFn(-1);
					return;
				}
				int Pos = CurIdx + 1 + Delta; // 0..NumDevs
				const int Total = NumDevs + 1;
				Pos = ((Pos % Total) + Total) % Total;
				SelectFn(Pos - 1);
			};

			if(DoButton_Menu(&Btns.m_Prev, "<", 0, &PrevBtn))
				CycleDev(-1);

			const char *pDisplay = (CurIdx >= 0) ? vDevices[CurIdx].c_str() : (pCurDeviceName[0] ? pCurDeviceName : TCLocalize("Predeterminado"));
			if(DoButton_Menu(&Btns.m_Cur, pDisplay, 0, &CurBtn))
				CycleDev(1);

			if(DoButton_Menu(&Btns.m_Next, ">", 0, &NextBtn))
				CycleDev(1);

			TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
			TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
			if(DoButton_Menu(&Btns.m_Refresh, FontIcon::ARROW_ROTATE_RIGHT, 0, &RefreshBtn))
				RefreshFn();
			TextRender()->SetRenderFlags(0);
			TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);
			GameClient()->m_Tooltips.DoToolTip(&Btns.m_Refresh, &RefreshBtn, pRefreshTooltip);
		};

		static SAudioSelectorBtns s_MicBtns, s_OutBtns;
		RenderAudioDeviceRow(
			TCLocalize("Microfono:"),
			TCLocalize("Actualizar lista de microfonos"),
			GameClient()->m_AClientVoice.GetMicDevices(),
			g_Config.m_AcVoiceMicDevice,
			s_MicBtns,
			[&](int Idx) { GameClient()->m_AClientVoice.SelectMicDevice(Idx); },
			[&]() { GameClient()->m_AClientVoice.RefreshMicDevices(); });

		RenderAudioDeviceRow(
			TCLocalize("Salida voz:"),
			TCLocalize("Actualizar lista de salidas de audio"),
			GameClient()->m_AClientVoice.GetOutputDevices(),
			g_Config.m_AcVoiceOutputDevice,
			s_OutBtns,
			[&](int Idx) { GameClient()->m_AClientVoice.SelectOutputDevice(Idx); },
			[&]() { GameClient()->m_AClientVoice.RefreshOutputDevices(); });
	}

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_AcVoiceMicGain, &g_Config.m_AcVoiceMicGain, &Button, TCLocalize("Ganancia de microfono"), 20, 300, &CUi::ms_LinearScrollbarScale, 0, "%");

	DoCheckboxWithHelp(&g_Config.m_AcVoiceHud, TCLocalize("Mostrar indicador de voz en HUD"), TCLocalize("Muestra en el HUD quienes estan hablando en tu team."), &g_Config.m_AcVoiceHud, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_AcVoiceLoopback, TCLocalize("Probar microfono (Loopback)"), TCLocalize("Te permite escucharte a vos mismo localmente pasando por Opus para probar el mic sin otra persona."), &g_Config.m_AcVoiceLoopback, &Column, LineSize);

	Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	Column.HSplitTop(LineSize, &Button, &Column);
	{
		CUIRect MeterBg, MeterFill;
		Button.VSplitLeft(120.0f, &Label, &MeterBg);
		Ui()->DoLabel(&Label, TCLocalize("Nivel de mic:"), FontSize, TEXTALIGN_ML);
		MeterBg.HMargin(4.0f, &MeterBg);
		MeterBg.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.45f), IGraphics::CORNER_ALL, 3.0f);
		const float Level = GameClient()->m_AClientVoice.GetLocalMicLevel();
		if(Level > 0.01f)
		{
			MeterFill = MeterBg;
			MeterFill.w = MeterBg.w * std::clamp(Level, 0.0f, 1.0f);
			const bool ActiveTx = GameClient()->m_AClientVoice.IsTransmitting();
			MeterFill.Draw(ActiveTx ? ColorRGBA(0.25f, 0.92f, 0.45f, 0.90f) : ColorRGBA(0.45f, 0.70f, 0.95f, 0.75f), IGraphics::CORNER_ALL, 3.0f);
		}
	}

	Column.HSplitTop(LineSize, &Button, &Column);
	{
		CAClientVoice &Voice = GameClient()->m_AClientVoice;
		char aVoiceStatus[128];
		if(!g_Config.m_AcVoiceEnabled)
		{
			str_copy(aVoiceStatus, TCLocalize("Estado: Desactivado"));
			TextRender()->TextColor(0.75f, 0.75f, 0.78f, 0.9f);
		}
		else if(!Voice.HasValidRelayAddr())
		{
			str_copy(aVoiceStatus, TCLocalize("Estado: Direccion de Relay invalida"));
			TextRender()->TextColor(1.0f, 0.45f, 0.45f, 0.95f);
		}
		else if(Voice.IsInCustomRoom())
		{
			str_format(aVoiceStatus, sizeof(aVoiceStatus), "Estado: Sala Privada '%s' (%zu amigo%s)", Voice.GetCustomRoomCode(), Voice.GetRoomPeers().size(), Voice.GetRoomPeers().size() == 1 ? "" : "s");
			TextRender()->TextColor(0.40f, 0.95f, 0.75f, 0.95f);
		}
		else if(Voice.IsRoomActive())
		{
			str_format(aVoiceStatus, sizeof(aVoiceStatus), "Estado: Conectado (Team %d)", Voice.GetCurrentTeam());
			TextRender()->TextColor(0.40f, 0.95f, 0.55f, 0.95f);
		}
		else
		{
			str_copy(aVoiceStatus, TCLocalize("Estado: En espera (entra a un team o sala privada)"));
			TextRender()->TextColor(0.95f, 0.85f, 0.45f, 0.95f);
		}
		Ui()->DoLabel(&Button, aVoiceStatus, FontSize * 0.9f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	// Banner de invitacion pendiente a sala privada
	{
		CAClientVoice &Voice = GameClient()->m_AClientVoice;
		if(Voice.HasPendingInvite())
		{
			Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
			Column.HSplitTop(LineSize + 4.0f, &Button, &Column);
			Button.Draw(ColorRGBA(0.14f, 0.28f, 0.48f, 0.85f), IGraphics::CORNER_ALL, 4.0f);
			CUIRect InviteRow = Button;
			InviteRow.Margin(2.0f, &InviteRow);

			CUIRect AcceptBtn, DeclineBtn;
			InviteRow.VSplitRight(58.0f, &InviteRow, &DeclineBtn);
			InviteRow.VSplitRight(4.0f, &InviteRow, nullptr);
			InviteRow.VSplitRight(58.0f, &InviteRow, &AcceptBtn);
			InviteRow.VSplitRight(6.0f, &InviteRow, nullptr);

			char aInvBanner[144];
			str_format(aInvBanner, sizeof(aInvBanner), "Invitacion de %s (%s%s)",
				Voice.GetPendingInviteFrom(), Voice.GetPendingInviteRoom(),
				Voice.HasPendingInvitePassword() ? " + clave" : "");
			TextRender()->TextColor(0.95f, 0.98f, 1.0f, 1.0f);
			Ui()->DoLabel(&InviteRow, aInvBanner, FontSize * 0.85f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			static CButtonContainer s_BtnAcceptInvite, s_BtnDeclineInvite;
			if(DoButton_Menu(&s_BtnAcceptInvite, TCLocalize("Aceptar"), 0, &AcceptBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 3.0f, 0.0f, ColorRGBA(0.25f, 0.78f, 0.40f, 0.85f)))
			{
				Voice.AcceptPendingInvite();
			}
			if(DoButton_Menu(&s_BtnDeclineInvite, TCLocalize("Rechazar"), 0, &DeclineBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 3.0f, 0.0f, ColorRGBA(0.80f, 0.28f, 0.28f, 0.80f)))
			{
				Voice.DeclinePendingInvite();
			}
		}
	}

	// Salas Privadas (Amigos)
	{
		CAClientVoice &Voice = GameClient()->m_AClientVoice;
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Label, &Column);
		Ui()->DoLabel(&Label, TCLocalize("Salas Privadas (Amigos):"), FontSize, TEXTALIGN_ML);

		static CLineInputBuffered<32> s_VoiceRoomInput;
		static CLineInputBuffered<32> s_VoiceRoomPassInput;
		static CLineInputBuffered<MAX_NAME_LENGTH> s_VoiceInviteInput;
		static CButtonContainer s_BtnCreateRoom, s_BtnJoinRoom, s_BtnCopyRoom, s_BtnLeaveRoom, s_BtnSendInvite;

		if(!Voice.IsInCustomRoom())
		{
			Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			CUIRect BoxRect, JoinBtn, CreateBtn;
			Button.VSplitRight(78.0f, &BoxRect, &CreateBtn);
			BoxRect.VSplitRight(4.0f, &BoxRect, nullptr);
			BoxRect.VSplitRight(60.0f, &BoxRect, &JoinBtn);
			BoxRect.VSplitRight(4.0f, &BoxRect, nullptr);

			s_VoiceRoomInput.SetEmptyText(TCLocalize("Codigo de sala..."));
			Ui()->DoEditBox(&s_VoiceRoomInput, &BoxRect, FontSize * 0.85f);

			Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			s_VoiceRoomPassInput.SetEmptyText(TCLocalize("Contraseña (opcional)..."));
			Ui()->DoEditBox(&s_VoiceRoomPassInput, &Button, FontSize * 0.85f);

			const bool RoomEnter = (s_VoiceRoomInput.IsActive() || s_VoiceRoomPassInput.IsActive()) && (Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || Input()->KeyPress(KEY_RETURN) || Input()->KeyPress(KEY_KP_ENTER));
			if(DoButton_Menu(&s_BtnJoinRoom, TCLocalize("Unirse"), 0, &JoinBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ColorRGBA(0.25f, 0.58f, 0.90f, 0.75f)) || RoomEnter)
			{
				if(s_VoiceRoomInput.GetString()[0] != '\0')
					Voice.JoinCustomRoom(s_VoiceRoomInput.GetString(), s_VoiceRoomPassInput.GetString());
				else
					Voice.CreateCustomRoom(nullptr, s_VoiceRoomPassInput.GetString());
			}
			if(DoButton_Menu(&s_BtnCreateRoom, TCLocalize("Crear Sala"), 0, &CreateBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ColorRGBA(0.28f, 0.75f, 0.45f, 0.80f)))
			{
				Voice.CreateCustomRoom(s_VoiceRoomInput.GetString(), s_VoiceRoomPassInput.GetString());
			}
		}
		else
		{
			Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			CUIRect RoomInfoRect, CopyBtn, LeaveBtn;
			Button.VSplitRight(82.0f, &RoomInfoRect, &LeaveBtn);
			RoomInfoRect.VSplitRight(4.0f, &RoomInfoRect, nullptr);
			RoomInfoRect.VSplitRight(56.0f, &RoomInfoRect, &CopyBtn);
			RoomInfoRect.VSplitRight(4.0f, &RoomInfoRect, nullptr);

			char aActiveRoom[112];
			if(Voice.HasCustomRoomPassword())
				str_format(aActiveRoom, sizeof(aActiveRoom), "Sala: %s | Clave: %s", Voice.GetCustomRoomCode(), Voice.GetCustomRoomPassword());
			else
				str_format(aActiveRoom, sizeof(aActiveRoom), "Sala: %s", Voice.GetCustomRoomCode());
			TextRender()->TextColor(0.45f, 0.95f, 0.75f, 1.0f);
			Ui()->DoLabel(&RoomInfoRect, aActiveRoom, FontSize * 0.88f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			if(DoButton_Menu(&s_BtnCopyRoom, TCLocalize("Copiar"), 0, &CopyBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ColorRGBA(0.30f, 0.45f, 0.65f, 0.75f)))
			{
				if(Voice.HasCustomRoomPassword())
				{
					char aClip[96];
					str_format(aClip, sizeof(aClip), "Sala: %s | Clave: %s", Voice.GetCustomRoomCode(), Voice.GetCustomRoomPassword());
					Input()->SetClipboardText(aClip);
					GameClient()->Echo("[AClient Voice] Sala y clave copiadas al portapapeles");
				}
				else
				{
					Input()->SetClipboardText(Voice.GetCustomRoomCode());
					GameClient()->Echo("[AClient Voice] Codigo de sala copiado al portapapeles");
				}
			}
			if(DoButton_Menu(&s_BtnLeaveRoom, TCLocalize("Salir Sala"), 0, &LeaveBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ColorRGBA(0.82f, 0.28f, 0.28f, 0.80f)))
			{
				Voice.LeaveCustomRoom();
				s_VoiceRoomPassInput.Clear();
			}

			Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			CUIRect InvBox, InvBtn;
			Button.VSplitRight(78.0f, &InvBox, &InvBtn);
			InvBox.VSplitRight(4.0f, &InvBox, nullptr);

			s_VoiceInviteInput.SetEmptyText(TCLocalize("Nombre de amigo a invitar..."));
			Ui()->DoEditBox(&s_VoiceInviteInput, &InvBox, FontSize * 0.85f);

			const bool InvEnter = s_VoiceInviteInput.IsActive() && (Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || Input()->KeyPress(KEY_RETURN) || Input()->KeyPress(KEY_KP_ENTER));
			if(DoButton_Menu(&s_BtnSendInvite, TCLocalize("Invitar"), 0, &InvBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ColorRGBA(0.30f, 0.62f, 0.95f, 0.80f)) || InvEnter)
			{
				if(s_VoiceInviteInput.GetString()[0] != '\0')
				{
					Voice.SendRoomInvite(s_VoiceInviteInput.GetString());
					s_VoiceInviteInput.Clear();
				}
			}
		}
	}

	// Volumen individual por jugador (solo personas disponibles para hablar en la sala)
	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(LineSize, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Volumen por jugador:"), FontSize, TEXTALIGN_ML);
	{
		CAClientVoice &Voice = GameClient()->m_AClientVoice;
		static CButtonContainer s_aPeerMuteBtns[MAX_CLIENTS];
		static CButtonContainer s_aPeerInviteBtns[MAX_CLIENTS];

		const auto vAvailablePeers = Voice.GetAvailableVoicePeers();
		int ShownPeers = 0;
		for(const auto &Peer : vAvailablePeers)
		{
			const int Slot = Peer.m_VirtualSlot;
			if(Slot < 0 || Slot >= MAX_CLIENTS)
				continue;

			Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
			Column.HSplitTop(LineSize, &Button, &Column);
			CUIRect SliderRect, MuteBtn;
			Button.VSplitRight(48.0f, &SliderRect, &MuteBtn);
			SliderRect.VSplitRight(4.0f, &SliderRect, nullptr);

			int *pPeerVol = Voice.GetClientVolumePtr(Slot);
			if(pPeerVol)
			{
				const int PrevVol = *pPeerVol;
				const char *pPeerName = Peer.m_aName[0] ? Peer.m_aName : (Voice.IsInCustomRoom() ? "Amigo" : "Jugador");
				Ui()->DoScrollbarOption(pPeerVol, pPeerVol, &SliderRect, pPeerName, 0, 200, &CUi::ms_LinearScrollbarScale, 0, "%");
				if(*pPeerVol != PrevVol)
					Voice.NotifyClientVolumeChanged(Slot);
			}

			const bool Muted = Voice.IsClientMuted(Slot);
			const ColorRGBA MuteColor = Muted ? ColorRGBA(0.85f, 0.25f, 0.25f, 0.85f) : ColorRGBA(0.25f, 0.28f, 0.35f, 0.65f);
			if(DoButton_Menu(&s_aPeerMuteBtns[Slot], Muted ? "Muted" : "Mute", 0, &MuteBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, MuteColor))
			{
				Voice.ToggleClientMute(Slot);
			}
			++ShownPeers;
		}

		if(ShownPeers == 0)
		{
			Column.HSplitTop(LineSize, &Button, &Column);
			TextRender()->TextColor(0.65f, 0.68f, 0.75f, 0.85f);
			const char *pEmptyMsg = TCLocalize("Ningun jugador en tu sala tiene Voice Chat activo ahora.");
			if(!g_Config.m_AcVoiceEnabled)
				pEmptyMsg = TCLocalize("Activa Voice Chat para ver con quien puedes hablar.");
			else if(Voice.IsInCustomRoom())
				pEmptyMsg = TCLocalize("Esperando amigos en la sala privada...");
			else if(!Voice.IsRoomActive())
				pEmptyMsg = TCLocalize("Entra a un /team (> 0) o crea una sala privada arriba.");
			Ui()->DoLabel(&Button, pEmptyMsg, FontSize * 0.85f, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());
		}

		if(Voice.IsInCustomRoom() && Client()->State() == IClient::STATE_ONLINE)
		{
			const int ActiveDummy = std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
			const int LocalId = GameClient()->m_aLocalIds[ActiveDummy];
			int ServerCandidates = 0;
			for(int i = 0; i < MAX_CLIENTS; ++i)
			{
				if(i == LocalId || !GameClient()->m_aClients[i].m_Active)
					continue;
				const char *pSrvName = GameClient()->m_aClients[i].m_aName;
				bool AlreadyInRoom = false;
				for(const auto &Peer : vAvailablePeers)
				{
					if(str_comp_nocase(Peer.m_aName, pSrvName) == 0)
					{
						AlreadyInRoom = true;
						break;
					}
				}
				if(AlreadyInRoom)
					continue;

				if(ServerCandidates == 0)
				{
					Column.HSplitTop(MarginSmall, nullptr, &Column);
					Column.HSplitTop(LineSize, &Label, &Column);
					Ui()->DoLabel(&Label, TCLocalize("Invitar del servidor actual:"), FontSize * 0.9f, TEXTALIGN_ML);
				}
				Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
				Column.HSplitTop(LineSize, &Button, &Column);
				CUIRect NameRect, InvBtn;
				Button.VSplitRight(56.0f, &NameRect, &InvBtn);
				NameRect.VSplitRight(4.0f, &NameRect, nullptr);

				Ui()->DoLabel(&NameRect, pSrvName, FontSize * 0.85f, TEXTALIGN_ML);
				if(DoButton_Menu(&s_aPeerInviteBtns[i], TCLocalize("Invitar"), 0, &InvBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ColorRGBA(0.28f, 0.58f, 0.90f, 0.75f)))
				{
					Voice.SendRoomInvite(pSrvName);
				}
				++ServerCandidates;
			}
		}
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	s_SectionBoxesAc.back().h = Column.y - s_SectionBoxesAc.back().y;
	}

	RightView = Column;

	if(!VisualTab)
	{
	// ***** Bloque: Configuraciones de jugadores ***** //
	{
		CUIRect ConfigSyncSection;
		ConfigSyncSection.x = LeftView.x;
		ConfigSyncSection.y = std::max(LeftView.y, RightView.y) + MarginBetweenSections;
		ConfigSyncSection.w = (RightView.x + RightView.w) - LeftView.x;
		ConfigSyncSection.h = 115.0f;

		s_SectionBoxesAc.push_back(ConfigSyncSection);

		const float UsedHeight = RenderPlayerConfigsPanel(ConfigSyncSection, false, nullptr);

		s_SectionBoxesAc.back().h = UsedHeight + MarginSmall;
		LeftView.y = ConfigSyncSection.y + UsedHeight + MarginSmall;
		RightView.y = ConfigSyncSection.y + UsedHeight + MarginSmall;
	}

	// ***** Bloque: Actualizador Integrado de AClient ***** //
	CUIRect UpdateSection;
	UpdateSection.x = LeftView.x;
	UpdateSection.y = std::max(LeftView.y, RightView.y) + MarginBetweenSections;
	UpdateSection.w = (RightView.x + RightView.w) - LeftView.x;
	UpdateSection.h = 75.0f;

	s_SectionBoxesAc.push_back(UpdateSection);

	UpdateSection.HSplitTop(HeadlineHeight, &Label, &UpdateSection);
	Ui()->DoLabel(&Label, TCLocalize("AClient In-Game Auto-Updater"), HeadlineFontSize, TEXTALIGN_ML);
	UpdateSection.HSplitTop(MarginSmall, nullptr, &UpdateSection);

	CUIRect StatusLine, ActionBtn;
	UpdateSection.HSplitTop(LineSize * 1.5f, &StatusLine, &UpdateSection);
	StatusLine.VSplitRight(140.0f, &StatusLine, &ActionBtn);

	const auto UpState = GameClient()->m_TClient.GetUpdateState();
	const char *pCurVer = ACLIENT_VERSION;
	const char *pLatVer = GameClient()->m_TClient.GetLatestVersion();

	char aStatusText[256];
	if(UpState == CTClient::UPDATE_STATE_AVAILABLE)
	{
		str_format(aStatusText, sizeof(aStatusText), "Nueva version disponible: %s (Instalada: %s)", pLatVer, pCurVer);
		TextRender()->TextColor(0.4f, 0.95f, 0.4f, 1.0f);
		static CButtonContainer s_BtnUpdateAc;
		if(DoButton_Menu(&s_BtnUpdateAc, TCLocalize("Actualizar ahora"), 0, &ActionBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.45f, 0.85f, 0.6f)))
		{
			GameClient()->m_TClient.InitiateUpdate();
		}
	}
	else if(UpState == CTClient::UPDATE_STATE_DOWNLOADING)
	{
		int Pct = GameClient()->m_TClient.GetUpdatePercent();
		str_format(aStatusText, sizeof(aStatusText), "Descargando DDNet.exe: %d%%", Pct);
		TextRender()->TextColor(0.4f, 0.8f, 1.0f, 1.0f);
		Ui()->RenderProgressBar(ActionBtn, Pct / 100.0f);
	}
	else if(UpState == CTClient::UPDATE_STATE_NEED_RESTART)
	{
		str_copy(aStatusText, "Actualizacion descargada. Reinicie para aplicar.", sizeof(aStatusText));
		TextRender()->TextColor(0.3f, 1.0f, 0.3f, 1.0f);
		static CButtonContainer s_BtnRestartAc;
		if(DoButton_Menu(&s_BtnRestartAc, TCLocalize("Reiniciar"), 0, &ActionBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.2f, 0.75f, 0.2f, 0.6f)))
		{
			Client()->Restart();
		}
	}
	else if(UpState == CTClient::UPDATE_STATE_CHECKING)
	{
		str_copy(aStatusText, "Comprobando ultima version en GitHub Releases...", sizeof(aStatusText));
		TextRender()->TextColor(0.7f, 0.85f, 1.0f, 1.0f);
	}
	else
	{
		str_format(aStatusText, sizeof(aStatusText), "AClient v%s (Cliente al dia)", pCurVer);
		TextRender()->TextColor(0.85f, 0.88f, 0.95f, 1.0f);
		static CButtonContainer s_BtnCheckAc;
		if(DoButton_Menu(&s_BtnCheckAc, TCLocalize("Buscar ahora"), 0, &ActionBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
		{
			GameClient()->m_TClient.FetchTClientInfo(true);
		}
	}
	Ui()->DoLabel(&StatusLine, aStatusText, 13.0f, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	s_SectionBoxesAc.back().h = UpdateSection.y - s_SectionBoxesAc.back().y + MarginSmall;

	LeftView.y = UpdateSection.y + MarginSmall;
	RightView.y = UpdateSection.y + MarginSmall;

	// ***** Bloque Inferior: Noticias y Novedades de Versiones ***** //
	CUIRect BottomWhatsNew;
	BottomWhatsNew.x = LeftView.x;
	BottomWhatsNew.y = std::max(LeftView.y, RightView.y) + MarginBetweenSections;
	BottomWhatsNew.w = (RightView.x + RightView.w) - LeftView.x;
	BottomWhatsNew.h = 360.0f;

	s_SectionBoxesAc.push_back(BottomWhatsNew);

	BottomWhatsNew.HSplitTop(HeadlineHeight, &Label, &BottomWhatsNew);
	TextRender()->TextColor(1.0f, 0.88f, 0.35f, 1.0f);
	Ui()->DoLabel(&Label, TCLocalize("Noticias y Notas de Versiones de AClient"), HeadlineFontSize, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	BottomWhatsNew.HSplitTop(MarginSmall, nullptr, &BottomWhatsNew);

	const auto &vReleases = GameClient()->m_TClient.GetReleases();
	int CurSelected = GameClient()->m_TClient.GetSelectedRelease();
	if(CurSelected < 0 || CurSelected >= (int)vReleases.size())
		CurSelected = 0;

	if(vReleases.empty())
	{
		CUIRect EmptyRect;
		BottomWhatsNew.HSplitTop(22.0f, &EmptyRect, &BottomWhatsNew);
		TextRender()->TextColor(0.75f, 0.75f, 0.85f, 0.9f);
		Ui()->DoLabel(&EmptyRect, TCLocalize("Obteniendo versiones desde GitHub Releases..."), 12.0f, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
	else
	{
		static std::vector<std::string> s_vReleaseNames;
		static std::vector<const char *> s_vpReleaseNames;
		static CUi::SDropDownState s_ReleaseDropDownState;
		static CScrollRegion s_ReleaseDropDownScrollRegion;
		s_ReleaseDropDownState.m_SelectionPopupContext.m_pScrollRegion = &s_ReleaseDropDownScrollRegion;

		s_vReleaseNames.clear();
		s_vpReleaseNames.clear();
		s_vReleaseNames.reserve(vReleases.size());
		s_vpReleaseNames.reserve(vReleases.size());
		bool FoundLatestOfficial = false;
		for(size_t i = 0; i < vReleases.size(); ++i)
		{
			char aTabLabel[64];
			if(vReleases[i].m_IsPrerelease)
			{
				str_format(aTabLabel, sizeof(aTabLabel), "%s (Pre-release)", vReleases[i].m_aTag);
			}
			else if(!FoundLatestOfficial)
			{
				FoundLatestOfficial = true;
				str_format(aTabLabel, sizeof(aTabLabel), "%s (Reciente)", vReleases[i].m_aTag);
			}
			else
			{
				str_copy(aTabLabel, vReleases[i].m_aTag, sizeof(aTabLabel));
			}
			s_vReleaseNames.emplace_back(aTabLabel);
		}
		for(const auto &Str : s_vReleaseNames) 
			s_vpReleaseNames.push_back(Str.c_str());

		// Fila del dropdown selector de versiones
		CUIRect VersionSelectRow;
		BottomWhatsNew.HSplitTop(22.0f, &VersionSelectRow, &BottomWhatsNew);

		CUIRect VerLabel, DropDownRect, DateRect;
		VersionSelectRow.VSplitLeft(140.0f, &VerLabel, &VersionSelectRow);
		VersionSelectRow.VSplitLeft(200.0f, &DropDownRect, &VersionSelectRow);
		VersionSelectRow.VSplitRight(160.0f, &VersionSelectRow, &DateRect);

		Ui()->DoLabel(&VerLabel, TCLocalize("Seleccionar versión:"), 12.0f, TEXTALIGN_ML);

		const int NewSelected = Ui()->DoDropDown(&DropDownRect, CurSelected, s_vpReleaseNames.data(), s_vpReleaseNames.size(), s_ReleaseDropDownState);
		if(NewSelected != CurSelected && NewSelected >= 0 && NewSelected < (int)vReleases.size())
		{
			GameClient()->m_TClient.SetSelectedRelease(NewSelected);
			CurSelected = NewSelected;
		}

		const CAClientRelease &Rel = vReleases[CurSelected];

		if(Rel.m_aPublishedAt[0] != '\0')
		{
			char aDateStr[64];
			str_format(aDateStr, sizeof(aDateStr), "Fecha: %s", Rel.m_aPublishedAt);
			TextRender()->TextColor(0.70f, 0.72f, 0.82f, 0.85f);
			Ui()->DoLabel(&DateRect, aDateStr, 11.5f, TEXTALIGN_MR);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}

		BottomWhatsNew.HSplitTop(MarginSmall, nullptr, &BottomWhatsNew);

		// Linea divisoria estetica
		CUIRect Divider;
		BottomWhatsNew.HSplitTop(1.0f, &Divider, &BottomWhatsNew);
		Divider.Draw(GetThemeColor(0.35f), 0, 0.0f);
		BottomWhatsNew.HSplitTop(MarginSmall, nullptr, &BottomWhatsNew);

		// Encabezado de la release seleccionada
		CUIRect RelHeader;
		BottomWhatsNew.HSplitTop(20.0f, &RelHeader, &BottomWhatsNew);

		char aHeaderTitle[128];
		if(Rel.m_aName[0] != '\0' && str_comp(Rel.m_aName, Rel.m_aTag) != 0)
			str_format(aHeaderTitle, sizeof(aHeaderTitle), "%s: %s", Rel.m_aTag, Rel.m_aName);
		else
			str_format(aHeaderTitle, sizeof(aHeaderTitle), "%s", Rel.m_aTag);

		TextRender()->TextColor(0.45f, 0.92f, 1.0f, 1.0f);
		Ui()->DoLabel(&RelHeader, aHeaderTitle, 13.0f, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

		BottomWhatsNew.HSplitTop(MarginSmall, nullptr, &BottomWhatsNew);

		// Renderizar cuerpo de la release (Markdown interpretado)
		if(Rel.m_Body.empty())
		{
			CUIRect EmptyRect;
			BottomWhatsNew.HSplitTop(20.0f, &EmptyRect, &BottomWhatsNew);
			TextRender()->TextColor(0.70f, 0.70f, 0.80f, 0.8f);
			Ui()->DoLabel(&EmptyRect, TCLocalize("No hay descripcion detallada para esta version."), 12.0f, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
		else
		{
			const char *pBody = Rel.m_Body.c_str();
			while(*pBody)
			{
				const char *pNext = strchr(pBody, '\n');
				std::string Line;
				if(pNext)
				{
					Line.assign(pBody, pNext - pBody);
					pBody = pNext + 1;
				}
				else
				{
					Line.assign(pBody);
					pBody += Line.size();
				}
				if(!Line.empty() && Line.back() == '\r')
					Line.pop_back();

				// Limpiar links markdown [Texto](URL) -> Texto
				size_t LinkStart = 0;
				while((LinkStart = Line.find('[', LinkStart)) != std::string::npos)
				{
					size_t LinkMid = Line.find("](", LinkStart);
					if(LinkMid != std::string::npos)
					{
						size_t LinkEnd = Line.find(')', LinkMid + 2);
						if(LinkEnd != std::string::npos)
						{
							std::string Label = Line.substr(LinkStart + 1, LinkMid - LinkStart - 1);
							Line.replace(LinkStart, LinkEnd - LinkStart + 1, Label);
							LinkStart += Label.length();
							continue;
						}
					}
					LinkStart++;
				}

				// Limpiar acentos graves `
				size_t Tick = 0;
				while((Tick = Line.find('`', Tick)) != std::string::npos)
					Line.erase(Tick, 1);

				// Trim derecho
				while(!Line.empty() && (Line.back() == ' ' || Line.back() == '\t'))
					Line.pop_back();

				// Indentacion
				int IndentSpaces = 0;
				while(IndentSpaces < (int)Line.size() && Line[IndentSpaces] == ' ')
					IndentSpaces++;

				std::string TrimmedLine = (IndentSpaces > 0) ? Line.substr(IndentSpaces) : Line;

				if(TrimmedLine.empty())
				{
					BottomWhatsNew.HSplitTop(4.0f, nullptr, &BottomWhatsNew);
					continue;
				}

				if(TrimmedLine.starts_with("## "))
				{
					std::string Heading = TrimmedLine.substr(3);
					CUIRect LineRect;
					BottomWhatsNew.HSplitTop(20.0f, &LineRect, &BottomWhatsNew);
					TextRender()->TextColor(1.0f, 0.88f, 0.35f, 1.0f);
					Ui()->DoLabel(&LineRect, Heading.c_str(), 13.0f, TEXTALIGN_ML);
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
					BottomWhatsNew.HSplitTop(2.0f, nullptr, &BottomWhatsNew);
				}
				else if(TrimmedLine.starts_with("### "))
				{
					std::string SubHeading = TrimmedLine.substr(4);
					CUIRect LineRect;
					BottomWhatsNew.HSplitTop(18.0f, &LineRect, &BottomWhatsNew);
					TextRender()->TextColor(0.80f, 0.65f, 1.0f, 1.0f);
					Ui()->DoLabel(&LineRect, SubHeading.c_str(), 12.0f, TEXTALIGN_ML);
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
					BottomWhatsNew.HSplitTop(2.0f, nullptr, &BottomWhatsNew);
				}
				else if(TrimmedLine.starts_with("- ") || TrimmedLine.starts_with("* "))
				{
					std::string Bullet = TrimmedLine.substr(2);
					size_t BoldStart = Bullet.find("**");
					size_t BoldEnd = (BoldStart != std::string::npos) ? Bullet.find("**", BoldStart + 2) : std::string::npos;

					float IndentOffset = (IndentSpaces >= 2) ? 14.0f : 0.0f;
					CUIRect BulletArea = BottomWhatsNew;
					if(IndentOffset > 0.0f)
						BulletArea.VSplitLeft(IndentOffset, nullptr, &BulletArea);

					if(BoldStart == 0 && BoldEnd != std::string::npos)
					{
						std::string Title = Bullet.substr(2, BoldEnd - 2);
						std::string Rest = Bullet.substr(BoldEnd + 2);
						if(Rest.starts_with(": "))
							Rest = Rest.substr(2);
						else if(Rest.starts_with(":"))
							Rest = Rest.substr(1);

						size_t Ast = 0;
						while((Ast = Rest.find("**", Ast)) != std::string::npos)
							Rest.erase(Ast, 2);

						std::string BulletTitle = "* " + Title + ": ";
						float TitleW = TextRender()->TextWidth(12.0f, BulletTitle.c_str()) + 4.0f;
						float AvailableW = BulletArea.w - TitleW - 6.0f;

						if(AvailableW > 120.0f)
						{
							STextBoundingBox Box = TextRender()->TextBoundingBox(11.5f, Rest.c_str(), -1, AvailableW);
							float LineH = std::max(18.0f, Box.m_H + 3.0f);
							CUIRect LineRect;
							BottomWhatsNew.HSplitTop(LineH, &LineRect, &BottomWhatsNew);
							if(IndentOffset > 0.0f)
								LineRect.VSplitLeft(IndentOffset, nullptr, &LineRect);

							CUIRect TitleBox, DetailBox;
							LineRect.VSplitLeft(TitleW, &TitleBox, &DetailBox);

							TextRender()->TextColor(0.45f, 0.92f, 1.0f, 1.0f);
							Ui()->DoLabel(&TitleBox, BulletTitle.c_str(), 12.0f, TEXTALIGN_TL);

							TextRender()->TextColor(0.88f, 0.90f, 0.95f, 0.95f);
							TextRender()->Text(DetailBox.x, DetailBox.y + 1.0f, 11.5f, Rest.c_str(), AvailableW);
							TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
						}
						else
						{
							std::string Combined = BulletTitle + Rest;
							STextBoundingBox Box = TextRender()->TextBoundingBox(11.5f, Combined.c_str(), -1, BulletArea.w);
							float LineH = std::max(18.0f, Box.m_H + 3.0f);
							CUIRect LineRect;
							BottomWhatsNew.HSplitTop(LineH, &LineRect, &BottomWhatsNew);
							if(IndentOffset > 0.0f)
								LineRect.VSplitLeft(IndentOffset, nullptr, &LineRect);

							TextRender()->TextColor(0.88f, 0.90f, 0.95f, 0.95f);
							TextRender()->Text(LineRect.x, LineRect.y + 1.0f, 11.5f, Combined.c_str(), BulletArea.w);
							TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
						}
					}
					else
					{
						std::string BulletText = "* " + Bullet;
						size_t Ast = 0;
						while((Ast = BulletText.find("**", Ast)) != std::string::npos)
							BulletText.erase(Ast, 2);

						STextBoundingBox Box = TextRender()->TextBoundingBox(11.5f, BulletText.c_str(), -1, BulletArea.w);
						float LineH = std::max(16.0f, Box.m_H + 3.0f);
						CUIRect LineRect;
						BottomWhatsNew.HSplitTop(LineH, &LineRect, &BottomWhatsNew);
						if(IndentOffset > 0.0f)
							LineRect.VSplitLeft(IndentOffset, nullptr, &LineRect);

						TextRender()->TextColor(0.88f, 0.90f, 0.95f, 0.95f);
						TextRender()->Text(LineRect.x, LineRect.y + 1.0f, 11.5f, BulletText.c_str(), BulletArea.w);
						TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
					}
					BottomWhatsNew.HSplitTop(2.0f, nullptr, &BottomWhatsNew);
				}
				else
				{
					std::string Clean = TrimmedLine;
					size_t Ast = 0;
					while((Ast = Clean.find("**", Ast)) != std::string::npos)
						Clean.erase(Ast, 2);

					STextBoundingBox Box = TextRender()->TextBoundingBox(12.0f, Clean.c_str(), -1, BottomWhatsNew.w);
					float LineH = std::max(16.0f, Box.m_H + 3.0f);
					CUIRect LineRect;
					BottomWhatsNew.HSplitTop(LineH, &LineRect, &BottomWhatsNew);
					TextRender()->TextColor(0.85f, 0.88f, 0.95f, 0.9f);
					TextRender()->Text(LineRect.x, LineRect.y + 1.0f, 12.0f, Clean.c_str(), BottomWhatsNew.w);
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
					BottomWhatsNew.HSplitTop(2.0f, nullptr, &BottomWhatsNew);
				}
			}
		}
	}

	BottomWhatsNew.HSplitTop(MarginSmall, nullptr, &BottomWhatsNew);
	s_SectionBoxesAc.back().h = BottomWhatsNew.y - s_SectionBoxesAc.back().y;

	LeftView.y = BottomWhatsNew.y;
	RightView.y = BottomWhatsNew.y;
	}

	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = std::max(LeftView.y, RightView.y) + MarginSmall * 2.0f;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegionAc.AddRect(ScrollRegion);
	s_ScrollRegionAc.End();
}


void CMenus::RenderSettingsAClientMod(CUIRect MainView)
{
	CUIRect Column, LeftView, RightView, Button, Label;

	static CScrollRegion s_ScrollRegionMod;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_ForceShowScrollbar = true;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegionMod.Begin(&MainView, &ScrollParams);

	static std::vector<CUIRect> s_SectionBoxesMod;
	const float ScrollOffset = MainView.y;
	static float s_PrevScrollOffsetMod = 0.0f;

	MainView.VSplitRight(5.0f, &MainView, nullptr);
	MainView.VSplitLeft(5.0f, nullptr, &MainView);

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	for(CUIRect &Section : s_SectionBoxesMod)
	{
		float Padding = MarginBetweenViews * 0.6666f;
		Section.w += Padding;
		Section.h += Padding;
		Section.x -= Padding * 0.5f;
		Section.y -= Padding * 0.5f;
		Section.y -= s_PrevScrollOffsetMod - ScrollOffset;
		if(Section.x < MainView.x + 1.0f)
		{
			float Diff = (MainView.x + 1.0f) - Section.x;
			Section.x += Diff;
			Section.w -= Diff;
		}
		if(Section.x + Section.w > MainView.x + MainView.w - 1.0f)
		{
			Section.w = (MainView.x + MainView.w - 1.0f) - Section.x;
		}
		if(g_Config.m_AcModernUi)
		{
			Section.Draw(GetColorBorder(0.40f), IGraphics::CORNER_ALL, 10.0f);
			CUIRect Inner = Section;
			Inner.Margin(1.0f, &Inner);
			Inner.Draw(GetColorBg(), IGraphics::CORNER_ALL, 9.0f);
		}
		else
		{
			Section.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
		}
	}
	s_PrevScrollOffsetMod = ScrollOffset;
	s_SectionBoxesMod.clear();

	// ***** Left Column: General & Mute Presets ***** //
	Column = LeftView;

	// Section 1: General Moderation
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxesMod.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Moderation"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoCheckboxWithHelp(&g_Config.m_ClModRecordOnPunish, TCLocalize("Auto-record 3-min demo on punish (Kick/Ban)"), TCLocalize("Automatically saves the last 3 minutes of gameplay via rolling buffer to demos/replays/ when a kick or ban is confirmed."), &g_Config.m_ClModRecordOnPunish, &Column, LineSize);
	DoCheckboxWithHelp(&g_Config.m_ClModAllowTargetDummy, TCLocalize("Allow moderating local dummy (testing)"), TCLocalize("Enables kick, ban, and mute buttons against your own dummy on Scoreboard so you can test moderation actions locally."), &g_Config.m_ClModAllowTargetDummy, &Column, LineSize);

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(LineSize + 4.0f, &Button, &Column);
	static CButtonContainer s_BtnOpenDiscordModChannel;
	ColorRGBA DiscordColor = ColorRGBA(0.35f, 0.40f, 0.95f, 0.75f * Ui()->ButtonColorMul(&s_BtnOpenDiscordModChannel));
	if(DoButton_Menu(&s_BtnOpenDiscordModChannel, TCLocalize("Abrir Canal en Discord (App)"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, DiscordColor))
	{
		if(!Client()->ViewLink("discord://-/channels/342003344476471296/channel-browser"))
			Client()->ViewLink("https://discord.com/channels/342003344476471296/channel-browser");
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	s_SectionBoxesMod.back().h = Column.y - s_SectionBoxesMod.back().y;

	// Section 2: Mute Presets (muteid)
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxesMod.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Mute Presets (muteid)"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	Column.HSplitTop(16.0f, &Label, &Column);
	TextRender()->TextColor(1.0f, 0.85f, 0.4f, 1.0f);
	Ui()->DoLabel(&Label, TCLocalize("Duration buttons (seconds):"), 12.0f, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static CLineInputNumber s_MuteSecInputs[6];
	int *apMuteSecs[6] = {
		&g_Config.m_ClModMuteSec1,
		&g_Config.m_ClModMuteSec2,
		&g_Config.m_ClModMuteSec3,
		&g_Config.m_ClModMuteSec4,
		&g_Config.m_ClModMuteSec5,
		&g_Config.m_ClModMuteSec6
	};
	const char *apMuteSecLabels[6] = {
		"Botón 1 (s):", "Botón 2 (s):", "Botón 3 (s):",
		"Botón 4 (s):", "Botón 5 (s):", "Botón 6 (s):"
	};

	for(int i = 0; i < 6; ++i)
	{
		Column.HSplitTop(LineSize, &Button, &Column);
		DoEditBoxNumberWithLabel(&s_MuteSecInputs[i], &Button, apMuteSecLabels[i], apMuteSecs[i], 1, 31536000);
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(16.0f, &Label, &Column);
	TextRender()->TextColor(1.0f, 0.85f, 0.4f, 1.0f);
	Ui()->DoLabel(&Label, TCLocalize("Reason buttons:"), 12.0f, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static CLineInput s_MuteReasonInputs[3];
	char *apMuteReasons[3] = {
		g_Config.m_ClModMuteReason1,
		g_Config.m_ClModMuteReason2,
		g_Config.m_ClModMuteReason3
	};
	const char *apMuteReasonLabels[3] = {
		"Razón 1:", "Razón 2:", "Razón 3:"
	};
	for(int i = 0; i < 3; ++i)
	{
		Column.HSplitTop(LineSize, &Button, &Column);
		DoEditBoxWithLabel(&s_MuteReasonInputs[i], &Button, apMuteReasonLabels[i], "", apMuteReasons[i], 64);
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	s_SectionBoxesMod.back().h = Column.y - s_SectionBoxesMod.back().y;

	LeftView = Column;

	// ***** Right Column: Kick & Ban Presets ***** //
	Column = RightView;

	// Section 3: Kick Presets (kick)
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxesMod.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Kick Presets (kick)"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static CLineInput s_KickReasonInputs[3];
	char *apKickReasons[3] = {
		g_Config.m_ClModKickReason1,
		g_Config.m_ClModKickReason2,
		g_Config.m_ClModKickReason3
	};
	const char *apKickReasonLabels[3] = {
		"Razón 1:", "Razón 2:", "Razón 3:"
	};
	for(int i = 0; i < 3; ++i)
	{
		Column.HSplitTop(LineSize, &Button, &Column);
		DoEditBoxWithLabel(&s_KickReasonInputs[i], &Button, apKickReasonLabels[i], "", apKickReasons[i], 64);
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	s_SectionBoxesMod.back().h = Column.y - s_SectionBoxesMod.back().y;

	// Section 4: Ban Presets (ban)
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxesMod.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Ban Presets (ban)"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	Column.HSplitTop(16.0f, &Label, &Column);
	TextRender()->TextColor(1.0f, 0.85f, 0.4f, 1.0f);
	Ui()->DoLabel(&Label, TCLocalize("Duration buttons (minutes):"), 12.0f, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static CLineInputNumber s_BanMinInputs[4];
	int *apBanMins[4] = {
		&g_Config.m_ClModBanMin1,
		&g_Config.m_ClModBanMin2,
		&g_Config.m_ClModBanMin3,
		&g_Config.m_ClModBanMin4
	};
	const char *apBanMinLabels[4] = {
		"Botón 1 (min):", "Botón 2 (min):", "Botón 3 (min):", "Botón 4 (min):"
	};
	for(int i = 0; i < 4; ++i)
	{
		Column.HSplitTop(LineSize, &Button, &Column);
		DoEditBoxNumberWithLabel(&s_BanMinInputs[i], &Button, apBanMinLabels[i], apBanMins[i], 1, 525600);
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(16.0f, &Label, &Column);
	TextRender()->TextColor(1.0f, 0.85f, 0.4f, 1.0f);
	Ui()->DoLabel(&Label, TCLocalize("Reason buttons:"), 12.0f, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	static CLineInput s_BanReasonInputs[3];
	char *apBanReasons[3] = {
		g_Config.m_ClModBanReason1,
		g_Config.m_ClModBanReason2,
		g_Config.m_ClModBanReason3
	};
	const char *apBanReasonLabels[3] = {
		"Razón 1:", "Razón 2:", "Razón 3:"
	};
	for(int i = 0; i < 3; ++i)
	{
		Column.HSplitTop(LineSize, &Button, &Column);
		DoEditBoxWithLabel(&s_BanReasonInputs[i], &Button, apBanReasonLabels[i], "", apBanReasons[i], 64);
		Column.HSplitTop(MarginExtraSmall, nullptr, &Column);
	}

	// Section 5: Reset Defaults Button
	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(LineSize + 4.0f, &Button, &Column);
	static CButtonContainer s_BtnResetModDefaults;
	ColorRGBA ResetColor = ColorRGBA(0.9f, 0.35f, 0.35f, 0.7f * Ui()->ButtonColorMul(&s_BtnResetModDefaults));
	if(DoButton_Menu(&s_BtnResetModDefaults, TCLocalize("Reset to Default Presets"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ResetColor))
	{
		g_Config.m_ClModMuteSec1 = 60;
		g_Config.m_ClModMuteSec2 = 300;
		g_Config.m_ClModMuteSec3 = 900;
		g_Config.m_ClModMuteSec4 = 3600;
		g_Config.m_ClModMuteSec5 = 14400;
		g_Config.m_ClModMuteSec6 = 86400;
		str_copy(g_Config.m_ClModMuteReason1, "Spam");
		str_copy(g_Config.m_ClModMuteReason2, "Insultos");
		str_copy(g_Config.m_ClModMuteReason3, "Bloqueo");

		str_copy(g_Config.m_ClModKickReason1, "AFK");
		str_copy(g_Config.m_ClModKickReason2, "Troll");
		str_copy(g_Config.m_ClModKickReason3, "Reglas");

		g_Config.m_ClModBanMin1 = 5;
		g_Config.m_ClModBanMin2 = 60;
		g_Config.m_ClModBanMin3 = 1440;
		g_Config.m_ClModBanMin4 = 10080;
		str_copy(g_Config.m_ClModBanReason1, "Cheats");
		str_copy(g_Config.m_ClModBanReason2, "Insultos");
		str_copy(g_Config.m_ClModBanReason3, "Griefing");

		for(int i = 0; i < 6; ++i) s_MuteSecInputs[i].SetInteger(*apMuteSecs[i]);
		for(int i = 0; i < 4; ++i) s_BanMinInputs[i].SetInteger(*apBanMins[i]);
		for(int i = 0; i < 3; ++i) s_MuteReasonInputs[i].Set(apMuteReasons[i]);
		for(int i = 0; i < 3; ++i) s_KickReasonInputs[i].Set(apKickReasons[i]);
		for(int i = 0; i < 3; ++i) s_BanReasonInputs[i].Set(apBanReasons[i]);
	}

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	s_SectionBoxesMod.back().h = Column.y - s_SectionBoxesMod.back().y;

	RightView = Column;

	// Section 6: Voice Chat Moderation (Global Relay)
	CUIRect VoiceModCol = MainView;
	VoiceModCol.y = std::max(LeftView.y, RightView.y) + MarginBetweenSections;
	VoiceModCol.x = MainView.x + MarginSmall;
	VoiceModCol.w = MainView.w - MarginSmall * 2.0f;

	s_SectionBoxesMod.push_back(VoiceModCol);

	VoiceModCol.HSplitTop(HeadlineHeight, &Label, &VoiceModCol);
	Ui()->DoLabel(&Label, TCLocalize("Voice Chat Moderation (Global Relay)"), HeadlineFontSize, TEXTALIGN_ML);
	VoiceModCol.HSplitTop(MarginSmall, nullptr, &VoiceModCol);

	{
		CAClientVoice &Voice = GameClient()->m_AClientVoice;
		static CLineInputBuffered<128> s_VoiceModKeyInput;
		static CLineInputBuffered<MAX_NAME_LENGTH> s_VoiceModTargetInput;
		static CLineInputNumber s_VoiceModMinInput;
		static CButtonContainer s_BtnVerifyVoiceModKey;
		static CButtonContainer s_BtnClearVoiceModKey;
		static CButtonContainer s_aBtnModPreset[5];
		static CButtonContainer s_BtnModManualMute;
		static CButtonContainer s_BtnModManualUnmute;
		static CButtonContainer s_aBtnModPlayerMute[MAX_CLIENTS];
		static CButtonContainer s_aBtnModPlayerUnmute[MAX_CLIENTS];

		if(!Voice.IsModKeyVerified())
		{
			VoiceModCol.HSplitTop(LineSize, &Button, &VoiceModCol);
			CUIRect KeyLabel, KeyBox, VerifyBtn;
			Button.VSplitLeft(110.0f, &KeyLabel, &KeyBox);
			KeyBox.VSplitRight(150.0f, &KeyBox, &VerifyBtn);
			KeyBox.VSplitRight(MarginExtraSmall, &KeyBox, nullptr);

			Ui()->DoLabel(&KeyLabel, TCLocalize("Key VoiceChat:"), FontSize, TEXTALIGN_ML);
			s_VoiceModKeyInput.SetHidden(true);
			s_VoiceModKeyInput.SetEmptyText(TCLocalize("Ingresa tu clave de moderador..."));
			Ui()->DoEditBox(&s_VoiceModKeyInput, &KeyBox, EditBoxFontSize);

			const bool KeyEnter = s_VoiceModKeyInput.IsActive() && (Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || Input()->KeyPress(KEY_RETURN) || Input()->KeyPress(KEY_KP_ENTER));
			if(DoButton_Menu(&s_BtnVerifyVoiceModKey, TCLocalize("Verificar y Guardar"), 0, &VerifyBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ColorRGBA(0.25f, 0.65f, 0.40f, 0.85f)) || KeyEnter)
			{
				if(Voice.VerifyAndSaveModKey(s_VoiceModKeyInput.GetString()))
				{
					s_VoiceModKeyInput.Clear();
				}
			}

			VoiceModCol.HSplitTop(MarginExtraSmall, nullptr, &VoiceModCol);
			VoiceModCol.HSplitTop(LineSize * 0.9f, &Label, &VoiceModCol);
			if(Voice.GetModStatusMsg()[0] != '\0')
			{
				TextRender()->TextColor(1.0f, 0.35f, 0.35f, 1.0f);
				Ui()->DoLabel(&Label, Voice.GetModStatusMsg(), FontSize * 0.85f, TEXTALIGN_ML);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
			else
			{
				TextRender()->TextColor(0.70f, 0.72f, 0.78f, 0.90f);
				Ui()->DoLabel(&Label, TCLocalize("Ingresa la clave correcta una sola vez; se guardara en tu configuracion."), FontSize * 0.85f, TEXTALIGN_ML);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
		}
		else
		{
			VoiceModCol.HSplitTop(LineSize, &Button, &VoiceModCol);
			CUIRect StatusLabel, LogoutBtn;
			Button.VSplitRight(125.0f, &StatusLabel, &LogoutBtn);
			StatusLabel.VSplitRight(MarginExtraSmall, &StatusLabel, nullptr);

			TextRender()->TextColor(0.30f, 0.95f, 0.45f, 1.0f);
			Ui()->DoLabel(&StatusLabel, TCLocalize("Clave verificada (Guardada en config)"), FontSize, TEXTALIGN_ML);
			TextRender()->TextColor(TextRender()->DefaultTextColor());

			if(DoButton_Menu(&s_BtnClearVoiceModKey, TCLocalize("Cerrar Sesion"), 0, &LogoutBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ColorRGBA(0.78f, 0.28f, 0.28f, 0.80f)))
			{
				Voice.ClearModKey();
			}

			// Duration presets + custom minutes input (0 = Permanente)
			VoiceModCol.HSplitTop(MarginExtraSmall, nullptr, &VoiceModCol);
			VoiceModCol.HSplitTop(LineSize, &Button, &VoiceModCol);
			CUIRect DurLabel, PresetsArea, MinBox;
			Button.VSplitLeft(128.0f, &DurLabel, &PresetsArea);
			PresetsArea.VSplitRight(125.0f, &PresetsArea, &MinBox);
			PresetsArea.VSplitRight(MarginSmall, &PresetsArea, nullptr);

			Ui()->DoLabel(&DurLabel, TCLocalize("Duracion (0=Perm):"), FontSize, TEXTALIGN_ML);

			static const int s_aPresetMins[5] = {5, 15, 60, 1440, 0};
			const char *apPresetLabels[5] = {TCLocalize("5m"), TCLocalize("15m"), TCLocalize("1h"), TCLocalize("24h"), TCLocalize("Permanente")};
			const float PresetGap = MarginExtraSmall;
			const float PresetTotalW = PresetsArea.w - PresetGap * 4.0f;
			const float PresetW = std::max(30.0f, (PresetTotalW - 36.0f) / 5.0f);
			CUIRect CurPreset = PresetsArea;
			for(int p = 0; p < 5; ++p)
			{
				CUIRect BtnRect;
				const float ThisW = (p == 4) ? std::max(45.0f, CurPreset.w) : PresetW;
				CurPreset.VSplitLeft(ThisW, &BtnRect, &CurPreset);
				if(p < 4)
					CurPreset.VSplitLeft(PresetGap, nullptr, &CurPreset);

				const bool Active = (g_Config.m_AcVoiceModMuteMinutes == s_aPresetMins[p]);
				const ColorRGBA BtnColor = Active ? ColorRGBA(0.25f, 0.72f, 0.42f, 0.88f) : ColorRGBA(0.25f, 0.28f, 0.35f, 0.65f);
				if(DoButton_Menu(&s_aBtnModPreset[p], apPresetLabels[p], Active ? 1 : 0, &BtnRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, BtnColor))
				{
					g_Config.m_AcVoiceModMuteMinutes = s_aPresetMins[p];
					char aMinBuf[16];
					str_format(aMinBuf, sizeof(aMinBuf), "%d", s_aPresetMins[p]);
					s_VoiceModMinInput.Set(aMinBuf);
				}
			}

			DoEditBoxNumberWithLabel(&s_VoiceModMinInput, &MinBox, TCLocalize("Min:"), &g_Config.m_AcVoiceModMuteMinutes, 0, 525600);

			const int MuteMinutes = std::clamp(g_Config.m_AcVoiceModMuteMinutes, 0, 525600);
			const int DurationSeconds = (MuteMinutes == 0) ? 0 : (MuteMinutes * 60);

			// Manual target input
			VoiceModCol.HSplitTop(MarginExtraSmall, nullptr, &VoiceModCol);
			VoiceModCol.HSplitTop(LineSize, &Button, &VoiceModCol);
			CUIRect TargetLabel, TargetBox, ManualMuteBtn, ManualUnmuteBtn;
			Button.VSplitLeft(68.0f, &TargetLabel, &TargetBox);
			TargetBox.VSplitRight(75.0f, &TargetBox, &ManualUnmuteBtn);
			TargetBox.VSplitRight(MarginExtraSmall, &TargetBox, nullptr);
			TargetBox.VSplitRight(98.0f, &TargetBox, &ManualMuteBtn);
			TargetBox.VSplitRight(MarginExtraSmall, &TargetBox, nullptr);

			Ui()->DoLabel(&TargetLabel, TCLocalize("Jugador:"), FontSize, TEXTALIGN_ML);
			s_VoiceModTargetInput.SetEmptyText(TCLocalize("Nombre exacto del jugador..."));
			Ui()->DoEditBox(&s_VoiceModTargetInput, &TargetBox, EditBoxFontSize);

			const bool TargetEnter = s_VoiceModTargetInput.IsActive() && (Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || Input()->KeyPress(KEY_RETURN) || Input()->KeyPress(KEY_KP_ENTER));
			if(DoButton_Menu(&s_BtnModManualMute, TCLocalize("Mute Global"), 0, &ManualMuteBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ColorRGBA(0.85f, 0.30f, 0.25f, 0.85f)) || TargetEnter)
			{
				if(s_VoiceModTargetInput.GetString()[0] != '\0')
				{
					Voice.SendGlobalModMute(-1, s_VoiceModTargetInput.GetString(), DurationSeconds);
				}
			}
			if(DoButton_Menu(&s_BtnModManualUnmute, TCLocalize("Unmute"), 0, &ManualUnmuteBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ColorRGBA(0.28f, 0.62f, 0.85f, 0.80f)))
			{
				if(s_VoiceModTargetInput.GetString()[0] != '\0')
				{
					Voice.SendGlobalModMute(-1, s_VoiceModTargetInput.GetString(), -1);
				}
			}

			// Connected players in current server
			if(Client()->State() == IClient::STATE_ONLINE)
			{
				const int ActiveDummy = std::clamp(g_Config.m_ClDummy, 0, NUM_DUMMIES - 1);
				const int LocalId = GameClient()->m_aLocalIds[ActiveDummy];
				int ListedPlayers = 0;
				for(int i = 0; i < MAX_CLIENTS; ++i)
				{
					if(i == LocalId || !GameClient()->m_aClients[i].m_Active)
						continue;

					if(ListedPlayers == 0)
					{
						VoiceModCol.HSplitTop(MarginSmall, nullptr, &VoiceModCol);
						VoiceModCol.HSplitTop(LineSize, &Label, &VoiceModCol);
						Ui()->DoLabel(&Label, TCLocalize("Jugadores conectados en este servidor:"), FontSize * 0.9f, TEXTALIGN_ML);
					}

					VoiceModCol.HSplitTop(MarginExtraSmall, nullptr, &VoiceModCol);
					VoiceModCol.HSplitTop(LineSize, &Button, &VoiceModCol);
					CUIRect NameLabel, PlayerMuteBtn, PlayerUnmuteBtn;
					Button.VSplitRight(75.0f, &NameLabel, &PlayerUnmuteBtn);
					NameLabel.VSplitRight(MarginExtraSmall, &NameLabel, nullptr);
					NameLabel.VSplitRight(98.0f, &NameLabel, &PlayerMuteBtn);
					NameLabel.VSplitRight(MarginExtraSmall, &NameLabel, nullptr);

					char aPlayerLabel[MAX_NAME_LENGTH + 16];
					str_format(aPlayerLabel, sizeof(aPlayerLabel), "[%d] %s", i, GameClient()->m_aClients[i].m_aName);
					Ui()->DoLabel(&NameLabel, aPlayerLabel, FontSize * 0.9f, TEXTALIGN_ML);

					if(DoButton_Menu(&s_aBtnModPlayerMute[i], TCLocalize("Mute Global"), 0, &PlayerMuteBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ColorRGBA(0.85f, 0.30f, 0.25f, 0.85f)))
					{
						Voice.SendGlobalModMute(i, GameClient()->m_aClients[i].m_aName, DurationSeconds);
					}
					if(DoButton_Menu(&s_aBtnModPlayerUnmute[i], TCLocalize("Unmute"), 0, &PlayerUnmuteBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ColorRGBA(0.28f, 0.62f, 0.85f, 0.80f)))
					{
						Voice.SendGlobalModMute(i, GameClient()->m_aClients[i].m_aName, -1);
					}
					++ListedPlayers;
				}

				if(ListedPlayers == 0)
				{
					VoiceModCol.HSplitTop(MarginExtraSmall, nullptr, &VoiceModCol);
					VoiceModCol.HSplitTop(LineSize, &Label, &VoiceModCol);
					TextRender()->TextColor(0.65f, 0.68f, 0.75f, 0.85f);
					Ui()->DoLabel(&Label, TCLocalize("No hay otros jugadores conectados en este servidor."), FontSize * 0.85f, TEXTALIGN_ML);
					TextRender()->TextColor(TextRender()->DefaultTextColor());
				}
			}

			if(Voice.GetModStatusMsg()[0] != '\0')
			{
				VoiceModCol.HSplitTop(MarginExtraSmall, nullptr, &VoiceModCol);
				VoiceModCol.HSplitTop(LineSize * 0.9f, &Label, &VoiceModCol);
				TextRender()->TextColor(0.45f, 0.90f, 1.0f, 1.0f);
				Ui()->DoLabel(&Label, Voice.GetModStatusMsg(), FontSize * 0.85f, TEXTALIGN_ML);
				TextRender()->TextColor(TextRender()->DefaultTextColor());
			}
		}
	}

	VoiceModCol.HSplitTop(MarginSmall, nullptr, &VoiceModCol);
	s_SectionBoxesMod.back().h = VoiceModCol.y - s_SectionBoxesMod.back().y;

	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = MainView.y;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = std::max(VoiceModCol.y, std::max(LeftView.y, RightView.y)) - MainView.y;
	s_ScrollRegionMod.AddRect(ScrollRegion);
	s_ScrollRegionMod.End();
}

static ColorRGBA GetCategoryColor(const char *pCategory)
{
	if(!pCategory || !pCategory[0])
		return ColorRGBA(0.7f, 0.7f, 0.7f, 1.0f);
	if(str_comp_nocase(pCategory, "Easy") == 0 || str_comp_nocase(pCategory, "Novice") == 0)
		return ColorRGBA(0.3f, 0.85f, 0.4f, 1.0f);
	if(str_comp_nocase(pCategory, "Main") == 0 || str_comp_nocase(pCategory, "Moderate") == 0)
		return ColorRGBA(0.3f, 0.75f, 0.95f, 1.0f);
	if(str_comp_nocase(pCategory, "Hard") == 0 || str_comp_nocase(pCategory, "Brutal") == 0)
		return ColorRGBA(0.95f, 0.55f, 0.15f, 1.0f);
	if(str_comp_nocase(pCategory, "Insane") == 0)
		return ColorRGBA(0.95f, 0.25f, 0.25f, 1.0f);
	if(str_comp_nocase(pCategory, "Extreme") == 0)
		return ColorRGBA(0.9f, 0.3f, 0.9f, 1.0f);
	if(str_find_nocase(pCategory, "Mod") != nullptr)
		return ColorRGBA(0.4f, 0.8f, 0.7f, 1.0f);
	if(str_comp_nocase(pCategory, "Solo") == 0 || str_comp_nocase(pCategory, "Dummy") == 0)
		return ColorRGBA(0.7f, 0.5f, 0.9f, 1.0f);
	if(str_find_nocase(pCategory, "DDMaX") != nullptr)
		return ColorRGBA(0.95f, 0.8f, 0.25f, 1.0f);
	return ColorRGBA(0.6f, 0.7f, 0.8f, 1.0f);
}

static bool MatchesCategoryFilter(const char *pMapCategory, const char *pFilter)
{
	if(!pFilter || pFilter[0] == '\0' || str_comp_nocase(pFilter, "Todos") == 0)
		return true;

	if(!pMapCategory || pMapCategory[0] == '\0')
		return str_comp_nocase(pFilter, "Otros") == 0;

	if(str_comp_nocase(pFilter, "DDMaX") == 0)
		return (str_find_nocase(pMapCategory, "DDMaX") != nullptr);

	if(str_comp_nocase(pFilter, "Mod") == 0)
		return (str_find_nocase(pMapCategory, "Mod") != nullptr);

	if(str_comp_nocase(pFilter, "Otros") == 0)
		return (str_comp_nocase(pMapCategory, "KoG") == 0 || str_comp_nocase(pMapCategory, "Unknown") == 0 || str_comp_nocase(pMapCategory, "DDNet") == 0);

	return (str_comp_nocase(pMapCategory, pFilter) == 0);
}

void CMenus::RenderSettingsAClientProfiles(CUIRect MainView)
{
	CUIRect TopBar, SearchBox, Button, Content;
	const float Margin = 5.0f;
	const float LineHeight = 22.0f;
	const float FontSize = 12.0f;

	CPlayerProfiles &Profiles = GameClient()->m_PlayerProfiles;

	// Search and control bar
	MainView.HSplitTop(LineHeight, &TopBar, &Content);
	Content.HSplitTop(Margin, nullptr, &Content);

	const bool IsKoG = (g_Config.m_AcProfileSource == 1);
	const bool IsOnline = (Client()->State() == IClient::STATE_ONLINE);
	const char *pReqName = IsKoG ? Profiles.KoGRequestedName() : Profiles.RequestedName();

	char aCurCategory[64] = "";
	if(IsOnline)
	{
		const char *pCurMap = Client()->ServerInfo().m_aMap;
		if(pCurMap && pCurMap[0])
		{
			CMapMetadata CurMeta{};
			bool GotMeta = IsKoG ? Profiles.GetKoGMapMeta(pCurMap, CurMeta) : Profiles.GetDDNetMapMeta(pCurMap, CurMeta);
			if(GotMeta && CurMeta.m_aCategory[0])
			{
				str_copy(aCurCategory, CurMeta.m_aCategory);
			}
		}
		if(aCurCategory[0] == '\0')
		{
			const char *pGameType = Client()->ServerInfo().m_aGameType;
			const char *pServerName = Client()->ServerInfo().m_aName;
			static const char *s_aCats[] = {"Easy", "Main", "Hard", "Insane", "Extreme", "Mod", "Novice", "Moderate", "Brutal", "Solo", "Dummy"};
			for(const char *pC : s_aCats)
			{
				if((pGameType && str_find_nocase(pGameType, pC)) || (pServerName && str_find_nocase(pServerName, pC)))
				{
					str_copy(aCurCategory, pC);
					break;
				}
			}
		}
	}

	auto VoteForCategory = [this](const char *pCategory) {
		if(!pCategory || pCategory[0] == '\0')
			return;

		int FoundOptionIndex = -1;
		int CurrentIndex = 0;
		for(const CVoteOptionClient *pOption = GameClient()->m_Voting.FirstOption(); pOption; pOption = pOption->m_pNext, CurrentIndex++)
		{
			if(str_comp_nocase(pOption->m_aDescription, pCategory) == 0)
			{
				FoundOptionIndex = CurrentIndex;
				break;
			}
		}

		if(FoundOptionIndex == -1)
		{
			CurrentIndex = 0;
			for(const CVoteOptionClient *pOption = GameClient()->m_Voting.FirstOption(); pOption; pOption = pOption->m_pNext, CurrentIndex++)
			{
				if(str_find_nocase(pOption->m_aDescription, pCategory) != nullptr)
				{
					FoundOptionIndex = CurrentIndex;
					break;
				}
			}
		}

		if(FoundOptionIndex != -1)
		{
			GameClient()->m_Voting.CallvoteOption(FoundOptionIndex, "");
		}
		else
		{
			GameClient()->m_Voting.Callvote("option", pCategory, "");
		}
	};

	auto VoteForMap = [this](const char *pMapName) {
		if(!pMapName || pMapName[0] == '\0')
			return;

		int FoundOptionIndex = -1;
		int CurrentIndex = 0;
		const int MapLen = str_length(pMapName);

		// 1. Coincidencia exacta con la opcion de votacion del servidor (ej. "60s")
		for(const CVoteOptionClient *pOption = GameClient()->m_Voting.FirstOption(); pOption; pOption = pOption->m_pNext, CurrentIndex++)
		{
			if(str_comp_nocase(pOption->m_aDescription, pMapName) == 0)
			{
				FoundOptionIndex = CurrentIndex;
				break;
			}
		}

		// 2. Coincidencia cuando la opcion empieza con el nombre del mapa seguido de separador
		if(FoundOptionIndex == -1)
		{
			CurrentIndex = 0;
			for(const CVoteOptionClient *pOption = GameClient()->m_Voting.FirstOption(); pOption; pOption = pOption->m_pNext, CurrentIndex++)
			{
				if(str_startswith_nocase(pOption->m_aDescription, pMapName))
				{
					char NextChar = pOption->m_aDescription[MapLen];
					if(NextChar == '\0' || NextChar == ' ' || NextChar == '|' || NextChar == '(' || NextChar == '[' || NextChar == '-' || NextChar == ':')
					{
						FoundOptionIndex = CurrentIndex;
						break;
					}
				}
			}
		}

		// 3. Coincidencia con delimitadores de palabra en cualquier parte de la opcion (ej. "Map: 60s")
		if(FoundOptionIndex == -1)
		{
			CurrentIndex = 0;
			for(const CVoteOptionClient *pOption = GameClient()->m_Voting.FirstOption(); pOption; pOption = pOption->m_pNext, CurrentIndex++)
			{
				const char *pFound = str_find_nocase(pOption->m_aDescription, pMapName);
				if(pFound != nullptr)
				{
					bool ValidStart = (pFound == pOption->m_aDescription) ||
						(*(pFound - 1) == ' ' || *(pFound - 1) == ':' || *(pFound - 1) == '|' || *(pFound - 1) == '[' || *(pFound - 1) == '(' || *(pFound - 1) == '-');
					char EndChar = pFound[MapLen];
					bool ValidEnd = (EndChar == '\0' || EndChar == ' ' || EndChar == '|' || EndChar == '(' || EndChar == '[' || EndChar == ']' || EndChar == ')' || EndChar == '-');
					if(ValidStart && ValidEnd)
					{
						FoundOptionIndex = CurrentIndex;
						break;
					}
				}
			}
		}

		if(FoundOptionIndex != -1)
		{
			GameClient()->m_Voting.CallvoteOption(FoundOptionIndex, "");
		}
		else
		{
			GameClient()->m_Voting.Callvote("option", pMapName, "");
		}
	};

	auto DoColorBtn = [this](CButtonContainer *pId, const char *pText, const CUIRect *pRect, ColorRGBA BaseColor, int Corners = IGraphics::CORNER_ALL, float Rounding = 3.5f, float LabelFontSize = 11.0f, bool Active = false) -> int {
		const bool Hot = Ui()->HotItem() == pId;
		ColorRGBA DrawCol = BaseColor;
		if(Hot)
		{
			DrawCol.r = std::min(1.0f, DrawCol.r + 0.10f);
			DrawCol.g = std::min(1.0f, DrawCol.g + 0.10f);
			DrawCol.b = std::min(1.0f, DrawCol.b + 0.10f);
			DrawCol.a = std::min(1.0f, DrawCol.a + 0.12f);
		}
		pRect->Draw(DrawCol, Corners, Rounding);
		if(Active)
		{
			CUIRect Underline;
			pRect->HSplitBottom(2.0f, nullptr, &Underline);
			Underline.Draw(ColorRGBA(1.0f, 1.0f, 1.0f, 0.85f), IGraphics::CORNER_NONE, 0.0f);
		}
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, (Active || Hot) ? 1.0f : 0.90f);
		Ui()->DoLabel(pRect, pText, LabelFontSize, TEXTALIGN_MC);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		return Ui()->DoButtonLogic(pId, 0, pRect, BUTTONFLAG_LEFT);
	};

	// Search input box with internal buffer
	static CLineInputBuffered<128> s_ProfileSearchInput;
	static char s_aLastSyncedReq[MAX_NAME_LENGTH] = "";
	static bool s_InitialFetchDone = false;

	if(!s_InitialFetchDone)
	{
		s_InitialFetchDone = true;
		const char *pInitial = pReqName[0] != '\0' ? pReqName : Client()->PlayerName();
		if(pInitial && pInitial[0] != '\0')
		{
			s_ProfileSearchInput.Set(pInitial);
			str_copy(s_aLastSyncedReq, pInitial);
			if(IsKoG)
				Profiles.FetchKoGProfile(pInitial);
			else
				Profiles.FetchProfile(pInitial);
		}
	}
	else if(pReqName[0] != '\0' && str_comp(s_aLastSyncedReq, pReqName) != 0)
	{
		str_copy(s_aLastSyncedReq, pReqName);
		s_ProfileSearchInput.Set(pReqName);
	}

	// ==========================================
	// Fila 1: Modo [Individual | Equipo] + Plataforma [DDNet | KoG] + [Recargar] + Webs
	// ==========================================
	const bool IsTeamMode = (g_Config.m_AcProfileViewMode == 1);
	static CButtonContainer s_BtnModeIndiv;
	static CButtonContainer s_BtnModeTeam;
	static CButtonContainer s_BtnSrcDDNet;
	static CButtonContainer s_BtnSrcKoG;
	static CButtonContainer s_BtnRefresh;
	static CButtonContainer s_BtnKoGWeb;
	static CButtonContainer s_BtnDDNetWeb;

	const ColorRGBA InactiveTabCol(0.14f, 0.16f, 0.22f, 0.72f);

	TopBar.VSplitLeft(76.0f, &Button, &TopBar);
	if(DoColorBtn(&s_BtnModeIndiv, TCLocalize("Individual"), &Button, !IsTeamMode ? ColorRGBA(0.20f, 0.50f, 0.88f, 0.90f) : InactiveTabCol, IGraphics::CORNER_L, 4.0f, 11.0f, !IsTeamMode))
	{
		g_Config.m_AcProfileViewMode = 0;
	}

	TopBar.VSplitLeft(68.0f, &Button, &TopBar);
	if(DoColorBtn(&s_BtnModeTeam, TCLocalize("Equipo"), &Button, IsTeamMode ? ColorRGBA(0.48f, 0.26f, 0.86f, 0.90f) : InactiveTabCol, IGraphics::CORNER_R, 4.0f, 11.0f, IsTeamMode))
	{
		g_Config.m_AcProfileViewMode = 1;
		if(Profiles.TeamPlayers().empty())
			Profiles.AutoDetectCurrentTeam();
		else
			Profiles.ComputeTeamCommonUnfinished();
	}

	TopBar.VSplitLeft(12.0f, nullptr, &TopBar);

	TopBar.VSplitLeft(56.0f, &Button, &TopBar);
	if(DoColorBtn(&s_BtnSrcDDNet, "DDNet", &Button, !IsKoG ? ColorRGBA(0.16f, 0.54f, 0.86f, 0.90f) : InactiveTabCol, IGraphics::CORNER_L, 4.0f, 11.0f, !IsKoG))
	{
		if(g_Config.m_AcProfileSource != 0)
		{
			g_Config.m_AcProfileSource = 0;
			const char *pSearch = s_ProfileSearchInput.GetString();
			if(pSearch && pSearch[0] != '\0')
			{
				str_copy(s_aLastSyncedReq, pSearch);
				Profiles.FetchProfile(pSearch);
			}
		}
	}

	TopBar.VSplitLeft(48.0f, &Button, &TopBar);
	if(DoColorBtn(&s_BtnSrcKoG, "KoG", &Button, IsKoG ? ColorRGBA(0.86f, 0.44f, 0.12f, 0.90f) : InactiveTabCol, IGraphics::CORNER_R, 4.0f, 11.0f, IsKoG))
	{
		if(g_Config.m_AcProfileSource != 1)
		{
			g_Config.m_AcProfileSource = 1;
			const char *pSearch = s_ProfileSearchInput.GetString();
			if(pSearch && pSearch[0] != '\0')
			{
				str_copy(s_aLastSyncedReq, pSearch);
				Profiles.FetchKoGProfile(pSearch);
			}
		}
	}

	TopBar.VSplitLeft(12.0f, nullptr, &TopBar);

	// Unico boton [ Recargar ] contextual (recarga equipo o perfil individual)
	TopBar.VSplitLeft(74.0f, &Button, &TopBar);
	if(DoColorBtn(&s_BtnRefresh, TCLocalize("Recargar"), &Button, ColorRGBA(0.18f, 0.46f, 0.54f, 0.82f), IGraphics::CORNER_ALL, 4.0f, 11.0f))
	{
		if(IsTeamMode)
		{
			for(const auto &Name : Profiles.TeamPlayers())
			{
				if(IsKoG)
					Profiles.FetchKoGProfile(Name.c_str(), true);
				else
					Profiles.FetchProfile(Name.c_str(), true);
			}
			if(IsKoG)
				Profiles.FetchKoGMapsMetaNetwork();
		}
		else
		{
			const char *pSearch = s_ProfileSearchInput.GetString();
			const char *pTarget = (pSearch && pSearch[0] != '\0') ? pSearch : Client()->PlayerName();
			if(pTarget && pTarget[0] != '\0')
			{
				str_copy(s_aLastSyncedReq, pTarget);
				if(IsKoG)
				{
					Profiles.FetchKoGProfile(pTarget, true);
					Profiles.FetchKoGMapsMetaNetwork();
				}
				else
					Profiles.FetchProfile(pTarget, true);
			}
		}
	}

	// Web links on right side of TopBar
	TopBar.VSplitRight(68.0f, &TopBar, &Button);
	if(DoColorBtn(&s_BtnKoGWeb, "KoG.tw", &Button, ColorRGBA(0.62f, 0.32f, 0.12f, 0.72f), IGraphics::CORNER_ALL, 4.0f, 10.5f))
	{
		const char *pTarget = IsKoG ? (Profiles.KoGState() == EProfileState::READY ? Profiles.CurrentKoGProfile().m_aName : s_ProfileSearchInput.GetString())
		                            : (Profiles.State() == EProfileState::READY ? Profiles.CurrentProfile().m_aName : s_ProfileSearchInput.GetString());
		if(!pTarget || pTarget[0] == '\0')
			pTarget = Client()->PlayerName();
		Profiles.OpenKoGWeb(pTarget);
	}

	TopBar.VSplitRight(Margin, &TopBar, nullptr);

	TopBar.VSplitRight(82.0f, &TopBar, &Button);
	if(DoColorBtn(&s_BtnDDNetWeb, "DDNet.org", &Button, ColorRGBA(0.16f, 0.38f, 0.66f, 0.72f), IGraphics::CORNER_ALL, 4.0f, 10.5f))
	{
		const char *pTarget = Profiles.State() == EProfileState::READY ? Profiles.CurrentProfile().m_aName : s_ProfileSearchInput.GetString();
		if(!pTarget || pTarget[0] == '\0')
			pTarget = Client()->PlayerName();
		Profiles.OpenDDNetWeb(pTarget);
	}

	// Fila 2 (solo en modo Individual: buscador + Buscar + Mi Perfil)
	if(!IsTeamMode)
	{
		CUIRect IndivBar;
		Content.HSplitTop(LineHeight, &IndivBar, &Content);
		Content.HSplitTop(Margin, nullptr, &Content);

		IndivBar.VSplitLeft(180.0f, &SearchBox, &IndivBar);
		s_ProfileSearchInput.SetEmptyText(TCLocalize("Buscar jugador..."));
		Ui()->DoEditBox(&s_ProfileSearchInput, &SearchBox, FontSize);

		const bool SearchTriggered = s_ProfileSearchInput.IsActive() && (Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || Input()->KeyPress(KEY_RETURN) || Input()->KeyPress(KEY_KP_ENTER));

		IndivBar.VSplitLeft(Margin, nullptr, &IndivBar);

		static CButtonContainer s_BtnSearch;
		IndivBar.VSplitLeft(66.0f, &Button, &IndivBar);
		if(DoColorBtn(&s_BtnSearch, TCLocalize("Buscar"), &Button, ColorRGBA(0.20f, 0.55f, 0.88f, 0.85f), IGraphics::CORNER_ALL, 4.0f, 11.0f) || SearchTriggered)
		{
			const char *pSearch = s_ProfileSearchInput.GetString();
			char aTrimmed[128];
			if(pSearch)
			{
				str_copy(aTrimmed, pSearch);
				char *pStart = aTrimmed;
				while(*pStart && (unsigned char)*pStart <= ' ')
					pStart++;
				char *pEnd = pStart + str_length(pStart);
				while(pEnd > pStart && (unsigned char)*(pEnd - 1) <= ' ')
					pEnd--;
				*pEnd = '\0';
				if(pStart[0] != '\0')
				{
					str_copy(s_aLastSyncedReq, pStart);
					s_ProfileSearchInput.Set(pStart);
					if(IsKoG)
						Profiles.FetchKoGProfile(pStart);
					else
						Profiles.FetchProfile(pStart);
				}
			}
		}

		IndivBar.VSplitLeft(Margin, nullptr, &IndivBar);

		static CButtonContainer s_BtnMyProfile;
		IndivBar.VSplitLeft(78.0f, &Button, &IndivBar);
		if(DoColorBtn(&s_BtnMyProfile, TCLocalize("Mi Perfil"), &Button, ColorRGBA(0.20f, 0.62f, 0.38f, 0.85f), IGraphics::CORNER_ALL, 4.0f, 11.0f))
		{
			const char *pMyName = Client()->PlayerName();
			if(pMyName && pMyName[0] != '\0')
			{
				s_ProfileSearchInput.Set(pMyName);
				str_copy(s_aLastSyncedReq, pMyName);
				if(IsKoG)
					Profiles.FetchKoGProfile(pMyName);
				else
					Profiles.FetchProfile(pMyName);
			}
		}
	}

	// ==========================================
	// Team Unfinished Maps Comparison View
	// ==========================================
	if(IsTeamMode)
	{
		CUIRect TeamControls;
		Content.HSplitTop(LineHeight, &TeamControls, &Content);
		Content.HSplitTop(Margin, nullptr, &Content);

		// [ Detectar Equipo ]
		static CButtonContainer s_BtnDetectTeam;
		TeamControls.VSplitLeft(108.0f, &Button, &TeamControls);
		if(DoColorBtn(&s_BtnDetectTeam, TCLocalize("Detectar Equipo"), &Button, ColorRGBA(0.18f, 0.62f, 0.38f, 0.85f), IGraphics::CORNER_ALL, 4.0f, 11.0f))
		{
			Profiles.AutoDetectCurrentTeam();
		}

		TeamControls.VSplitLeft(Margin, nullptr, &TeamControls);

		// Input box to add custom player name
		static CLineInputBuffered<64> s_AddPlayerInput;
		CUIRect AddBox;
		TeamControls.VSplitLeft(140.0f, &AddBox, &TeamControls);
		s_AddPlayerInput.SetEmptyText(TCLocalize("Agregar jugador..."));
		Ui()->DoEditBox(&s_AddPlayerInput, &AddBox, FontSize);

		const bool AddTriggered = s_AddPlayerInput.IsActive() && (Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || Input()->KeyPress(KEY_RETURN) || Input()->KeyPress(KEY_KP_ENTER));

		TeamControls.VSplitLeft(Margin, nullptr, &TeamControls);

		// [ + Agregar ]
		static CButtonContainer s_BtnAddPlayer;
		TeamControls.VSplitLeft(74.0f, &Button, &TeamControls);
		if(DoColorBtn(&s_BtnAddPlayer, TCLocalize("+ Agregar"), &Button, ColorRGBA(0.20f, 0.52f, 0.86f, 0.85f), IGraphics::CORNER_ALL, 4.0f, 11.0f) || AddTriggered)
		{
			const char *pAdd = s_AddPlayerInput.GetString();
			if(pAdd && pAdd[0] != '\0')
			{
				Profiles.AddTeamPlayer(pAdd);
				s_AddPlayerInput.Clear();
			}
		}

		TeamControls.VSplitLeft(Margin, nullptr, &TeamControls);

		// [ Limpiar ]
		static CButtonContainer s_BtnClearTeam;
		TeamControls.VSplitLeft(62.0f, &Button, &TeamControls);
		if(DoColorBtn(&s_BtnClearTeam, TCLocalize("Limpiar"), &Button, ColorRGBA(0.68f, 0.22f, 0.24f, 0.82f), IGraphics::CORNER_ALL, 4.0f, 11.0f))
		{
			Profiles.ClearTeamPlayers();
		}

		// Status text on right side of controls
		TeamControls.VSplitLeft(Margin * 2.0f, nullptr, &TeamControls);
		TextRender()->TextColor(0.85f, 0.85f, 0.85f, 0.9f);
		Ui()->DoLabel(&TeamControls, Profiles.TeamStatusMsg(), 11.0f, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

		// Two-column layout
		CUIRect LeftCol, RightCol;
		Content.VSplitLeft(230.0f, &LeftCol, &RightCol);
		RightCol.VSplitLeft(Margin * 2.0f, nullptr, &RightCol);

		// ------------------------------------------
		// Left: Team Members
		// ------------------------------------------
		CUIRect LeftHeader;
		LeftCol.HSplitTop(26.0f, &LeftHeader, &LeftCol);
		LeftHeader.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), IGraphics::CORNER_T, 4.0f);

		char aMembersTitle[64];
		str_format(aMembersTitle, sizeof(aMembersTitle), "%s (%zu)", TCLocalize("Miembros del Equipo"), Profiles.TeamPlayers().size());
		TextRender()->TextColor(1.0f, 0.85f, 0.4f, 1.0f);
		Ui()->DoLabel(&LeftHeader, aMembersTitle, 11.5f, TEXTALIGN_MC);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

		LeftCol.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.2f), IGraphics::CORNER_B, 4.0f);
		LeftCol.Margin(4.0f, &LeftCol);

		static CScrollRegion s_ScrollTeamMembers;
		CScrollRegionParams MemParams;
		MemParams.m_ScrollUnit = 30.0f;
		MemParams.m_ForceShowScrollbar = false;
		s_ScrollTeamMembers.Begin(&LeftCol, &MemParams);

		if(Profiles.TeamPlayers().empty())
		{
			CUIRect EmptyLabel;
			LeftCol.HSplitTop(60.0f, &EmptyLabel, &LeftCol);
			if(s_ScrollTeamMembers.AddRect(EmptyLabel))
			{
				TextRender()->TextColor(0.6f, 0.6f, 0.6f, 0.8f);
				Ui()->DoLabel(&EmptyLabel, TCLocalize("Ningun jugador en el equipo.\nUsa 'Detectar Equipo' o\nagrega nombres arriba."), 10.5f, TEXTALIGN_MC);
				TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			}
		}
		else
		{
			const float ItemH = 26.0f;
			std::string RemoveTarget = "";

			for(size_t i = 0; i < Profiles.TeamPlayers().size(); ++i)
			{
				const std::string &PlayerName = Profiles.TeamPlayers()[i];
				CUIRect ItemRect;
				LeftCol.HSplitTop(ItemH, &ItemRect, &LeftCol);
				LeftCol.HSplitTop(2.0f, nullptr, &LeftCol);

				if(s_ScrollTeamMembers.AddRect(ItemRect))
				{
					ItemRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 3.0f);
					ItemRect.Margin(3.0f, &ItemRect);

					CUIRect RemoveBtn, StatusCol, NameCol;
					ItemRect.VSplitRight(22.0f, &StatusCol, &RemoveBtn);
					StatusCol.VSplitRight(55.0f, &NameCol, &StatusCol);

					// Player Name
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
					Ui()->DoLabel(&NameCol, PlayerName.c_str(), 11.5f, TEXTALIGN_ML);

					// Status badge
					bool Loaded = Profiles.IsPlayerProfileLoaded(PlayerName.c_str(), IsKoG);
					if(Loaded)
					{
						TextRender()->TextColor(0.4f, 1.0f, 0.4f, 1.0f);
						Ui()->DoLabel(&StatusCol, TCLocalize("Listo"), 10.0f, TEXTALIGN_MC);
					}
					else
					{
						TextRender()->TextColor(1.0f, 0.7f, 0.2f, 1.0f);
						Ui()->DoLabel(&StatusCol, TCLocalize("Cargando..."), 9.5f, TEXTALIGN_MC);
					}
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

					// Remove button
					static CButtonContainer s_aBtnRemoveMember[64];
					CButtonContainer *pRemBtn = (i < 64) ? &s_aBtnRemoveMember[i] : &s_aBtnRemoveMember[0];
					if(DoButton_Menu(pRemBtn, "X", 0, &RemoveBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 2.0f, 0.0f, ColorRGBA(0.7f, 0.2f, 0.2f, 0.6f)))
					{
						RemoveTarget = PlayerName;
					}
				}
			}

			if(!RemoveTarget.empty())
			{
				Profiles.RemoveTeamPlayer(RemoveTarget.c_str());
			}
		}

		s_ScrollTeamMembers.End();

		// ------------------------------------------
		// Right: Common Unfinished Maps
		// ------------------------------------------
		CUIRect RightHeader;
		RightCol.HSplitTop(28.0f, &RightHeader, &RightCol);
		RightHeader.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), IGraphics::CORNER_T, 4.0f);
		RightHeader.Margin(3.0f, &RightHeader);

		CUIRect MapFilterBox, SortBtnRect, RightTitle;
		RightHeader.VSplitRight(140.0f, &RightTitle, &MapFilterBox);
		RightTitle.VSplitRight(4.0f, &RightTitle, nullptr);
		RightTitle.VSplitRight(96.0f, &RightTitle, &SortBtnRect);
		RightTitle.VSplitRight(6.0f, &RightTitle, nullptr);

		static int s_TeamMapSortMode = 0; // 0 = A-Z, 1 = Puntos desc, 2 = Puntos asc
		static CButtonContainer s_BtnTeamSort;
		const char *pTeamSortLabel = s_TeamMapSortMode == 1 ? TCLocalize("Puntos (Mayor)") : (s_TeamMapSortMode == 2 ? TCLocalize("Puntos (Menor)") : TCLocalize("Orden: A-Z"));
		const ColorRGBA TeamSortCol = s_TeamMapSortMode == 1 ? ColorRGBA(0.78f, 0.54f, 0.14f, 0.88f) : (s_TeamMapSortMode == 2 ? ColorRGBA(0.16f, 0.56f, 0.52f, 0.88f) : ColorRGBA(0.22f, 0.25f, 0.34f, 0.82f));
		if(DoColorBtn(&s_BtnTeamSort, pTeamSortLabel, &SortBtnRect, TeamSortCol, IGraphics::CORNER_ALL, 3.0f, 10.0f, s_TeamMapSortMode != 0))
		{
			s_TeamMapSortMode = (s_TeamMapSortMode + 1) % 3;
		}

		static CLineInputBuffered<64> s_TeamMapFilter;
		s_TeamMapFilter.SetEmptyText(TCLocalize("Filtrar mapa..."));
		Ui()->DoEditBox(&s_TeamMapFilter, &MapFilterBox, 11.0f);

		// Category Filter Buttons Row
		CUIRect CatFilterRow;
		RightCol.HSplitTop(22.0f, &CatFilterRow, &RightCol);
		RightCol.HSplitTop(3.0f, nullptr, &RightCol);

		static char s_aSelectedCategory[32] = "";
		static int s_LastSourceForCat = -1;
		if(s_LastSourceForCat != g_Config.m_AcProfileSource)
		{
			s_aSelectedCategory[0] = '\0';
			s_LastSourceForCat = g_Config.m_AcProfileSource;
		}

		const char *apCatsKoG[] = { "Todos", "Easy", "Main", "Hard", "Insane", "Extreme", "Mod", "Otros" };
		const char *apCatsDDNet[] = { "Todos", "Novice", "Moderate", "Brutal", "Insane", "Dummy", "DDMaX", "Solo" };

		const char **apCats = IsKoG ? apCatsKoG : apCatsDDNet;
		const int NumCats = IsKoG ? (int)(sizeof(apCatsKoG) / sizeof(apCatsKoG[0])) : (int)(sizeof(apCatsDDNet) / sizeof(apCatsDDNet[0]));

		static CButtonContainer s_aBtnCatFilter[16];

		const float CatBtnW = CatFilterRow.w / (float)NumCats;
		for(int c = 0; c < NumCats; ++c)
		{
			CUIRect CatBtnRect;
			CatFilterRow.VSplitLeft(CatBtnW, &CatBtnRect, &CatFilterRow);

			const bool IsAll = (c == 0);
			const bool IsActive = (IsAll && s_aSelectedCategory[0] == '\0') ||
				(!IsAll && str_comp_nocase(s_aSelectedCategory, apCats[c]) == 0);

			int Corners = IGraphics::CORNER_NONE;
			if(c == 0) Corners = IGraphics::CORNER_L;
			else if(c == NumCats - 1) Corners = IGraphics::CORNER_R;

			ColorRGBA BtnCol;
			if(IsActive)
			{
				BtnCol = IsKoG ? ColorRGBA(0.86f, 0.44f, 0.12f, 0.90f) : ColorRGBA(0.18f, 0.54f, 0.86f, 0.90f);
			}
			else
			{
				BtnCol = ColorRGBA(0.13f, 0.15f, 0.21f, 0.65f);
			}

			if(DoColorBtn(&s_aBtnCatFilter[c], TCLocalize(apCats[c]), &CatBtnRect, BtnCol, Corners, 3.0f, 10.5f, IsActive))
			{
				if(IsAll || IsActive)
					s_aSelectedCategory[0] = '\0';
				else
					str_copy(s_aSelectedCategory, apCats[c]);
			}
		}

		RightCol.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.2f), IGraphics::CORNER_B, 4.0f);
		RightCol.Margin(4.0f, &RightCol);

		static CScrollRegion s_ScrollTeamMaps;
		CScrollRegionParams TeamMapScrollParams;
		TeamMapScrollParams.m_ScrollUnit = 30.0f;
		TeamMapScrollParams.m_ForceShowScrollbar = false;
		s_ScrollTeamMaps.Begin(&RightCol, &TeamMapScrollParams);

		const char *pFilter = s_TeamMapFilter.GetString();
		char aFilterLower[64] = "";
		if(pFilter && pFilter[0])
		{
			str_copy(aFilterLower, pFilter);
			for(char *p = aFilterLower; *p; ++p) *p = (char)tolower((unsigned char)*p);
		}

		const auto &vCommonMaps = Profiles.CommonUnfinishedMaps();
		std::vector<size_t> vFilteredIdx;
		vFilteredIdx.reserve(vCommonMaps.size());
		for(size_t i = 0; i < vCommonMaps.size(); ++i)
		{
			const CTeamUnfinishedMap &M = vCommonMaps[i];
			if(!MatchesCategoryFilter(M.m_aCategory, s_aSelectedCategory))
				continue;
			if(aFilterLower[0] != '\0')
			{
				char aMapLower[64];
				str_copy(aMapLower, M.m_aMap);
				for(char *p = aMapLower; *p; ++p) *p = (char)tolower((unsigned char)*p);
				if(str_find(aMapLower, aFilterLower) == nullptr)
					continue;
			}
			vFilteredIdx.push_back(i);
		}

		if(s_TeamMapSortMode == 1)
		{
			std::stable_sort(vFilteredIdx.begin(), vFilteredIdx.end(), [&](size_t A, size_t B) {
				if(vCommonMaps[A].m_Points != vCommonMaps[B].m_Points)
					return vCommonMaps[A].m_Points > vCommonMaps[B].m_Points;
				return str_comp_nocase(vCommonMaps[A].m_aMap, vCommonMaps[B].m_aMap) < 0;
			});
		}
		else if(s_TeamMapSortMode == 2)
		{
			std::stable_sort(vFilteredIdx.begin(), vFilteredIdx.end(), [&](size_t A, size_t B) {
				const int Pa = vCommonMaps[A].m_Points > 0 ? vCommonMaps[A].m_Points : 999999;
				const int Pb = vCommonMaps[B].m_Points > 0 ? vCommonMaps[B].m_Points : 999999;
				if(Pa != Pb)
					return Pa < Pb;
				return str_comp_nocase(vCommonMaps[A].m_aMap, vCommonMaps[B].m_aMap) < 0;
			});
		}

		const float ItemH = 24.0f;
		const size_t Shown = vFilteredIdx.size();

		for(size_t idx = 0; idx < vFilteredIdx.size(); ++idx)
		{
			const size_t i = vFilteredIdx[idx];
			const CTeamUnfinishedMap &M = vCommonMaps[i];

			CUIRect ItemRect;
			RightCol.HSplitTop(ItemH, &ItemRect, &RightCol);
			RightCol.HSplitTop(2.0f, nullptr, &RightCol);

			if(s_ScrollTeamMaps.AddRect(ItemRect))
			{
				ItemRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 3.0f);
				ItemRect.Margin(3.0f, &ItemRect);

				// Category Badge (fixed width)
				CUIRect CatBadge;
				ItemRect.VSplitLeft(56.0f, &CatBadge, &ItemRect);
				ItemRect.VSplitLeft(4.0f, nullptr, &ItemRect);
				ColorRGBA CatCol = GetCategoryColor(M.m_aCategory);
				ColorRGBA CatBgCol = ColorRGBA(CatCol.r * 0.35f, CatCol.g * 0.35f, CatCol.b * 0.35f, 0.6f);
				CatBadge.Draw(CatBgCol, IGraphics::CORNER_ALL, 2.0f);
				TextRender()->TextColor(CatCol.r, CatCol.g, CatCol.b, 1.0f);
				Ui()->DoLabel(&CatBadge, M.m_aCategory[0] ? M.m_aCategory : "-", 10.0f, TEXTALIGN_MC);

				// Points Badge (ALWAYS reserved width so map names align perfectly)
				CUIRect PtsBadge;
				ItemRect.VSplitLeft(46.0f, &PtsBadge, &ItemRect);
				ItemRect.VSplitLeft(6.0f, nullptr, &ItemRect);
				if(M.m_Points > 0)
				{
					char aPtsBuf[16];
					str_format(aPtsBuf, sizeof(aPtsBuf), "%d pts", M.m_Points);
					TextRender()->TextColor(1.0f, 0.85f, 0.3f, 0.95f);
					Ui()->DoLabel(&PtsBadge, aPtsBuf, 10.0f, TEXTALIGN_MC);
				}
				else
				{
					TextRender()->TextColor(0.45f, 0.48f, 0.55f, 0.65f);
					Ui()->DoLabel(&PtsBadge, "-", 10.0f, TEXTALIGN_MC);
				}

				if(IsOnline)
				{
					bool DifferentDiff = (aCurCategory[0] != '\0' && M.m_aCategory[0] != '\0' && str_comp_nocase(M.m_aCategory, aCurCategory) != 0);

					CUIRect ActionCol;
					ItemRect.VSplitRight(54.0f, &ItemRect, &ActionCol);
					static std::vector<CButtonContainer> s_vBtnVoteMap;
					if(s_vBtnVoteMap.size() < vCommonMaps.size())
						s_vBtnVoteMap.resize(vCommonMaps.size());
					CButtonContainer *pVoteBtn = &s_vBtnVoteMap[i];
					if(DoColorBtn(pVoteBtn, TCLocalize("Votar"), &ActionCol, ColorRGBA(0.18f, 0.50f, 0.86f, 0.82f), IGraphics::CORNER_ALL, 3.0f, 10.0f))
					{
						VoteForMap(M.m_aMap);
					}

					if(DifferentDiff)
					{
						CUIRect DiffCol;
						ItemRect.VSplitRight(4.0f, &ItemRect, nullptr);
						ItemRect.VSplitRight(98.0f, &ItemRect, &DiffCol);
						static std::vector<CButtonContainer> s_vBtnVoteDiff;
						if(s_vBtnVoteDiff.size() < vCommonMaps.size())
							s_vBtnVoteDiff.resize(vCommonMaps.size());
						CButtonContainer *pDiffBtn = &s_vBtnVoteDiff[i];
						if(DoColorBtn(pDiffBtn, TCLocalize("Votar Dificultad"), &DiffCol, ColorRGBA(0.84f, 0.45f, 0.12f, 0.85f), IGraphics::CORNER_ALL, 3.0f, 10.0f))
						{
							VoteForCategory(M.m_aCategory);
						}
					}
					ItemRect.VSplitRight(6.0f, &ItemRect, nullptr);
				}

				TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
				Ui()->DoLabel(&ItemRect, M.m_aMap, 11.5f, TEXTALIGN_ML);
			}
		}

		char aTitleBuf[96];
		if(s_aSelectedCategory[0] != '\0' || aFilterLower[0] != '\0')
			str_format(aTitleBuf, sizeof(aTitleBuf), "%s: %zu / %zu", TCLocalize("Mapas en comun"), Shown, Profiles.CommonUnfinishedMaps().size());
		else
			str_format(aTitleBuf, sizeof(aTitleBuf), "%s: %zu", TCLocalize("Mapas en comun pendientes"), Profiles.CommonUnfinishedMaps().size());
		TextRender()->TextColor(1.0f, 0.85f, 0.3f, 1.0f);
		Ui()->DoLabel(&RightTitle, aTitleBuf, 11.5f, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

		if(Shown == 0)
		{
			CUIRect EmptyLabel;
			RightCol.HSplitTop(40.0f, &EmptyLabel, &RightCol);
			if(s_ScrollTeamMaps.AddRect(EmptyLabel))
			{
				TextRender()->TextColor(0.6f, 0.6f, 0.6f, 0.8f);
				if(Profiles.TeamPlayers().empty())
					Ui()->DoLabel(&EmptyLabel, TCLocalize("Agrega jugadores para calcular mapas pendientes"), 11.0f, TEXTALIGN_MC);
				else if(!Profiles.TeamComparisonReady())
					Ui()->DoLabel(&EmptyLabel, TCLocalize("Cargando perfiles de los miembros del equipo..."), 11.0f, TEXTALIGN_MC);
				else
					Ui()->DoLabel(&EmptyLabel, TCLocalize("No se encontraron mapas pendientes en comun"), 11.0f, TEXTALIGN_MC);
				TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			}
		}

		s_ScrollTeamMaps.End();
		return;
	}

	// ==========================================
	// KoG Profile View
	// ==========================================
	if(IsKoG)
	{
		EProfileState State = Profiles.KoGState();

		if(State == EProfileState::IDLE)
		{
			CUIRect EmptyLabel;
			Content.HSplitTop(60.0f, nullptr, &EmptyLabel);
			TextRender()->TextColor(0.8f, 0.8f, 0.8f, 0.9f);
			Ui()->DoLabel(&EmptyLabel, TCLocalize("Ingresa un jugador y presiona 'Buscar' o consulta 'Mi Perfil' para ver datos de KoG."), 14.0f, TEXTALIGN_MC);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			return;
		}

		if(State == EProfileState::FETCHING)
		{
			CUIRect LoadingBox;
			Content.HSplitTop(80.0f, nullptr, &LoadingBox);
			LoadingBox.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 5.0f);
			TextRender()->TextColor(1.0f, 0.7f, 0.2f, 1.0f);
			Ui()->DoLabel(&LoadingBox, Profiles.KoGStatusMsg(), 14.0f, TEXTALIGN_MC);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			return;
		}

		if(State == EProfileState::NOT_FOUND || State == EProfileState::ERROR_NETWORK)
		{
			CUIRect ErrorBox;
			Content.HSplitTop(75.0f, nullptr, &ErrorBox);
			ErrorBox.Draw(ColorRGBA(0.3f, 0.1f, 0.1f, 0.35f), IGraphics::CORNER_ALL, 5.0f);
			TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
			Ui()->DoLabel(&ErrorBox, Profiles.KoGStatusMsg(), 14.0f, TEXTALIGN_MC);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			return;
		}

		// State == READY
		const CKoGPlayerProfile &Prof = Profiles.CurrentKoGProfile();

		// Header Card
		CUIRect HeaderCard, CardLeft, CardMid, CardRight, Label;
		Content.HSplitTop(52.0f, &HeaderCard, &Content);
		HeaderCard.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), IGraphics::CORNER_ALL, 5.0f);
		HeaderCard.Margin(5.0f, &HeaderCard);

		// Left: Player Name & Skin
		HeaderCard.VSplitLeft(200.0f, &CardLeft, &HeaderCard);
		CardLeft.HSplitTop(20.0f, &Label, &CardLeft);
		TextRender()->TextColor(1.0f, 0.75f, 0.2f, 1.0f);
		Ui()->DoLabel(&Label, Prof.m_aName, 15.0f, TEXTALIGN_ML);

		char aSkinInfo[128];
		if(Prof.m_aSkinName[0] != '\0')
			str_format(aSkinInfo, sizeof(aSkinInfo), "Skin: %s %s", Prof.m_aSkinName, Prof.m_FromDiskCache ? "(Cache local)" : "");
		else
			str_format(aSkinInfo, sizeof(aSkinInfo), "%s", Prof.m_FromDiskCache ? "(En cache local)" : "");

		CardLeft.HSplitTop(16.0f, &Label, &CardLeft);
		TextRender()->TextColor(0.6f, 0.85f, 0.95f, 0.85f);
		Ui()->DoLabel(&Label, aSkinInfo, 10.5f, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

		// Mid: Total Points, Rank, Season, PvP
		HeaderCard.VSplitLeft(HeaderCard.w * 0.58f, &CardMid, &CardRight);

		char aPointsStr[128];
		if(Prof.m_Rank > 0)
			str_format(aPointsStr, sizeof(aPointsStr), "Puntos KoG: %d pts  |  Rank: #%d", Prof.m_TotalPoints, Prof.m_Rank);
		else
			str_format(aPointsStr, sizeof(aPointsStr), "Puntos KoG: %d pts", Prof.m_TotalPoints);

		CardMid.HSplitTop(18.0f, &Label, &CardMid);
		TextRender()->TextColor(0.4f, 0.95f, 0.4f, 1.0f);
		Ui()->DoLabel(&Label, aPointsStr, 12.0f, TEXTALIGN_ML);

		char aSubPts[128];
		str_format(aSubPts, sizeof(aSubPts), "Temporada: %d pts  |  PvP: %d pts", Prof.m_SeasonPoints, Prof.m_PvPPoints);
		CardMid.HSplitTop(16.0f, &Label, &CardMid);
		TextRender()->TextColor(0.85f, 0.85f, 0.6f, 0.9f);
		Ui()->DoLabel(&Label, aSubPts, 10.5f, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

		// Right: Time Wasted & Finishes Count
		char aTimeStr[128];
		if(Prof.m_TimeWasted > 0)
		{
			int Hours = (int)(Prof.m_TimeWasted / 3600);
			int Mins = (int)((Prof.m_TimeWasted % 3600) / 60);
			str_format(aTimeStr, sizeof(aTimeStr), "Tiempo: %dh %02dm", Hours, Mins);
		}
		else
		{
			str_copy(aTimeStr, "Tiempo: 0h");
		}

		CardRight.HSplitTop(18.0f, &Label, &CardRight);
		TextRender()->TextColor(0.5f, 0.85f, 1.0f, 1.0f);
		Ui()->DoLabel(&Label, aTimeStr, 11.5f, TEXTALIGN_ML);

		char aFinCountStr[64];
		str_format(aFinCountStr, sizeof(aFinCountStr), "Mapas terminados: %zu", Prof.m_vFinishes.size());
		CardRight.HSplitTop(16.0f, &Label, &CardRight);
		TextRender()->TextColor(0.8f, 0.8f, 0.8f, 0.85f);
		Ui()->DoLabel(&Label, aFinCountStr, 10.5f, TEXTALIGN_ML);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

		Content.HSplitTop(Margin, nullptr, &Content);

		// Split View: Left = Categories & Teammates, Right = Map Finishes & Unfinished
		CUIRect LeftCol, RightCol;
		Content.VSplitMid(&LeftCol, &RightCol, Margin);

		// --- Left Column ---
		CUIRect LeftHeader, LeftList, TabBtn;
		LeftCol.HSplitTop(22.0f, &LeftHeader, &LeftList);
		LeftHeader.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 3.0f);

		static int s_KoGLeftTab = 0; // 0: Categorias, 1: Compañeros
		static CButtonContainer s_BtnKoGCats;
		static CButtonContainer s_BtnKoGTms;

		LeftHeader.VSplitLeft(100.0f, &TabBtn, &LeftHeader);
		ColorRGBA TabCatCol = (s_KoGLeftTab == 0) ? ColorRGBA(0.9f, 0.5f, 0.1f, 0.7f) : ColorRGBA(0.2f, 0.2f, 0.2f, 0.3f);
		if(DoButton_Menu(&s_BtnKoGCats, TCLocalize("Categorias"), 0, &TabBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L, 3.0f, 0.0f, TabCatCol))
			s_KoGLeftTab = 0;

		char aTmTabStr[64];
		str_format(aTmTabStr, sizeof(aTmTabStr), "%s (%zu)", TCLocalize("Compañeros"), Prof.m_vTeammates.size());
		ColorRGBA TabTmCol = (s_KoGLeftTab == 1) ? ColorRGBA(0.9f, 0.5f, 0.1f, 0.7f) : ColorRGBA(0.2f, 0.2f, 0.2f, 0.3f);
		if(DoButton_Menu(&s_BtnKoGTms, aTmTabStr, 0, &LeftHeader, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R, 3.0f, 0.0f, TabTmCol))
			s_KoGLeftTab = 1;

		LeftList.Margin(2.0f, &LeftList);

		if(s_KoGLeftTab == 0)
		{
			static CScrollRegion s_ScrollKoGCats;
			CScrollRegionParams CatScrollParams;
			CatScrollParams.m_ScrollUnit = 40.0f;
			CatScrollParams.m_ForceShowScrollbar = false;
			CatScrollParams.m_ScrollbarMargin = 3.0f;
			s_ScrollKoGCats.Begin(&LeftList, &CatScrollParams);

			const float ItemH = 26.0f;
			for(size_t i = 0; i < Prof.m_vCategories.size(); ++i)
			{
				const CKoGCategoryStats &Cat = Prof.m_vCategories[i];
				CUIRect ItemRect;
				LeftList.HSplitTop(ItemH, &ItemRect, &LeftList);
				LeftList.HSplitTop(2.0f, nullptr, &LeftList);

				if(s_ScrollKoGCats.AddRect(ItemRect))
				{
					ItemRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.2f), IGraphics::CORNER_ALL, 3.0f);
					ItemRect.Margin(3.0f, &ItemRect);

					CUIRect NameCol, StatsCol, BarRect, BarFill;
					ItemRect.HSplitTop(14.0f, &NameCol, &BarRect);
					NameCol.VSplitLeft(100.0f, &NameCol, &StatsCol);

					ColorRGBA CatColor = ColorRGBA(0.9f, 0.9f, 0.9f, 1.0f);
					if(str_comp_nocase(Cat.m_aName, "Solo") == 0) CatColor = ColorRGBA(0.3f, 0.8f, 1.0f, 1.0f);
					else if(str_comp_nocase(Cat.m_aName, "Easy") == 0) CatColor = ColorRGBA(0.4f, 0.95f, 0.4f, 1.0f);
					else if(str_comp_nocase(Cat.m_aName, "Main") == 0) CatColor = ColorRGBA(0.95f, 0.95f, 0.3f, 1.0f);
					else if(str_comp_nocase(Cat.m_aName, "Hard") == 0) CatColor = ColorRGBA(0.95f, 0.55f, 0.2f, 1.0f);
					else if(str_comp_nocase(Cat.m_aName, "Insane") == 0) CatColor = ColorRGBA(0.95f, 0.25f, 0.25f, 1.0f);
					else if(str_comp_nocase(Cat.m_aName, "Extreme") == 0) CatColor = ColorRGBA(0.85f, 0.3f, 0.95f, 1.0f);
					else if(str_comp_nocase(Cat.m_aName, "Moderate") == 0) CatColor = ColorRGBA(0.9f, 0.75f, 0.3f, 1.0f);

					TextRender()->TextColor(CatColor);
					Ui()->DoLabel(&NameCol, Cat.m_aName, 11.0f, TEXTALIGN_ML);

					float Pct = Cat.m_Total > 0 ? ((float)Cat.m_Finished / (float)Cat.m_Total) * 100.0f : 0.0f;
					char aStatsStr[64];
					str_format(aStatsStr, sizeof(aStatsStr), "%d / %d mapas (%.1f%%)", Cat.m_Finished, Cat.m_Total, Pct);
					TextRender()->TextColor(0.85f, 0.85f, 0.85f, 1.0f);
					Ui()->DoLabel(&StatsCol, aStatsStr, 10.0f, TEXTALIGN_MR);
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

					// Mini progress bar
					BarRect.Margin(1.0f, &BarRect);
					BarRect.Draw(ColorRGBA(0.1f, 0.1f, 0.1f, 0.7f), IGraphics::CORNER_ALL, 1.5f);
					float FillW = std::clamp(Pct / 100.0f, 0.0f, 1.0f) * BarRect.w;
					if(FillW >= 1.0f)
					{
						BarRect.VSplitLeft(FillW, &BarFill, nullptr);
						BarFill.Draw(CatColor, IGraphics::CORNER_ALL, 1.5f);
					}
				}
			}
			s_ScrollKoGCats.End();
		}
		else
		{
			// Teammates List
			static CScrollRegion s_ScrollKoGTms;
			CScrollRegionParams TmScrollParams;
			TmScrollParams.m_ScrollUnit = 40.0f;
			TmScrollParams.m_ForceShowScrollbar = false;
			TmScrollParams.m_ScrollbarMargin = 3.0f;
			s_ScrollKoGTms.Begin(&LeftList, &TmScrollParams);

			const float ItemH = 22.0f;
			if(Prof.m_vTeammates.empty())
			{
				CUIRect EmptyLabel;
				LeftList.HSplitTop(30.0f, &EmptyLabel, &LeftList);
				if(s_ScrollKoGTms.AddRect(EmptyLabel))
				{
					TextRender()->TextColor(0.6f, 0.6f, 0.6f, 0.8f);
					Ui()->DoLabel(&EmptyLabel, TCLocalize("Sin compañeros registrados"), 11.0f, TEXTALIGN_MC);
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
				}
			}
			else
			{
				for(size_t i = 0; i < Prof.m_vTeammates.size(); ++i)
				{
					const CKoGTeammate &Tm = Prof.m_vTeammates[i];
					CUIRect ItemRect;
					LeftList.HSplitTop(ItemH, &ItemRect, &LeftList);
					LeftList.HSplitTop(2.0f, nullptr, &LeftList);

					if(s_ScrollKoGTms.AddRect(ItemRect))
					{
						ItemRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.2f), IGraphics::CORNER_ALL, 3.0f);
						ItemRect.Margin(3.0f, &ItemRect);

						CUIRect TmNameCol, TmFinCol;
						ItemRect.VSplitLeft(ItemRect.w * 0.55f, &TmNameCol, &TmFinCol);

						TextRender()->TextColor(1.0f, 0.9f, 0.4f, 1.0f);
						Ui()->DoLabel(&TmNameCol, Tm.m_aName, 11.0f, TEXTALIGN_ML);

						char aTmFinStr[64];
						str_format(aTmFinStr, sizeof(aTmFinStr), "%d finishes juntos", Tm.m_Finishes);
						TextRender()->TextColor(0.8f, 0.8f, 0.8f, 0.9f);
						Ui()->DoLabel(&TmFinCol, aTmFinStr, 10.0f, TEXTALIGN_MR);
						TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
					}
				}
			}
			s_ScrollKoGTms.End();
		}

		// --- Right Column ---
		CUIRect RightHeader, RightFilter, RightList;
		RightCol.HSplitTop(22.0f, &RightHeader, &RightCol);
		RightCol.HSplitTop(2.0f, nullptr, &RightCol);
		RightCol.HSplitTop(20.0f, &RightFilter, &RightList);

		RightHeader.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 3.0f);

		static int s_KoGRightTab = 0; // 0: Finishes, 1: Pendientes
		static CButtonContainer s_BtnKoGFins;
		static CButtonContainer s_BtnKoGUnfin;

		char aFinTabStr[64];
		str_format(aFinTabStr, sizeof(aFinTabStr), "%s (%zu)", TCLocalize("Mapas Terminados"), Prof.m_vFinishes.size());
		RightHeader.VSplitLeft(RightHeader.w * 0.5f, &TabBtn, &RightHeader);
		if(DoColorBtn(&s_BtnKoGFins, aFinTabStr, &TabBtn, s_KoGRightTab == 0 ? ColorRGBA(0.20f, 0.54f, 0.86f, 0.88f) : InactiveTabCol, IGraphics::CORNER_L, 3.0f, 10.5f, s_KoGRightTab == 0))
			s_KoGRightTab = 0;

		char aUnfinTabStr[64];
		str_format(aUnfinTabStr, sizeof(aUnfinTabStr), "%s (%zu)", TCLocalize("Pendientes"), Prof.m_vUnfinishedMaps.size());
		if(DoColorBtn(&s_BtnKoGUnfin, aUnfinTabStr, &RightHeader, s_KoGRightTab == 1 ? ColorRGBA(0.86f, 0.44f, 0.12f, 0.88f) : InactiveTabCol, IGraphics::CORNER_R, 3.0f, 10.5f, s_KoGRightTab == 1))
			s_KoGRightTab = 1;

		// Map search filter input + sort by points (in Pendientes tab)
		static int s_KoGUnfinSortMode = 0; // 0 = A-Z, 1 = Puntos desc, 2 = Puntos asc
		static CButtonContainer s_BtnKoGUnfinSort;
		if(s_KoGRightTab == 1)
		{
			CUIRect KoGSortRect;
			RightFilter.VSplitRight(96.0f, &RightFilter, &KoGSortRect);
			RightFilter.VSplitRight(4.0f, &RightFilter, nullptr);
			const char *pSortLbl = s_KoGUnfinSortMode == 1 ? TCLocalize("Puntos (Mayor)") : (s_KoGUnfinSortMode == 2 ? TCLocalize("Puntos (Menor)") : TCLocalize("Orden: A-Z"));
			const ColorRGBA SortCol = s_KoGUnfinSortMode == 1 ? ColorRGBA(0.78f, 0.54f, 0.14f, 0.88f) : (s_KoGUnfinSortMode == 2 ? ColorRGBA(0.16f, 0.56f, 0.52f, 0.88f) : ColorRGBA(0.22f, 0.25f, 0.34f, 0.82f));
			if(DoColorBtn(&s_BtnKoGUnfinSort, pSortLbl, &KoGSortRect, SortCol, IGraphics::CORNER_ALL, 3.0f, 10.0f, s_KoGUnfinSortMode != 0))
			{
				s_KoGUnfinSortMode = (s_KoGUnfinSortMode + 1) % 3;
			}
		}

		static CLineInputBuffered<64> s_KoGMapFilter;
		s_KoGMapFilter.SetEmptyText(TCLocalize("Filtrar por nombre de mapa..."));
		Ui()->DoEditBox(&s_KoGMapFilter, &RightFilter, 11.0f);

		const char *pFilter = s_KoGMapFilter.GetString();
		char aFilterLower[64] = "";
		if(pFilter && pFilter[0] != '\0')
		{
			str_copy(aFilterLower, pFilter);
			for(char *p = aFilterLower; *p; ++p) *p = (char)tolower((unsigned char)*p);
		}

		RightList.Margin(2.0f, &RightList);

		if(s_KoGRightTab == 0)
		{
			// Finishes list
			static CScrollRegion s_ScrollKoGFinishes;
			CScrollRegionParams FinScrollParams;
			FinScrollParams.m_ScrollUnit = 40.0f;
			FinScrollParams.m_ForceShowScrollbar = false;
			FinScrollParams.m_ScrollbarMargin = 3.0f;
			s_ScrollKoGFinishes.Begin(&RightList, &FinScrollParams);

			const float ItemH = 22.0f;
			size_t Shown = 0;

			for(size_t i = 0; i < Prof.m_vFinishes.size(); ++i)
			{
				const CKoGFinish &F = Prof.m_vFinishes[i];
				if(aFilterLower[0] != '\0')
				{
					char aMapLower[64];
					str_copy(aMapLower, F.m_aMap);
					for(char *p = aMapLower; *p; ++p) *p = (char)tolower((unsigned char)*p);
					if(str_find(aMapLower, aFilterLower) == nullptr)
						continue;
				}

				Shown++;
				CUIRect ItemRect;
				RightList.HSplitTop(ItemH, &ItemRect, &RightList);
				RightList.HSplitTop(2.0f, nullptr, &RightList);

				if(s_ScrollKoGFinishes.AddRect(ItemRect))
				{
					ItemRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.2f), IGraphics::CORNER_ALL, 3.0f);
					ItemRect.Margin(3.0f, &ItemRect);

					CUIRect MapNameCol, CountCol, TimeCol;
					ItemRect.VSplitLeft(ItemRect.w * 0.5f, &MapNameCol, &CountCol);
					CountCol.VSplitLeft(ItemRect.w * 0.2f, &CountCol, &TimeCol);

					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
					Ui()->DoLabel(&MapNameCol, F.m_aMap, 11.0f, TEXTALIGN_ML);

					char aCountBuf[32];
					str_format(aCountBuf, sizeof(aCountBuf), "x%d", F.m_Count);
					TextRender()->TextColor(1.0f, 0.75f, 0.3f, 0.9f);
					Ui()->DoLabel(&CountCol, aCountBuf, 10.5f, TEXTALIGN_MC);

					char aTimeBuf[32];
					int Minutes = (int)(F.m_Time / 60.0f);
					float Seconds = F.m_Time - (Minutes * 60);
					if(Minutes > 0)
						str_format(aTimeBuf, sizeof(aTimeBuf), "%02d:%05.2f", Minutes, Seconds);
					else
						str_format(aTimeBuf, sizeof(aTimeBuf), "%05.2f", Seconds);

					TextRender()->TextColor(0.4f, 1.0f, 0.4f, 1.0f);
					Ui()->DoLabel(&TimeCol, aTimeBuf, 11.0f, TEXTALIGN_MR);
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
				}
			}

			if(Shown == 0)
			{
				CUIRect EmptyLabel;
				RightList.HSplitTop(30.0f, &EmptyLabel, &RightList);
				if(s_ScrollKoGFinishes.AddRect(EmptyLabel))
				{
					TextRender()->TextColor(0.6f, 0.6f, 0.6f, 0.8f);
					Ui()->DoLabel(&EmptyLabel, TCLocalize("Ningun mapa coincide con la busqueda"), 11.0f, TEXTALIGN_MC);
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
				}
			}

			s_ScrollKoGFinishes.End();
		}
		else
		{
			// Unfinished maps list
			static CScrollRegion s_ScrollKoGUnfinished;
			CScrollRegionParams UnfinScrollParams;
			UnfinScrollParams.m_ScrollUnit = 40.0f;
			UnfinScrollParams.m_ForceShowScrollbar = false;
			UnfinScrollParams.m_ScrollbarMargin = 3.0f;
			s_ScrollKoGUnfinished.Begin(&RightList, &UnfinScrollParams);

			struct SUnfinRow
			{
				size_t m_Idx;
				CMapMetadata m_Meta;
				bool m_HasMeta;
			};
			std::vector<SUnfinRow> vUnfinRows;
			vUnfinRows.reserve(Prof.m_vUnfinishedMaps.size());
			for(size_t i = 0; i < Prof.m_vUnfinishedMaps.size(); ++i)
			{
				const std::string &MapName = Prof.m_vUnfinishedMaps[i];
				if(aFilterLower[0] != '\0')
				{
					char aMapLower[64];
					str_copy(aMapLower, MapName.c_str());
					for(char *p = aMapLower; *p; ++p) *p = (char)tolower((unsigned char)*p);
					if(str_find(aMapLower, aFilterLower) == nullptr)
						continue;
				}
				CMapMetadata Meta{};
				const bool HasMeta = Profiles.GetKoGMapMeta(MapName.c_str(), Meta);
				vUnfinRows.push_back({i, Meta, HasMeta});
			}

			if(s_KoGUnfinSortMode == 1)
			{
				std::stable_sort(vUnfinRows.begin(), vUnfinRows.end(), [&](const SUnfinRow &A, const SUnfinRow &B) {
					const int Pa = A.m_HasMeta ? A.m_Meta.m_Points : 0;
					const int Pb = B.m_HasMeta ? B.m_Meta.m_Points : 0;
					if(Pa != Pb)
						return Pa > Pb;
					return str_comp_nocase(Prof.m_vUnfinishedMaps[A.m_Idx].c_str(), Prof.m_vUnfinishedMaps[B.m_Idx].c_str()) < 0;
				});
			}
			else if(s_KoGUnfinSortMode == 2)
			{
				std::stable_sort(vUnfinRows.begin(), vUnfinRows.end(), [&](const SUnfinRow &A, const SUnfinRow &B) {
					const int Pa = (A.m_HasMeta && A.m_Meta.m_Points > 0) ? A.m_Meta.m_Points : 999999;
					const int Pb = (B.m_HasMeta && B.m_Meta.m_Points > 0) ? B.m_Meta.m_Points : 999999;
					if(Pa != Pb)
						return Pa < Pb;
					return str_comp_nocase(Prof.m_vUnfinishedMaps[A.m_Idx].c_str(), Prof.m_vUnfinishedMaps[B.m_Idx].c_str()) < 0;
				});
			}

			const float ItemH = 22.0f;
			const size_t Shown = vUnfinRows.size();

			for(const SUnfinRow &Row : vUnfinRows)
			{
				const size_t i = Row.m_Idx;
				const std::string &MapName = Prof.m_vUnfinishedMaps[i];
				const CMapMetadata &Meta = Row.m_Meta;
				const bool HasMeta = Row.m_HasMeta;

				CUIRect ItemRect;
				RightList.HSplitTop(ItemH, &ItemRect, &RightList);
				RightList.HSplitTop(2.0f, nullptr, &RightList);

				if(s_ScrollKoGUnfinished.AddRect(ItemRect))
				{
					ItemRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.2f), IGraphics::CORNER_ALL, 3.0f);
					ItemRect.Margin(3.0f, &ItemRect);

					// Category Badge (fixed width)
					CUIRect CatBadge;
					ItemRect.VSplitLeft(56.0f, &CatBadge, &ItemRect);
					ItemRect.VSplitLeft(4.0f, nullptr, &ItemRect);
					const char *pCatStr = (HasMeta && Meta.m_aCategory[0]) ? Meta.m_aCategory : "-";
					ColorRGBA CatCol = GetCategoryColor(pCatStr);
					ColorRGBA CatBgCol = ColorRGBA(CatCol.r * 0.35f, CatCol.g * 0.35f, CatCol.b * 0.35f, 0.6f);
					CatBadge.Draw(CatBgCol, IGraphics::CORNER_ALL, 2.0f);
					TextRender()->TextColor(CatCol.r, CatCol.g, CatCol.b, 1.0f);
					Ui()->DoLabel(&CatBadge, pCatStr, 10.0f, TEXTALIGN_MC);

					// Points Badge (ALWAYS reserved width so map names align)
					CUIRect PtsBadge;
					ItemRect.VSplitLeft(46.0f, &PtsBadge, &ItemRect);
					ItemRect.VSplitLeft(6.0f, nullptr, &ItemRect);
					if(HasMeta && Meta.m_Points > 0)
					{
						char aPtsBuf[16];
						str_format(aPtsBuf, sizeof(aPtsBuf), "%d pts", Meta.m_Points);
						TextRender()->TextColor(1.0f, 0.85f, 0.3f, 0.95f);
						Ui()->DoLabel(&PtsBadge, aPtsBuf, 10.0f, TEXTALIGN_MC);
					}
					else
					{
						TextRender()->TextColor(0.45f, 0.48f, 0.55f, 0.65f);
						Ui()->DoLabel(&PtsBadge, "-", 10.0f, TEXTALIGN_MC);
					}

					if(IsOnline)
					{
						bool DifferentDiff = (aCurCategory[0] != '\0' && HasMeta && Meta.m_aCategory[0] != '\0' && str_comp_nocase(Meta.m_aCategory, aCurCategory) != 0);

						CUIRect ActionCol;
						ItemRect.VSplitRight(54.0f, &ItemRect, &ActionCol);
						static std::vector<CButtonContainer> s_vBtnIndivVoteMap;
						if(s_vBtnIndivVoteMap.size() < Prof.m_vUnfinishedMaps.size())
							s_vBtnIndivVoteMap.resize(Prof.m_vUnfinishedMaps.size());
						CButtonContainer *pVoteBtn = &s_vBtnIndivVoteMap[i];
						if(DoColorBtn(pVoteBtn, TCLocalize("Votar"), &ActionCol, ColorRGBA(0.18f, 0.50f, 0.86f, 0.82f), IGraphics::CORNER_ALL, 3.0f, 10.0f))
						{
							VoteForMap(MapName.c_str());
						}

						if(DifferentDiff)
						{
							CUIRect DiffCol;
							ItemRect.VSplitRight(4.0f, &ItemRect, nullptr);
							ItemRect.VSplitRight(98.0f, &ItemRect, &DiffCol);
							static std::vector<CButtonContainer> s_vBtnIndivVoteDiff;
							if(s_vBtnIndivVoteDiff.size() < Prof.m_vUnfinishedMaps.size())
								s_vBtnIndivVoteDiff.resize(Prof.m_vUnfinishedMaps.size());
							CButtonContainer *pDiffBtn = &s_vBtnIndivVoteDiff[i];
							if(DoColorBtn(pDiffBtn, TCLocalize("Votar Dificultad"), &DiffCol, ColorRGBA(0.84f, 0.45f, 0.12f, 0.85f), IGraphics::CORNER_ALL, 3.0f, 10.0f))
							{
								VoteForCategory(Meta.m_aCategory);
							}
						}
						ItemRect.VSplitRight(6.0f, &ItemRect, nullptr);
					}

					TextRender()->TextColor(1.0f, 0.72f, 0.62f, 1.0f);
					Ui()->DoLabel(&ItemRect, MapName.c_str(), 11.0f, TEXTALIGN_ML);
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
				}
			}

			if(Shown == 0)
			{
				CUIRect EmptyLabel;
				RightList.HSplitTop(30.0f, &EmptyLabel, &RightList);
				if(s_ScrollKoGUnfinished.AddRect(EmptyLabel))
				{
					TextRender()->TextColor(0.6f, 0.6f, 0.6f, 0.8f);
					Ui()->DoLabel(&EmptyLabel, TCLocalize("Ningun mapa pendiente coincide con la busqueda"), 11.0f, TEXTALIGN_MC);
					TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
				}
			}

			s_ScrollKoGUnfinished.End();
		}

		return;
	}

	// ==========================================
	// DDNet Profile View
	// ==========================================
	EProfileState State = Profiles.State();

	if(State == EProfileState::IDLE)
	{
		CUIRect EmptyLabel;
		Content.HSplitTop(60.0f, nullptr, &EmptyLabel);
		TextRender()->TextColor(0.8f, 0.8f, 0.8f, 0.9f);
		Ui()->DoLabel(&EmptyLabel, TCLocalize("Ingresa un jugador y presiona 'Buscar' o consulta 'Mi Perfil'."), 14.0f, TEXTALIGN_MC);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}

	if(State == EProfileState::FETCHING)
	{
		CUIRect LoadingBox;
		Content.HSplitTop(80.0f, nullptr, &LoadingBox);
		LoadingBox.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 5.0f);
		TextRender()->TextColor(0.4f, 0.8f, 1.0f, 1.0f);
		Ui()->DoLabel(&LoadingBox, Profiles.StatusMsg(), 14.0f, TEXTALIGN_MC);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}

	if(State == EProfileState::NOT_FOUND || State == EProfileState::ERROR_NETWORK)
	{
		CUIRect ErrorBox;
		Content.HSplitTop(75.0f, nullptr, &ErrorBox);
		ErrorBox.Draw(ColorRGBA(0.3f, 0.1f, 0.1f, 0.35f), IGraphics::CORNER_ALL, 5.0f);
		TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
		Ui()->DoLabel(&ErrorBox, Profiles.StatusMsg(), 14.0f, TEXTALIGN_MC);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		return;
	}

	// State == READY: Render profile summary and split lists
	const CDDNetPlayerProfile &Prof = Profiles.CurrentProfile();

	// Summary Header Card
	CUIRect HeaderCard, CardLeft, CardMid, CardRight, Label;
	Content.HSplitTop(52.0f, &HeaderCard, &Content);
	HeaderCard.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.35f), IGraphics::CORNER_ALL, 5.0f);
	HeaderCard.Margin(5.0f, &HeaderCard);

	// Left: Player Name and Server Flag
	HeaderCard.VSplitLeft(180.0f, &CardLeft, &HeaderCard);
	char aNameTitle[128];
	if(Prof.m_aFavoriteServer[0] != '\0')
		str_format(aNameTitle, sizeof(aNameTitle), "%s [%s]", Prof.m_aName, Prof.m_aFavoriteServer);
	else
		str_copy(aNameTitle, Prof.m_aName);

	CardLeft.HSplitTop(22.0f, &Label, &CardLeft);
	TextRender()->TextColor(1.0f, 0.95f, 0.4f, 1.0f);
	Ui()->DoLabel(&Label, aNameTitle, 15.0f, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	// Mid: Points & Global Rank
	HeaderCard.VSplitLeft(HeaderCard.w * 0.55f, &CardMid, &CardRight);

	char aPointsStr[128];
	float Ratio = Prof.m_TotalPoints > 0 ? ((float)Prof.m_Points / (float)Prof.m_TotalPoints) * 100.0f : 0.0f;
	if(Prof.m_Rank > 0)
		str_format(aPointsStr, sizeof(aPointsStr), "%s: %d / %d (%.1f%%)  |  %s: #%d", TCLocalize("Puntos"), Prof.m_Points, Prof.m_TotalPoints, Ratio, TCLocalize("Rank"), Prof.m_Rank);
	else
		str_format(aPointsStr, sizeof(aPointsStr), "%s: %d / %d (%.1f%%)", TCLocalize("Puntos"), Prof.m_Points, Prof.m_TotalPoints, Ratio);

	CardMid.HSplitTop(18.0f, &Label, &CardMid);
	TextRender()->TextColor(0.5f, 1.0f, 0.5f, 1.0f);
	Ui()->DoLabel(&Label, aPointsStr, 11.5f, TEXTALIGN_ML);

	// Progress bar for points
	CUIRect BarBg, BarFill;
	CardMid.HSplitTop(7.0f, &BarBg, &CardMid);
	BarBg.Draw(ColorRGBA(0.1f, 0.1f, 0.1f, 0.7f), IGraphics::CORNER_ALL, 2.0f);
	float FillW = std::clamp(Ratio / 100.0f, 0.0f, 1.0f) * BarBg.w;
	if(FillW >= 1.0f)
	{
		BarBg.VSplitLeft(FillW, &BarFill, nullptr);
		BarFill.Draw(ColorRGBA(0.2f, 0.75f, 0.3f, 0.85f), IGraphics::CORNER_ALL, 2.0f);
	}
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	// Right: Team Rank & Points
	char aTeamStr[128];
	if(Prof.m_TeamRank > 0)
		str_format(aTeamStr, sizeof(aTeamStr), "%s: %d pts  |  %s: #%d", TCLocalize("Equipo"), Prof.m_TeamPoints, TCLocalize("Rank Eq"), Prof.m_TeamRank);
	else
		str_format(aTeamStr, sizeof(aTeamStr), "%s: %d pts", TCLocalize("Equipo"), Prof.m_TeamPoints);

	CardRight.HSplitTop(18.0f, &Label, &CardRight);
	TextRender()->TextColor(0.4f, 0.8f, 1.0f, 1.0f);
	Ui()->DoLabel(&Label, aTeamStr, 11.5f, TEXTALIGN_ML);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	Content.HSplitTop(Margin, nullptr, &Content);

	// Split View: Left = Categories, Right = Recent Finishes
	CUIRect LeftCol, RightCol;
	Content.VSplitMid(&LeftCol, &RightCol, Margin);

	// --- Left Column: Categories ---
	CUIRect CatHeader, CatList;
	LeftCol.HSplitTop(20.0f, &CatHeader, &CatList);
	CatHeader.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 3.0f);
	TextRender()->TextColor(1.0f, 0.85f, 0.3f, 1.0f);
	Ui()->DoLabel(&CatHeader, TCLocalize("Categorias y Dificultades"), 12.0f, TEXTALIGN_MC);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	CatList.Margin(2.0f, &CatList);
	static CScrollRegion s_ScrollCategories;
	CScrollRegionParams CatScrollParams;
	CatScrollParams.m_ScrollUnit = 40.0f;
	CatScrollParams.m_ForceShowScrollbar = false;
	CatScrollParams.m_ScrollbarMargin = 3.0f;
	s_ScrollCategories.Begin(&CatList, &CatScrollParams);

	const float ItemH = 24.0f;
	if(Prof.m_vCategories.empty())
	{
		CUIRect EmptyLabel;
		CatList.HSplitTop(30.0f, &EmptyLabel, &CatList);
		if(s_ScrollCategories.AddRect(EmptyLabel))
		{
			TextRender()->TextColor(0.6f, 0.6f, 0.6f, 0.8f);
			Ui()->DoLabel(&EmptyLabel, TCLocalize("Sin categorias registradas"), 11.0f, TEXTALIGN_MC);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
	}
	else
	{
		for(size_t i = 0; i < Prof.m_vCategories.size(); ++i)
		{
			const CDDNetCategoryStats &Cat = Prof.m_vCategories[i];
			CUIRect ItemRect;
			CatList.HSplitTop(ItemH, &ItemRect, &CatList);
			CatList.HSplitTop(2.0f, nullptr, &CatList);

			if(s_ScrollCategories.AddRect(ItemRect))
			{
				ItemRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.2f), IGraphics::CORNER_ALL, 3.0f);
				ItemRect.Margin(3.0f, &ItemRect);

				CUIRect NameCol, StatsCol;
				ItemRect.VSplitLeft(110.0f, &NameCol, &StatsCol);

				ColorRGBA CatColor = ColorRGBA(0.9f, 0.9f, 0.9f, 1.0f);
				if(str_comp_nocase(Cat.m_aName, "Novice") == 0) CatColor = ColorRGBA(0.4f, 0.9f, 0.4f, 1.0f);
				else if(str_comp_nocase(Cat.m_aName, "Moderate") == 0) CatColor = ColorRGBA(0.9f, 0.9f, 0.3f, 1.0f);
				else if(str_comp_nocase(Cat.m_aName, "Brutal") == 0) CatColor = ColorRGBA(0.95f, 0.4f, 0.2f, 1.0f);
				else if(str_comp_nocase(Cat.m_aName, "Insane") == 0) CatColor = ColorRGBA(0.95f, 0.2f, 0.2f, 1.0f);
				else if(str_comp_nocase(Cat.m_aName, "Solo") == 0) CatColor = ColorRGBA(0.4f, 0.7f, 1.0f, 1.0f);

				TextRender()->TextColor(CatColor);
				Ui()->DoLabel(&NameCol, Cat.m_aName, 11.0f, TEXTALIGN_ML);

				char aCatStats[64];
				if(Cat.m_Rank > 0)
					str_format(aCatStats, sizeof(aCatStats), "%d / %d pts (#%d)", Cat.m_Points, Cat.m_TotalPoints, Cat.m_Rank);
				else
					str_format(aCatStats, sizeof(aCatStats), "%d / %d pts", Cat.m_Points, Cat.m_TotalPoints);

				TextRender()->TextColor(0.85f, 0.85f, 0.85f, 1.0f);
				Ui()->DoLabel(&StatsCol, aCatStats, 10.0f, TEXTALIGN_MR);
				TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			}
		}
	}
	s_ScrollCategories.End();

	// --- Right Column: Recent Finishes ---
	CUIRect FinHeader, FinList;
	RightCol.HSplitTop(20.0f, &FinHeader, &FinList);
	FinHeader.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 3.0f);
	TextRender()->TextColor(0.4f, 0.8f, 1.0f, 1.0f);
	Ui()->DoLabel(&FinHeader, TCLocalize("Ultimos Finishes"), 12.0f, TEXTALIGN_MC);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);

	FinList.Margin(2.0f, &FinList);
	static CScrollRegion s_ScrollFinishes;
	CScrollRegionParams FinScrollParams;
	FinScrollParams.m_ScrollUnit = 40.0f;
	FinScrollParams.m_ForceShowScrollbar = false;
	FinScrollParams.m_ScrollbarMargin = 3.0f;
	s_ScrollFinishes.Begin(&FinList, &FinScrollParams);

	if(Prof.m_vLastFinishes.empty())
	{
		CUIRect EmptyLabel;
		FinList.HSplitTop(30.0f, &EmptyLabel, &FinList);
		if(s_ScrollFinishes.AddRect(EmptyLabel))
		{
			TextRender()->TextColor(0.6f, 0.6f, 0.6f, 0.8f);
			Ui()->DoLabel(&EmptyLabel, TCLocalize("Sin records recientes"), 11.0f, TEXTALIGN_MC);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
	}
	else
	{
		for(size_t i = 0; i < Prof.m_vLastFinishes.size(); ++i)
		{
			const CDDNetFinish &F = Prof.m_vLastFinishes[i];
			CUIRect ItemRect;
			FinList.HSplitTop(ItemH, &ItemRect, &FinList);
			FinList.HSplitTop(2.0f, nullptr, &FinList);

			if(s_ScrollFinishes.AddRect(ItemRect))
			{
				ItemRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.2f), IGraphics::CORNER_ALL, 3.0f);
				ItemRect.Margin(3.0f, &ItemRect);

				CUIRect MapNameCol, TypeCol, TimeCol;
				ItemRect.VSplitLeft(ItemRect.w * 0.45f, &MapNameCol, &TypeCol);
				TypeCol.VSplitLeft(ItemRect.w * 0.25f, &TypeCol, &TimeCol);

				TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
				Ui()->DoLabel(&MapNameCol, F.m_aMap, 11.0f, TEXTALIGN_ML);

				TextRender()->TextColor(0.7f, 0.7f, 0.9f, 0.9f);
				Ui()->DoLabel(&TypeCol, F.m_aType, 10.0f, TEXTALIGN_MC);

				char aTimeBuf[32];
				int Minutes = (int)(F.m_Time / 60.0f);
				float Seconds = F.m_Time - (Minutes * 60);
				if(Minutes > 0)
					str_format(aTimeBuf, sizeof(aTimeBuf), "%02d:%05.2f", Minutes, Seconds);
				else
					str_format(aTimeBuf, sizeof(aTimeBuf), "%05.2f", Seconds);

				TextRender()->TextColor(0.4f, 1.0f, 0.4f, 1.0f);
				Ui()->DoLabel(&TimeCol, aTimeBuf, 11.0f, TEXTALIGN_MR);
				TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
			}
		}
	}
	s_ScrollFinishes.End();
}

float CMenus::RenderPlayerConfigsPanel(CUIRect Area, bool IsModal, bool *pCloseRequested)
{
	const float StartY = Area.y;
	CUIRect Label;
	Area.HSplitTop(HeadlineHeight, &Label, &Area);
	CUIRect ConfigTitleRect, WebConfigsBtnRect, DiscordActionRect;
	Label.VSplitRight(205.0f, &ConfigTitleRect, &DiscordActionRect);
	ConfigTitleRect.VSplitRight(8.0f, &ConfigTitleRect, nullptr);
	ConfigTitleRect.VSplitRight(155.0f, &ConfigTitleRect, &WebConfigsBtnRect);
	ConfigTitleRect.VSplitRight(8.0f, &ConfigTitleRect, nullptr);
	Ui()->DoLabel(&ConfigTitleRect, TCLocalize("Configuraciones de jugadores"), HeadlineFontSize, TEXTALIGN_ML);

	static CButtonContainer s_BtnOpenWebConfigs;
	if(DoButton_Menu(&s_BtnOpenWebConfigs, TCLocalize("Ver configuraciones"), 0, &WebConfigsBtnRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.42f, 0.25f, 0.85f, 0.85f)))
	{
		Client()->ViewLink("https://agusclient.github.io/configs-players/");
	}
	GameClient()->m_Tooltips.DoToolTip(&s_BtnOpenWebConfigs, &WebConfigsBtnRect, "https://agusclient.github.io/configs-players/");

	auto &ConfigSync = GameClient()->m_AClientConfigSync;
	static CButtonContainer s_BtnDiscordStart;
	static CButtonContainer s_BtnDiscordCancel;
	static CButtonContainer s_BtnDiscordLogout;
	if(ConfigSync.HasDiscordSession())
	{
		CUIRect DiscUserRect, DiscOutBtn;
		DiscordActionRect.VSplitRight(52.0f, &DiscUserRect, &DiscOutBtn);
		DiscUserRect.VSplitRight(4.0f, &DiscUserRect, nullptr);
		const char *pDiscName = ConfigSync.GetDiscordUsername()[0] != '\0' ? ConfigSync.GetDiscordUsername() : (ConfigSync.GetDiscordHandle()[0] != '\0' ? ConfigSync.GetDiscordHandle() : "Conectado");
		char aDiscBadge[96];
		str_format(aDiscBadge, sizeof(aDiscBadge), "Discord: %s", pDiscName);
		TextRender()->TextColor(0.55f, 0.88f, 1.0f, 1.0f);
		Ui()->DoLabel(&DiscUserRect, aDiscBadge, FontSize * 0.88f, TEXTALIGN_MR);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		if(DoButton_Menu(&s_BtnDiscordLogout, TCLocalize("Salir"), 0, &DiscOutBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, ColorRGBA(0.35f, 0.22f, 0.25f, 0.75f)))
		{
			ConfigSync.LogoutDiscord();
		}
	}
	else if(ConfigSync.IsDiscordBusy())
	{
		if(DoButton_Menu(&s_BtnDiscordCancel, TCLocalize("Cancelar Discord..."), 0, &DiscordActionRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.45f, 0.38f, 0.20f, 0.80f)))
		{
			ConfigSync.CancelDiscordLogin();
		}
	}
	else
	{
		if(DoButton_Menu(&s_BtnDiscordStart, TCLocalize("Iniciar sesion con Discord"), 0, &DiscordActionRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.345f, 0.396f, 0.949f, 0.85f)))
		{
			ConfigSync.StartDiscordLogin();
		}
	}

	Area.HSplitTop(MarginSmall, nullptr, &Area);

	if(ConfigSync.GetDiscordStatusMessage()[0] != '\0' && ConfigSync.DiscordState() != CAClientConfigSync::DISCORD_READY)
	{
		CUIRect DiscMsgRect;
		Area.HSplitTop(LineSize * 0.95f, &DiscMsgRect, &Area);
		if(ConfigSync.DiscordState() == CAClientConfigSync::DISCORD_ERROR)
			TextRender()->TextColor(0.95f, 0.35f, 0.35f, 1.0f);
		else
			TextRender()->TextColor(0.65f, 0.75f, 1.0f, 1.0f);
		Ui()->DoLabel(&DiscMsgRect, ConfigSync.GetDiscordStatusMessage(), FontSize * 0.86f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		Area.HSplitTop(MarginExtraSmall, nullptr, &Area);
	}

	char aPlayerName[MAX_NAME_LENGTH];
	char aResolution[32];
	int MouseMaxDistance = 0;
	int MouseSens = 200;
	int Hz = 0;
	ConfigSync.GetCurrentSnapshot(aPlayerName, sizeof(aPlayerName), MouseMaxDistance, MouseSens, aResolution, sizeof(aResolution), Hz);

	CUIRect PreviewLine, HintLine, InputRow;
	Area.HSplitTop(LineSize, &PreviewLine, &Area);
	char aPreview[256];
	str_format(aPreview, sizeof(aPreview), "Jugador: %s  |  Sens: %d  |  Max Dist: %d  |  Res: %s  |  %d Hz",
		aPlayerName[0] != '\0' ? aPlayerName : "(sin nombre)",
		MouseSens,
		MouseMaxDistance,
		aResolution,
		Hz);
	TextRender()->TextColor(aPlayerName[0] != '\0' ? ColorRGBA(0.85f, 0.90f, 0.98f, 1.0f) : ColorRGBA(0.95f, 0.65f, 0.25f, 1.0f));
	Ui()->DoLabel(&PreviewLine, aPreview, FontSize * 0.92f, TEXTALIGN_ML);
	TextRender()->TextColor(TextRender()->DefaultTextColor());

	Area.HSplitTop(LineSize * 0.95f, &HintLine, &Area);
	if(ConfigSync.HasDiscordSession())
	{
		TextRender()->TextColor(0.68f, 0.72f, 0.80f, 0.95f);
		Ui()->DoLabel(&HintLine, TCLocalize("Vinculado a tu cuenta de Discord (sin contrasena). Ingresa tu DPI y guarda o actualiza."), FontSize * 0.85f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());

		Area.HSplitTop(MarginExtraSmall, nullptr, &Area);
		Area.HSplitTop(LineSize * 1.25f, &InputRow, &Area);

		CUIRect DpiLabelRect, DpiInputRect, UploadBtnRect;
		InputRow.VSplitLeft(84.0f, &DpiLabelRect, &InputRow);
		InputRow.VSplitLeft(110.0f, &DpiInputRect, &InputRow);
		InputRow.VSplitRight(175.0f, &InputRow, &UploadBtnRect);

		Ui()->DoLabel(&DpiLabelRect, TCLocalize("DPI Mouse:"), FontSize, TEXTALIGN_ML);
		static CLineInputBuffered<8> s_ConfigSyncDpi;
		s_ConfigSyncDpi.SetEmptyText("Ej: 800");
		Ui()->DoEditBox(&s_ConfigSyncDpi, &DpiInputRect, EditBoxFontSize);

		static CButtonContainer s_BtnUploadConfig;
		const bool Uploading = ConfigSync.IsUploading();
		const ColorRGBA UploadColor = Uploading ? ColorRGBA(0.35f, 0.35f, 0.42f, 0.65f) : ColorRGBA(0.18f, 0.52f, 0.88f, 0.80f);
		if(DoButton_Menu(&s_BtnUploadConfig, Uploading ? TCLocalize("Enviando...") : TCLocalize("Guardar / Actualizar"), 0, &UploadBtnRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 4.0f, 0.0f, UploadColor) && !Uploading)
		{
			const int ParsedDpi = str_toint(s_ConfigSyncDpi.GetString());
			ConfigSync.UploadCurrentConfig(ParsedDpi);
		}
	}
	else
	{
		TextRender()->TextColor(0.95f, 0.75f, 0.30f, 1.0f);
		Ui()->DoLabel(&HintLine, TCLocalize("Inicia sesion con Discord arriba para habilitar el guardado y actualizacion de tu configuracion."), FontSize * 0.88f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	if(ConfigSync.State() != CAClientConfigSync::UPLOAD_IDLE && ConfigSync.GetStatusMessage()[0] != '\0')
	{
		Area.HSplitTop(MarginExtraSmall, nullptr, &Area);
		CUIRect SyncStatusLine;
		Area.HSplitTop(LineSize, &SyncStatusLine, &Area);
		if(ConfigSync.State() == CAClientConfigSync::UPLOAD_SENDING)
			TextRender()->TextColor(0.95f, 0.85f, 0.30f, 1.0f);
		else if(ConfigSync.State() == CAClientConfigSync::UPLOAD_SUCCESS)
			TextRender()->TextColor(0.35f, 0.95f, 0.45f, 1.0f);
		else
			TextRender()->TextColor(0.95f, 0.35f, 0.35f, 1.0f);
		Ui()->DoLabel(&SyncStatusLine, ConfigSync.GetStatusMessage(), FontSize * 0.90f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	if(IsModal && pCloseRequested != nullptr)
	{
		Area.HSplitTop(Margin, nullptr, &Area);
		CUIRect FooterRow, EscHintRect, CloseBtnRect;
		Area.HSplitTop(24.0f, &FooterRow, &Area);
		FooterRow.VSplitRight(110.0f, &EscHintRect, &CloseBtnRect);
		TextRender()->TextColor(0.60f, 0.65f, 0.75f, 0.85f);
		Ui()->DoLabel(&EscHintRect, TCLocalize("Pulsa ESC para cerrar"), FontSize * 0.82f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
		static CButtonContainer s_BtnCloseModal;
		if(DoButton_Menu(&s_BtnCloseModal, TCLocalize("Cerrar"), 0, &CloseBtnRect, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.24f, 0.26f, 0.34f, 0.85f)))
		{
			*pCloseRequested = true;
		}
	}

	return Area.y - StartY;
}

