#include "race_stats.h"

#include <engine/graphics.h>
#include <engine/textrender.h>
#include <engine/storage.h>
#include <game/client/animstate.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <generated/protocol.h>

#include <algorithm>
#include <cmath>
#include <vector>

static const float HAMMER_MAX_DIST = 64.0f;

void CRaceStats::OnInit()
{
	m_Active = false;
	m_aTextures[ICON_USER]   = Graphics()->LoadTexture("NovaClient/racestats/user.png", IStorage::TYPE_ALL);
	m_aTextures[ICON_FREEZE] = Graphics()->LoadTexture("NovaClient/racestats/freeze.png", IStorage::TYPE_ALL);
	m_aTextures[ICON_SAVE]   = Graphics()->LoadTexture("NovaClient/racestats/save.png", IStorage::TYPE_ALL);
	m_aTextures[ICON_LAST]   = Graphics()->LoadTexture("NovaClient/racestats/last.png", IStorage::TYPE_ALL);
	m_aTextures[ICON_EFF]    = Graphics()->LoadTexture("NovaClient/racestats/eff.png", IStorage::TYPE_ALL);
	m_aTextures[ICON_SKULL]  = Graphics()->LoadTexture("NovaClient/racestats/skull.png", IStorage::TYPE_ALL);
}

void CRaceStats::ConKeyCompactStatboard(IConsole::IResult *pResult, void *pUserData)
{
	CRaceStats *pSelf = (CRaceStats *)pUserData;
	pSelf->m_Active = pResult->GetInteger(0) != 0;
}

void CRaceStats::OnConsoleInit()
{
	Console()->Register("+compact_statboard", "", CFGFLAG_CLIENT, ConKeyCompactStatboard, this, "Toggle or hold compact statboard");
}

void CRaceStats::OnReset()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		m_aPlayerStats[i].Reset();
	}
	m_RoundDeaths = 0;
	m_Active = false;
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
	if(Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

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
	int MaxPlayers = std::max(1, (int)g_Config.m_TcMiniStatboardMaxPlayers);
	int DisplayCount = std::min(TotalTeamPlayers, MaxPlayers);
	bool HasMore = TotalTeamPlayers > DisplayCount;

	float TeeSize = FontSize * 1.35f;
	float MaxFoundNameW = 40.0f * Scale;
	for(int idx = 0; idx < DisplayCount; idx++)
	{
		int CID = vPlayers[idx].m_ClientId;
		float NameW = TextRender()->TextWidth(FontSize, GameClient()->m_aClients[CID].m_aName, -1, -1.0f);
		if(NameW > MaxFoundNameW)
			MaxFoundNameW = NameW;
	}
	MaxFoundNameW = std::clamp(MaxFoundNameW, 40.0f * Scale, 90.0f * Scale);
	float ColWidthName = MaxFoundNameW + TeeSize + 4.0f * Scale;

	bool ShowFreeze = (g_Config.m_TcMiniStatboardColumns & 1) != 0;
	bool ShowSave   = (g_Config.m_TcMiniStatboardColumns & 2) != 0;
	bool ShowLast   = (g_Config.m_TcMiniStatboardColumns & 4) != 0;
	bool ShowEff    = (g_Config.m_TcMiniStatboardColumns & 8) != 0;
	bool ShowDeath  = (g_Config.m_TcMiniStatboardColumns & 16) != 0;

	int NumStatCols = (ShowFreeze ? 1 : 0) + (ShowSave ? 1 : 0) + (ShowLast ? 1 : 0) + (ShowEff ? 1 : 0) + (ShowDeath ? 1 : 0);
	float TotalWidth = ColWidthName + (float)NumStatCols * ColWidthStat + Padding * 2.0f;

	float HeaderHeight = RowHeight + 1.0f * Scale;
	float RowsHeight = (float)DisplayCount * RowHeight;
	float MoreHeight = HasMore ? (RowHeight * 0.85f) : 0.0f;
	float TotalHeight = HeaderHeight + RowsHeight + MoreHeight + Padding * 2.0f;

	// Coordenadas proporcionales (NovaClient default X=495 Y=150)
	float BaseX = (float)g_Config.m_TcMiniStatboardX;
	float BaseY = (float)g_Config.m_TcMiniStatboardY;

	// Si las coordenadas guardadas se exceden del aspect ratio actual, auto-ajustar a la pantalla
	if(BaseX + TotalWidth > Width)
		BaseX = Width - TotalWidth - 4.0f;
	if(BaseX < 2.0f)
		BaseX = 2.0f;
	if(BaseY + TotalHeight > Height)
		BaseY = Height - TotalHeight - 4.0f;
	if(BaseY < 2.0f)
		BaseY = 2.0f;

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
	if(ShowDeath)
	{
		DrawIcon(m_aTextures[ICON_SKULL], CurrX + (ColWidthStat - IconSize) / 2.0f, HeaderY, IconSize, ColorRGBA(1.0f, 0.35f, 0.35f, 1.0f));
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
		float MaxNameW = ColWidthName - TeeInfo.m_Size - 3.0f * Scale;

		// Nombre de Jugador en 1 sola línea (nunca salta de línea)
		if(IsMe)
			TextRender()->TextColor(1.0f, 0.9f, 0.3f, 1.0f);
		else
			TextRender()->TextColor(1.0f, 1.0f, 1.0f, 0.95f);

		CTextCursor Cursor;
		Cursor.m_FontSize = FontSize;
		Cursor.m_X = NameX;
		Cursor.m_Y = TextY;
		Cursor.m_LineWidth = MaxNameW;
		Cursor.m_Flags = TEXTFLAG_RENDER | TEXTFLAG_STOP_AT_END | TEXTFLAG_ELLIPSIS_AT_END | TEXTFLAG_DISALLOW_NEWLINE;
		TextRender()->TextEx(&Cursor, GameClient()->m_aClients[CID].m_aName);
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
		if(ShowDeath)
		{
			str_format(aBuf, sizeof(aBuf), "%d", m_aPlayerStats[CID].m_Deaths);
			DrawStatNum(aBuf, ColorRGBA(1.0f, 0.4f, 0.4f, 0.95f));
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
