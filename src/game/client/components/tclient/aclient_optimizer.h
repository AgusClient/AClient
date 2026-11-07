#ifndef GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_OPTIMIZER_H
#define GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_OPTIMIZER_H

#include <engine/console.h>
#include <game/client/component.h>

class CAcOptimizer : public CComponent
{
	bool m_LastHighPriority = false;

public:
	int Sizeof() const override { return sizeof(*this); }
	void OnInit() override;
	void OnRender() override;
	void OnConsoleInit() override;

	static void ConToggleOptimizer(IConsole::IResult *pResult, void *pUserData);
};

#endif // GAME_CLIENT_COMPONENTS_TCLIENT_ACLIENT_OPTIMIZER_H
