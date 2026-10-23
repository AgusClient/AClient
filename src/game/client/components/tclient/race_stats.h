#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_RACE_STATS_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_RACE_STATS_H

#include <game/client/component.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>

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
		int m_LastFreezeTick;
		bool m_WasFrozen;

		void Reset()
		{
			m_Deaths = 0;
			m_Freezes = 0;
			m_Saves = 0;
			m_Lasts = 0;
			m_LastAttackTick = 0;
			m_LastFreezeTick = 0;
			m_WasFrozen = false;
		}

		float GetEfficiency() const
		{
			if(m_Freezes == 0)
				return (float)m_Saves;
			return (float)m_Saves / (float)m_Freezes;
		}
	};

	CPlayerStats m_aPlayerStats[MAX_CLIENTS];
	int m_RoundDeaths;
	int m_SessionDeaths;
	bool m_Active;
	IGraphics::CTextureHandle m_aTextures[NUM_ICONS];

	void OnInit() override;
	void OnReset() override;
	void OnRender() override;
	void OnMessage(int MsgType, void *pRawMsg) override;
	void OnConsoleInit() override;

	static void ConKeyCompactStatboard(IConsole::IResult *pResult, void *pUserData);

	void RenderMiniStatboard();
	void CheckTeamWipe();
};

#endif // GAME_CLIENT_COMPONENTS_TCLIENT_RACE_STATS_H
