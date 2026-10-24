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
#include <generated/protocol.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static const float HAMMER_MAX_DIST = 110.0f;
static const char *RACESTATS_HISTORY_FILE = "tclient_racestats_history.txt";

void CRaceStats::OnInit()
{
	m_Active = false;
	m_LastLocalTeam = -1;
	m_RunStartTick = 0;

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
	m_aTextures[ICON_LAST]   = LoadEmbeddedIcon(gs_aRaceStatIcon_LAST, sizeof(gs_aRaceStatIcon_LAST), "racestat_last");
	m_aTextures[ICON_EFF]    = LoadEmbeddedIcon(gs_aRaceStatIcon_EFF, sizeof(gs_aRaceStatIcon_EFF), "racestat_eff");
	m_aTextures[ICON_SKULL]  = LoadEmbeddedIcon(gs_aRaceStatIcon_SKULL, sizeof(gs_aRaceStatIcon_SKULL), "racestat_skull");

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

void CRaceStats::OnConsoleInit()
{
	Console()->Register("+compact_statboard", "", CFGFLAG_CLIENT, ConKeyCompactStatboard, this, "Toggle or hold compact statboard");
	Console()->Register("tc_save_racestats", "", CFGFLAG_CLIENT, ConSaveRaceStats, this, "Save current RaceStats overlay statistics to history");
}

void CRaceStats::OnReset()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_aPlayerStats[i].Reset();
	}
	m_RoundDeaths = 0;
	m_LastLocalTeam = -1;
	m_RunStartTick = 0;
	m_Active = false;
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
	}
	m_RoundDeaths = 0;
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

	SaveHistoryToFile();

	if(NotifyChat)
	{
		char aMsg[128];
		str_format(aMsg, sizeof(aMsg), "RaceStats guardadas! (%s - Run: %s)", Entry.m_aMapName, Entry.m_aRunTime);
		GameClient()->m_Chat.Echo(aMsg);
	}

	return true;
}

void CRaceStats::DeleteSavedEntry(int Index)
{
	if(Index < 0 || Index >= (int)m_vSavedEntries.size())
		return;
	m_vSavedEntries.erase(m_vSavedEntries.begin() + Index);
	SaveHistoryToFile();
}

void CRaceStats::ClearSavedHistory()
{
	m_vSavedEntries.clear();
	SaveHistoryToFile();
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
			str_format(aLine, sizeof(aLine), "PLAYER\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
				Pl.m_aName, Pl.m_aSkinName, Pl.m_CustomColors ? 1 : 0, Pl.m_ColorBody, Pl.m_ColorFeet,
				Pl.m_Freezes, Pl.m_Saves, Pl.m_Lasts, Pl.m_EfficiencyPercent, Pl.m_IsLocal ? 1 : 0);
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
			bool IsLocalKill = (pMsg->m_Victim == LocalClientId);
			bool IsTeammateKill = (LocalTeam > 0 && GameClient()->m_Teams.Team(pMsg->m_Victim) == LocalTeam);

			if(g_Config.m_TcStatboardResetOnTeamKill && (IsLocalKill || IsTeammateKill))
			{
				if(g_Config.m_TcStatboardAutoSave)
					SaveCurrentStats("Auto (/kill)", true, false);
				ResetTeamStats();
			}
		}
	}
	else if(MsgType == NETMSGTYPE_SV_KILLMSGTEAM)
	{
		CNetMsg_Sv_KillMsgTeam *pMsg = (CNetMsg_Sv_KillMsgTeam *)pRawMsg;
		if(g_Config.m_TcStatboardResetOnTeamKill && pMsg->m_Team == LocalTeam)
		{
			if(g_Config.m_TcStatboardAutoSave)
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
		if(g_Config.m_TcStatboardResetOnTeamKill)
		{
			if(g_Config.m_TcStatboardAutoSave)
				SaveCurrentStats("Auto (/team)", true, false);
			ResetTeamStats();
		}
	}
	m_LastLocalTeam = CurrentTeam;

	// Detección de Freezes y Saves
	int TeamActivePlayers = 0;
	int TeamUnfrozenPlayers = 0;
	int LastUnfrozenCID = -1;

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameClient()->m_Snap.m_apPlayerInfos[i] || !GameClient()->m_Snap.m_aCharacters[i].m_Active)
		{
			m_aPlayerStats[i].m_WasFrozen = false;
			continue;
		}

		const auto &ClientData = GameClient()->m_aClients[i];
		const auto &Char = GameClient()->m_Snap.m_aCharacters[i];

		bool IsFrozen = (ClientData.m_FreezeEnd > 0 || ClientData.m_DeepFrozen);

		if(GameClient()->m_Teams.Team(i) == CurrentTeam)
		{
			TeamActivePlayers++;
			if(!IsFrozen)
			{
				TeamUnfrozenPlayers++;
				LastUnfrozenCID = i;
			}
		}

		// Seguimiento de ataque de martillo
		if(Char.m_Cur.m_Weapon == WEAPON_HAMMER && (Char.m_Cur.m_AttackTick != m_aPlayerStats[i].m_LastAttackTick))
		{
			m_aPlayerStats[i].m_LastAttackTick = Char.m_Cur.m_AttackTick;
			m_aPlayerStats[i].m_LastHammerTick = CurrentTick;
			m_aPlayerStats[i].m_LastHammerPos = vec2(Char.m_Cur.m_X, Char.m_Cur.m_Y);
		}

		// Transición: Entró en congelación
		if(IsFrozen && !m_aPlayerStats[i].m_WasFrozen)
		{
			m_aPlayerStats[i].m_Freezes++;
			m_aPlayerStats[i].m_LastFreezeTick = CurrentTick;
		}

		// Transición: Se descongeló -> Comprobar si un compañero lo salvó con martillo
		if(!IsFrozen && m_aPlayerStats[i].m_WasFrozen)
		{
			vec2 VictimPos = vec2(Char.m_Cur.m_X, Char.m_Cur.m_Y);
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
				if(TickDiff <= 10)
				{
					vec2 AttackerPos = vec2(GameClient()->m_Snap.m_aCharacters[j].m_Cur.m_X, GameClient()->m_Snap.m_aCharacters[j].m_Cur.m_Y);
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
			}
		}

		m_aPlayerStats[i].m_WasFrozen = IsFrozen;
	}

	// Detección de Lasts (único sobreviviente descongelado en un equipo de >= 2 jugadores)
	if(TeamActivePlayers >= 2 && TeamUnfrozenPlayers == 1 && LastUnfrozenCID != -1)
	{
		if(!m_aPlayerStats[LastUnfrozenCID].m_WasSoleSurvivor)
		{
			m_aPlayerStats[LastUnfrozenCID].m_Lasts++;
			m_aPlayerStats[LastUnfrozenCID].m_WasSoleSurvivor = true;
		}
	}
	else if(TeamUnfrozenPlayers != 1)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameClient()->m_Teams.Team(i) == CurrentTeam)
				m_aPlayerStats[i].m_WasSoleSurvivor = false;
		}
	}

	if(g_Config.m_TcMiniStatboard || m_Active)
	{
		RenderMiniStatboard();
	}
}

void CRaceStats::RenderMiniStatboard()
{
	float Width = 300.0f * Graphics()->ScreenAspect();
	float Height = 300.0f;
	Graphics()->MapScreenToSize(Width, Height);

	float Scale = (float)g_Config.m_TcMiniStatboardScale / 100.0f;
	if(Scale <= 0.05f)
		Scale = 0.70f;

	float FontSize = 6.2f * Scale;
	float IconSize = 7.0f * Scale;
	float RowHeight = 9.5f * Scale;
	float ColWidthStat = 14.0f * Scale;
	float Padding = 3.5f * Scale;

	int LocalClientId = GameClient()->m_Snap.m_LocalClientId;
	int LocalTeam = GameClient()->m_Teams.Team(LocalClientId);

	struct SPlayerEntry
	{
		int m_ClientId;
		bool m_IsLocal;
	};
	std::vector<SPlayerEntry> vPlayers;

	if(GameClient()->m_Snap.m_apPlayerInfos[LocalClientId])
		vPlayers.push_back({LocalClientId, true});

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(i == LocalClientId || !GameClient()->m_Snap.m_apPlayerInfos[i])
			continue;
		if(LocalTeam > 0 && GameClient()->m_Teams.Team(i) != LocalTeam)
			continue;
		vPlayers.push_back({i, false});
	}

	if(vPlayers.empty())
		return;

	int TotalTeamPlayers = (int)vPlayers.size();
	int MaxPlayers = std::clamp((int)g_Config.m_TcMiniStatboardMaxPlayers, 0, (int)MAX_CLIENTS);
	int DisplayCount = std::min(TotalTeamPlayers, MaxPlayers);
	bool HasMore = TotalTeamPlayers > DisplayCount;

	float TeeSize = FontSize * 1.35f;
	float MaxFoundNameW = 25.0f * Scale;
	for(int idx = 0; idx < DisplayCount; idx++)
	{
		int CID = vPlayers[idx].m_ClientId;
		float NameW = TextRender()->TextWidth(FontSize, GameClient()->m_aClients[CID].m_aName, -1, -1.0f);
		if(NameW > MaxFoundNameW)
			MaxFoundNameW = NameW;
	}
	float ColWidthName = MaxFoundNameW + TeeSize + 8.0f * Scale;

	bool ShowFreeze = (g_Config.m_TcMiniStatboardColumns & 1) != 0;
	bool ShowSave   = (g_Config.m_TcMiniStatboardColumns & 2) != 0;
	bool ShowLast   = (g_Config.m_TcMiniStatboardColumns & 4) != 0;
	bool ShowEff    = (g_Config.m_TcMiniStatboardColumns & 8) != 0;

	int NumStatCols = (ShowFreeze ? 1 : 0) + (ShowSave ? 1 : 0) + (ShowLast ? 1 : 0) + (ShowEff ? 1 : 0);
	float TotalWidth = ColWidthName + (float)NumStatCols * ColWidthStat + Padding * 2.0f;

	float HeaderHeight = RowHeight + 1.0f * Scale;
	float RowsHeight = (float)DisplayCount * RowHeight;
	float MoreHeight = HasMore ? (RowHeight * 0.85f) : 0.0f;
	float TotalHeight = HeaderHeight + RowsHeight + MoreHeight + Padding * 2.0f;

	// Posicionamiento porcentual X e Y (0% = izquierda/arriba, 100% = derecha/abajo)
	float PosXPercent = 0.95f;
	float PosYPercent = 0.50f;

	if(g_Config.m_TcMiniStatboardX <= 100)
		PosXPercent = (float)g_Config.m_TcMiniStatboardX / 100.0f;
	else
		PosXPercent = (float)g_Config.m_TcMiniStatboardX / Width;

	if(g_Config.m_TcMiniStatboardY <= 100)
		PosYPercent = (float)g_Config.m_TcMiniStatboardY / 100.0f;
	else
		PosYPercent = (float)g_Config.m_TcMiniStatboardY / Height;

	PosXPercent = std::clamp(PosXPercent, 0.0f, 1.0f);
	PosYPercent = std::clamp(PosYPercent, 0.0f, 1.0f);

	float BaseX = (Width - TotalWidth) * PosXPercent;
	float BaseY = (Height - TotalHeight) * PosYPercent;

	BaseX = std::clamp(BaseX, 2.0f, Width - TotalWidth - 2.0f);
	BaseY = std::clamp(BaseY, 2.0f, Height - TotalHeight - 2.0f);

	// Fondo redondeado elegante con opacidad configurable
	float Alpha = (float)g_Config.m_TcMiniStatboardOpacity / 100.0f;
	if(Alpha > 0.0f)
	{
		// Panel principal oscuro
		Graphics()->DrawRect(BaseX, BaseY, TotalWidth, TotalHeight, ColorRGBA(0.04f, 0.04f, 0.06f, Alpha * 0.70f), IGraphics::CORNER_ALL, 4.0f * Scale);
		// Cabecera destacada
		Graphics()->DrawRect(BaseX, BaseY, TotalWidth, HeaderHeight + Padding, ColorRGBA(0.12f, 0.12f, 0.18f, Alpha * 0.50f), IGraphics::CORNER_T, 4.0f * Scale);
	}

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

	// Dibujar Iconos de Cabecera
	float CurrX = BaseX + Padding;
	float HeaderY = BaseY + Padding + (HeaderHeight - IconSize) / 2.0f;

	// Icono Jugador
	DrawIcon(m_aTextures[ICON_USER], CurrX + 2.0f * Scale, HeaderY, IconSize, ColorRGBA(1.0f, 1.0f, 1.0f, 0.9f));
	CurrX += ColWidthName;

	if(ShowFreeze)
	{
		DrawIcon(m_aTextures[ICON_FREEZE], CurrX + (ColWidthStat - IconSize) / 2.0f, HeaderY, IconSize, ColorRGBA(0.5f, 0.85f, 1.0f, 1.0f));
		CurrX += ColWidthStat;
	}
	if(ShowSave)
	{
		DrawIcon(m_aTextures[ICON_SAVE], CurrX + (ColWidthStat - IconSize) / 2.0f, HeaderY, IconSize, ColorRGBA(0.4f, 1.0f, 0.5f, 1.0f));
		CurrX += ColWidthStat;
	}
	if(ShowLast)
	{
		DrawIcon(m_aTextures[ICON_LAST], CurrX + (ColWidthStat - IconSize) / 2.0f, HeaderY, IconSize, ColorRGBA(1.0f, 0.75f, 0.3f, 1.0f));
		CurrX += ColWidthStat;
	}
	if(ShowEff)
	{
		DrawIcon(m_aTextures[ICON_EFF], CurrX + (ColWidthStat - IconSize) / 2.0f, HeaderY, IconSize, ColorRGBA(0.9f, 0.5f, 1.0f, 1.0f));
		CurrX += ColWidthStat;
	}

	// Filas de Jugadores
	float RowY = BaseY + Padding + HeaderHeight;

	for(int idx = 0; idx < DisplayCount; idx++)
	{
		int CID = vPlayers[idx].m_ClientId;
		bool IsMe = vPlayers[idx].m_IsLocal;

		// Efecto cebra suave
		if(idx % 2 == 1)
		{
			Graphics()->DrawRect(BaseX + 1.0f * Scale, RowY, TotalWidth - 2.0f * Scale, RowHeight, ColorRGBA(1.0f, 1.0f, 1.0f, 0.04f), IGraphics::CORNER_NONE, 0.0f);
		}
		// Resaltar al jugador local
		if(IsMe)
		{
			Graphics()->DrawRect(BaseX + 1.0f * Scale, RowY, TotalWidth - 2.0f * Scale, RowHeight, ColorRGBA(1.0f, 0.85f, 0.2f, 0.14f), IGraphics::CORNER_ALL, 2.0f * Scale);
		}

		CurrX = BaseX + Padding;
		float TextY = RowY + (RowHeight - FontSize) / 2.0f;

		// Mini Tee
		CTeeRenderInfo TeeInfo = GameClient()->m_aClients[CID].m_RenderInfo;
		TeeInfo.m_Size = FontSize * 1.35f;
		const vec2 TeePos = vec2(CurrX + TeeInfo.m_Size * 0.5f, RowY + RowHeight * 0.5f);
		RenderTools()->RenderTee(CAnimState::GetIdle(), &TeeInfo, EMOTE_NORMAL, vec2(1.0f, 0.0f), TeePos);

		float NameX = CurrX + TeeInfo.m_Size + 2.0f * Scale;

		// Nombre de Jugador completo en 1 sola línea
		if(IsMe)
			TextRender()->TextColor(1.0f, 0.9f, 0.3f, 1.0f);
		else
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f);

		TextRender()->Text(NameX, TextY, FontSize, GameClient()->m_aClients[CID].m_aName, -1.0f);
		CurrX += ColWidthName;

		// Números bajo cada columna
		auto DrawStatNum = [&](const char *pText, ColorRGBA col) {
			float TextW = TextRender()->TextWidth(FontSize, pText, -1, -1.0f);
			float StatX = CurrX + (ColWidthStat - TextW) / 2.0f;
			TextRender()->TextColor(col);
			TextRender()->Text(StatX, TextY, FontSize, pText, -1.0f);
			CurrX += ColWidthStat;
		};

		char aBuf[32];
		if(ShowFreeze)
		{
			str_format(aBuf, sizeof(aBuf), "%d", m_aPlayerStats[CID].m_Freezes);
			DrawStatNum(aBuf, ColorRGBA(0.7f, 0.9f, 1.0f, 0.95f));
		}
		if(ShowSave)
		{
			str_format(aBuf, sizeof(aBuf), "%d", m_aPlayerStats[CID].m_Saves);
			DrawStatNum(aBuf, ColorRGBA(0.45f, 1.0f, 0.55f, 0.95f));
		}
		if(ShowLast)
		{
			str_format(aBuf, sizeof(aBuf), "%d", m_aPlayerStats[CID].m_Lasts);
			DrawStatNum(aBuf, ColorRGBA(1.0f, 0.85f, 0.4f, 0.95f));
		}
		if(ShowEff)
		{
			str_format(aBuf, sizeof(aBuf), "%d", m_aPlayerStats[CID].GetEfficiencyPercent());
			DrawStatNum(aBuf, ColorRGBA(0.95f, 0.65f, 1.0f, 0.95f));
		}

		RowY += RowHeight;
	}

	// +N more players
	if(HasMore)
	{
		char aMore[32];
		str_format(aMore, sizeof(aMore), "+%d more players", TotalTeamPlayers - DisplayCount);
		TextRender()->TextColor(0.7f, 0.7f, 0.7f, 0.7f);
		float MoreW = TextRender()->TextWidth(FontSize * 0.85f, aMore, -1, -1.0f);
		TextRender()->Text(BaseX + (TotalWidth - MoreW) / 2.0f, RowY + 1.0f * Scale, FontSize * 0.85f, aMore, -1.0f);
	}

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}
