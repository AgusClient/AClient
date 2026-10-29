#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_PLAYER_PROFILES_H

#define GAME_CLIENT_COMPONENTS_TCLIENT_PLAYER_PROFILES_H



#include <engine/http.h>

#include <engine/shared/console.h>

#include <engine/shared/protocol.h>

#include <game/client/component.h>



#include <deque>

#include <map>

#include <string>

#include <vector>



enum class EProfileState

{

	IDLE,

	FETCHING,

	READY,

	NOT_FOUND,

	ERROR_NETWORK

};



struct CDDNetCategoryStats

{

	char m_aName[32];

	int m_Points;

	int m_TotalPoints;

	int m_Rank;

};



struct CDDNetFinish

{

	char m_aMap[64];

	float m_Time;

	char m_aType[32];

	char m_aServer[32];

	int64_t m_Timestamp;

};



struct CDDNetPlayerProfile

{

	char m_aName[MAX_NAME_LENGTH];

	int m_Points;

	int m_TotalPoints;

	int m_Rank;

	int m_TeamPoints;

	int m_TeamRank;

	char m_aFavoriteServer[32];



	std::vector<CDDNetCategoryStats> m_vCategories;

	std::vector<CDDNetFinish> m_vLastFinishes;



	int64_t m_CachedTime;

	bool m_Valid;

};



class CPlayerProfiles : public CComponent

{

private:

	std::shared_ptr<IHttpRequest> m_pHttpRequest = nullptr;

	char m_aRequestedName[MAX_NAME_LENGTH] = "";



	EProfileState m_State = EProfileState::IDLE;

	char m_aStatusMsg[128] = "";



	// Active profile being viewed

	CDDNetPlayerProfile m_CurrentProfile;



	// In-memory LRU cache: maps lowercase name -> profile

	std::map<std::string, CDDNetPlayerProfile> m_mProfilesCache;

	std::deque<std::string> m_dCacheOrder;

	static const size_t MAX_CACHE_ENTRIES = 25;

	static const int CACHE_TTL_SECONDS = 180; // 3 minutes



	void ProcessHttpResponse();

	void AddToCache(const CDDNetPlayerProfile &Profile);

	bool GetFromCache(const char *pName, CDDNetPlayerProfile &OutProfile);



	static void ConProfile(IConsole::IResult *pResult, void *pUserData);

	static void ConProfileWebDDNet(IConsole::IResult *pResult, void *pUserData);

	static void ConProfileWebKoG(IConsole::IResult *pResult, void *pUserData);



public:

	CPlayerProfiles();



	int Sizeof() const override { return sizeof(*this); }

	void OnConsoleInit() override;

	void OnRender() override;

	void OnReset() override;



	void FetchProfile(const char *pPlayerName, bool ForceRefresh = false);

	void AbortCurrentFetch();



	void OpenDDNetWeb(const char *pPlayerName);

	void OpenKoGWeb(const char *pPlayerName);



	EProfileState State() const { return m_State; }

	const char *StatusMsg() const { return m_aStatusMsg; }

	const CDDNetPlayerProfile &CurrentProfile() const { return m_CurrentProfile; }

	const char *RequestedName() const { return m_aRequestedName; }

};



#endif

