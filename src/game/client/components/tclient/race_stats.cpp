#include "race_stats.h"

#include <engine/graphics.h>
#include <engine/textrender.h>
#include <game/client/gameclient.h>
#include <generated/protocol.h>

#include <cmath>

static const float HAMMER_MAX_DIST = 64.0f;

void CRaceStats::OnReset()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_aPlayerStats[i].Reset();
	}
	m_RoundDeaths = 0;
}

void CRaceStats::OnMessage(int MsgType, void *pRawMsg)
{
	if(MsgType == NETMSGTYPE_SV_KILLMSG)
	{
		CNetMsg_Sv_KillMsg *pMsg = (CNetMsg_Sv_KillMsg *)pRawMsg;
		if(pMsg->m_Victim >= 0 && pMsg->m_Victim < MAX_CLIENTS)
		{
			m_aPlayerStats[pMsg->m_Victim].m_Deaths++;
			if(pMsg->m_Victim == GameClient()->m_Snap.m_LocalClientId)
			{
				m_RoundDeaths++;
				m_SessionDeaths++;
			}
		}
	}
}

void CRaceStats::CheckTeamWipe()
{
	if(!g_Config.m_TcStatboardResetOnTeamKill)
		return;

	int LocalTeam = GameClient()->m_Teams.Team(GameClient()->m_Snap.m_LocalClientId);
	if(LocalTeam <= 0)
		return;

	bool AnyAlive = false;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(GameClient()->m_Snap.m_apPlayerInfos[i] && GameClient()->m_Teams.Team(i) == LocalTeam)
		{
			if(GameClient()->m_Snap.m_aCharacters[i].m_Active)
			{
				AnyAlive = true;
				break;
			}
		}
	}

	if(!AnyAlive && m_RoundDeaths > 0)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(GameClient()->m_Teams.Team(i) == LocalTeam)
				m_aPlayerStats[i].Reset();
		}
		m_RoundDeaths = 0;
	}
}

void CRaceStats::OnRender()
{
	if(!GameClient()->m_Snap.m_pGameInfoObj)
		return;

	CheckTeamWipe();

	int CurrentTick = Client()->GameTick(g_Config.m_ClDummy);

	// Detección de Freezes, Saves y Lasts
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

		// Seguimiento de ataque de martillo
		if(Char.m_Cur.m_Weapon == WEAPON_HAMMER && (Char.m_Cur.m_AttackTick != m_aPlayerStats[i].m_LastAttackTick))
		{
			m_aPlayerStats[i].m_LastAttackTick = Char.m_Cur.m_AttackTick;
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

				if(std::abs(CurrentTick - m_aPlayerStats[j].m_LastAttackTick) <= 3)
				{
					vec2 AttackerPos = vec2(GameClient()->m_Snap.m_aCharacters[j].m_Cur.m_X, GameClient()->m_Snap.m_aCharacters[j].m_Cur.m_Y);
					float Dist = distance(VictimPos, AttackerPos);
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

	if(g_Config.m_TcMiniStatboard)
	{
		RenderMiniStatboard();
	}
}

void CRaceStats::RenderMiniStatboard()
{
	float ScreenX = (float)g_Config.m_TcMiniStatboardX;
	float ScreenY = (float)g_Config.m_TcMiniStatboardY;
	float Scale = (float)g_Config.m_TcMiniStatboardScale / 100.0f;
	float FontSize = 11.0f * Scale;
	float RowHeight = FontSize + 4.0f;
	float Alpha = (float)g_Config.m_TcMiniStatboardOpacity / 100.0f;

	Graphics()->MapScreen({0.0f, 0.0f, (float)Graphics()->ScreenWidth(), (float)Graphics()->ScreenHeight()});

	char aHeader[128] = "Player       ";
	if(g_Config.m_TcMiniStatboardColumns & 1)  str_append(aHeader, " Frz");
	if(g_Config.m_TcMiniStatboardColumns & 2)  str_append(aHeader, " Sav");
	if(g_Config.m_TcMiniStatboardColumns & 4)  str_append(aHeader, " Lst");
	if(g_Config.m_TcMiniStatboardColumns & 8)  str_append(aHeader, "  Eff");
	if(g_Config.m_TcMiniStatboardColumns & 16) str_append(aHeader, " Dth");

	TextRender()->TextColor(0.85f, 0.85f, 0.85f, Alpha);
	TextRender()->Text(ScreenX, ScreenY, FontSize, aHeader);
	ScreenY += RowHeight;

	int Count = 0;
	int LocalTeam = GameClient()->m_Teams.Team(GameClient()->m_Snap.m_LocalClientId);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(!GameClient()->m_Snap.m_apPlayerInfos[i])
			continue;

		if(LocalTeam > 0 && GameClient()->m_Teams.Team(i) != LocalTeam)
			continue;

		if(Count >= g_Config.m_TcMiniStatboardMaxPlayers)
			break;

		char aRow[128];
		char aName[16];
		str_copy(aName, GameClient()->m_aClients[i].m_aName);
		str_format(aRow, sizeof(aRow), "%-12.12s", aName);

		char aCol[16];
		if(g_Config.m_TcMiniStatboardColumns & 1)
		{
			str_format(aCol, sizeof(aCol), " %3d", m_aPlayerStats[i].m_Freezes);
			str_append(aRow, aCol);
		}
		if(g_Config.m_TcMiniStatboardColumns & 2)
		{
			str_format(aCol, sizeof(aCol), " %3d", m_aPlayerStats[i].m_Saves);
			str_append(aRow, aCol);
		}
		if(g_Config.m_TcMiniStatboardColumns & 4)
		{
			str_format(aCol, sizeof(aCol), " %3d", m_aPlayerStats[i].m_Lasts);
			str_append(aRow, aCol);
		}
		if(g_Config.m_TcMiniStatboardColumns & 8)
		{
			str_format(aCol, sizeof(aCol), " %4.1f", m_aPlayerStats[i].GetEfficiency());
			str_append(aRow, aCol);
		}
		if(g_Config.m_TcMiniStatboardColumns & 16)
		{
			str_format(aCol, sizeof(aCol), " %3d", m_aPlayerStats[i].m_Deaths);
			str_append(aRow, aCol);
		}

		if(i == GameClient()->m_Snap.m_LocalClientId)
			TextRender()->TextColor(1.0f, 1.0f, 0.4f, 1.0f);
		else
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, Alpha);

		TextRender()->Text(ScreenX, ScreenY, FontSize, aRow);
		ScreenY += RowHeight;
		Count++;
	}

	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}
