/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "menus_start.h"

#include <engine/client/updater.h>
#include <engine/font_icons.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/textrender.h>

#include <generated/client_data.h>

#include <game/client/gameclient.h>
#include <game/client/ui.h>
#include <game/localization.h>
#include <game/version.h>

#if defined(CONF_PLATFORM_ANDROID)
#include <android/android_main.h>
#endif

void CMenusStart::RenderStartMenu(CUIRect MainView)
{
	GameClient()->m_MenuBackground.ChangePosition(CMenuBackground::POS_START);

	// render logo
	Graphics()->TextureSet(g_pData->m_aImages[IMAGE_BANNER].m_Id);
	Graphics()->QuadsBegin();
	Graphics()->SetColor(1, 1, 1, 1);
	IGraphics::CQuadItem QuadItem(MainView.w / 2 - 170, 60, 360, 103);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	Graphics()->QuadsEnd();

	const float Rounding = 10.0f;
	const float VMargin = MainView.w / 2 - 190.0f;

	CUIRect Button;
	int NewPage = -1;

	CUIRect ExtMenu;
	MainView.VSplitLeft(30.0f, nullptr, &ExtMenu);
	ExtMenu.VSplitLeft(100.0f, &ExtMenu, nullptr);

	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_DiscordButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_DiscordButton, Localize("Discord"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Client()->ViewLink(Localize("https://ddnet.org/discord"));
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_LearnButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_LearnButton, Localize("Learn"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Client()->ViewLink(Localize("https://wiki.ddnet.org/"));
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_TutorialButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_TutorialButton, Localize("Tutorial"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		GameClient()->m_Menus.JoinTutorial();
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_WebsiteButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_WebsiteButton, Localize("Website"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)))
	{
		Client()->ViewLink("https://ddnet.org/");
	}

	ExtMenu.HSplitBottom(5.0f, &ExtMenu, nullptr); // little space
	ExtMenu.HSplitBottom(20.0f, &ExtMenu, &Button);
	static CButtonContainer s_NewsButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_NewsButton, Localize("News"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, g_Config.m_UiUnreadNews ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_N))
		NewPage = CMenus::PAGE_NEWS;

	CUIRect Menu;
	MainView.VMargin(VMargin, &Menu);
	Menu.HSplitBottom(25.0f, &Menu, nullptr);

	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_QuitButton;
	bool UsedEscape = false;
	if(GameClient()->m_Menus.DoButton_Menu(&s_QuitButton, Localize("Quit"), 0, &Button, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || (UsedEscape = Ui()->ConsumeHotkey(CUi::HOTKEY_ESCAPE)) || CheckHotKey(KEY_Q))
	{
		if(UsedEscape || GameClient()->Editor()->HasUnsavedData() || (GameClient()->CurrentRaceTime() / 60 >= g_Config.m_ClConfirmQuitTime && g_Config.m_ClConfirmQuitTime >= 0))
		{
			GameClient()->m_Menus.ShowQuitPopup();
		}
		else
		{
			Client()->Quit();
		}
	}

	Menu.HSplitBottom(100.0f, &Menu, nullptr);
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_SettingsButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_SettingsButton, Localize("Settings"), 0, &Button, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "settings" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_S))
		NewPage = CMenus::PAGE_SETTINGS;

	Menu.HSplitBottom(5.0f, &Menu, nullptr); // little space
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_LocalServerButton;

	const bool LocalServerRunning = GameClient()->m_LocalServer.IsServerRunning();
	if(GameClient()->m_Menus.DoButton_Menu(&s_LocalServerButton, LocalServerRunning ? Localize("Stop server") : Localize("Run server"), 0, &Button, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "local_server" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, LocalServerRunning ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || (CheckHotKey(KEY_R) && Input()->KeyPress(KEY_R)))
	{
		if(LocalServerRunning)
		{
			GameClient()->m_LocalServer.KillServer();
		}
		else
		{
			GameClient()->m_LocalServer.RunServer({});
		}
	}

	Menu.HSplitBottom(5.0f, &Menu, nullptr); // little space
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_MapEditorButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_MapEditorButton, Localize("Editor"), 0, &Button, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "editor" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, GameClient()->Editor()->HasUnsavedData() ? ColorRGBA(0.0f, 1.0f, 0.0f, 0.25f) : ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_E))
	{
		g_Config.m_ClEditor = 1;
		Input()->MouseModeRelative();
	}

	Menu.HSplitBottom(5.0f, &Menu, nullptr); // little space
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_DemoButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_DemoButton, Localize("Demos"), 0, &Button, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "demos" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || CheckHotKey(KEY_D))
	{
		NewPage = CMenus::PAGE_DEMOS;
	}

	Menu.HSplitBottom(5.0f, &Menu, nullptr); // little space
	Menu.HSplitBottom(40.0f, &Menu, &Button);
	static CButtonContainer s_PlayButton;
	if(GameClient()->m_Menus.DoButton_Menu(&s_PlayButton, Localize("Play", "Start menu"), 0, &Button, BUTTONFLAG_LEFT, g_Config.m_ClShowStartMenuImages ? "play_game" : nullptr, IGraphics::CORNER_ALL, Rounding, 0.5f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.25f)) || Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER) || CheckHotKey(KEY_P))
	{
		NewPage = g_Config.m_UiPage >= CMenus::PAGE_INTERNET && g_Config.m_UiPage <= CMenus::PAGE_FAVORITE_COMMUNITY_5 ? g_Config.m_UiPage : CMenus::PAGE_INTERNET;
	}

	// render version
	CUIRect CurVersion, ConsoleButton;
	MainView.HSplitBottom(45.0f, nullptr, &CurVersion);
	CurVersion.VSplitRight(40.0f, &CurVersion, nullptr);
	CurVersion.HSplitTop(20.0f, &ConsoleButton, &CurVersion);
	CurVersion.HSplitTop(5.0f, nullptr, &CurVersion);
	ConsoleButton.VSplitRight(40.0f, nullptr, &ConsoleButton);
	Ui()->DoLabel(&CurVersion, GAME_RELEASE_VERSION, 14.0f, TEXTALIGN_MR);

	CUIRect TClientVersion;
	MainView.HSplitTop(15.0f, &TClientVersion, &MainView);
	TClientVersion.VSplitRight(40.0f, &TClientVersion, nullptr);
	char aTBuf[64];
	str_format(aTBuf, sizeof(aTBuf), CLIENT_NAME " %s", CLIENT_RELEASE_VERSION);
	Ui()->DoLabel(&TClientVersion, aTBuf, 14.0f, TEXTALIGN_MR);
	CUIRect UpdateToDateText;
	MainView.HSplitTop(15.0f, &UpdateToDateText, nullptr);
	UpdateToDateText.VSplitRight(40.0f, &UpdateToDateText, nullptr);
	const auto UpState = GameClient()->m_TClient.GetUpdateState();
	if(UpState == CTClient::UPDATE_STATE_CHECKING)
	{
		Ui()->DoLabel(&UpdateToDateText, TCLocalize("(Buscando actualizacion...)"), 14.0f, TEXTALIGN_MR);
	}
	else if(UpState == CTClient::UPDATE_STATE_AVAILABLE || GameClient()->m_TClient.NeedUpdate())
	{
		Ui()->DoLabel(&UpdateToDateText, TCLocalize("(Actualizacion disponible)"), 14.0f, TEXTALIGN_MR);
	}
	else if(GameClient()->m_TClient.m_FetchedTClientInfo)
	{
		Ui()->DoLabel(&UpdateToDateText, TCLocalize("(Ultima version)"), 14.0f, TEXTALIGN_MR);
	}
	static CButtonContainer s_ConsoleButton;
	TextRender()->SetFontPreset(EFontPreset::ICON_FONT);
	TextRender()->SetRenderFlags(ETextRenderFlags::TEXT_RENDER_FLAG_ONLY_ADVANCE_WIDTH | ETextRenderFlags::TEXT_RENDER_FLAG_NO_X_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_Y_BEARING | ETextRenderFlags::TEXT_RENDER_FLAG_NO_PIXEL_ALIGNMENT | ETextRenderFlags::TEXT_RENDER_FLAG_NO_OVERSIZE);
	if(GameClient()->m_Menus.DoButton_Menu(&s_ConsoleButton, FontIcon::TERMINAL, 0, &ConsoleButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.0f, 0.0f, 0.1f)))
	{
		GameClient()->m_GameConsole.Toggle(CGameConsole::CONSOLETYPE_LOCAL);
	}
	TextRender()->SetRenderFlags(0);
	TextRender()->SetFontPreset(EFontPreset::DEFAULT_FONT);

	CUIRect VersionUpdate;
	MainView.HSplitBottom(20.0f, nullptr, &VersionUpdate);
	VersionUpdate.VMargin(VMargin, &VersionUpdate);
	CUIRect UpdateButton;
	VersionUpdate.VSplitRight(120.0f, &VersionUpdate, &UpdateButton);
	VersionUpdate.VSplitRight(10.0f, &VersionUpdate, nullptr);

	char aBuf[128] = "";
	const auto State = GameClient()->m_TClient.GetUpdateState();
	const char *pLatestVersion = GameClient()->m_TClient.GetLatestVersion();

	if(State == CTClient::UPDATE_STATE_AVAILABLE)
	{
		static CButtonContainer s_VersionUpdate;
		if(GameClient()->m_Menus.DoButton_Menu(&s_VersionUpdate, Localize("Actualizar"), 0, &UpdateButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.0f, 0.45f, 0.85f, 0.6f)))
		{
			GameClient()->m_TClient.InitiateUpdate();
		}
		str_format(aBuf, sizeof(aBuf), Localize("AClient %s disponible!"), pLatestVersion);
		TextRender()->TextColor(0.4f, 0.95f, 0.4f, 1.0f);
	}
	else if(State == CTClient::UPDATE_STATE_DOWNLOADING)
	{
		int Percent = GameClient()->m_TClient.GetUpdatePercent();
		Ui()->RenderProgressBar(UpdateButton, Percent / 100.0f);
		str_format(aBuf, sizeof(aBuf), Localize("Descargando DDNet.exe: %d%%"), Percent);
		TextRender()->TextColor(0.4f, 0.8f, 1.0f, 1.0f);
	}
	else if(State == CTClient::UPDATE_STATE_NEED_RESTART)
	{
		static CButtonContainer s_RestartUpdate;
		if(GameClient()->m_Menus.DoButton_Menu(&s_RestartUpdate, Localize("Reiniciar"), 0, &UpdateButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.2f, 0.75f, 0.2f, 0.6f)))
		{
			Client()->Restart();
		}
		str_copy(aBuf, Localize("AClient actualizado correctamente!"));
		TextRender()->TextColor(0.3f, 1.0f, 0.3f, 1.0f);
	}
	else if(State == CTClient::UPDATE_STATE_FAIL)
	{
		static CButtonContainer s_RetryUpdate;
		if(GameClient()->m_Menus.DoButton_Menu(&s_RetryUpdate, Localize("Reintentar"), 0, &UpdateButton, BUTTONFLAG_LEFT, nullptr, IGraphics::CORNER_ALL, 5.0f, 0.0f, ColorRGBA(0.8f, 0.2f, 0.2f, 0.5f)))
		{
			GameClient()->m_TClient.FetchTClientInfo(true);
		}
		str_copy(aBuf, Localize("Error al actualizar."));
		TextRender()->TextColor(1.0f, 0.4f, 0.4f, 1.0f);
	}

	if(aBuf[0] != '\0')
	{
		Ui()->DoLabel(&VersionUpdate, aBuf, 14.0f, TEXTALIGN_ML);
		TextRender()->TextColor(TextRender()->DefaultTextColor());
	}

	if(NewPage != -1)
	{
		GameClient()->m_Menus.SetShowStart(false);
		GameClient()->m_Menus.SetMenuPage(NewPage);
	}
}

bool CMenusStart::CheckHotKey(int Key) const
{
	return !Input()->ShiftIsPressed() && !Input()->ModifierIsPressed() && !Input()->AltIsPressed() && // no modifier
	       Input()->KeyPress(Key) &&
	       !GameClient()->m_GameConsole.IsActive();
}
