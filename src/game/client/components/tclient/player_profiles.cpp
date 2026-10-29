#include "player_profiles.h"



#include <base/log.h>

#include <base/str.h>

#include <base/time.h>



#include <engine/client.h>

#include <engine/http.h>

#include <engine/shared/config.h>

#include <engine/shared/json.h>



#include <game/client/components/menus.h>

#include <game/client/gameclient.h>

#include <game/localization.h>



#include <algorithm>

#include <cctype>



CPlayerProfiles::CPlayerProfiles()

{

	m_CurrentProfile.m_Valid = false;

}



void CPlayerProfiles::OnReset()

{

	AbortCurrentFetch();

}



void CPlayerProfiles::AbortCurrentFetch()

{

	if(m_pHttpRequest)

	{

		m_pHttpRequest->Abort();

		m_pHttpRequest = nullptr;

	}

	if(m_State == EProfileState::FETCHING)

	{

		m_State = EProfileState::IDLE;

		str_copy(m_aStatusMsg, "");

	}

}



void CPlayerProfiles::AddToCache(const CDDNetPlayerProfile &Profile)

{

	char aLower[MAX_NAME_LENGTH];

	str_copy(aLower, Profile.m_aName);

	for(char *p = aLower; *p; ++p)

		*p = (char)tolower((unsigned char)*p);



	std::string Key(aLower);

	m_mProfilesCache[Key] = Profile;



	// Update LRU order

	m_dCacheOrder.erase(std::remove(m_dCacheOrder.begin(), m_dCacheOrder.end(), Key), m_dCacheOrder.end());

	m_dCacheOrder.push_back(Key);



	while(m_dCacheOrder.size() > MAX_CACHE_ENTRIES)

	{

		std::string Oldest = m_dCacheOrder.front();

		m_dCacheOrder.pop_front();

		m_mProfilesCache.erase(Oldest);

	}

}



bool CPlayerProfiles::GetFromCache(const char *pName, CDDNetPlayerProfile &OutProfile)

{

	if(!pName || pName[0] == '\0')

		return false;



	char aLower[MAX_NAME_LENGTH];

	str_copy(aLower, pName);

	for(char *p = aLower; *p; ++p)

		*p = (char)tolower((unsigned char)*p);



	std::string Key(aLower);

	auto It = m_mProfilesCache.find(Key);

	if(It != m_mProfilesCache.end())

	{

		int64_t Now = time_timestamp();

		if(Now - It->second.m_CachedTime < CACHE_TTL_SECONDS)

		{

			OutProfile = It->second;

			// Refresh LRU

			m_dCacheOrder.erase(std::remove(m_dCacheOrder.begin(), m_dCacheOrder.end(), Key), m_dCacheOrder.end());

			m_dCacheOrder.push_back(Key);

			return true;

		}

	}

	return false;

}



void CPlayerProfiles::FetchProfile(const char *pPlayerName, bool ForceRefresh)

{

	if(!pPlayerName || pPlayerName[0] == '\0')

		return;



	str_copy(m_aRequestedName, pPlayerName);



	// Check cache first if not forcing refresh

	if(!ForceRefresh && GetFromCache(pPlayerName, m_CurrentProfile))

	{

		m_State = EProfileState::READY;

		str_copy(m_aStatusMsg, "");

		return;

	}



	AbortCurrentFetch();



	char aEscaped[256];

	EscapeUrl(aEscaped, pPlayerName);



	char aUrl[512];

	str_format(aUrl, sizeof(aUrl), "https://ddnet.org/players/?json2=%s", aEscaped);



	m_pHttpRequest = HttpGet(aUrl);

	m_pHttpRequest->Timeout(CTimeout{5000, 5000, 500, 5});

	m_pHttpRequest->IpResolve(IPRESOLVE::V4);

	Http()->Run(m_pHttpRequest);



	m_State = EProfileState::FETCHING;

	str_format(m_aStatusMsg, sizeof(m_aStatusMsg), "Cargando perfil de '%s'...", pPlayerName);

}



void CPlayerProfiles::OnRender()

{

	if(!m_pHttpRequest)

		return;



	if(m_pHttpRequest->Done())

	{

		if(m_pHttpRequest->State() == EHttpState::DONE)

		{

			ProcessHttpResponse();

		}

		else

		{

			m_State = EProfileState::ERROR_NETWORK;

			str_copy(m_aStatusMsg, "Error al conectar con DDNet (tiempo de espera agotado)");

			m_CurrentProfile.m_Valid = false;

		}

		m_pHttpRequest = nullptr;

	}

}



void CPlayerProfiles::ProcessHttpResponse()

{

	if(!m_pHttpRequest)

		return;



	json_value *pJson = m_pHttpRequest->ResultJson();

	if(!pJson)

	{

		m_State = EProfileState::ERROR_NETWORK;

		str_copy(m_aStatusMsg, "Error: respuesta JSON invalida de DDNet");

		m_CurrentProfile.m_Valid = false;

		return;

	}



	const json_value &Json = *pJson;

	if(Json.type != json_object || Json["player"].type != json_string)

	{

		// Player not found or has 0 finishes

		m_State = EProfileState::NOT_FOUND;

		str_format(m_aStatusMsg, sizeof(m_aStatusMsg), "Jugador '%s' no encontrado en DDNet", m_aRequestedName);

		m_CurrentProfile.m_Valid = false;

		json_value_free(pJson);

		return;

	}



	// Successfully parsed

	CDDNetPlayerProfile Profile{};

	str_copy(Profile.m_aName, (const char *)Json["player"]);



	const json_value &Points = Json["points"];

	if(Points.type == json_object)

	{

		Profile.m_Points = Points["points"].type == json_integer ? (int)Points["points"].u.integer : 0;

		Profile.m_TotalPoints = Points["total"].type == json_integer ? (int)Points["total"].u.integer : 0;

		Profile.m_Rank = Points["rank"].type == json_integer ? (int)Points["rank"].u.integer : 0;

	}



	const json_value &TeamRank = Json["team_rank"];

	if(TeamRank.type == json_object)

	{

		Profile.m_TeamPoints = TeamRank["points"].type == json_integer ? (int)TeamRank["points"].u.integer : 0;

		Profile.m_TeamRank = TeamRank["rank"].type == json_integer ? (int)TeamRank["rank"].u.integer : 0;

	}



	const json_value &FavServer = Json["favorite_server"];

	if(FavServer.type == json_object && FavServer["server"].type == json_string)

	{

		str_copy(Profile.m_aFavoriteServer, (const char *)FavServer["server"]);

	}



	// Categories (Novice, Moderate, Brutal, Insane, Solo, Oldschool, Race, Dummy, DDmaX, etc.)

	const json_value &Types = Json["types"];

	if(Types.type == json_object)

	{

		for(unsigned int i = 0; i < Types.u.object.length; ++i)

		{

			const char *pCatName = Types.u.object.values[i].name;

			const json_value &CatObj = *Types.u.object.values[i].value;

			if(CatObj.type != json_object)

				continue;



			const json_value &PtsObj = CatObj["points"];

			if(PtsObj.type != json_object)

				continue;



			CDDNetCategoryStats CatStats{};

			str_copy(CatStats.m_aName, pCatName);

			CatStats.m_Points = PtsObj["points"].type == json_integer ? (int)PtsObj["points"].u.integer : 0;

			CatStats.m_TotalPoints = PtsObj["total"].type == json_integer ? (int)PtsObj["total"].u.integer : 0;

			CatStats.m_Rank = PtsObj["rank"].type == json_integer ? (int)PtsObj["rank"].u.integer : 0;

			Profile.m_vCategories.push_back(CatStats);

		}

	}



	// Last finishes (bounded to 15 items max)

	const json_value &Finishes = Json["last_finishes"];

	if(Finishes.type == json_array)

	{

		for(unsigned int i = 0; i < Finishes.u.array.length && i < 15; ++i)

		{

			const json_value &F = *Finishes.u.array.values[i];

			if(F.type != json_object)

				continue;



			CDDNetFinish Finish{};

			str_copy(Finish.m_aMap, (const char *)F["map"]);

			Finish.m_Time = (float)(F["time"].type == json_double ? F["time"].u.dbl : (F["time"].type == json_integer ? (double)F["time"].u.integer : 0.0));

			str_copy(Finish.m_aType, (const char *)F["type"]);

			str_copy(Finish.m_aServer, (const char *)F["country"]);

			Finish.m_Timestamp = (int64_t)(F["timestamp"].type == json_double ? F["timestamp"].u.dbl : (F["timestamp"].type == json_integer ? F["timestamp"].u.integer : 0));

			Profile.m_vLastFinishes.push_back(Finish);

		}

	}



	Profile.m_CachedTime = time_timestamp();

	Profile.m_Valid = true;



	m_CurrentProfile = Profile;

	AddToCache(Profile);



	m_State = EProfileState::READY;

	str_copy(m_aStatusMsg, "");



	json_value_free(pJson);

}



void CPlayerProfiles::OpenDDNetWeb(const char *pPlayerName)

{

	if(!pPlayerName || pPlayerName[0] == '\0')

		return;

	char aEscaped[256];

	EscapeUrl(aEscaped, pPlayerName);

	char aUrl[512];

	str_format(aUrl, sizeof(aUrl), "https://ddnet.org/players/%s", aEscaped);

	Client()->ViewLink(aUrl);

}



void CPlayerProfiles::OpenKoGWeb(const char *pPlayerName)

{

	if(!pPlayerName || pPlayerName[0] == '\0')

		return;

	char aEscaped[256];

	EscapeUrl(aEscaped, pPlayerName);

	char aUrl[512];

	str_format(aUrl, sizeof(aUrl), "https://kog.tw/#p=player&player=%s", aEscaped);

	Client()->ViewLink(aUrl);

}



void CPlayerProfiles::ConProfile(IConsole::IResult *pResult, void *pUserData)

{

	CPlayerProfiles *pSelf = (CPlayerProfiles *)pUserData;

	const char *pName = pResult->GetString(0);

	if(!pName || pName[0] == '\0')

		pName = pSelf->Client()->PlayerName();



	pSelf->FetchProfile(pName, true);



	// Navigate to AClient -> Profiles menu

	g_Config.m_UiPage = CMenus::PAGE_SETTINGS;

	g_Config.m_UiSettingsPage = CMenus::SETTINGS_ACLIENT;

	pSelf->GameClient()->m_Menus.SetAClientTab(CMenus::ACLIENT_TAB_PROFILES);

	pSelf->GameClient()->m_Menus.SetActive(true);

}



void CPlayerProfiles::ConProfileWebDDNet(IConsole::IResult *pResult, void *pUserData)

{

	CPlayerProfiles *pSelf = (CPlayerProfiles *)pUserData;

	const char *pName = pResult->GetString(0);

	if(!pName || pName[0] == '\0')

		pName = pSelf->Client()->PlayerName();

	pSelf->OpenDDNetWeb(pName);

}



void CPlayerProfiles::ConProfileWebKoG(IConsole::IResult *pResult, void *pUserData)

{

	CPlayerProfiles *pSelf = (CPlayerProfiles *)pUserData;

	const char *pName = pResult->GetString(0);

	if(!pName || pName[0] == '\0')

		pName = pSelf->Client()->PlayerName();

	pSelf->OpenKoGWeb(pName);

}



void CPlayerProfiles::OnConsoleInit()

{

	Console()->Register("profile", "?s[name]", CFGFLAG_CLIENT, ConProfile, this, "View player DDNet profile in AClient");

	Console()->Register("profile_web_ddnet", "?s[name]", CFGFLAG_CLIENT, ConProfileWebDDNet, this, "Open player profile in DDNet website");

	Console()->Register("profile_web_kog", "?s[name]", CFGFLAG_CLIENT, ConProfileWebKoG, this, "Open player profile in King of Gores website");

}

