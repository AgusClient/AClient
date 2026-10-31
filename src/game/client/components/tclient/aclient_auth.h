#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_AUTH_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_AUTH_H

#include <engine/console.h>
#include <engine/http.h>
#include <game/client/component.h>
#include <game/client/lineinput.h>
#include <game/client/ui.h>

#include <memory>
#include <string>

class CAClientAuth : public CComponent
{
public:
	enum EAuthState
	{
		AUTH_NONE = 0,
		AUTH_CHECKING,
		AUTH_LOGGED_IN,
		AUTH_ERROR
	};

private:
	EAuthState m_AuthState;
	char m_aErrorMessage[256];
	char m_aHWID[65];

	char m_aCurrentUsername[64];
	bool m_Remember;
	bool m_InitialCheckDone;
	bool m_InitialFocusSet;

	std::shared_ptr<IHttpRequest> m_pAuthRequest;

	CLineInputBuffered<64> m_UsernameInput;
	CLineInputBuffered<64> m_PasswordInput;

	void GenerateHWID(char *pOutHWID, int MaxSize);

	static void ConAClientLogin(IConsole::IResult *pResult, void *pUserData);
	static void ConAClientLogout(IConsole::IResult *pResult, void *pUserData);
	static void ConAClientStatus(IConsole::IResult *pResult, void *pUserData);

public:
	CAClientAuth();
	~CAClientAuth() override;

	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnRender() override;
	void OnConsoleInit() override;
	void OnReset() override;

	void StartLogin(const char *pUser, const char *pPass);
	void Logout();

	bool IsAuthenticated() const { return m_AuthState == AUTH_LOGGED_IN; }
	EAuthState State() const { return m_AuthState; }
	const char *GetHWID() const { return m_aHWID; }
	const char *GetUsername() const { return m_aCurrentUsername; }
	const char *GetErrorMessage() const { return m_aErrorMessage; }

	void RenderAuthModal(CUIRect Screen);
};

#endif // GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_AUTH_H
