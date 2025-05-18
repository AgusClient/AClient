#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include <base/system.h>

#include "mod.h"
#include "engine/shared/protocol.h"

int CMod::IdFromName(const char *pName)
{
	for(const auto &Player : GameClient()->m_aClients)
		if(str_comp(pName, Player.m_aName) == 0)
			return Player.ClientId();
	for(const auto &Player : GameClient()->m_aClients)
		if(str_comp_nocase(pName, Player.m_aName) == 0)
			return Player.ClientId();
	for(const auto &Player : GameClient()->m_aClients)
		if(str_utf8_comp_confusable(pName, Player.m_aName) == 0)
			return Player.ClientId();
	return -1;
}

static int UnitLengthSeconds(char Unit)
{
	switch(Unit)
	{
	case 's':
	case 'S': return 1;
	case 'm':
	case 'M': return 60;
	case 'h':
	case 'H': return 60 * 60;
	case 'd':
	case 'D': return 60 * 60 * 24;
	default: return -1;
	}
}

int CMod::TimeFromStr(const char *pStr, char OutUnit)
{
	double Time = -1;
	char InUnit = OutUnit;
	std::sscanf(pStr, "%lf%c", &Time, &InUnit);
	if(Time < 0)
		return -1;
	int InUnitLength = UnitLengthSeconds(InUnit);
	if(InUnitLength < 0)
		return -1;
	int OutUnitLength = UnitLengthSeconds(OutUnit);
	if(OutUnitLength < 0)
		return -1;
	return std::round(Time * (float)InUnitLength / (float)OutUnitLength);
}

void CMod::ConModCmd(IConsole::IResult *pResult, const char *pCmd, bool IsName, char Unit)
{
	const char *pClient = nullptr;
	if(IsName)
	{
		int ClientId = IdFromName(pResult->GetString(0));
		if(ClientId < 0 || ClientId > SERVERINFO_MAX_CLIENTS)
		{
			GameClient()->Echo("Invalid id, out of range");
			return;
		}
		if(!GameClient()->m_Snap.m_aCharacters[ClientId].m_Active)
		{
			GameClient()->Echo("Invalid id, no player with this id");
			return;
		}
		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "%d", ClientId);
		pClient = aBuf;
	}
	else
	{
		pClient = pResult->GetString(0); // Could be ID or IP
	}
	int Time = TimeFromStr(pResult->GetString(1), Unit);
	if(Time < 0)
	{
		GameClient()->Echo("Invalid time");
		return;
	}

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "%s %s %d %s", pCmd, pClient, Time, pResult->GetString(2));
	Client()->Rcon(aBuf);
}

void CMod::OnInit()
{
#define ADD_MOD_CMD(Name, Cmd, Unit) \
	Console()->Register( \
		"mod_rcon_" Name, "i[id] s[time (" #Unit ")] ?r[reason]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUserData) { \
			static_cast<CMod *>(pUserData)->ConModCmd(pResult, Cmd, false, #Unit[0]); \
		}, \
		this, "RCON " Name " someone"); \
	Console()->Register( \
		"mod_rcon_" Name "_name", "s[name] s[time (" #Unit ")] ?r[reason]", CFGFLAG_CLIENT, [](IConsole::IResult *pResult, void *pUserData) { \
			static_cast<CMod *>(pUserData)->ConModCmd(pResult, Cmd, true, #Unit[0]); \
		}, \
		this, "RCON " Name " someone")
	ADD_MOD_CMD("mute", "muteid", s);
	ADD_MOD_CMD("ban", "ban", m);
#undef ADD_MOD_CMD
}
