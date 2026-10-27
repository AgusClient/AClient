#include "race_stats.h"
#include "race_stats_icons.h"

#include <base/io.h>
#include <base/mem.h>
#include <base/str.h>
#include <base/time.h>
#include <engine/graphics.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>
#include <engine/textrender.h>
#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/localization.h>
#include <generated/protocol.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static const float HAMMER_MAX_DIST = 135.0f;
static const char *RACESTATS_HISTORY_FILE = "aclient_racestats_history.txt";
static const char *RACESTATS_HISTORY_FILE_FALLBACK = "tclient_racestats_history.txt";

void CRaceStats::OnInit()
{
	m_Active = false;
	m_LastLocalTeam = -1;
	m_RunStartTick = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
		m_aRowAnim[i].Reset();

	auto LoadEmbeddedIcon = [&](const uint8_t *pData, size_t Size, const char *pName) -> IGraphics::CTextureHandle {
		CImageInfo Img;
		if(Graphics()->LoadPng(Img, pData, Size, pName))
		{
			return Graphics()->LoadTextureRawMove(Img, 0, pName);
		}
		return IGraphics::CTextureHandle();
	};

	m_aTextures[ICON_USER]   = LoadEmbeddedIcon(gs_aRaceStatIcon_USER, sizeof(gs_aRaceStatIcon_USER), "racestat_user");
	m_aTextures[ICON_FREEZE] = LoadEmbeddedIcon(gs_aRaceStatIcon_FREEZE, sizeof(gs_aRaceStatIcon_FREEZE), "racestat_freeze");
	m_aTextures[ICON_SAVE]   = LoadEmbeddedIcon(gs_aRaceStatIcon_SAVE, sizeof(gs_aRaceStatIcon_SAVE), "racestat_save");
	m_aTextures[ICON_SKULL]  = LoadEmbeddedIcon(gs_aRaceStatIcon_SKULL, sizeof(gs_aRaceStatIcon_SKULL), "racestat_skull");
	m_aTextures[ICON_EFF]    = LoadEmbeddedIcon(gs_aRaceStatIcon_EFF, sizeof(gs_aRaceStatIcon_EFF), "racestat_eff");
	m_aTextures[ICON_LAST]   = LoadEmbeddedIcon(gs_aRaceStatIcon_LAST, sizeof(gs_aRaceStatIcon_LAST), "racestat_last");
	m_aTextures[ICON_NET]    = LoadEmbeddedIcon(gs_aRaceStatIcon_NET, sizeof(gs_aRaceStatIcon_NET), "racestat_net");
	m_aTextures[ICON_TIME]   = LoadEmbeddedIcon(gs_aRaceStatIcon_TIME, sizeof(gs_aRaceStatIcon_TIME), "racestat_time");
	m_aTextures[ICON_HOOK]   = LoadEmbeddedIcon(gs_aRaceStatIcon_HOOK, sizeof(gs_aRaceStatIcon_HOOK), "racestat_hook");
	m_IconAtlasTexture = LoadEmbeddedIcon(gs_aRaceStatIconAtlas, sizeof(gs_aRaceStatIconAtlas), "racestat_atlas");

	LoadHistoryFromFile();
}

void CRaceStats::ConKeyCompactStatboard(IConsole::IResult *pResult, void *pUserData)
{
	CRaceStats *pSelf = (CRaceStats *)pUserData;
	pSelf->m_Active = pResult->GetInteger(0) != 0;
}

void CRaceStats::ConSaveRaceStats(IConsole::IResult *pResult, void *pUserData)
{
	CRaceStats *pSelf = (CRaceStats *)pUserData;
	pSelf->SaveCurrentStats("Manual / Bind", false, true);
}

void CRaceStats::ConClearRaceStatsHistory(IConsole::IResult *pResult, void *pUserData)
{
	CRaceStats *pSelf = (CRaceStats *)pUserData;
	pSelf->ClearSavedHistory();
	if(pSelf->GameClient())
		pSelf->GameClient()->Echo(TCLocalize("[AClient] RaceStats history cleared."));
}

void CRaceStats::OnConsoleInit()
{
	Console()->Register("+compact_statboard", "", CFGFLAG_CLIENT, ConKeyCompactStatboard, this, "Toggle or hold compact statboard");
	Console()->Register("ac_save_racestats", "", CFGFLAG_CLIENT, ConSaveRaceStats, this, "Save current RaceStats overlay statistics to history");
	Console()->Register("tc_save_racestats", "", CFGFLAG_CLIENT, ConSaveRaceStats, this, "Save current RaceStats overlay statistics to history (alias)");
	Console()->Register("ac_clear_racestats_history", "", CFGFLAG_CLIENT, ConClearRaceStatsHistory, this, "Clear saved RaceStats history");
	Console()->Register("tc_clear_racestats_history", "", CFGFLAG_CLIENT, ConClearRaceStatsHistory, this, "Clear saved RaceStats history (alias)");
}

void CRaceStats::OnShutdown()
{
	if(m_HistoryNeedsSave)
	{
		SaveHistoryToFile();
		m_HistoryNeedsSave = false;
	}
}

void CRaceStats::OnReset()
{
	if(m_HistoryNeedsSave)
	{
		SaveHistoryToFile();
		m_HistoryNeedsSave = false;
	}

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_aPlayerStats[i].Reset();
		m_aRowAnim[i].Reset();
	}
	m_RoundDeaths = 0;
	m_LastLocalTeam = -1;
	m_RunStartTick = 0;
	m_Active = false;
	m_LastStatsTick = -1;
	m_CacheValid = false;
	m_LastCacheTick = -1;
	m_vCachedRows.clear();
	m_CachedDisplayCount = 0;
	m_CachedTotalTeamPlayers = 0;
	m_StatsDirty = true;
}

bool CRaceStats::HasCurrentTeamActivity() const
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_aPlayerStats[i].m_Freezes > 0 || m_aPlayerStats[i].m_Saves > 0 || m_aPlayerStats[i].m_Lasts > 0)
			return true;
	}
	return false;
}

void CRaceStats::FormatCurrentRunTime(char *pBuf, int BufSize) const
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		str_copy(pBuf, "00:00", BufSize);
		return;
	}

	int CurTick = Client()->GameTick(g_Config.m_ClDummy);
	int TickSpeed = std::max(1, Client()->GameTickSpeed());
	int StartTick = m_RunStartTick > 0 ? m_RunStartTick : CurTick;
	int ElapsedSec = std::max(0, (CurTick - StartTick) / TickSpeed);

	int Hours = ElapsedSec / 3600;
	int Minutes = (ElapsedSec % 3600) / 60;
	int Seconds = ElapsedSec % 60;

	if(Hours > 0)
		str_format(pBuf, BufSize, "%02d:%02d:%02d", Hours, Minutes, Seconds);
	else
		str_format(pBuf, BufSize, "%02d:%02d", Minutes, Seconds);
}

void CRaceStats::ResetTeamStats()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_aPlayerStats[i].Reset();
		m_aRowAnim[i].Reset();
	}
	m_RoundDeaths = 0;
	m_CacheValid = false;
	m_LastCacheTick = -1;
	m_vCachedRows.clear();
	m_CachedDisplayCount = 0;
	m_CachedTotalTeamPlayers = 0;
	m_StatsDirty = true;
	if(Client()->State() == IClient::STATE_ONLINE || Client()->State() == IClient::STATE_DEMOPLAYBACK)
		m_RunStartTick = Client()->GameTick(g_Config.m_ClDummy);
	else
		m_RunStartTick = 0;
}

bool CRaceStats::SaveCurrentStats(const char *pReason, bool RequireActivity, bool NotifyChat)
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
	{
		if(NotifyChat)
			GameClient()->m_Chat.Echo("RaceStats: Debes estar en una partida para guardar estadisticas.");
		return false;
	}

	if(RequireActivity && !HasCurrentTeamActivity())
		return false;

	int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS)
		return false;

	int RecordTeam = m_LastLocalTeam >= 0 ? m_LastLocalTeam : GameClient()->m_Teams.Team(LocalClientId);

	CSavedRaceStatsEntry Entry;
	str_timestamp_format(Entry.m_aTimestamp, sizeof(Entry.m_aTimestamp), "%d/%m %H:%M:%S");

	const char *pMapName = Client()->ServerInfo().m_aMap;
	if(!pMapName || pMapName[0] == '\0')
		pMapName = "Unknown Map";
	str_copy(Entry.m_aMapName, pMapName, sizeof(Entry.m_aMapName));
	Entry.m_Team = RecordTeam;
	FormatCurrentRunTime(Entry.m_aRunTime, sizeof(Entry.m_aRunTime));
	str_copy(Entry.m_aReason, pReason ? pReason : "Manual", sizeof(Entry.m_aReason));

	auto AddPlayerToEntry = [&](int CID, bool IsLocal) {
		CSavedPlayerStat PStat;
		const char *pPlayerName = GameClient()->m_aClients[CID].m_aName;
		str_copy(PStat.m_aName, (pPlayerName && pPlayerName[0]) ? pPlayerName : "Nameless", sizeof(PStat.m_aName));

		const char *pSkinName = GameClient()->m_aClients[CID].m_aSkinName;
		str_copy(PStat.m_aSkinName, (pSkinName && pSkinName[0]) ? pSkinName : "default", sizeof(PStat.m_aSkinName));

		PStat.m_CustomColors = GameClient()->m_aClients[CID].m_UseCustomColor != 0;
		PStat.m_ColorBody = GameClient()->m_aClients[CID].m_ColorBody;
		PStat.m_ColorFeet = GameClient()->m_aClients[CID].m_ColorFeet;
		PStat.m_Freezes = m_aPlayerStats[CID].m_Freezes;
		PStat.m_Saves = m_aPlayerStats[CID].m_Saves;
		PStat.m_Lasts = m_aPlayerStats[CID].m_Lasts;
		PStat.m_EfficiencyPercent = m_aPlayerStats[CID].GetEfficiencyPercent();
		PStat.m_Deaths = m_aPlayerStats[CID].m_Deaths;
		PStat.m_NetBalance = m_aPlayerStats[CID].GetNetBalance();
		PStat.m_FrozenSeconds = m_aPlayerStats[CID].GetFrozenSeconds(Client()->GameTickSpeed());
		PStat.m_HookAccuracyPercent = m_aPlayerStats[CID].GetHookAccuracyPercent();
		PStat.m_IsLocal = IsLocal;
		Entry.m_vPlayers.push_back(PStat);
	};

	if(GameClient()->m_Snap.m_apPlayerInfos[LocalClientId])
		AddPlayerToEntry(LocalClientId, true);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == LocalClientId || !GameClient()->m_Snap.m_apPlayerInfos[i])
			continue;

		int PlayerTeam = GameClient()->m_Teams.Team(i);
		if(RecordTeam > 0)
		{
			if(PlayerTeam == RecordTeam)
				AddPlayerToEntry(i, false);
		}
		else
		{
			bool HasActivity = (m_aPlayerStats[i].m_Freezes > 0 || m_aPlayerStats[i].m_Saves > 0 || m_aPlayerStats[i].m_Lasts > 0);
			if(PlayerTeam == 0 && HasActivity)
				AddPlayerToEntry(i, false);
		}
	}

	if(Entry.m_vPlayers.empty())
		return false;

	// Insertar al inicio para ver las más recientes primero (límite de 100 entradas)
	m_vSavedEntries.insert(m_vSavedEntries.begin(), Entry);
	if(m_vSavedEntries.size() > 100)
		m_vSavedEntries.resize(100);

	if(NotifyChat)
	{
		SaveHistoryToFile();
		m_HistoryNeedsSave = false;

		char aMsg[128];
		str_format(aMsg, sizeof(aMsg), "RaceStats guardadas! (%s - Run: %s)", Entry.m_aMapName, Entry.m_aRunTime);
		GameClient()->m_Chat.Echo(aMsg);
	}
	else
	{
		m_HistoryNeedsSave = true;
	}

	return true;
}

void CRaceStats::DeleteSavedEntry(int Index)
{
	if(Index < 0 || Index >= (int)m_vSavedEntries.size())
		return;
	m_vSavedEntries.erase(m_vSavedEntries.begin() + Index);
	SaveHistoryToFile();
	m_HistoryNeedsSave = false;
}

void CRaceStats::ClearSavedHistory()
{
	m_vSavedEntries.clear();
	SaveHistoryToFile();
	m_HistoryNeedsSave = false;
}

void CRaceStats::SaveHistoryToFile()
{
	if(!Storage())
		return;

	IOHANDLE File = Storage()->OpenFile(RACESTATS_HISTORY_FILE, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(!File)
		return;

	char aLine[512];
	for(const auto &Entry : m_vSavedEntries)
	{
		str_format(aLine, sizeof(aLine), "ENTRY\t%s\t%s\t%d\t%s\t%s\n",
			Entry.m_aTimestamp, Entry.m_aMapName, Entry.m_Team, Entry.m_aRunTime, Entry.m_aReason);
		io_write(File, aLine, str_length(aLine));

		for(const auto &Pl : Entry.m_vPlayers)
		{
			str_format(aLine, sizeof(aLine), "PLAYER\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
				Pl.m_aName, Pl.m_aSkinName, Pl.m_CustomColors ? 1 : 0, Pl.m_ColorBody, Pl.m_ColorFeet,
				Pl.m_Freezes, Pl.m_Saves, Pl.m_Lasts, Pl.m_EfficiencyPercent, Pl.m_IsLocal ? 1 : 0, Pl.m_Deaths,
				Pl.m_NetBalance, Pl.m_FrozenSeconds, Pl.m_HookAccuracyPercent);
			io_write(File, aLine, str_length(aLine));
		}

		const char *pEnd = "END\n";
		io_write(File, pEnd, str_length(pEnd));
	}

	io_close(File);
}

static bool NextTabToken(const char **ppCursor, char *pOut, int OutSize)
{
	if(!ppCursor || !*ppCursor || **ppCursor == '\0')
	{
		pOut[0] = '\0';
		return false;
	}
	const char *pStart = *ppCursor;
	const char *pTab = strchr(pStart, '\t');
	if(pTab)
	{
		int Len = (int)(pTab - pStart);
		if(Len >= OutSize)
			Len = OutSize - 1;
		mem_copy(pOut, pStart, Len);
		pOut[Len] = '\0';
		*ppCursor = pTab + 1;
	}
	else
	{
		str_copy(pOut, pStart, OutSize);
		*ppCursor = pStart + str_length(pStart);
	}
	return true;
}

void CRaceStats::LoadHistoryFromFile()
{
	if(!Storage())
		return;

	IOHANDLE File = Storage()->OpenFile(RACESTATS_HISTORY_FILE, IOFLAG_READ, IStorage::TYPE_ALL);
	if(!File)
		File = Storage()->OpenFile(RACESTATS_HISTORY_FILE_FALLBACK, IOFLAG_READ, IStorage::TYPE_ALL);
	if(!File)
		return;

	CLineReader LineReader;
	if(!LineReader.OpenFile(File))
		return;

	m_vSavedEntries.clear();
	CSavedRaceStatsEntry CurrentEntry;
	bool InEntry = false;

	while(const char *pLine = LineReader.Get())
	{
		const char *pCursor = pLine;
		char aTag[32];
		if(!NextTabToken(&pCursor, aTag, sizeof(aTag)))
			continue;

		if(str_comp(aTag, "ENTRY") == 0)
		{
			CurrentEntry = CSavedRaceStatsEntry();
			char aTeam[16];
			NextTabToken(&pCursor, CurrentEntry.m_aTimestamp, sizeof(CurrentEntry.m_aTimestamp));
			NextTabToken(&pCursor, CurrentEntry.m_aMapName, sizeof(CurrentEntry.m_aMapName));
			NextTabToken(&pCursor, aTeam, sizeof(aTeam));
			CurrentEntry.m_Team = str_toint(aTeam);
			NextTabToken(&pCursor, CurrentEntry.m_aRunTime, sizeof(CurrentEntry.m_aRunTime));
			if(CurrentEntry.m_aRunTime[0] == '\0')
				str_copy(CurrentEntry.m_aRunTime, "00:00", sizeof(CurrentEntry.m_aRunTime));
			NextTabToken(&pCursor, CurrentEntry.m_aReason, sizeof(CurrentEntry.m_aReason));
			if(CurrentEntry.m_aReason[0] == '\0')
				str_copy(CurrentEntry.m_aReason, "Saved", sizeof(CurrentEntry.m_aReason));
			InEntry = true;
		}
		else if(str_comp(aTag, "PLAYER") == 0 && InEntry)
		{
			CSavedPlayerStat Pl;
			char aBuf[32];
			NextTabToken(&pCursor, Pl.m_aName, sizeof(Pl.m_aName));
			NextTabToken(&pCursor, Pl.m_aSkinName, sizeof(Pl.m_aSkinName));
			NextTabToken(&pCursor, aBuf, sizeof(aBuf));
			Pl.m_CustomColors = str_toint(aBuf) != 0;
			NextTabToken(&pCursor, aBuf, sizeof(aBuf));
			Pl.m_ColorBody = str_toint(aBuf);
			NextTabToken(&pCursor, aBuf, sizeof(aBuf));
			Pl.m_ColorFeet = str_toint(aBuf);
			NextTabToken(&pCursor, aBuf, sizeof(aBuf));
			Pl.m_Freezes = str_toint(aBuf);
			NextTabToken(&pCursor, aBuf, sizeof(aBuf));
			Pl.m_Saves = str_toint(aBuf);
			NextTabToken(&pCursor, aBuf, sizeof(aBuf));
			Pl.m_Lasts = str_toint(aBuf);
			NextTabToken(&pCursor, aBuf, sizeof(aBuf));
			Pl.m_EfficiencyPercent = str_toint(aBuf);
			NextTabToken(&pCursor, aBuf, sizeof(aBuf));
			Pl.m_IsLocal = str_toint(aBuf) != 0;
			if(NextTabToken(&pCursor, aBuf, sizeof(aBuf)))
				Pl.m_Deaths = str_toint(aBuf);
			else
				Pl.m_Deaths = 0;
			if(NextTabToken(&pCursor, aBuf, sizeof(aBuf)))
				Pl.m_NetBalance = str_toint(aBuf);
			else
				Pl.m_NetBalance = Pl.m_Saves - Pl.m_Freezes;
			if(NextTabToken(&pCursor, aBuf, sizeof(aBuf)))
				Pl.m_FrozenSeconds = str_toint(aBuf);
			else
				Pl.m_FrozenSeconds = 0;
			if(NextTabToken(&pCursor, aBuf, sizeof(aBuf)))
				Pl.m_HookAccuracyPercent = str_toint(aBuf);
			else
				Pl.m_HookAccuracyPercent = -1;
			CurrentEntry.m_vPlayers.push_back(Pl);
		}
		else if(str_comp(aTag, "END") == 0 && InEntry)
		{
			if(!CurrentEntry.m_vPlayers.empty())
				m_vSavedEntries.push_back(CurrentEntry);
			InEntry = false;
		}
	}
}

void CRaceStats::OnMessage(int MsgType, void *pRawMsg)
{
	int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS)
		return;

	int LocalTeam = GameClient()->m_Teams.Team(LocalClientId);

	if(MsgType == NETMSGTYPE_SV_KILLMSG)
	{
		CNetMsg_Sv_KillMsg *pMsg = (CNetMsg_Sv_KillMsg *)pRawMsg;
		if(pMsg->m_Victim >= 0 && pMsg->m_Victim < MAX_CLIENTS)
		{
			m_aPlayerStats[pMsg->m_Victim].m_Deaths++;
			m_StatsDirty = true;
			if(pMsg->m_Victim == LocalClientId)
			{
				m_RoundDeaths++;
				m_SessionDeaths++;
			}

			bool IsLocalKill = (pMsg->m_Victim == LocalClientId);
			bool IsTeammateKill = (LocalTeam > 0 && GameClient()->m_Teams.Team(pMsg->m_Victim) == LocalTeam);

			if(!g_Config.m_AcStatboardKeepOnDeath && g_Config.m_AcStatboardResetOnTeamKill && (IsLocalKill || IsTeammateKill))
			{
				if(g_Config.m_AcStatboardAutoSave)
					SaveCurrentStats("Auto (/kill)", true, false);
				ResetTeamStats();
			}
		}
	}
	else if(MsgType == NETMSGTYPE_SV_KILLMSGTEAM)
	{
		CNetMsg_Sv_KillMsgTeam *pMsg = (CNetMsg_Sv_KillMsgTeam *)pRawMsg;
		if(pMsg->m_Team >= 0)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(GameClient()->m_Teams.Team(i) == pMsg->m_Team)
				{
					m_aPlayerStats[i].m_Deaths++;
					m_StatsDirty = true;
					if(i == LocalClientId)
					{
						m_RoundDeaths++;
						m_SessionDeaths++;
					}
				}
			}
		}

		if(!g_Config.m_AcStatboardKeepOnDeath && g_Config.m_AcStatboardResetOnTeamKill && pMsg->m_Team == LocalTeam)
		{
			if(g_Config.m_AcStatboardAutoSave)
				SaveCurrentStats("Auto (/kill)", true, false);
			ResetTeamStats();
		}
	}
}

void CRaceStats::OnRender()
{
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	if(!GameClient()->m_Snap.m_pGameInfoObj)
		return;

	int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS)
		return;

	int CurrentTick = Client()->GameTick(g_Config.m_ClDummy);
	if(m_RunStartTick <= 0)
		m_RunStartTick = CurrentTick;

	// Detectar cambio de equipo (/team 1, /team 0, etc.)
	int CurrentTeam = GameClient()->m_Teams.Team(LocalClientId);
	if(m_LastLocalTeam != -1 && CurrentTeam != m_LastLocalTeam)
	{
		if(g_Config.m_AcStatboardResetOnTeamKill)
		{
			if(g_Config.m_AcStatboardAutoSave)
				SaveCurrentStats("Auto (/team)", true, false);
			ResetTeamStats();
		}
	}
	m_LastLocalTeam = CurrentTeam;

	// Detección sincronizada por tick de juego (evita sobrecarga y deriva de FPS)
	const bool IsNewTick = (CurrentTick != m_LastStatsTick);
	if(IsNewTick)
	{
		const int ElapsedTicks = (m_LastStatsTick > 0 && CurrentTick > m_LastStatsTick) ? std::min(CurrentTick - m_LastStatsTick, 25) : 1;
		m_LastStatsTick = CurrentTick;

		int TeamActivePlayers = 0;
		int TeamUnfrozenPlayers = 0;
		int LastUnfrozenCID = -1;

		// FASE 1: Registro global de ataques de martillo (con soporte para predicción local del atacante)
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!GameClient()->m_Snap.m_apPlayerInfos[i] || !GameClient()->m_Snap.m_aCharacters[i].m_Active)
				continue;

			const auto &Char = GameClient()->m_Snap.m_aCharacters[i];
			bool IsLocal = (i == LocalClientId);

			bool IsHammer = (Char.m_Cur.m_Weapon == WEAPON_HAMMER);
			int CurAttackTick = Char.m_Cur.m_AttackTick;

			if(IsLocal)
			{
				if(GameClient()->m_aClients[i].m_Predicted.m_ActiveWeapon == WEAPON_HAMMER ||
				   GameClient()->m_aClients[i].m_RenderCur.m_Weapon == WEAPON_HAMMER)
				{
					IsHammer = true;
				}
				if(GameClient()->m_aClients[i].m_RenderCur.m_AttackTick > CurAttackTick)
				{
					CurAttackTick = GameClient()->m_aClients[i].m_RenderCur.m_AttackTick;
				}
			}

			if(IsHammer && (CurAttackTick != m_aPlayerStats[i].m_LastAttackTick))
			{
				m_aPlayerStats[i].m_LastAttackTick = CurAttackTick;
				m_aPlayerStats[i].m_LastHammerTick = CurrentTick;
				m_aPlayerStats[i].m_LastHammerPos = IsLocal ? GameClient()->m_LocalCharacterPos : vec2(Char.m_Cur.m_X, Char.m_Cur.m_Y);
			}
		}

		// FASE 2: Métricas por jugador y resolución de Saves por descongelación
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(!GameClient()->m_Snap.m_apPlayerInfos[i] || !GameClient()->m_Snap.m_aCharacters[i].m_Active)
			{
				m_aPlayerStats[i].m_WasFrozen = false;
				continue;
			}

			const auto &ClientData = GameClient()->m_aClients[i];
			const auto &Char = GameClient()->m_Snap.m_aCharacters[i];
			bool IsLocal = (i == LocalClientId);

			// Usar el estado real confirmado para evitar parpadeos y desincronizaciones de predicción
			bool IsFrozen = (ClientData.m_FreezeEnd != 0 || ClientData.m_DeepFrozen || ClientData.m_LiveFrozen);

			if(GameClient()->m_Teams.Team(i) == CurrentTeam)
			{
				TeamActivePlayers++;
				if(!IsFrozen)
				{
					TeamUnfrozenPlayers++;
					LastUnfrozenCID = i;
				}
			}

			// Seguimiento exacto de tiempo congelado por ticks de juego
			if(IsFrozen)
			{
				m_aPlayerStats[i].m_FrozenTicks += ElapsedTicks;
			}

			// Seguimiento de hook accuracy
			int CurHookState = Char.m_Cur.m_HookState;
			if(CurHookState == HOOK_FLYING && m_aPlayerStats[i].m_LastHookState != HOOK_FLYING && m_aPlayerStats[i].m_LastHookState != HOOK_GRABBED)
			{
				m_aPlayerStats[i].m_HooksThrown++;
			}
			if(CurHookState == HOOK_GRABBED && Char.m_Cur.m_HookedPlayer >= 0 && Char.m_Cur.m_HookedPlayer < MAX_CLIENTS && m_aPlayerStats[i].m_LastHookState != HOOK_GRABBED)
			{
				m_aPlayerStats[i].m_HooksHit++;
			}
			m_aPlayerStats[i].m_LastHookState = CurHookState;

			// Transición: Entró en congelación
			if(IsFrozen && !m_aPlayerStats[i].m_WasFrozen)
			{
				m_aPlayerStats[i].m_Freezes++;
				m_aPlayerStats[i].m_LastFreezeTick = CurrentTick;
				m_StatsDirty = true;
			}

			// Transición: Se descongeló -> Comprobar si un compañero lo salvó con martillo
			if(!IsFrozen && m_aPlayerStats[i].m_WasFrozen)
			{
				vec2 VictimPos = IsLocal ? GameClient()->m_LocalCharacterPos : vec2(Char.m_Cur.m_X, Char.m_Cur.m_Y);
				int Team = GameClient()->m_Teams.Team(i);

				int SaverCID = -1;
				float BestDist = HAMMER_MAX_DIST;

				for(int j = 0; j < MAX_CLIENTS; j++)
				{
					if(j == i || !GameClient()->m_Snap.m_apPlayerInfos[j] || !GameClient()->m_Snap.m_aCharacters[j].m_Active)
						continue;

					if(GameClient()->m_Teams.Team(j) != Team)
						continue;

					int TickDiff = std::abs(CurrentTick - m_aPlayerStats[j].m_LastHammerTick);
					if(TickDiff <= 25)
					{
						vec2 AttackerPos = (j == LocalClientId) ? GameClient()->m_LocalCharacterPos : vec2(GameClient()->m_Snap.m_aCharacters[j].m_Cur.m_X, GameClient()->m_Snap.m_aCharacters[j].m_Cur.m_Y);
						float DistCurrent = distance(VictimPos, AttackerPos);
						float DistAtHit = distance(VictimPos, m_aPlayerStats[j].m_LastHammerPos);
						float Dist = std::min(DistCurrent, DistAtHit);
						if(Dist <= BestDist)
						{
							BestDist = Dist;
							SaverCID = j;
						}
					}
				}

				if(SaverCID != -1)
				{
					m_aPlayerStats[SaverCID].m_Saves++;
					m_StatsDirty = true;
				}
			}

			m_aPlayerStats[i].m_WasFrozen = IsFrozen;
		}

		// Detección estabilizada de Lasts (requiere al menos 15 ticks reales como único descongelado)
		if(TeamActivePlayers >= 2 && TeamUnfrozenPlayers == 1 && LastUnfrozenCID != -1)
		{
			m_aPlayerStats[LastUnfrozenCID].m_SoleSurvivorTicks += ElapsedTicks;
			if(m_aPlayerStats[LastUnfrozenCID].m_SoleSurvivorTicks >= 15 && !m_aPlayerStats[LastUnfrozenCID].m_WasSoleSurvivor)
			{
				m_aPlayerStats[LastUnfrozenCID].m_Lasts++;
				m_aPlayerStats[LastUnfrozenCID].m_WasSoleSurvivor = true;
				m_StatsDirty = true;
			}
		}
		else
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				if(GameClient()->m_Teams.Team(i) == CurrentTeam)
				{
					m_aPlayerStats[i].m_SoleSurvivorTicks = 0;
					m_aPlayerStats[i].m_WasSoleSurvivor = false;
				}
			}
		}
	}

	if(g_Config.m_AcMiniStatboard || m_Active)
	{
		RenderMiniStatboard();
	}
}

ColorRGBA CRaceStats::GetEfficiencyColor(int Percent)
{
	if(Percent < 0)
		return ColorRGBA(0.75f, 0.75f, 0.80f, 0.90f); // Sin datos: gris neutro

	float Norm = std::clamp((float)Percent / 100.0f, 0.0f, 1.0f);
	float R, G, B = 0.25f;
	if(Norm < 0.5f)
	{
		float t = Norm / 0.5f;
		R = 1.0f;
		G = 0.20f + 0.80f * t;
	}
	else
	{
		float t = (Norm - 0.5f) / 0.5f;
		R = 1.0f - 0.80f * t;
		G = 1.0f;
	}
	return ColorRGBA(R, G, B, 0.95f);
}

void CRaceStats::UpdateMiniStatboardCache()
{
	int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS)
	{
		m_vCachedRows.clear();
		m_CachedDisplayCount = 0;
		m_CacheValid = false;
		return;
	}

	int LocalTeam = GameClient()->m_Teams.Team(LocalClientId);

	m_CacheValid = true;
	m_LastCacheTick = Client()->GameTick(g_Config.m_ClDummy);
	m_LastCacheColumns = g_Config.m_AcMiniStatboardColumns;
	m_LastCacheSort = g_Config.m_AcMiniStatboardSort;
	m_LastCacheScale = g_Config.m_AcMiniStatboardScale;
	m_LastCacheMaxPlayers = g_Config.m_AcMiniStatboardMaxPlayers;
	m_LastCacheShowTees = g_Config.m_AcMiniStatboardShowTees;
	m_LastCacheTeam = LocalTeam;
	m_LastCacheLocalClientId = LocalClientId;

	m_vCachedRows.clear();
	m_CachedDisplayCount = 0;
	m_CachedTotalTeamPlayers = 0;
	m_CachedHasMore = false;
	m_aCachedMoreText[0] = '\0';
	m_CachedMoreTextWidth = 0.0f;
	m_StatsDirty = false;

	struct SPlayerEntry
	{
		int m_ClientId;
		bool m_IsLocal;
		int m_SortKey;
	};
	SPlayerEntry aPlayers[MAX_CLIENTS];
	int NumPlayers = 0;

	if(GameClient()->m_Snap.m_apPlayerInfos[LocalClientId])
		aPlayers[NumPlayers++] = {LocalClientId, true, 0};

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == LocalClientId || !GameClient()->m_Snap.m_apPlayerInfos[i])
			continue;
		if(LocalTeam > 0 && GameClient()->m_Teams.Team(i) != LocalTeam)
			continue;
		aPlayers[NumPlayers++] = {i, false, 0};
	}

	if(NumPlayers == 0)
		return;

	// Precomputar la metrica de ordenamiento O(N) una sola vez por jugador para evitar evaluar formulas durante el sort
	for(int i = 0; i < NumPlayers; i++)
	{
		int Cid = aPlayers[i].m_ClientId;
		const auto &Stat = m_aPlayerStats[Cid];
		switch(g_Config.m_AcMiniStatboardSort)
		{
		case STATBOARD_SORT_SAVES: aPlayers[i].m_SortKey = Stat.m_Saves; break;
		case STATBOARD_SORT_DEATHS: aPlayers[i].m_SortKey = Stat.m_Deaths; break;
		case STATBOARD_SORT_FREEZES: aPlayers[i].m_SortKey = Stat.m_Freezes; break;
		case STATBOARD_SORT_EFFICIENCY: aPlayers[i].m_SortKey = Stat.GetEfficiencyPercent(); break;
		case STATBOARD_SORT_NET: aPlayers[i].m_SortKey = Stat.GetNetBalance(); break;
		case STATBOARD_SORT_TIME: aPlayers[i].m_SortKey = Stat.GetFrozenSeconds(Client()->GameTickSpeed()); break;
		case STATBOARD_SORT_HOOK: aPlayers[i].m_SortKey = Stat.GetHookAccuracyPercent(); break;
		case STATBOARD_SORT_LASTS: aPlayers[i].m_SortKey = Stat.m_Lasts; break;
		case STATBOARD_SORT_DEFAULT:
		default:
			aPlayers[i].m_SortKey = 0;
			break;
		}
	}

	std::sort(aPlayers, aPlayers + NumPlayers, [](const SPlayerEntry &a, const SPlayerEntry &b) {
		if(g_Config.m_AcMiniStatboardSort != STATBOARD_SORT_DEFAULT && a.m_SortKey != b.m_SortKey)
			return a.m_SortKey > b.m_SortKey;
		if(a.m_IsLocal != b.m_IsLocal)
			return a.m_IsLocal;
		return a.m_ClientId < b.m_ClientId;
	});

	float Scale = (float)g_Config.m_AcMiniStatboardScale / 100.0f;
	if(Scale <= 0.05f)
		Scale = 0.70f;

	float FontSize = 6.2f * Scale;
	float RowHeight = 9.5f * Scale;
	float Padding = 3.5f * Scale;

	int TotalTeamPlayers = NumPlayers;
	int MaxPlayers = std::clamp((int)g_Config.m_AcMiniStatboardMaxPlayers, 0, (int)MAX_CLIENTS);
	int DisplayCount = std::min(TotalTeamPlayers, MaxPlayers);
	bool HasMore = TotalTeamPlayers > DisplayCount;

	float TeeSize = g_Config.m_AcMiniStatboardShowTees ? (FontSize * 1.35f) : 0.0f;
	float MaxFoundNameW = 25.0f * Scale;
	for(int idx = 0; idx < DisplayCount; idx++)
	{
		int CID = aPlayers[idx].m_ClientId;
		float NameW = TextRender()->TextWidth(FontSize, GameClient()->m_aClients[CID].m_aName, -1, -1.0f);
		if(NameW > MaxFoundNameW)
			MaxFoundNameW = NameW;
	}
	float ColWidthName = MaxFoundNameW + (g_Config.m_AcMiniStatboardShowTees ? TeeSize : 0.0f) + 8.0f * Scale;

	bool ShowFreeze = (g_Config.m_AcMiniStatboardColumns & 1) != 0;
	bool ShowSave   = (g_Config.m_AcMiniStatboardColumns & 2) != 0;
	bool ShowLast   = (g_Config.m_AcMiniStatboardColumns & 4) != 0;
	bool ShowEff    = (g_Config.m_AcMiniStatboardColumns & 8) != 0;
	bool ShowDeath  = (g_Config.m_AcMiniStatboardColumns & 16) != 0;
	bool ShowNet    = (g_Config.m_AcMiniStatboardColumns & 32) != 0;
	bool ShowTime   = (g_Config.m_AcMiniStatboardColumns & 64) != 0;
	bool ShowHook   = (g_Config.m_AcMiniStatboardColumns & 128) != 0;

	auto GetColWidth = [&](int ColType) -> float {
		if(ColType == ICON_TIME)
			return 19.0f * Scale;
		if(ColType == ICON_EFF)
			return 16.5f * Scale;
		return 14.0f * Scale;
	};

	float StatsWidth = 0.0f;
	if(ShowFreeze) StatsWidth += GetColWidth(ICON_FREEZE);
	if(ShowSave)   StatsWidth += GetColWidth(ICON_SAVE);
	if(ShowDeath)  StatsWidth += GetColWidth(ICON_SKULL);
	if(ShowEff)    StatsWidth += GetColWidth(ICON_EFF);
	if(ShowNet)    StatsWidth += GetColWidth(ICON_NET);
	if(ShowTime)   StatsWidth += GetColWidth(ICON_TIME);
	if(ShowHook)   StatsWidth += GetColWidth(ICON_HOOK);
	if(ShowLast)   StatsWidth += GetColWidth(ICON_LAST);

	float TotalWidth = ColWidthName + StatsWidth + Padding * 2.0f;
	float HeaderHeight = RowHeight + 1.0f * Scale;
	float RowsHeight = (float)DisplayCount * RowHeight;
	float MoreHeight = HasMore ? (RowHeight * 0.85f) : 0.0f;
	float TotalHeight = HeaderHeight + RowsHeight + MoreHeight + Padding * 2.0f;

	m_CachedColWidthName = ColWidthName;
	m_CachedStatsWidth = StatsWidth;
	m_CachedTotalWidth = TotalWidth;
	m_CachedHeaderHeight = HeaderHeight;
	m_CachedRowsHeight = RowsHeight;
	m_CachedTotalHeight = TotalHeight;
	m_CachedTotalTeamPlayers = TotalTeamPlayers;
	m_CachedDisplayCount = DisplayCount;
	m_CachedHasMore = HasMore;

	if(HasMore)
	{
		str_format(m_aCachedMoreText, sizeof(m_aCachedMoreText), "+%d more players", TotalTeamPlayers - DisplayCount);
		m_CachedMoreTextWidth = TextRender()->TextWidth(FontSize * 0.85f, m_aCachedMoreText, -1, -1.0f);
	}

	m_vCachedRows.reserve(DisplayCount);
	for(int idx = 0; idx < DisplayCount; idx++)
	{
		int CID = aPlayers[idx].m_ClientId;
		SCachedPlayerRow Row;
		Row.m_ClientId = CID;
		Row.m_IsLocal = aPlayers[idx].m_IsLocal;
		str_copy(Row.m_aName, GameClient()->m_aClients[CID].m_aName, sizeof(Row.m_aName));
		Row.m_NameWidth = TextRender()->TextWidth(FontSize, Row.m_aName, -1, -1.0f);
		Row.m_NumCols = 0;

		if(g_Config.m_AcMiniStatboardShowTees && GameClient()->m_Snap.m_apPlayerInfos[CID] && GameClient()->m_aClients[CID].m_Active)
		{
			Row.m_HasTee = true;
			Row.m_TeeInfo = GameClient()->m_aClients[CID].m_RenderInfo;
			Row.m_TeeInfo.m_Size = TeeSize;
		}
		else
		{
			Row.m_HasTee = false;
		}

		auto AddCol = [&](int ColType, const char *pText, ColorRGBA col) {
			if(Row.m_NumCols >= (int)NUM_ICONS)
				return;
			SCachedStatCol &Col = Row.m_aCols[Row.m_NumCols++];
			Col.m_ColType = ColType;
			str_copy(Col.m_aText, pText, sizeof(Col.m_aText));
			Col.m_ColWidth = GetColWidth(ColType);
			Col.m_TextWidth = TextRender()->TextWidth(FontSize, pText, -1, -1.0f);
			Col.m_TextXOffset = (Col.m_ColWidth - Col.m_TextWidth) * 0.5f;
			Col.m_Color = col;
		};

		char aBuf[32];
		if(ShowFreeze)
		{
			str_format(aBuf, sizeof(aBuf), "%d", m_aPlayerStats[CID].m_Freezes);
			AddCol(ICON_FREEZE, aBuf, ColorRGBA(0.7f, 0.9f, 1.0f, 0.95f));
		}
		if(ShowSave)
		{
			str_format(aBuf, sizeof(aBuf), "%d", m_aPlayerStats[CID].m_Saves);
			AddCol(ICON_SAVE, aBuf, ColorRGBA(0.45f, 1.0f, 0.55f, 0.95f));
		}
		if(ShowDeath)
		{
			str_format(aBuf, sizeof(aBuf), "%d", m_aPlayerStats[CID].m_Deaths);
			AddCol(ICON_SKULL, aBuf, ColorRGBA(1.0f, 0.45f, 0.45f, 0.95f));
		}
		if(ShowEff)
		{
			int EffPct = m_aPlayerStats[CID].GetEfficiencyPercent();
			if(EffPct < 0)
				str_copy(aBuf, "-", sizeof(aBuf));
			else
				str_format(aBuf, sizeof(aBuf), "%d%%", EffPct);
			AddCol(ICON_EFF, aBuf, GetEfficiencyColor(EffPct));
		}
		if(ShowNet)
		{
			int Net = m_aPlayerStats[CID].GetNetBalance();
			ColorRGBA NetColor;
			if(Net > 0)
			{
				str_format(aBuf, sizeof(aBuf), "+%d", Net);
				NetColor = ColorRGBA(0.35f, 1.0f, 0.45f, 0.95f);
			}
			else if(Net == 0)
			{
				str_copy(aBuf, "0", sizeof(aBuf));
				NetColor = ColorRGBA(0.80f, 0.80f, 0.85f, 0.90f);
			}
			else
			{
				str_format(aBuf, sizeof(aBuf), "%d", Net);
				NetColor = ColorRGBA(1.0f, 0.35f, 0.35f, 0.95f);
			}
			AddCol(ICON_NET, aBuf, NetColor);
		}
		if(ShowTime)
		{
			int Sec = m_aPlayerStats[CID].GetFrozenSeconds(Client()->GameTickSpeed());
			if(Sec < 60)
				str_format(aBuf, sizeof(aBuf), "%ds", Sec);
			else
				str_format(aBuf, sizeof(aBuf), "%dm %02ds", Sec / 60, Sec % 60);
			AddCol(ICON_TIME, aBuf, ColorRGBA(0.5f, 0.85f, 1.0f, 0.95f));
		}
		if(ShowHook)
		{
			int HookPct = m_aPlayerStats[CID].GetHookAccuracyPercent();
			if(HookPct < 0)
				str_copy(aBuf, "-", sizeof(aBuf));
			else
				str_format(aBuf, sizeof(aBuf), "%d%%", HookPct);
			AddCol(ICON_HOOK, aBuf, ColorRGBA(1.0f, 0.85f, 0.35f, 0.95f));
		}
		if(ShowLast)
		{
			str_format(aBuf, sizeof(aBuf), "%d", m_aPlayerStats[CID].m_Lasts);
			AddCol(ICON_LAST, aBuf, ColorRGBA(1.0f, 0.85f, 0.4f, 0.95f));
		}

		m_vCachedRows.push_back(Row);
	}
	m_CachedDisplayCount = (int)m_vCachedRows.size();
}

void CRaceStats::RenderMiniStatboard()
{
	int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalClientId < 0 || LocalClientId >= MAX_CLIENTS)
		return;

	float Width = 300.0f * Graphics()->ScreenAspect();
	float Height = 300.0f;
	Graphics()->MapScreenToSize(Width, Height);

	int CurrentTick = Client()->GameTick(g_Config.m_ClDummy);
	int LocalTeam = GameClient()->m_Teams.Team(LocalClientId);

	bool NeedUpdate = !m_CacheValid ||
	                  m_StatsDirty ||
	                  (CurrentTick - m_LastCacheTick >= 50) ||
	                  (g_Config.m_AcMiniStatboardColumns != m_LastCacheColumns) ||
	                  (g_Config.m_AcMiniStatboardSort != m_LastCacheSort) ||
	                  (g_Config.m_AcMiniStatboardScale != m_LastCacheScale) ||
	                  (g_Config.m_AcMiniStatboardMaxPlayers != m_LastCacheMaxPlayers) ||
	                  (g_Config.m_AcMiniStatboardShowTees != m_LastCacheShowTees) ||
	                  (LocalTeam != m_LastCacheTeam) ||
	                  (LocalClientId != m_LastCacheLocalClientId);

	if(NeedUpdate)
	{
		UpdateMiniStatboardCache();
	}

	int DisplayCount = std::min((int)m_vCachedRows.size(), m_CachedDisplayCount);
	if(DisplayCount <= 0 || m_vCachedRows.empty())
		return;

	float Scale = (float)g_Config.m_AcMiniStatboardScale / 100.0f;
	if(Scale <= 0.05f)
		Scale = 0.70f;

	float FontSize = 6.2f * Scale;
	float IconSize = 7.0f * Scale;
	float RowHeight = 9.5f * Scale;
	float Padding = 3.5f * Scale;

	float ColWidthName = m_CachedColWidthName;
	float TotalWidth = m_CachedTotalWidth;
	float HeaderHeight = m_CachedHeaderHeight;
	float TotalHeight = m_CachedTotalHeight;

	// Posicionamiento porcentual X e Y
	float PosXPercent = 0.95f;
	float PosYPercent = 0.50f;

	if(g_Config.m_AcMiniStatboardX <= 100)
		PosXPercent = (float)g_Config.m_AcMiniStatboardX / 100.0f;
	else
		PosXPercent = (float)g_Config.m_AcMiniStatboardX / Width;

	if(g_Config.m_AcMiniStatboardY <= 100)
		PosYPercent = (float)g_Config.m_AcMiniStatboardY / 100.0f;
	else
		PosYPercent = (float)g_Config.m_AcMiniStatboardY / Height;

	PosXPercent = std::clamp(PosXPercent, 0.0f, 1.0f);
	PosYPercent = std::clamp(PosYPercent, 0.0f, 1.0f);

	float BaseX = (Width - TotalWidth) * PosXPercent;
	float BaseY = (Height - TotalHeight) * PosYPercent;

	BaseX = std::clamp(BaseX, 2.0f, Width - TotalWidth - 2.0f);
	BaseY = std::clamp(BaseY, 2.0f, Height - TotalHeight - 2.0f);

	// Fondo redondeado elegante con opacidad configurable
	float Alpha = (float)g_Config.m_AcMiniStatboardOpacity / 100.0f;
	if(Alpha > 0.0f)
	{
		Graphics()->DrawRect(BaseX, BaseY, TotalWidth, TotalHeight, ColorRGBA(0.04f, 0.04f, 0.06f, Alpha * 0.70f), IGraphics::CORNER_ALL, 4.0f * Scale);
		Graphics()->DrawRect(BaseX, BaseY, TotalWidth, HeaderHeight + Padding, ColorRGBA(0.12f, 0.12f, 0.18f, Alpha * 0.50f), IGraphics::CORNER_T, 4.0f * Scale);
	}

	auto GetColWidth = [&](int ColType) -> float {
		if(ColType == ICON_TIME)
			return 19.0f * Scale;
		if(ColType == ICON_EFF)
			return 16.5f * Scale;
		return 14.0f * Scale;
	};

	bool ShowFreeze = (g_Config.m_AcMiniStatboardColumns & 1) != 0;
	bool ShowSave   = (g_Config.m_AcMiniStatboardColumns & 2) != 0;
	bool ShowLast   = (g_Config.m_AcMiniStatboardColumns & 4) != 0;
	bool ShowEff    = (g_Config.m_AcMiniStatboardColumns & 8) != 0;
	bool ShowDeath  = (g_Config.m_AcMiniStatboardColumns & 16) != 0;
	bool ShowNet    = (g_Config.m_AcMiniStatboardColumns & 32) != 0;
	bool ShowTime   = (g_Config.m_AcMiniStatboardColumns & 64) != 0;
	bool ShowHook   = (g_Config.m_AcMiniStatboardColumns & 128) != 0;

	// Dibujado de Iconos de Cabecera agrupados (Single Draw Call con Texture Atlas)
	float CurrX = BaseX + Padding;
	float HeaderY = BaseY + Padding + (HeaderHeight - IconSize) / 2.0f;

	if(m_IconAtlasTexture.IsValid())
	{
		Graphics()->TextureSet(m_IconAtlasTexture);
		Graphics()->QuadsBegin();

		auto DrawAtlasIcon = [&](int ColType, float x, float y, float size, ColorRGBA col) {
			int c = ColType % 3;
			int r = ColType / 3;
			float U0 = (float)c / 3.0f;
			float V0 = (float)r / 3.0f;
			float U1 = (float)(c + 1) / 3.0f;
			float V1 = (float)(r + 1) / 3.0f;

			Graphics()->SetColor(col);
			Graphics()->QuadsSetSubset(U0, V0, U1, V1);
			IGraphics::CQuadItem Quad(x, y, size, size);
			Graphics()->QuadsDrawTL(&Quad, 1);
		};

		DrawAtlasIcon(ICON_USER, CurrX + 2.0f * Scale, HeaderY, IconSize, ColorRGBA(1.0f, 1.0f, 1.0f, 0.9f));
		float HeaderCurrX = BaseX + Padding + ColWidthName;

		auto DrawHeaderCol = [&](int ColType, ColorRGBA col) {
			float W = GetColWidth(ColType);
			DrawAtlasIcon(ColType, HeaderCurrX + (W - IconSize) / 2.0f, HeaderY, IconSize, col);
			HeaderCurrX += W;
		};

		if(ShowFreeze) DrawHeaderCol(ICON_FREEZE, ColorRGBA(0.5f, 0.85f, 1.0f, 1.0f));
		if(ShowSave)   DrawHeaderCol(ICON_SAVE, ColorRGBA(0.4f, 1.0f, 0.5f, 1.0f));
		if(ShowDeath)  DrawHeaderCol(ICON_SKULL, ColorRGBA(0.95f, 0.35f, 0.35f, 1.0f));
		if(ShowEff)    DrawHeaderCol(ICON_EFF, ColorRGBA(0.95f, 0.75f, 0.3f, 1.0f));
		if(ShowNet)    DrawHeaderCol(ICON_NET, ColorRGBA(0.45f, 0.90f, 0.95f, 1.0f));
		if(ShowTime)   DrawHeaderCol(ICON_TIME, ColorRGBA(0.55f, 0.80f, 1.0f, 1.0f));
		if(ShowHook)   DrawHeaderCol(ICON_HOOK, ColorRGBA(1.0f, 0.85f, 0.35f, 1.0f));
		if(ShowLast)   DrawHeaderCol(ICON_LAST, ColorRGBA(1.0f, 0.75f, 0.3f, 1.0f));

		Graphics()->QuadsEnd();
		Graphics()->QuadsSetSubset(0, 0, 1, 1);
	}
	else
	{
		auto DrawIcon = [&](IGraphics::CTextureHandle Tex, float x, float y, float size, ColorRGBA col) {
			if(!Tex.IsValid())
				return;
			Graphics()->TextureSet(Tex);
			Graphics()->QuadsBegin();
			Graphics()->SetColor(col);
			IGraphics::CQuadItem Quad(x, y, size, size);
			Graphics()->QuadsDrawTL(&Quad, 1);
			Graphics()->QuadsEnd();
		};

		DrawIcon(m_aTextures[ICON_USER], CurrX + 2.0f * Scale, HeaderY, IconSize, ColorRGBA(1.0f, 1.0f, 1.0f, 0.9f));
		float HeaderCurrX = BaseX + Padding + ColWidthName;
		auto DrawHeaderCol = [&](IGraphics::CTextureHandle Tex, int ColType, ColorRGBA col) {
			float W = GetColWidth(ColType);
			DrawIcon(Tex, HeaderCurrX + (W - IconSize) / 2.0f, HeaderY, IconSize, col);
			HeaderCurrX += W;
		};

		if(ShowFreeze) DrawHeaderCol(m_aTextures[ICON_FREEZE], ICON_FREEZE, ColorRGBA(0.5f, 0.85f, 1.0f, 1.0f));
		if(ShowSave)   DrawHeaderCol(m_aTextures[ICON_SAVE], ICON_SAVE, ColorRGBA(0.4f, 1.0f, 0.5f, 1.0f));
		if(ShowDeath)  DrawHeaderCol(m_aTextures[ICON_SKULL], ICON_SKULL, ColorRGBA(0.95f, 0.35f, 0.35f, 1.0f));
		if(ShowEff)    DrawHeaderCol(m_aTextures[ICON_EFF], ICON_EFF, ColorRGBA(0.95f, 0.75f, 0.3f, 1.0f));
		if(ShowNet)    DrawHeaderCol(m_aTextures[ICON_NET], ICON_NET, ColorRGBA(0.45f, 0.90f, 0.95f, 1.0f));
		if(ShowTime)   DrawHeaderCol(m_aTextures[ICON_TIME], ICON_TIME, ColorRGBA(0.55f, 0.80f, 1.0f, 1.0f));
		if(ShowHook)   DrawHeaderCol(m_aTextures[ICON_HOOK], ICON_HOOK, ColorRGBA(1.0f, 0.85f, 0.35f, 1.0f));
		if(ShowLast)   DrawHeaderCol(m_aTextures[ICON_LAST], ICON_LAST, ColorRGBA(1.0f, 0.75f, 0.3f, 1.0f));
	}

	// Filas de Jugadores con Animacion Fluida Estilo F1
	float BaseRowY = BaseY + Padding + HeaderHeight;
	float RowY = BaseRowY;
	float DeltaTime = Client()->RenderFrameTime();
	float AnimSpeed = (float)std::clamp((int)g_Config.m_AcMiniStatboardAnimSpeed, 3, 30);
	if(AnimSpeed <= 0.0f)
		AnimSpeed = 10.0f;

	// Actualizar interpolacion de movimiento de cada jugador hacia su puesto objetivo
	for(int idx = 0; idx < DisplayCount; idx++)
	{
		if(idx >= (int)m_vCachedRows.size())
			break;
		int CID = m_vCachedRows[idx].m_ClientId;
		if(CID < 0 || CID >= MAX_CLIENTS)
			continue;
		SRowAnimState &Anim = m_aRowAnim[CID];
		float TargetY = BaseRowY + (float)idx * RowHeight;

		if(Anim.m_VisualY < 0.0f || !g_Config.m_AcMiniStatboardAnim)
		{
			Anim.m_VisualY = TargetY;
			Anim.m_PrevRank = idx;
			Anim.m_RankChangeTimer = 0.0f;
			Anim.m_RankChangeDir = 0;
		}
		else
		{
			if(Anim.m_PrevRank >= 0 && Anim.m_PrevRank != idx)
			{
				if(idx < Anim.m_PrevRank)
				{
					Anim.m_RankChangeDir = 1;
					Anim.m_RankChangeTimer = 0.45f;
				}
				else
				{
					Anim.m_RankChangeDir = -1;
					Anim.m_RankChangeTimer = 0.35f;
				}
				Anim.m_PrevRank = idx;
			}

			float Diff = TargetY - Anim.m_VisualY;
			if(std::abs(Diff) < 0.20f)
			{
				Anim.m_VisualY = TargetY;
			}
			else
			{
				float Factor = std::clamp(DeltaTime * AnimSpeed, 0.0f, 1.0f);
				Anim.m_VisualY += Diff * Factor;
			}

			if(Anim.m_RankChangeTimer > 0.0f)
				Anim.m_RankChangeTimer = std::max(0.0f, Anim.m_RankChangeTimer - DeltaTime);
		}
	}

	float TeeSize = g_Config.m_AcMiniStatboardShowTees ? (FontSize * 1.35f) : 0.0f;

	// Batching de lineas cebra en una unica llamada de dibujo GPU (Single Draw Call)
	IGraphics::CQuadItem aZebraQuads[MAX_CLIENTS];
	int NumZebraQuads = 0;
	for(int idx = 1; idx < DisplayCount; idx += 2)
	{
		if(idx >= (int)m_vCachedRows.size())
			break;
		int CID = m_vCachedRows[idx].m_ClientId;
		if(CID < 0 || CID >= MAX_CLIENTS)
			continue;
		SRowAnimState &Anim = m_aRowAnim[CID];
		float CurrentRowY = (Anim.m_VisualY >= 0.0f) ? Anim.m_VisualY : (BaseRowY + (float)idx * RowHeight);
		aZebraQuads[NumZebraQuads++] = IGraphics::CQuadItem(BaseX + 1.0f * Scale, CurrentRowY, TotalWidth - 2.0f * Scale, RowHeight);
	}
	if(NumZebraQuads > 0)
	{
		Graphics()->TextureClear();
		Graphics()->QuadsBegin();
		Graphics()->SetColor(ColorRGBA(1.0f, 1.0f, 1.0f, 0.04f));
		Graphics()->QuadsDrawTL(aZebraQuads, NumZebraQuads);
		Graphics()->QuadsEnd();
	}

	for(int idx = 0; idx < DisplayCount; idx++)
	{
		if(idx >= (int)m_vCachedRows.size())
			break;
		const SCachedPlayerRow &Row = m_vCachedRows[idx];
		int CID = Row.m_ClientId;
		if(CID < 0 || CID >= MAX_CLIENTS)
			continue;
		bool IsMe = Row.m_IsLocal;
		SRowAnimState &Anim = m_aRowAnim[CID];
		float CurrentRowY = (Anim.m_VisualY >= 0.0f) ? Anim.m_VisualY : (BaseRowY + (float)idx * RowHeight);

		// Resaltar al jugador local
		if(IsMe)
		{
			Graphics()->DrawRect(BaseX + 1.0f * Scale, CurrentRowY, TotalWidth - 2.0f * Scale, RowHeight, ColorRGBA(1.0f, 0.85f, 0.2f, 0.14f), IGraphics::CORNER_ALL, 2.0f * Scale);
		}

		// Efecto F1 de cambio de posicion (adelantamiento)
		if(Anim.m_RankChangeTimer > 0.0f)
		{
			if(Anim.m_RankChangeDir == 1)
			{
				float FlashAlpha = 0.28f * (Anim.m_RankChangeTimer / 0.45f);
				Graphics()->DrawRect(BaseX + 1.0f * Scale, CurrentRowY, TotalWidth - 2.0f * Scale, RowHeight, ColorRGBA(0.20f, 1.0f, 0.40f, FlashAlpha), IGraphics::CORNER_ALL, 2.0f * Scale);
			}
			else if(Anim.m_RankChangeDir == -1)
			{
				float FlashAlpha = 0.18f * (Anim.m_RankChangeTimer / 0.35f);
				Graphics()->DrawRect(BaseX + 1.0f * Scale, CurrentRowY, TotalWidth - 2.0f * Scale, RowHeight, ColorRGBA(1.0f, 0.40f, 0.20f, FlashAlpha), IGraphics::CORNER_ALL, 2.0f * Scale);
			}
		}

		CurrX = BaseX + Padding;
		float TextY = CurrentRowY + (RowHeight - FontSize) / 2.0f;

		// Mini Tee cacheado (cero pointer chasing en render loop)
		if(g_Config.m_AcMiniStatboardShowTees)
		{
			if(Row.m_HasTee)
			{
				const vec2 TeePos = vec2(CurrX + TeeSize * 0.5f, CurrentRowY + RowHeight * 0.5f);
				RenderTools()->RenderTee(CAnimState::GetIdle(), &Row.m_TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeePos);
			}
			CurrX += TeeSize;
		}

		float NameX = CurrX + 2.0f * Scale;

		// Nombre de Jugador completo en 1 sola linea
		if(IsMe)
			TextRender()->TextColor(1.0f, 0.9f, 0.3f, 1.0f);
		else
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f);

		TextRender()->Text(NameX, TextY, FontSize, Row.m_aName, -1.0f);
		CurrX = BaseX + Padding + ColWidthName;

		// Dibujado directo de columnas con offset X precalculado en cache
		for(int c = 0; c < Row.m_NumCols; c++)
		{
			const SCachedStatCol &Col = Row.m_aCols[c];
			TextRender()->TextColor(Col.m_Color);
			TextRender()->Text(CurrX + Col.m_TextXOffset, TextY, FontSize, Col.m_aText, -1.0f);
			CurrX += Col.m_ColWidth;
		}

		RowY += RowHeight;
	}

	// +N more players (cacheados)
	if(m_CachedHasMore)
	{
		TextRender()->TextColor(0.7f, 0.7f, 0.7f, 0.7f);
		TextRender()->Text(BaseX + (TotalWidth - m_CachedMoreTextWidth) / 2.0f, RowY + 1.0f * Scale, FontSize * 0.85f, m_aCachedMoreText, -1.0f);
	}

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}
