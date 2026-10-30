#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_PLAYER_PROFILES_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_PLAYER_PROFILES_H

#include <engine/http.h>
#include <engine/shared/console.h>
#include <engine/shared/protocol.h>
#include <game/client/component.h>

#include <atomic>
#include <deque>
#include <map>
#include <mutex>
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

// ==========================================
// DDNet Profile Structures
// ==========================================
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
	std::vector<std::string> m_vUnfinishedMaps;

	int64_t m_CachedTime;
	bool m_Valid;
};

// ==========================================
// Team Unfinished Map Comparison
// ==========================================
struct CTeamUnfinishedMap
{
	char m_aMap[64];
	char m_aCategory[32];
	int m_Points;
};

struct CMapMetadata
{
	char m_aCategory[32];
	int m_Points;
};

// ==========================================
// King of Gores (KoG) Profile Structures
// ==========================================
struct CKoGCategoryStats
{
	char m_aName[32];
	int m_Finished;
	int m_Total;
};

struct CKoGFinish
{
	char m_aMap[64];
	float m_Time;
	int m_Count;
	char m_aServer[32];
	char m_aTimestamp[32];
};

struct CKoGTeammate
{
	char m_aName[MAX_NAME_LENGTH];
	int m_Finishes;
};

struct CKoGPlayerProfile
{
	char m_aName[MAX_NAME_LENGTH];
	int m_TotalPoints;
	int m_Rank;
	int m_Points;
	int m_SeasonPoints;
	int m_PvPPoints;
	int64_t m_TimeWasted; // in seconds

	char m_aSkinName[64];
	int m_SkinColorBody;
	int m_SkinColorFeet;

	std::vector<CKoGCategoryStats> m_vCategories;
	std::vector<CKoGFinish> m_vFinishes;
	std::vector<std::string> m_vUnfinishedMaps;
	std::vector<CKoGTeammate> m_vTeammates;

	int64_t m_CachedTime;
	bool m_Valid;
	bool m_FromDiskCache;
};

class CPlayerProfiles : public CComponent
{
private:
	// DDNet fetch state
	std::shared_ptr<IHttpRequest> m_pHttpRequest = nullptr;
	char m_aRequestedName[MAX_NAME_LENGTH] = "";
	EProfileState m_State = EProfileState::IDLE;
	char m_aStatusMsg[128] = "";
	CDDNetPlayerProfile m_CurrentProfile;

	// In-memory LRU cache for DDNet
	std::map<std::string, CDDNetPlayerProfile> m_mProfilesCache;
	std::deque<std::string> m_dCacheOrder;
	static const size_t MAX_CACHE_ENTRIES = 25;
	static const int CACHE_TTL_SECONDS = 180; // 3 minutes

	// King of Gores (KoG) state
	char m_aKoGRequestedName[MAX_NAME_LENGTH] = "";
	EProfileState m_KoGState = EProfileState::IDLE;
	char m_aKoGStatusMsg[128] = "";
	CKoGPlayerProfile m_CurrentKoGProfile;

	// In-memory cache for KoG
	std::map<std::string, CKoGPlayerProfile> m_mKoGProfilesCache;
	std::deque<std::string> m_dKoGCacheOrder;

	// Thread management for background KoG fetch
	void *m_pKoGThread = nullptr;
	std::atomic<bool> m_KoGFetchRunning{false};
	std::atomic<bool> m_KoGFetchDone{false};
	bool m_KoGFetchSuccess = false;
	int m_KoGResultStatusCode = 0;
	std::string m_KoGResultData;
	std::string m_KoGFetchErrorMsg;
	char m_aKoGFetchThreadPlayer[MAX_NAME_LENGTH] = "";

	void ProcessHttpResponse();
	void AddToCache(const CDDNetPlayerProfile &Profile);
	bool GetFromCache(const char *pName, CDDNetPlayerProfile &OutProfile);

	static void KoGFetchWorker(void *pUser);
#if defined(CONF_FAMILY_WINDOWS)
	bool KoGFetchWorkerWindows();
#endif
	void ProcessKoGFetchCompletion();
	void AddToKoGCache(const CKoGPlayerProfile &Profile);
	bool GetFromKoGCache(const char *pName, CKoGPlayerProfile &OutProfile);

	static void ConProfile(IConsole::IResult *pResult, void *pUserData);
	static void ConProfileWebDDNet(IConsole::IResult *pResult, void *pUserData);
	static void ConProfileWebKoG(IConsole::IResult *pResult, void *pUserData);

public:
	CPlayerProfiles();
	~CPlayerProfiles() override;

	int Sizeof() const override { return sizeof(*this); }
	void OnConsoleInit() override;
	void OnRender() override;
	void OnReset() override;

	// DDNet profile operations
	void FetchProfile(const char *pPlayerName, bool ForceRefresh = false);
	void AbortCurrentFetch();
	EProfileState State() const { return m_State; }
	const char *StatusMsg() const { return m_aStatusMsg; }
	const CDDNetPlayerProfile &CurrentProfile() const { return m_CurrentProfile; }
	const char *RequestedName() const { return m_aRequestedName; }

	// KoG profile operations
	void FetchKoGProfile(const char *pPlayerName, bool ForceRefresh = false);
	void AbortKoGFetch();
	bool LoadKoGFromDisk(const char *pPlayerName, CKoGPlayerProfile &OutProfile);
	void SaveKoGToDisk(const char *pPlayerName, const char *pJsonData);
	bool ParseKoGProfileJson(const char *pJsonStr, CKoGPlayerProfile &OutProfile);

	EProfileState KoGState() const { return m_KoGState; }
	const char *KoGStatusMsg() const { return m_aKoGStatusMsg; }
	const CKoGPlayerProfile &CurrentKoGProfile() const { return m_CurrentKoGProfile; }
	const char *KoGRequestedName() const { return m_aKoGRequestedName; }

	// Web openers
	void OpenDDNetWeb(const char *pPlayerName);
	void OpenKoGWeb(const char *pPlayerName);

	// Team unfinished maps operations
	void AddTeamPlayer(const char *pName);
	void RemoveTeamPlayer(const char *pName);
	void ClearTeamPlayers();
	void AutoDetectCurrentTeam();
	void ComputeTeamCommonUnfinished();
	bool IsPlayerProfileLoaded(const char *pName, bool IsKoG) const;
	void QueueTeamProfileFetch(const char *pName, bool IsKoG);
	bool GetKoGMapMeta(const char *pMapName, CMapMetadata &OutMeta);
	bool GetDDNetMapMeta(const char *pMapName, CMapMetadata &OutMeta);
	void EnsureKoGMapsMetaLoaded();

	const std::vector<std::string> &TeamPlayers() const { return m_vTeamPlayers; }
	const std::vector<CTeamUnfinishedMap> &CommonUnfinishedMaps() const { return m_vCommonUnfinishedMaps; }
	const char *TeamStatusMsg() const { return m_aTeamStatusMsg; }
	bool TeamComparisonReady() const { return m_TeamComparisonReady; }

private:
	std::vector<std::string> m_vTeamPlayers;
	std::vector<CTeamUnfinishedMap> m_vCommonUnfinishedMaps;
	std::deque<std::string> m_qTeamPendingDDNet;
	std::deque<std::string> m_qTeamPendingKoG;
	bool m_TeamComparisonReady = false;
	char m_aTeamStatusMsg[128] = "";
	std::map<std::string, CMapMetadata> m_mKoGMapsMeta;
	std::map<std::string, CMapMetadata> m_mDDNetMapsMeta;
	bool m_KoGMapsMetaLoaded = false;
};

#endif
