#include "aclient_config_sync.h"
#include "aclient_backend.h"

#include <base/str.h>
#include <base/time.h>
#include <engine/graphics.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <game/client/gameclient.h>

#if defined(_WIN32) || defined(CONF_FAMILY_WINDOWS)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

static void TrimWhitespace(const char *pSrc, char *pDst, int DstSize)
{
	if(!pDst || DstSize <= 0)
		return;
	pDst[0] = '\0';
	if(!pSrc)
		return;

	while(*pSrc == ' ' || *pSrc == '\t' || *pSrc == '\r' || *pSrc == '\n')
		++pSrc;

	str_copy(pDst, pSrc, DstSize);
	int Len = str_length(pDst);
	while(Len > 0 && (pDst[Len - 1] == ' ' || pDst[Len - 1] == '\t' || pDst[Len - 1] == '\r' || pDst[Len - 1] == '\n'))
	{
		pDst[Len - 1] = '\0';
		--Len;
	}
}

static std::string NormalizeLowerKey(const char *pSrc)
{
	char aTrimmed[128];
	TrimWhitespace(pSrc, aTrimmed, sizeof(aTrimmed));
	std::string Key(aTrimmed);
	for(char &c : Key)
	{
		if(c >= 'A' && c <= 'Z')
			c = (char)(c - 'A' + 'a');
	}
	return Key;
}

static const char *GetSupabaseBaseUrl()
{
	return (g_Config.m_AcPresenceUrl[0] != '\0' && str_find(g_Config.m_AcPresenceUrl, "workers.dev") == nullptr) ? g_Config.m_AcPresenceUrl : AClientBackend::SUPABASE_URL;
}

static void ConfigureSupabaseUserRequest(const std::shared_ptr<IHttpRequest> &pRequest, const std::string &BearerToken, const CTimeout &Timeout, int64_t MaxResponseBytes = 64 * 1024)
{
	if(!pRequest)
		return;
	pRequest->Timeout(Timeout);
	pRequest->IpResolve(IPRESOLVE::V4);
	pRequest->LogProgress(HTTPLOG::NONE);
	AClientBackend::ApplyDownloadSpeedLimit(pRequest, MaxResponseBytes);
	pRequest->Header(AClientBackend::USER_AGENT_HEADER);
	pRequest->Header(AClientBackend::API_KEY_HEADER);
	std::string AuthHeader = "Authorization: Bearer " + BearerToken;
	pRequest->Header(AuthHeader.c_str());
}

static const char *const gs_pDiscordCallbackHtml =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: text/html; charset=utf-8\r\n"
	"Connection: close\r\n\r\n"
	"<!DOCTYPE html><html lang=\"es\"><head><meta charset=\"UTF-8\">"
	"<title>AClient - Discord</title>"
	"<style>body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;"
	"background:#0b0d14;color:#eef2ff;font-family:Segoe UI,system-ui,sans-serif;}"
	".card{max-width:420px;padding:28px;border-radius:16px;background:#131724;border:1px solid #2b3552;text-align:center;}"
	"h2{margin:0 0 8px;font-size:1.3rem;}p{margin:0;color:#9aa7c4;font-size:.95rem;}</style></head>"
	"<body><div class=\"card\"><h2 id=\"t\">Conectando con AClient...</h2><p id=\"s\">Procesando autorizacion de Discord.</p></div>"
	"<script>(async()=>{"
	"const h=new URLSearchParams((location.hash||'').replace(/^#/,''));"
	"const q=new URLSearchParams((location.search||'').replace(/^\\?/,''));"
	"const tok=h.get('access_token');"
	"const err=h.get('error_description')||h.get('error')||q.get('error_description')||q.get('error');"
	"if(tok){await fetch('/token',{method:'POST',body:tok});"
	"document.getElementById('t').textContent='Discord conectado en AClient';"
	"document.getElementById('s').textContent='Ya podes cerrar esta pestana y volver al juego.';}"
	"else{await fetch('/error',{method:'POST',body:err||'No se recibio token de Discord'});"
	"document.getElementById('t').textContent='No se pudo iniciar sesion';"
	"document.getElementById('s').textContent=err||'Vuelve a intentarlo desde AClient.';}"
	"})();</script></body></html>";

static const char *const gs_pHttpOkPlain =
	"HTTP/1.1 200 OK\r\n"
	"Content-Type: text/plain; charset=utf-8\r\n"
	"Connection: close\r\n\r\nOK";

CAClientConfigSync::CAClientConfigSync() = default;

CAClientConfigSync::~CAClientConfigSync()
{
	StopDiscordListener();
	if(m_pUploadRequest)
	{
		m_pUploadRequest->Abort();
		m_pUploadRequest = nullptr;
	}
	if(m_pLikesFetchRequest)
	{
		m_pLikesFetchRequest->Abort();
		m_pLikesFetchRequest = nullptr;
	}
	if(m_pLikeToggleRequest)
	{
		m_pLikeToggleRequest->Abort();
		m_pLikeToggleRequest = nullptr;
	}
	if(m_pDiscordUserRequest)
	{
		m_pDiscordUserRequest->Abort();
		m_pDiscordUserRequest = nullptr;
	}
	if(m_pDiscordVoterRequest)
	{
		m_pDiscordVoterRequest->Abort();
		m_pDiscordVoterRequest = nullptr;
	}
}

void CAClientConfigSync::OnConsoleInit()
{
	Console()->Register("ac_upload_config", "i[dpi]", CFGFLAG_CLIENT, ConUploadConfig, this, "Guardar configuracion actual en la BD de AClient (solo Discord)");
	Console()->Register("ac_like_gameskin", "s[gameskin]", CFGFLAG_CLIENT, ConLikeGameSkin, this, "Dar o quitar Me gusta a un gameskin con Discord (usa '.' para el actual)");
	Console()->Register("ac_discord_login", "", CFGFLAG_CLIENT, ConDiscordLogin, this, "Iniciar sesion con Discord en AClient");
	Console()->Register("ac_discord_logout", "", CFGFLAG_CLIENT, ConDiscordLogout, this, "Cerrar sesion de Discord en AClient");
}

void CAClientConfigSync::ConUploadConfig(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CAClientConfigSync *>(pUserData);
	pSelf->UploadCurrentConfig(pResult->GetInteger(0));
}

void CAClientConfigSync::ConLikeGameSkin(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CAClientConfigSync *>(pUserData);
	const char *pSkin = pResult->GetString(0);
	if(!pSkin || !pSkin[0] || str_comp(pSkin, ".") == 0 || str_comp_nocase(pSkin, "current") == 0)
		pSkin = g_Config.m_ClAssetGame;
	pSelf->ToggleGameSkinLikeWithDiscord(pSkin);
}

void CAClientConfigSync::ConDiscordLogin(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	auto *pSelf = static_cast<CAClientConfigSync *>(pUserData);
	pSelf->StartDiscordLogin();
}

void CAClientConfigSync::ConDiscordLogout(IConsole::IResult *pResult, void *pUserData)
{
	(void)pResult;
	auto *pSelf = static_cast<CAClientConfigSync *>(pUserData);
	pSelf->LogoutDiscord();
}

void CAClientConfigSync::StopDiscordListener()
{
	m_DiscordStopFlag.store(true);
	if(m_DiscordAuthThread.joinable())
		m_DiscordAuthThread.join();
}

void CAClientConfigSync::RunDiscordLoopbackServer()
{
#if defined(_WIN32) || defined(CONF_FAMILY_WINDOWS)
	SOCKET ListenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(ListenSock == INVALID_SOCKET)
	{
		std::lock_guard<std::mutex> Lock(m_DiscordMutex);
		m_ReceivedOAuthError = "No se pudo abrir socket local para Discord.";
		m_HasOAuthResult = true;
		return;
	}
	int Reuse = 1;
	setsockopt(ListenSock, SOL_SOCKET, SO_REUSEADDR, (const char *)&Reuse, sizeof(Reuse));
#else
	int ListenSock = socket(AF_INET, SOCK_STREAM, 0);
	if(ListenSock < 0)
	{
		std::lock_guard<std::mutex> Lock(m_DiscordMutex);
		m_ReceivedOAuthError = "No se pudo abrir socket local para Discord.";
		m_HasOAuthResult = true;
		return;
	}
	int Reuse = 1;
	setsockopt(ListenSock, SOL_SOCKET, SO_REUSEADDR, &Reuse, sizeof(Reuse));
#endif

	sockaddr_in Addr{};
	Addr.sin_family = AF_INET;
	Addr.sin_port = htons((uint16_t)DISCORD_CALLBACK_PORT);
	Addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if(bind(ListenSock, (sockaddr *)&Addr, sizeof(Addr)) != 0 || listen(ListenSock, 4) != 0)
	{
#if defined(_WIN32) || defined(CONF_FAMILY_WINDOWS)
		closesocket(ListenSock);
#else
		close(ListenSock);
#endif
		std::lock_guard<std::mutex> Lock(m_DiscordMutex);
		m_ReceivedOAuthError = "Puerto local 43821 ocupado. Cierra otra instancia e intenta de nuevo.";
		m_HasOAuthResult = true;
		return;
	}

	const int64_t Deadline = time_get() + time_freq() * 180;
	bool GotFinal = false;

	while(!m_DiscordStopFlag.load() && !GotFinal && time_get() < Deadline)
	{
		fd_set ReadSet;
		FD_ZERO(&ReadSet);
		FD_SET(ListenSock, &ReadSet);
		timeval Tv{};
		Tv.tv_sec = 0;
		Tv.tv_usec = 200000;

		const int Ready = select((int)ListenSock + 1, &ReadSet, nullptr, nullptr, &Tv);
		if(Ready <= 0)
			continue;

#if defined(_WIN32) || defined(CONF_FAMILY_WINDOWS)
		SOCKET ClientSock = accept(ListenSock, nullptr, nullptr);
		if(ClientSock == INVALID_SOCKET)
			continue;
		DWORD RecvTimeoutMs = 2000;
		setsockopt(ClientSock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&RecvTimeoutMs, sizeof(RecvTimeoutMs));
#else
		int ClientSock = accept(ListenSock, nullptr, nullptr);
		if(ClientSock < 0)
			continue;
		timeval RecvTv{};
		RecvTv.tv_sec = 2;
		setsockopt(ClientSock, SOL_SOCKET, SO_RCVTIMEO, &RecvTv, sizeof(RecvTv));
#endif

		std::string Req;
		char aBuf[4096];
		int ContentLength = -1;
		size_t HeaderEnd = std::string::npos;

		while(Req.size() < 32768)
		{
			const int n = recv(ClientSock, aBuf, sizeof(aBuf), 0);
			if(n <= 0)
				break;
			Req.append(aBuf, n);
			if(HeaderEnd == std::string::npos)
			{
				HeaderEnd = Req.find("\r\n\r\n");
				if(HeaderEnd != std::string::npos)
				{
					size_t ClPos = Req.find("Content-Length:");
					if(ClPos == std::string::npos)
						ClPos = Req.find("content-length:");
					if(ClPos != std::string::npos && ClPos < HeaderEnd)
					{
						ContentLength = str_toint(Req.c_str() + ClPos + 15);
					}
					else
					{
						ContentLength = 0;
					}
				}
			}
			if(HeaderEnd != std::string::npos && (int)(Req.size() - (HeaderEnd + 4)) >= ContentLength)
				break;
		}

		if(Req.rfind("GET /callback", 0) == 0)
		{
			send(ClientSock, gs_pDiscordCallbackHtml, str_length(gs_pDiscordCallbackHtml), 0);
		}
		else if(Req.rfind("POST /token", 0) == 0 && HeaderEnd != std::string::npos)
		{
			std::string Body = Req.substr(HeaderEnd + 4);
			while(!Body.empty() && (Body.back() == '\r' || Body.back() == '\n' || Body.back() == ' '))
				Body.pop_back();
			send(ClientSock, gs_pHttpOkPlain, str_length(gs_pHttpOkPlain), 0);
			if(!Body.empty())
			{
				std::lock_guard<std::mutex> Lock(m_DiscordMutex);
				m_ReceivedOAuthToken = Body;
				m_ReceivedOAuthError.clear();
				m_HasOAuthResult = true;
				GotFinal = true;
			}
		}
		else if(Req.rfind("POST /error", 0) == 0 && HeaderEnd != std::string::npos)
		{
			std::string Body = Req.substr(HeaderEnd + 4);
			send(ClientSock, gs_pHttpOkPlain, str_length(gs_pHttpOkPlain), 0);
			std::lock_guard<std::mutex> Lock(m_DiscordMutex);
			m_ReceivedOAuthToken.clear();
			m_ReceivedOAuthError = Body.empty() ? "Autenticacion con Discord cancelada." : Body;
			m_HasOAuthResult = true;
			GotFinal = true;
		}
		else
		{
			send(ClientSock, gs_pHttpOkPlain, str_length(gs_pHttpOkPlain), 0);
		}

#if defined(_WIN32) || defined(CONF_FAMILY_WINDOWS)
		closesocket(ClientSock);
#else
		close(ClientSock);
#endif
	}

#if defined(_WIN32) || defined(CONF_FAMILY_WINDOWS)
	closesocket(ListenSock);
#else
	close(ListenSock);
#endif

	if(!GotFinal && !m_DiscordStopFlag.load())
	{
		std::lock_guard<std::mutex> Lock(m_DiscordMutex);
		m_ReceivedOAuthError = "Tiempo de espera agotado para Discord.";
		m_HasOAuthResult = true;
	}
}

void CAClientConfigSync::StartDiscordLogin(const char *pPendingLikeSkin)
{
	StopDiscordListener();

	if(m_pDiscordUserRequest)
	{
		m_pDiscordUserRequest->Abort();
		m_pDiscordUserRequest = nullptr;
	}
	if(m_pDiscordVoterRequest)
	{
		m_pDiscordVoterRequest->Abort();
		m_pDiscordVoterRequest = nullptr;
	}

	{
		std::lock_guard<std::mutex> Lock(m_DiscordMutex);
		m_ReceivedOAuthToken.clear();
		m_ReceivedOAuthError.clear();
		m_HasOAuthResult = false;
	}

	if(pPendingLikeSkin && pPendingLikeSkin[0] != '\0')
		str_copy(m_aPendingLikeSkin, pPendingLikeSkin, sizeof(m_aPendingLikeSkin));
	else
		m_aPendingLikeSkin[0] = '\0';

	m_DiscordStopFlag.store(false);
	m_DiscordState = DISCORD_WAITING_BROWSER;
	str_copy(m_aDiscordStatusMessage, "Abriendo Discord en tu navegador...", sizeof(m_aDiscordStatusMessage));

	m_DiscordAuthThread = std::thread(&CAClientConfigSync::RunDiscordLoopbackServer, this);

	char aAuthUrl[512];
	str_format(aAuthUrl, sizeof(aAuthUrl),
		"%s/auth/v1/authorize?provider=discord&redirect_to=http%%3A%%2F%%2F127.0.0.1%%3A%d%%2Fcallback",
		GetSupabaseBaseUrl(),
		DISCORD_CALLBACK_PORT);

	Client()->ViewLink(aAuthUrl);
}

void CAClientConfigSync::CancelDiscordLogin()
{
	StopDiscordListener();
	if(m_DiscordState == DISCORD_WAITING_BROWSER || m_DiscordState == DISCORD_VERIFYING)
	{
		m_DiscordState = DISCORD_NONE;
		m_aDiscordStatusMessage[0] = '\0';
		m_aPendingLikeSkin[0] = '\0';
	}
}

void CAClientConfigSync::LogoutDiscord()
{
	StopDiscordListener();
	m_DiscordState = DISCORD_NONE;
	m_DiscordAccessToken.clear();
	m_aDiscordHandle[0] = '\0';
	m_aDiscordUsername[0] = '\0';
	m_aDiscordStatusMessage[0] = '\0';
	m_aPendingLikeSkin[0] = '\0';
	m_SessionLikedGameSkins.clear();
}

void CAClientConfigSync::StartDiscordVerification(const std::string &Token)
{
	m_DiscordAccessToken = Token;
	m_DiscordState = DISCORD_VERIFYING;
	str_copy(m_aDiscordStatusMessage, "Verificando cuenta de Discord...", sizeof(m_aDiscordStatusMessage));

	// 1) Obtener nombre sugerido desde /auth/v1/user
	char aUserUrl[512];
	str_format(aUserUrl, sizeof(aUserUrl), "%s/auth/v1/user", GetSupabaseBaseUrl());
	m_pDiscordUserRequest = HttpGet(aUserUrl);
	ConfigureSupabaseUserRequest(m_pDiscordUserRequest, m_DiscordAccessToken, CTimeout{8000, 8000, 500, 8}, 64 * 1024);
	m_pDiscordUserRequest->FailOnErrorStatus(false);
	Http()->Run(m_pDiscordUserRequest);

	// 2) Verificar / auto-registrar sesion de Discord (sin contrasena)
	char aVoterUrl[512];
	str_format(aVoterUrl, sizeof(aVoterUrl), "%s/rest/v1/rpc/get_my_discord_voter", GetSupabaseBaseUrl());
	m_pDiscordVoterRequest = HttpPostJson(aVoterUrl, "{}");
	ConfigureSupabaseUserRequest(m_pDiscordVoterRequest, m_DiscordAccessToken, CTimeout{8000, 8000, 500, 8}, 64 * 1024);
	m_pDiscordVoterRequest->FailOnErrorStatus(false);
	Http()->Run(m_pDiscordVoterRequest);
}

void CAClientConfigSync::GetCurrentSnapshot(char *pPlayerName, int PlayerNameSize, int &MouseMaxDistance, int &MouseSens, char *pResolution, int ResolutionSize, int &Hz) const
{
	TrimWhitespace(g_Config.m_PlayerName, pPlayerName, PlayerNameSize);
	MouseMaxDistance = g_Config.m_ClMouseMaxDistance;
	MouseSens = g_Config.m_InpMousesens;

	int ScreenWidth = Graphics()->ScreenWidth();
	int ScreenHeight = Graphics()->ScreenHeight();
	if(ScreenWidth <= 0)
		ScreenWidth = g_Config.m_GfxScreenWidth;
	if(ScreenHeight <= 0)
		ScreenHeight = g_Config.m_GfxScreenHeight;

	if(ScreenWidth > 0 && ScreenHeight > 0)
		str_format(pResolution, ResolutionSize, "%dx%d", ScreenWidth, ScreenHeight);
	else
		str_copy(pResolution, "0x0", ResolutionSize);

	CVideoMode CurMode{};
	Graphics()->GetCurrentVideoMode(CurMode, g_Config.m_GfxScreen);
	Hz = CurMode.m_RefreshRate;
	if(Hz <= 0)
		Hz = g_Config.m_GfxScreenRefreshRate;
	if(Hz < 0)
		Hz = 0;
}

void CAClientConfigSync::UploadCurrentConfig(int MouseDpi)
{
	if(m_pUploadRequest)
	{
		m_pUploadRequest->Abort();
		m_pUploadRequest = nullptr;
	}

	if(!HasDiscordSession())
	{
		m_UploadState = UPLOAD_ERROR;
		str_copy(m_aStatusMessage, "Debes iniciar sesion con Discord primero.", sizeof(m_aStatusMessage));
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "aclient_config", m_aStatusMessage);
		return;
	}

	char aPlayerName[MAX_NAME_LENGTH];
	char aResolution[32];
	int MouseMaxDistance = 0;
	int MouseSens = 200;
	int Hz = 0;
	GetCurrentSnapshot(aPlayerName, sizeof(aPlayerName), MouseMaxDistance, MouseSens, aResolution, sizeof(aResolution), Hz);

	if(aPlayerName[0] == '\0')
	{
		m_UploadState = UPLOAD_ERROR;
		str_copy(m_aStatusMessage, "Configura un nombre de jugador principal primero.", sizeof(m_aStatusMessage));
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "aclient_config", m_aStatusMessage);
		return;
	}

	if(MouseDpi < 50 || MouseDpi > 50000)
	{
		m_UploadState = UPLOAD_ERROR;
		str_copy(m_aStatusMessage, "Ingresa el DPI de tu mouse (ej: 800, 1600).", sizeof(m_aStatusMessage));
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "aclient_config", m_aStatusMessage);
		return;
	}

	m_UploadState = UPLOAD_SENDING;
	str_copy(m_aStatusMessage, "Enviando configuracion a la BD...", sizeof(m_aStatusMessage));

	char aEscPlayer[128];
	char aEscResolution[64];
	EscapeJson(aEscPlayer, sizeof(aEscPlayer), aPlayerName);
	EscapeJson(aEscResolution, sizeof(aEscResolution), aResolution);

	char aPayload[512];
	str_format(aPayload, sizeof(aPayload),
		"{\"p_player_name\":\"%s\",\"p_password\":null,\"p_mouse_max_distance\":%d,\"p_mouse_sens\":%d,\"p_mouse_dpi\":%d,\"p_resolution\":\"%s\",\"p_hz\":%d}",
		aEscPlayer, MouseMaxDistance, MouseSens, MouseDpi, aEscResolution, Hz);

	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "%s/rest/v1/rpc/upsert_player_config", GetSupabaseBaseUrl());

	m_pUploadRequest = HttpPostJson(aUrl, aPayload);
	ConfigureSupabaseUserRequest(m_pUploadRequest, m_DiscordAccessToken, CTimeout{8000, 8000, 500, 8}, 32 * 1024);
	m_pUploadRequest->FailOnErrorStatus(false);
	Http()->Run(m_pUploadRequest);
}

void CAClientConfigSync::RefreshGameSkinLikes(bool Force)
{
	if(m_pLikesFetchRequest)
		return;

	const int64_t Now = time_get();
	const int64_t Freq = time_freq();
	if(!Force && m_LastLikesFetchTime != 0 && (Now - m_LastLikesFetchTime) < Freq * 30)
		return;

	m_LastLikesFetchTime = Now;

	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "%s/rest/v1/rpc/get_gameskin_likes", GetSupabaseBaseUrl());

	m_pLikesFetchRequest = HttpPostJson(aUrl, "{}");
	AClientBackend::ConfigureSupabaseRequest(m_pLikesFetchRequest, CTimeout{8000, 8000, 500, 8}, 256 * 1024);
	m_pLikesFetchRequest->FailOnErrorStatus(false);
	Http()->Run(m_pLikesFetchRequest);
}

int CAClientConfigSync::GetGameSkinLikes(const char *pGameSkinName) const
{
	if(!pGameSkinName || !pGameSkinName[0])
		return 0;
	const std::string Key = NormalizeLowerKey(pGameSkinName);
	const auto It = m_GameSkinLikes.find(Key);
	return It != m_GameSkinLikes.end() ? It->second : 0;
}

bool CAClientConfigSync::HasLikedGameSkinInSession(const char *pGameSkinName) const
{
	if(!pGameSkinName || !pGameSkinName[0])
		return false;
	return m_SessionLikedGameSkins.find(NormalizeLowerKey(pGameSkinName)) != m_SessionLikedGameSkins.end();
}

void CAClientConfigSync::ClearLikeStatus()
{
	if(m_LikeState != LIKE_SENDING)
	{
		m_LikeState = LIKE_IDLE;
		m_aLikeStatusMessage[0] = '\0';
	}
}

void CAClientConfigSync::ToggleGameSkinLikeWithDiscord(const char *pGameSkinName)
{
	if(!IsDiscordReady())
	{
		StartDiscordLogin(pGameSkinName);
		return;
	}

	if(m_pLikeToggleRequest)
	{
		m_pLikeToggleRequest->Abort();
		m_pLikeToggleRequest = nullptr;
	}

	char aSkinName[64];
	TrimWhitespace(pGameSkinName, aSkinName, sizeof(aSkinName));
	if(aSkinName[0] == '\0' || str_comp_nocase(aSkinName, "default") == 0)
		return;

	m_LikeState = LIKE_SENDING;
	str_copy(m_aLikeStatusMessage, "Enviando puntuacion con Discord...", sizeof(m_aLikeStatusMessage));

	char aEscSkin[192];
	EscapeJson(aEscSkin, sizeof(aEscSkin), aSkinName);

	char aPayload[256];
	str_format(aPayload, sizeof(aPayload), "{\"p_gameskin_name\":\"%s\"}", aEscSkin);

	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "%s/rest/v1/rpc/toggle_gameskin_like_discord", GetSupabaseBaseUrl());

	m_pLikeToggleRequest = HttpPostJson(aUrl, aPayload);
	ConfigureSupabaseUserRequest(m_pLikeToggleRequest, m_DiscordAccessToken, CTimeout{8000, 8000, 500, 8}, 32 * 1024);
	m_pLikeToggleRequest->FailOnErrorStatus(false);
	Http()->Run(m_pLikeToggleRequest);
}

void CAClientConfigSync::OnRender()
{
	// Revisar si llego el callback de Discord del thread loopback
	if(m_DiscordState == DISCORD_WAITING_BROWSER)
	{
		bool HasRes = false;
		std::string Token;
		std::string Err;
		{
			std::lock_guard<std::mutex> Lock(m_DiscordMutex);
			if(m_HasOAuthResult)
			{
				HasRes = true;
				Token = m_ReceivedOAuthToken;
				Err = m_ReceivedOAuthError;
				m_HasOAuthResult = false;
			}
		}

		if(HasRes)
		{
			StopDiscordListener();
			if(!Token.empty())
			{
				StartDiscordVerification(Token);
			}
			else
			{
				m_DiscordState = DISCORD_ERROR;
				str_copy(m_aDiscordStatusMessage, Err.empty() ? "Error al autenticar con Discord." : Err.c_str(), sizeof(m_aDiscordStatusMessage));
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "aclient_discord", m_aDiscordStatusMessage);
			}
		}
	}

	// Procesar /auth/v1/user para nombre sugerido de Discord
	if(m_pDiscordUserRequest && m_pDiscordUserRequest->Done())
	{
		if(m_pDiscordUserRequest->State() == EHttpState::DONE && m_pDiscordUserRequest->StatusCode() >= 200 && m_pDiscordUserRequest->StatusCode() < 300)
		{
			json_value *pJson = m_pDiscordUserRequest->ResultJson();
			if(pJson && pJson->type == json_object)
			{
				const json_value *pMeta = json_object_get(pJson, "user_metadata");
				if(pMeta && pMeta->type == json_object)
				{
					const char *pHandle = nullptr;
					const json_value *pFullName = json_object_get(pMeta, "full_name");
					const json_value *pName = json_object_get(pMeta, "name");
					const json_value *pUserName = json_object_get(pMeta, "user_name");
					if(pFullName && pFullName->type == json_string && json_string_get(pFullName)[0] != '\0')
						pHandle = json_string_get(pFullName);
					else if(pName && pName->type == json_string && json_string_get(pName)[0] != '\0')
						pHandle = json_string_get(pName);
					else if(pUserName && pUserName->type == json_string && json_string_get(pUserName)[0] != '\0')
						pHandle = json_string_get(pUserName);

					if(pHandle)
						str_copy(m_aDiscordHandle, pHandle, sizeof(m_aDiscordHandle));
				}
				json_value_free(pJson);
			}
		}
		m_pDiscordUserRequest = nullptr;
	}

	// Procesar get_my_discord_voter
	if(m_pDiscordVoterRequest && m_pDiscordVoterRequest->Done())
	{
		if(m_pDiscordVoterRequest->State() == EHttpState::DONE)
		{
			const int StatusCode = m_pDiscordVoterRequest->StatusCode();
			json_value *pJson = m_pDiscordVoterRequest->ResultJson();
			if(pJson && pJson->type == json_object)
			{
				const json_value *pOk = json_object_get(pJson, "ok");
				const bool IsOk = (StatusCode >= 200 && StatusCode < 300) && pOk && pOk->type == json_boolean && json_boolean_get(pOk) != 0;
				if(IsOk)
				{
					m_DiscordState = DISCORD_READY;
					const json_value *pUser = json_object_get(pJson, "username");
					if(pUser && pUser->type == json_string && json_string_get(pUser)[0] != '\0')
						str_copy(m_aDiscordUsername, json_string_get(pUser), sizeof(m_aDiscordUsername));
					else if(m_aDiscordHandle[0] != '\0')
						str_copy(m_aDiscordUsername, m_aDiscordHandle, sizeof(m_aDiscordUsername));
					else
						str_copy(m_aDiscordUsername, "Discord", sizeof(m_aDiscordUsername));
					str_format(m_aDiscordStatusMessage, sizeof(m_aDiscordStatusMessage), "Discord conectado: %s", m_aDiscordUsername);

					if(m_aPendingLikeSkin[0] != '\0')
					{
						char aSkin[64];
						str_copy(aSkin, m_aPendingLikeSkin, sizeof(aSkin));
						m_aPendingLikeSkin[0] = '\0';
						ToggleGameSkinLikeWithDiscord(aSkin);
					}
				}
				else
				{
					m_DiscordState = DISCORD_ERROR;
					const json_value *pErr = json_object_get(pJson, "error");
					const json_value *pMsg = json_object_get(pJson, "message");
					if(pErr && pErr->type == json_string)
						str_copy(m_aDiscordStatusMessage, json_string_get(pErr), sizeof(m_aDiscordStatusMessage));
					else if(pMsg && pMsg->type == json_string)
						str_copy(m_aDiscordStatusMessage, json_string_get(pMsg), sizeof(m_aDiscordStatusMessage));
					else
						str_format(m_aDiscordStatusMessage, sizeof(m_aDiscordStatusMessage), "Error verificando Discord (HTTP %d).", StatusCode);
				}
				json_value_free(pJson);
			}
			else
			{
				m_DiscordState = DISCORD_ERROR;
				str_format(m_aDiscordStatusMessage, sizeof(m_aDiscordStatusMessage), "Respuesta invalida de Discord (HTTP %d).", StatusCode);
			}
		}
		else
		{
			m_DiscordState = DISCORD_ERROR;
			str_copy(m_aDiscordStatusMessage, "No se pudo conectar con Supabase.", sizeof(m_aDiscordStatusMessage));
		}
		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "aclient_discord", m_aDiscordStatusMessage);
		m_pDiscordVoterRequest = nullptr;
	}

	if(m_pUploadRequest && m_pUploadRequest->Done())
	{
		if(m_pUploadRequest->State() == EHttpState::DONE)
		{
			const int StatusCode = m_pUploadRequest->StatusCode();
			json_value *pJson = m_pUploadRequest->ResultJson();
			if(pJson && pJson->type == json_object)
			{
				const json_value *pOk = json_object_get(pJson, "ok");
				const bool IsOk = (StatusCode >= 200 && StatusCode < 300) && pOk && pOk->type == json_boolean && json_boolean_get(pOk) != 0;
				if(IsOk)
				{
					m_UploadState = UPLOAD_SUCCESS;
					const json_value *pMsg = json_object_get(pJson, "message");
					if(pMsg && pMsg->type == json_string && json_string_get(pMsg)[0] != '\0')
						str_copy(m_aStatusMessage, json_string_get(pMsg), sizeof(m_aStatusMessage));
					else
						str_copy(m_aStatusMessage, "Configuracion guardada correctamente.", sizeof(m_aStatusMessage));
				}
				else
				{
					m_UploadState = UPLOAD_ERROR;
					const json_value *pErr = json_object_get(pJson, "error");
					const json_value *pMsg = json_object_get(pJson, "message");
					if(pErr && pErr->type == json_string && json_string_get(pErr)[0] != '\0')
						str_copy(m_aStatusMessage, json_string_get(pErr), sizeof(m_aStatusMessage));
					else if(pMsg && pMsg->type == json_string && json_string_get(pMsg)[0] != '\0')
						str_copy(m_aStatusMessage, json_string_get(pMsg), sizeof(m_aStatusMessage));
					else
						str_format(m_aStatusMessage, sizeof(m_aStatusMessage), "Error del servidor (HTTP %d).", StatusCode);
				}
				json_value_free(pJson);
			}
			else
			{
				m_UploadState = UPLOAD_ERROR;
				str_format(m_aStatusMessage, sizeof(m_aStatusMessage), "Respuesta invalida del servidor (HTTP %d).", StatusCode);
			}
		}
		else
		{
			m_UploadState = UPLOAD_ERROR;
			str_copy(m_aStatusMessage, "No se pudo conectar con Supabase.", sizeof(m_aStatusMessage));
		}

		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "aclient_config", m_aStatusMessage);
		m_pUploadRequest = nullptr;
	}

	if(m_pLikesFetchRequest && m_pLikesFetchRequest->Done())
	{
		if(m_pLikesFetchRequest->State() == EHttpState::DONE && m_pLikesFetchRequest->StatusCode() >= 200 && m_pLikesFetchRequest->StatusCode() < 300)
		{
			json_value *pJson = m_pLikesFetchRequest->ResultJson();
			if(pJson && pJson->type == json_array)
			{
				std::unordered_map<std::string, int> NextLikes;
				const int Len = json_array_length(pJson);
				for(int i = 0; i < Len; ++i)
				{
					const json_value *pRow = json_array_get(pJson, i);
					if(!pRow || pRow->type != json_object)
						continue;
					const json_value *pKey = json_object_get(pRow, "gameskin_key");
					const json_value *pName = json_object_get(pRow, "gameskin_name");
					const json_value *pCount = json_object_get(pRow, "likes_count");
					if(!pCount || pCount->type != json_integer)
						continue;
					const char *pKeyStr = (pKey && pKey->type == json_string) ? json_string_get(pKey) : ((pName && pName->type == json_string) ? json_string_get(pName) : "");
					if(pKeyStr && pKeyStr[0] != '\0')
					{
						NextLikes[NormalizeLowerKey(pKeyStr)] = json_int_get(pCount);
					}
				}
				m_GameSkinLikes = std::move(NextLikes);
			}
			if(pJson)
				json_value_free(pJson);
		}
		m_pLikesFetchRequest = nullptr;
	}

	if(m_pLikeToggleRequest && m_pLikeToggleRequest->Done())
	{
		if(m_pLikeToggleRequest->State() == EHttpState::DONE)
		{
			const int StatusCode = m_pLikeToggleRequest->StatusCode();
			json_value *pJson = m_pLikeToggleRequest->ResultJson();
			if(pJson && pJson->type == json_object)
			{
				const json_value *pOk = json_object_get(pJson, "ok");
				const bool IsOk = (StatusCode >= 200 && StatusCode < 300) && pOk && pOk->type == json_boolean && json_boolean_get(pOk) != 0;
				if(IsOk)
				{
					m_LikeState = LIKE_SUCCESS;
					const json_value *pKey = json_object_get(pJson, "gameskin_key");
					const json_value *pCount = json_object_get(pJson, "likes_count");
					const json_value *pLiked = json_object_get(pJson, "liked");
					const json_value *pMsg = json_object_get(pJson, "message");

					if(pKey && pKey->type == json_string)
					{
						const std::string NormKey = NormalizeLowerKey(json_string_get(pKey));
						if(pCount && pCount->type == json_integer)
							m_GameSkinLikes[NormKey] = json_int_get(pCount);
						if(pLiked && pLiked->type == json_boolean)
						{
							if(json_boolean_get(pLiked) != 0)
								m_SessionLikedGameSkins.insert(NormKey);
							else
								m_SessionLikedGameSkins.erase(NormKey);
						}
					}

					if(pMsg && pMsg->type == json_string && json_string_get(pMsg)[0] != '\0')
						str_copy(m_aLikeStatusMessage, json_string_get(pMsg), sizeof(m_aLikeStatusMessage));
					else
						str_copy(m_aLikeStatusMessage, "Puntuacion actualizada.", sizeof(m_aLikeStatusMessage));
				}
				else
				{
					m_LikeState = LIKE_ERROR;
					const json_value *pErr = json_object_get(pJson, "error");
					const json_value *pMsg = json_object_get(pJson, "message");
					if(pErr && pErr->type == json_string && json_string_get(pErr)[0] != '\0')
						str_copy(m_aLikeStatusMessage, json_string_get(pErr), sizeof(m_aLikeStatusMessage));
					else if(pMsg && pMsg->type == json_string && json_string_get(pMsg)[0] != '\0')
						str_copy(m_aLikeStatusMessage, json_string_get(pMsg), sizeof(m_aLikeStatusMessage));
					else
						str_format(m_aLikeStatusMessage, sizeof(m_aLikeStatusMessage), "Error del servidor (HTTP %d).", StatusCode);
				}
				json_value_free(pJson);
			}
			else
			{
				m_LikeState = LIKE_ERROR;
				str_format(m_aLikeStatusMessage, sizeof(m_aLikeStatusMessage), "Respuesta invalida del servidor (HTTP %d).", StatusCode);
			}
		}
		else
		{
			m_LikeState = LIKE_ERROR;
			str_copy(m_aLikeStatusMessage, "No se pudo conectar con Supabase.", sizeof(m_aLikeStatusMessage));
		}

		Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "aclient_gameskin_like", m_aLikeStatusMessage);
		m_pLikeToggleRequest = nullptr;
	}
}
