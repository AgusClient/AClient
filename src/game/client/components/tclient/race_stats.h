#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_RACE_STATS_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_RACE_STATS_H

#include <game/client/component.h>
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
		ICON_LAST,
		ICON_EFF,
		ICON_SKULL,
		NUM_ICONS
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
		}

		float GetEfficiency() const
		{
			if(m_Freezes == 0)
				return (float)m_Saves;
			return (float)m_Saves / (float)m_Freezes;
		}

		int GetEfficiencyPercent() const
		{
			if(m_Freezes == 0)
				return m_Saves > 0 ? 100 : 0;
			int Pct = (int)std::round(((float)m_Saves / (float)m_Freezes) * 100.0f);
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
		bool m_IsLocal;
	};

	struct CSavedRaceStatsEntry
	{
		char m_aTimestamp[32];
		char m_aMapName[64];
		int m_Team;
		char m_aRunTime[32];
		char m_aReason[32];
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

	void OnInit() override;
	void OnReset() override;
	void OnRender() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnConsoleInit() override;

	static void ConKeyCompactStatboard(IConsole::IResult *pResult, void *pUserData);
	static void ConSaveRaceStats(IConsole::IResult *pResult, void *pUserData);

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
