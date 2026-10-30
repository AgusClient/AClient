#include <base/log.h>
#include <base/math.h>
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
#include <string>
#include <unordered_map>
#include <vector>

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
		Section.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
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

	// ***** Input ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxes.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Input"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInput, TCLocalize("Fast Input (reduced visual delay)"), &g_Config.m_TcFastInput, &Column, LineSize);

	Column.HSplitTop(LineSize, &Button, &Column);
	DoSliderWithScaledValue(&g_Config.m_TcFastInputAmount, &g_Config.m_TcFastInputAmount, &Button, TCLocalize("Amount"), 1, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	if(g_Config.m_TcFastInput)
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInputOthers, TCLocalize("Fast Input others"), &g_Config.m_TcFastInputOthers, &Column, LineSize);
		Column.HSplitTop(LineSize, &Button, &Column);
		if(g_Config.m_TcFastInputOthers)
			DoSliderWithScaledValue(&g_Config.m_TcFastInputOthersAmount, &g_Config.m_TcFastInputOthersAmount, &Button, TCLocalize("Others Amount"), 1, 40, 1, &CUi::ms_LinearScrollbarScale, CUi::SCROLLBAR_OPTION_NOCLAMPVALUE, "ms");
	}
	else
		Column.HSplitTop(LineSize, nullptr, &Column);

	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSubTickAiming, TCLocalize("Sub-Tick aiming"), &g_Config.m_ClSubTickAiming, &Column, LineSize);
	if(!g_Config.m_ClSubTickAiming)
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_TcFastInputRepredictHook, TCLocalize("Repredict on mid-tick hook direction change (may lag)"), &g_Config.m_TcFastInputRepredictHook, &Column, LineSize);

	// A little extra spacing because these are multi line
	Column.HSplitTop(MarginSmall, nullptr, &Column);

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
		Section.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
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

		CUIRect TopLine, BottomLine, MapRect, TimeRect;
		ItemRect.Margin(4.0f, &ItemRect);
		ItemRect.HSplitMid(&TopLine, &BottomLine);
		TopLine.VSplitRight(95.0f, &MapRect, &TimeRect);

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

	CUIRect MetaMap, MetaRunTime;
	MetaTop.VSplitRight(160.0f, &MetaMap, &MetaRunTime);

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
}

void CMenus::RenderSettingsAClient(CUIRect MainView)
{
	CUIRect TabBar, Button;
	MainView.HSplitTop(LineSize, &TabBar, &MainView);
	const float TabWidth = TabBar.w / 5.0f;
	static CButtonContainer s_aAClientTabs[5] = {};
	const char *apTabNames[5] = {
		TCLocalize("Settings"),
		TCLocalize("Race Stats"),
		TCLocalize("Perfiles"),
		TCLocalize("Commands"),
		TCLocalize("Mod")};

	for(int Tab = 0; Tab < 5; ++Tab)
	{
		TabBar.VSplitLeft(TabWidth, &Button, &TabBar);
		const int Corners = Tab == 0 ? IGraphics::CORNER_L : (Tab == 4 ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);
		if(DoButton_MenuTab(&s_aAClientTabs[Tab], apTabNames[Tab], m_CurAClientTab == Tab, &Button, Corners, nullptr, nullptr, nullptr, nullptr, 4.0f))
			m_CurAClientTab = Tab;
	}

	MainView.HSplitTop(Margin, nullptr, &MainView);

	if(m_CurAClientTab == ACLIENT_TAB_SETTINGS)
		RenderSettingsAClientSettings(MainView);
	else if(m_CurAClientTab == ACLIENT_TAB_RACESTATS)
		RenderSettingsTClientRaceStats(MainView);
	else if(m_CurAClientTab == ACLIENT_TAB_PROFILES)
		RenderSettingsAClientProfiles(MainView);
	else if(m_CurAClientTab == ACLIENT_TAB_COMMANDS)
		RenderSettingsAClientCommands(MainView);
	else
		RenderSettingsAClientMod(MainView);
}

void CMenus::RenderSettingsAClientCommands(CUIRect MainView)
{
	CUIRect Column, LeftView, RightView, Label, Row;

	static CScrollRegion s_ScrollRegionCommands;
	CScrollRegionParams ScrollParams;
	ScrollParams.m_ScrollUnit = 60.0f;
	ScrollParams.m_ForceShowScrollbar = true;
	ScrollParams.m_ScrollbarMargin = 5.0f;
	s_ScrollRegionCommands.Begin(&MainView, &ScrollParams);

	static std::vector<CUIRect> s_SectionBoxesCmds;
	const float ScrollOffset = MainView.y;
	static float s_PrevScrollOffsetCmds = 0.0f;

	MainView.VSplitRight(5.0f, &MainView, nullptr);
	MainView.VSplitLeft(5.0f, nullptr, &MainView);

	MainView.VSplitMid(&LeftView, &RightView, MarginBetweenViews);
	LeftView.VSplitLeft(MarginSmall, nullptr, &LeftView);
	RightView.VSplitRight(MarginSmall, &RightView, nullptr);

	for(CUIRect &Section : s_SectionBoxesCmds)
	{
		float Padding = MarginBetweenViews * 0.6666f;
		Section.w += Padding;
		Section.h += Padding;
		Section.x -= Padding * 0.5f;
		Section.y -= Padding * 0.5f;
		Section.y -= s_PrevScrollOffsetCmds - ScrollOffset;
		Section.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
	}
	s_PrevScrollOffsetCmds = ScrollOffset;
	s_SectionBoxesCmds.clear();

	auto DrawCommandCard = [&](CUIRect &Col, const char *pCmd, const char *pDesc) {
		Col.HSplitTop(44.0f, &Row, &Col);
		if(s_ScrollRegionCommands.AddRect(Row))
		{
			Row.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.28f), IGraphics::CORNER_ALL, 6.0f);
			Row.Margin(5.0f, &Row);
			CUIRect TopLine, BottomLine;
			Row.HSplitMid(&TopLine, &BottomLine);
			TextRender()->TextColor(1.0f, 0.88f, 0.35f, 1.0f);
			Ui()->DoLabel(&TopLine, pCmd, 13.0f, TEXTALIGN_ML);
			TextRender()->TextColor(0.85f, 0.88f, 0.94f, 0.95f);
			Ui()->DoLabel(&BottomLine, pDesc, 11.0f, TEXTALIGN_ML);
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
		}
		Col.HSplitTop(MarginSmall, nullptr, &Col);
	};

	// ==================== COLUMNA IZQUIERDA ====================
	// 1. General & Chat
	Column = LeftView;
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxesCmds.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("General & Chat"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DrawCommandCard(Column, "ac_save_config", TCLocalize("Save current AClient configuration to settings_aclient.cfg."));
	DrawCommandCard(Column, "ac_show_chat_client [0|1]", TCLocalize("Toggle showing local client echo messages in chat."));
	DrawCommandCard(Column, "ac_silent_typing [0|1]", TCLocalize("Hide your own typing chat bubble (Silent Typing)."));
	DrawCommandCard(Column, "ac_hide_others_chat_bubbles [0|1]", TCLocalize("Hide typing chat bubbles of other players on your screen."));
	DrawCommandCard(Column, "ac_auto_team_lock [0|1]", TCLocalize("Automatically lock team (/lock 1) after joining a team."));
	DrawCommandCard(Column, "ac_auto_team_lock_delay [sec]", TCLocalize("Delay in seconds before sending /lock 1 after joining."));
	DrawCommandCard(Column, "/invite friends", TCLocalize("Invites only connected players on your friends list to your team."));
	s_SectionBoxesCmds.back().h = Column.y - s_SectionBoxesCmds.back().y;

	// 2. RaceStats & HUD
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxesCmds.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("RaceStats & Mini Statboard"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DrawCommandCard(Column, "toggle ac_mini_statboard 0 1", TCLocalize("Toggles the mini statboard HUD overlay visibility."));
	DrawCommandCard(Column, "ac_save_racestats", TCLocalize("Manually saves current run stats to history log."));
	DrawCommandCard(Column, "ac_clear_racestats_history", TCLocalize("Clears all saved RaceStats history."));
	DrawCommandCard(Column, "ac_mini_statboard_sort [0-8]", TCLocalize("Sort stat: 0=Default, 1=Saves, 2=Deaths, 3=Freezes, 4=Eff, 5=Net, 6=Time, 7=Hook, 8=Lasts."));
	DrawCommandCard(Column, "ac_mini_statboard_anim [0|1]", TCLocalize("Toggle smooth F1-style ladder animation when ranks change."));
	DrawCommandCard(Column, "ac_mini_statboard_anim_speed [3-30]", TCLocalize("Speed of F1 ladder rank transitions (default: 10)."));
	DrawCommandCard(Column, "ac_mini_statboard_show_tees [0|1]", TCLocalize("Show mini Tee skins next to names in overlay (default: 1)."));
	DrawCommandCard(Column, "ac_statboard_keep_on_death [0|1]", TCLocalize("Keep accumulated stats on death/kill (do not reset)."));
	DrawCommandCard(Column, "ac_statboard_reset_on_team_kill [0|1]", TCLocalize("Reset stats when all team dies or you switch team."));
	DrawCommandCard(Column, "ac_statboard_auto_save [0|1]", TCLocalize("Auto-save stats before resetting on kill or team switch."));
	DrawCommandCard(Column, "ac_mini_statboard_columns [1-255]", TCLocalize("Bitmask of visible columns in the mini statboard HUD."));
	DrawCommandCard(Column, "ac_mini_statboard_scale [20-200]", TCLocalize("HUD scale percentage for the mini statboard overlay."));
	DrawCommandCard(Column, "ac_mini_statboard_opacity [0-100]", TCLocalize("Background opacity percentage for the mini statboard."));
	DrawCommandCard(Column, "ac_mini_statboard_x / y [0-100]", TCLocalize("Screen position (percent) of the mini statboard overlay."));
	s_SectionBoxesCmds.back().h = Column.y - s_SectionBoxesCmds.back().y;

	LeftView = Column; // Guarda la posición final de la columna izquierda

	// ==================== COLUMNA DERECHA ====================
	// 3. Fast Input & Predicción
	Column = RightView;
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxesCmds.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Fast Input & Prediction"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DrawCommandCard(Column, "ac_inputs [0-8]", TCLocalize("Prediction mode: 0=Off, 1=Fast, 2=Best, 3=Saiko, 4=Delta, 5=F, 6=Cloud, 7=AClient, 8=aX30."));
	DrawCommandCard(Column, "cl_prediction_margin [-50 to 50]", TCLocalize("Prediction margin in ms (Anti-lag tool)."));
	DrawCommandCard(Column, "ac_ax30_input_amount / others", TCLocalize("Prediction offset in 0.01 ticks for aX30 mode."));
	DrawCommandCard(Column, "ac_ax30_adaptive_jitter [0|1]", TCLocalize("Real-time ping jitter tracking & dynamic ping compensation for aX30."));
	DrawCommandCard(Column, "ac_ax30_collision_clamp [0|1]", TCLocalize("Collision & freeze boundary clamping to prevent rubberband snap."));
	DrawCommandCard(Column, "ac_aclient_input_amount / others", TCLocalize("Prediction offset in 0.01 ticks for AClient mode."));
	DrawCommandCard(Column, "ac_aclient_input_latency_comp [0-50]", TCLocalize("Latency compensation percentage for AClient mode."));
	DrawCommandCard(Column, "ac_fast_input_amount / others", TCLocalize("Prediction offset in ms for Fast mode (self and others)."));
	DrawCommandCard(Column, "ac_best_input_amount / others", TCLocalize("Prediction offset in 0.01 ticks for Best mode."));
	DrawCommandCard(Column, "ac_best_input_smoothing [0-100]", TCLocalize("Input smoothing amount percentage for Best mode."));
	DrawCommandCard(Column, "ac_best_input_latency_comp [0-50]", TCLocalize("Latency compensation percentage for Best mode."));
	DrawCommandCard(Column, "ac_saiko_input_amount / others", TCLocalize("Prediction offset in 0.01 ticks for Saiko mode."));
	DrawCommandCard(Column, "ac_delta_input_amount / others", TCLocalize("Prediction offset in 0.01 ticks for Delta mode."));
	DrawCommandCard(Column, "ac_f_input_amount / others", TCLocalize("Prediction offset in 0.001 ticks for F mode."));
	DrawCommandCard(Column, "ac_cloud_input_amount / others", TCLocalize("Prediction offset in 0.01 ticks for Cloud mode."));
	DrawCommandCard(Column, "ac_fast_input_repredict_hook [0|1]", TCLocalize("Repredict hook trajectory when direction changes."));
	DrawCommandCard(Column, "ac_fast_input_others_only_frozen [0|1]", TCLocalize("Only apply fast input to other tees when they are frozen."));
	s_SectionBoxesCmds.back().h = Column.y - s_SectionBoxesCmds.back().y;

	// 4. Background Alerts (AFK)
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxesCmds.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Background Alerts (AFK)"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DrawCommandCard(Column, "ac_notify_unfreeze [0|1]", TCLocalize("Alert when unfreezing while tabbed out / minimized."));
	DrawCommandCard(Column, "ac_change_tile_notification [0|1]", TCLocalize("Alert when moved or tile changes while tabbed out."));
	DrawCommandCard(Column, "ac_notify_sound / flash / desktop", TCLocalize("Independent alert channels (Sound, Taskbar flash, Toast)."));
	s_SectionBoxesCmds.back().h = Column.y - s_SectionBoxesCmds.back().y;

	// 5. Fast Practice (Ghost Mode)
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
	s_SectionBoxesCmds.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Fast Practice / Ghost Mode"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DrawCommandCard(Column, "ac_fast_practice_toggle", TCLocalize("Enables or disables ghost practice mode without leaving your run."));
	DrawCommandCard(Column, "ac_fast_practice_ghost_alpha [0-100]", TCLocalize("Ghost tee transparency percentage in Fast Practice."));
	DrawCommandCard(Column, "ac_fast_practice_spec_original [0|1]", TCLocalize("Put original tee in spectator mode (/spec) when practicing."));
	DrawCommandCard(Column, "ac_fast_practice_show_text [0|1]", TCLocalize("Show top help text (/tp /tc /invincible) in practice."));
	DrawCommandCard(Column, "/tp & /tc", TCLocalize("Teleports to cursor (/tp) or toggles telecursor aim (/tc)."));
	DrawCommandCard(Column, "/invincible & /weapons", TCLocalize("Local freeze invincibility and all weapons in practice."));
	DrawCommandCard(Column, "/save & /load", TCLocalize("Saves and loads local practice positions to repeat map parts."));
	s_SectionBoxesCmds.back().h = Column.y - s_SectionBoxesCmds.back().y;

	RightView = Column; // Guarda la posición final de la columna derecha

	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = std::max(LeftView.y, RightView.y) + MarginSmall * 2.0f;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = 0.0f;
	s_ScrollRegionCommands.AddRect(ScrollRegion);
	s_ScrollRegionCommands.End();
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
		Section.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
	}
	s_PrevScrollOffsetAc = ScrollOffset;
	s_SectionBoxesAc.clear();

	// ***** Left Column: Gameplay, Network & Input ***** //
	Column = LeftView;

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
	Column.HSplitTop(LineSize + 4.0f, &Button, &Column);
	static CButtonContainer s_BtnToggleAntiPingNow;
	char aAntiPingBtn[64];
	str_format(aAntiPingBtn, sizeof(aAntiPingBtn), "%s: %s", TCLocalize("Toggle Anti-Ping Now"), g_Config.m_ClAntiPing ? "ON" : "OFF");
	if(DoButton_Menu(&s_BtnToggleAntiPingNow, aAntiPingBtn, g_Config.m_ClAntiPing ? 1 : 0, &Button))
	{
		Console()->ExecuteLine("toggle_antiping", IConsole::CLIENT_ID_UNSPECIFIED);
	}

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

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(LineSize, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Fast Input Mode:"), Label.h * CUi::ms_FontmodHeight * 0.8f, TEXTALIGN_ML);

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(LineSize, &Button, &Column);
	{
		static CButtonContainer s_aInputButtons[9];
		static const char *s_apInputNames[] = {"Off", "AClient", "aX30", "Fast", "Best", "Saiko", "Delta", "F", "Cloud"};
		static const int s_aInputValues[] = {AC_INPUTS_OFF, AC_INPUTS_ACLIENT, AC_INPUTS_AX30, AC_INPUTS_FAST, AC_INPUTS_BEST, AC_INPUTS_SAIKO, AC_INPUTS_DELTA, AC_INPUTS_F, AC_INPUTS_CLOUD};

		CUIRect ButtonsRect = Button;
		const float Spacing = 2.0f;
		const float ModeButtonWidth = (ButtonsRect.w - Spacing * 8.0f) / 9.0f;
		for(int i = 0; i < 9; ++i)
		{
			CUIRect ModeButton;
			if(i < 8)
			{
				ButtonsRect.VSplitLeft(ModeButtonWidth, &ModeButton, &ButtonsRect);
				ButtonsRect.VSplitLeft(Spacing, nullptr, &ButtonsRect);
			}
			else
				ModeButton = ButtonsRect;

			int Corners = (i == 0) ? IGraphics::CORNER_L : ((i == 8) ? IGraphics::CORNER_R : IGraphics::CORNER_NONE);

			if(DoButton_Menu(&s_aInputButtons[i], TCLocalize(s_apInputNames[i]), g_Config.m_AcInputs == s_aInputValues[i], &ModeButton, BUTTONFLAG_LEFT, nullptr, Corners))
			{
				g_Config.m_AcInputs = s_aInputValues[i];
				g_Config.m_TcFastInput = (g_Config.m_AcInputs != AC_INPUTS_OFF) ? 1 : 0;
			}
		}
	}

	if(g_Config.m_AcInputs == AC_INPUTS_AX30)
	{
		Column.HSplitTop(MarginSmall, nullptr, &Column);
		Column.HSplitTop(LineSize, &Button, &Column);
		DoTickAmountSlider(&g_Config.m_AcAX30InputAmount, &Button, TCLocalize("Prediction offset"), 0, 1000);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcAX30AdaptiveJitter, TCLocalize("Adaptive ping jitter tracking (Auto anti-lag)"), &g_Config.m_AcAX30AdaptiveJitter, &Column, LineSize);

		Column.HSplitTop(MarginSmall, nullptr, &Column);
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcAX30CollisionClamp, TCLocalize("Collision & freeze boundary clamping (Anti-snap)"), &g_Config.m_AcAX30CollisionClamp, &Column, LineSize);

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
	DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_ClSubTickAiming, TCLocalize("Sub-Tick aiming"), &g_Config.m_ClSubTickAiming, &Column, LineSize);
	if(!g_Config.m_ClSubTickAiming)
	{
		DoButton_CheckBoxAutoVMarginAndSet(&g_Config.m_AcFastInputRepredictHook, TCLocalize("Repredict on mid-tick hook direction change (may lag)"), &g_Config.m_AcFastInputRepredictHook, &Column, LineSize);
		g_Config.m_TcFastInputRepredictHook = g_Config.m_AcFastInputRepredictHook;
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

	Column.HSplitTop(LineSize, &Button, &Column);
	Ui()->DoScrollbarOption(&g_Config.m_AcFastPracticeGhostAlpha, &g_Config.m_AcFastPracticeGhostAlpha, &Button, TCLocalize("Ghost transparency"), 0, 100, &CUi::ms_LinearScrollbarScale, 0, "%");

	Column.HSplitTop(MarginSmall, nullptr, &Column);
	Column.HSplitTop(16.0f, &Label, &Column);
	TextRender()->TextColor(0.55f, 0.9f, 1.0f, 1.0f);
	Ui()->DoLabel(&Label, TCLocalize("Practice commands: /tp  /tc  /invincible  /weapons  /save  /load"), 11.5f, TEXTALIGN_ML);
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

	// ***** Right Column: HUD, Social, Chat & Alerts ***** //
	LeftView = Column;
	Column = RightView;

	// ***** Overlay RaceStats ***** //
	Column.HSplitTop(Margin, nullptr, &Column);
	s_SectionBoxesAc.push_back(Column);
	Column.HSplitTop(HeadlineHeight, &Label, &Column);
	Ui()->DoLabel(&Label, TCLocalize("Overlay"), HeadlineFontSize, TEXTALIGN_ML);
	Column.HSplitTop(MarginSmall, nullptr, &Column);

	DoCheckboxWithHelp(&g_Config.m_AcMiniStatboard, TCLocalize("Enable Mini Statboard HUD"), TCLocalize("Shows the compact live team statistics table on your screen during the game."), &g_Config.m_AcMiniStatboard, &Column, LineSize);
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

	if(DoCheckboxWithHelp(&g_Config.m_AcSilentTyping, TCLocalize("Silent typing (Hide 3-dots bubble when typing)"), TCLocalize("Prevents the 3-dots bubble (...) from appearing above your Tee when the chat is open."), &g_Config.m_AcSilentTyping, &Column, LineSize))
	{
		g_Config.m_AcSilentTyping = g_Config.m_AcSilentTyping;
		g_Config.m_AcHideChatBubbles = g_Config.m_AcSilentTyping;
	}

	DoCheckboxWithHelp(&g_Config.m_AcHideOthersChatBubbles, TCLocalize("Hide typing bubbles of other players"), TCLocalize("Hides the 3-dots typing bubble of other players on your screen when they are typing."), &g_Config.m_AcHideOthersChatBubbles, &Column, LineSize);

	DoCheckboxWithHelp(&g_Config.m_AcHideStartMessages, TCLocalize("Hide start line messages in chat"), TCLocalize("Hides team start warnings and team started notifications when touching the start line."), &g_Config.m_AcHideStartMessages, &Column, LineSize);

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

	// ***** Background Alerts (Tabbed out) ***** //
	Column.HSplitTop(MarginBetweenSections, nullptr, &Column);
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
	s_SectionBoxesAc.back().h = Column.y - s_SectionBoxesAc.back().y;

	RightView = Column;

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
		Section.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f), IGraphics::CORNER_ALL, 10.0f);
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

	CUIRect ScrollRegion;
	ScrollRegion.x = MainView.x;
	ScrollRegion.y = MainView.y;
	ScrollRegion.w = MainView.w;
	ScrollRegion.h = std::max(LeftView.y, RightView.y) - MainView.y;
	s_ScrollRegionMod.AddRect(ScrollRegion);
	s_ScrollRegionMod.End();
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
	const char *pReqName = IsKoG ? Profiles.KoGRequestedName() : Profiles.RequestedName();

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

	TopBar.VSplitLeft(140.0f, &SearchBox, &TopBar);
	s_ProfileSearchInput.SetEmptyText(TCLocalize("Buscar jugador..."));
	Ui()->DoEditBox(&s_ProfileSearchInput, &SearchBox, FontSize);

	const bool SearchTriggered = s_ProfileSearchInput.IsActive() && (Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || Input()->KeyPress(KEY_RETURN) || Input()->KeyPress(KEY_KP_ENTER));

	TopBar.VSplitLeft(Margin, nullptr, &TopBar);

	// [ Buscar ]
	static CButtonContainer s_BtnSearch;
	TopBar.VSplitLeft(55.0f, &Button, &TopBar);
	if(DoButton_Menu(&s_BtnSearch, TCLocalize("Buscar"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 3.0f, 0.0f, ColorRGBA(0.2f, 0.6f, 0.9f, 0.5f)) || SearchTriggered)
	{
		const char *pSearch = s_ProfileSearchInput.GetString();
		if(pSearch && pSearch[0] != '\0')
		{
			str_copy(s_aLastSyncedReq, pSearch);
			if(IsKoG)
				Profiles.FetchKoGProfile(pSearch);
			else
				Profiles.FetchProfile(pSearch);
		}
	}

	TopBar.VSplitLeft(Margin, nullptr, &TopBar);

	// [ Mi Perfil ]
	static CButtonContainer s_BtnMyProfile;
	TopBar.VSplitLeft(70.0f, &Button, &TopBar);
	if(DoButton_Menu(&s_BtnMyProfile, TCLocalize("Mi Perfil"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 3.0f, 0.0f, ColorRGBA(0.3f, 0.7f, 0.3f, 0.5f)))
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

	TopBar.VSplitLeft(Margin, nullptr, &TopBar);

	// [ Recargar ]
	static CButtonContainer s_BtnRefresh;
	TopBar.VSplitLeft(65.0f, &Button, &TopBar);
	if(DoButton_Menu(&s_BtnRefresh, TCLocalize("Recargar"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 3.0f, 0.0f, ColorRGBA(0.6f, 0.6f, 0.6f, 0.5f)))
	{
		const char *pSearch = s_ProfileSearchInput.GetString();
		const char *pTarget = (pSearch && pSearch[0] != '\0') ? pSearch : Client()->PlayerName();
		if(pTarget && pTarget[0] != '\0')
		{
			str_copy(s_aLastSyncedReq, pTarget);
			if(IsKoG)
				Profiles.FetchKoGProfile(pTarget, true);
			else
				Profiles.FetchProfile(pTarget, true);
		}
	}

	TopBar.VSplitLeft(Margin * 1.5f, nullptr, &TopBar);

	// Source Selector: [ DDNet | KoG ]
	static CButtonContainer s_BtnSrcDDNet;
	static CButtonContainer s_BtnSrcKoG;
	TopBar.VSplitLeft(50.0f, &Button, &TopBar);
	ColorRGBA DDNetColor = !IsKoG ? ColorRGBA(0.2f, 0.55f, 0.9f, 0.85f) : ColorRGBA(0.2f, 0.2f, 0.2f, 0.45f);
	if(DoButton_Menu(&s_BtnSrcDDNet, "DDNet", 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L, 3.0f, 0.0f, DDNetColor))
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

	TopBar.VSplitLeft(45.0f, &Button, &TopBar);
	ColorRGBA KoGColor = IsKoG ? ColorRGBA(0.9f, 0.45f, 0.1f, 0.85f) : ColorRGBA(0.2f, 0.2f, 0.2f, 0.45f);
	if(DoButton_Menu(&s_BtnSrcKoG, "KoG", 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R, 3.0f, 0.0f, KoGColor))
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

	// Web links on right side of TopBar
	static CButtonContainer s_BtnKoGWeb;
	TopBar.VSplitRight(100.0f, &TopBar, &Button);
	if(DoButton_Menu(&s_BtnKoGWeb, TCLocalize("Abrir KoG.tw"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 3.0f, 0.0f, ColorRGBA(0.9f, 0.4f, 0.1f, 0.5f)))
	{
		const char *pTarget = IsKoG ? (Profiles.KoGState() == EProfileState::READY ? Profiles.CurrentKoGProfile().m_aName : s_ProfileSearchInput.GetString())
		                            : (Profiles.State() == EProfileState::READY ? Profiles.CurrentProfile().m_aName : s_ProfileSearchInput.GetString());
		if(!pTarget || pTarget[0] == '\0')
			pTarget = Client()->PlayerName();
		Profiles.OpenKoGWeb(pTarget);
	}

	TopBar.VSplitRight(Margin, &TopBar, nullptr);

	static CButtonContainer s_BtnDDNetWeb;
	TopBar.VSplitRight(115.0f, &TopBar, &Button);
	if(DoButton_Menu(&s_BtnDDNetWeb, TCLocalize("Abrir DDNet.org"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 3.0f, 0.0f, ColorRGBA(0.2f, 0.5f, 0.85f, 0.5f)))
	{
		const char *pTarget = Profiles.State() == EProfileState::READY ? Profiles.CurrentProfile().m_aName : s_ProfileSearchInput.GetString();
		if(!pTarget || pTarget[0] == '\0')
			pTarget = Client()->PlayerName();
		Profiles.OpenDDNetWeb(pTarget);
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
			Content.HSplitTop(80.0f, nullptr, &ErrorBox);
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

		static int s_KoGLeftTab = 0; // 0: Categorias, 1: Companeros
		static CButtonContainer s_BtnKoGCats;
		static CButtonContainer s_BtnKoGTms;

		LeftHeader.VSplitLeft(100.0f, &TabBtn, &LeftHeader);
		ColorRGBA TabCatCol = (s_KoGLeftTab == 0) ? ColorRGBA(0.9f, 0.5f, 0.1f, 0.7f) : ColorRGBA(0.2f, 0.2f, 0.2f, 0.3f);
		if(DoButton_Menu(&s_BtnKoGCats, TCLocalize("Categorias"), 0, &TabBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L, 3.0f, 0.0f, TabCatCol))
			s_KoGLeftTab = 0;

		char aTmTabStr[64];
		str_format(aTmTabStr, sizeof(aTmTabStr), "%s (%zu)", TCLocalize("Companeros"), Prof.m_vTeammates.size());
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
					BarRect.VSplitLeft(FillW, &BarFill, nullptr);
					BarFill.Draw(CatColor, IGraphics::CORNER_ALL, 1.5f);
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
					Ui()->DoLabel(&EmptyLabel, TCLocalize("Sin companeros registrados"), 11.0f, TEXTALIGN_MC);
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
		ColorRGBA TabFinCol = (s_KoGRightTab == 0) ? ColorRGBA(0.2f, 0.6f, 0.9f, 0.7f) : ColorRGBA(0.2f, 0.2f, 0.2f, 0.3f);
		if(DoButton_Menu(&s_BtnKoGFins, aFinTabStr, 0, &TabBtn, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_L, 3.0f, 0.0f, TabFinCol))
			s_KoGRightTab = 0;

		char aUnfinTabStr[64];
		str_format(aUnfinTabStr, sizeof(aUnfinTabStr), "%s (%zu)", TCLocalize("Pendientes"), Prof.m_vUnfinishedMaps.size());
		ColorRGBA TabUnfinCol = (s_KoGRightTab == 1) ? ColorRGBA(0.2f, 0.6f, 0.9f, 0.7f) : ColorRGBA(0.2f, 0.2f, 0.2f, 0.3f);
		if(DoButton_Menu(&s_BtnKoGUnfin, aUnfinTabStr, 0, &RightHeader, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_R, 3.0f, 0.0f, TabUnfinCol))
			s_KoGRightTab = 1;

		// Map search filter input
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

			const float ItemH = 22.0f;
			size_t Shown = 0;

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

				Shown++;
				CUIRect ItemRect;
				RightList.HSplitTop(ItemH, &ItemRect, &RightList);
				RightList.HSplitTop(2.0f, nullptr, &RightList);

				if(s_ScrollKoGUnfinished.AddRect(ItemRect))
				{
					ItemRect.Draw(ColorRGBA(0.0f, 0.0f, 0.0f, 0.2f), IGraphics::CORNER_ALL, 3.0f);
					ItemRect.Margin(3.0f, &ItemRect);

					CUIRect MapNameCol, StatusCol;
					ItemRect.VSplitLeft(ItemRect.w * 0.65f, &MapNameCol, &StatusCol);

					TextRender()->TextColor(1.0f, 0.6f, 0.5f, 1.0f);
					Ui()->DoLabel(&MapNameCol, MapName.c_str(), 11.0f, TEXTALIGN_ML);

					TextRender()->TextColor(0.6f, 0.6f, 0.6f, 0.8f);
					Ui()->DoLabel(&StatusCol, TCLocalize("Pendiente"), 10.0f, TEXTALIGN_MR);
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
		Content.HSplitTop(80.0f, nullptr, &ErrorBox);
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
	BarBg.VSplitLeft(FillW, &BarFill, nullptr);
	BarFill.Draw(ColorRGBA(0.2f, 0.75f, 0.3f, 0.85f), IGraphics::CORNER_ALL, 2.0f);
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

