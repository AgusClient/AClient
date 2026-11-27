#include <set>
#include "aclient_backend.h"
#include "player_profiles.h"
#include "kog_maps_data.h"
#include <base/io.h>
#include <base/log.h>
#include <base/str.h>
#include <base/time.h>
#include <engine/client.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <algorithm>
#include <cctype>

CPlayerProfiles::CPlayerProfiles()
{
	m_CurrentProfile.m_Valid = false;
	m_CurrentKoGProfile.m_Valid = false;
}

void CPlayerProfiles::OnInit()
{
	Storage()->GetCompletePath(IStorage::TYPE_SAVE, "aclient/kog_cookies.txt", m_aKoGCookieFile, sizeof(m_aKoGCookieFile));
	Storage()->CreateFolder("aclient", IStorage::TYPE_SAVE);
}

CPlayerProfiles::~CPlayerProfiles()
{
	AbortCurrentFetch();
	AbortKoGFetch();
	if(m_pKoGMapsHttpRequest)
	{
		m_pKoGMapsHttpRequest->Abort();
		m_pKoGMapsHttpRequest = nullptr;
	}
}

void CPlayerProfiles::OnReset()
{
	AbortCurrentFetch();
	AbortKoGFetch();
	if(m_pKoGMapsHttpRequest)
	{
		m_pKoGMapsHttpRequest->Abort();
		m_pKoGMapsHttpRequest = nullptr;
	}
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

void CPlayerProfiles::AbortKoGFetch()
{
	if(m_pKoGHttpRequest)
	{
		m_pKoGHttpRequest->Abort();
		m_pKoGHttpRequest = nullptr;
	}
	m_KoGFetchStage = EKoGFetchStage::NONE;
	m_KoGNonce.clear();
	if(m_KoGState == EProfileState::FETCHING)
	{
		m_KoGState = EProfileState::IDLE;
		str_copy(m_aKoGStatusMsg, "");
	}
}

// ==========================================
// DDNet Profile Operations
// ==========================================

void CPlayerProfiles::AddToCache(const CDDNetPlayerProfile &Profile)
{
	char aLower[MAX_NAME_LENGTH];
	str_copy(aLower, Profile.m_aName);
	for(char *p = aLower; *p; ++p)
		*p = (char)tolower((unsigned char)*p);
	std::string Key(aLower);
	m_mProfilesCache[Key] = Profile;

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

	char aClean[MAX_NAME_LENGTH];
	str_copy(aClean, pPlayerName);
	char *pStart = aClean;
	while(*pStart && (unsigned char)*pStart <= ' ')
		pStart++;
	char *pEnd = pStart + str_length(pStart);
	while(pEnd > pStart && (unsigned char)*(pEnd - 1) <= ' ')
		pEnd--;
	*pEnd = '\0';

	if(pStart[0] == '\0')
		return;

	str_copy(m_aRequestedName, pStart);

	if(!ForceRefresh && GetFromCache(pStart, m_CurrentProfile))
	{
		m_State = EProfileState::READY;
		str_copy(m_aStatusMsg, "");
		return;
	}

	if(!ForceRefresh)
	{
		char aLowerDDNet[MAX_NAME_LENGTH];
		str_copy(aLowerDDNet, pStart);
		for(char *p = aLowerDDNet; *p; ++p)
			*p = (char)tolower((unsigned char)*p);
		auto ItFail = m_mFailedFetchesDDNet.find(aLowerDDNet);
		if(ItFail != m_mFailedFetchesDDNet.end() && time_get() < ItFail->second)
		{
			m_State = EProfileState::NOT_FOUND;
			str_format(m_aStatusMsg, sizeof(m_aStatusMsg), "Jugador '%s' no encontrado en DDNet", pStart);
			m_CurrentProfile.m_Valid = false;
			return;
		}
	}

	AbortCurrentFetch();

	char aEscaped[256];
	EscapeUrl(aEscaped, pStart);
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "https://ddnet.org/players/?json2=%s", aEscaped);

	m_pHttpRequest = HttpGet(aUrl);
	m_pHttpRequest->Timeout(CTimeout{5000, 5000, 500, 5});
	m_pHttpRequest->IpResolve(IPRESOLVE::V4);
	AClientBackend::ApplyDownloadSpeedLimit(m_pHttpRequest, 4 * 1024 * 1024);
	Http()->Run(m_pHttpRequest);

	m_State = EProfileState::FETCHING;
	str_format(m_aStatusMsg, sizeof(m_aStatusMsg), "Cargando perfil de '%s'...", pStart);
}

void CPlayerProfiles::OnRender()
{
	// 1. Check DDNet HTTP request
	if(m_pHttpRequest && m_pHttpRequest->Done())
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

		if(!m_vTeamPlayers.empty())
			ComputeTeamCommonUnfinished();
	}

	// 2. Check KoG HTTP request completion
	if(m_pKoGHttpRequest && m_pKoGHttpRequest->Done())
	{
		AdvanceKoGFetch();
	}

	// 3. Process queued team background fetches
	if(!m_pHttpRequest && !m_qTeamPendingDDNet.empty())
	{
		std::string Next = m_qTeamPendingDDNet.front();
		m_qTeamPendingDDNet.pop_front();
		FetchProfile(Next.c_str(), false);
	}

	if(!m_pKoGHttpRequest && !m_qTeamPendingKoG.empty())
	{
		std::string Next = m_qTeamPendingKoG.front();
		m_qTeamPendingKoG.pop_front();
		FetchKoGProfile(Next.c_str(), false, true);
	}

	// 4. Check KoG maps metadata background download
	if(m_pKoGMapsHttpRequest && m_pKoGMapsHttpRequest->Done())
	{
		ProcessKoGMapsMetaResponse();
	}
}

void CPlayerProfiles::ProcessHttpResponse()
{
	if(!m_pHttpRequest)
		return;

	json_value *pJson = m_pHttpRequest->ResultJson();
	if(!pJson)
	{
		char aLowerReq[MAX_NAME_LENGTH];
		str_copy(aLowerReq, m_aRequestedName);
		for(char *p = aLowerReq; *p; ++p)
			*p = (char)tolower((unsigned char)*p);
		m_mFailedFetchesDDNet[aLowerReq] = time_get() + time_freq() * 60;

		if(m_pHttpRequest->StatusCode() == 404)
		{
			m_State = EProfileState::NOT_FOUND;
			str_format(m_aStatusMsg, sizeof(m_aStatusMsg), "Jugador '%s' no encontrado en DDNet", m_aRequestedName);
		}
		else
		{
			m_State = EProfileState::ERROR_NETWORK;
			str_copy(m_aStatusMsg, "Error: respuesta JSON invalida de DDNet");
		}
		m_CurrentProfile.m_Valid = false;
		return;
	}

	const json_value &Json = *pJson;
	if(Json.type != json_object || Json["player"].type != json_string)
	{
		char aLowerReq[MAX_NAME_LENGTH];
		str_copy(aLowerReq, m_aRequestedName);
		for(char *p = aLowerReq; *p; ++p)
			*p = (char)tolower((unsigned char)*p);
		m_mFailedFetchesDDNet[aLowerReq] = time_get() + time_freq() * 120;

		m_State = EProfileState::NOT_FOUND;
		str_format(m_aStatusMsg, sizeof(m_aStatusMsg), "Jugador '%s' no encontrado en DDNet", m_aRequestedName);
		m_CurrentProfile.m_Valid = false;
		json_value_free(pJson);
		return;
	}

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

			const json_value &CatMaps = CatObj["maps"];
			if(CatMaps.type == json_object)
			{
				for(unsigned int m = 0; m < CatMaps.u.object.length; ++m)
				{
					const char *pMapName = CatMaps.u.object.values[m].name;
					const json_value &MapInfo = *CatMaps.u.object.values[m].value;
					if(MapInfo.type == json_object)
					{
						int Finishes = MapInfo["finishes"].type == json_integer ? (int)MapInfo["finishes"].u.integer : 0;
						int Pts = MapInfo["points"].type == json_integer ? (int)MapInfo["points"].u.integer : 0;
						if(pMapName && pMapName[0])
						{
							char aLower[64];
							str_copy(aLower, pMapName);
							for(char *p = aLower; *p; ++p) *p = (char)tolower((unsigned char)*p);
							CMapMetadata Meta{};
							str_copy(Meta.m_aCategory, pCatName ? pCatName : "");
							Meta.m_Points = Pts;
							m_mDDNetMapsMeta[aLower] = Meta;

							if(Finishes == 0)
							{
								Profile.m_vUnfinishedMaps.push_back(pMapName);
							}
						}
					}
				}
			}
		}
	}

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


// ==========================================
// King of Gores (KoG) Profile Operations
// ==========================================

void CPlayerProfiles::ConfigureKoGRequest(const std::shared_ptr<IHttpRequest> &pRequest, bool WithOrigin)
{
	if(m_aKoGCookieFile[0] == '\0' && Storage())
	{
		Storage()->GetCompletePath(IStorage::TYPE_SAVE, "aclient/kog_cookies.txt", m_aKoGCookieFile, sizeof(m_aKoGCookieFile));
		Storage()->CreateFolder("aclient", IStorage::TYPE_SAVE);
	}
	if(m_aKoGCookieFile[0] != '\0')
	{
		pRequest->CookieFile(m_aKoGCookieFile);
	}
	pRequest->Header("User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/149.0.0.0 Safari/537.36");
	pRequest->Header("Accept: */*");
	pRequest->Header("Accept-Language: ru-RU,ru;q=0.9");
	pRequest->Header("Referer: https://kog.tw/");
	if(WithOrigin)
	{
		pRequest->Header("Origin: https://kog.tw");
	}
	pRequest->Timeout(CTimeout{8000, 30000, 0, 0});
	pRequest->LogProgress(HTTPLOG::FAILURE);
	AClientBackend::ApplyDownloadSpeedLimit(pRequest, 4 * 1024 * 1024);
}

void CPlayerProfiles::StartKoGCsrfRequest()
{
	m_pKoGHttpRequest = HttpGet("https://kog.tw/api.php?type=csrf-token");
	ConfigureKoGRequest(m_pKoGHttpRequest, false);
	m_KoGFetchStage = EKoGFetchStage::CSRF;
	Http()->Run(m_pKoGHttpRequest);
}

void CPlayerProfiles::StartKoGPlayerRequest()
{
	if(!str_utf8_check(m_aKoGRequestedName) || !str_utf8_check(m_KoGNonce.c_str()))
	{
		m_KoGState = EProfileState::ERROR_NETWORK;
		str_copy(m_aKoGStatusMsg, "Error: caracteres invalidos en la consulta a KoG.tw");
		m_CurrentKoGProfile.m_Valid = false;
		m_KoGFetchStage = EKoGFetchStage::NONE;
		m_KoGNonce.clear();
		return;
	}

	char aEscapedPlayer[256];
	char aEscapedNonce[256];
	EscapeJson(aEscapedPlayer, sizeof(aEscapedPlayer), m_aKoGRequestedName);
	EscapeJson(aEscapedNonce, sizeof(aEscapedNonce), m_KoGNonce.c_str());

	char aBody[768];
	str_format(aBody, sizeof(aBody), "{\"nonce\":\"%s\",\"type\":\"players\",\"player\":\"%s\",\"tz\":\"Europe/Moscow\"}", aEscapedNonce, aEscapedPlayer);

	m_pKoGHttpRequest = HttpPostJson("https://kog.tw/api.php", aBody);
	ConfigureKoGRequest(m_pKoGHttpRequest, true);
	m_KoGFetchStage = EKoGFetchStage::PLAYER;
	Http()->Run(m_pKoGHttpRequest);
}

void CPlayerProfiles::AdvanceKoGFetch()
{
	if(!m_pKoGHttpRequest || !m_pKoGHttpRequest->Done())
		return;

	auto MarkFailed = [this]() {
		char aLowerKey[MAX_NAME_LENGTH];
		str_copy(aLowerKey, m_aKoGRequestedName);
		for(char *p = aLowerKey; *p; ++p)
			*p = (char)tolower((unsigned char)*p);
		m_mFailedFetchesKoG[aLowerKey] = time_get() + time_freq() * 60;
	};

	if(m_pKoGHttpRequest->State() != EHttpState::DONE)
	{
		log_warn("kog", "Fallo HTTP en etapa %d para '%s' (estado %d)", (int)m_KoGFetchStage, m_aKoGRequestedName, (int)m_pKoGHttpRequest->State());
		m_pKoGHttpRequest = nullptr;
		m_KoGFetchStage = EKoGFetchStage::NONE;
		m_KoGNonce.clear();
		m_KoGState = EProfileState::ERROR_NETWORK;
		str_copy(m_aKoGStatusMsg, "Error al conectar con KoG.tw");
		m_CurrentKoGProfile.m_Valid = false;
		MarkFailed();
		return;
	}

	if(m_KoGFetchStage == EKoGFetchStage::SESSION)
	{
		m_pKoGHttpRequest = nullptr;
		StartKoGCsrfRequest();
		return;
	}

	if(m_KoGFetchStage == EKoGFetchStage::CSRF)
	{
		m_KoGNonce.clear();
		json_value *pCsrfJson = m_pKoGHttpRequest->ResultJson();
		if(pCsrfJson)
		{
			const json_value *pNonce = json_object_get(pCsrfJson, "nonce");
			if(pNonce->type == json_string && pNonce->u.string.ptr && pNonce->u.string.ptr[0] != '\0')
			{
				m_KoGNonce = pNonce->u.string.ptr;
			}
			json_value_free(pCsrfJson);
		}
		m_pKoGHttpRequest = nullptr;

		if(m_KoGNonce.empty())
		{
			m_KoGFetchStage = EKoGFetchStage::NONE;
			m_KoGState = EProfileState::ERROR_NETWORK;
			str_copy(m_aKoGStatusMsg, "Error: no se pudo obtener el token CSRF de KoG.tw");
			m_CurrentKoGProfile.m_Valid = false;
			MarkFailed();
			return;
		}

		StartKoGPlayerRequest();
		return;
	}

	if(m_KoGFetchStage == EKoGFetchStage::PLAYER)
	{
		if(!ProcessKoGPlayerResponse())
		{
			MarkFailed();
		}
		m_pKoGHttpRequest = nullptr;
		m_KoGFetchStage = EKoGFetchStage::NONE;
		m_KoGNonce.clear();

		if(!m_vTeamPlayers.empty())
			ComputeTeamCommonUnfinished();
	}
}

bool CPlayerProfiles::ProcessKoGPlayerResponse()
{
	if(!m_pKoGHttpRequest)
		return false;

	if(m_pKoGHttpRequest->StatusCode() != 200)
	{
		m_KoGState = EProfileState::ERROR_NETWORK;
		str_format(m_aKoGStatusMsg, sizeof(m_aKoGStatusMsg), "Error de KoG.tw (HTTP %d)", m_pKoGHttpRequest->StatusCode());
		m_CurrentKoGProfile.m_Valid = false;
		return false;
	}

	json_value *pOuter = m_pKoGHttpRequest->ResultJson();
	if(!pOuter)
	{
		m_KoGState = EProfileState::ERROR_NETWORK;
		str_copy(m_aKoGStatusMsg, "Error: respuesta JSON invalida de KoG");
		m_CurrentKoGProfile.m_Valid = false;
		return false;
	}

	const json_value *pData = (pOuter->type == json_object) ? json_object_get(pOuter, "data") : &json_value_none;
	if(pData->type != json_string || !pData->u.string.ptr || pData->u.string.length == 0 || pData->u.string.ptr[0] == '\0')
	{
		m_KoGState = EProfileState::NOT_FOUND;
		str_format(m_aKoGStatusMsg, sizeof(m_aKoGStatusMsg), "Jugador '%s' no encontrado en KoG.tw", m_aKoGRequestedName);
		m_CurrentKoGProfile.m_Valid = false;
		json_value_free(pOuter);
		return false;
	}

	CKoGPlayerProfile NewProfile{};
	if(!ParseKoGProfileJson(pData->u.string.ptr, NewProfile))
	{
		m_KoGState = EProfileState::ERROR_NETWORK;
		str_format(m_aKoGStatusMsg, sizeof(m_aKoGStatusMsg), "Error al procesar datos de '%s' en KoG", m_aKoGRequestedName);
		log_error("kog", "ParseKoGProfileJson fallo para '%s'", m_aKoGRequestedName);
		m_CurrentKoGProfile.m_Valid = false;
		json_value_free(pOuter);
		return false;
	}

	NewProfile.m_FromDiskCache = false;
	m_CurrentKoGProfile = NewProfile;
	AddToKoGCache(NewProfile);

	m_KoGState = EProfileState::READY;
	str_copy(m_aKoGStatusMsg, "");
	json_value_free(pOuter);
	return true;
}

bool CPlayerProfiles::ParseKoGProfileJson(const char *pJsonStr, CKoGPlayerProfile &OutProfile)
{
	if(!pJsonStr || pJsonStr[0] == '\0')
		return false;

	json_value *pJson = JsonParse(pJsonStr, str_length(pJsonStr));
	if(!pJson || pJson->type != json_object)
	{
		if(pJson) json_value_free(pJson);
		return false;
	}

	const json_value &Json = *pJson;
	const json_value &Pts = Json["points"];
	if(Pts.type != json_object)
	{
		json_value_free(pJson);
		return false;
	}

	OutProfile = CKoGPlayerProfile{};

	if(Pts["Name"].type == json_string)
		str_copy(OutProfile.m_aName, (const char *)Pts["Name"]);
	else
		str_copy(OutProfile.m_aName, m_aKoGRequestedName);

	OutProfile.m_TotalPoints = Pts["TPoints"].type == json_integer ? (int)Pts["TPoints"].u.integer : 0;
	OutProfile.m_Rank = Pts["Rank"].type == json_integer ? (int)Pts["Rank"].u.integer : 0;
	OutProfile.m_Points = Pts["Points"].type == json_integer ? (int)Pts["Points"].u.integer : 0;
	OutProfile.m_SeasonPoints = Pts["Seasonpoints"].type == json_integer ? (int)Pts["Seasonpoints"].u.integer : 0;
	OutProfile.m_PvPPoints = Pts["PvPpoints"].type == json_integer ? (int)Pts["PvPpoints"].u.integer : 0;

	if(Json["time_wasted"].type == json_integer)
		OutProfile.m_TimeWasted = Json["time_wasted"].u.integer;

	const json_value &Tee = Json["last_tee"];
	if(Tee.type == json_array && Tee.u.array.length > 0)
	{
		const json_value &T0 = *Tee.u.array.values[0];
		if(T0.type == json_object)
		{
			if(T0["SkinName"].type == json_string)
				str_copy(OutProfile.m_aSkinName, (const char *)T0["SkinName"]);
			OutProfile.m_SkinColorBody = T0["SkinColorBody"].type == json_integer ? (int)T0["SkinColorBody"].u.integer : 0;
			OutProfile.m_SkinColorFeet = T0["SkinColorFeet"].type == json_integer ? (int)T0["SkinColorFeet"].u.integer : 0;
		}
	}

	// Categories
	auto AddCat = [&](const char *pName, const char *pFinKey, const char *pTotKey) {
		CKoGCategoryStats Cat{};
		str_copy(Cat.m_aName, pName);
		Cat.m_Finished = Json[pFinKey].type == json_integer ? (int)Json[pFinKey].u.integer : 0;
		Cat.m_Total = Json[pTotKey].type == json_integer ? (int)Json[pTotKey].u.integer : 0;
		OutProfile.m_vCategories.push_back(Cat);
	};

	AddCat("Solo", "fin_sol", "sol_maps");
	AddCat("Easy", "fin_main", "easy_maps");
	AddCat("Main", "fin_mn", "main_maps");
	AddCat("Hard", "fin_hrd", "hard_maps");
	AddCat("Insane", "fin_ins", "ins_maps");
	AddCat("Extreme", "fin_ext", "ext_maps");
	AddCat("Moderate", "fin_mod", "mod_maps");

	// Finished maps
	const json_value &Finishes = Json["mapsFinished"];
	if(Finishes.type == json_array)
	{
		OutProfile.m_vFinishes.reserve(Finishes.u.array.length);
		for(unsigned int i = 0; i < Finishes.u.array.length; ++i)
		{
			const json_value &F = *Finishes.u.array.values[i];
			if(F.type != json_object || F["Map"].type != json_string)
				continue;

			CKoGFinish Fin{};
			str_copy(Fin.m_aMap, (const char *)F["Map"]);
			Fin.m_Time = (float)(F["T"].type == json_double ? F["T"].u.dbl : (F["T"].type == json_integer ? (double)F["T"].u.integer : 0.0));
			Fin.m_Count = F["count"].type == json_integer ? (int)F["count"].u.integer : 1;
			if(F["Server"].type == json_string)
				str_copy(Fin.m_aServer, (const char *)F["Server"]);
			if(F["TS"].type == json_string)
				str_copy(Fin.m_aTimestamp, (const char *)F["TS"]);
			OutProfile.m_vFinishes.push_back(Fin);
		}
	}

	// Unfinished maps
	const json_value &Unfinished = Json["unfinishedMaps"];
	if(Unfinished.type == json_array)
	{
		for(unsigned int i = 0; i < Unfinished.u.array.length; ++i)
		{
			const json_value &U = *Unfinished.u.array.values[i];
			if(U.type == json_object && U["Map"].type == json_string)
			{
				OutProfile.m_vUnfinishedMaps.push_back((const char *)U["Map"]);
			}
		}
	}

	// Teammates
	const json_value &Teammates = Json["lastteammates"];
	if(Teammates.type == json_array)
	{
		for(unsigned int i = 0; i < Teammates.u.array.length; ++i)
		{
			const json_value &T = *Teammates.u.array.values[i];
			if(T.type == json_object && T["Namee"].type == json_string)
			{
				CKoGTeammate Tm{};
				str_copy(Tm.m_aName, (const char *)T["Namee"]);
				Tm.m_Finishes = T["finishesnumber"].type == json_integer ? (int)T["finishesnumber"].u.integer : 0;
				OutProfile.m_vTeammates.push_back(Tm);
			}
		}
	}

	OutProfile.m_CachedTime = time_timestamp();
	OutProfile.m_Valid = true;
	json_value_free(pJson);
	return true;
}

void CPlayerProfiles::AddToKoGCache(const CKoGPlayerProfile &Profile)
{
	char aLower[MAX_NAME_LENGTH];
	str_copy(aLower, Profile.m_aName);
	for(char *p = aLower; *p; ++p)
		*p = (char)tolower((unsigned char)*p);
	std::string Key(aLower);
	m_mKoGProfilesCache[Key] = Profile;

	m_dKoGCacheOrder.erase(std::remove(m_dKoGCacheOrder.begin(), m_dKoGCacheOrder.end(), Key), m_dKoGCacheOrder.end());
	m_dKoGCacheOrder.push_back(Key);
	while(m_dKoGCacheOrder.size() > MAX_CACHE_ENTRIES)
	{
		std::string Oldest = m_dKoGCacheOrder.front();
		m_dKoGCacheOrder.pop_front();
		m_mKoGProfilesCache.erase(Oldest);
	}
}

bool CPlayerProfiles::GetFromKoGCache(const char *pName, CKoGPlayerProfile &OutProfile)
{
	if(!pName || pName[0] == '\0')
		return false;
	char aLower[MAX_NAME_LENGTH];
	str_copy(aLower, pName);
	for(char *p = aLower; *p; ++p)
		*p = (char)tolower((unsigned char)*p);
	std::string Key(aLower);
	auto It = m_mKoGProfilesCache.find(Key);
	if(It != m_mKoGProfilesCache.end())
	{
		OutProfile = It->second;
		m_dKoGCacheOrder.erase(std::remove(m_dKoGCacheOrder.begin(), m_dKoGCacheOrder.end(), Key), m_dKoGCacheOrder.end());
		m_dKoGCacheOrder.push_back(Key);
		return true;
	}
	return false;
}

void CPlayerProfiles::FetchKoGProfile(const char *pPlayerName, bool ForceRefresh, bool BackgroundTeam)
{
	(void)ForceRefresh;
	(void)BackgroundTeam;

	if(!pPlayerName || pPlayerName[0] == '\0')
		return;

	char aClean[MAX_NAME_LENGTH];
	str_copy(aClean, pPlayerName);
	char *pStart = aClean;
	while(*pStart && (unsigned char)*pStart <= ' ')
		pStart++;
	char *pEnd = pStart + str_length(pStart);
	while(pEnd > pStart && (unsigned char)*(pEnd - 1) <= ' ')
		pEnd--;
	*pEnd = '\0';

	if(pStart[0] == '\0')
		return;

	str_copy(m_aKoGRequestedName, pStart);

	AbortKoGFetch();

	m_KoGState = EProfileState::FETCHING;
	str_format(m_aKoGStatusMsg, sizeof(m_aKoGStatusMsg), "Consultando datos en KoG.tw para '%s'...", pStart);

	m_pKoGHttpRequest = HttpGet("https://kog.tw");
	ConfigureKoGRequest(m_pKoGHttpRequest, false);
	m_KoGFetchStage = EKoGFetchStage::SESSION;
	Http()->Run(m_pKoGHttpRequest);
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
	str_format(aUrl, sizeof(aUrl), "https://kog.tw/#p=players&player=%s", aEscaped);
	Client()->ViewLink(aUrl);
}

void CPlayerProfiles::ConProfile(IConsole::IResult *pResult, void *pUserData)
{
	CPlayerProfiles *pSelf = (CPlayerProfiles *)pUserData;
	const char *pName = pResult->GetString(0);
	if(!pName || pName[0] == '\0')
		pName = pSelf->Client()->PlayerName();

	if(g_Config.m_AcProfileSource == 1)
		pSelf->FetchKoGProfile(pName, true);
	else
		pSelf->FetchProfile(pName, true);

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
	Console()->Register("profile", "?s[name]", CFGFLAG_CLIENT, ConProfile, this, "View player DDNet or KoG profile in AClient");
	Console()->Register("profile_web_ddnet", "?s[name]", CFGFLAG_CLIENT, ConProfileWebDDNet, this, "Open player profile in DDNet website");
	Console()->Register("profile_web_kog", "?s[name]", CFGFLAG_CLIENT, ConProfileWebKoG, this, "Open player profile in King of Gores website");
}

// ==========================================
// Team Unfinished Maps Comparison Operations
// ==========================================

bool CPlayerProfiles::IsPlayerProfileLoaded(const char *pName, bool IsKoG) const
{
	if(!pName || pName[0] == '\0')
		return false;

	char aLower[MAX_NAME_LENGTH];
	str_copy(aLower, pName);
	for(char *p = aLower; *p; ++p)
		*p = (char)tolower((unsigned char)*p);
	std::string Key(aLower);

	if(IsKoG)
	{
		return m_mKoGProfilesCache.find(Key) != m_mKoGProfilesCache.end();
	}
	else
	{
		return m_mProfilesCache.find(Key) != m_mProfilesCache.end();
	}
}

void CPlayerProfiles::QueueTeamProfileFetch(const char *pName, bool IsKoG)
{
	if(!pName || pName[0] == '\0')
		return;

	if(IsPlayerProfileLoaded(pName, IsKoG))
		return;

	char aLowerQ[MAX_NAME_LENGTH];
	str_copy(aLowerQ, pName);
	for(char *p = aLowerQ; *p; ++p)
		*p = (char)tolower((unsigned char)*p);
	std::string Key(aLowerQ);

	std::string NameStr(pName);
	if(IsKoG)
	{
		auto it = m_mFailedFetchesKoG.find(Key);
		if(it != m_mFailedFetchesKoG.end() && time_get() < it->second)
			return;
		if(str_comp(m_aKoGRequestedName, pName) == 0 && m_pKoGHttpRequest != nullptr)
			return;
		for(const auto &Pending : m_qTeamPendingKoG)
		{
			if(str_comp(Pending.c_str(), pName) == 0)
				return;
		}
		m_qTeamPendingKoG.push_back(NameStr);
	}
	else
	{
		auto it = m_mFailedFetchesDDNet.find(Key);
		if(it != m_mFailedFetchesDDNet.end() && time_get() < it->second)
			return;
		if(str_comp(m_aRequestedName, pName) == 0 && m_pHttpRequest != nullptr)
			return;
		for(const auto &Pending : m_qTeamPendingDDNet)
		{
			if(str_comp(Pending.c_str(), pName) == 0)
				return;
		}
		m_qTeamPendingDDNet.push_back(NameStr);
	}
}

void CPlayerProfiles::AddTeamPlayer(const char *pName)
{
	if(!pName || pName[0] == '\0')
		return;

	for(const auto &Existing : m_vTeamPlayers)
	{
		if(str_comp(Existing.c_str(), pName) == 0)
			return;
	}

	m_vTeamPlayers.push_back(pName);
	QueueTeamProfileFetch(pName, g_Config.m_AcProfileSource == 1);
	ComputeTeamCommonUnfinished();
}

void CPlayerProfiles::RemoveTeamPlayer(const char *pName)
{
	if(!pName || pName[0] == '\0')
		return;

	m_vTeamPlayers.erase(
		std::remove_if(m_vTeamPlayers.begin(), m_vTeamPlayers.end(),
			[&](const std::string &Item) { return str_comp(Item.c_str(), pName) == 0; }),
		m_vTeamPlayers.end());

	ComputeTeamCommonUnfinished();
}

void CPlayerProfiles::ClearTeamPlayers()
{
	m_vTeamPlayers.clear();
	m_qTeamPendingDDNet.clear();
	m_qTeamPendingKoG.clear();
	m_vCommonUnfinishedMaps.clear();
	m_TeamComparisonReady = false;
	str_copy(m_aTeamStatusMsg, "");
}

void CPlayerProfiles::AutoDetectCurrentTeam()
{
	if(!GameClient() || !Client() || Client()->State() != IClient::STATE_ONLINE)
		return;

	int LocalId = GameClient()->m_Snap.m_LocalClientId;
	if(LocalId < 0 || LocalId >= MAX_CLIENTS)
		return;

	int MyTeam = GameClient()->m_Teams.Team(LocalId);
	if(MyTeam == 0)
	{
		const char *pMyName = Client()->PlayerName();
		if(pMyName && pMyName[0] != '\0')
			AddTeamPlayer(pMyName);
		return;
	}

	ClearTeamPlayers();
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(GameClient()->m_aClients[i].m_Active && GameClient()->m_Teams.SameTeam(LocalId, i))
		{
			const char *pName = GameClient()->m_aClients[i].m_aName;
			if(pName && pName[0] != '\0')
				AddTeamPlayer(pName);
		}
	}

	ComputeTeamCommonUnfinished();
}

void CPlayerProfiles::ComputeTeamCommonUnfinished()
{
	m_vCommonUnfinishedMaps.clear();
	m_TeamComparisonReady = false;

	if(m_vTeamPlayers.empty())
	{
		str_copy(m_aTeamStatusMsg, "Agrega jugadores al equipo para comparar mapas pendientes");
		return;
	}

	const bool IsKoG = (g_Config.m_AcProfileSource == 1);

	for(const auto &Name : m_vTeamPlayers)
	{
		QueueTeamProfileFetch(Name.c_str(), IsKoG);
	}

	bool AllLoaded = true;
	std::string PendingName = "";
	for(const auto &Name : m_vTeamPlayers)
	{
		if(!IsPlayerProfileLoaded(Name.c_str(), IsKoG))
		{
			AllLoaded = false;
			PendingName = Name;
			break;
		}
	}

	if(!AllLoaded)
	{
		str_format(m_aTeamStatusMsg, sizeof(m_aTeamStatusMsg), "Cargando perfil de '%s'...", PendingName.c_str());
		return;
	}

	std::map<std::string, int> MapCounts;

	for(const auto &Name : m_vTeamPlayers)
	{
		std::vector<std::string> UnfinList;
		if(IsKoG)
		{
			CKoGPlayerProfile Prof{};
			if(GetFromKoGCache(Name.c_str(), Prof))
			{
				UnfinList = Prof.m_vUnfinishedMaps;
			}
		}
		else
		{
			CDDNetPlayerProfile Prof{};
			if(GetFromCache(Name.c_str(), Prof))
			{
				UnfinList = Prof.m_vUnfinishedMaps;
			}
		}

		std::set<std::string> UniqueUnfin(UnfinList.begin(), UnfinList.end());
		for(const auto &MapName : UniqueUnfin)
		{
			MapCounts[MapName]++;
		}
	}

	EnsureKoGMapsMetaLoaded();

	const int TeamSize = (int)m_vTeamPlayers.size();
	for(const auto &Pair : MapCounts)
	{
		if(Pair.second == TeamSize)
		{
			CTeamUnfinishedMap M{};
			str_copy(M.m_aMap, Pair.first.c_str());

			if(IsKoG)
			{
				CMapMetadata Meta{};
				if(GetKoGMapMeta(Pair.first.c_str(), Meta))
				{
					str_copy(M.m_aCategory, Meta.m_aCategory);
					M.m_Points = Meta.m_Points;
				}
				else
				{
					str_copy(M.m_aCategory, "KoG");
					M.m_Points = 0;
				}
			}
			else
			{
				CMapMetadata Meta{};
				if(GetDDNetMapMeta(Pair.first.c_str(), Meta))
				{
					str_copy(M.m_aCategory, Meta.m_aCategory);
					M.m_Points = Meta.m_Points;
				}
				else
				{
					str_copy(M.m_aCategory, "DDNet");
					M.m_Points = 0;
				}
			}

			m_vCommonUnfinishedMaps.push_back(M);
		}
	}

	std::sort(m_vCommonUnfinishedMaps.begin(), m_vCommonUnfinishedMaps.end(),
		[](const CTeamUnfinishedMap &a, const CTeamUnfinishedMap &b) {
			return str_comp_nocase(a.m_aMap, b.m_aMap) < 0;
		});

	m_TeamComparisonReady = true;
	str_format(m_aTeamStatusMsg, sizeof(m_aTeamStatusMsg), "%zu mapas incompletos en comun para el equipo", m_vCommonUnfinishedMaps.size());
}

bool CPlayerProfiles::ParseKoGMapsMetaJson(const char *pJsonStr, size_t Length)
{
	if(!pJsonStr || Length == 0)
		return false;

	json_value *pJson = JsonParse(pJsonStr, Length);
	if(!pJson)
		return false;

	if(pJson->type == json_object)
	{
		for(unsigned int i = 0; i < pJson->u.object.length; ++i)
		{
			const char *pMapName = pJson->u.object.values[i].name;
			const json_value &Val = *pJson->u.object.values[i].value;
			if(Val.type == json_object && pMapName && pMapName[0])
			{
				CMapMetadata Meta{};
				if(Val["cat"].type == json_string)
					str_copy(Meta.m_aCategory, (const char *)Val["cat"]);
				if(Val["pts"].type == json_integer)
					Meta.m_Points = (int)Val["pts"].u.integer;

				char aLower[64];
				str_copy(aLower, pMapName);
				for(char *p = aLower; *p; ++p) *p = (char)tolower((unsigned char)*p);
				m_mKoGMapsMeta[aLower] = Meta;
			}
		}
	}

	json_value_free(pJson);
	return !m_mKoGMapsMeta.empty();
}

void CPlayerProfiles::EnsureKoGMapsMetaLoaded()
{
	if(m_KoGMapsMetaLoaded)
		return;

	IOHANDLE File = Storage()->OpenFile("kog_cache/maps.json", IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
	{
		File = Storage()->OpenFile("tclient/kog_maps.json", IOFLAG_READ, IStorage::TYPE_ALL);
	}
	if(!File)
	{
		File = Storage()->OpenFile("data/tclient/kog_maps.json", IOFLAG_READ, IStorage::TYPE_ALL);
	}

	if(File)
	{
		int64_t FileSize = io_length(File);
		if(FileSize > 0 && FileSize <= 10 * 1024 * 1024)
		{
			std::string Content;
			Content.resize((size_t)FileSize);
			io_read(File, &Content[0], (unsigned int)FileSize);
			io_close(File);
			File = nullptr;

			if(ParseKoGMapsMetaJson(Content.c_str(), Content.size()))
			{
				m_KoGMapsMetaLoaded = true;
			}
		}
		if(File)
		{
			io_close(File);
			File = nullptr;
		}
	}

	if(!m_KoGMapsMetaLoaded)
	{
		if(ParseKoGMapsMetaJson((const char *)s_aDefaultKoGMapsData, s_DefaultKoGMapsDataSize))
		{
			m_KoGMapsMetaLoaded = true;
			log_info("kog", "KoG maps metadata loaded from built-in fallback (%zu maps)", m_mKoGMapsMeta.size());
		}
	}

	FetchKoGMapsMetaNetwork();
}

void CPlayerProfiles::FetchKoGMapsMetaNetwork()
{
	if(m_pKoGMapsHttpRequest)
		return;

	Storage()->CreateFolder("kog_cache", IStorage::TYPE_SAVE);
	m_pKoGMapsHttpRequest = HttpGetFile("https://raw.githubusercontent.com/AgusClient/AClient/master/data/tclient/kog_maps.json", Storage(), "kog_cache/maps.json", IStorage::TYPE_SAVE);
	m_pKoGMapsHttpRequest->Timeout(CTimeout{4000, 10000, 500, 5});
	m_pKoGMapsHttpRequest->SkipByFileTime(true);
	m_pKoGMapsHttpRequest->IpResolve(IPRESOLVE::V4);
	AClientBackend::ApplyDownloadSpeedLimit(m_pKoGMapsHttpRequest, 2 * 1024 * 1024);
	Http()->Run(m_pKoGMapsHttpRequest);
}

void CPlayerProfiles::ProcessKoGMapsMetaResponse()
{
	if(!m_pKoGMapsHttpRequest)
		return;

	if(m_pKoGMapsHttpRequest->State() == EHttpState::DONE && m_pKoGMapsHttpRequest->StatusCode() == 200)
	{
		IOHANDLE File = Storage()->OpenFile("kog_cache/maps.json", IOFLAG_READ, IStorage::TYPE_SAVE);
		if(File)
		{
			const int64_t FileSize = io_length(File);
			if(FileSize > 0 && FileSize <= 10 * 1024 * 1024)
			{
				std::string Content;
				Content.resize((size_t)FileSize);
				io_read(File, &Content[0], (unsigned int)FileSize);
				if(ParseKoGMapsMetaJson(Content.c_str(), Content.size()))
				{
					m_KoGMapsMetaLoaded = true;
					log_info("kog", "KoG maps metadata updated from network (%zu maps)", m_mKoGMapsMeta.size());

					if(m_TeamComparisonReady && !m_vTeamPlayers.empty())
					{
						ComputeTeamCommonUnfinished();
					}
				}
			}
			io_close(File);
		}
	}
	m_pKoGMapsHttpRequest = nullptr;
}

bool CPlayerProfiles::GetKoGMapMeta(const char *pMapName, CMapMetadata &OutMeta)
{
	if(!pMapName || !pMapName[0])
		return false;

	EnsureKoGMapsMetaLoaded();

	char aLower[64];
	str_copy(aLower, pMapName);
	for(char *p = aLower; *p; ++p) *p = (char)tolower((unsigned char)*p);

	auto It = m_mKoGMapsMeta.find(aLower);
	if(It != m_mKoGMapsMeta.end())
	{
		OutMeta = It->second;
		return true;
	}
	return false;
}

bool CPlayerProfiles::GetDDNetMapMeta(const char *pMapName, CMapMetadata &OutMeta)
{
	if(!pMapName || !pMapName[0])
		return false;

	char aLower[64];
	str_copy(aLower, pMapName);
	for(char *p = aLower; *p; ++p) *p = (char)tolower((unsigned char)*p);

	auto It = m_mDDNetMapsMeta.find(aLower);
	if(It != m_mDDNetMapsMeta.end())
	{
		OutMeta = It->second;
		return true;
	}
	return false;
}
