#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_BACKEND_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_BACKEND_H

#include <engine/http.h>
#include <engine/shared/config.h>

#include <cstdint>
#include <memory>

namespace AClientBackend
{
	constexpr const char *SUPABASE_URL = "https://hmrgihzjynnnappzbvdd.supabase.co";
	constexpr const char *SUPABASE_KEY = "sb_publishable_pvDz7mWQ0e2qD4Q9tkyNKw_KEHQx8L6";
	constexpr const char *API_KEY_HEADER = "apikey: sb_publishable_pvDz7mWQ0e2qD4Q9tkyNKw_KEHQx8L6";
	constexpr const char *AUTH_BEARER_HEADER = "Authorization: Bearer sb_publishable_pvDz7mWQ0e2qD4Q9tkyNKw_KEHQx8L6";
	constexpr const char *USER_AGENT_HEADER = "User-Agent: AClient";

	inline void ApplyDownloadSpeedLimit(const std::shared_ptr<IHttpRequest> &pRequest, int64_t MaxResponseBytes = 0)
	{
		if(!pRequest)
			return;
		if(MaxResponseBytes > 0)
			pRequest->MaxResponseSize(MaxResponseBytes);
		if(g_Config.m_AcDownloadSpeedLimitKbps > 0)
			pRequest->MaxDownloadSpeed((int64_t)g_Config.m_AcDownloadSpeedLimitKbps * 1024);
	}

	inline void ConfigureSupabaseRequest(const std::shared_ptr<IHttpRequest> &pRequest, const CTimeout &Timeout, int64_t MaxResponseBytes = 128 * 1024)
	{
		if(!pRequest)
			return;
		pRequest->Timeout(Timeout);
		pRequest->IpResolve(IPRESOLVE::V4);
		pRequest->LogProgress(HTTPLOG::NONE);
		ApplyDownloadSpeedLimit(pRequest, MaxResponseBytes);
		pRequest->Header(USER_AGENT_HEADER);
		pRequest->Header(API_KEY_HEADER);
		pRequest->Header(AUTH_BEARER_HEADER);
	}
}

#endif // GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_BACKEND_H
