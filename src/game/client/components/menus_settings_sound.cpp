/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus.h"

#include <engine/shared/config.h>
#include <engine/sound.h>
#include <engine/font_icons.h>

#include <game/client/gameclient.h>

#include <game/localization.h>

void CMenus::RenderSettingsSound(CUIRect MainView)
{
	CUIRect Button;
	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndEnable, Localize("Use sounds"), g_Config.m_SndEnable, &Button))
	{
		g_Config.m_SndEnable ^= 1;
		UpdateMusicState();
	}

	m_NeedRestartSound = g_Config.m_SndEnable && !Sound()->IsSoundEnabled();

	if(!g_Config.m_SndEnable)
		return;

	// Audio output device selection
	{
		CUIRect DeviceRow, Label, PrevBtn, DevBtn, NextBtn;
		MainView.HSplitTop(20.0f, &DeviceRow, &MainView);
		DeviceRow.VSplitLeft(160.0f, &Label, &DeviceRow);
		Ui()->DoLabel(&Label, Localize("Audio device"), 14.0f, TEXTALIGN_ML);

		DeviceRow.VSplitLeft(24.0f, &PrevBtn, &DeviceRow);
		DeviceRow.VSplitRight(24.0f, &DeviceRow, &NextBtn);
		DeviceRow.VSplitLeft(4.0f, nullptr, &DevBtn);
		DevBtn.VSplitRight(4.0f, &DevBtn, nullptr);

		int NumDevices = Sound()->GetNumAudioDevices();
		int CurIdx = -1;
		if(g_Config.m_SndDevice[0] && str_comp(g_Config.m_SndDevice, "default") != 0)
		{
			for(int i = 0; i < NumDevices; i++)
			{
				const char *pName = Sound()->GetAudioDeviceName(i);
				if(pName && str_comp(pName, g_Config.m_SndDevice) == 0)
				{
					CurIdx = i;
					break;
				}
			}
		}

		static CButtonContainer s_PrevDevice;
		static CButtonContainer s_NextDevice;
		static CButtonContainer s_CurrentDevice;

		if(DoButton_Menu(&s_PrevDevice, "<", 0, &PrevBtn))
		{
			int NextIdx = CurIdx - 1;
			if(NextIdx < -1)
				NextIdx = NumDevices - 1;
			if(NextIdx == -1 || NumDevices <= 0)
				Sound()->SetAudioDevice("default");
			else
				Sound()->SetAudioDevice(Sound()->GetAudioDeviceName(NextIdx));
		}

		const char *pCurrentName = (CurIdx >= 0) ? Sound()->GetAudioDeviceName(CurIdx) : Localize("Default");
		if(DoButton_Menu(&s_CurrentDevice, pCurrentName ? pCurrentName : Localize("Default"), 0, &DevBtn))
		{
			int NextIdx = CurIdx + 1;
			if(NextIdx >= NumDevices)
				NextIdx = -1;
			if(NextIdx == -1 || NumDevices <= 0)
				Sound()->SetAudioDevice("default");
			else
				Sound()->SetAudioDevice(Sound()->GetAudioDeviceName(NextIdx));
		}

		if(DoButton_Menu(&s_NextDevice, ">", 0, &NextBtn))
		{
			int NextIdx = CurIdx + 1;
			if(NextIdx >= NumDevices)
				NextIdx = -1;
			if(NextIdx == -1 || NumDevices <= 0)
				Sound()->SetAudioDevice("default");
			else
				Sound()->SetAudioDevice(Sound()->GetAudioDeviceName(NextIdx));
		}
		MainView.HSplitTop(4.0f, nullptr, &MainView);
	}

	// Sound pack selection
	{
		CUIRect PackRow, Label, PrevBtn, PackBtn, NextBtn, DirBtn, ReloadBtn;
		MainView.HSplitTop(20.0f, &PackRow, &MainView);
		PackRow.VSplitLeft(160.0f, &Label, &PackRow);
		Ui()->DoLabel(&Label, Localize("Sound pack"), 14.0f, TEXTALIGN_ML);

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

		static CButtonContainer s_PrevPack;
		static CButtonContainer s_NextPack;
		static CButtonContainer s_CurrentPack;
		static CButtonContainer s_DirPack;
		static CButtonContainer s_ReloadPack;

		if(DoButton_Menu(&s_PrevPack, "<", 0, &PrevBtn))
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
			str_copy(aDisplayName, Localize("Default"));
		else
			str_copy(aDisplayName, pCurrentName);

		if(DoButton_Menu(&s_CurrentPack, aDisplayName, 0, &PackBtn))
		{
			int NextIdx = CurIdx + 1;
			if(NextIdx >= NumPacks)
				NextIdx = 0;
			if(NextIdx >= 0 && NextIdx < NumPacks)
				GameClient()->m_Sounds.SetSoundPack(vPacks[NextIdx].c_str());
		}

		if(DoButton_Menu(&s_NextPack, ">", 0, &NextBtn))
		{
			int NextIdx = CurIdx + 1;
			if(NextIdx >= NumPacks)
				NextIdx = 0;
			if(NextIdx >= 0 && NextIdx < NumPacks)
				GameClient()->m_Sounds.SetSoundPack(vPacks[NextIdx].c_str());
		}

		TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
		TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);

		if(DoButton_Menu(&s_DirPack, FontIcon::FOLDER, 0, &DirBtn))
		{
			Storage()->CreateFolder("assets", IStorage::TYPE_SAVE);
			Storage()->CreateFolder("assets/audio", IStorage::TYPE_SAVE);
			char aBuf[IO_MAX_PATH_LENGTH];
			Storage()->GetCompletePath(IStorage::TYPE_SAVE, "assets/audio", aBuf, sizeof(aBuf));
			Client()->ViewFile(aBuf);
		}
		GameClient()->m_Tooltips.DoToolTip(&s_DirPack, &DirBtn, Localize("Open the directory to add custom sound packs"));

		if(DoButton_Menu(&s_ReloadPack, FontIcon::ARROW_ROTATE_RIGHT, 0, &ReloadBtn))
		{
			GameClient()->m_Sounds.ScanSoundPacks();
			GameClient()->m_Sounds.ReloadSoundPack();
		}
		GameClient()->m_Tooltips.DoToolTip(&s_ReloadPack, &ReloadBtn, Localize("Reload sound packs"));

		TextRender()->SetRenderFlags(0);
		TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

		MainView.HSplitTop(4.0f, nullptr, &MainView);
	}

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndMusic, Localize("Play background music"), g_Config.m_SndMusic, &Button))
	{
		g_Config.m_SndMusic ^= 1;
		UpdateMusicState();
	}

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndNonactiveMute, Localize("Mute when not active"), g_Config.m_SndNonactiveMute, &Button))
		g_Config.m_SndNonactiveMute ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndGame, Localize("Enable game sounds"), g_Config.m_SndGame, &Button))
		g_Config.m_SndGame ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndGun, Localize("Enable gun sound"), g_Config.m_SndGun, &Button))
		g_Config.m_SndGun ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndLongPain, Localize("Enable long pain sound (used when shooting in freeze)"), g_Config.m_SndLongPain, &Button))
		g_Config.m_SndLongPain ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndServerMessage, Localize("Enable server message sound"), g_Config.m_SndServerMessage, &Button))
		g_Config.m_SndServerMessage ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndChat, Localize("Enable regular chat sound"), g_Config.m_SndChat, &Button))
		g_Config.m_SndChat ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndTeamChat, Localize("Enable team chat sound"), g_Config.m_SndTeamChat, &Button))
		g_Config.m_SndTeamChat ^= 1;

	MainView.HSplitTop(20.0f, &Button, &MainView);
	if(DoButton_CheckBox(&g_Config.m_SndHighlight, Localize("Enable highlighted chat sound"), g_Config.m_SndHighlight, &Button))
		g_Config.m_SndHighlight ^= 1;

	// volume slider
	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_SndVolume, &g_Config.m_SndVolume, &Button, Localize("Sound volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}

	// volume slider game sounds
	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_SndGameVolume, &g_Config.m_SndGameVolume, &Button, Localize("Game sound volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}

	// volume slider gui sounds
	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_SndChatVolume, &g_Config.m_SndChatVolume, &Button, Localize("Chat sound volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}

	// volume slider map sounds
	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_SndMapVolume, &g_Config.m_SndMapVolume, &Button, Localize("Map sound volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}

	// volume slider background music
	{
		MainView.HSplitTop(5.0f, nullptr, &MainView);
		MainView.HSplitTop(20.0f, &Button, &MainView);
		Ui()->DoScrollbarOption(&g_Config.m_SndBackgroundMusicVolume, &g_Config.m_SndBackgroundMusicVolume, &Button, Localize("Background music volume"), 0, 100, &CUi::ms_LogarithmicScrollbarScale, 0u, "%");
	}
}
