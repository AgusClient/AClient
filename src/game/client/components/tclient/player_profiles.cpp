#include <set>
#include "player_profiles.h"
#include <base/io.h>
#include <base/log.h>
#include <base/str.h>
#include <base/thread.h>
#include <base/time.h>
#include <engine/client.h>
#include <engine/http.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <engine/storage.h>
#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/localization.h>

#include <curl/curl.h>

#include <algorithm>
#include <cctype>

static size_t CurlStringWriteCallback(void *pContents, size_t Size, size_t Nmemb, void *pUser)
{
	size_t TotalSize = Size * Nmemb;
	std::string *pStr = static_cast<std::string *>(pUser);
	pStr->append(static_cast<char *>(pContents), TotalSize);
	return TotalSize;
}

static std::string EscapeForJson(const char *pStr)
{
	std::string Out;
	if(!pStr)
		return Out;
	for(const char *p = pStr; *p; ++p)
	{
		if(*p == '"') Out += "\\\"";
		else if(*p == '\\') Out += "\\\\";
		else if(*p == '\n') Out += "\\n";
		else if(*p == '\r') Out += "\\r";
		else if(*p == '\t') Out += "\\t";
		else Out += *p;
	}
	return Out;
}

CPlayerProfiles::CPlayerProfiles()
{
	m_CurrentProfile.m_Valid = false;
	m_CurrentKoGProfile.m_Valid = false;
}

CPlayerProfiles::~CPlayerProfiles()
{
	AbortCurrentFetch();
	AbortKoGFetch();
}

void CPlayerProfiles::OnReset()
{
	AbortCurrentFetch();
	AbortKoGFetch();
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
	if(m_KoGFetchRunning)
	{
		if(m_pKoGThread)
		{
			thread_wait(m_pKoGThread);
			m_pKoGThread = nullptr;
		}
		m_KoGFetchRunning = false;
		m_KoGFetchDone = false;
	}
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

	AbortCurrentFetch();

	char aEscaped[256];
	EscapeUrl(aEscaped, pStart);
	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "https://ddnet.org/players/?json2=%s", aEscaped);

	m_pHttpRequest = HttpGet(aUrl);
	m_pHttpRequest->Timeout(CTimeout{5000, 5000, 500, 5});
	m_pHttpRequest->IpResolve(IPRESOLVE::V4);
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

	// 2. Check KoG background fetch thread completion
	if(m_KoGFetchRunning && m_KoGFetchDone)
	{
		ProcessKoGFetchCompletion();

		if(!m_vTeamPlayers.empty())
			ComputeTeamCommonUnfinished();
	}

	// 3. Process queued team background fetches
	if(!m_pHttpRequest && !m_qTeamPendingDDNet.empty())
	{
		std::string Next = m_qTeamPendingDDNet.front();
		m_qTeamPendingDDNet.pop_front();
		FetchProfile(Next.c_str(), false);
	}

	if(!m_KoGFetchRunning && !m_qTeamPendingKoG.empty())
	{
		std::string Next = m_qTeamPendingKoG.front();
		m_qTeamPendingKoG.pop_front();
		FetchKoGProfile(Next.c_str(), false);
	}
}

void CPlayerProfiles::ProcessHttpResponse()
{
	if(!m_pHttpRequest)
		return;

	json_value *pJson = m_pHttpRequest->ResultJson();
	if(!pJson)
	{
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

#if defined(CONF_FAMILY_WINDOWS)
#include <windows.h>
#include <cstdio>

static bool ExecuteProcessCaptureOutput(const wchar_t *pCommandLine, std::string &Output)
{
	HANDLE hReadPipe = nullptr, hWritePipe = nullptr;
	SECURITY_ATTRIBUTES sa{};
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = nullptr;

	if(!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
		return false;

	SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOW si{};
	si.cb = sizeof(STARTUPINFOW);
	si.hStdOutput = hWritePipe;
	si.hStdError = hWritePipe;
	si.dwFlags |= STARTF_USESTDHANDLES;

	PROCESS_INFORMATION pi{};
	wchar_t aCmd[4096];
	wcsncpy(aCmd, pCommandLine, 4095);
	aCmd[4095] = L'\0';

	BOOL Success = CreateProcessW(
		nullptr,
		aCmd,
		nullptr,
		nullptr,
		TRUE,
		CREATE_NO_WINDOW,
		nullptr,
		nullptr,
		&si,
		&pi
	);

	CloseHandle(hWritePipe);

	if(!Success)
	{
		CloseHandle(hReadPipe);
		return false;
	}

	char aBuffer[4096];
	DWORD BytesRead = 0;
	while(ReadFile(hReadPipe, aBuffer, sizeof(aBuffer), &BytesRead, nullptr) && BytesRead > 0)
	{
		Output.append(aBuffer, BytesRead);
	}

	CloseHandle(hReadPipe);
	WaitForSingleObject(pi.hProcess, 15000);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return true;
}

bool CPlayerProfiles::KoGFetchWorkerWindows()
{
	char aTempDir[MAX_PATH];
	if(!GetTempPathA(MAX_PATH, aTempDir))
		return false;

	DWORD Pid = GetCurrentProcessId();
	char aCookieFile[MAX_PATH];
	str_format(aCookieFile, sizeof(aCookieFile), "%skog_c_%u.txt", aTempDir, (unsigned)Pid);
	char aPostFile[MAX_PATH];
	str_format(aPostFile, sizeof(aPostFile), "%skog_p_%u.json", aTempDir, (unsigned)Pid);

	DeleteFileA(aCookieFile);
	DeleteFileA(aPostFile);

	// 1. Visit root to establish session cookie
	std::string Dummy;
	wchar_t aCmd1[2048];
	swprintf_s(aCmd1, L"C:\\Windows\\System32\\curl.exe -s -c \"%hs\" -A \"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\" -H \"Referer: https://kog.tw/\" -H \"Origin: https://kog.tw\" \"https://kog.tw/\"", aCookieFile);
	if(!ExecuteProcessCaptureOutput(aCmd1, Dummy))
	{
		DeleteFileA(aCookieFile);
		return false;
	}

	// Helper lambda to fetch a fresh CSRF nonce using the existing session cookie
	auto FetchFreshNonce = [&]() -> std::string {
		std::string FreshCsrf;
		wchar_t aCmdCsrf[2048];
		swprintf_s(aCmdCsrf, L"C:\\Windows\\System32\\curl.exe -s -b \"%hs\" -c \"%hs\" -A \"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\" -H \"Referer: https://kog.tw/\" -H \"Origin: https://kog.tw\" -H \"Accept: application/json, text/javascript, */*; q=0.01\" -H \"X-Requested-With: XMLHttpRequest\" \"https://kog.tw/api.php?type=csrf-token\"", aCookieFile, aCookieFile);
		if(!ExecuteProcessCaptureOutput(aCmdCsrf, FreshCsrf) || FreshCsrf.empty())
			return "";

		std::string FreshToken;
		json_value *pCsrf = JsonParse(FreshCsrf.c_str(), FreshCsrf.size());
		if(pCsrf)
		{
			if(pCsrf->type == json_object && (*pCsrf)["nonce"].type == json_string)
			{
				FreshToken = (const char *)(*pCsrf)["nonce"];
			}
			json_value_free(pCsrf);
		}
		if(FreshToken.empty())
		{
			size_t Pos = FreshCsrf.find("\"nonce\":\"");
			if(Pos != std::string::npos)
			{
				Pos += 9;
				size_t EndPos = FreshCsrf.find('"', Pos);
			if(EndPos != std::string::npos)
				{
					FreshToken = FreshCsrf.substr(Pos, EndPos - Pos);
				}
			}
		}
		return FreshToken;
	};

	// 2. Fetch CSRF token
	std::string Nonce = FetchFreshNonce();
	if(Nonce.empty())
	{
		DeleteFileA(aCookieFile);
		return false;
	}

	// 3. Write POST payload file without BOM
	std::string PostData = "{\"nonce\":\"" + Nonce + "\",\"type\":\"players\",\"player\":\"" + EscapeForJson(m_aKoGFetchThreadPlayer) + "\",\"tz\":\"UTC\"}";
	FILE *pFile = fopen(aPostFile, "wb");
	if(!pFile)
	{
		DeleteFileA(aCookieFile);
		return false;
	}
	fwrite(PostData.data(), 1, PostData.size(), pFile);
	fclose(pFile);

	// 4. Post request to api.php
	std::string ResultJson;
	wchar_t aCmd3[2048];
	swprintf_s(aCmd3, L"C:\\Windows\\System32\\curl.exe -s -b \"%hs\" -A \"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36\" -H \"Referer: https://kog.tw/\" -H \"Origin: https://kog.tw\" -H \"Accept: application/json, text/javascript, */*; q=0.01\" -H \"X-Requested-With: XMLHttpRequest\" -H \"Content-Type: application/json;charset=UTF-8\" -d \"@%hs\" \"https://kog.tw/api.php\"", aCookieFile, aPostFile);

	bool ExecOk = ExecuteProcessCaptureOutput(aCmd3, ResultJson);

	// If player was not found, check alternative casing variants (e.g. "tae" -> "Tae", "Akisi" -> "akisi")
	if(ExecOk && ResultJson.find("\"data\":[]") != std::string::npos)
	{
		std::string BaseName = m_aKoGFetchThreadPlayer;
		std::vector<std::string> vVariants;

		if(!BaseName.empty())
		{
			// Variant 1: Capitalized (first upper, rest lower)
			std::string Cap = BaseName;
			Cap[0] = (char)toupper((unsigned char)Cap[0]);
			for(size_t i = 1; i < Cap.size(); ++i)
				Cap[i] = (char)tolower((unsigned char)Cap[i]);
			if(Cap != BaseName)
				vVariants.push_back(Cap);

			// Variant 2: All lowercase
			std::string Low = BaseName;
			for(size_t i = 0; i < Low.size(); ++i)
				Low[i] = (char)tolower((unsigned char)Low[i]);
			if(Low != BaseName && std::find(vVariants.begin(), vVariants.end(), Low) == vVariants.end())
				vVariants.push_back(Low);

			// Variant 3: All uppercase
			std::string Up = BaseName;
			for(size_t i = 0; i < Up.size(); ++i)
				Up[i] = (char)toupper((unsigned char)Up[i]);
			if(Up != BaseName && std::find(vVariants.begin(), vVariants.end(), Up) == vVariants.end())
				vVariants.push_back(Up);
		}

		for(const auto &Variant : vVariants)
		{
			// Nonce is one-time use; fetch a fresh nonce for each variant query
			std::string FreshNonce = FetchFreshNonce();
			if(FreshNonce.empty())
				break;

			std::string PostDataVar = "{\"nonce\":\"" + FreshNonce + "\",\"type\":\"players\",\"player\":\"" + EscapeForJson(Variant.c_str()) + "\",\"tz\":\"UTC\"}";
			FILE *pVarFile = fopen(aPostFile, "wb");
			if(!pVarFile)
				break;
			fwrite(PostDataVar.data(), 1, PostDataVar.size(), pVarFile);
			fclose(pVarFile);

			std::string VarResult;
			if(ExecuteProcessCaptureOutput(aCmd3, VarResult) && !VarResult.empty())
			{
				if(VarResult.find("\"data\":[]") == std::string::npos && VarResult.find("\"status\":200") != std::string::npos)
				{
					ResultJson = VarResult;
					str_copy(m_aKoGFetchThreadPlayer, Variant.c_str());
					str_copy(m_aKoGRequestedName, Variant.c_str());
					break;
				}
			}
		}
	}
	DeleteFileA(aCookieFile);
	DeleteFileA(aPostFile);

	if(!ExecOk || ResultJson.empty())
		return false;

	m_KoGResultData = ResultJson;
	m_KoGFetchSuccess = true;
	m_KoGFetchDone = true;
	return true;
}
#endif

// ==========================================
// King of Gores (KoG) Profile Operations
// ==========================================

void CPlayerProfiles::KoGFetchWorker(void *pUser)
{
	CPlayerProfiles *pSelf = static_cast<CPlayerProfiles *>(pUser);
#if defined(CONF_FAMILY_WINDOWS)
	if(pSelf->KoGFetchWorkerWindows())
		return;
#endif
	CURL *pCurl = curl_easy_init();
	if(!pCurl)
	{
		pSelf->m_KoGFetchSuccess = false;
		pSelf->m_KoGFetchErrorMsg = "Error al inicializar cliente de red (libcurl)";
		pSelf->m_KoGFetchDone = true;
		return;
	}

	// In-memory cookie handling keeps PHPSESSID across requests
	curl_easy_setopt(pCurl, CURLOPT_COOKIEFILE, "");
	curl_easy_setopt(pCurl, CURLOPT_TIMEOUT, 20L);
	curl_easy_setopt(pCurl, CURLOPT_CONNECTTIMEOUT, 10L);
	curl_easy_setopt(pCurl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
	curl_easy_setopt(pCurl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(pCurl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(pCurl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(pCurl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(pCurl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/130.0.0.0 Safari/537.36");
	curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, CurlStringWriteCallback);

	// Setup browser headers required by Cloudflare protection on kog.tw
	struct curl_slist *pHeaders = nullptr;
	pHeaders = curl_slist_append(pHeaders, "Referer: https://kog.tw/");
	pHeaders = curl_slist_append(pHeaders, "Origin: https://kog.tw");
	pHeaders = curl_slist_append(pHeaders, "Accept: application/json, text/plain, */*");
	pHeaders = curl_slist_append(pHeaders, "X-Requested-With: XMLHttpRequest");
	pHeaders = curl_slist_append(pHeaders, "Accept-Language: en-US,en;q=0.9");
	curl_easy_setopt(pCurl, CURLOPT_HTTPHEADER, pHeaders);

	// 1. Visit root to establish PHP session
	std::string InitSink;
	curl_easy_setopt(pCurl, CURLOPT_URL, "https://kog.tw/");
	curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &InitSink);
	CURLcode Res = curl_easy_perform(pCurl);
	if(Res != CURLE_OK)
	{
		pSelf->m_KoGFetchSuccess = false;
		pSelf->m_KoGFetchErrorMsg = "No se pudo conectar con kog.tw";
		curl_slist_free_all(pHeaders);
		curl_easy_cleanup(pCurl);
		pSelf->m_KoGFetchDone = true;
		return;
	}

	// 2. Fetch CSRF nonce with session cookie and Referer
	std::string CsrfJson;
	curl_easy_setopt(pCurl, CURLOPT_URL, "https://kog.tw/api.php?type=csrf-token");
	curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &CsrfJson);
	Res = curl_easy_perform(pCurl);
	if(Res != CURLE_OK)
	{
		pSelf->m_KoGFetchSuccess = false;
		pSelf->m_KoGFetchErrorMsg = "Error al obtener token de sesion de kog.tw";
		curl_slist_free_all(pHeaders);
		curl_easy_cleanup(pCurl);
		pSelf->m_KoGFetchDone = true;
		return;
	}

	std::string Nonce;
	json_value *pCsrfJson = JsonParse(CsrfJson.c_str(), CsrfJson.size());
	if(pCsrfJson)
	{
		if(pCsrfJson->type == json_object && (*pCsrfJson)["nonce"].type == json_string)
		{
			Nonce = (const char *)(*pCsrfJson)["nonce"];
		}
		json_value_free(pCsrfJson);
	}
	if(Nonce.empty())
	{
		size_t Pos = CsrfJson.find("\"nonce\":\"");
		if(Pos != std::string::npos)
		{
			Pos += 9;
			size_t EndPos = CsrfJson.find("\"", Pos);
			if(EndPos != std::string::npos)
			{
				Nonce = CsrfJson.substr(Pos, EndPos - Pos);
			}
		}
	}

	if(Nonce.empty())
	{
		pSelf->m_KoGFetchSuccess = false;
		log_error("kog", "Respuesta CSRF inesperada de kog.tw: %s", CsrfJson.c_str());
		if(CsrfJson.find("Just a moment") != std::string::npos || CsrfJson.find("cf-mitigated") != std::string::npos)
			pSelf->m_KoGFetchErrorMsg = "Proteccion Cloudflare activa en kog.tw";
		else
			pSelf->m_KoGFetchErrorMsg = "Token de sesion invalido recibido de kog.tw";
		curl_slist_free_all(pHeaders);
		curl_easy_cleanup(pCurl);
		pSelf->m_KoGFetchDone = true;
		return;
	}

	// 3. Post to api.php requesting player profile data
	pHeaders = curl_slist_append(pHeaders, "Content-Type: application/json;charset=UTF-8");
	curl_easy_setopt(pCurl, CURLOPT_HTTPHEADER, pHeaders);

	std::string PostData = "{\"nonce\":\"" + Nonce + "\",\"type\":\"players\",\"player\":\"" + EscapeForJson(pSelf->m_aKoGFetchThreadPlayer) + "\",\"tz\":\"UTC\"}";
	std::string ResponseJson;

	curl_easy_setopt(pCurl, CURLOPT_URL, "https://kog.tw/api.php");
	curl_easy_setopt(pCurl, CURLOPT_POST, 1L);
	curl_easy_setopt(pCurl, CURLOPT_POSTFIELDS, PostData.c_str());
	curl_easy_setopt(pCurl, CURLOPT_POSTFIELDSIZE, (long)PostData.size());
	curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &ResponseJson);

	Res = curl_easy_perform(pCurl);
	curl_slist_free_all(pHeaders);

	if(Res != CURLE_OK)
	{
		pSelf->m_KoGFetchSuccess = false;
		pSelf->m_KoGFetchErrorMsg = "Error al consultar perfil en kog.tw";
		curl_easy_cleanup(pCurl);
		pSelf->m_KoGFetchDone = true;
		return;
	}

	long Code = 0;
	curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &Code);
	pSelf->m_KoGResultStatusCode = (int)Code;
	curl_easy_cleanup(pCurl);

	pSelf->m_KoGResultData = ResponseJson;
	pSelf->m_KoGFetchSuccess = true;
	pSelf->m_KoGFetchDone = true;
}

void CPlayerProfiles::ProcessKoGFetchCompletion()
{
	if(!m_KoGFetchRunning || !m_KoGFetchDone)
		return;

	if(m_pKoGThread)
	{
		thread_wait(m_pKoGThread);
		m_pKoGThread = nullptr;
	}

	m_KoGFetchRunning = false;
	m_KoGFetchDone = false;

	if(!m_KoGFetchSuccess)
	{
		CKoGPlayerProfile CachedProf{};
		if(LoadKoGFromDisk(m_aKoGRequestedName, CachedProf))
		{
			m_CurrentKoGProfile = CachedProf;
			m_KoGState = EProfileState::READY;
			str_copy(m_aKoGStatusMsg, "Sin conexion: usando datos de cache local");
		}
		else
		{
			m_KoGState = EProfileState::ERROR_NETWORK;
			str_copy(m_aKoGStatusMsg, m_KoGFetchErrorMsg.c_str());
			m_CurrentKoGProfile.m_Valid = false;
		}
		return;
	}

	json_value *pOuter = JsonParse(m_KoGResultData.c_str(), m_KoGResultData.size());
	if(!pOuter)
	{
		m_KoGState = EProfileState::ERROR_NETWORK;
		str_copy(m_aKoGStatusMsg, "Error: respuesta JSON invalida de KoG");
		m_CurrentKoGProfile.m_Valid = false;
		return;
	}

	const json_value &OuterJson = *pOuter;
	bool StatusOk = false;
	if(OuterJson.type == json_object)
	{
		if(OuterJson["status"].type == json_string && str_comp((const char *)OuterJson["status"], "200") == 0)
			StatusOk = true;
		else if(OuterJson["status"].type == json_integer && OuterJson["status"].u.integer == 200)
			StatusOk = true;
	}

	if(!StatusOk || OuterJson["data"].type != json_string)
	{
		m_KoGState = EProfileState::NOT_FOUND;
		str_format(m_aKoGStatusMsg, sizeof(m_aKoGStatusMsg), "Jugador '%s' no encontrado en KoG.tw", m_aKoGRequestedName);
		m_CurrentKoGProfile.m_Valid = false;
		json_value_free(pOuter);
		return;
	}

	const char *pInnerStr = (const char *)OuterJson["data"];

	CKoGPlayerProfile NewProfile{};
	if(!ParseKoGProfileJson(pInnerStr, NewProfile))
	{
		m_KoGState = EProfileState::ERROR_NETWORK;
		str_format(m_aKoGStatusMsg, sizeof(m_aKoGStatusMsg), "Error al procesar datos de '%s' en KoG", m_aKoGRequestedName);
		log_error("kog", "ParseKoGProfileJson fallo para '%s'", m_aKoGRequestedName);
		m_CurrentKoGProfile.m_Valid = false;
		json_value_free(pOuter);
		return;
	}

	// Persist raw inner JSON to disk cache
	SaveKoGToDisk(m_aKoGRequestedName, pInnerStr);

	NewProfile.m_FromDiskCache = false;
	m_CurrentKoGProfile = NewProfile;
	AddToKoGCache(NewProfile);

	m_KoGState = EProfileState::READY;
	str_copy(m_aKoGStatusMsg, "");
	json_value_free(pOuter);
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

void CPlayerProfiles::SaveKoGToDisk(const char *pPlayerName, const char *pJsonData)
{
	if(!pPlayerName || !pPlayerName[0] || !pJsonData || !pJsonData[0])
		return;

	Storage()->CreateFolder("kog_cache", IStorage::TYPE_SAVE);
	Storage()->CreateFolder("kog_cache/players", IStorage::TYPE_SAVE);

	char aSafeName[MAX_NAME_LENGTH];
	str_copy(aSafeName, pPlayerName);
	for(char *p = aSafeName; *p; ++p)
	{
		if(*p == '/' || *p == '\\' || *p == ':' || *p == '*' || *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|')
			*p = '_';
	}

	char aPath[256];
	str_format(aPath, sizeof(aPath), "kog_cache/players/%s.json", aSafeName);

	IOHANDLE File = Storage()->OpenFile(aPath, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	if(File)
	{
		io_write(File, pJsonData, str_length(pJsonData));
		io_close(File);
	}
}

bool CPlayerProfiles::LoadKoGFromDisk(const char *pPlayerName, CKoGPlayerProfile &OutProfile)
{
	if(!pPlayerName || !pPlayerName[0])
		return false;

	char aSafeName[MAX_NAME_LENGTH];
	str_copy(aSafeName, pPlayerName);
	for(char *p = aSafeName; *p; ++p)
	{
		if(*p == '/' || *p == '\\' || *p == ':' || *p == '*' || *p == '?' || *p == '"' || *p == '<' || *p == '>' || *p == '|')
			*p = '_';
	}

	char aPath[256];
	str_format(aPath, sizeof(aPath), "kog_cache/players/%s.json", aSafeName);

	char *pData = Storage()->ReadFileStr(aPath, IStorage::TYPE_SAVE);
	if(!pData)
		return false;

	bool Success = ParseKoGProfileJson(pData, OutProfile);
	free(pData);
	if(Success)
	{
		OutProfile.m_FromDiskCache = true;
	}
	return Success;
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

void CPlayerProfiles::FetchKoGProfile(const char *pPlayerName, bool ForceRefresh)
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

	str_copy(m_aKoGRequestedName, pStart);

	// Check RAM cache first
	if(!ForceRefresh && GetFromKoGCache(pStart, m_CurrentKoGProfile))
	{
		m_KoGState = EProfileState::READY;
		str_copy(m_aKoGStatusMsg, "");
		return;
	}

	// Check disk cache
	if(!ForceRefresh && LoadKoGFromDisk(pStart, m_CurrentKoGProfile))
	{
		AddToKoGCache(m_CurrentKoGProfile);
		m_KoGState = EProfileState::READY;
		str_copy(m_aKoGStatusMsg, "");
		return;
	}

	// Network fetch in background thread
	AbortKoGFetch();

	str_copy(m_aKoGFetchThreadPlayer, pStart);
	m_KoGFetchSuccess = false;
	m_KoGFetchDone = false;
	m_KoGFetchRunning = true;
	m_KoGResultData.clear();
	m_KoGFetchErrorMsg.clear();

	m_KoGState = EProfileState::FETCHING;
	str_format(m_aKoGStatusMsg, sizeof(m_aKoGStatusMsg), "Consultando datos en KoG.tw para '%s'...", pStart);

	m_pKoGThread = thread_init(KoGFetchWorker, this, "KoGFetch");
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
		if(m_mKoGProfilesCache.find(Key) != m_mKoGProfilesCache.end())
			return true;
		CKoGPlayerProfile DiskProf{};
		if(const_cast<CPlayerProfiles *>(this)->LoadKoGFromDisk(pName, DiskProf))
		{
			const_cast<CPlayerProfiles *>(this)->AddToKoGCache(DiskProf);
			return true;
		}
		return false;
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

	std::string NameStr(pName);
	if(IsKoG)
	{
		if(str_comp(m_aKoGFetchThreadPlayer, pName) == 0 && m_KoGFetchRunning)
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
			if(GetFromKoGCache(Name.c_str(), Prof) || LoadKoGFromDisk(Name.c_str(), Prof))
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

void CPlayerProfiles::EnsureKoGMapsMetaLoaded()
{
	if(m_KoGMapsMetaLoaded)
		return;

	IOHANDLE File = Storage()->OpenFile("kog_cache/maps.json", IOFLAG_READ, IStorage::TYPE_SAVE);
	if(!File)
	{
		File = Storage()->OpenFile("data/tclient/kog_maps.json", IOFLAG_READ, IStorage::TYPE_ALL);
	}

	if(!File)
		return;

	int64_t FileSize = io_length(File);
	if(FileSize <= 0 || FileSize > 10 * 1024 * 1024)
	{
		io_close(File);
		return;
	}

	std::string Content;
	Content.resize((size_t)FileSize);
	io_read(File, &Content[0], (unsigned int)FileSize);
	io_close(File);

	json_value *pJson = JsonParse(Content.c_str(), Content.size());
	if(!pJson)
		return;

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
	m_KoGMapsMetaLoaded = true;
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
