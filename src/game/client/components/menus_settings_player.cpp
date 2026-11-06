/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <base/str.h>

#include <engine/graphics.h>
#include <engine/shared/config.h>

#include <game/client/components/console.h>
#include <game/client/components/countryflags.h>
#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/client/ui_listbox.h>
#include <game/localization.h>

#include <vector>

void CMenus::RenderSettingsTeeIdentity(CUIRect MainView)
{
	static CLineInput s_NameInput;
	static CLineInput s_ClanInput;
	char *pName;
	char *pClan;
	int *pCountry;
	const char *pCurrentName;
	if(!m_Dummy)
	{
		pName = g_Config.m_PlayerName;
		pClan = g_Config.m_PlayerClan;
		pCountry = &g_Config.m_PlayerCountry;
		pCurrentName = Client()->PlayerName();
	}
	else
	{
		pName = g_Config.m_ClDummyName;
		pClan = g_Config.m_ClDummyClan;
		pCountry = &g_Config.m_ClDummyCountry;
		pCurrentName = Client()->DummyName();
	}

	s_NameInput.SetBuffer(pName, sizeof(g_Config.m_PlayerName));
	s_NameInput.SetEmptyText(pCurrentName);
	s_ClanInput.SetBuffer(pClan, sizeof(g_Config.m_PlayerClan));

	CUIRect NameRow, NameLabel, NameInput;
	MainView.HSplitTop(20.0f, &NameRow, &MainView);
	NameRow.VSplitLeft(55.0f, &NameLabel, &NameInput);
	if(NameInput.w > 220.0f)
		NameInput.w = 220.0f;
	char aLabel[64];
	str_format(aLabel, sizeof(aLabel), "%s:", Localize("Name"));
	Ui()->DoLabel(&NameLabel, aLabel, 14.0f, TEXTALIGN_ML);
	if(Ui()->DoEditBox(&s_NameInput, &NameInput, 14.0f) && !Client()->IsSixup())
		SetNeedSendInfo();

	MainView.HSplitTop(4.0f, nullptr, &MainView);
	CUIRect ClanRow, ClanLabel, ClanInput;
	MainView.HSplitTop(20.0f, &ClanRow, &MainView);
	ClanRow.VSplitLeft(55.0f, &ClanLabel, &ClanInput);
	if(ClanInput.w > 170.0f)
		ClanInput.w = 170.0f;
	str_format(aLabel, sizeof(aLabel), "%s:", Localize("Clan"));
	Ui()->DoLabel(&ClanLabel, aLabel, 14.0f, TEXTALIGN_ML);
	if(Ui()->DoEditBox(&s_ClanInput, &ClanInput, 14.0f) && !Client()->IsSixup())
		SetNeedSendInfo();

	MainView.HSplitTop(4.0f, nullptr, &MainView);
	CUIRect CountryRow, CountryLabel, CountryButton;
	MainView.HSplitTop(20.0f, &CountryRow, &MainView);
	CountryRow.VSplitLeft(55.0f, &CountryLabel, &CountryButton);
	str_format(aLabel, sizeof(aLabel), "%s:", Localize("Country"));
	Ui()->DoLabel(&CountryLabel, aLabel, 14.0f, TEXTALIGN_ML);
	CountryButton.VSplitLeft(72.0f, &CountryButton, nullptr);
	static CButtonContainer s_CountryButton;
	if(DoButton_Menu(&s_CountryButton, "", 0, &CountryButton))
		m_Popup = POPUP_COUNTRY;
	CUIRect FlagRect;
	CountryButton.Margin(3.0f, &FlagRect);
	const float OldFlagWidth = FlagRect.w;
	FlagRect.w = FlagRect.h * 2.0f;
	FlagRect.x += (OldFlagWidth - FlagRect.w) / 2.0f;
	GameClient()->m_CountryFlags.Render(*pCountry, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h);
	GameClient()->m_Tooltips.DoToolTip(&s_CountryButton, &CountryButton, Localize("Select country"));

	if(Client()->IsSixup() && Client()->State() == IClient::STATE_ONLINE)
	{
		MainView.HSplitTop(4.0f, nullptr, &MainView);
		CUIRect Notice;
		MainView.HSplitTop(18.0f, &Notice, &MainView);
		Ui()->DoLabel(&Notice, Localize("Name, clan and country changes apply after reconnecting."), 12.0f, TEXTALIGN_ML);
	}
}

bool CMenus::RenderCountrySelection(CUIRect MainView)
{
	int *pCountry = m_Dummy ? &g_Config.m_ClDummyCountry : &g_Config.m_PlayerCountry;
	static CLineInputBuffered<25> s_FlagFilterInput;
	class CCountryFlagEntry
	{
	public:
		const CCountryFlags::CCountryFlag *m_pFlag;
		std::optional<std::pair<int, int>> m_NameMatch;
	};
	std::vector<CCountryFlagEntry> vFilteredFlags;
	for(size_t i = 0; i < GameClient()->m_CountryFlags.Num(); ++i)
	{
		const CCountryFlags::CCountryFlag &Entry = GameClient()->m_CountryFlags.GetByIndex(i);
		if(!s_FlagFilterInput.IsEmpty())
		{
			const char *pNameMatchEnd;
			const char *pNameMatchStart = str_utf8_find_nocase(Entry.m_aCountryCodeString, s_FlagFilterInput.GetString(), &pNameMatchEnd);
			if(pNameMatchStart != nullptr)
				vFilteredFlags.emplace_back(&Entry, std::make_pair<int, int>(pNameMatchStart - Entry.m_aCountryCodeString, pNameMatchEnd - pNameMatchStart));
		}
		else
			vFilteredFlags.emplace_back(&Entry, std::nullopt);
	}

	CUIRect QuickSearch;
	MainView.HSplitBottom(20.0f, &MainView, &QuickSearch);
	MainView.HSplitBottom(5.0f, &MainView, nullptr);
	QuickSearch.VSplitLeft(220.0f, &QuickSearch, nullptr);

	int OldSelected = -1;
	static CListBox s_ListBox;
	s_ListBox.DoStart(48.0f, vFilteredFlags.size(), 10, 3, OldSelected, &MainView);
	for(size_t i = 0; i < vFilteredFlags.size(); ++i)
	{
		const CCountryFlagEntry &Entry = vFilteredFlags[i];
		if(Entry.m_pFlag->m_CountryCode == *pCountry)
			OldSelected = i;

		const CListboxItem Item = s_ListBox.DoNextItem(&Entry.m_pFlag->m_CountryCode, OldSelected >= 0 && (size_t)OldSelected == i);
		if(!Item.m_Visible)
			continue;

		CUIRect FlagRect, Label;
		Item.m_Rect.Margin(5.0f, &FlagRect);
		FlagRect.HSplitBottom(12.0f, &FlagRect, &Label);
		Label.HSplitTop(2.0f, nullptr, &Label);
		const float OldWidth = FlagRect.w;
		FlagRect.w = FlagRect.h * 2.0f;
		FlagRect.x += (OldWidth - FlagRect.w) / 2.0f;
		GameClient()->m_CountryFlags.Render(Entry.m_pFlag->m_CountryCode, ColorRGBA(1.0f, 1.0f, 1.0f, 1.0f), FlagRect.x, FlagRect.y, FlagRect.w, FlagRect.h);

		SLabelProperties Props;
		Props.m_MaxWidth = Label.w - 5.0f;
		if(Entry.m_NameMatch.has_value())
		{
			const auto [MatchStart, MatchLength] = Entry.m_NameMatch.value();
			Props.m_vColorSplits.emplace_back(MatchStart, MatchLength, ColorRGBA(0.4f, 0.4f, 1.0f, 1.0f));
		}
		Ui()->DoLabel(&Label, Entry.m_pFlag->m_aCountryCodeString, 10.0f, TEXTALIGN_MC, Props);
	}

	const int NewSelected = s_ListBox.DoEnd();
	if(NewSelected >= 0 && (s_ListBox.WasItemSelected() || s_ListBox.WasItemActivated()))
	{
		*pCountry = vFilteredFlags[NewSelected].m_pFlag->m_CountryCode;
		if(!Client()->IsSixup())
			SetNeedSendInfo();
		return true;
	}

	if(GameClient()->m_CountryFlags.Num() > 0 && vFilteredFlags.empty())
	{
		CUIRect FilterLabel;
		MainView.HMargin((MainView.h - 16.0f) / 2.0f, &FilterLabel);
		Ui()->DoLabel(&FilterLabel, Localize("No country flags match your filter criteria"), 16.0f, TEXTALIGN_MC);
	}

	Ui()->DoEditBox_Search(&s_FlagFilterInput, &QuickSearch, 14.0f, !Ui()->IsPopupOpen() && !GameClient()->m_GameConsole.IsActive());
	return false;
}
