#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_RACE_STATS_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_RACE_STATS_H

#include <game/client/component.h>
#include <game/client/render.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <generated/protocol.h>

#include <cmath>
#include <algorithm>
#include <vector>

class CRaceStats : public CComponent
{
public:
	int Sizeof() const override { return sizeof(*this); }

	enum EIcon
	{
		ICON_USER = 0,
		ICON_FREEZE,
		ICON_SAVE,
		ICON_SKULL,
		ICON_EFF,
		ICON_LAST,
		ICON_NET,
		ICON_TIME,
		ICON_HOOK,
		NUM_ICONS
	};

	enum EStatboardSort
	{
		STATBOARD_SORT_DEFAULT = 0,
		STATBOARD_SORT_SAVES,
		STATBOARD_SORT_DEATHS,
		STATBOARD_SORT_FREEZES,
		STATBOARD_SORT_EFFICIENCY,
		STATBOARD_SORT_NET,
		STATBOARD_SORT_TIME,
		STATBOARD_SORT_HOOK,
		STATBOARD_SORT_LASTS,
		NUM_STATBOARD_SORTS
	};

	struct SRowAnimState
	{
		float m_VisualY = -1.0f;
		int m_PrevRank = -1;
		float m_RankChangeTimer = 0.0f;
		int m_RankChangeDir = 0; // +1 = subio (adelantamiento), -1 = bajo

		void Reset()
		{
			m_VisualY = -1.0f;
			m_PrevRank = -1;
			m_RankChangeTimer = 0.0f;
			m_RankChangeDir = 0;
		}
	};

	struct CPlayerStats
	{
		int m_Deaths;
		int m_Freezes;
		int m_Saves;
		int m_Lasts;
		int m_LastAttackTick;
		int m_LastHammerTick;
		vec2 m_LastHammerPos;
		int m_LastFreezeTick;
		bool m_WasFrozen;
		bool m_WasSoleSurvivor;
		int m_SoleSurvivorTicks;

		// Estadísticas avanzadas
		int m_FrozenTicks;
		int m_HooksThrown;
		int m_HooksHit;
		int m_LastHookState;

		void Reset()
		{
			m_Deaths = 0;
			m_Freezes = 0;
			m_Saves = 0;
			m_Lasts = 0;
			m_LastAttackTick = 0;
			m_LastHammerTick = 0;
			m_LastHammerPos = vec2(0.0f, 0.0f);
			m_LastFreezeTick = 0;
			m_WasFrozen = false;
			m_WasSoleSurvivor = false;
			m_SoleSurvivorTicks = 0;

			m_FrozenTicks = 0;
			m_HooksThrown = 0;
			m_HooksHit = 0;
			m_LastHookState = -1;
		}

		// Retorna -1 si no hay actividad aún (0 saves y 0 freezes), o un valor entre 0 y 100
		int GetEfficiencyPercent() const
		{
			if(m_Saves == 0 && m_Freezes == 0)
				return -1;
			int Total = m_Saves + m_Freezes;
			int Pct = (int)std::round(((float)m_Saves / (float)Total) * 100.0f);
			return std::clamp(Pct, 0, 100);
		}

		int GetNetBalance() const
		{
			return m_Saves - m_Freezes;
		}

		int GetFrozenSeconds(int TickSpeed) const
		{
			return std::max(0, m_FrozenTicks / std::max(1, TickSpeed));
		}

		int GetHookAccuracyPercent() const
		{
			if(m_HooksThrown <= 0)
				return -1;
			int Pct = (int)std::round(((float)m_HooksHit / (float)m_HooksThrown) * 100.0f);
			return std::clamp(Pct, 0, 100);
		}
	};

	struct CSavedPlayerStat
	{
		char m_aName[MAX_NAME_LENGTH];
		char m_aSkinName[24];
		bool m_CustomColors;
		int m_ColorBody;
		int m_ColorFeet;
		int m_Freezes;
		int m_Saves;
		int m_Lasts;
		int m_EfficiencyPercent;
		int m_Deaths;
		int m_NetBalance;
		int m_FrozenSeconds;
		int m_HookAccuracyPercent;
		bool m_IsLocal;
	};

	struct CSavedRaceStatsEntry
	{
		char m_aTimestamp[32];
		char m_aMapName[64];
		int m_Team;
		char m_aRunTime[32];
		char m_aReason[32];
		bool m_Completed = false;
		std::vector<CSavedPlayerStat> m_vPlayers;
	};

	CPlayerStats m_aPlayerStats[MAX_CLIENTS];
	std::vector<CSavedRaceStatsEntry> m_vSavedEntries;
	int m_RoundDeaths;
	int m_SessionDeaths;
	int m_LastLocalTeam;
	int m_RunStartTick;
	bool m_Active;
	IGraphics::CTextureHandle m_aTextures[NUM_ICONS];
	IGraphics::CTextureHandle m_IconAtlasTexture;
	SRowAnimState m_aRowAnim[MAX_CLIENTS];
	bool m_HistoryNeedsSave = false;
	int m_LastStatsTick = -1;
	bool m_StatsDirty = true;
	bool m_RunFinishedSaved = false;
	int64_t m_LastFinishSaveTime = 0;

	struct SCachedStatCol
	{
		int m_ColType;
		char m_aText[16];
		float m_ColWidth;
		float m_TextWidth;
		float m_TextXOffset;
		ColorRGBA m_Color;
	};

	struct SCachedPlayerRow
	{
		int m_ClientId;
		bool m_IsLocal;
		char m_aName[MAX_NAME_LENGTH];
		float m_NameWidth;
		int m_NumCols;
		SCachedStatCol m_aCols[NUM_ICONS];
		CTeeRenderInfo m_TeeInfo;
		bool m_HasTee = false;
	};

	std::vector<SCachedPlayerRow> m_vCachedRows;
	float m_CachedColWidthName = 0.0f;
	float m_CachedStatsWidth = 0.0f;
	float m_CachedTotalWidth = 0.0f;
	float m_CachedHeaderHeight = 0.0f;
	float m_CachedRowsHeight = 0.0f;
	float m_CachedTotalHeight = 0.0f;
	int m_CachedTotalTeamPlayers = 0;
	int m_CachedDisplayCount = 0;
	bool m_CachedHasMore = false;
	char m_aCachedMoreText[32];
	float m_CachedMoreTextWidth = 0.0f;

	int m_LastCacheTick = -1;
	int m_LastCacheColumns = -1;
	int m_LastCacheSort = -1;
	int m_LastCacheScale = -1;
	int m_LastCacheMaxPlayers = -1;
	int m_LastCacheShowTees = -1;
	int m_LastCacheTeam = -1;
	int m_LastCacheLocalClientId = -1;
	bool m_CacheValid = false;

	void UpdateMiniStatboardCache();

	void OnInit() override;
	void OnReset() override;
	void OnShutdown() override;
	void OnRender() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnConsoleInit() override;

	static void ConKeyCompactStatboard(IConsole::IResult *pResult, void *pUserData);
	static void ConSaveRaceStats(IConsole::IResult *pResult, void *pUserData);
	static void ConClearRaceStatsHistory(IConsole::IResult *pResult, void *pUserData);
	static ColorRGBA GetEfficiencyColor(int Percent);

	void RenderMiniStatboard();
	bool HasCurrentTeamActivity() const;
	void FormatCurrentRunTime(char *pBuf, int BufSize) const;
	void ResetTeamStats();
	bool SaveCurrentStats(const char *pReason, bool RequireActivity = false, bool NotifyChat = false);
	void DeleteSavedEntry(int Index);
	void ClearSavedHistory();
	void SaveHistoryToFile();
	void LoadHistoryFromFile();
};

#endif // GAME_CLIENT_COMPONENTS_TCLIENT_RACE_STATS_H
