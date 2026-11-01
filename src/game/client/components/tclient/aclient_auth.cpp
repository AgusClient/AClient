#include "aclient_auth.h"
#include "aclient_backend.h"

#include <base/hash.h>
#include <base/str.h>
#include <engine/shared/config.h>
#include <engine/shared/json.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>

#if defined(CONF_FAMILY_WINDOWS)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

CAClientAuth::CAClientAuth()
{
	m_AuthState = AUTH_NONE;
	m_aErrorMessage[0] = '\0';
	m_aHWID[0] = '\0';
	m_aCurrentUsername[0] = '\0';
	m_Remember = true;
	m_InitialCheckDone = false;
	m_InitialFocusSet = false;

}

CAClientAuth::~CAClientAuth()
{
	if(m_pAuthRequest)
	{
		m_pAuthRequest->Abort();
		m_pAuthRequest = nullptr;
	}
}

void CAClientAuth::GenerateHWID(char *pOutHWID, int MaxSize)
{
	if(!pOutHWID || MaxSize < 65)
		return;

#if defined(CONF_FAMILY_WINDOWS)
	DWORD VolumeSerial = 0;
	GetVolumeInformationW(L"C:\\", NULL, 0, &VolumeSerial, NULL, NULL, NULL, 0);

	wchar_t aCompName[MAX_COMPUTERNAME_LENGTH + 1] = {0};
	DWORD CompSize = sizeof(aCompName) / sizeof(aCompName[0]);
	GetComputerNameW(aCompName, &CompSize);

	wchar_t aUserName[256] = {0};
	DWORD UserSize = sizeof(aUserName) / sizeof(aUserName[0]);
	GetUserNameW(aUserName, &UserSize);

	char aRawHWID[512];
	str_format(aRawHWID, sizeof(aRawHWID), "%ls-%ls-%lu-AClient-Salt-2026", aCompName, aUserName, VolumeSerial);
	SHA256_DIGEST Digest = sha256(aRawHWID, str_length(aRawHWID));
	sha256_str(Digest, pOutHWID, MaxSize);
#else
	SHA256_DIGEST Digest = sha256("AClient-NonWindows-Fallback-2026", 32);
	sha256_str(Digest, pOutHWID, MaxSize);
#endif
}

void CAClientAuth::OnInit()
{
	GenerateHWID(m_aHWID, sizeof(m_aHWID));
	str_copy(g_Config.m_ClAuthHwid, m_aHWID, sizeof(g_Config.m_ClAuthHwid));

	m_Remember = (g_Config.m_ClAuthRemember != 0);

	if(g_Config.m_ClAuthUser[0] != '\0')
	{
		m_UsernameInput.Set(g_Config.m_ClAuthUser);
	}
}

void CAClientAuth::OnReset()
{
	if(m_pAuthRequest)
	{
		m_pAuthRequest->Abort();
		m_pAuthRequest = nullptr;
	}
}

void CAClientAuth::OnConsoleInit()
{
	Console()->Register("aclient_login", "s[user] ?s[pass]", CFGFLAG_CLIENT, ConAClientLogin, this, "Iniciar sesion en AClient");
	Console()->Register("aclient_logout", "", CFGFLAG_CLIENT, ConAClientLogout, this, "Cerrar sesion en AClient");
	Console()->Register("aclient_status", "", CFGFLAG_CLIENT, ConAClientStatus, this, "Ver estado de autenticacion de AClient");
}

void CAClientAuth::ConAClientLogin(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CAClientAuth *>(pUserData);
	pSelf->StartLogin(pResult->GetString(0), pResult->GetString(1));
}

void CAClientAuth::ConAClientLogout(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CAClientAuth *>(pUserData);
	pSelf->Logout();
}

void CAClientAuth::ConAClientStatus(IConsole::IResult *pResult, void *pUserData)
{
	auto *pSelf = static_cast<CAClientAuth *>(pUserData);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "AClient Auth Status: %s | User: '%s' | HWID: %.16s...",
		pSelf->IsAuthenticated() ? "LOGGED IN" : (pSelf->State() == AUTH_CHECKING ? "CHECKING" : "NOT LOGGED IN"),
		pSelf->GetUsername(),
		pSelf->GetHWID());
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "aclient_auth", aBuf);
}

void CAClientAuth::StartLogin(const char *pUser, const char *pPass)
{
	if(m_pAuthRequest)
	{
		m_pAuthRequest->Abort();
		m_pAuthRequest = nullptr;
	}

	if(!pUser || !pUser[0])
	{
		m_AuthState = AUTH_ERROR;
		str_copy(m_aErrorMessage, "Ingresa tu usuario", sizeof(m_aErrorMessage));
		return;
	}

	m_AuthState = AUTH_CHECKING;
	m_aErrorMessage[0] = '\0';
	str_copy(m_aCurrentUsername, pUser, sizeof(m_aCurrentUsername));

	char aEscUser[128];
	char aEscHwid[128];
	EscapeJson(aEscUser, sizeof(aEscUser), pUser);
	EscapeJson(aEscHwid, sizeof(aEscHwid), m_aHWID);

	char aPayload[512];
	str_format(aPayload, sizeof(aPayload),
		"{\"p_user\":\"%s\",\"p_hwid\":\"%s\"}",
		aEscUser, aEscHwid);

	const char *pBaseUrl = (g_Config.m_AcPresenceUrl[0] != '\0' && str_find(g_Config.m_AcPresenceUrl, "workers.dev") == nullptr) ? g_Config.m_AcPresenceUrl : AClientBackend::SUPABASE_URL;

	char aUrl[512];
	str_format(aUrl, sizeof(aUrl), "%s/rest/v1/rpc/aclient_login", pBaseUrl);

	m_pAuthRequest = HttpPostJson(aUrl, aPayload);
	m_pAuthRequest->Timeout(CTimeout{6000, 6000, 500, 6});
	m_pAuthRequest->IpResolve(IPRESOLVE::V4);
	m_pAuthRequest->FailOnErrorStatus(false);
	m_pAuthRequest->Header(AClientBackend::USER_AGENT_HEADER);
	m_pAuthRequest->Header(AClientBackend::API_KEY_HEADER);
	m_pAuthRequest->Header(AClientBackend::AUTH_BEARER_HEADER);
	Http()->Run(m_pAuthRequest);
}

void CAClientAuth::Logout()
{
	m_AuthState = AUTH_NONE;
	m_aCurrentUsername[0] = '\0';
	m_aErrorMessage[0] = '\0';
	g_Config.m_ClAuthUser[0] = '\0';
	g_Config.m_ClAuthPass[0] = '\0';
	g_Config.m_ClAuthRemember = 0;
	m_InitialFocusSet = false;
	m_UsernameInput.Clear();
}

void CAClientAuth::OnRender()
{
	// Auto-login silencioso al iniciar
	if(!m_InitialCheckDone)
	{
		m_InitialCheckDone = true;
		if(g_Config.m_ClAuthRemember && g_Config.m_ClAuthUser[0] != '\0' && true)
		{
			StartLogin(g_Config.m_ClAuthUser, g_Config.m_ClAuthPass);
		}
	}

	// Procesar respuesta asincrona de autenticacion
	if(m_pAuthRequest && m_pAuthRequest->Done())
	{
		if(m_pAuthRequest->State() == EHttpState::DONE)
		{
			json_value *pJson = m_pAuthRequest->ResultJson();
			if(pJson && pJson->type == json_object)
			{
				const json_value *pStatus = json_object_get(pJson, "status");
				if(m_pAuthRequest->StatusCode() == 200 && pStatus && pStatus->type == json_string && str_comp(json_string_get(pStatus), "ok") == 0)
				{
					m_AuthState = AUTH_LOGGED_IN;
					m_aErrorMessage[0] = '\0';

					if(m_Remember)
					{
						str_copy(g_Config.m_ClAuthUser, m_UsernameInput.GetString(), sizeof(g_Config.m_ClAuthUser));
						
						g_Config.m_ClAuthRemember = 1;
					}
					else
					{
						g_Config.m_ClAuthUser[0] = '\0';
						g_Config.m_ClAuthPass[0] = '\0';
						g_Config.m_ClAuthRemember = 0;
					}
				}
				else
				{
					m_AuthState = AUTH_ERROR;
					const json_value *pMsg = json_object_get(pJson, "message");
					if(pMsg && pMsg->type == json_string)
						str_copy(m_aErrorMessage, json_string_get(pMsg), sizeof(m_aErrorMessage));
					else
						str_copy(m_aErrorMessage, "Error de autenticacion", sizeof(m_aErrorMessage));
				}
				json_value_free(pJson);
			}
			else
			{
				m_AuthState = AUTH_ERROR;
				str_copy(m_aErrorMessage, "Respuesta invalida del servidor", sizeof(m_aErrorMessage));
			}
		}
		else
		{
			m_AuthState = AUTH_ERROR;
			str_copy(m_aErrorMessage, "Error al conectar con el servidor", sizeof(m_aErrorMessage));
		}

		m_pAuthRequest = nullptr;
	}

}

void CAClientAuth::RenderAuthModal(CUIRect Screen)
{
	// Fondo solido y completamente opaco para cubrir toda la pantalla y bloquear los menus de atras
	Screen.Draw(ColorRGBA(0.06f, 0.07f, 0.10f, 1.0f), IGraphics::CORNER_NONE, 0.0f);

	// Botones estaticos declarados para gestion de focus
	static CButtonContainer s_RememberBtn;
	static CButtonContainer s_LoginBtn;
	static CButtonContainer s_QuitBtn;

	// Auto-enfoque inicial una sola vez al aparecer la ventana
	if(!m_InitialFocusSet)
	{
		m_InitialFocusSet = true;
		Ui()->SetActiveItem(&m_UsernameInput);
		m_UsernameInput.SelectAll();
	}

	// Ventana modal centrada
	const float ModalWidth = 440.0f;
	const float ModalHeight = 240.0f;
	CUIRect Box;
	Box.x = Screen.x + (Screen.w - ModalWidth) / 2.0f;
	Box.y = Screen.y + (Screen.h - ModalHeight) / 2.0f;
	Box.w = ModalWidth;
	Box.h = ModalHeight;

	Box.Draw(ColorRGBA(0.12f, 0.14f, 0.19f, 1.0f), IGraphics::CORNER_ALL, 12.0f);

	Box.Margin(20.0f, &Box);

	// Titulo y Subtitulo
	CUIRect Title, Subtitle, UserLabel, UserInput, RememberBox, StatusBox, ButtonsRow;
	Box.HSplitTop(26.0f, &Title, &Box);
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	TextRender()->Text(Title.x, Title.y, 18.0f, "Autenticacion de AClient", -1.0f);

	Box.HSplitTop(18.0f, &Subtitle, &Box);
	TextRender()->TextColor(0.6f, 0.65f, 0.75f, 1.0f);
	TextRender()->Text(Subtitle.x, Subtitle.y, 11.0f, "Ingresa tu usuario para desbloquear el cliente", -1.0f);

	Box.HSplitTop(12.0f, nullptr, &Box);

	// Campo: Usuario
	Box.HSplitTop(15.0f, &UserLabel, &Box);
	TextRender()->TextColor(0.85f, 0.85f, 0.9f, 1.0f);
	TextRender()->Text(UserLabel.x, UserLabel.y, 11.5f, "Usuario:", -1.0f);

	Box.HSplitTop(24.0f, &UserInput, &Box);
	Ui()->DoEditBox(&m_UsernameInput, &UserInput, 12.0f);

	Box.HSplitTop(12.0f, nullptr, &Box);

	// Checkbox Recordar sesion
	Box.HSplitTop(18.0f, &RememberBox, &Box);
	if(GameClient()->m_Menus.DoButton_CheckBox(&s_RememberBtn, "Recordar usuario en esta PC", m_Remember, &RememberBox))
	{
		m_Remember = !m_Remember;
	}

	// Mensaje de estado o error
	Box.HSplitTop(22.0f, &StatusBox, &Box);
	if(m_AuthState == AUTH_CHECKING)
	{
		TextRender()->TextColor(0.95f, 0.85f, 0.2f, 1.0f);
		TextRender()->Text(StatusBox.x, StatusBox.y + 4.0f, 11.0f, "Verificando sesion...", -1.0f);
	}
	else if(m_AuthState == AUTH_ERROR && m_aErrorMessage[0] != '\0')
	{
		TextRender()->TextColor(0.95f, 0.3f, 0.3f, 1.0f);
		TextRender()->Text(StatusBox.x, StatusBox.y + 4.0f, 11.0f, m_aErrorMessage, -1.0f);
	}

	// Botones: [ Iniciar Sesion ] y [ Salir ]
	Box.HSplitBottom(28.0f, &Box, &ButtonsRow);
	CUIRect LoginBtnRect, QuitBtnRect;
	ButtonsRow.VSplitMid(&LoginBtnRect, &QuitBtnRect, 15.0f);

	bool Submit = GameClient()->m_Menus.DoButton_Menu(&s_LoginBtn, "Iniciar Sesion", 0, &LoginBtnRect);
	if(!Submit && Ui()->ConsumeHotkey(CUi::HOTKEY_ENTER))
	{
		if(m_AuthState != AUTH_CHECKING)
		{
			Submit = true;
		}
	}

	if(Submit && m_AuthState != AUTH_CHECKING)
	{
		StartLogin(m_UsernameInput.GetString(), "");
	}

	if(GameClient()->m_Menus.DoButton_Menu(&s_QuitBtn, "Salir", 0, &QuitBtnRect))
	{
		Client()->Quit();
	}

	// Restaurar color de texto
	TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
}
