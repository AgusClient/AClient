#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_BINDCHAT_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_BINDCHAT_H

#include <game/client/component.h>

#include <engine/console.h>

class IConfigManager;

enum
{
	BINDCHAT_MAX_NAME = 64,
	BINDCHAT_MAX_PARAMS = 64,
	BINDCHAT_MAX_HELP = 128,
	BINDCHAT_MAX_CMD = 1024,
	BINDCHAT_MAX_BINDS = 256,
};

class CBindChat : public CComponent
{
public:
	class CBind
	{
	public:
		bool m_IsEx = false;
		char m_aName[BINDCHAT_MAX_NAME];
		char m_aParams[BINDCHAT_MAX_PARAMS];
		char m_aHelp[BINDCHAT_MAX_HELP];
		char m_aCommand[BINDCHAT_MAX_CMD];
		CBind() = default;
		CBind(const char *pName, const char *pCommand)
		{
			str_copy(m_aName, pName);
			m_aParams[0] = '\0';
			m_aHelp[0] = '\0';
			str_copy(m_aCommand, pCommand);
		}
	};
	class CBindDefault
	{
	public:
		const char *m_pTitle;
		CBind m_Bind;
		CBindDefault(const char *pTitle, const char *pName, const char *pCommand)
		{
			m_pTitle = pTitle;
			m_Bind = CBind(pName, pCommand);
		}
	};

private:
	static void ConAddBindchat(IConsole::IResult *pResult, void *pUserData);
	static void ConAddBindchatEx(IConsole::IResult *pResult, void *pUserData);
	static void ConBindchats(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveBindchat(IConsole::IResult *pResult, void *pUserData);
	static void ConRemoveBindchatAll(IConsole::IResult *pResult, void *pUserData);
	static void ConBindchatDefaults(IConsole::IResult *pResult, void *pUserData);

	static void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData);

	static void ExecuteBindExCallback(IConsole::IResult *pResult, void *pUserData);
	void ExecuteBind(const CBind &Bind, const char *pArgs);

public:
	std::vector<CBind> m_vBinds;

	CBindChat();
	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;

	void AddBind(const char *pName, const char *pParams, const char *pHelp, const char *pCommand, bool IsEx);
	void AddBind(const CBind &Bind);

	void RemoveBind(const char *pName);
	void RemoveAllBinds();

	CBind *GetBind(const char *pCommand);

	bool CheckBindChat(const char *pText);
	bool ChatDoBinds(const char *pText);
	bool ChatDoAutocomplete(bool ShiftPressed);
};

static CBindChat::CBindDefault s_aDefaultBindChatKaomoji[] = {
	{"Shrug:", "!shrug", "say ¯\\_(ツ)_/¯"},
	{"Flip:", "!flip", "say (╯°□°)╯︵ ┻━┻"},
	{"Unflip:", "!unflip", "say ┬─┬ノ( º _ ºノ)"},
	{"Cute:", "!cute", "say ૮ ˶ᵔ ᵕ ᵔ˶ ა"},
	{"Lenny:", "!lenny", "say ( ͡° ͜ʖ ͡°)"},
};

static CBindChat::CBindDefault s_aDefaultBindChatWarlist[] = {
	{"Add war name", "!war", "war_name_index 1"},
	{"Add war clan", "!warclan", "war_clan_index 1"},
	{"Add team name", "!team", "war_name_index 2"},
	{"Add team clan", "!teamclan", "war_clan_index 2"},
	{"Remove war name", "!delwar", "remove_war_name_index 1"},
	{"Remove war name", "!delwarclan", "remove_war_clan_index 1"},
	{"Remove team name", "!delteam", "remove_war_name_index 2"},
	{"Remove team clan", "!delteamclan", "remove_war_clan_index 2"},
	{"Add [group] [name] [reason]", "!name", "war_name"},
	{"Add [group] [clan] [reason]", "!clan", "war_clan"},
	{"Remove [group] [name]", "!delname", "remove_war_name"},
	{"Remove [group] [clan]", "!delclan", "remove_war_clan"},
};

static CBindChat::CBindDefault s_aDefaultBindChatMod[] = {
	{"Mute ID:", "!mmute", "mod_rcon_mute"},
	{"Mute Name:", "!mmuten", "mod_rcon_mute_name"},
	{"Unmute Last:", "!munmutelast", "rcon unmute 0"},
	{"Kick ID:", "!mkick", "mod_rcon_kick"},
	{"Kick Name:", "!mkickn", "mod_rcon_kick_name"},
	{"Ban ID:", "!mban", "mod_rcon_ban"},
	{"Ban Name:", "!mbann", "mod_rcon_ban_name"},
	{"Unban Last:", "!munbanlast", "rcon unban 0"},
	{"Kill Ids:", "!mkill", "rcon mod_rcon_kill"},
	{"Kill Names:", "!mkilln", "rcon mod_rcon_kill_name"},
};

static CBindChat::CBindDefault s_aDefaultBindChatOther[] = {
	{"Translate:", "!translate", "translate"},
	{"Translate ID:", "!translateid", "translate_id"},
	{"Mute:", "!mute", "add_foe"},
	{"Unmute:", "!unmute", "remove_foe"},
};

#endif
