#include <engine/shared/config.h>

#include <game/client/gameclient.h>

#include "bindchat.h"
#include "base/log.h"

CBindChat::CBindChat()
{
	OnReset();
}

void CBindChat::ConAddBindchat(IConsole::IResult *pResult, void *pUserData)
{
	const char *pName = pResult->GetString(0);
	const char *pCommand = pResult->GetString(1);

	CBindChat *pThis = static_cast<CBindChat *>(pUserData);
	pThis->AddBind(pName, nullptr, nullptr, pCommand, false);
}

void CBindChat::ConAddBindchatEx(IConsole::IResult *pResult, void *pUserData)
{
	const char *pName = pResult->GetString(0);
	const char *pParams = pResult->GetString(1);
	const char *pHelp = pResult->GetString(2);
	const char *pCommand = pResult->GetString(3);

	CBindChat *pThis = static_cast<CBindChat *>(pUserData);
	pThis->AddBind(pName, pParams, pHelp, pCommand, true);
}

void CBindChat::ConBindchats(IConsole::IResult *pResult, void *pUserData)
{
	CBindChat *pThis = static_cast<CBindChat *>(pUserData);
	char aBuf[BINDCHAT_MAX_NAME + BINDCHAT_MAX_CMD + 32];
	if(pResult->NumArguments() == 1)
	{
		const char *pName = pResult->GetString(0);
		for(const CBind &Bind : pThis->m_vBinds)
		{
			if(str_comp_nocase(Bind.m_aName, pName) == 0)
			{
				str_format(aBuf, sizeof(aBuf), "%s = %s", Bind.m_aName, Bind.m_aCommand);
				pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "bindchat", aBuf);
				return;
			}
		}
		str_format(aBuf, sizeof(aBuf), "%s is not bound", pName);
		pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "bindchat", aBuf);
	}
	else
	{
		for(const CBind &Bind : pThis->m_vBinds)
		{
			str_format(aBuf, sizeof(aBuf), "%s = %s", Bind.m_aName, Bind.m_aCommand);
			pThis->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "bindchat", aBuf);
		}
	}
}

void CBindChat::ConRemoveBindchat(IConsole::IResult *pResult, void *pUserData)
{
	const char *aName = pResult->GetString(0);
	CBindChat *pThis = static_cast<CBindChat *>(pUserData);
	if(!pThis->RemoveBind(aName))
		log_info("bindchat", "bindchat \"%s\" not found", aName);
}

void CBindChat::ConRemoveBindchatAll(IConsole::IResult *pResult, void *pUserData)
{
	CBindChat *pThis = static_cast<CBindChat *>(pUserData);
	pThis->RemoveAllBinds();
}

void CBindChat::ConBindchatDefaults(IConsole::IResult *pResult, void *pUserData)
{
	CBindChat *pThis = static_cast<CBindChat *>(pUserData);

	for(const CBindDefault &BindDefault : s_aDefaultBindChatKaomoji)
		pThis->AddBind(BindDefault.m_Bind);
	for(const CBindDefault &BindDefault : s_aDefaultBindChatWarlist)
		pThis->AddBind(BindDefault.m_Bind);
	for(const CBindDefault &BindDefault : s_aDefaultBindChatOther)
		pThis->AddBind(BindDefault.m_Bind);
	for(const CBindDefault &BindDefault : s_aDefaultBindChatMod)
		pThis->AddBind(BindDefault.m_Bind);
}

void CBindChat::AddBind(const char *pName, const char *pParams, const char *pHelp, const char *pCommand, bool IsEx)
{
	if((pName[0] == '\0' && pCommand[0] == '\0') || m_vBinds.size() >= BINDCHAT_MAX_BINDS)
		return;

	RemoveBind(pName); // Prevent duplicates

	CBind Bind;
	str_copy(Bind.m_aName, pName);
	str_copy(Bind.m_aParams, pParams ? pParams : "?r[args]");
	str_copy(Bind.m_aHelp, pHelp ? pHelp : "Bound with bindchat");
	str_copy(Bind.m_aCommand, pCommand);
	Bind.m_IsEx = IsEx;
	
	AddBind(Bind);
}

// You must ensure input is valid and not a duplicate externally
void CBindChat::AddBind(const CBind &Bind)
{
	m_vBinds.push_back(Bind);
}

bool CBindChat::RemoveBind(const char *pName)
{
	if(pName[0] == '\0')
		return false;
	for(auto It = m_vBinds.begin(); It != m_vBinds.end(); ++It)
	{
		if(str_comp(It->m_aName, pName) == 0)
		{
			m_vBinds.erase(It);
			return true;
		}
	}
	return false;
}

void CBindChat::RemoveAllBinds()
{
	m_vBinds.clear();
}

CBindChat::CBind *CBindChat::GetBind(const char *pCommand)
{
	if(pCommand[0] == '\0')
		return nullptr;
	for(auto &Bind : m_vBinds)
		if(str_comp_nocase(Bind.m_aCommand, pCommand) == 0)
			return &Bind;
	return nullptr;
}

void CBindChat::OnConsoleInit()
{
	IConfigManager *pConfigManager = Kernel()->RequestInterface<IConfigManager>();
	if(pConfigManager)
		pConfigManager->RegisterCallback(ConfigSaveCallback, this, ConfigDomain::TCLIENTCHATBINDS);

	Console()->Register("bindchat", "s[name] r[command]", CFGFLAG_CLIENT, ConAddBindchat, this, "Add a chat bind");
	Console()->Register("bindchat_ex", "s[name] s[params] s[help] r[command]", CFGFLAG_CLIENT, ConAddBindchatEx, this, "Add a chat bind");
	Console()->Register("bindchats", "?s[name]", CFGFLAG_CLIENT, ConBindchats, this, "Print command executed by this name or all chat binds");
	Console()->Register("unbindchat", "s[name]", CFGFLAG_CLIENT, ConRemoveBindchat, this, "Remove a chat bind");
	Console()->Register("unbindchatall", "", CFGFLAG_CLIENT, ConRemoveBindchatAll, this, "Removes all chat binds");
	Console()->Register("bindchatdefaults", "", CFGFLAG_CLIENT, ConBindchatDefaults, this, "Adds default chat binds");

	ConBindchatDefaults(nullptr, this);
}

class CBindChatExCallbackData
{
public:
	CBindChat &m_This;
	const CBindChat::CBind &m_Bind;
};

void CBindChat::ExecuteBindExCallback(CConsole::IResult *pResult, void *pUserData)
{
	if(pUserData == nullptr)
		return;
	const auto &Data = *(CBindChatExCallbackData *)pUserData;
	Data.m_This.GameClient()->m_Conditional.m_pResult = pResult;
	Data.m_This.Console()->ExecuteLine(Data.m_Bind.m_aCommand);
	Data.m_This.GameClient()->m_Conditional.m_pResult = nullptr;
}

void CBindChat::ExecuteBind(const CBindChat::CBind &Bind, const char *pArgs)
{
	char aBuf[BINDCHAT_MAX_CMD] = "";
	CBindChatExCallbackData ExCallbackData{*this, Bind};
	if(Bind.m_IsEx)
	{
		// Not use after stack free, the command is used and invalidated
		Console()->Register("bindchat_ex_temp", Bind.m_aParams, CFGFLAG_CLIENT, ExecuteBindExCallback, &ExCallbackData, "Internal command, do not use");
		str_append(aBuf, "bindchat_ex_temp");
	}
	else
	{
		str_append(aBuf, Bind.m_aCommand);
	}
	if(pArgs && pArgs[0] != '\0')
	{
		str_append(aBuf, " ");
		str_append(aBuf, pArgs);
		// Remove extra space caused by tab
		if(aBuf[str_length(aBuf) - 1] == ' ')
			aBuf[str_length(aBuf) - 1] = '\0';
	}
	Console()->ExecuteLine(aBuf);
	if(Bind.m_IsEx)
	{
		Console()->Register("bindchat_ex_temp", "", CFGFLAG_CLIENT, ExecuteBindExCallback, nullptr, "Internal command, do not use");
	}
}

bool CBindChat::CheckBindChat(const char *pText)
{
	const char *pSpace = str_find(pText, " ");
	size_t SpaceIndex = pSpace ? pSpace - pText : strlen(pText);
	for(const CBind &Bind : m_vBinds)
	{
		if(str_comp_nocase_num(pText, Bind.m_aName, SpaceIndex) == 0)
			return true;
	}
	return false;
}

bool CBindChat::ChatDoBinds(const char *pText)
{
	if(pText[0] == ' ' || pText[0] == '\0' || pText[1] == '\0')
		return false;

	CChat &Chat = GameClient()->m_Chat;
	const char *pSpace = str_find(pText, " ");
	size_t SpaceIndex = pSpace ? pSpace - pText : strlen(pText);
	for(const CBind &Bind : m_vBinds)
	{
		if(str_startswith_nocase(pText, Bind.m_aName) &&
			str_comp_nocase_num(pText, Bind.m_aName, SpaceIndex) == 0)
		{
			ExecuteBind(Bind, pSpace ? pSpace + 1 : nullptr);
			// Add to history (see CChat::SendChatQueued)
			const int Length = str_length(pText);
			CChat::CHistoryEntry *pEntry = Chat.m_History.Allocate(sizeof(CChat::CHistoryEntry) + Length);
			pEntry->m_Team = 0; // All
			str_copy(pEntry->m_aText, pText, Length + 1);
			return true;
		}
	}
	return false;
}

bool CBindChat::ChatDoAutocomplete(bool ShiftPressed)
{
	CChat &Chat = GameClient()->m_Chat;

	if(m_vBinds.empty())
		return false;
	if(*Chat.m_aCompletionBuffer == '\0')
		return false;

	const CBind *pCompletionBind = nullptr;
	int InitialCompletionChosen = Chat.m_CompletionChosen;
	int InitialCompletionUsed = Chat.m_CompletionUsed;

	if(ShiftPressed && Chat.m_CompletionUsed)
		Chat.m_CompletionChosen--;
	else if(!ShiftPressed)
		Chat.m_CompletionChosen++;
	Chat.m_CompletionChosen = (Chat.m_CompletionChosen + m_vBinds.size()) % m_vBinds.size(); // size != 0

	Chat.m_CompletionUsed = true;
	int Index = Chat.m_CompletionChosen;
	for(int i = 0; i < (int)m_vBinds.size(); i++)
	{
		int CommandIndex = (Index + i) % m_vBinds.size();
		if(str_startswith_nocase(m_vBinds.at(CommandIndex).m_aName, Chat.m_aCompletionBuffer))
		{
			pCompletionBind = &m_vBinds.at(CommandIndex);
			Chat.m_CompletionChosen = CommandIndex;
			break;
		}
	}
	/*
	for(const CBind &Bind : m_vBinds)
	{
		if(str_startswith_nocase(Bind.m_aName, Chat.m_aCompletionBuffer))
		{
			pCompletionBind = &Bind;
			Chat.m_CompletionChosen = &Bind - m_vBinds.data();
			break;
		}
	}
	*/

	// insert the command
	if(pCompletionBind)
	{
		char aBuf[CChat::MAX_LINE_LENGTH];
		// add part before the name
		str_truncate(aBuf, sizeof(aBuf), Chat.m_Input.GetString(), Chat.m_PlaceholderOffset);

		// add the command
		str_append(aBuf, pCompletionBind->m_aName);

		// add separator
		// TODO: figure out if the command would accept an extra param
		// char commandBuf[128];
		// str_next_token(pCompletionBind->m_aCommand, " ", commandBuf, sizeof(commandBuf));
		// CCommandInfo *pInfo = m_pClient->Console()->GetCommandInfo(commandBuf, CFGFLAG_CLIENT, false);
		// if(pInfo && pInfo->m_pParams != '\0')
		const char *pSeperator = " ";
		str_append(aBuf, pSeperator);

		// add part after the name
		str_append(aBuf, Chat.m_Input.GetString() + Chat.m_PlaceholderOffset + Chat.m_PlaceholderLength);

		Chat.m_PlaceholderLength = str_length(pSeperator) + str_length(pCompletionBind->m_aName);
		Chat.m_Input.Set(aBuf);
		Chat.m_Input.SetCursorOffset(Chat.m_PlaceholderOffset + Chat.m_PlaceholderLength);
	}
	else
	{
		Chat.m_CompletionChosen = InitialCompletionChosen;
		Chat.m_CompletionUsed = InitialCompletionUsed;
	}

	return pCompletionBind != nullptr;
}

void CBindChat::ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData)
{
	CBindChat *pThis = (CBindChat *)pUserData;
	pConfigManager->WriteLine("unbindchatall", ConfigDomain::TCLIENTCHATBINDS);
	for(CBind &Bind : pThis->m_vBinds)
	{
		char aBuf[BINDCHAT_MAX_CMD * 2] = "";
		char *pEnd = aBuf + sizeof(aBuf);
		char *pDst;
		if(Bind.m_IsEx)
		{
			str_append(aBuf, "bindchat_ex \"");
			// Escape name
			pDst = aBuf + str_length(aBuf);
			str_escape(&pDst, Bind.m_aName, pEnd);
			str_append(aBuf, "\" \"");
			// Escape params
			pDst = aBuf + str_length(aBuf);
			str_escape(&pDst, Bind.m_aParams, pEnd);
			str_append(aBuf, "\" \"");
			// Escape help
			pDst = aBuf + str_length(aBuf);
			str_escape(&pDst, Bind.m_aHelp, pEnd);
			str_append(aBuf, "\" \"");
			// Escape command
			pDst = aBuf + str_length(aBuf);
			str_escape(&pDst, Bind.m_aCommand, pEnd);
			str_append(aBuf, "\"");
		}
		else
		{
			str_append(aBuf, "bindchat \"");
			// Escape name
			pDst = aBuf + str_length(aBuf);
			str_escape(&pDst, Bind.m_aName, pEnd);
			str_append(aBuf, "\" \"");
			// Escape command
			pDst = aBuf + str_length(aBuf);
			str_escape(&pDst, Bind.m_aCommand, pEnd);
			str_append(aBuf, "\"");
		}
		pConfigManager->WriteLine(aBuf, ConfigDomain::TCLIENTCHATBINDS);
	}
}
